// Processor-level tests that need the plugin's own classes (SPEC.md section 8):
// undo/redo, A/B, Learn (incl. saving the learned profile in the project), state, and the
// editor itself (created, resized, painted while audio runs).
//
// Usage: ProcessorTests [snapshot-dir]   (with a directory, PNG snapshots of the editor are written)

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

float value (SasariProcessor& p, const juce::String& id)
{
    auto* prm = p.apvts.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

void set (SasariProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    prm->endChangeGesture();
}

/** Synthetic "music": vocal-like tone, sibilant bursts, hi-hat ticks, a steady resonance, pink-ish noise. */
struct Music
{
    std::mt19937 rng { 7 };
    double t = 0.0;
    float p0 = 0, p1 = 0;
    void fill (juce::AudioBuffer<float>& b, double sr)
    {
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        for (int i = 0; i < b.getNumSamples(); ++i, t += 1.0 / sr)
        {
            const float white = d (rng);
            p0 = 0.99f * p0 + 0.05f * white;
            p1 = 0.9f * p1 + 0.1f * white;
            const float pink = (p0 + p1 * 0.5f + white * 0.08f) * 0.25f;
            const double beat = std::fmod (t, 0.5);
            const double twoPi = juce::MathConstants<double>::twoPi;
            const double vowel = 0.12 * (std::sin (twoPi * 220 * t) + 0.5 * std::sin (twoPi * 440 * t) + 0.3 * std::sin (twoPi * 880 * t));
            const double ess = (std::fmod (t, 1.3) < 0.12 ? 0.35 : 0.0) * d (rng) * (0.6 + 0.4 * std::sin (twoPi * 7200 * t));
            const double hat = beat < 0.03 ? 0.25 * d (rng) * std::exp (-beat * 150) : 0.0;
            const double resonance = 0.05 * std::sin (twoPi * 3300 * t);
            const float v = (float) (pink + vowel + ess + hat + resonance);
            b.setSample (0, i, v);
            b.setSample (1, i, v * 0.95f);
        }
    }
};

/** The editor's preset box: the combo box whose first item is the first factory preset. */
juce::ComboBox* findPresetBox (juce::Component& root, const juce::String& firstPreset)
{
    for (auto* c : root.getChildren())
    {
        if (auto* box = dynamic_cast<juce::ComboBox*> (c); box != nullptr && box->getNumItems() > 0 && box->getItemText (0) == firstPreset)
            return box;
        if (auto* found = findPresetBox (*c, firstPreset))
            return found;
    }
    return nullptr;
}

void process (SasariProcessor& p, Music& m, double seconds, std::function<void()> everyBlock = nullptr)
{
    juce::AudioBuffer<float> b (2, 480);
    juce::MidiBuffer midi;
    for (int i = 0; i < (int) (seconds * 100); ++i)
    {
        m.fill (b, 48000);
        p.processBlock (b, midi);
        if (everyBlock)
            everyBlock();
    }
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File snapDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    using namespace sasari::params;

    auto proc = std::make_unique<SasariProcessor>();
    auto& p = *proc;
    p.setPlayConfigDetails (2, 2, 48000, 480);
    p.prepareToPlay (48000, 480);
    Music music;

    // ---- undo / redo ----
    std::printf ("[undo / redo]\n");
    p.undoManager.beginNewTransaction();
    const float before = value (p, id (1, "proc"));
    set (p, id (1, "proc"), 80.0f);
    p.apvts.state.getPropertyAsValue ("dummy", nullptr); // no-op, keep the tree alive
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400); // APVTS flushes parameter values to the tree on a timer
    check (std::abs (value (p, id (1, "proc")) - 80.0f) < 0.01f, "parameter changed");
    check (p.undoManager.canUndo(), "change recorded for undo");
    p.undoManager.undo();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    check (std::abs (value (p, id (1, "proc")) - before) < 0.01f, "undo restores the previous value");
    p.undoManager.redo();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    check (std::abs (value (p, id (1, "proc")) - 80.0f) < 0.01f, "redo applies it again");

    // ---- A/B ----
    std::printf ("[A/B]\n");
    set (p, mix, 70.0f);
    p.switchToSlot (1); // B starts as a copy
    check (p.getActiveSlot() == 1 && std::abs (value (p, mix) - 70.0f) < 0.01f, "B starts as a copy of A");
    set (p, mix, 30.0f);
    p.switchToSlot (0);
    check (std::abs (value (p, mix) - 70.0f) < 0.01f, "back to A restores A");
    p.switchToSlot (1);
    check (std::abs (value (p, mix) - 30.0f) < 0.01f, "B kept its own settings");
    p.switchToSlot (0);

    // ---- learn ----
    std::printf ("[learn]\n");
    p.startLearn();
    check ((int) value (p, reference) == (int) sasari::Reference::Learned, "Learn switches Reference to Learned");
    bool sawProgress = false;
    process (p, music, sasari::Engine::kLearnSeconds + 1.0,
             [&] { sawProgress = sawProgress || p.engine.meters.learnProgress.load() > 0.5f; });
    check (sawProgress, "learn progress reported");
    check (p.engine.meters.profileValid.load(), "profile learned from the music");

    // ---- state: parameters, learned profile and A/B survive save / load ----
    std::printf ("[state]\n");
    juce::MemoryBlock state;
    p.getStateInformation (state);
    const auto savedProfile = p.engine.getProfile();
    {
        SasariProcessor q;
        q.setPlayConfigDetails (2, 2, 48000, 480);
        q.prepareToPlay (48000, 480);
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (value (q, mix) - 70.0f) < 0.01f, "parameters restored");
        const auto prof = q.engine.getProfile();
        bool same = prof.valid;
        for (int d = 0; d < sasari::kNumDet; ++d)
            for (int k = 0; k < sasari::kProfileBands; ++k)
            {
                const float x = prof.db[(size_t) d][(size_t) k], y = savedProfile.db[(size_t) d][(size_t) k];
                same = same && ((std::isnan (x) && std::isnan (y)) || std::abs (x - y) < 0.01f);
            }
        check (same, "learned profile restored with the project");
        q.switchToSlot (1);
        check (std::abs (value (q, mix) - 30.0f) < 0.01f, "A/B slots restored with the project");
    }
    {
        // A host may leave an on/off parameter between 0 and 1 (still "on"); loading a state must still set it exactly.
        SasariProcessor q;
        auto* on = q.apvts.getParameter (id (1, "on"));
        on->setValueNotifyingHost (1.0f);
        juce::MemoryBlock st;
        q.getStateInformation (st);
        on->setValueNotifyingHost (0.72f);
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (on->getValue() - 1.0f) < 1.0e-6f, "an on/off parameter is restored exactly: " + juce::String (on->getValue()));
    }

    // ---- presets ----
    std::printf ("[presets]\n");
    p.setCurrentProgram (6);
    check ((int) value (p, id (1, "ch")) == (int) sasari::ChannelMode::Mid && (int) value (p, id (2, "ch")) == (int) sasari::ChannelMode::Side,
           "preset sets per-target channels (vocal Mid, hi-hat Side)");
    {
        const auto st = p.currentSettings();
        check (st.focus[1].channel == sasari::ChannelMode::Mid && st.focus[2].channel == sasari::ChannelMode::Side,
               "per-target channels reach the engine settings");
    }
    check (p.getCurrentProgram() == 6 && p.programMatchesParameters (6), "current preset is reported and matches the parameters");
    set (p, range, 3.0f);
    check (! p.programMatchesParameters (6), "changing a sound setting departs from the preset");
    set (p, range, 12.0f);
    set (p, selected, 2.0f);
    set (p, listen, 1.0f);
    check (p.programMatchesParameters (6), "edit target and monitor mode do not count as changes");
    set (p, listen, 0.0f);
    {
        juce::MemoryBlock st;
        p.getStateInformation (st);
        SasariProcessor q;
        q.setPlayConfigDetails (2, 2, 48000, 480);
        q.prepareToPlay (48000, 480);
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.getCurrentProgram() == 6 && q.programMatchesParameters (6), "preset number restored with the project");
    }
    {
        SasariProcessor fresh;
        check (fresh.getCurrentProgram() == 0 && fresh.programMatchesParameters (0), "a new instance holds the Standard preset");
    }
    p.setCurrentProgram (7);
    check (value (p, id (3, "on")) > 0.5f && value (p, analysis) > 0.5f, "nasal preset: custom target + extended range");
    process (p, music, 0.2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    check (p.getLatencySamples() == 1024, "latency follows the analysis range: " + juce::String (p.getLatencySamples()));
    p.setCurrentProgram (0);
    set (p, analysis, 0.0f);
    set (p, reference, (float) sasari::Reference::Auto);

    // ---- user presets (files in a test folder) ----
    std::printf ("[user presets]\n");
    const auto presetFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SasariOsaeruTestPresets");
    presetFolder.deleteRecursively();
    p.setUserPresetFolder (presetFolder);
    const auto userName = juce::String::fromUTF8 ("ボーカル用/強め"); // '/' cannot be in a file name
    const auto stored = SasariProcessor::legalPresetName (userName);
    {
        p.setCurrentProgram (1); // female vocal: guitar target off
        set (p, range, 18.0f);
        set (p, mix, 80.0f);
        check (p.saveUserPreset (userName), "user preset saved");
        check (p.getUserPresetNames().contains (stored) && p.getCurrentUserPreset() == stored && ! stored.containsChar ('/'),
               "listed under a file-safe name: " + stored);
        check (p.currentPresetMatchesParameters(), "the saved preset is current and unchanged");
        p.setCurrentProgram (0);
        check (p.getCurrentUserPreset().isEmpty() && std::abs (value (p, range) - 12.0f) < 0.01f, "a factory preset replaces it");
        check (p.loadUserPreset (stored) && std::abs (value (p, range) - 18.0f) < 0.01f && std::abs (value (p, mix) - 80.0f) < 0.01f
                   && value (p, id (0, "on")) < 0.5f,
               "loading brings back every setting");
        set (p, mix, 50.0f);
        check (! p.currentPresetMatchesParameters(), "a change after loading is noticed");
        set (p, mix, 80.0f);
        juce::MemoryBlock st;
        p.getStateInformation (st);
        SasariProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.getCurrentUserPreset() == stored && q.currentPresetMatchesParameters(), "the user preset in use is restored with the project");
        check (p.saveUserPreset ("Second") && p.getUserPresetNames().size() == 2, "a second preset is listed");
        check (p.deleteUserPreset ("Second") && ! p.userPresetExists ("Second") && p.getCurrentUserPreset().isEmpty(),
               "deleting removes the file and the selection");
        check (! p.loadUserPreset ("does not exist") && ! p.saveUserPreset ("   "), "a missing preset or an empty name is refused");
        p.setCurrentProgram (0);
    }

    // ---- editor ----
    std::printf ("[editor]\n");
    set (p, selected, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    check (editor != nullptr && editor->getWidth() == SasariEditor::designWidth,
           "editor opens at the design size: " + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()));
    // Feed audio while the editor's timer runs (30 Hz), like a DAW would.
    juce::AudioBuffer<float> b (2, 480);
    juce::MidiBuffer midi;
    struct Feeder : juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { fn(); }
    } feeder;
    feeder.fn = [&] {
        music.fill (b, 48000);
        p.processBlock (b, midi);
    };
    feeder.startTimer (10);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (6000);
    if (snapDir != juce::File())
    {
        snapDir.createDirectory();
        auto save = [&] (const juce::String& name) {
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (snapDir.getChildFile (name));
            os.setPosition (0);
            os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        save ("editor_vocal.png");
        set (p, selected, 3.0f);
        set (p, analysis, 1.0f);
        set (p, id (3, "on"), 1.0f);
        set (p, listen, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
        save ("editor_custom.png");
        p.setCurrentProgram (6); // vocal on Mid, cymbals on Side
        set (p, analysis, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
        save ("editor_midside.png");
    }
    editor->setSize (1300, 962);
    check (std::abs (p.editorScale - 1.3f) < 0.01f, "editor scales and remembers its size");
    p.setCurrentProgram (6); // the reopened editor has to show it
    set (p, analysis, 0.0f);
    // Close and reopen the way the VST3 wrapper does.
    p.editorBeingDeleted (editor.get());
    editor = nullptr;
    editor.reset (p.createEditorIfNeeded());
    check (editor != nullptr && editor->getWidth() == 1300,
           "reopened editor keeps the size: " + juce::String (editor != nullptr ? editor->getWidth() : -1));

    // ---- preset box ----
    std::printf ("[preset box]\n");
    if (auto* box = editor != nullptr ? findPresetBox (*editor, p.getProgramName (0)) : nullptr)
    {
        check (box->getSelectedId() == 7 && box->getText() == p.getProgramName (6),
               "reopened editor shows the current preset: " + box->getText());
        set (p, range, 3.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (box->getSelectedId() == 0 && box->getText().contains (p.getProgramName (6)) && box->getText() != p.getProgramName (6),
               "a changed setting is marked on the preset");
        box->setSelectedId (7, juce::sendNotificationSync);
        check (std::abs (value (p, range) - 12.0f) < 0.01f, "picking the same preset again re-applies it");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (box->getSelectedId() == 7 && box->getText() == p.getProgramName (6), "preset shown plainly again");
        p.loadUserPreset (stored);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (box->getText() == stored, "the box shows the user preset in use");
        set (p, mix, 40.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (box->getText().contains (stored) && box->getText() != stored, "a changed user preset is marked");
    }
    else
        check (false, "preset box found in the editor");

    // ---- reference-level view ----
    std::printf ("[reference view]\n");
    p.setCurrentProgram (0);
    p.spectrumView = 1;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500); // painted while audio runs
    if (snapDir != juce::File())
    {
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (snapDir.getChildFile ("editor_reference.png"));
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    {
        juce::MemoryBlock st;
        p.getStateInformation (st);
        SasariProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.spectrumView == 1, "the chosen view is saved with the project");
    }
    p.spectrumView = 0;
    presetFolder.deleteRecursively();
    feeder.stopTimer();
    p.editorBeingDeleted (editor.get());
    editor = nullptr;

    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL OK" : "FAILED", failures);
    proc = nullptr;
    return failures == 0 ? 0 : 1;
}
