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
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor helpers (message thread) ----
    sasari::Settings currentSettings() const { return raw.read(); }

    /** Learn the usual levels for a few seconds, then freeze them (switches Reference to Learned). */
    void startLearn();

    /** A/B comparison. */
    int getActiveSlot() const { return ab.active; }
    void switchToSlot (int slot);
    void copySlotToOther();

    /** Undo history for parameter changes (one transaction per mouse gesture in the editor). */
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    sasari::Engine engine;

    float editorScale = 1.0f;

private:
    void run (juce::AudioBuffer<float>& buffer, bool bypass);
    void handleAsyncUpdate() override;
    sasari::AbSlots::Values captureValues() const;
    void applyValues (const sasari::AbSlots::Values& values);
    void setParam (const juce::String& id, float value);

    sasari::params::Raw raw;
    int currentProgram = 0;
    sasari::AbSlots ab;
    std::atomic<int> latencyWanted { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SasariProcessor)
};
