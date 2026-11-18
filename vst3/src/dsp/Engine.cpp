#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace sasari
{

namespace
{
    constexpr float kSilenceDb = -70.0f;
    constexpr float kLongTauSec = 10.0f;
    constexpr float kRmsTauSec = 4.0f;
    constexpr float kMaxDeviationDb = 18.0f;
    constexpr double kPi = 3.14159265358979323846;

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }
} // namespace

void Engine::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    numCh = std::clamp (numChannels, 1, 2);
    // ~10.7 ms frames whatever the sample rate.
    n = sr <= 50000.0 ? 512 : (sr <= 100000.0 ? 1024 : 2048);
    hop = n / 4;
    half = n / 2;
    hopSec = (float) (hop / sr);

    fft.prepare (n);
    window.resize ((size_t) n);
    for (int i = 0; i < n; ++i) // periodic sqrt-Hann: analysis x synthesis = Hann, sums to 2 at 75 % overlap
        window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));

    for (int c = 0; c < 2; ++c)
    {
        inBuf[c].assign ((size_t) n, 0.0f);
        outBuf[c].assign ((size_t) n, 0.0f);
        re[c].assign ((size_t) n, 0.0f);
        im[c].assign ((size_t) n, 0.0f);
    }
    binHz.resize ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
        binHz[(size_t) k] = (float) (k * sr / n);
    binSeg.assign ((size_t) half + 1, -1);
    binT.assign ((size_t) half + 1, 0.0f);
    binShelf.assign ((size_t) half + 1, 1.0f);
    binSolo.assign ((size_t) half + 1, 0.0f);
    binGain.assign ((size_t) half + 1, 1.0f);
    binGainOther.assign ((size_t) half + 1, 1.0f);

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
        env[(size_t) b] = ref[(size_t) b] = model[(size_t) b];
        gr[(size_t) b] = 0.0f;
    }
    longRmsDb = kAssumedRmsDb;
    learnedSec = 0.0f;
    for (int b = 0; b < kMaxBands; ++b)
    {
        meters.overDb[(size_t) b].store (-100.0f); // "no signal" until audio arrives
        meters.grDb[(size_t) b].store (0.0f);
    }
    meters.learnedSeconds.store (0.0f);
}

void Engine::setSettings (const Settings& s)
{
    const Settings old = settings;
    settings = s;
    if (s.bandsPerOctave != layoutPerOctave)
        rebuildLayout();
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
}

void Engine::rebuildLayout()
{
    layout = Layout (settings.bandsPerOctave);
    layoutPerOctave = layout.bandsPerOctave;
    for (int b = 0; b < kMaxBands; ++b)
    {
        model[(size_t) b] = b < layout.numBands
                                ? kAssumedRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave)
                                : -100.0f;
        env[(size_t) b] = ref[(size_t) b] = model[(size_t) b];
        gr[(size_t) b] = 0.0f;
    }
    learnedSec = 0.0f;
    meters.numBands.store (layout.numBands);

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
        bandBinLo[(size_t) b] = std::max (1, (int) std::ceil (layout.low (b) * n / sr));
        bandBinHi[(size_t) b] = std::min (half - 1, (int) std::floor (layout.high (b) * n / sr));
    }
    mapsDirty = true;
}

void Engine::rebuildBinMaps()
{
    mapsDirty = false;
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
                std::move (outBuf[c].begin() + hop, outBuf[c].end(), outBuf[c].begin());
                std::fill (outBuf[c].end() - hop, outBuf[c].end(), 0.0f);
            }
            processFrame (bypass);
            for (int c = 0; c < 2; ++c)
                std::move (inBuf[c].begin() + hop, inBuf[c].end(), inBuf[c].begin());
            pos = 0;
        }
    }
}

void Engine::processFrame (bool bypass)
{
    const bool stereoIn = numCh > 1;
    const ChannelMode mode = stereoIn ? settings.channels : ChannelMode::Linked;
    // Which channels feed detection and get the de-essing gain.
    bool processed[2] = { true, stereoIn };
    if (mode == ChannelMode::Mid)
        processed[1] = false;
    if (mode == ChannelMode::Side)
        processed[0] = false;
    const int chans = stereoIn ? 2 : 1;

    for (int c = 0; c < chans; ++c)
    {
        for (int i = 0; i < n; ++i)
        {
            re[c][(size_t) i] = inBuf[c][(size_t) i] * window[(size_t) i];
            im[c][(size_t) i] = 0.0f;
        }
        fft.forward (re[c].data(), im[c].data());
    }

    // ---- detection ----
    const float norm = 4.0f / ((float) n * (float) n); // one-sided power -> mean square (sqrt-Hann)
    float total = 0.0f;
    std::array<float, kMaxBands> level {};
    level.fill (-120.0f);
    for (int c = 0; c < chans; ++c)
    {
        if (! processed[c])
            continue;
        float t = 0.0f;
        for (int k = 1; k < half; ++k)
            t += re[c][(size_t) k] * re[c][(size_t) k] + im[c][(size_t) k] * im[c][(size_t) k];
        total = std::max (total, t * norm);
        for (int b = 0; b < layout.numBands; ++b)
        {
            float p = 0.0f;
            for (int k = bandBinLo[(size_t) b]; k <= bandBinHi[(size_t) b]; ++k)
                p += re[c][(size_t) k] * re[c][(size_t) k] + im[c][(size_t) k] * im[c][(size_t) k];
            level[(size_t) b] = std::max (level[(size_t) b], 10.0f * std::log10 (p * norm + 1e-12f));
        }
    }
    const float broadbandDb = 10.0f * std::log10 (total + 1e-12f);
    const bool silent = broadbandDb < kSilenceDb;
    const float aLong = std::exp (-hopSec / kLongTauSec);
    const float aRms = std::exp (-hopSec / kRmsTauSec);
    if (! silent)
    {
        longRmsDb = broadbandDb + aRms * (longRmsDb - broadbandDb);
        learnedSec += hopSec;
    }

    for (int b = 0; b < layout.numBands; ++b)
    {
        const auto& p = plan[(size_t) b];
        const float lvl = level[(size_t) b];
        float& e = env[(size_t) b];
        if (p.owner >= 0)
        {
            const float tau = (lvl > e ? p.attackMs : p.releaseMs) * 0.001f;
            e = lvl + std::exp (-hopSec / tau) * (e - lvl);
        }
        else
        {
            e = lvl + std::exp (-hopSec / 0.05f) * (e - lvl);
        }

        float& rf = ref[(size_t) b];
        const float expected = longRmsDb + modelRelativeDb (layout.center (b), layout.bandsPerOctave);
        if (settings.autoLevel)
        {
            if (! silent)
                rf = lvl + aLong * (rf - lvl);
            rf = std::clamp (rf, expected - kMaxDeviationDb, expected + kMaxDeviationDb);
        }
        else
        {
            rf = model[(size_t) b];
        }

        float g = 0.0f;
        if (p.owner >= 0 && ! silent)
            g = std::min (compressorReductionDb (e - (rf + p.marginDb), p.ratio, p.kneeDb), settings.rangeDb);
        gr[(size_t) b] = g;
        meters.overDb[(size_t) b].store (silent ? -100.0f : e - rf, std::memory_order_relaxed);
        meters.grDb[(size_t) b].store (bypass ? 0.0f : g, std::memory_order_relaxed);
    }
    meters.learnedSeconds.store (learnedSec, std::memory_order_relaxed);

    // ---- per-bin gains ----
    const int nb = layout.numBands;
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
        fft.inverse (R, I);
        auto& o = outBuf[c];
        for (int i = 0; i < n; ++i)
            o[(size_t) i] += R[i] * window[(size_t) i] * 0.5f; // Hann at 75 % overlap sums to 2
    }
}

} // namespace sasari
