#pragma once

// Settings and the pure math that turns them into per-band compressor numbers.
// Mirrors the Android app's DspMapping.kt so both versions behave alike.
// No JUCE dependency: unit tested on its own.

#include <algorithm>
#include <array>
#include <cmath>

namespace sasari
{

enum class Focus : int { Guitar = 0, Vocal = 1, Hihat = 2 };
constexpr int kNumFocus = 3;

struct FocusInfo
{
    float defaultLowHz, defaultHighHz;
    float baseAttackMs, baseReleaseMs;
};

// Guitar fizz is sustained (slow, dynamic-EQ-like); sibilance and hi-hats are short bursts.
inline constexpr FocusInfo kFocusInfo[kNumFocus] = {
    { 2500.0f, 5000.0f, 4.0f, 120.0f },
    { 5000.0f, 10000.0f, 1.0f, 60.0f },
    { 8000.0f, 16000.0f, 0.7f, 45.0f },
};

struct FocusSettings
{
    bool enabled = true;
    float lowHz = 5000.0f, highHz = 10000.0f;
    float processing = 50.0f; // 0..100: how readily it reacts
    float intensity = 50.0f;  // 0..100: ratio 1:1 .. 20:1
};

enum class Listen : int { Normal = 0, Filter = 1, Delta = 2 };
enum class ChannelMode : int { Linked = 0, Mid = 1, Side = 2 };

struct Settings
{
    std::array<FocusSettings, kNumFocus> focus {
        FocusSettings { true, 2500.0f, 5000.0f, 45.0f, 45.0f },
        FocusSettings { true, 5000.0f, 10000.0f, 55.0f, 60.0f },
        FocusSettings { true, 8000.0f, 16000.0f, 50.0f, 55.0f },
    };
    int selected = 1;           // target soloed by Filter listen
    float sharpness = 50.0f;    // knee + reaction speed
    float mix = 100.0f;         // true dry/wet, %
    float rangeDb = 12.0f;      // maximum reduction per band
    float outputDb = 0.0f;
    int bandsPerOctave = 3;     // 3 or 6
    bool autoLevel = true;      // learn each band's usual level from the music
    Listen listen = Listen::Normal;
    ChannelMode channels = ChannelMode::Linked;
    bool shelfOn = false;
    float shelfHz = 8000.0f;
    float shelfDb = -4.0f;
};

constexpr float kMinFreqHz = 1600.0f;
constexpr float kMaxFreqHz = 16000.0f;
constexpr int kMaxBands = 20;
constexpr float kAssumedRmsDb = -20.0f;

/** Analysis bands: 1/3 or 1/6 octave from 2 kHz * 2^(-1/3) (1587 Hz) to 16 kHz. */
struct Layout
{
    int bandsPerOctave = 3;
    int numBands = 10;
    std::array<float, kMaxBands + 1> edges {};

    explicit Layout (int perOctave = 3)
    {
        bandsPerOctave = perOctave == 6 ? 6 : 3;
        numBands = 10 * bandsPerOctave / 3;
        for (int k = 0; k <= numBands; ++k)
            edges[(size_t) k] = (float) (2000.0 * std::pow (2.0, -1.0 / 3.0 + (double) k / bandsPerOctave));
    }

    float low (int b) const { return edges[(size_t) b]; }
    float high (int b) const { return edges[(size_t) b + 1]; }
    float center (int b) const { return std::sqrt (low (b) * high (b)); }
};

inline float pct (float v) { return std::clamp (v / 100.0f, 0.0f, 1.0f); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

/** Usual 1/N-octave band level relative to broadband: roughly pink, -3 dB/oct extra in the treble. */
inline float modelRelativeDb (float centerHz, int bandsPerOctave)
{
    return -15.0f - 3.0f * std::log2 (centerHz / 2000.0f) - 10.0f * std::log10 ((float) bandsPerOctave / 3.0f);
}

/** Processing 0% = +30 dB over usual (almost never), 50% = +10 dB, 100% = -10 dB. */
inline float marginDb (float processing) { return 30.0f - 40.0f * pct (processing); }
inline float ratioFor (float intensity) { return std::pow (20.0f, pct (intensity)); }
inline float kneeDb (float sharpness) { return lerp (12.0f, 0.0f, pct (sharpness)); }
inline float timeFactor (float sharpness) { return lerp (2.0f, 0.5f, pct (sharpness)); }
/** Higher bands react faster (as in soothe2): x1.4 at 2 kHz, x1 at 4 kHz, x0.5 at 16 kHz. */
inline float frequencyTimeFactor (float centerHz) { return std::clamp (std::sqrt (4000.0f / centerHz), 0.5f, 1.5f); }

/** Gain reduction (positive dB) of a soft-knee downward compressor for a level `over` dB above threshold. */
inline float compressorReductionDb (float over, float ratio, float knee)
{
    float out;
    if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
    {
        const float x = over + knee * 0.5f;
        out = over + (1.0f / ratio - 1.0f) * x * x / (2.0f * knee);
    }
    else if (over <= 0.0f)
        out = over;
    else
        out = over / ratio;
    return std::max (0.0f, over - out);
}

/** Bands (indices) covered by a target: those whose centre is in range, or the nearest one. */
inline int coveredBands (const FocusSettings& f, const Layout& l, std::array<int, kMaxBands>& out)
{
    const float lo = std::clamp (std::min (f.lowHz, f.highHz), kMinFreqHz, kMaxFreqHz);
    const float hi = std::clamp (std::max (f.lowHz, f.highHz), kMinFreqHz, kMaxFreqHz);
    int n = 0;
    for (int b = 0; b < l.numBands; ++b)
        if (l.center (b) >= lo && l.center (b) <= hi)
            out[(size_t) n++] = b;
    if (n == 0)
    {
        const float mid = std::sqrt (lo * hi);
        int best = 0;
        for (int b = 1; b < l.numBands; ++b)
            if (std::abs (std::log2 (l.center (b) / mid)) < std::abs (std::log2 (l.center (best) / mid)))
                best = b;
        out[0] = best;
        n = 1;
    }
    return n;
}

struct BandPlan
{
    int owner = -1;         // Focus index or -1 (untouched)
    float marginDb = 0.0f;
    float ratio = 1.0f;
    float kneeDb = 0.0f;
    float attackMs = 1.0f;
    float releaseMs = 50.0f;
};

/** Per-band plan. Overlaps go to the most sensitive target (ties: stronger one). */
inline std::array<BandPlan, kMaxBands> makePlan (const Settings& s, const Layout& l)
{
    std::array<BandPlan, kMaxBands> plan {};
    std::array<int, kMaxBands> idx {};
    const float knee = kneeDb (s.sharpness);
    const float tf = timeFactor (s.sharpness);
    for (int f = 0; f < kNumFocus; ++f)
    {
        const auto& fs = s.focus[(size_t) f];
        if (! fs.enabled)
            continue;
        const int n = coveredBands (fs, l, idx);
        for (int i = 0; i < n; ++i)
        {
            auto& p = plan[(size_t) idx[(size_t) i]];
            const float m = marginDb (fs.processing);
            const bool tie = p.owner >= 0 && std::abs (m - p.marginDb) < 1.0e-6f;
            const bool better = p.owner < 0 || (! tie && m < p.marginDb)
                                || (tie && fs.intensity > s.focus[(size_t) p.owner].intensity);
            if (! better)
                continue;
            const float ftf = frequencyTimeFactor (l.center (idx[(size_t) i]));
            p.owner = f;
            p.marginDb = m;
            p.ratio = ratioFor (fs.intensity);
            p.kneeDb = knee;
            p.attackMs = kFocusInfo[f].baseAttackMs * tf * ftf;
            p.releaseMs = kFocusInfo[f].baseReleaseMs * tf * ftf;
        }
    }
    return plan;
}

} // namespace sasari
