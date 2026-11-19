// Unit tests for the DSP engine and A/B logic (no JUCE, no host). See SPEC.md section 8.

#include "../src/AbSlots.h"
#include "../src/dsp/Engine.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

// ---- allocation counter (for the real-time safety test) ----
static std::atomic<bool> gCountAllocs { false };
static std::atomic<long> gAllocs { 0 };
void* operator new (std::size_t size)
{
    if (gCountAllocs.load())
        gAllocs.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using namespace sasari;

namespace
{
int failures = 0;
int checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

constexpr double kPi = 3.14159265358979323846;

struct Stereo
{
    std::vector<float> l, r;
};

using Gen = std::function<void (int i, double t, float& l, float& r)>;
using Hook = std::function<void (Engine&, double t)>;

/** Runs generated audio through a fresh engine in odd-sized blocks. */
Stereo run (const Settings& s, double sr, double seconds, const Gen& gen, Stereo* input = nullptr,
            bool bypass = false, const Hook& hook = nullptr, Engine* keep = nullptr)
{
    auto owned = std::make_unique<Engine>();
    Engine& e = keep != nullptr ? *keep : *owned;
    if (keep == nullptr)
        e.prepare (sr, 2);
    e.setSettings (s);
    const int total = (int) (sr * seconds);
    Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    for (int i = 0; i < total; ++i)
    {
        float l, r;
        gen (i, i / sr, l, r);
        out.l[(size_t) i] = l;
        out.r[(size_t) i] = r;
    }
    if (input != nullptr)
        *input = out;
    int i = 0;
    int block = 97;
    while (i < total)
    {
        const int len = std::min (block, total - i);
        if (hook)
            hook (e, i / sr);
        float* ch[2] = { out.l.data() + i, out.r.data() + i };
        e.process (ch, 2, len, bypass);
        i += len;
        block = block == 97 ? 512 : (block == 512 ? 33 : 97);
    }
    return out;
}

/** Level (dB, amplitude) of a sine at freq within [from, to) seconds (Goertzel). */
double toneDb (const std::vector<float>& x, double sr, double freq, double from, double to)
{
    const int a = (int) (from * sr), b = std::min ((int) (to * sr), (int) x.size());
    const double w = 2.0 * kPi * freq / sr;
    double re = 0, im = 0;
    for (int i = a; i < b; ++i)
    {
        re += x[(size_t) i] * std::cos (w * i);
        im -= x[(size_t) i] * std::sin (w * i);
    }
    const double amp = 2.0 * std::sqrt (re * re + im * im) / (b - a);
    return 20.0 * std::log10 (amp + 1e-12);
}

/** Power (dB) of a signal in [f0, f1) Hz averaged over [from, to) seconds. */
double bandDb (const std::vector<float>& x, double sr, double f0, double f1, double from, double to)
{
    Fft fft;
    const int n = 4096;
    fft.prepare (n);
    std::vector<float> re ((size_t) n), im ((size_t) n);
    double p = 0;
    int frames = 0;
    for (int start = (int) (from * sr); start + n <= (int) (to * sr); start += n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / n));
            re[(size_t) i] = x[(size_t) (start + i)] * w;
            im[(size_t) i] = 0;
        }
        fft.forward (re.data(), im.data());
        for (int k = (int) (f0 * n / sr); k < (int) (f1 * n / sr); ++k)
            p += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
        ++frames;
    }
    return 10 * std::log10 (p / std::max (1, frames) + 1e-20);
}

double maxDiffDelayed (const std::vector<float>& out, const std::vector<float>& in, int delay)
{
    double m = 0;
    for (size_t i = (size_t) delay; i < out.size(); ++i)
        m = std::max (m, (double) std::abs (out[i] - in[i - (size_t) delay]));
    return m;
}

Settings allOff()
{
    Settings s;
    for (auto& f : s.focus)
        f.enabled = false;
    s.reference = Reference::Fixed;
    s.selectivity = 0.0f;
    return s;
}

Settings vocalOnly (float processing, float intensity, Reference ref = Reference::Fixed, float selectivity = 0.0f)
{
    Settings s = allOff();
    s.focus[1] = FocusSettings { true, 5000.0f, 10000.0f, processing, intensity };
    s.reference = ref;
    s.selectivity = selectivity;
    return s;
}

Gen bedPlusHigh (double highAmp, double highHz = 7000.0)
{
    return [=] (int, double t, float& l, float& r) {
        const double v = 0.3 * std::sin (2 * kPi * 500 * t) + highAmp * std::sin (2 * kPi * highHz * t);
        l = r = (float) v;
    };
}

Gen noisePlusTone (double noiseAmp, double toneAmp, double toneHz, unsigned seed = 1)
{
    auto rng = std::make_shared<std::mt19937> (seed);
    return [=] (int, double t, float& l, float& r) {
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        const double v = noiseAmp * d (*rng) + toneAmp * std::sin (2 * kPi * toneHz * t);
        l = r = (float) v;
    };
}

// ======================================================================== v1 behaviour

void testIdentityAndLatency()
{
    for (bool ext : { false, true })
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Engine e;
            e.prepare (sr, 2);
            Settings s = allOff();
            s.extendedRange = ext;
            e.setSettings (s);
            const int base = sr <= 50000 ? 512 : (sr <= 100000 ? 1024 : 2048);
            const int expected = ext ? base * 2 : base;
            check (e.latency() == expected, "latency at " + std::to_string ((int) sr) + (ext ? " extended" : ""));

            Stereo in;
            auto noise = [] (int i, double, float& l, float& r) {
                l = (float) std::sin (i * 0.37) * 0.4f + (float) std::sin (i * 1.91) * 0.2f;
                r = (float) std::cos (i * 0.53) * 0.3f;
            };
            auto out = run (s, sr, 0.5, noise, &in);
            const double d = std::max (maxDiffDelayed (out.l, in.l, expected), maxDiffDelayed (out.r, in.r, expected));
            check (d < 1e-4, "transparent when nothing is enabled at " + std::to_string ((int) sr) + (ext ? " ext" : "")
                                 + " (max diff " + std::to_string (d) + ")");
            Settings v = vocalOnly (100, 100);
            v.extendedRange = ext;
            auto byp = run (v, sr, 0.5, noise, &in, true);
            check (maxDiffDelayed (byp.l, in.l, expected) < 1e-4, "bypass keeps latency and sound");
        }
}

void testLoudHighReduced()
{
    const double sr = 48000;
    auto out = run (vocalOnly (100, 100), sr, 2.0, bedPlusHigh (0.3));
    auto ref = run (allOff(), sr, 2.0, bedPlusHigh (0.3));
    const double hi = toneDb (out.l, sr, 7000, 1.0, 2.0) - toneDb (ref.l, sr, 7000, 1.0, 2.0);
    const double lo = toneDb (out.l, sr, 500, 1.0, 2.0) - toneDb (ref.l, sr, 500, 1.0, 2.0);
    std::printf ("  loud 7 kHz, strongest settings: 7k %.1f dB, 500 Hz %.2f dB\n", hi, lo);
    check (hi < -10.0, "loud 7 kHz pulled down");
    check (std::abs (lo) < 0.5, "500 Hz untouched");
}

void testRangeCapsReduction()
{
    const double sr = 48000;
    Settings s = vocalOnly (100, 100);
    s.rangeDb = 6.0f;
    auto out = run (s, sr, 2.0, bedPlusHigh (0.3));
    auto ref = run (allOff(), sr, 2.0, bedPlusHigh (0.3));
    const double hi = toneDb (out.l, sr, 7000, 1.0, 2.0) - toneDb (ref.l, sr, 7000, 1.0, 2.0);
    std::printf ("  range 6 dB: 7k %.1f dB\n", hi);
    check (hi < -4.5 && hi > -7.5, "range caps the reduction near 6 dB");
}

void testDefaultsReactOnlyToLoudHighs()
{
    const double sr = 48000;
    Settings s; // defaults (incl. selectivity 30 %), fixed reference for a deterministic check
    s.reference = Reference::Fixed;
    auto quiet = run (s, sr, 2.0, bedPlusHigh (0.003));
    auto quietRef = run (allOff(), sr, 2.0, bedPlusHigh (0.003));
    auto loud = run (s, sr, 2.0, bedPlusHigh (0.3));
    auto loudRef = run (allOff(), sr, 2.0, bedPlusHigh (0.3));
    const double q = toneDb (quiet.l, sr, 7000, 1, 2) - toneDb (quietRef.l, sr, 7000, 1, 2);
    const double l = toneDb (loud.l, sr, 7000, 1, 2) - toneDb (loudRef.l, sr, 7000, 1, 2);
    std::printf ("  defaults: quiet 7k %.2f dB, loud 7k %.1f dB\n", q, l);
    check (std::abs (q) < 1.0, "quiet highs left alone");
    check (l < -3.0, "loud highs reduced");
}

void testMixZeroIsTransparent()
{
    Settings s = vocalOnly (100, 100);
    s.mix = 0.0f;
    Stereo in;
    auto out = run (s, 48000, 1.0, bedPlusHigh (0.3), &in);
    check (maxDiffDelayed (out.l, in.l, 512) < 1e-4, "mix 0 is transparent");
}

void testDeltaIsWhatWasRemoved()
{
    Settings s = vocalOnly (100, 100);
    Stereo in;
    auto normal = run (s, 48000, 1.5, bedPlusHigh (0.3), &in);
    s.listen = Listen::Delta;
    auto delta = run (s, 48000, 1.5, bedPlusHigh (0.3));
    double m = 0;
    for (size_t i = 512; i < in.l.size(); ++i)
        m = std::max (m, (double) std::abs (normal.l[i] + delta.l[i] - in.l[i - 512]));
    check (m < 1e-4, "normal + delta == input (max diff " + std::to_string (m) + ")");
    check (toneDb (delta.l, 48000, 500, 0.5, 1.5) < -60, "delta contains no 500 Hz");
}

void testFilterSolosSelectedTarget()
{
    Settings s = vocalOnly (50, 50);
    s.selected = 1;
    s.listen = Listen::Filter;
    auto out = run (s, 48000, 1.0, bedPlusHigh (0.3));
    const double lo = toneDb (out.l, 48000, 500, 0.3, 1.0);
    const double hi = toneDb (out.l, 48000, 7000, 0.3, 1.0);
    check (lo < -60, "filter mutes outside the target");
    check (std::abs (hi - 20 * std::log10 (0.3)) < 1.0, "filter passes the target band unprocessed");
}

void testPerTargetChannels()
{
    const double sr = 48000;
    // 7 kHz only in the side signal (L = -R), 500 Hz in the middle.
    auto sideHigh = [] (int, double t, float& l, float& r) {
        const double m = 0.3 * std::sin (2 * kPi * 500 * t);
        const double s = 0.3 * std::sin (2 * kPi * 7000 * t);
        l = (float) (m + s);
        r = (float) (m - s);
    };
    Settings s = vocalOnly (100, 100);
    s.focus[1].channel = ChannelMode::Mid;
    auto mid = run (s, sr, 2.0, sideHigh);
    s.focus[1].channel = ChannelMode::Side;
    auto side = run (s, sr, 2.0, sideHigh);
    auto ref = run (allOff(), sr, 2.0, sideHigh);
    const double midHi = toneDb (mid.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    const double sideHi = toneDb (side.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    std::printf ("  side-only 7k: Mid target %.2f dB, Side target %.1f dB\n", midHi, sideHi);
    check (std::abs (midHi) < 0.5, "Mid target leaves side content alone");
    check (sideHi < -10, "Side target reduces side content");

    // Vocal on Mid and hi-hat on Side at the same time, overlapping at 9 kHz:
    // a centred 9 kHz "sibilant" and a wide 9.3 kHz "cymbal" are both pulled down,
    // and a centred 13 kHz tone (hi-hat range, but in the middle) is left alone.
    auto mixGen = [] (int, double t, float& l, float& r) {
        const double m = 0.3 * std::sin (2 * kPi * 9000 * t) + 0.2 * std::sin (2 * kPi * 13000 * t) + 0.2 * std::sin (2 * kPi * 500 * t);
        const double sd = 0.3 * std::sin (2 * kPi * 9300 * t);
        l = (float) (m + sd);
        r = (float) (m - sd);
    };
    Settings both = allOff();
    both.focus[1] = FocusSettings { true, 5000.0f, 10000.0f, 100.0f, 100.0f };
    both.focus[1].channel = ChannelMode::Mid;
    both.focus[2] = FocusSettings { true, 8000.0f, 16000.0f, 100.0f, 100.0f };
    both.focus[2].channel = ChannelMode::Side;
    auto out = run (both, sr, 2.0, mixGen);
    auto bref = run (allOff(), sr, 2.0, mixGen);
    auto midOf = [] (const Stereo& x) { std::vector<float> m (x.l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (x.l[i] + x.r[i]); return m; };
    auto sideOf = [] (const Stereo& x) { std::vector<float> m (x.l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (x.l[i] - x.r[i]); return m; };
    const auto om = midOf (out), os = sideOf (out), rm = midOf (bref), rs = sideOf (bref);
    const double m9 = toneDb (om, sr, 9000, 1, 2) - toneDb (rm, sr, 9000, 1, 2);
    const double s93 = toneDb (os, sr, 9300, 1, 2) - toneDb (rs, sr, 9300, 1, 2);
    const double m13 = toneDb (om, sr, 13000, 1, 2) - toneDb (rm, sr, 13000, 1, 2);
    const double m500 = toneDb (om, sr, 500, 1, 2) - toneDb (rm, sr, 500, 1, 2);
    std::printf ("  vocal=Mid + hi-hat=Side: mid 9k %.1f dB, side 9.3k %.1f dB, mid 13k %.2f dB, mid 500 %.2f dB\n", m9, s93, m13, m500);
    check (m9 < -6, "centred sibilant reduced by the Mid vocal target");
    check (s93 < -6, "wide cymbal reduced by the Side hi-hat target in the same band");
    check (std::abs (m13) < 0.5, "centred content in the hi-hat range untouched by a Side target");
    check (std::abs (m500) < 0.5, "500 Hz untouched");
}

void testStereoTargetKeepsTheImage()
{
    // A loud 7 kHz only on the left: a stereo (linked) target must reduce both sides equally,
    // so a quiet 7 kHz on the right drops by the same amount (no image shift).
    const double sr = 48000;
    auto gen = [] (int, double t, float& l, float& r) {
        l = (float) (0.3 * std::sin (2 * kPi * 7000 * t) + 0.2 * std::sin (2 * kPi * 500 * t));
        r = (float) (0.03 * std::sin (2 * kPi * 7000 * t) + 0.2 * std::sin (2 * kPi * 500 * t));
    };
    auto out = run (vocalOnly (100, 100), sr, 2, gen);
    auto ref = run (allOff(), sr, 2, gen);
    const double dl = toneDb (out.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    const double dr = toneDb (out.r, sr, 7000, 1, 2) - toneDb (ref.r, sr, 7000, 1, 2);
    std::printf ("  stereo target, loud left: left %.1f dB, right %.1f dB\n", dl, dr);
    check (dl < -6, "left reduced");
    check (std::abs (dl - dr) < 0.5, "right reduced by the same amount (image kept)");
}

void testAutoLevelFollowsTheMusic()
{
    const double sr = 48000;
    Settings s = vocalOnly (60, 100, Reference::Auto);
    auto out = run (s, sr, 40.0, bedPlusHigh (0.3));
    auto ref = run (allOff(), sr, 40.0, bedPlusHigh (0.3));
    const double early = toneDb (out.l, sr, 7000, 0.5, 1.5) - toneDb (ref.l, sr, 7000, 0.5, 1.5);
    const double late = toneDb (out.l, sr, 7000, 38, 40) - toneDb (ref.l, sr, 7000, 38, 40);
    std::printf ("  auto, steady 7k: early %.1f dB, after 38 s %.1f dB\n", early, late);
    check (early < -3, "steady loud high reduced before it is learned");
    check (late > early + 3, "steady high treated as usual once learned");

    auto bursts = [] (int, double t, float& l, float& r) {
        const bool on = std::fmod (t, 0.5) < 0.06;
        const double v = 0.3 * std::sin (2 * kPi * 500 * t) + 0.01 * std::sin (2 * kPi * 7000 * t)
                         + (on ? 0.3 * std::sin (2 * kPi * 7100 * t) : 0.0);
        l = r = (float) v;
    };
    auto b = run (s, sr, 30.0, bursts);
    auto bref = run (allOff(), sr, 30.0, bursts);
    double sumOut = 0, sumRef = 0;
    for (int k = 50; k < 59; ++k)
    {
        const double t0 = k * 0.5 + 0.01, t1 = t0 + 0.04;
        sumOut += std::pow (10.0, toneDb (b.l, sr, 7100, t0, t1) / 10);
        sumRef += std::pow (10.0, toneDb (bref.l, sr, 7100, t0, t1) / 10);
    }
    const double burstDb = 10 * std::log10 (sumOut / sumRef);
    std::printf ("  auto, 60 ms bursts after 25 s: %.1f dB\n", burstDb);
    check (burstDb < -3, "bursts still reduced with auto level");
}

void testMono()
{
    Engine e;
    e.prepare (48000, 1);
    e.setSettings (allOff());
    std::vector<float> x (24000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) std::sin (i * 0.2);
    auto in = x;
    float* ch[1] = { x.data() };
    e.process (ch, 1, (int) x.size());
    check (maxDiffDelayed (x, in, 512) < 1e-4, "mono transparent");
}

void testPlanAndLayouts()
{
    Layout third (3), sixth (6), ext3 (3, true), ext6 (6, true);
    check (third.numBands == 10 && sixth.numBands == 20 && ext3.numBands == 16 && ext6.numBands == 32, "band counts");
    check (std::abs (third.edges[0] - 1587.4f) < 0.5f && std::abs (third.edges[10] - 16000.0f) < 0.5f, "standard edges");
    check (std::abs (ext3.edges[0] - 396.9f) < 0.5f && std::abs (ext6.edges[32] - 16000.0f) < 0.5f, "extended edges");
    Settings s;
    const auto plan = makePlan (s, third).mid;
    check (plan[2].owner == 0 && plan[4].owner == 0, "guitar bands");
    check (plan[5].owner == 1 && plan[7].owner == 1, "vocal bands (vocal wins the overlap)");
    check (plan[8].owner == 2 && plan[9].owner == 2, "hihat bands");
    check (plan[0].owner == -1, "lowest band untouched");
    check (plan[9].attackMs < plan[8].attackMs, "higher band reacts faster");
    const auto both = makePlan (s, third);
    bool linked = true;
    for (int b = 0; b < third.numBands; ++b)
        linked = linked && both.mid[(size_t) b].owner == both.side[(size_t) b].owner && both.mid[(size_t) b].det == Det::LR;
    check (linked, "stereo targets claim Mid and Side alike and listen to L/R");
    Settings ms = s;
    ms.focus[1].channel = ChannelMode::Mid;
    ms.focus[2].channel = ChannelMode::Side;
    const auto split = makePlan (ms, third);
    check (split.mid[7].owner == 1 && split.side[7].owner == 2, "overlapping band: vocal on Mid, hi-hat on Side");
    check (split.mid[9].owner == -1 && split.side[5].owner == -1, "Mid / Side targets leave the other signal alone");
    check (split.mid[7].det == Det::M && split.side[7].det == Det::S, "Mid / Side targets listen to their own signal");
    // Custom target: only in the extended layout, with its own times.
    s.focus[3].enabled = true;
    check (makePlan (s, third).mid[0].owner == -1, "custom 600-1200 Hz is outside the standard layout");
    auto ep = makePlan (s, ext3).mid;
    int customBands = 0;
    for (int b = 0; b < ext3.numBands; ++b)
        if (ep[(size_t) b].owner == kCustom)
            ++customBands;
    check (customBands == 3, "custom covers three 1/3-octave bands in the extended layout");
    check (std::abs (compressorReductionDb (12, 2, 0) - 6) < 1e-4f, "compressor curve");
}

// ======================================================================== v2 features

void testSelectivityCatchesSteadyResonance()
{
    // A steady narrow tone that sticks out of noise: with auto level it is learned as "usual"
    // and released at 0 % selectivity, but still pulled down at 100 % (it sticks out of its neighbours).
    const double sr = 48000;
    auto gen = noisePlusTone (0.05, 0.1, 7000);
    auto ref = run (allOff(), sr, 30, noisePlusTone (0.05, 0.1, 7000));
    auto s0 = run (vocalOnly (60, 100, Reference::Auto, 0), sr, 30, noisePlusTone (0.05, 0.1, 7000));
    auto s100 = run (vocalOnly (60, 100, Reference::Auto, 100), sr, 30, noisePlusTone (0.05, 0.1, 7000));
    const double r0 = toneDb (s0.l, sr, 7000, 27, 30) - toneDb (ref.l, sr, 7000, 27, 30);
    const double r100 = toneDb (s100.l, sr, 7000, 27, 30) - toneDb (ref.l, sr, 7000, 27, 30);
    std::printf ("  steady resonance after 27 s: selectivity 0 %% %.1f dB, 100 %% %.1f dB\n", r0, r100);
    check (r0 > -2, "selectivity 0 %: learned resonance released");
    check (r100 < -6, "selectivity 100 %: resonance that sticks out of its neighbours reduced");
}

void testSelectivityIgnoresBroadBrightness()
{
    // Loud white noise (bright everywhere): above the fixed estimate, but no band sticks out of its neighbours.
    const double sr = 48000;
    auto ref = run (allOff(), sr, 3, noisePlusTone (0.3, 0.0, 1000));
    auto s0 = run (vocalOnly (60, 100, Reference::Fixed, 0), sr, 3, noisePlusTone (0.3, 0.0, 1000));
    auto s100 = run (vocalOnly (60, 100, Reference::Fixed, 100), sr, 3, noisePlusTone (0.3, 0.0, 1000));
    const double base = bandDb (ref.l, sr, 6500, 7500, 1, 3);
    const double r0 = bandDb (s0.l, sr, 6500, 7500, 1, 3) - base;
    const double r100 = bandDb (s100.l, sr, 6500, 7500, 1, 3) - base;
    std::printf ("  bright white noise 6.5-7.5 kHz: selectivity 0 %% %.1f dB, 100 %% %.1f dB\n", r0, r100);
    check (r0 < -6, "selectivity 0 %: broad brightness above the estimate is reduced");
    check (r100 > -2, "selectivity 100 %: broad brightness left alone");
}

void testExtendedRangeAndCustomTarget()
{
    const double sr = 48000;
    auto gen = [] (int, double t, float& l, float& r) {
        const double v = 0.3 * std::sin (2 * kPi * 600 * t) + 0.1 * std::sin (2 * kPi * 7000 * t)
                         + 0.2 * std::sin (2 * kPi * 150 * t);
        l = r = (float) v;
    };
    Settings s = allOff();
    s.focus[3] = FocusSettings { true, 500.0f, 800.0f, 100.0f, 100.0f, 5.0f, 100.0f };
    s.extendedRange = true;
    Engine e;
    e.prepare (sr, 2);
    e.setSettings (s);
    check (e.latency() == 1024, "extended range latency 1024 at 48 kHz");
    auto out = run (s, sr, 3, gen);
    auto ref = run (allOff(), sr, 3, gen);
    const double c600 = toneDb (out.l, sr, 600, 1.5, 3) - toneDb (ref.l, sr, 600, 1.5, 3);
    const double c7k = toneDb (out.l, sr, 7000, 1.5, 3) - toneDb (ref.l, sr, 7000, 1.5, 3);
    const double c150 = toneDb (out.l, sr, 150, 1.5, 3) - toneDb (ref.l, sr, 150, 1.5, 3);
    std::printf ("  extended + custom 500-800 Hz: 600 Hz %.1f dB, 7 kHz %.2f dB, 150 Hz %.2f dB\n", c600, c7k, c150);
    check (c600 < -6, "custom target reduces 600 Hz");
    check (std::abs (c7k) < 0.5 && std::abs (c150) < 0.5, "other frequencies untouched");

    s.extendedRange = false;
    auto std600 = run (s, sr, 3, gen);
    auto stdRef = run (allOff(), sr, 3, gen);
    const double c = toneDb (std600.l, sr, 600, 1.5, 3) - toneDb (stdRef.l, sr, 600, 1.5, 3);
    check (std::abs (c) < 0.5, "custom target outside the standard range does nothing");
}

void testLearnFreezeAndRestore()
{
    const double sr = 48000;
    Settings s = vocalOnly (60, 100, Reference::Learned);
    auto gen = bedPlusHigh (0.3);
    Engine e;
    e.prepare (sr, 2);
    // Before learning: no profile -> fixed estimate -> loud 7 kHz reduced.
    auto before = run (s, sr, 2, gen, nullptr, false, nullptr, &e);
    auto ref = run (allOff(), sr, 2, gen);
    const double b = toneDb (before.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    check (b < -6, "before learning: reduced against the estimate");
    check (! e.meters.profileValid.load(), "no profile yet");

    e.requestLearn();
    bool sawProgress = false;
    run (s, sr, Engine::kLearnSeconds + 1.0, gen, nullptr, false,
         [&] (Engine& en, double) { sawProgress = sawProgress || en.meters.learnProgress.load() > 0.5f; }, &e);
    check (sawProgress, "learn progress reported");
    check (e.meters.profileValid.load(), "profile learned");
    auto after = run (s, sr, 3, gen, nullptr, false, nullptr, &e);
    const double a = toneDb (after.l, sr, 7000, 2, 3) - toneDb (ref.l, sr, 7000, 1, 2);
    std::printf ("  learned: before %.1f dB, after learning %.1f dB\n", b, a);
    check (a > -2, "after learning the steady 7 kHz is 'usual' and left alone");

    // Restore into a fresh engine, and with a different layout.
    const auto profile = e.getProfile();
    for (int perOct : { 3, 6 })
    {
        Engine e2;
        e2.prepare (sr, 2);
        e2.setProfile (profile);
        Settings s2 = s;
        s2.bandsPerOctave = perOct;
        auto restored = run (s2, sr, 3, gen, nullptr, false, nullptr, &e2);
        const double r = toneDb (restored.l, sr, 7000, 1, 3) - toneDb (ref.l, sr, 7000, 1, 2);
        check (r > -2, "restored profile used (1/" + std::to_string (perOct) + " oct): " + std::to_string (r));
    }
    bool allFinite = true;
    for (int k = 0; k < kProfileBands; ++k)
        allFinite = allFinite && std::isfinite (profile.db[(size_t) Det::LR][(size_t) k]) && std::isfinite (profile.db[(size_t) Det::M][(size_t) k]);
    check (allFinite, "profile covers all 32 1/6-octave bands (L/R and Mid)");
    // Also usable with the extended range (learned in the standard range).
    Engine e3;
    e3.prepare (sr, 2);
    e3.setProfile (profile);
    Settings s3 = s;
    s3.extendedRange = true;
    auto ext = run (s3, sr, 3, gen, nullptr, false, nullptr, &e3);
    const double x = toneDb (ext.l, sr, 7000, 1.5, 3) - toneDb (ref.l, sr, 7000, 1, 2);
    check (x > -2, "restored profile used with the extended range: " + std::to_string (x));
}

void testSpectrumMeters()
{
    const double sr = 48000;
    Engine e;
    e.prepare (sr, 2);
    run (vocalOnly (100, 100), sr, 1.5, bedPlusHigh (0.3), nullptr, false, nullptr, &e);
    int peak = 0;
    for (int i = 1; i < Engine::kSpecPoints; ++i)
        if (e.meters.specIn[(size_t) i].load() > e.meters.specIn[(size_t) peak].load()
            && Engine::kSpecLowHz * std::pow (Engine::kSpecHighHz / Engine::kSpecLowHz, (i + 0.5f) / Engine::kSpecPoints) > 2000)
            peak = i;
    const double f = Engine::kSpecLowHz * std::pow (Engine::kSpecHighHz / Engine::kSpecLowHz, (peak + 0.5) / Engine::kSpecPoints);
    const float in = e.meters.specIn[(size_t) peak].load(), out = e.meters.specOut[(size_t) peak].load();
    std::printf ("  spectrum: high peak at %.0f Hz, in %.1f dB, out %.1f dB\n", f, in, out);
    check (f > 6500 && f < 7500, "input spectrum peaks at 7 kHz");
    check (in > -20 && in < 0, "input level about -10 dB for a 0.3 sine");
    check (in - out > 6, "output spectrum shows the reduction");
}

void testReferenceMeters()
{
    const double sr = 48000;
    const Layout layout (3, false);
    int b7 = -1;
    for (int b = 0; b < layout.numBands; ++b)
        if (layout.low (b) <= 7000.0f && 7000.0f < layout.high (b))
            b7 = b;
    check (b7 >= 0, "band holding 7 kHz found");

    // Before any audio (e.g. a DAW with no audio device) there is nothing to show.
    Engine fresh;
    fresh.prepare (sr, 2);
    fresh.setSettings (Settings {});
    check (std::isnan (fresh.meters.refDb[0][0].load()) && fresh.meters.levelDb[0][0].load() < -100.0f,
           "no reference shown before any audio");

    // Fixed estimate: the reference shown is the estimate, and the band level shows the tone.
    Engine e;
    e.prepare (sr, 2);
    run (vocalOnly (50, 50, Reference::Fixed), sr, 1.0, bedPlusHigh (0.3), nullptr, false, nullptr, &e);
    bool same = true, finite = true;
    for (int b = 0; b < e.meters.numBands.load(); ++b)
        for (int d = 0; d < kNumDet; ++d)
        {
            const float rf = e.meters.refDb[(size_t) d][(size_t) b].load(), est = e.meters.estimateDb[(size_t) d][(size_t) b].load();
            same = same && std::abs (rf - est) < 1.0e-3f;
            finite = finite && std::isfinite (rf) && std::isfinite (est) && std::isfinite (e.meters.levelDb[(size_t) d][(size_t) b].load());
        }
    check (same, "fixed: reference shown = estimate");
    check (finite, "reference-view meters are finite");
    const float lv = e.meters.levelDb[(size_t) Det::LR][(size_t) b7].load();
    std::printf ("  7 kHz band: level %.1f dB, estimate %.1f dB\n", lv, e.meters.estimateDb[(size_t) Det::LR][(size_t) b7].load());
    check (lv > -18.0f && lv < -9.0f, "band level shows the 0.3 sine (about -13.5 dB)");

    // Auto: a steady loud band pulls its reference up from the estimate.
    Engine e2;
    e2.prepare (sr, 2);
    run (vocalOnly (50, 50, Reference::Auto), sr, 6.0, bedPlusHigh (0.3), nullptr, false, nullptr, &e2);
    const float moved = e2.meters.refDb[(size_t) Det::LR][(size_t) b7].load() - e2.meters.estimateDb[(size_t) Det::LR][(size_t) b7].load();
    std::printf ("  auto: reference moved %+.1f dB from the estimate after 6 s\n", moved);
    check (moved > 3.0f, "auto: reference rises toward a steady loud band");
}

void testThresholdAndPerBandTrims()
{
    const double sr = 48000;
    auto gen = bedPlusHigh (0.3);
    const auto ref = run (allOff(), sr, 2, gen);
    auto cut = [&] (const Settings& s) {
        const auto out = run (s, sr, 2, gen);
        return toneDb (out.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    };
    const Settings base = vocalOnly (50, 100);
    Settings up = base, down = base;
    up.thresholdDb = 20.0f;
    down.thresholdDb = -10.0f;
    const double r0 = cut (base), rUp = cut (up), rDown = cut (down);
    std::printf ("  THRESHOLD: 0 dB %.1f, +20 dB %.1f, -10 dB %.1f\n", r0, rUp, rDown);
    check (r0 < -6, "loud 7 kHz reduced at THRESHOLD 0 dB");
    check (rUp > r0 + 4, "a higher THRESHOLD cuts less");
    check (rDown <= r0 + 0.1, "a lower THRESHOLD cuts at least as much");

    // Per-band trim raised only on the 1/3-octave band holding 7 kHz.
    const Layout layout (3, false);
    int b7 = -1;
    for (int b = 0; b < layout.numBands; ++b)
        if (layout.low (b) <= 7000.0f && 7000.0f < layout.high (b))
            b7 = b;
    Settings trimmed = base;
    const int per = profileBandsPer (layout), first = profileOffset (layout) + b7 * per;
    for (int j = 0; j < per; ++j)
        trimmed.bandTrimDb[(size_t) (first + j)] = 20.0f;
    const double rOff = cut (trimmed);
    trimmed.perBandThreshold = true;
    const double rOn = cut (trimmed);
    std::printf ("  trim +20 dB on the 7 kHz band: mode 'all bands' %.1f, 'per band' %.1f\n", rOff, rOn);
    check (std::abs (rOff - r0) < 0.3, "per-band trims are ignored in 'all bands' mode");
    check (rOn > r0 + 4, "a raised trim on the 7 kHz band cuts it less");

    const auto p0 = makePlan (base, layout), p1 = makePlan (trimmed, layout);
    check (p1.mid[(size_t) b7].owner == p0.mid[(size_t) b7].owner
               && std::abs (p1.mid[(size_t) b7].marginDb - p0.mid[(size_t) b7].marginDb - 20.0f) < 1.0e-3f,
           "the trim moves that band's threshold, same owner");
    check (p0.mid[(size_t) b7 - 1].owner >= 0 && std::abs (p1.mid[(size_t) b7 - 1].marginDb - p0.mid[(size_t) b7 - 1].marginDb) < 1.0e-3f,
           "the neighbouring band keeps its threshold");
    Settings half = base;
    half.perBandThreshold = true;
    half.bandTrimDb[(size_t) first] = 10.0f;
    check (std::abs (bandTrimDb (half, layout, b7) - 5.0f) < 1.0e-4f, "a 1/3-octave band uses the mean of its two 1/6-octave trims");
    Settings g = base;
    g.thresholdDb = 7.0f;
    check (std::abs (makePlan (g, layout).mid[(size_t) b7].marginDb - p0.mid[(size_t) b7].marginDb - 7.0f) < 1.0e-3f,
           "THRESHOLD moves every band's threshold");
}

void testAbSlots()
{
    AbSlots ab;
    AbSlots::Values a { { "x", 1.0f } }, b { { "x", 2.0f } };
    check (ab.switchTo (1, a) == nullptr && ab.active == 1, "first switch to B keeps current settings");
    const auto* back = ab.switchTo (0, b); // leaving B with x=2
    check (back != nullptr && back->at ("x") == 1.0f && ab.active == 0, "back to A restores A");
    const auto* toB = ab.switchTo (1, *back);
    check (toB != nullptr && toB->at ("x") == 2.0f, "B kept its own settings");
    ab.copyToOther (AbSlots::Values { { "x", 3.0f } });
    check (ab.slots[0].at ("x") == 3.0f && ab.slots[1].at ("x") == 3.0f, "copy to other");
}

void testNoAllocationWhileProcessing()
{
    Engine e;
    e.prepare (48000, 2);
    Settings s;
    e.setSettings (s);
    std::vector<float> l (480), r (480);
    long before = 0;
    gCountAllocs = true;
    before = gAllocs.load();
    bool finite = true;
    for (int blk = 0; blk < 600; ++blk)
    {
        for (int i = 0; i < 480; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) (0.3 * std::sin ((blk * 480 + i) * 0.9) + 0.2 * std::sin ((blk * 480 + i) * 0.07));
        s.bandsPerOctave = (blk / 10) % 2 ? 6 : 3;
        s.extendedRange = (blk / 25) % 2;
        s.listen = (Listen) ((blk / 7) % 3);
        for (int f = 0; f < kNumFocus; ++f)
            s.focus[(size_t) f].channel = (ChannelMode) ((blk / 13 + f) % 3);
        s.reference = (Reference) ((blk / 17) % 3);
        s.selectivity = (float) ((blk * 7) % 101);
        s.focus[0].lowHz = 500.0f + (float) ((blk * 150) % 15000);
        s.focus[3].enabled = blk % 3 == 0;
        s.shelfOn = blk % 2;
        if (blk == 300)
            e.requestLearn();
        e.setSettings (s);
        float* ch[2] = { l.data(), r.data() };
        e.process (ch, 2, 480);
        for (int i = 0; i < 480; ++i)
            finite = finite && std::isfinite (l[(size_t) i]) && std::abs (l[(size_t) i]) < 4.0f;
    }
    const long count = gAllocs.load() - before;
    gCountAllocs = false;
    std::printf ("  allocations during 600 blocks with constant setting changes: %ld\n", count);
    check (count == 0, "no allocation in setSettings/process");
    check (finite, "output finite and bounded");
}
} // namespace

int main()
{
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {
        { "plan and layouts", testPlanAndLayouts },
        { "identity and latency", testIdentityAndLatency },
        { "loud high reduced", testLoudHighReduced },
        { "range caps reduction", testRangeCapsReduction },
        { "defaults react only to loud highs", testDefaultsReactOnlyToLoudHighs },
        { "mix 0 transparent", testMixZeroIsTransparent },
        { "delta = removed part", testDeltaIsWhatWasRemoved },
        { "filter solos target", testFilterSolosSelectedTarget },
        { "per-target channels", testPerTargetChannels },
        { "stereo target keeps the image", testStereoTargetKeepsTheImage },
        { "auto level", testAutoLevelFollowsTheMusic },
        { "mono", testMono },
        { "selectivity: steady resonance", testSelectivityCatchesSteadyResonance },
        { "selectivity: broad brightness", testSelectivityIgnoresBroadBrightness },
        { "extended range + custom target", testExtendedRangeAndCustomTarget },
        { "learn, freeze, restore", testLearnFreezeAndRestore },
        { "spectrum meters", testSpectrumMeters },
        { "reference-level meters", testReferenceMeters },
        { "threshold and per-band trims", testThresholdAndPerBandTrims },
        { "A/B slots", testAbSlots },
        { "no allocation while processing", testNoAllocationWhileProcessing },
    };
    for (const auto& t : tests)
    {
        std::printf ("[%s]\n", t.name);
        t.fn();
    }
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
