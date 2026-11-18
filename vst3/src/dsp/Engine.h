#pragma once

// Spectral harshness suppressor.
//
// Short-time Fourier transform (sqrt-Hann, 75 % overlap, ~10.7 ms frames). For every
// 1/3- or 1/6-octave band from 1.6 to 16 kHz it follows the band level with target-specific
// attack/release, compares it with that band's usual level (learned from the music when
// auto level is on) and turns only that band down while it sticks out. The frame length is
// reported as latency, so the gain is in place the moment a sibilant starts (lookahead).
//
// Real-time safe after prepare(): no allocation, no locks in process().

#include "Fft.h"
#include "Mapping.h"

#include <atomic>
#include <vector>

namespace sasari
{

class Engine
{
public:
    Engine()
    {
        for (auto& v : meters.overDb)
            v.store (-100.0f); // "no signal" until audio has been processed
    }

    /** Allocates everything. numChannels is 1 or 2. */
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Latency in samples (one frame). */
    int latency() const { return n; }
    int fftSize() const { return n; }

    /** Call from the audio thread before process() when settings may have changed. */
    void setSettings (const Settings& s);
    const Settings& getSettings() const { return settings; }
    const Layout& getLayout() const { return layout; }

    /** In-place processing. bypass keeps the latency but leaves the sound untouched. */
    void process (float* const* io, int numChannels, int numSamples, bool bypass = false);

    // ---- metering, written once per frame by the audio thread, read by the UI ----
    struct Meters
    {
        std::atomic<int> numBands { 10 };
        std::array<std::atomic<float>, kMaxBands> overDb {}; // band level above its usual level
        std::array<std::atomic<float>, kMaxBands> grDb {};   // current gain reduction
        std::atomic<float> learnedSeconds { 0.0f };
    };
    Meters meters;

private:
    void rebuildLayout();
    void rebuildBinMaps();
    void processFrame (bool bypass);

    double sr = 48000.0;
    int numCh = 2;
    int n = 512, hop = 128, half = 256;
    float hopSec = 128.0f / 48000.0f;
    int pos = 0;

    Fft fft;
    std::vector<float> window;
    std::vector<float> inBuf[2], outBuf[2], re[2], im[2];

    Settings settings;
    Layout layout;
    std::array<BandPlan, kMaxBands> plan {};
    bool mapsDirty = true;
    int layoutPerOctave = 0;

    // per band state (dB)
    std::array<float, kMaxBands> env {}, ref {}, model {}, gr {};
    float longRmsDb = kAssumedRmsDb;
    float learnedSec = 0.0f;

    // per bin maps
    std::vector<float> binHz;
    std::vector<int> binSeg;     // control-point segment, -1 = outside analysed region
    std::vector<float> binT;     // interpolation weight within the segment
    std::vector<float> binShelf; // linear static-shelf gain
    std::vector<float> binSolo;  // 0..1 mask of the selected target (Filter listen)
    std::vector<float> binGain;  // scratch: final linear gain per bin (processed channel)
    std::vector<float> binGainOther; // scratch: gain for the unprocessed channel (M/S modes)
    std::array<float, kMaxBands + 2> cpHz {}, cpDb {};
    std::array<int, kMaxBands> bandBinLo {}, bandBinHi {};
};

} // namespace sasari
