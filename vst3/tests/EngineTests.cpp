// Unit tests for the DSP engine (no JUCE, no host): run with ctest or directly.

#include "../src/dsp/Engine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

/** Runs `seconds` of generated audio through a fresh engine in odd-sized blocks. */
Stereo run (const Settings& s, double sr, double seconds, const Gen& gen, Stereo* input = nullptr, bool bypass = false)
{
    auto e = std::make_unique<Engine>();
    e->prepare (sr, 2);
    e->setSettings (s);
    const int total = (int) (sr * seconds);
    Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    if (input != nullptr)
        *input = out;
    for (int i = 0; i < total; ++i)
    {
        float l, r;
        gen (i, i / sr, l, r);
        out.l[(size_t) i] = l;
        out.r[(size_t) i] = r;
        if (input != nullptr)
        {
            input->l[(size_t) i] = l;
            input->r[(size_t) i] = r;
        }
    }
    int i = 0;
    int block = 97;
    while (i < total)
    {
        const int len = std::min (block, total - i);
        float* ch[2] = { out.l.data() + i, out.r.data() + i };
        e->process (ch, 2, len, bypass);
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

double maxDiffDelayed (const std::vector<float>& out, const std::vector<float>& in, int delay, int skip)
{
    double m = 0;
    for (size_t i = (size_t) (delay + skip); i < out.size(); ++i)
        m = std::max (m, (double) std::abs (out[i] - in[i - (size_t) delay]));
    return m;
}

Settings allOff()
{
    Settings s;
    for (auto& f : s.focus)
        f.enabled = false;
    s.autoLevel = false;
    return s;
}

Settings vocalOnly (float processing, float intensity, bool autoLevel = false)
{
    Settings s = allOff();
    s.focus[1] = FocusSettings { true, 5000.0f, 10000.0f, processing, intensity };
    s.autoLevel = autoLevel;
    return s;
}

Gen bedPlusHigh (double highAmp, double highHz = 7000.0)
{
    return [=] (int, double t, float& l, float& r) {
        const double v = 0.3 * std::sin (2 * kPi * 500 * t) + highAmp * std::sin (2 * kPi * highHz * t);
        l = r = (float) v;
    };
}

void testIdentityAndLatency()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Engine e;
        e.prepare (sr, 2);
        const int expected = sr <= 50000 ? 512 : (sr <= 100000 ? 1024 : 2048);
        check (e.latency() == expected, "latency at " + std::to_string ((int) sr));

        Stereo in;
        auto noise = [] (int i, double, float& l, float& r) {
            l = (float) std::sin (i * 0.37) * 0.4f + (float) std::sin (i * 1.91) * 0.2f;
            r = (float) std::cos (i * 0.53) * 0.3f;
        };
        auto out = run (allOff(), sr, 0.5, noise, &in);
        const double d = std::max (maxDiffDelayed (out.l, in.l, expected, 0), maxDiffDelayed (out.r, in.r, expected, 0));
        check (d < 1e-4, "transparent when nothing is enabled at " + std::to_string ((int) sr) + " (max diff " + std::to_string (d) + ")");

        auto byp = run (vocalOnly (100, 100), sr, 0.5, noise, &in, true);
        check (maxDiffDelayed (byp.l, in.l, expected, 0) < 1e-4, "bypass keeps latency and sound");
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
    Settings s; // defaults, fixed estimate for a deterministic check
    s.autoLevel = false;
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
    check (maxDiffDelayed (out.l, in.l, 512, 0) < 1e-4, "mix 0 is transparent");
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
    std::printf ("  filter: 500 Hz %.1f dB, 7k %.1f dB\n", lo, hi);
    check (lo < -60, "filter mutes outside the target");
    check (std::abs (hi - 20 * std::log10 (0.3)) < 1.0, "filter passes the target band unprocessed");
}

void testMidSideModes()
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
    s.channels = ChannelMode::Mid;
    auto mid = run (s, sr, 2.0, sideHigh);
    s.channels = ChannelMode::Side;
    auto side = run (s, sr, 2.0, sideHigh);
    auto ref = run (allOff(), sr, 2.0, sideHigh);
    const double midHi = toneDb (mid.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    const double sideHi = toneDb (side.l, sr, 7000, 1, 2) - toneDb (ref.l, sr, 7000, 1, 2);
    std::printf ("  side-only 7k: Mid mode %.2f dB, Side mode %.1f dB\n", midHi, sideHi);
    check (std::abs (midHi) < 0.5, "Mid mode leaves side content alone");
    check (sideHi < -10, "Side mode reduces side content");
}

void testAutoLevelFollowsTheMusic()
{
    const double sr = 48000;
    // Steady loud 7 kHz: at first reduced, then learned as "usual" and released.
    Settings s = vocalOnly (60, 100, true);
    auto out = run (s, sr, 40.0, bedPlusHigh (0.3));
    auto ref = run (allOff(), sr, 40.0, bedPlusHigh (0.3));
    const double early = toneDb (out.l, sr, 7000, 0.5, 1.5) - toneDb (ref.l, sr, 7000, 0.5, 1.5);
    const double late = toneDb (out.l, sr, 7000, 38, 40) - toneDb (ref.l, sr, 7000, 38, 40);
    std::printf ("  auto, steady 7k: early %.1f dB, after 38 s %.1f dB\n", early, late);
    check (early < -3, "steady loud high reduced before it is learned");
    check (late > early + 3, "steady high treated as usual once learned");

    // Short bursts on top of a learned bed stay reduced.
    auto bursts = [] (int, double t, float& l, float& r) {
        const bool on = std::fmod (t, 0.5) < 0.06;
        const double v = 0.3 * std::sin (2 * kPi * 500 * t) + 0.01 * std::sin (2 * kPi * 7000 * t)
                         + (on ? 0.3 * std::sin (2 * kPi * 7100 * t) : 0.0);
        l = r = (float) v;
    };
    auto b = run (s, sr, 30.0, bursts);
    auto bref = run (allOff(), sr, 30.0, bursts);
    double sumOut = 0, sumRef = 0;
    for (int k = 50; k < 59; ++k) // bursts between 25 s and 29.5 s
    {
        const double t0 = k * 0.5 + 0.01, t1 = t0 + 0.04;
        sumOut += std::pow (10.0, toneDb (b.l, sr, 7100, t0, t1) / 10);
        sumRef += std::pow (10.0, toneDb (bref.l, sr, 7100, t0, t1) / 10);
    }
    const double burstDb = 10 * std::log10 (sumOut / sumRef);
    std::printf ("  auto, 60 ms bursts after 25 s: %.1f dB\n", burstDb);
    check (burstDb < -3, "bursts still reduced with auto level");
}

void testResolutionAndSettingChangesAreSafe()
{
    Engine e;
    e.prepare (48000, 2);
    Settings s;
    std::vector<float> l (48000), r (48000);
    for (int i = 0; i < 48000; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) (0.3 * std::sin (i * 0.9) + 0.2 * std::sin (i * 0.07));
    bool finite = true;
    for (int blk = 0; blk < 48000 / 480; ++blk)
    {
        s.bandsPerOctave = (blk / 10) % 2 ? 6 : 3;
        s.listen = (Listen) ((blk / 7) % 3);
        s.channels = (ChannelMode) ((blk / 13) % 3);
        s.focus[0].lowHz = 1000.0f + blk * 150.0f;
        s.shelfOn = blk % 2;
        e.setSettings (s);
        float* ch[2] = { l.data() + blk * 480, r.data() + blk * 480 };
        e.process (ch, 2, 480);
        for (int i = 0; i < 480; ++i)
            finite = finite && std::isfinite (ch[0][i]) && std::isfinite (ch[1][i]) && std::abs (ch[0][i]) < 4.0f;
    }
    check (finite, "random setting changes keep output finite and bounded");
    check (e.meters.numBands.load() == 10 || e.meters.numBands.load() == 20, "meters report band count");
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
    check (maxDiffDelayed (x, in, 512, 0) < 1e-4, "mono transparent");
}

void testPlanMatchesAndroidMapping()
{
    Layout third (3), sixth (6);
    check (third.numBands == 10 && sixth.numBands == 20, "band counts");
    check (std::abs (third.edges[0] - 1587.4f) < 0.5f && std::abs (third.edges[10] - 16000.0f) < 0.5f, "edges");
    Settings s;
    auto plan = makePlan (s, third);
    // Defaults: guitar bands 2..4, vocal 5..7 (vocal wins 7 over hi-hat), hi-hat 8..9.
    check (plan[2].owner == 0 && plan[4].owner == 0, "guitar bands");
    check (plan[5].owner == 1 && plan[7].owner == 1, "vocal bands");
    check (plan[8].owner == 2 && plan[9].owner == 2, "hihat bands");
    check (plan[0].owner == -1, "lowest band untouched");
    check (plan[9].attackMs < plan[8].attackMs, "higher band reacts faster");
    check (std::abs (compressorReductionDb (12, 2, 0) - 6) < 1e-4f, "compressor curve");
}
} // namespace

int main()
{
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {
        { "plan matches Android mapping", testPlanMatchesAndroidMapping },
        { "identity and latency", testIdentityAndLatency },
        { "loud high reduced", testLoudHighReduced },
        { "range caps reduction", testRangeCapsReduction },
        { "defaults react only to loud highs", testDefaultsReactOnlyToLoudHighs },
        { "mix 0 transparent", testMixZeroIsTransparent },
        { "delta = removed part", testDeltaIsWhatWasRemoved },
        { "filter solos target", testFilterSolosSelectedTarget },
        { "mid/side modes", testMidSideModes },
        { "auto level", testAutoLevelFollowsTheMusic },
        { "setting changes are safe", testResolutionAndSettingChangesAreSafe },
        { "mono", testMono },
    };
    for (const auto& t : tests)
    {
        std::printf ("[%s]\n", t.name);
        t.fn();
    }
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
