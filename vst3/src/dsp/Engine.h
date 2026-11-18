#pragma once

// Spectral harshness suppressor (SPEC.md section 4).
//
// Short-time Fourier transform (sqrt-Hann, 75 % overlap). Frames are ~10.7 ms for the
// standard analysis range (1.6-16 kHz) and ~21.3 ms for the extended range (400 Hz-16 kHz).
// For every 1/3- or 1/6-octave band it measures how far the band sticks out, either above
// its usual level, above its neighbours, or a blend of both (SELECTIVITY), follows that with
// target-specific attack/release and turns only that band down while it sticks out.
// The frame length is reported as latency, so the gain is in place the moment a sibilant
// starts (lookahead).
//
// Real-time safe after prepare(): process() and setSettings() never allocate or lock,
// including when the analysis range (and with it the frame size) changes.

#include "Fft.h"
#include "Mapping.h"

#include <atomic>
#include <vector>

namespace sasari
{

class Engine
{
public:
    static constexpr int kSpecPoints = 96;   // spectrum display points, 100 Hz - 20 kHz
    static constexpr float kSpecLowHz = 100.0f, kSpecHighHz = 20000.0f;
    static constexpr float kLearnSeconds = 8.0f;

    Engine();

    /** Allocates everything for both frame sizes. numChannels is 1 or 2. */
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Latency in samples (one frame); changes with the analysis range. */
    int latency() const { return n; }
    /** Latency the engine would have for a given analysis range at the prepared sample rate. */
    int latencyFor (bool extendedRange) const { return extendedRange ? nLarge : nSmall; }

    /** Call from the audio thread before process() when settings may have changed. */
    void setSettings (const Settings& s);
    const Settings& getSettings() const { return settings; }
    const Layout& getLayout() const { return layout; }

    /** In-place processing. bypass keeps the latency but leaves the sound untouched. */
    void process (float* const* io, int numChannels, int numSamples, bool bypass = false);

    // ---- learning (any thread) ----
    /** Start learning the usual levels for kLearnSeconds; the result is frozen afterwards. */
    void requestLearn() { learnRequested.store (true); }

    struct Profile
    {
        std::array<float, kProfileBands> db {}; // 1/6-octave band levels (dBFS), NaN = unknown
        bool valid = false;
    };
    Profile getProfile() const;
    void setProfile (const Profile& p);

    // ---- metering, written by the audio thread, read by the UI ----
    struct Meters
    {
        std::atomic<int> numBands { 10 };
        std::array<std::atomic<float>, kMaxBands> overDb {}; // how far the band sticks out (detector)
        std::array<std::atomic<float>, kMaxBands> grDb {};   // current gain reduction
        std::array<std::atomic<float>, kSpecPoints> specIn {}, specOut {}; // dB, 0 = full-scale sine
        std::atomic<float> learnedSeconds { 0.0f };         // auto level
        std::atomic<float> learnProgress { -1.0f };         // 0..1 while learning, -1 idle
        std::atomic<bool> profileValid { false };
    };
    Meters meters;

private:
    void configure (bool extended);
    void rebuildLayout();
    void rebuildBinMaps();
    void refreshFrozenRefs();
    void finishLearning();
    void processFrame (bool bypass);

    double sr = 48000.0;
    int numCh = 2;
    int nSmall = 512, nLarge = 1024;
    int n = 512, hop = 128, half = 256;
    float hopSec = 128.0f / 48000.0f;
    int pos = 0;
    bool configuredExtended = false;
    int frameCounter = 0;

    Fft fftSmall, fftLarge;
    const Fft* fft = &fftSmall;
    std::vector<float> windowSmall, windowLarge;
    const std::vector<float>* window = &windowSmall;
    std::vector<float> inBuf[2], outBuf[2], re[2], im[2];

    Settings settings;
    Layout layout;
    std::array<BandPlan, kMaxBands> plan {};
    int layoutPerOctave = 0;
    bool layoutExtended = false;

    // per band state (dB)
    std::array<float, kMaxBands> env {}, ref {}, model {}, frozen {}, gr {}, level {}, over {};
    float longRmsDb = kAssumedRmsDb;
    float learnedSec = 0.0f;

    // learning
    std::atomic<bool> learnRequested { false };
    bool learning = false;
    float learnTime = 0.0f;
    int learnFrames = 0;
    std::array<double, kProfileBands> learnSum {};
    std::array<std::atomic<float>, kProfileBands> profileDb {};
    std::array<int, kProfileBands> profBinLo {}, profBinHi {};
    std::atomic<int> profileVersion { 0 };
    int profileVersionSeen = -1;

    // per bin maps (sized for the large frame)
    std::vector<float> binHz, binT, binShelf, binSolo, binGain, binGainOther;
    std::vector<int> binSeg;
    std::array<float, kMaxBands + 2> cpHz {}, cpDb {};
    std::array<int, kMaxBands> bandBinLo {}, bandBinHi {};
    std::array<int, kSpecPoints> specLo {}, specHi {};
};

} // namespace sasari
