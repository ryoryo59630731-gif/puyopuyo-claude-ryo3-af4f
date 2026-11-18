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
    constexpr double kPi = 3.14159265358979323846;
    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }

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
    for (auto& v : meters.specIn)
        v.store (-120.0f);
    for (auto& v : meters.specOut)
        v.store (-120.0f);
    for (auto& v : profileDb)
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
    for (auto* v : { &binHz, &binT, &binShelf, &binSolo, &binGain, &binGainOther })
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

    // Spectrum display points: log spaced, each mapped to a bin range.
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const float f0 = kSpecLowHz * std::pow (kSpecHighHz / kSpecLowHz, (float) i / kSpecPoints);
        const float f1 = kSpecLowHz * std::pow (kSpecHighHz / kSpecLowHz, (float) (i + 1) / kSpecPoints);
        int lo = (int) std::ceil (f0 * n / sr), hi = (int) std::floor (f1 * n / sr);
        if (hi < lo)
            lo = hi = (int) std::lround (std::sqrt (f0 * f1) * n / sr);
        specLo[(size_t) i] = std::clamp (lo, 1, half - 1);
        specHi[(size_t) i] = std::clamp (hi, 1, half - 1);
    }

    // Profile bands (1/6 octave, 397 Hz - 16 kHz) for learning.
    for (int k = 0; k < kProfileBands; ++k)
    {
        const float f0 = profileEdgeHz (k), f1 = profileEdgeHz (k + 1);
        int lo = (int) std::ceil (f0 * n / sr), hi = (int) std::floor (f1 * n / sr);
        if (hi < lo)
            lo = hi = (int) std::lround (std::sqrt (f0 * f1) * n / sr);
        profBinLo[(size_t) k] = std::clamp (lo, 1, half - 1);
        profBinHi[(size_t) k] = std::clamp (hi, 1, half - 1);
    }

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
    for (int b = 0; b < kMaxBands; ++b)
    {
        ref[(size_t) b] = model[(size_t) b];
        env[(size_t) b] = kEnvStartDb;
        gr[(size_t) b] = 0.0f;
        meters.overDb[(size_t) b].store (-100.0f);
        meters.grDb[(size_t) b].store (0.0f);
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
    layoutExtended = layout.extended;
    for (int b = 0; b < kMaxBands; ++b)
    {
        model[(size_t) b] = b < layout.numBands
                                ? kAssumedRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave)
                                : -100.0f;
        ref[(size_t) b] = model[(size_t) b];
        env[(size_t) b] = kEnvStartDb;
        gr[(size_t) b] = 0.0f;
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
    for (int k = 0; k < kProfileBands; ++k)
        p.db[(size_t) k] = profileDb[(size_t) k].load();
    p.valid = meters.profileValid.load();
    return p;
}

void Engine::setProfile (const Profile& p)
{
    for (int k = 0; k < kProfileBands; ++k)
        profileDb[(size_t) k].store (p.valid ? p.db[(size_t) k] : kNaN);
    meters.profileValid.store (p.valid);
    profileVersion.fetch_add (1, std::memory_order_release);
}

/** Converts the profile (1/6-octave bands) to the current layout (NaN where unknown). */
void Engine::refreshFrozenRefs()
{
    profileVersionSeen = profileVersion.load (std::memory_order_acquire);
    const int offset = profileOffset (layout);
    const int per = layout.bandsPerOctave == 6 ? 1 : 2; // profile bands per layout band
    for (int b = 0; b < layout.numBands; ++b)
    {
        double power = 0.0;
        bool known = true;
        for (int j = 0; j < per; ++j)
        {
            const float v = profileDb[(size_t) (offset + b * per + j)].load();
            known = known && ! std::isnan (v);
            power += std::pow (10.0, v / 10.0);
        }
        frozen[(size_t) b] = known ? (float) (10.0 * std::log10 (power)) : kNaN;
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
    for (int k = 0; k < kProfileBands; ++k)
        p.db[(size_t) k] = (float) (learnSum[(size_t) k] / learnFrames);
    setProfile (p);
    refreshFrozenRefs();
}

// ---- processing ----

void Engine::process (float* const* io, int numChannels, int numSamples, bool bypass)
{
    const bool stereo = numChannels > 1 && numCh > 1;
    const bool ms = stereo && settings.channels != ChannelMode::Linked;
    float* const l = io[0];
    float* const r = stereo ? io[1] : nullptr;
    for (int i = 0; i < numSamples; ++i)
    {
        float a = l[i];
        float b = r != nullptr ? r[i] : 0.0f;
        if (ms)
        {
            const float m = 0.5f * (a + b), s = 0.5f * (a - b);
            a = m;
            b = s;
        }
        const size_t w = (size_t) (n - hop + pos);
        inBuf[0][w] = a;
        inBuf[1][w] = b;

        float y0 = outBuf[0][(size_t) pos];
        float y1 = outBuf[1][(size_t) pos];
        if (ms)
        {
            const float m = y0, s = y1;
            y0 = m + s;
            y1 = m - s;
        }
        l[i] = y0;
        if (r != nullptr)
            r[i] = y1;

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
    const bool stereoIn = numCh > 1;
    const ChannelMode mode = stereoIn ? settings.channels : ChannelMode::Linked;
    bool processed[2] = { true, stereoIn };
    if (mode == ChannelMode::Mid)
        processed[1] = false;
    if (mode == ChannelMode::Side)
        processed[0] = false;
    const int chans = stereoIn ? 2 : 1;
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

    // ---- band levels ----
    const float norm = 4.0f / ((float) n * (float) n); // one-sided power -> mean square (sqrt-Hann)
    const int nb = layout.numBands;
    float total = 0.0f;
    for (int b = 0; b < nb; ++b)
        level[(size_t) b] = -120.0f;
    for (int c = 0; c < chans; ++c)
    {
        if (! processed[c])
            continue;
        float t = 0.0f;
        for (int k = 1; k < half; ++k)
            t += re[c][(size_t) k] * re[c][(size_t) k] + im[c][(size_t) k] * im[c][(size_t) k];
        total = std::max (total, t * norm);
        for (int b = 0; b < nb; ++b)
        {
            float p = 0.0f;
            for (int k = bandBinLo[(size_t) b]; k <= bandBinHi[(size_t) b]; ++k)
                p += re[c][(size_t) k] * re[c][(size_t) k] + im[c][(size_t) k] * im[c][(size_t) k];
            level[(size_t) b] = std::max (level[(size_t) b], 10.0f * std::log10 (p * norm + 1e-12f));
        }
    }
    const float broadbandDb = 10.0f * std::log10 (total + 1e-12f);
    const bool silent = broadbandDb < kSilenceDb;
    if (! silent)
    {
        longRmsDb = broadbandDb + std::exp (-hopSec / kRmsTauSec) * (longRmsDb - broadbandDb);
        learnedSec += hopSec;
    }

    // ---- learning ----
    if (learnRequested.exchange (false))
    {
        learning = true;
        learnTime = 0.0f;
        learnFrames = 0;
        learnSum.fill (0.0);
    }
    if (learning && ! silent)
    {
        for (int k = 0; k < kProfileBands; ++k)
        {
            float lv = -120.0f;
            for (int c = 0; c < chans; ++c)
            {
                if (! processed[c])
                    continue;
                float p = 0.0f;
                for (int bin = profBinLo[(size_t) k]; bin <= profBinHi[(size_t) k]; ++bin)
                    p += re[c][(size_t) bin] * re[c][(size_t) bin] + im[c][(size_t) bin] * im[c][(size_t) bin];
                lv = std::max (lv, 10.0f * std::log10 (p * norm + 1e-12f));
            }
            learnSum[(size_t) k] += lv;
        }
        ++learnFrames;
        learnTime += hopSec;
        meters.learnProgress.store (std::min (1.0f, learnTime / kLearnSeconds), std::memory_order_relaxed);
        if (learnTime >= kLearnSeconds)
            finishLearning();
    }

    // ---- reference (usual level) per band ----
    const float aLong = std::exp (-hopSec / kLongTauSec);
    for (int b = 0; b < nb; ++b)
    {
        float& rf = ref[(size_t) b];
        const float lvl = level[(size_t) b];
        switch (settings.reference)
        {
            case Reference::Auto:
            {
                const float expected = longRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave);
                if (! silent)
                    rf = lvl + aLong * (rf - lvl);
                rf = std::clamp (rf, expected - kMaxDeviationDb, expected + kMaxDeviationDb);
                break;
            }
            case Reference::Learned:
                rf = std::isnan (frozen[(size_t) b]) ? model[(size_t) b] : frozen[(size_t) b];
                break;
            case Reference::Fixed:
            default:
                rf = model[(size_t) b];
                break;
        }
    }

    // ---- how far each band sticks out: blend of "above usual" and "above neighbours" ----
    const float s = pct (settings.selectivity);
    const int w = layout.bandsPerOctave; // +-1 octave
    for (int b = 0; b < nb; ++b)
    {
        const float oT = level[(size_t) b] - ref[(size_t) b];
        float oN = 0.0f;
        if (s > 0.0f)
        {
            float sum = 0.0f;
            int cnt = 0;
            for (int j = std::max (0, b - w); j <= std::min (nb - 1, b + w); ++j)
                if (j != b)
                {
                    sum += level[(size_t) j];
                    ++cnt;
                }
            // Neighbours are never taken as quieter than 12 dB below this band's usual level,
            // so a quiet sound in otherwise silent highs is not treated as "sticking out".
            const float neighbours = cnt > 0 ? std::max (sum / (float) cnt, ref[(size_t) b] - 12.0f) : level[(size_t) b];
            oN = level[(size_t) b] - neighbours;
        }
        over[(size_t) b] = (1.0f - s) * oT + s * oN;
    }

    for (int b = 0; b < nb; ++b)
    {
        const auto& p = plan[(size_t) b];
        const float o = over[(size_t) b];
        float& e = env[(size_t) b];
        const float attack = p.owner >= 0 ? p.attackMs : 1.0f;
        const float release = p.owner >= 0 ? p.releaseMs : 50.0f;
        const float tau = (o > e ? attack : release) * 0.001f;
        e = o + std::exp (-hopSec / tau) * (e - o);

        float g = 0.0f;
        if (p.owner >= 0 && ! silent)
            g = std::min (compressorReductionDb (e - p.marginDb, p.ratio, p.kneeDb), settings.rangeDb);
        gr[(size_t) b] = g;
        meters.overDb[(size_t) b].store (silent ? -100.0f : e, std::memory_order_relaxed);
        meters.grDb[(size_t) b].store (bypass ? 0.0f : g, std::memory_order_relaxed);
    }
    meters.learnedSeconds.store (learnedSec, std::memory_order_relaxed);

    // ---- per-bin gains ----
    cpDb[0] = 0.0f;
    for (int b = 0; b < nb; ++b)
        cpDb[(size_t) b + 1] = gr[(size_t) b];
    cpDb[(size_t) nb + 1] = 0.0f;

    const float mix = pct (settings.mix);
    const float out = dbToGain (settings.outputDb);
    for (int k = 0; k <= half; ++k)
    {
        float reduction = 0.0f;
        const int seg = binSeg[(size_t) k];
        if (seg >= 0)
            reduction = cpDb[(size_t) seg] + binT[(size_t) k] * (cpDb[(size_t) seg + 1] - cpDb[(size_t) seg]);
        const float g = 1.0f - mix * (1.0f - dbToGain (-reduction)); // true dry/wet in the gain
        float gp, go;
        if (bypass)
        {
            gp = go = 1.0f;
        }
        else
        {
            switch (settings.listen)
            {
                case Listen::Delta:  gp = (1.0f - g) * out; go = 0.0f; break;
                case Listen::Filter: gp = binSolo[(size_t) k] * out; go = 0.0f; break;
                case Listen::Normal:
                default:             gp = g * binShelf[(size_t) k] * out; go = binShelf[(size_t) k] * out; break;
            }
        }
        binGain[(size_t) k] = gp;
        binGainOther[(size_t) k] = go;
    }

    // ---- spectrum display (every other frame) ----
    if ((++frameCounter & 1) == 0)
    {
        const float specNorm = 1.0f / (0.3183f * (float) n * 0.3183f * (float) n); // full-scale sine peak -> 0 dB
        for (int i = 0; i < kSpecPoints; ++i)
        {
            float pin = 0.0f, pout = 0.0f;
            for (int c = 0; c < chans; ++c)
            {
                if (! processed[c])
                    continue;
                float a = 0.0f, o = 0.0f;
                for (int k = specLo[(size_t) i]; k <= specHi[(size_t) i]; ++k)
                {
                    const float p = re[c][(size_t) k] * re[c][(size_t) k] + im[c][(size_t) k] * im[c][(size_t) k];
                    a = std::max (a, p);
                    o = std::max (o, p * binGain[(size_t) k] * binGain[(size_t) k]);
                }
                pin = std::max (pin, a);
                pout = std::max (pout, o);
            }
            meters.specIn[(size_t) i].store (10.0f * std::log10 (pin * specNorm + 1e-12f), std::memory_order_relaxed);
            meters.specOut[(size_t) i].store (10.0f * std::log10 (pout * specNorm + 1e-12f), std::memory_order_relaxed);
        }
    }

    // ---- apply, inverse transform, overlap-add ----
    for (int c = 0; c < chans; ++c)
    {
        const auto& gains = processed[c] ? binGain : binGainOther;
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
