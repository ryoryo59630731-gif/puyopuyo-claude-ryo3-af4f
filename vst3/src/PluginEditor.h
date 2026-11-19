#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace sasari::ui
{

struct Colours
{
    static constexpr juce::uint32 bg = 0xFF101318, panel = 0xFF1A1E26, stroke = 0xFF2A303B, text = 0xFFECEFF4,
                                  dim = 0xFF8C96A6, accent = 0xFF4FD1C5, warn = 0xFFF6AD55, freq = 0xFF63B3ED,
                                  red = 0xFFFC8181, display = 0xFF171B22;
    static juce::Colour focus (int f)
    {
        static const juce::uint32 c[] = { 0xFFB794F4, 0xFF4FD1C5, 0xFFF6AD55, 0xFFF687B3 };
        return juce::Colour (c[juce::jlimit (0, 3, f)]);
    }
};

/** Shared log-frequency axis (100 Hz - 20 kHz) for the spectrum, prominence strip and history. */
struct FreqAxis
{
    static constexpr float lo = 100.0f, hi = 20000.0f;
    static float x (float f, float left, float right)
    {
        return left + std::log (juce::jlimit (lo, hi, f) / lo) / std::log (hi / lo) * (right - left);
    }
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
};

/** Rotary knob with its value and name drawn underneath, attachable to any float parameter. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, juce::Colour colour);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    void setColour (juce::Colour c);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Input / output spectrum with band ownership, reduction, selected target and shelf; or, switched
    with the two buttons in its header, the reference-level view: per band the current level, the
    usual level in use (reference) and the fixed estimate it started from. */
class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (SasariProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    // Per-band threshold editing in the reference view (per-band mode): drag a band's white line
    // up / down, double-click to set it back to 0 dB.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr float kRefTopDb = 0.0f, kRefBottomDb = -90.0f; // vertical range of the reference view

private:
    void paintSpectrum (juce::Graphics&);
    void paintReference (juce::Graphics&);
    bool editingTrims() const;
    /** The analysed band under x in the reference view, or -1. */
    int bandAt (float x, const sasari::Layout& layout) const;
    juce::Rectangle<float> referencePlot() const;

    SasariProcessor& proc;
    std::array<float, sasari::Engine::kSpecPoints> in {}, out {};
    std::array<float, sasari::kMaxBands> level {}; // displayed level of the reference view (fast rise, slow fall)
    juce::TextButton viewButtons[2], resetTrimsButton;
    sasari::Layout dragLayout;
    int dragBand = -1;
    float dragStartTrim = 0.0f, dragStartY = 0.0f;
};

/** Preset menu that rebuilds its list (factory presets + the user's saved ones) each time it opens. */
class PresetBox : public juce::ComboBox
{
public:
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup)
            beforePopup();
        juce::ComboBox::showPopup();
    }
};

/** Small dialog drawn inside the editor (no separate window): a preset name to save, or a yes / no question. */
class PresetDialog : public juce::Component
{
public:
    PresetDialog();
    /** onSave returns false when the preset could not be written (the dialog then stays open). */
    void askName (const juce::String& suggestion, std::function<bool (const juce::String&)> exists,
                  std::function<bool (const juce::String&)> onSave);
    void askConfirm (const juce::String& message, const juce::String& okText, std::function<void()> onOk);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // clicks outside the panel do nothing

    juce::TextEditor nameEditor;
    juce::TextButton okButton, cancelButton;

private:
    void open();
    void close();
    void updateWarning();
    juce::Rectangle<int> panelBounds() const;

    juce::Label title, warning;
    std::function<bool (const juce::String&)> nameExists, saveAction;
    std::function<void()> confirmAction;
    bool asksName = false;
};

/** Per-band "how far it sticks out" bars with each band's threshold. */
class ProminenceDisplay : public juce::Component
{
public:
    explicit ProminenceDisplay (SasariProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();

private:
    SasariProcessor& proc;
    std::array<float, kMaxBands> over {};
};

/** Scrolling heat map of the gain reduction: time x frequency. */
class HistoryDisplay : public juce::Component
{
public:
    explicit HistoryDisplay (SasariProcessor& p);
    void paint (juce::Graphics&) override;
    void tick();

private:
    SasariProcessor& proc;
    juce::Image image;
};

/** Transfer curve of the selected target, axes relative to the band's usual level / neighbours. */
class CurveDisplay : public juce::Component
{
public:
    explicit CurveDisplay (SasariProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SasariProcessor& proc;
};

/** Fixed-size design surface that the editor scales as a whole. */
class Body : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override
    {
        if (painter)
            painter (g);
    }
};

} // namespace sasari::ui

class SasariEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int designWidth = 1000, designHeight = 740;

    explicit SasariEditor (SasariProcessor&);
    ~SasariEditor() override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updatePresetBox();
    void rebuildPresetMenu();
    void openSaveDialog();
    void openDeleteDialog();
    void attachSelectedTarget();
    void setChoice (const juce::String& id, int index);
    int choice (const juce::String& id) const;
    void paintBody (juce::Graphics&);
    void layoutBody();

    SasariProcessor& proc;
    sasari::ui::LookAndFeel lnf;
    sasari::ui::Body body;

    sasari::ui::PresetBox presetBox;
    sasari::ui::PresetDialog presetDialog;
    juce::StringArray userPresetNames; // as listed in the open menu
    juce::TextButton abButtons[2], abCopy, undoButton, redoButton;
    juce::TextButton listenButtons[3];
    sasari::ui::CurveDisplay curve;
    sasari::ui::SpectrumDisplay spectrum;
    sasari::ui::ProminenceDisplay prominence;
    sasari::ui::HistoryDisplay history;

    juce::TextButton targetButtons[sasari::kNumFocus];
    juce::ToggleButton targetOn;
    juce::TextButton channelButtons[3];
    juce::Label channelLabel;
    sasari::ui::Knob lowKnob { "LOW", juce::Colour (sasari::ui::Colours::freq) },
        highKnob { "HIGH", juce::Colour (sasari::ui::Colours::freq) },
        procKnob { "PROCESSING", juce::Colour (sasari::ui::Colours::accent) },
        intKnob { "INTENSITY", juce::Colour (sasari::ui::Colours::accent) },
        attackKnob { "ATTACK", juce::Colour (sasari::ui::Colours::focus (3)) },
        releaseKnob { "RELEASE", juce::Colour (sasari::ui::Colours::focus (3)) };
    juce::Label targetHint, rangeWarning;

    sasari::ui::Knob sharpKnob { "SHARPNESS", juce::Colour (sasari::ui::Colours::accent) },
        mixKnob { "MIX", juce::Colour (sasari::ui::Colours::accent) },
        rangeKnob { "RANGE", juce::Colour (sasari::ui::Colours::red) },
        outKnob { "OUTPUT", juce::Colour (0xFFA0AEC0) },
        selectivityKnob { "SELECTIVITY", juce::Colour (0xFF9AE6B4) },
        thresholdKnob { "THRESHOLD", juce::Colour (0xFFE2E8F0) },
        shelfFreqKnob { "SHELF FREQ", juce::Colour (sasari::ui::Colours::freq) },
        shelfGainKnob { "SHELF GAIN", juce::Colour (0xFFE2E8F0) };
    juce::ToggleButton shelfOn;
    juce::TextButton thresholdModeButtons[2]; // 一括 / 帯域ごと
    juce::Label thresholdLabel;
    juce::ComboBox referenceBox, resolutionBox, analysisBox;
    juce::Label referenceLabel, resolutionLabel, analysisLabel, learnStatus;
    juce::TextButton learnButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> targetOnAttachment, shelfOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> referenceAttachment, resolutionAttachment,
        analysisAttachment;

    int attachedTarget = -1;
    int shownProgram = -2;       // what the preset box currently shows (see updatePresetBox; -2 = refresh)
    juce::String shownUser;
    bool shownModified = false;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SasariEditor)
};
