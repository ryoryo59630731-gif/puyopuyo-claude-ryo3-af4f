#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
using namespace sasari;

struct Preset
{
    const char* name;
    std::function<void (Settings&)> apply;
};

FocusSettings fs (int f, bool on, float proc, float inten, float lo = -1, float hi = -1)
{
    return { on, lo > 0 ? lo : kFocusInfo[f].defaultLowHz, hi > 0 ? hi : kFocusInfo[f].defaultHighHz, proc, inten };
}

// Same presets as the Android app.
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Standard (all three targets)", [] (Settings&) {} },
        { "Female vocal sibilance", [] (Settings& s) {
              s.focus = { fs (0, false, 40, 40), fs (1, true, 65, 70, 5500, 11000), fs (2, true, 40, 45, 11000, 16000) };
              s.selected = 1;
          } },
        { "Hi-hats and cymbals", [] (Settings& s) {
              s.focus = { fs (0, false, 40, 40), fs (1, true, 45, 50, 4500, 7000), fs (2, true, 65, 70, 7000, 16000) };
              s.selected = 2;
          } },
        { "Distorted guitar", [] (Settings& s) {
              s.focus = { fs (0, true, 60, 55, 2000, 6000), fs (1, true, 45, 50, 6000, 10000), fs (2, true, 40, 45) };
              s.selected = 0;
          } },
        { "Gentle", [] (Settings& s) {
              s.focus = { fs (0, true, 35, 35), fs (1, true, 45, 45), fs (2, true, 40, 40) };
              s.sharpness = 35;
          } },
        { "Long listening (+ shelf)", [] (Settings& s) {
              s.shelfOn = true;
              s.shelfHz = 9000;
              s.shelfDb = -3;
          } },
    };
    return list;
}
} // namespace

SasariProcessor::SasariProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SasariOsaeru", sasari::params::createLayout()),
      raw (apvts)
{
}

void SasariProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmin (2, getTotalNumOutputChannels()));
    engine.setSettings (raw.read());
    setLatencySamples (engine.latency());
}

bool SasariProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SasariProcessor::run (juce::AudioBuffer<float>& buffer, bool bypass)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    engine.setSettings (raw.read());
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), buffer.getNumSamples(), bypass);
}

void SasariProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, false); }

// Keep the reported latency while bypassed so the track stays in time.
void SasariProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, true); }

juce::AudioProcessorEditor* SasariProcessor::createEditor() { return new SasariEditor (*this); }

int SasariProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String SasariProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void SasariProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;
    sasari::Settings s;
    const auto current = raw.read();
    s.bandsPerOctave = current.bandsPerOctave; // device-level choices are kept
    s.autoLevel = current.autoLevel;
    s.channels = current.channels;
    presets()[(size_t) index].apply (s);

    auto set = [this] (const juce::String& id, float value) {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    using namespace sasari::params;
    for (int f = 0; f < kNumFocus; ++f)
    {
        const auto& fs = s.focus[(size_t) f];
        set (id (f, "on"), fs.enabled ? 1.0f : 0.0f);
        set (id (f, "low"), fs.lowHz);
        set (id (f, "high"), fs.highHz);
        set (id (f, "proc"), fs.processing);
        set (id (f, "int"), fs.intensity);
    }
    set (selected, (float) s.selected);
    set (sharpness, s.sharpness);
    set (mix, s.mix);
    set (range, s.rangeDb);
    set (output, s.outputDb);
    set (listen, 0.0f);
    set (shelfOn, s.shelfOn ? 1.0f : 0.0f);
    set (shelfFreq, s.shelfHz);
    set (shelfGain, s.shelfDb);
}

void SasariProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SasariProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasariProcessor(); }
