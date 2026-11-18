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
        for (int k = 0; k < sasari::kProfileBands; ++k)
            same = same && std::abs (prof.db[(size_t) k] - savedProfile.db[(size_t) k]) < 0.01f;
        check (same, "learned profile restored with the project");
        q.switchToSlot (1);
        check (std::abs (value (q, mix) - 30.0f) < 0.01f, "A/B slots restored with the project");
    }

    // ---- presets ----
    std::printf ("[presets]\n");
    p.setCurrentProgram (6);
    check (value (p, id (3, "on")) > 0.5f && value (p, analysis) > 0.5f, "nasal preset: custom target + extended range");
    process (p, music, 0.2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    check (p.getLatencySamples() == 1024, "latency follows the analysis range: " + juce::String (p.getLatencySamples()));
    p.setCurrentProgram (0);
    set (p, analysis, 0.0f);
    set (p, reference, (float) sasari::Reference::Auto);

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
    }
    editor->setSize (1300, 962);
    check (std::abs (p.editorScale - 1.3f) < 0.01f, "editor scales and remembers its size");
    // Close and reopen the way the VST3 wrapper does.
    p.editorBeingDeleted (editor.get());
    editor = nullptr;
    editor.reset (p.createEditorIfNeeded());
    check (editor != nullptr && editor->getWidth() == 1300,
           "reopened editor keeps the size: " + juce::String (editor != nullptr ? editor->getWidth() : -1));
    feeder.stopTimer();
    p.editorBeingDeleted (editor.get());
    editor = nullptr;

    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL OK" : "FAILED", failures);
    proc = nullptr;
    return failures == 0 ? 0 : 1;
}
