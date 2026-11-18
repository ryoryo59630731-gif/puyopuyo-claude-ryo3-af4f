#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sasari
{

namespace
{
    constexpr float kSilenceDb = -70.0f;
    constexpr float kLongTauSec = 10.0f;
    constexpr float kRmsTauSec = 4.0f;
    constexpr float kMaxDeviationDb = 18.0f;
    constexpr float kEnvStartDb = -30.0f; // detector starts well below any threshold
    constexpr float kSideModelOffsetDb = -12.0f; // side content is usually much quieter than L/R
    constexpr double kPi = 3.14159265358979323846;
    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }
    inline float toDb (float power) { return 10.0f * std::log10 (power + 1e-12f); }

    void makeWindow (std::vector<float>& w, int size)
    {
        w.resize ((size_t) size);
        for (int i = 0; i < size; ++i) // periodic sqrt-Hann: analysis x synthesis = Hann, sums to 2 at 75 % overlap
            w[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * i / size));
    }
} // namespace

Engine::Engine()
{
    for (auto& v : meters.overDb)
        v.store (-100.0f); // "no signal" until audio has been processed
    for (auto& v : meters.owner)
        v.store (-1);
    for (auto& v : meters.specIn)
        v.store (-120.0f);
    for (auto& v : meters.specOut)
        v.store (-120.0f);
    for (auto& d : profileDb)
        for (auto& v : d)
            v.store (kNaN);
}

void Engine::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    numCh = std::clamp (numChannels, 1, 2);
    nSmall = sr <= 50000.0 ? 512 : (sr <= 100000.0 ? 1024 : 2048); // ~10.7 ms whatever the rate
    nLarge = nSmall * 2;

    fftSmall.prepare (nSmall);
    fftLarge.prepare (nLarge);
    makeWindow (windowSmall, nSmall);
    makeWindow (windowLarge, nLarge);

    const auto maxN = (size_t) nLarge;
    for (int c = 0; c < 2; ++c)
    {
        inBuf[c].assign (maxN, 0.0f);
        outBuf[c].assign (maxN, 0.0f);
        re[c].assign (maxN, 0.0f);
        im[c].assign (maxN, 0.0f);
    }
    const auto maxBins = maxN / 2 + 1;
    for (auto* v : { &binHz, &binT, &binShelf, &binSolo, &binGainM, &binGainS })
        v->assign (maxBins, 0.0f);
    binSeg.assign (maxBins, -1);

    configure (settings.extendedRange);
}

void Engine::configure (bool extended)
{
    configuredExtended = extended;
    n = extended ? nLarge : nSmall;
    fft = extended ? &fftLarge : &fftSmall;
    window = extended ? &windowLarge : &windowSmall;
    hop = n / 4;
    half = n / 2;
    hopSec = (float) (hop / sr);
    for (int k = 0; k <= half; ++k)
        binHz[(size_t) k] = (float) (k * sr / n);

    auto binRange = [this] (float f0, float f1, int& lo, int& hi) {
        lo = (int) std::ceil (f0 * n / sr);
        hi = (int) std::floor (f1 * n / sr);
        if (hi < lo)
            lo = hi = (int) std::lround (std::sqrt (f0 * f1) * n / sr);
        lo = std::clamp (lo, 1, half - 1);
        hi = std::clamp (hi, 1, half - 1);
    };
    for (int i = 0; i < kSpecPoints; ++i)
        binRange (kSpecLowHz * std::pow (kSpecHighHz / kSpecLowHz, (float) i / kSpecPoints),
                  kSpecLowHz * std::pow (kSpecHighHz / kSpecLowHz, (float) (i + 1) / kSpecPoints),
                  specLo[(size_t) i], specHi[(size_t) i]);
    for (int k = 0; k < kProfileBands; ++k)
        binRange (profileEdgeHz (k), profileEdgeHz (k + 1), profBinLo[(size_t) k], profBinHi[(size_t) k]);

    layoutPerOctave = 0; // force rebuild
    rebuildLayout();
    plan = makePlan (settings, layout);
    rebuildBinMaps();
    reset();
}

void Engine::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
        std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
    }
    pos = 0;
    for (int d = 0; d < kNumDet; ++d)
        ref[(size_t) d] = model[(size_t) d];
    for (int s = 0; s < 2; ++s)
    {
        env[(size_t) s].fill (kEnvStartDb);
        gr[(size_t) s].fill (0.0f);
    }
    for (int b = 0; b < kMaxBands; ++b)
    {
        meters.overDb[(size_t) b].store (-100.0f);
        meters.grDb[(size_t) b].store (0.0f);
        meters.owner[(size_t) b].store (-1);
    }
    longRmsDb = kAssumedRmsDb;
    learnedSec = 0.0f;
    meters.learnedSeconds.store (0.0f);
    learning = false;
    meters.learnProgress.store (-1.0f);
}

void Engine::setSettings (const Settings& s)
{
    const Settings old = settings;
    settings = s;
    if (s.extendedRange != configuredExtended)
    {
        configure (s.extendedRange); // switches frame size; no allocation (sized for the large frame)
        return;
    }
    bool mapsDirty = false;
    if (s.bandsPerOctave != layoutPerOctave)
    {
        rebuildLayout();
        mapsDirty = true;
    }
    if (old.shelfOn != s.shelfOn || differs (old.shelfHz, s.shelfHz) || differs (old.shelfDb, s.shelfDb)
        || old.selected != s.selected)
        mapsDirty = true;
    for (int f = 0; f < kNumFocus; ++f)
        if (differs (old.focus[(size_t) f].lowHz, s.focus[(size_t) f].lowHz)
            || differs (old.focus[(size_t) f].highHz, s.focus[(size_t) f].highHz))
            mapsDirty = true;
    plan = makePlan (settings, layout);
    if (mapsDirty)
        rebuildBinMaps();
    if (profileVersion.load (std::memory_order_acquire) != profileVersionSeen)
        refreshFrozenRefs();
}

void Engine::rebuildLayout()
{
    layout = Layout (settings.bandsPerOctave, configuredExtended);
    layoutPerOctave = layout.bandsPerOctave;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const float m = b < layout.numBands ? kAssumedRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave) : -100.0f;
        model[(size_t) Det::LR][(size_t) b] = m;
        model[(size_t) Det::M][(size_t) b] = m;
        model[(size_t) Det::S][(size_t) b] = m + kSideModelOffsetDb;
    }
    for (int d = 0; d < kNumDet; ++d)
        ref[(size_t) d] = model[(size_t) d];
    for (int s = 0; s < 2; ++s)
    {
        env[(size_t) s].fill (kEnvStartDb);
        gr[(size_t) s].fill (0.0f);
    }
    learnedSec = 0.0f;
    meters.numBands.store (layout.numBands);
    refreshFrozenRefs();

    // Gain control points: half a band outside each end (0 dB), then every band centre.
    const int nb = layout.numBands;
    const float halfBand = std::pow (2.0f, 0.5f / (float) layout.bandsPerOctave);
    cpHz[0] = layout.low (0) / halfBand;
    for (int b = 0; b < nb; ++b)
        cpHz[(size_t) b + 1] = layout.center (b);
    cpHz[(size_t) nb + 1] = layout.high (nb - 1) * halfBand;

    for (int k = 0; k <= half; ++k)
    {
        const float f = binHz[(size_t) k];
        binSeg[(size_t) k] = -1;
        if (f <= cpHz[0] || f >= cpHz[(size_t) nb + 1])
            continue;
        int j = 0;
        while (j < nb + 1 && f >= cpHz[(size_t) j + 1])
            ++j;
        binSeg[(size_t) k] = j;
        binT[(size_t) k] = std::log2 (f / cpHz[(size_t) j]) / std::log2 (cpHz[(size_t) j + 1] / cpHz[(size_t) j]);
    }
    for (int b = 0; b < nb; ++b)
    {
        int lo = std::max (1, (int) std::ceil (layout.low (b) * n / sr));
        int hi = std::min (half - 1, (int) std::floor (layout.high (b) * n / sr));
        if (hi < lo) // band narrower than a bin: use the nearest bin
            lo = hi = std::clamp ((int) std::lround (layout.center (b) * n / sr), 1, half - 1);
        bandBinLo[(size_t) b] = lo;
        bandBinHi[(size_t) b] = hi;
    }
}

void Engine::rebuildBinMaps()
{
    // Static high shelf: smooth half-octave transition centred on shelfHz.
    for (int k = 0; k <= half; ++k)
    {
        float g = 1.0f;
        if (settings.shelfOn && k > 0)
        {
            const float t = std::clamp ((std::log2 (binHz[(size_t) k] / settings.shelfHz) + 0.25f) / 0.5f, 0.0f, 1.0f);
            g = dbToGain (settings.shelfDb * t * t * (3.0f - 2.0f * t));
        }
        binShelf[(size_t) k] = g;
    }
    // Solo mask of the selected target for Filter listen (1/12-octave tapers).
    std::array<int, kMaxBands> idx {};
    const int cnt = coveredBands (settings.focus[(size_t) std::clamp (settings.selected, 0, kNumFocus - 1)], layout, idx);
    const float lo = layout.low (idx[0]);
    const float hi = layout.high (idx[(size_t) cnt - 1]);
    for (int k = 0; k <= half; ++k)
    {
        const float f = std::max (binHz[(size_t) k], 1.0f);
        const float a = std::clamp ((std::log2 (f / lo) + 1.0f / 12.0f) * 12.0f, 0.0f, 1.0f);
        const float b = std::clamp ((std::log2 (hi / f) + 1.0f / 12.0f) * 12.0f, 0.0f, 1.0f);
        binSolo[(size_t) k] = std::min (a, b);
    }
}

// ---- learned profile ----

Engine::Profile Engine::getProfile() const
{
    Profile p;
    for (int d = 0; d < kNumDet; ++d)
        for (int k = 0; k < kProfileBands; ++k)
            p.db[(size_t) d][(size_t) k] = profileDb[(size_t) d][(size_t) k].load();
    p.valid = meters.profileValid.load();
    return p;
}

void Engine::setProfile (const Profile& p)
{
    for (int d = 0; d < kNumDet; ++d)
        for (int k = 0; k < kProfileBands; ++k)
            profileDb[(size_t) d][(size_t) k].store (p.valid ? p.db[(size_t) d][(size_t) k] : kNaN);
    meters.profileValid.store (p.valid);
    profileVersion.fetch_add (1, std::memory_order_release);
}

/** Converts the profile (1/6-octave bands) to the current layout (NaN where unknown). */
void Engine::refreshFrozenRefs()
{
    profileVersionSeen = profileVersion.load (std::memory_order_acquire);
    const int offset = profileOffset (layout);
    const int per = layout.bandsPerOctave == 6 ? 1 : 2; // profile bands per layout band
    for (int d = 0; d < kNumDet; ++d)
        for (int b = 0; b < layout.numBands; ++b)
        {
            double power = 0.0;
            bool known = true;
            for (int j = 0; j < per; ++j)
            {
                const float v = profileDb[(size_t) d][(size_t) (offset + b * per + j)].load();
                known = known && ! std::isnan (v);
                power += std::pow (10.0, v / 10.0);
            }
            frozen[(size_t) d][(size_t) b] = known ? (float) (10.0 * std::log10 (power)) : kNaN;
        }
}

void Engine::finishLearning()
{
    learning = false;
    meters.learnProgress.store (-1.0f);
    if (learnFrames <= 0)
        return;
    Profile p;
    p.valid = true;
    for (int d = 0; d < kNumDet; ++d)
        for (int k = 0; k < kProfileBands; ++k)
            p.db[(size_t) d][(size_t) k] = (float) (learnSum[(size_t) d][(size_t) k] / learnFrames);
    setProfile (p);
    refreshFrozenRefs();
}

// ---- processing ----

void Engine::process (float* const* io, int numChannels, int numSamples, bool bypass)
{
    const bool stereo = numChannels > 1 && numCh > 1;
    float* const l = io[0];
    float* const r = stereo ? io[1] : nullptr;
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t w = (size_t) (n - hop + pos);
        if (stereo)
        {
            inBuf[0][w] = 0.5f * (l[i] + r[i]); // Mid
            inBuf[1][w] = 0.5f * (l[i] - r[i]); // Side
            const float m = outBuf[0][(size_t) pos], s = outBuf[1][(size_t) pos];
            l[i] = m + s;
            r[i] = m - s;
        }
        else
        {
            inBuf[0][w] = l[i];
            l[i] = outBuf[0][(size_t) pos];
        }

        if (++pos == hop)
        {
            // Drop the hop just played, then overlap-add the new frame so that outBuf[0]
            // lines up with the oldest input sample of the frame (latency = n).
            for (int c = 0; c < 2; ++c)
            {
                std::move (outBuf[c].begin() + hop, outBuf[c].begin() + n, outBuf[c].begin());
                std::fill (outBuf[c].begin() + (n - hop), outBuf[c].begin() + n, 0.0f);
            }
            processFrame (bypass);
            for (int c = 0; c < 2; ++c)
                std::move (inBuf[c].begin() + hop, inBuf[c].begin() + n, inBuf[c].begin());
            pos = 0;
        }
    }
}

void Engine::processFrame (bool bypass)
{
    const bool stereo = numCh > 1;
    const int chans = stereo ? 2 : 1;
    const auto& win = *window;

    for (int c = 0; c < chans; ++c)
    {
        for (int i = 0; i < n; ++i)
        {
            re[c][(size_t) i] = inBuf[c][(size_t) i] * win[(size_t) i];
            im[c][(size_t) i] = 0.0f;
        }
        fft->forward (re[c].data(), im[c].data());
    }
    const float* mr = re[0].data();
    const float* mi = im[0].data();
    const float* sR = stereo ? re[1].data() : nullptr;
    const float* sI = stereo ? im[1].data() : nullptr;

    // Power of L (= M + S), R (= M - S), M and S in a bin range (sums of |X|^2).
    struct Pow { float l = 0, r = 0, m = 0, s = 0; };
    auto power = [&] (int lo, int hi) {
        Pow p;
        for (int k = lo; k <= hi; ++k)
        {
            const float pm = mr[k] * mr[k] + mi[k] * mi[k];
            if (! stereo)
            {
                p.l += pm;
                p.r += pm;
                p.m += pm;
                continue;
            }
            const float lr = mr[k] + sR[k], li = mi[k] + sI[k];
            const float rr = mr[k] - sR[k], ri = mi[k] - sI[k];
            p.l += lr * lr + li * li;
            p.r += rr * rr + ri * ri;
            p.m += pm;
            p.s += sR[k] * sR[k] + sI[k] * sI[k];
        }
        return p;
    };

    // ---- band levels for the three detector signals ----
    const float norm = 4.0f / ((float) n * (float) n); // one-sided power -> mean square (sqrt-Hann)
    const int nb = layout.numBands;
    for (int b = 0; b < nb; ++b)
    {
        const Pow p = power (bandBinLo[(size_t) b], bandBinHi[(size_t) b]);
        level[(size_t) Det::LR][(size_t) b] = toDb (std::max (p.l, p.r) * norm);
        level[(size_t) Det::M][(size_t) b] = toDb (p.m * norm);
        level[(size_t) Det::S][(size_t) b] = toDb (p.s * norm);
    }
    const Pow all = power (1, half - 1);
    const float broadbandDb = toDb (std::max (all.l, all.r) * norm);
    const bool silent = broadbandDb < kSilenceDb;
    if (! silent)
    {
        longRmsDb = broadbandDb + std::exp (-hopSec / kRmsTauSec) * (longRmsDb - broadbandDb);
        learnedSec += hopSec;
    }

    // ---- learning (always on the 32 1/6-octave profile bands, all three signals) ----
    if (learnRequested.exchange (false))
    {
        learning = true;
        learnTime = 0.0f;
        learnFrames = 0;
        for (auto& d : learnSum)
            d.fill (0.0);
    }
    if (learning && ! silent)
    {
        for (int k = 0; k < kProfileBands; ++k)
        {
            const Pow p = power (profBinLo[(size_t) k], profBinHi[(size_t) k]);
            learnSum[(size_t) Det::LR][(size_t) k] += toDb (std::max (p.l, p.r) * norm);
            learnSum[(size_t) Det::M][(size_t) k] += toDb (p.m * norm);
            learnSum[(size_t) Det::S][(size_t) k] += toDb (p.s * norm);
        }
        ++learnFrames;
        learnTime += hopSec;
        meters.learnProgress.store (std::min (1.0f, learnTime / kLearnSeconds), std::memory_order_relaxed);
        if (learnTime >= kLearnSeconds)
            finishLearning();
    }

    // ---- reference (usual level) per band and signal ----
    const float aLong = std::exp (-hopSec / kLongTauSec);
    for (int d = 0; d < kNumDet; ++d)
        for (int b = 0; b < nb; ++b)
        {
            float& rf = ref[(size_t) d][(size_t) b];
            const float lvl = level[(size_t) d][(size_t) b];
            switch (settings.reference)
            {
                case Reference::Auto:
                {
                    const float expected = longRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave)
                                           + (d == (int) Det::S ? kSideModelOffsetDb : 0.0f);
                    if (! silent)
                        rf = lvl + aLong * (rf - lvl);
                    rf = std::clamp (rf, expected - kMaxDeviationDb, expected + kMaxDeviationDb);
                    break;
                }
                case Reference::Learned:
                {
                    const float fz = frozen[(size_t) d][(size_t) b];
                    rf = std::isnan (fz) ? model[(size_t) d][(size_t) b] : fz;
                    break;
                }
                case Reference::Fixed:
                default:
                    rf = model[(size_t) d][(size_t) b];
                    break;
            }
        }

    // ---- detection and gain reduction, Mid and Side slots ----
    const float sel = pct (settings.selectivity);
    const int w = layout.bandsPerOctave; // neighbours: +-1 octave
    for (int slot = 0; slot < 2; ++slot)
        for (int b = 0; b < nb; ++b)
        {
            const auto& p = plan.slot (slot, b);
            float& e = env[(size_t) slot][(size_t) b];
            float& g = gr[(size_t) slot][(size_t) b];
            if (p.owner < 0 || (slot == 1 && ! stereo))
            {
                e = kEnvStartDb;
                g = 0.0f;
                continue;
            }
            const auto& lv = level[(size_t) p.det];
            const float rf = ref[(size_t) p.det][(size_t) b];
            const float oT = lv[(size_t) b] - rf;
            float oN = 0.0f;
            if (sel > 0.0f)
            {
                float sum = 0.0f;
                int cnt = 0;
                for (int j = std::max (0, b - w); j <= std::min (nb - 1, b + w); ++j)
                    if (j != b)
                    {
                        sum += lv[(size_t) j];
                        ++cnt;
                    }
                // Neighbours are never taken as quieter than 12 dB below this band's usual level,
                // so a quiet sound in otherwise silent highs is not treated as "sticking out".
                const float neighbours = cnt > 0 ? std::max (sum / (float) cnt, rf - 12.0f) : lv[(size_t) b];
                oN = lv[(size_t) b] - neighbours;
            }
            const float o = (1.0f - sel) * oT + sel * oN;
            const float tau = (o > e ? p.attackMs : p.releaseMs) * 0.001f;
            e = o + std::exp (-hopSec / tau) * (e - o);
            g = silent ? 0.0f : std::min (compressorReductionDb (e - p.marginDb, p.ratio, p.kneeDb), settings.rangeDb);
        }

    // ---- meters ----
    for (int b = 0; b < nb; ++b)
    {
        int best = -1;
        float bestOver = -1.0e9f;
        for (int slot = 0; slot < (stereo ? 2 : 1); ++slot)
        {
            const auto& p = plan.slot (slot, b);
            if (p.owner >= 0 && env[(size_t) slot][(size_t) b] - p.marginDb > bestOver)
            {
                bestOver = env[(size_t) slot][(size_t) b] - p.marginDb;
                best = slot;
            }
        }
        if (best >= 0)
        {
            const auto& p = plan.slot (best, b);
            meters.overDb[(size_t) b].store (silent ? -100.0f : env[(size_t) best][(size_t) b], std::memory_order_relaxed);
            meters.marginDb[(size_t) b].store (p.marginDb, std::memory_order_relaxed);
            meters.owner[(size_t) b].store (p.owner, std::memory_order_relaxed);
        }
        else
        {
            // No target here: show how far the L/R level is above its usual level.
            meters.overDb[(size_t) b].store (silent ? -100.0f : level[0][(size_t) b] - ref[0][(size_t) b], std::memory_order_relaxed);
            meters.owner[(size_t) b].store (-1, std::memory_order_relaxed);
        }
        meters.grDb[(size_t) b].store (bypass ? 0.0f : std::max (gr[0][(size_t) b], gr[1][(size_t) b]), std::memory_order_relaxed);
    }
    meters.learnedSeconds.store (learnedSec, std::memory_order_relaxed);

    // ---- per-bin gains for Mid and Side ----
    cpDbM[0] = cpDbS[0] = 0.0f;
    for (int b = 0; b < nb; ++b)
    {
        cpDbM[(size_t) b + 1] = gr[0][(size_t) b];
        cpDbS[(size_t) b + 1] = gr[1][(size_t) b];
    }
    cpDbM[(size_t) nb + 1] = cpDbS[(size_t) nb + 1] = 0.0f;

    const auto& selFocus = settings.focus[(size_t) std::clamp (settings.selected, 0, kNumFocus - 1)];
    const float soloM = selFocus.channel == ChannelMode::Side ? 0.0f : 1.0f;
    const float soloS = selFocus.channel == ChannelMode::Mid ? 0.0f : 1.0f;
    const float mix = pct (settings.mix);
    const float out = dbToGain (settings.outputDb);
    for (int k = 0; k <= half; ++k)
    {
        float redM = 0.0f, redS = 0.0f;
        const int seg = binSeg[(size_t) k];
        if (seg >= 0)
        {
            const float t = binT[(size_t) k];
            redM = cpDbM[(size_t) seg] + t * (cpDbM[(size_t) seg + 1] - cpDbM[(size_t) seg]);
            redS = cpDbS[(size_t) seg] + t * (cpDbS[(size_t) seg + 1] - cpDbS[(size_t) seg]);
        }
        const float gm = 1.0f - mix * (1.0f - dbToGain (-redM)); // true dry/wet in the gain
        const float gs = 1.0f - mix * (1.0f - dbToGain (-redS));
        float GM, GS;
        if (bypass)
        {
            GM = GS = 1.0f;
        }
        else
        {
            switch (settings.listen)
            {
                case Listen::Delta:  GM = (1.0f - gm) * out; GS = (1.0f - gs) * out; break;
                case Listen::Filter: GM = binSolo[(size_t) k] * soloM * out; GS = binSolo[(size_t) k] * soloS * out; break;
                case Listen::Normal:
                default:             GM = gm * binShelf[(size_t) k] * out; GS = gs * binShelf[(size_t) k] * out; break;
            }
        }
        binGainM[(size_t) k] = GM;
        binGainS[(size_t) k] = GS;
    }

    // ---- spectrum display (every other frame), in L/R terms ----
    if ((++frameCounter & 1) == 0)
    {
        const float specNorm = 1.0f / (0.3183f * (float) n * 0.3183f * (float) n); // full-scale sine peak -> 0 dB
        for (int i = 0; i < kSpecPoints; ++i)
        {
            float pin = 0.0f, pout = 0.0f;
            for (int k = specLo[(size_t) i]; k <= specHi[(size_t) i]; ++k)
            {
                const float gM = binGainM[(size_t) k], gS = binGainS[(size_t) k];
                if (stereo)
                {
                    const float lr = mr[k] + sR[k], li = mi[k] + sI[k], rr = mr[k] - sR[k], ri = mi[k] - sI[k];
                    pin = std::max (pin, std::max (lr * lr + li * li, rr * rr + ri * ri));
                    const float olr = gM * mr[k] + gS * sR[k], oli = gM * mi[k] + gS * sI[k];
                    const float orr = gM * mr[k] - gS * sR[k], ori = gM * mi[k] - gS * sI[k];
                    pout = std::max (pout, std::max (olr * olr + oli * oli, orr * orr + ori * ori));
                }
                else
                {
                    const float pm = mr[k] * mr[k] + mi[k] * mi[k];
                    pin = std::max (pin, pm);
                    pout = std::max (pout, pm * gM * gM);
                }
            }
            meters.specIn[(size_t) i].store (toDb (pin * specNorm), std::memory_order_relaxed);
            meters.specOut[(size_t) i].store (toDb (pout * specNorm), std::memory_order_relaxed);
        }
    }

    // ---- apply, inverse transform, overlap-add ----
    for (int c = 0; c < chans; ++c)
    {
        const auto& gains = c == 0 ? binGainM : binGainS;
        auto* R = re[c].data();
        auto* I = im[c].data();
        R[0] *= gains[0];
        I[0] *= gains[0];
        for (int k = 1; k < half; ++k)
        {
            const float g = gains[(size_t) k];
            R[k] *= g;
            I[k] *= g;
            R[n - k] *= g;
            I[n - k] *= g;
        }
        R[half] *= gains[(size_t) half];
        I[half] *= gains[(size_t) half];
        fft->inverse (R, I);
        auto& o = outBuf[c];
        for (int i = 0; i < n; ++i)
            o[(size_t) i] += R[i] * win[(size_t) i] * 0.5f; // Hann at 75 % overlap sums to 2
    }
}

} // namespace sasari
