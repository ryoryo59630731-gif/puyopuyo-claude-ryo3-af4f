#pragma once

#include "AbSlots.h"
#include "Params.h"
#include "dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

class SasariProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    SasariProcessor();
    ~SasariProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    /** True while the parameters still hold what preset `index` sets. The edit target and the monitor
        mode are ignored (they do not change the sound settings). Message thread. */
    bool programMatchesParameters (int index);

    // ---- user presets: one file per preset in the user's folder, shared by all projects (message thread) ----
    static juce::File defaultUserPresetFolder(); // %APPDATA%\SasariOsaeru\Presets on Windows
    void setUserPresetFolder (const juce::File& folder) { userPresetFolder = folder; }
    juce::File getUserPresetFolder() const { return userPresetFolder; }
    /** The name a preset is stored under (characters a file name cannot hold are replaced). */
    static juce::String legalPresetName (const juce::String& name);
    juce::StringArray getUserPresetNames() const;
    bool userPresetExists (const juce::String& name) const;
    /** Saves every setting except the monitor mode (overwrites a preset of the same name). */
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    /** The user preset in use, or empty when the current preset is a factory one. */
    juce::String getCurrentUserPreset() const;
    /** True while the parameters still hold the current preset (factory or user). */
    bool currentPresetMatchesParameters();

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor helpers (message thread) ----
    sasari::Settings currentSettings() const { return raw.read(); }

    /** Learn the usual levels for a few seconds, then freeze them (switches Reference to Learned). */
    void startLearn();

    /** Per-band threshold trim (dB) of a band of the given layout (mean of its 1/6-octave trims). */
    float getBandTrim (const sasari::Layout& layout, int band) const;
    /** Sets the trims behind a band of the given layout (one or two 1/6-octave bands). Wrap a drag in
        begin / end so the host and undo see one gesture. */
    void beginBandTrimGesture (const sasari::Layout& layout, int band);
    void setBandTrim (const sasari::Layout& layout, int band, float db);
    void endBandTrimGesture (const sasari::Layout& layout, int band);
    void resetBandTrims();

    /** A/B comparison. */
    int getActiveSlot() const { return ab.active; }
    void switchToSlot (int slot);
    void copySlotToOther();

    /** Undo history for parameter changes (one transaction per mouse gesture in the editor). */
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    sasari::Engine engine;

    float editorScale = 1.0f;
    int spectrumView = 0; // 0 = spectrum, 1 = reference level (editor state, saved with the project)

private:
    using ValueList = std::vector<std::pair<juce::String, float>>;
    /** Every parameter that defines the sound (all but the monitor mode), with its current value. */
    ValueList captureSoundValues() const;
    bool valuesMatchParameters (const ValueList& values);
    juce::File userPresetFile (const juce::String& name) const;
    void run (juce::AudioBuffer<float>& buffer, bool bypass);
    void handleAsyncUpdate() override;
    sasari::AbSlots::Values captureValues() const;
    void applyValues (const sasari::AbSlots::Values& values);
    void setParam (const juce::String& id, float value);
    /** The parameter values preset `index` sets, given the current device-level choices. */
    std::vector<std::pair<juce::String, float>> programValues (int index) const;

    sasari::params::Raw raw;
    std::atomic<int> currentProgram { 0 };

    juce::File userPresetFolder { defaultUserPresetFolder() };
    mutable juce::CriticalSection userPresetLock; // guards the two below (state may be saved off the message thread)
    juce::String currentUserPreset;
    ValueList currentUserValues;
    sasari::AbSlots ab;
    std::atomic<int> latencyWanted { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SasariProcessor)
};
