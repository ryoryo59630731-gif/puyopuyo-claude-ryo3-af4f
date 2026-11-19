#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <map>

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
    FocusSettings r { on, lo > 0 ? lo : kFocusInfo[f].defaultLowHz, hi > 0 ? hi : kFocusInfo[f].defaultHighHz, proc, inten };
    r.attackMs = kFocusInfo[f].baseAttackMs;
    r.releaseMs = kFocusInfo[f].baseReleaseMs;
    return r;
}

// Same presets as the Android app, plus one for the custom target (SPEC 4.4).
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Standard (all three targets)", [] (Settings&) {} },
        { "Female vocal sibilance", [] (Settings& s) {
              s.focus[0] = fs (0, false, 40, 40);
              s.focus[1] = fs (1, true, 65, 70, 5500, 11000);
              s.focus[2] = fs (2, true, 40, 45, 11000, 16000);
              s.selected = 1;
          } },
        { "Hi-hats and cymbals", [] (Settings& s) {
              s.focus[0] = fs (0, false, 40, 40);
              s.focus[1] = fs (1, true, 45, 50, 4500, 7000);
              s.focus[2] = fs (2, true, 65, 70, 7000, 16000);
              s.selected = 2;
          } },
        { "Distorted guitar", [] (Settings& s) {
              s.focus[0] = fs (0, true, 60, 55, 2000, 6000);
              s.focus[1] = fs (1, true, 45, 50, 6000, 10000);
              s.focus[2] = fs (2, true, 40, 45);
              s.selected = 0;
          } },
        { "Gentle", [] (Settings& s) {
              s.focus[0] = fs (0, true, 35, 35);
              s.focus[1] = fs (1, true, 45, 45);
              s.focus[2] = fs (2, true, 40, 40);
              s.sharpness = 35;
          } },
        { "Long listening (+ shelf)", [] (Settings& s) {
              s.shelfOn = true;
              s.shelfHz = 9000;
              s.shelfDb = -3;
          } },
        { "Vocal on Mid, cymbals on Side", [] (Settings& s) {
              s.focus[0] = fs (0, false, 40, 40);
              s.focus[1] = fs (1, true, 60, 65, 5000, 11000);
              s.focus[1].channel = ChannelMode::Mid;
              s.focus[2] = fs (2, true, 60, 65, 7000, 16000);
              s.focus[2].channel = ChannelMode::Side;
              s.selected = 1;
          } },
        { "Nasal mids (extended range)", [] (Settings& s) {
              s.extendedRange = true;
              s.focus[3] = fs (3, true, 55, 50, 600, 1200);
              s.selectivity = 60;
              s.selected = 3;
          } },
    };
    return list;
}
} // namespace

SasariProcessor::SasariProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "SasariOsaeru", sasari::params::createLayout()),
      raw (apvts)
{
}

SasariProcessor::~SasariProcessor() { cancelPendingUpdate(); }

void SasariProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmin (2, getTotalNumOutputChannels()));
    engine.setSettings (raw.read());
    latencyWanted.store (engine.latency());
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
    // The analysis range changes the frame size and so the latency: tell the host (message thread).
    if (engine.latency() != latencyWanted.load())
    {
        latencyWanted.store (engine.latency());
        triggerAsyncUpdate();
    }
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), buffer.getNumSamples(), bypass);
}

void SasariProcessor::handleAsyncUpdate() { setLatencySamples (latencyWanted.load()); }

void SasariProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, false); }

// Keep the reported latency while bypassed so the track stays in time.
void SasariProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, true); }

juce::AudioProcessorEditor* SasariProcessor::createEditor() { return new SasariEditor (*this); }

// ---- presets ----

int SasariProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String SasariProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void SasariProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

std::vector<std::pair<juce::String, float>> SasariProcessor::programValues (int index) const
{
    sasari::Settings s;
    const auto current = raw.read();
    s.bandsPerOctave = current.bandsPerOctave; // device-level choices are kept
    s.reference = current.reference;
    s.extendedRange = current.extendedRange;
    presets()[(size_t) index].apply (s);

    using namespace sasari::params;
    std::vector<std::pair<juce::String, float>> v;
    for (int f = 0; f < kNumFocus; ++f)
    {
        const auto& fs = s.focus[(size_t) f];
        v.emplace_back (id (f, "on"), fs.enabled ? 1.0f : 0.0f);
        v.emplace_back (id (f, "low"), fs.lowHz);
        v.emplace_back (id (f, "high"), fs.highHz);
        v.emplace_back (id (f, "proc"), fs.processing);
        v.emplace_back (id (f, "int"), fs.intensity);
        v.emplace_back (id (f, "ch"), (float) fs.channel);
    }
    v.emplace_back (customAttack, s.focus[(size_t) kCustom].attackMs);
    v.emplace_back (customRelease, s.focus[(size_t) kCustom].releaseMs);
    v.emplace_back (selected, (float) s.selected);
    v.emplace_back (sharpness, s.sharpness);
    v.emplace_back (mix, s.mix);
    v.emplace_back (range, s.rangeDb);
    v.emplace_back (output, s.outputDb);
    v.emplace_back (selectivity, s.selectivity);
    v.emplace_back (analysis, s.extendedRange ? 1.0f : 0.0f);
    v.emplace_back (listen, 0.0f);
    v.emplace_back (shelfOn, s.shelfOn ? 1.0f : 0.0f);
    v.emplace_back (shelfFreq, s.shelfHz);
    v.emplace_back (shelfGain, s.shelfDb);
    return v;
}

void SasariProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;
    {
        const juce::ScopedLock sl (userPresetLock);
        currentUserPreset.clear();
        currentUserValues.clear();
    }
    undoManager.beginNewTransaction();
    for (const auto& [paramId, value] : programValues (index))
        setParam (paramId, value);
}

bool SasariProcessor::valuesMatchParameters (const ValueList& values)
{
    for (const auto& [paramId, value] : values)
    {
        if (paramId == sasari::params::selected || paramId == sasari::params::listen)
            continue; // which target is being edited and the monitor mode are not sound settings
        if (auto* p = apvts.getParameter (paramId))
            if (std::abs (p->getValue() - p->convertTo0to1 (value)) > 1.0e-4f)
                return false;
    }
    return true;
}

bool SasariProcessor::programMatchesParameters (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return false;
    return valuesMatchParameters (programValues (index));
}

// ---- user presets ----

juce::File SasariProcessor::defaultUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("SasariOsaeru")
        .getChildFile ("Presets");
}

juce::String SasariProcessor::legalPresetName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 60).trim();
}

juce::File SasariProcessor::userPresetFile (const juce::String& name) const
{
    return userPresetFolder.getChildFile (legalPresetName (name) + ".sasaripreset");
}

juce::StringArray SasariProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : userPresetFolder.findChildFiles (juce::File::findFiles, false, "*.sasaripreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool SasariProcessor::userPresetExists (const juce::String& name) const
{
    return legalPresetName (name).isNotEmpty() && userPresetFile (name).existsAsFile();
}

SasariProcessor::ValueList SasariProcessor::captureSoundValues() const
{
    ValueList v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != sasari::params::listen)
                v.emplace_back (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    return v;
}

bool SasariProcessor::saveUserPreset (const juce::String& name)
{
    const auto legal = legalPresetName (name);
    if (legal.isEmpty() || ! userPresetFolder.createDirectory())
        return false;
    const auto values = captureSoundValues();
    juce::XmlElement xml ("SasariOsaeruPreset");
    xml.setAttribute ("version", 1);
    for (const auto& [paramId, value] : values)
    {
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", paramId);
        e->setAttribute ("value", (double) value);
    }
    if (! xml.writeTo (userPresetFile (legal)))
        return false;
    const juce::ScopedLock sl (userPresetLock);
    currentUserPreset = legal;
    currentUserValues = values;
    return true;
}

bool SasariProcessor::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::parseXML (userPresetFile (name));
    if (xml == nullptr || ! xml->hasTagName ("SasariOsaeruPreset"))
        return false;
    std::map<juce::String, float> stored;
    for (auto* e : xml->getChildWithTagNameIterator ("Param"))
        stored[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

    // A setting the file does not have (saved by an older version) goes back to its default.
    ValueList values;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != sasari::params::listen)
            {
                const auto it = stored.find (rp->getParameterID());
                values.emplace_back (rp->getParameterID(), it != stored.end() ? it->second : rp->convertFrom0to1 (rp->getDefaultValue()));
            }
    undoManager.beginNewTransaction();
    for (const auto& [paramId, value] : values)
        setParam (paramId, value);
    const juce::ScopedLock sl (userPresetLock);
    currentUserPreset = legalPresetName (name);
    currentUserValues = captureSoundValues(); // the values as the parameters hold them (snapped to their ranges)
    return true;
}

bool SasariProcessor::deleteUserPreset (const juce::String& name)
{
    const auto file = userPresetFile (name);
    if (! file.existsAsFile() || ! file.deleteFile())
        return false;
    const juce::ScopedLock sl (userPresetLock);
    if (currentUserPreset == legalPresetName (name))
    {
        currentUserPreset.clear();
        currentUserValues.clear();
    }
    return true;
}

juce::String SasariProcessor::getCurrentUserPreset() const
{
    const juce::ScopedLock sl (userPresetLock);
    return currentUserPreset;
}

bool SasariProcessor::currentPresetMatchesParameters()
{
    ValueList values;
    {
        const juce::ScopedLock sl (userPresetLock);
        if (currentUserPreset.isEmpty())
            values = programValues (currentProgram.load());
        else
            values = currentUserValues;
    }
    return valuesMatchParameters (values);
}

// ---- learn ----

void SasariProcessor::startLearn()
{
    undoManager.beginNewTransaction();
    setParam (sasari::params::reference, (float) sasari::Reference::Learned);
    engine.requestLearn();
}

// ---- A/B ----

sasari::AbSlots::Values SasariProcessor::captureValues() const
{
    sasari::AbSlots::Values v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            v[rp->getParameterID().toStdString()] = rp->convertFrom0to1 (rp->getValue());
    return v;
}

void SasariProcessor::applyValues (const sasari::AbSlots::Values& values)
{
    for (const auto& [key, value] : values)
        setParam (juce::String (key), value);
}

void SasariProcessor::switchToSlot (int slot)
{
    undoManager.beginNewTransaction();
    if (const auto* values = ab.switchTo (slot, captureValues()))
        applyValues (*values);
}

void SasariProcessor::copySlotToOther() { ab.copyToOther (captureValues()); }

// ---- state ----

void SasariProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("editorScale", editorScale, nullptr);
    state.setProperty ("spectrumView", spectrumView, nullptr);
    {
        const juce::ScopedLock sl (userPresetLock);
        if (currentUserPreset.isNotEmpty())
        {
            // The preset's values travel with the project, so "changed" still shows correctly after reopening.
            juce::ValueTree user ("UserPreset");
            user.setProperty ("name", currentUserPreset, nullptr);
            for (const auto& [paramId, value] : currentUserValues)
                user.setProperty (juce::Identifier (paramId), value, nullptr);
            state.appendChild (user, nullptr);
        }
    }

    const auto profile = engine.getProfile();
    if (profile.valid)
    {
        juce::StringArray values; // L/R, Mid, Side: 3 x 32 values
        for (const auto& det : profile.db)
            for (float v : det)
                values.add (std::isnan (v) ? "nan" : juce::String (v, 3));
        state.setProperty ("learnedProfile", values.joinIntoString (" "), nullptr);
    }

    juce::ValueTree abTree ("AB");
    abTree.setProperty ("active", ab.active, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        juce::ValueTree slot ("Slot");
        for (const auto& [key, value] : ab.slots[(size_t) i])
            slot.setProperty (juce::Identifier (key), value, nullptr);
        abTree.appendChild (slot, nullptr);
    }
    state.appendChild (abTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SasariProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    editorScale = (float) (double) tree.getProperty ("editorScale", 1.0);
    spectrumView = juce::jlimit (0, 1, (int) tree.getProperty ("spectrumView", 0));
    {
        const juce::ScopedLock sl (userPresetLock);
        currentUserPreset.clear();
        currentUserValues.clear();
        if (auto user = tree.getChildWithName ("UserPreset"); user.isValid())
        {
            currentUserPreset = user.getProperty ("name").toString();
            for (int i = 0; i < user.getNumProperties(); ++i)
                if (const auto key = user.getPropertyName (i); key.toString() != "name")
                    currentUserValues.emplace_back (key.toString(), (float) (double) user.getProperty (key));
            tree.removeChild (user, nullptr);
        }
    }

    sasari::Engine::Profile profile;
    const auto text = tree.getProperty ("learnedProfile").toString();
    if (text.isNotEmpty())
    {
        auto tokens = juce::StringArray::fromTokens (text, " ", "");
        const int per = sasari::kProfileBands;
        if (tokens.size() == per * sasari::kNumDet)
        {
            profile.valid = true;
            for (int d = 0; d < sasari::kNumDet; ++d)
                for (int k = 0; k < per; ++k)
                {
                    const auto& t = tokens[d * per + k];
                    profile.db[(size_t) d][(size_t) k] = t == "nan" ? std::numeric_limits<float>::quiet_NaN() : t.getFloatValue();
                }
        }
    }
    engine.setProfile (profile);

    ab = {};
    if (auto abTree = tree.getChildWithName ("AB"); abTree.isValid())
    {
        ab.active = juce::jlimit (0, 1, (int) abTree.getProperty ("active", 0));
        for (int i = 0; i < juce::jmin (2, abTree.getNumChildren()); ++i)
        {
            const auto slot = abTree.getChild (i);
            for (int p = 0; p < slot.getNumProperties(); ++p)
            {
                const auto name = slot.getPropertyName (p);
                ab.slots[(size_t) i][name.toString().toStdString()] = (float) (double) slot.getProperty (name);
            }
        }
        tree.removeChild (abTree, nullptr);
    }

    apvts.replaceState (tree);
    // replaceState skips a parameter whose stored value equals the one it already has after rounding
    // (an on/off parameter that the host left at e.g. 0.72 counts as "on"), so set every parameter to
    // exactly the stored value (found by pluginval, strictness 10).
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const auto node = tree.getChildWithProperty ("id", rp->getParameterID()); node.hasProperty ("value"))
            {
                const float normalised = rp->convertTo0to1 ((float) (double) node.getProperty ("value"));
                if (std::abs (rp->getValue() - normalised) > 1.0e-6f)
                    rp->setValueNotifyingHost (normalised);
            }
    undoManager.clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasariProcessor(); }
