#pragma once

// Settings and the pure math that turns them into per-band compressor numbers.
// Mirrors the Android app's DspMapping.kt (same targets, same 1/3- and 1/6-octave grid).
// No JUCE dependency: unit tested on its own. See SPEC.md section 4.

#include <algorithm>
#include <array>
#include <cmath>

namespace sasari
{

enum class Focus : int { Guitar = 0, Vocal = 1, Hihat = 2, Custom = 3 };
constexpr int kNumFocus = 4;
constexpr int kCustom = 3;

struct FocusInfo
{
    float defaultLowHz, defaultHighHz;
    float baseAttackMs, baseReleaseMs;
};

// Guitar fizz is sustained (slow, dynamic-EQ-like); sibilance and hi-hats are short bursts.
// The custom target uses its own attack / release from FocusSettings.
inline constexpr FocusInfo kFocusInfo[kNumFocus] = {
    { 2500.0f, 5000.0f, 4.0f, 120.0f },
    { 5000.0f, 10000.0f, 1.0f, 60.0f },
    { 8000.0f, 16000.0f, 0.7f, 45.0f },
    { 600.0f, 1200.0f, 5.0f, 100.0f },
};

/** Which part of the stereo image a target listens to and acts on. */
enum class ChannelMode : int
{
    Linked = 0, // detect on the louder of L / R, reduce Mid and Side equally (image unchanged)
    Mid = 1,    // detect and reduce the centre only (lead vocal)
    Side = 2,   // detect and reduce the stereo width only (wide cymbals, doubled guitars)
};

struct FocusSettings
{
    bool enabled = true;
    float lowHz = 5000.0f, highHz = 10000.0f;
    float processing = 50.0f; // 0..100: how readily it reacts
    float intensity = 50.0f;  // 0..100: ratio 1:1 .. 20:1
    float attackMs = 5.0f;    // custom target only
    float releaseMs = 100.0f; // custom target only
    ChannelMode channel = ChannelMode::Linked;
};

enum class Listen : int { Normal = 0, Filter = 1, Delta = 2 };
enum class Reference : int { Fixed = 0, Auto = 1, Learned = 2 };

struct Settings
{
    std::array<FocusSettings, kNumFocus> focus {
        FocusSettings { true, 2500.0f, 5000.0f, 45.0f, 45.0f },
        FocusSettings { true, 5000.0f, 10000.0f, 55.0f, 60.0f },
        FocusSettings { true, 8000.0f, 16000.0f, 50.0f, 55.0f },
        FocusSettings { false, 600.0f, 1200.0f, 50.0f, 50.0f, 5.0f, 100.0f },
    };
    int selected = 1;           // target soloed by Filter listen
    float sharpness = 50.0f;    // knee + reaction speed
    float mix = 100.0f;         // true dry/wet, %
    float rangeDb = 12.0f;      // maximum reduction per band
    float outputDb = 0.0f;
    float selectivity = 30.0f;  // 0 = compare with usual level, 100 = compare with neighbouring bands
    int bandsPerOctave = 3;     // 3 or 6
    bool extendedRange = false; // analyse from 400 Hz instead of 1.6 kHz (longer frames)
    Reference reference = Reference::Auto;
    Listen listen = Listen::Normal;
    bool shelfOn = false;
    float shelfHz = 8000.0f;
    float shelfDb = -4.0f;
};

constexpr float kMinFreqHz = 400.0f;  // lowest value of the LOW / HIGH knobs
constexpr float kMaxFreqHz = 16000.0f;
constexpr int kMaxBands = 32;
constexpr float kAssumedRmsDb = -20.0f;

/** Analysis bands: 1/3 or 1/6 octave up to 16 kHz, from 1587 Hz (standard) or 397 Hz (extended). */
struct Layout
{
    int bandsPerOctave = 3;
    bool extended = false;
    int numBands = 10;
    std::array<float, kMaxBands + 1> edges {};

    explicit Layout (int perOctave = 3, bool extendedRange = false)
    {
        bandsPerOctave = perOctave == 6 ? 6 : 3;
        extended = extendedRange;
        const int thirdsBelow2k = extended ? 7 : 1; // 2 kHz * 2^(-7/3) = 397 Hz, 2^(-1/3) = 1587 Hz
        numBands = (9 + thirdsBelow2k) * bandsPerOctave / 3;
        for (int k = 0; k <= numBands; ++k)
            edges[(size_t) k] = (float) (2000.0 * std::pow (2.0, -thirdsBelow2k / 3.0 + (double) k / bandsPerOctave));
    }

    float low (int b) const { return edges[(size_t) b]; }
    float high (int b) const { return edges[(size_t) b + 1]; }
    float center (int b) const { return std::sqrt (low (b) * high (b)); }
    float lowLimit() const { return edges[0]; }
    float highLimit() const { return edges[(size_t) numBands]; }
};

inline float pct (float v) { return std::clamp (v / 100.0f, 0.0f, 1.0f); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

/** Usual band level relative to broadband: pink below 2 kHz, -3 dB/oct extra above. */
inline float modelRelativeDb (float centerHz, int bandsPerOctave)
{
    return -15.0f - 3.0f * std::max (0.0f, std::log2 (centerHz / 2000.0f))
           - 10.0f * std::log10 ((float) bandsPerOctave / 3.0f);
}

/** Processing 0% = +30 dB over usual (almost never), 50% = +10 dB, 100% = -10 dB. */
inline float marginDb (float processing) { return 30.0f - 40.0f * pct (processing); }
inline float ratioFor (float intensity) { return std::pow (20.0f, pct (intensity)); }
inline float kneeDb (float sharpness) { return lerp (12.0f, 0.0f, pct (sharpness)); }
inline float timeFactor (float sharpness) { return lerp (2.0f, 0.5f, pct (sharpness)); }
/** Higher bands react faster (as in soothe2): x1.4 at 2 kHz, x1 at 4 kHz, x0.5 at 16 kHz, max x1.5. */
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

/** True when a target's range lies completely outside the analysed region. */
inline bool outsideLayout (const FocusSettings& f, const Layout& l)
{
    const float lo = std::min (f.lowHz, f.highHz), hi = std::max (f.lowHz, f.highHz);
    return hi < l.lowLimit() || lo > l.highLimit();
}

/** Bands (indices) covered by a target: those whose centre is in range, or the nearest one. */
inline int coveredBands (const FocusSettings& f, const Layout& l, std::array<int, kMaxBands>& out)
{
    const float lo = std::clamp (std::min (f.lowHz, f.highHz), l.lowLimit(), l.highLimit());
    const float hi = std::clamp (std::max (f.lowHz, f.highHz), l.lowLimit(), l.highLimit());
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

/** Signal a band detector listens to. */
enum class Det : int { LR = 0, M = 1, S = 2 };
constexpr int kNumDet = 3;

struct BandPlan
{
    int owner = -1;         // Focus index or -1 (untouched)
    Det det = Det::LR;      // what the owner listens to
    float marginDb = 0.0f;
    float ratio = 1.0f;
    float kneeDb = 0.0f;
    float attackMs = 1.0f;
    float releaseMs = 50.0f;
};

/**
 * Per-band plan for the Mid and the Side signal separately. A stereo (linked) target claims
 * both, a Mid target only Mid, a Side target only Side. Where several targets claim the same
 * band of the same signal, the most sensitive one wins (ties: the stronger one), so e.g. a Mid
 * vocal target and a Side hi-hat target both act on an overlapping band.
 */
struct Plan
{
    std::array<BandPlan, kMaxBands> mid {}, side {};
    const BandPlan& slot (int s, int b) const { return s == 0 ? mid[(size_t) b] : side[(size_t) b]; }
    /** Target to colour a band with: the Mid owner, else the Side owner. */
    int displayOwner (int b) const { return mid[(size_t) b].owner >= 0 ? mid[(size_t) b].owner : side[(size_t) b].owner; }
};

inline Plan makePlan (const Settings& s, const Layout& l)
{
    Plan plan;
    std::array<int, kMaxBands> idx {};
    const float knee = kneeDb (s.sharpness);
    const float tf = timeFactor (s.sharpness);
    for (int f = 0; f < kNumFocus; ++f)
    {
        const auto& fs = s.focus[(size_t) f];
        if (! fs.enabled || outsideLayout (fs, l))
            continue;
        const float baseAttack = f == kCustom ? fs.attackMs : kFocusInfo[f].baseAttackMs;
        const float baseRelease = f == kCustom ? fs.releaseMs : kFocusInfo[f].baseReleaseMs;
        const Det det = fs.channel == ChannelMode::Mid ? Det::M : (fs.channel == ChannelMode::Side ? Det::S : Det::LR);
        const bool claimsMid = fs.channel != ChannelMode::Side;
        const bool claimsSide = fs.channel != ChannelMode::Mid;
        const int n = coveredBands (fs, l, idx);
        for (int i = 0; i < n; ++i)
        {
            const int b = idx[(size_t) i];
            const float m = marginDb (fs.processing);
            const float ftf = frequencyTimeFactor (l.center (b));
            for (auto* slot : { claimsMid ? &plan.mid[(size_t) b] : nullptr, claimsSide ? &plan.side[(size_t) b] : nullptr })
            {
                if (slot == nullptr)
                    continue;
                auto& p = *slot;
                const bool tie = p.owner >= 0 && std::abs (m - p.marginDb) < 1.0e-6f;
                const bool better = p.owner < 0 || (! tie && m < p.marginDb)
                                    || (tie && fs.intensity > s.focus[(size_t) p.owner].intensity);
                if (! better)
                    continue;
                p.owner = f;
                p.det = det;
                p.marginDb = m;
                p.ratio = ratioFor (fs.intensity);
                p.kneeDb = knee;
                p.attackMs = baseAttack * tf * ftf;
                p.releaseMs = baseRelease * tf * ftf;
            }
        }
    }
    return plan;
}

// ---- learned reference profile (SPEC 4.3) ----
// Always learned on the 32 1/6-octave bands from 397 Hz to 16 kHz, whatever layout is in use.
// Every layout's bands are made of whole profile bands (a 1/3-octave band = two of them, power
// summed), so a profile converts exactly to any resolution / analysis range.
constexpr int kProfileBands = 32;
inline float profileEdgeHz (int k) { return (float) (2000.0 * std::pow (2.0, -7.0 / 3.0 + k / 6.0)); }
/** Index of the first profile band of a layout's band 0. */
inline int profileOffset (const Layout& l) { return l.extended ? 0 : 12; }

} // namespace sasari
