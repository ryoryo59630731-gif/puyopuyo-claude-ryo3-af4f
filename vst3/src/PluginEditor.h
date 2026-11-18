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

/** Input / output spectrum with band ownership, reduction, selected target and shelf. */
class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (SasariProcessor& p) : proc (p) { in.fill (-120.0f); out.fill (-120.0f); }
    void paint (juce::Graphics&) override;
    void tick();

private:
    SasariProcessor& proc;
    std::array<float, sasari::Engine::kSpecPoints> in {}, out {};
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
    void attachSelectedTarget();
    void setChoice (const juce::String& id, int index);
    int choice (const juce::String& id) const;
    void paintBody (juce::Graphics&);
    void layoutBody();

    SasariProcessor& proc;
    sasari::ui::LookAndFeel lnf;
    sasari::ui::Body body;

    juce::ComboBox presetBox;
    juce::TextButton abButtons[2], abCopy, undoButton, redoButton;
    juce::TextButton listenButtons[3];
    sasari::ui::CurveDisplay curve;
    sasari::ui::SpectrumDisplay spectrum;
    sasari::ui::ProminenceDisplay prominence;
    sasari::ui::HistoryDisplay history;

    juce::TextButton targetButtons[sasari::kNumFocus];
    juce::ToggleButton targetOn;
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
        shelfFreqKnob { "SHELF FREQ", juce::Colour (sasari::ui::Colours::freq) },
        shelfGainKnob { "SHELF GAIN", juce::Colour (0xFFE2E8F0) };
    juce::ToggleButton shelfOn;
    juce::ComboBox referenceBox, resolutionBox, analysisBox, channelsBox;
    juce::Label referenceLabel, resolutionLabel, analysisLabel, channelsLabel, learnStatus;
    juce::TextButton learnButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> targetOnAttachment, shelfOnAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> referenceAttachment, resolutionAttachment,
        analysisAttachment, channelsAttachment;

    int attachedTarget = -1;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SasariEditor)
};
