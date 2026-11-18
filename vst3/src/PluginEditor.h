#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace sasari::ui
{

struct Colours
{
    static constexpr juce::uint32 bg = 0xFF101318, panel = 0xFF1A1E26, stroke = 0xFF2A303B, text = 0xFFECEFF4,
                                  dim = 0xFF8C96A6, accent = 0xFF4FD1C5, warn = 0xFFF6AD55, freq = 0xFF63B3ED,
                                  red = 0xFFFC8181;
    static juce::Colour focus (int f)
    {
        static const juce::uint32 c[] = { 0xFFB794F4, 0xFF4FD1C5, 0xFFF6AD55 };
        return juce::Colour (c[juce::jlimit (0, 2, f)]);
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

/** Frequency strip: band ownership, live level vs usual level, threshold and real gain reduction. */
class BandDisplay : public juce::Component
{
public:
    explicit BandDisplay (SasariProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();

private:
    SasariProcessor& proc;
    std::array<float, kMaxBands> over {}, gr {}, grPeak {};
    int bands = 10;
};

/** Transfer curve of the selected target, axes relative to the band's usual level. */
class CurveDisplay : public juce::Component
{
public:
    explicit CurveDisplay (SasariProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SasariProcessor& proc;
};

} // namespace sasari::ui

class SasariEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SasariEditor (SasariProcessor&);
    ~SasariEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void attachSelectedTarget();
    void setChoice (const juce::String& id, int index);
    int choice (const juce::String& id) const;

    SasariProcessor& proc;
    sasari::ui::LookAndFeel lnf;

    juce::ComboBox presetBox;
    juce::TextButton listenButtons[3];
    sasari::ui::CurveDisplay curve;
    sasari::ui::BandDisplay bands;

    juce::TextButton targetButtons[3];
    juce::ToggleButton targetOn;
    sasari::ui::Knob lowKnob { "LOW", juce::Colour (sasari::ui::Colours::freq) },
        highKnob { "HIGH", juce::Colour (sasari::ui::Colours::freq) },
        procKnob { "PROCESSING", juce::Colour (sasari::ui::Colours::accent) },
        intKnob { "INTENSITY", juce::Colour (sasari::ui::Colours::accent) };
    juce::Label targetHint;

    sasari::ui::Knob sharpKnob { "SHARPNESS", juce::Colour (sasari::ui::Colours::accent) },
        mixKnob { "MIX", juce::Colour (sasari::ui::Colours::accent) },
        rangeKnob { "RANGE", juce::Colour (sasari::ui::Colours::red) },
        outKnob { "OUTPUT", juce::Colour (0xFFA0AEC0) },
        shelfFreqKnob { "SHELF FREQ", juce::Colour (sasari::ui::Colours::freq) },
        shelfGainKnob { "SHELF GAIN", juce::Colour (0xFFE2E8F0) };
    juce::ToggleButton shelfOn, autoOn;
    juce::ComboBox resolutionBox, channelsBox;
    juce::Label autoStatus, resolutionLabel, channelsLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> targetOnAttachment, shelfOnAttachment,
        autoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resolutionAttachment, channelsAttachment;

    int attachedTarget = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SasariEditor)
};
