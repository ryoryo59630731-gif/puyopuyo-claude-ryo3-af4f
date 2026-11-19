#pragma once

#include "dsp/Mapping.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs, ranges and the conversion from host parameters to engine Settings (SPEC.md 6).
// Parameter names are ASCII so every host can display them; the editor is in Japanese.

namespace sasari::params
{

inline const char* focusKey (int f)
{
    static const char* keys[] = { "guitar", "vocal", "hihat", "custom" };
    return keys[f];
}
inline const char* focusLabel (int f)
{
    static const char* labels[] = { "Guitar", "Vocal S", "Hi-hat", "Custom" };
    return labels[f];
}

inline juce::String id (int f, const char* what) { return juce::String (focusKey (f)) + "_" + what; }

inline const juce::String selected { "selected" }, sharpness { "sharpness" }, mix { "mix" }, range { "range" },
    output { "output" }, selectivity { "selectivity" }, resolution { "resolution" }, analysis { "analysis" },
    reference { "reference" }, listen { "listen" }, shelfOn { "shelf_on" },
    shelfFreq { "shelf_freq" }, shelfGain { "shelf_gain" }, customAttack { "custom_att" },
    customRelease { "custom_rel" }, threshold { "threshold" }, thresholdMode { "threshold_mode" };

/** Per-band threshold trim of profile band k (0..31, 1/6 octave from 397 Hz). */
inline juce::String trimId (int k) { return "trim_" + juce::String (k).paddedLeft ('0', 2); }
constexpr float kTrimRangeDb = 20.0f;

inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float t) { return s * std::pow (e / s, t); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

inline juce::String hzText (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) v) + " Hz";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const Settings d;
    auto pctAttr = AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v)) + " %"; });
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
        [] (float v, int) { return String (v, 1) + " dB"; });
    auto msAttr = AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
        [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String ((int) v) + " ms"; });
    auto hzAttr = AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText);
    auto signedDb = [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };
    // PROCESSING also shows the threshold it gives: how far over the usual level a band must go.
    auto procAttr = AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
        const int m = roundToInt (marginDb (v));
        return String (roundToInt (v)) + " % (" + (m > 0 ? "+" : "") + String (m) + " dB)";
    });

    for (int f = 0; f < kNumFocus; ++f)
    {
        const auto& fs = d.focus[(size_t) f];
        const String name (focusLabel (f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (f, "on"), 1 }, name + " On", fs.enabled));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (f, "low"), 1 }, name + " Low",
                                                           logRange (kMinFreqHz, kMaxFreqHz), fs.lowHz, hzAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (f, "high"), 1 }, name + " High",
                                                           logRange (kMinFreqHz, kMaxFreqHz), fs.highHz, hzAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (f, "proc"), 1 }, name + " Processing",
                                                           NormalisableRange<float> (0.0f, 100.0f), fs.processing, procAttr));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (f, "int"), 1 }, name + " Intensity",
                                                           NormalisableRange<float> (0.0f, 100.0f), fs.intensity, pctAttr));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (f, "ch"), 1 }, name + " Channel",
                                                            StringArray { "Stereo", "Mid", "Side" }, (int) fs.channel));
    }
    const auto& c = d.focus[(size_t) kCustom];
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { customAttack, 1 }, "Custom Attack",
                                                       logRange (0.5f, 50.0f), c.attackMs, msAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { customRelease, 1 }, "Custom Release",
                                                       logRange (10.0f, 500.0f), c.releaseMs, msAttr));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { selected, 1 }, "Edit Target",
                                                        StringArray { "Guitar", "Vocal S", "Hi-hat", "Custom" }, d.selected));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { sharpness, 1 }, "Sharpness",
                                                       NormalisableRange<float> (0.0f, 100.0f), d.sharpness, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f), d.mix, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { range, 1 }, "Range",
                                                       NormalisableRange<float> (0.0f, 24.0f, 0.5f), d.rangeDb, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 6.0f, 0.1f), d.outputDb, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { selectivity, 1 }, "Selectivity",
                                                       NormalisableRange<float> (0.0f, 100.0f), d.selectivity, pctAttr));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { resolution, 1 }, "Resolution",
                                                        StringArray { "1/3 oct", "1/6 oct" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { analysis, 1 }, "Analysis Range",
                                                        StringArray { "1.6k-16k", "400-16k" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { reference, 1 }, "Reference",
                                                        StringArray { "Fixed", "Auto", "Learned" }, (int) d.reference));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { listen, 1 }, "Listen",
                                                        StringArray { "Normal", "Filter", "Delta" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { shelfOn, 1 }, "Shelf On", d.shelfOn));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { shelfFreq, 1 }, "Shelf Freq",
                                                       logRange (2000.0f, 16000.0f), d.shelfHz, hzAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { shelfGain, 1 }, "Shelf Gain",
                                                       NormalisableRange<float> (-18.0f, 0.0f, 0.1f), d.shelfDb, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { threshold, 1 }, "Threshold",
                                                       NormalisableRange<float> (-20.0f, 20.0f, 0.5f), d.thresholdDb,
                                                       AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (signedDb)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { thresholdMode, 1 }, "Threshold Mode",
                                                        StringArray { "All bands", "Per band" }, d.perBandThreshold ? 1 : 0));
    // Per-band trims: edited by dragging in the reference view, saved with the project / presets / A-B,
    // but not offered for automation (32 of them would only clutter the host's list).
    for (int k = 0; k < kProfileBands; ++k)
    {
        const float hz = std::sqrt (profileEdgeHz (k) * profileEdgeHz (k + 1));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { trimId (k), 1 }, "Band Trim " + hzText (hz, 0),
                                                           NormalisableRange<float> (-kTrimRangeDb, kTrimRangeDb, 0.5f), 0.0f,
                                                           AudioParameterFloatAttributes()
                                                               .withLabel ("dB")
                                                               .withStringFromValueFunction (signedDb)
                                                               .withAutomatable (false)));
    }
    return layout;
}

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
struct Raw
{
    std::array<std::array<std::atomic<float>*, 6>, kNumFocus> focus {};
    std::atomic<float>*sel {}, *sharp {}, *mixP {}, *rangeP {}, *out {}, *selP {}, *res {}, *ana {}, *refP {},
        *listenP {}, *shelfOnP {}, *shelfHz {}, *shelfDb {}, *att {}, *rel {}, *thr {}, *thrMode {};
    std::array<std::atomic<float>*, kProfileBands> trims {};

    explicit Raw (juce::AudioProcessorValueTreeState& s)
    {
        static const char* what[] = { "on", "low", "high", "proc", "int", "ch" };
        for (int f = 0; f < kNumFocus; ++f)
            for (int w = 0; w < 6; ++w)
                focus[(size_t) f][(size_t) w] = s.getRawParameterValue (id (f, what[w]));
        sel = s.getRawParameterValue (selected);
        sharp = s.getRawParameterValue (sharpness);
        mixP = s.getRawParameterValue (mix);
        rangeP = s.getRawParameterValue (range);
        out = s.getRawParameterValue (output);
        selP = s.getRawParameterValue (selectivity);
        res = s.getRawParameterValue (resolution);
        ana = s.getRawParameterValue (analysis);
        refP = s.getRawParameterValue (reference);
        listenP = s.getRawParameterValue (listen);
        shelfOnP = s.getRawParameterValue (shelfOn);
        shelfHz = s.getRawParameterValue (shelfFreq);
        shelfDb = s.getRawParameterValue (shelfGain);
        att = s.getRawParameterValue (customAttack);
        rel = s.getRawParameterValue (customRelease);
        thr = s.getRawParameterValue (threshold);
        thrMode = s.getRawParameterValue (thresholdMode);
        for (int k = 0; k < kProfileBands; ++k)
            trims[(size_t) k] = s.getRawParameterValue (trimId (k));
    }

    Settings read() const
    {
        Settings s;
        for (int f = 0; f < kNumFocus; ++f)
        {
            auto& fs = s.focus[(size_t) f];
            const auto& p = focus[(size_t) f];
            fs.enabled = p[0]->load() > 0.5f;
            fs.lowHz = p[1]->load();
            fs.highHz = p[2]->load();
            fs.processing = p[3]->load();
            fs.intensity = p[4]->load();
            fs.channel = (ChannelMode) juce::jlimit (0, 2, (int) p[5]->load());
        }
        s.focus[(size_t) kCustom].attackMs = att->load();
        s.focus[(size_t) kCustom].releaseMs = rel->load();
        s.selected = juce::jlimit (0, kNumFocus - 1, (int) sel->load());
        s.sharpness = sharp->load();
        s.mix = mixP->load();
        s.rangeDb = rangeP->load();
        s.outputDb = out->load();
        s.selectivity = selP->load();
        s.bandsPerOctave = res->load() > 0.5f ? 6 : 3;
        s.extendedRange = ana->load() > 0.5f;
        s.reference = (Reference) juce::jlimit (0, 2, (int) refP->load());
        s.listen = (Listen) juce::jlimit (0, 2, (int) listenP->load());
        s.shelfOn = shelfOnP->load() > 0.5f;
        s.shelfHz = shelfHz->load();
        s.shelfDb = shelfDb->load();
        s.thresholdDb = thr->load();
        s.perBandThreshold = thrMode->load() > 0.5f;
        for (int k = 0; k < kProfileBands; ++k)
            s.bandTrimDb[(size_t) k] = trims[(size_t) k]->load();
        return s;
    }
};

} // namespace sasari::params
