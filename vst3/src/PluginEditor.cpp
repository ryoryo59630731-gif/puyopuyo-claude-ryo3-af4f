#include "PluginEditor.h"

using namespace sasari;
using namespace sasari::ui;

namespace
{
juce::String jp (const char* utf8) { return juce::String::fromUTF8 (utf8); }

const char* targetName (int f)
{
    static const char* n[] = { "歪みギター", "さ行", "ハイハット" };
    return n[f];
}

const char* targetHintText (int f)
{
    static const char* h[] = {
        "歪んだギターの「ジャリジャリ・チリチリ」。持続する音なので、ゆっくり反応して滑らかに下げます（目安 2〜6 kHz）。",
        "ボーカルの「サ・シ・ス・セ・ソ」「ツ」。一瞬で出て消えるので素早く反応します（女性 5.5〜11 kHz、男性 4〜8 kHz が目安）。",
        "ハイハット・シンバルの「チッ」「シャーン」。素早く反応します（目安 8〜16 kHz）。",
    };
    return h[f];
}

constexpr float kFMin = 1000.0f, kFMax = 20000.0f;
} // namespace

// ============================================================================ LookAndFeel

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (Colours::bg));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (Colours::panel));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (Colours::stroke));
    setColour (juce::ComboBox::textColourId, juce::Colour (Colours::text));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (Colours::dim));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (Colours::panel));
    setColour (juce::PopupMenu::textColourId, juce::Colour (Colours::text));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (Colours::accent).withAlpha (0.3f));
    setColour (juce::Label::textColourId, juce::Colour (Colours::text));
    setColour (juce::ToggleButton::textColourId, juce::Colour (Colours::text));
    setColour (juce::ToggleButton::tickColourId, juce::Colour (Colours::accent));
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float radius = size / 2.0f - 3.0f;
    const float angle = start + pos * (end - start);

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
    value.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, angle, true);
    const juce::PathStrokeType stroke (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colour (0xFF2E3440).withAlpha (alpha));
    g.strokePath (track, stroke);
    g.setColour (colour.withAlpha (alpha));
    g.strokePath (value, stroke);

    const float body = radius - 8.0f;
    g.setColour (juce::Colour (0xFF232833));
    g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    g.setColour (juce::Colour (0xFF3B4252));
    g.drawEllipse (c.x - body, c.y - body, body * 2, body * 2, 1.2f);
    const auto p1 = c.getPointOnCircumference (body * 0.35f, angle);
    const auto p2 = c.getPointOnCircumference (body * 0.85f, angle);
    g.setColour (colour.withAlpha (alpha));
    g.drawLine ({ p1, p2 }, 2.6f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const auto on = b.findColour (juce::TextButton::buttonOnColourId);
    if (b.getToggleState())
    {
        g.setColour (on);
        g.fillRoundedRectangle (r, 7.0f);
    }
    else
    {
        g.setColour (juce::Colour (Colours::panel).brighter (over ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (juce::Colour (Colours::stroke));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colour (0xFF0B1A19) : juce::Colour (Colours::text));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds();
    auto sw = r.removeFromRight (44).withSizeKeepingCentre (38, 20).toFloat();
    const bool on = b.getToggleState();
    g.setColour (on ? juce::Colour (Colours::accent).withAlpha (0.45f) : juce::Colour (0xFF3B4252));
    g.fillRoundedRectangle (sw, 10.0f);
    const float knobX = on ? sw.getRight() - 18.0f : sw.getX() + 2.0f;
    g.setColour (on ? juce::Colour (Colours::accent) : juce::Colour (0xFFB8C0CC));
    g.fillEllipse (knobX, sw.getY() + 2.0f, 16.0f, 16.0f);
    g.setColour (juce::Colour (Colours::text).withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (b.getButtonText(), r.reduced (2, 0), juce::Justification::centredLeft, 1);
}

// ============================================================================ Knob

Knob::Knob (const juce::String& n, juce::Colour colour) : name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.onValueChange = [this] { repaint(); };
    setColour (colour);
    addAndMakeVisible (slider);
}

void Knob::setColour (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    repaint();
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (32)); }

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (32);
    g.setColour (juce::Colour (Colours::text).withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromTop (16), juce::Justification::centred);
    g.setColour (juce::Colour (0xFFB8C0CC));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (name, r, juce::Justification::centred);
}

// ============================================================================ BandDisplay

void BandDisplay::tick()
{
    auto& m = proc.engine.meters;
    bands = m.numBands.load();
    for (int b = 0; b < kMaxBands; ++b)
    {
        over[(size_t) b] = m.overDb[(size_t) b].load();
        gr[(size_t) b] = m.grDb[(size_t) b].load();
        grPeak[(size_t) b] = juce::jmax (gr[(size_t) b], grPeak[(size_t) b] - 0.6f); // slow fall for readability
    }
    repaint();
}

void BandDisplay::paint (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave);
    const auto plan = makePlan (s, layout);

    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xFF171B22));
    g.fillRoundedRectangle (area, 6.0f);
    const float left = area.getX() + 10, right = area.getRight() - 10;
    const float top = area.getY() + 26, bottom = area.getBottom() - 18;
    auto x = [&] (float f) { return left + std::log (f / kFMin) / std::log (kFMax / kFMin) * (right - left); };
    // Bars: -12 dB .. +18 dB relative to the band's usual level. GR hangs from the top (0..24 dB).
    auto y = [&] (float db) { return bottom - juce::jlimit (0.0f, 1.0f, (db + 12.0f) / 30.0f) * (bottom - top); };

    g.setFont (juce::FontOptions (10.0f));
    for (auto [f, label] : { std::pair { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } })
    {
        g.setColour (juce::Colour (Colours::stroke));
        g.drawVerticalLine ((int) x (f), top, bottom);
        g.setColour (juce::Colour (Colours::dim));
        g.drawText (label, juce::Rectangle<float> (x (f) - 15, bottom + 2, 30, 14), juce::Justification::centred);
    }

    for (int b = 0; b < layout.numBands; ++b)
    {
        const float l = x (layout.low (b)) + 1.0f, r = x (layout.high (b)) - 1.0f;
        const int owner = plan[(size_t) b].owner;
        g.setColour (owner >= 0 ? Colours::focus (owner).withAlpha (0.22f) : juce::Colour (0xFF2E3440));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l, top, r, bottom));

        if (b < bands)
        {
            const float v = over[(size_t) b];
            if (v > -90.0f)
            {
                const bool above = owner >= 0 && v >= plan[(size_t) b].marginDb;
                g.setColour (above ? Colours::focus (owner) : juce::Colour (Colours::dim).withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l + 2, y (v), r - 2, bottom));
            }
            const float grh = juce::jlimit (0.0f, 1.0f, grPeak[(size_t) b] / 24.0f) * (bottom - top);
            if (grh > 0.5f)
            {
                g.setColour (juce::Colour (Colours::red).withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (l + 2, top, r - l - 4, grh));
            }
        }
        if (owner >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawHorizontalLine ((int) y (plan[(size_t) b].marginDb), l, r);
        }
    }

    // Selected target outline + label.
    std::array<int, kMaxBands> idx {};
    const int sel = s.selected;
    const int n = coveredBands (s.focus[(size_t) sel], layout, idx);
    const float l = x (layout.low (idx[0])), r = x (layout.high (idx[(size_t) n - 1]));
    if (s.listen == Listen::Filter)
    {
        g.setColour (juce::Colour (0xB00B0D11));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, top, l, bottom));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r, top, right, bottom));
    }
    g.setColour (Colours::focus (sel));
    g.drawRect (juce::Rectangle<float>::leftTopRightBottom (l, top, r, bottom), 2.0f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (jp (targetName (sel)) + "  " + params::hzText (layout.low (idx[0]), 0) + " - "
                    + params::hzText (layout.high (idx[(size_t) n - 1]), 0),
                juce::Rectangle<float> (left, area.getY() + 4, right - left, 18), juce::Justification::centred);

    float maxGr = 0;
    for (int b = 0; b < bands; ++b)
        maxGr = juce::jmax (maxGr, gr[(size_t) b]);
    g.setColour (juce::Colour (Colours::red));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (jp ("リダクション ") + juce::String (-maxGr, 1) + " dB",
                juce::Rectangle<float> (right - 150, area.getY() + 4, 150, 18), juce::Justification::centredRight);

    if (s.shelfOn)
    {
        const float zeroY = top + (bottom - top) * 0.25f;
        auto gy = [&] (float db) { return zeroY + (-db / 18.0f) * (bottom - zeroY); };
        juce::Path p;
        p.startNewSubPath (left, zeroY);
        const float fx = x (s.shelfHz);
        p.lineTo (fx - 12, zeroY);
        p.cubicTo (fx, zeroY, fx, gy (s.shelfDb), fx + 12, gy (s.shelfDb));
        p.lineTo (right, gy (s.shelfDb));
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colour (0xFFE2E8F0));
        g.fillPath (dashed);
    }
}

// ============================================================================ CurveDisplay

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const auto& f = s.focus[(size_t) s.selected];
    const float thr = marginDb (f.processing), ratio = ratioFor (f.intensity), knee = kneeDb (s.sharpness);

    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xFF171B22));
    g.fillRoundedRectangle (area, 6.0f);
    const float left = area.getX() + 26, right = area.getRight() - 8, top = area.getY() + 8, bottom = area.getBottom() - 20;
    auto x = [&] (float db) { return left + (db + 30.0f) / 60.0f * (right - left); };
    auto y = [&] (float db) { return bottom - (db + 30.0f) / 60.0f * (bottom - top); };
    g.setFont (juce::FontOptions (10.0f));
    for (int d = -30; d <= 30; d += 10)
    {
        g.setColour (juce::Colour (d == 0 ? 0xFF3B4252 : Colours::stroke));
        g.drawVerticalLine ((int) x ((float) d), top, bottom);
        g.drawHorizontalLine ((int) y ((float) d), left, right);
        g.setColour (juce::Colour (Colours::dim));
        g.drawText (juce::String (d), juce::Rectangle<float> (area.getX() + 2, y ((float) d) - 6, 22, 12), juce::Justification::centredLeft);
    }
    g.drawText (jp ("普段のレベル比 dB →"), juce::Rectangle<float> (left, bottom + 4, right - left, 14), juce::Justification::centredRight);
    g.setColour (juce::Colour (0xFF4C566A));
    g.drawLine (x (-30), y (-30), x (30), y (30), 1.0f);

    const auto colour = Colours::focus (s.selected).withAlpha (f.enabled ? 1.0f : 0.35f);
    g.setColour (colour);
    g.drawVerticalLine ((int) x (juce::jlimit (-30.0f, 30.0f, thr)), top, bottom);
    juce::Path p;
    for (int i = 0; i <= 120; ++i)
    {
        const float in = -30.0f + 0.5f * (float) i;
        const float red = std::min (compressorReductionDb (in - thr, ratio, knee), s.rangeDb) * (s.mix / 100.0f);
        const auto pt = juce::Point<float> (x (in), y (in - red));
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.strokePath (p, juce::PathStrokeType (2.5f));
    g.setColour (juce::Colour (Colours::text));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (juce::String (ratio, 1) + ":1  " + (thr >= 0 ? "+" : "") + juce::String (thr, 0) + " dB",
                juce::Rectangle<float> (left, top + 2, right - left - 4, 16), juce::Justification::centredRight);
}

// ============================================================================ Editor

SasariEditor::SasariEditor (SasariProcessor& p) : AudioProcessorEditor (p), proc (p), curve (p), bands (p)
{
    setLookAndFeel (&lnf);
    auto& state = proc.apvts;

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected (jp ("プリセット"));
    presetBox.onChange = [this] {
        if (presetBox.getSelectedId() > 0)
            proc.setCurrentProgram (presetBox.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox);

    const char* listenNames[] = { "Normal", "Filter", "Delta" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = listenButtons[i];
        b.setButtonText (listenNames[i]);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Colours::accent));
        b.onClick = [this, i] { setChoice (params::listen, i); };
        addAndMakeVisible (b);
    }
    listenButtons[0].setTooltip ("Normal");
    listenButtons[1].setTooltip (jp ("選択中のターゲットの帯域だけを聴く"));
    listenButtons[2].setTooltip (jp ("削った成分だけを聴く"));

    addAndMakeVisible (curve);
    addAndMakeVisible (bands);

    for (int f = 0; f < kNumFocus; ++f)
    {
        auto& b = targetButtons[f];
        b.setColour (juce::TextButton::buttonOnColourId, Colours::focus (f));
        b.onClick = [this, f] { setChoice (params::selected, f); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (targetOn);
    for (auto* k : { &lowKnob, &highKnob, &procKnob, &intKnob, &sharpKnob, &mixKnob, &rangeKnob, &outKnob,
                     &shelfFreqKnob, &shelfGainKnob })
        addAndMakeVisible (*k);
    targetHint.setFont (juce::FontOptions (12.0f));
    targetHint.setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
    targetHint.setJustificationType (juce::Justification::topLeft);
    targetHint.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (targetHint);

    sharpKnob.attach (state, params::sharpness);
    mixKnob.attach (state, params::mix);
    rangeKnob.attach (state, params::range);
    outKnob.attach (state, params::output);
    shelfFreqKnob.attach (state, params::shelfFreq);
    shelfGainKnob.attach (state, params::shelfGain);

    shelfOn.setButtonText (jp ("高域シェルフ（常時）"));
    shelfOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::shelfOn, shelfOn);
    addAndMakeVisible (shelfOn);

    autoOn.setButtonText (jp ("オート（曲に追従）"));
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::autoLevel, autoOn);
    addAndMakeVisible (autoOn);
    autoStatus.setFont (juce::FontOptions (11.0f));
    autoStatus.setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
    addAndMakeVisible (autoStatus);

    resolutionBox.addItemList ({ jp ("標準 1/3 オクターブ×10"), jp ("細かい 1/6 オクターブ×20") }, 1);
    resolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::resolution, resolutionBox);
    addAndMakeVisible (resolutionBox);
    channelsBox.addItemList ({ jp ("ステレオ（左右連動）"), jp ("Mid のみ（中央・ボーカル）"), jp ("Side のみ（左右・シンバル）") }, 1);
    channelsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::channels, channelsBox);
    addAndMakeVisible (channelsBox);
    for (auto* l : { &resolutionLabel, &channelsLabel })
    {
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
        addAndMakeVisible (*l);
    }
    resolutionLabel.setText (jp ("検出の細かさ"), juce::dontSendNotification);
    channelsLabel.setText (jp ("処理するチャンネル"), juce::dontSendNotification);

    attachSelectedTarget();
    setSize (880, 640);
    startTimerHz (30);
}

SasariEditor::~SasariEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

int SasariEditor::choice (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        return p->getIndex();
    return 0;
}

void SasariEditor::setChoice (const juce::String& id, int index)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void SasariEditor::attachSelectedTarget()
{
    const int f = choice (params::selected);
    attachedTarget = f;
    auto& state = proc.apvts;
    lowKnob.attach (state, params::id (f, "low"));
    highKnob.attach (state, params::id (f, "high"));
    procKnob.attach (state, params::id (f, "proc"));
    intKnob.attach (state, params::id (f, "int"));
    for (auto* k : { &procKnob, &intKnob })
        k->setColour (Colours::focus (f));
    targetOnAttachment.reset();
    targetOn.setButtonText (jp ("「") + jp (targetName (f)) + jp ("」を抑える"));
    targetOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id (f, "on"), targetOn);
    targetHint.setText (jp (targetHintText (f)), juce::dontSendNotification);
}

void SasariEditor::timerCallback()
{
    if (choice (params::selected) != attachedTarget)
        attachSelectedTarget();

    const auto s = proc.currentSettings();
    for (int f = 0; f < kNumFocus; ++f)
    {
        targetButtons[f].setButtonText (jp (targetName (f)) + "\n" + (s.focus[(size_t) f].enabled ? "ON" : "OFF"));
        targetButtons[f].setToggleState (f == s.selected, juce::dontSendNotification);
    }
    for (int i = 0; i < 3; ++i)
        listenButtons[i].setToggleState ((int) s.listen == i, juce::dontSendNotification);
    const bool on = s.focus[(size_t) s.selected].enabled;
    for (auto* k : { &lowKnob, &highKnob, &procKnob, &intKnob })
        k->slider.setEnabled (on);
    shelfFreqKnob.slider.setEnabled (s.shelfOn);
    shelfGainKnob.slider.setEnabled (s.shelfOn);

    const float learned = proc.engine.meters.learnedSeconds.load();
    autoStatus.setText (! s.autoLevel ? jp ("固定の推定値を基準にしています")
                                      : (learned < 10.0f ? jp ("学習中… ") + juce::String (learned, 0) + jp (" 秒")
                                                         : jp ("追従中（バー＝普段比、白線＝しきい値、赤＝リダクション）")),
                        juce::dontSendNotification);
    bands.tick();
    curve.repaint();
}

void SasariEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Colours::bg));
    g.setColour (juce::Colour (Colours::text));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (jp ("刺さりを抑えるソフト"), 16, 10, 320, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colour (Colours::dim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Sasari Osaeru  -  harshness / sibilance suppressor", 16, 38, 400, 14, juce::Justification::centredLeft);

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title) {
        g.setColour (juce::Colour (Colours::panel));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colour (Colours::stroke));
        g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);
        g.setColour (juce::Colour (Colours::dim));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 10, r.getY() + 4, r.getWidth() - 20, 16, juce::Justification::centredLeft);
    };
    panel ({ 12, 300, 856, 186 }, jp ("ターゲット（抑えたい音の種類）"));
    panel ({ 12, 494, 856, 134 }, jp ("共通 ／ 高域シェルフ ／ 検出"));
}

void SasariEditor::resized()
{
    presetBox.setBounds (440, 14, 200, 28);
    for (int i = 0; i < 3; ++i)
        listenButtons[i].setBounds (652 + i * 72, 14, 70, 28);

    curve.setBounds (12, 60, 250, 230);
    bands.setBounds (272, 60, 596, 230);

    for (int f = 0; f < kNumFocus; ++f)
        targetButtons[f].setBounds (22 + f * 124, 324, 120, 40);
    targetOn.setBounds (420, 330, 260, 28);
    const int ky = 372;
    lowKnob.setBounds (22, ky, 92, 108);
    highKnob.setBounds (118, ky, 92, 108);
    procKnob.setBounds (214, ky, 92, 108);
    intKnob.setBounds (310, ky, 92, 108);
    targetHint.setBounds (420, 370, 436, 100);

    const int gy = 516;
    sharpKnob.setBounds (22, gy, 84, 106);
    mixKnob.setBounds (106, gy, 84, 106);
    rangeKnob.setBounds (190, gy, 84, 106);
    outKnob.setBounds (274, gy, 84, 106);
    shelfOn.setBounds (372, 508, 200, 26);
    shelfFreqKnob.setBounds (376, gy + 22, 84, 84);
    shelfGainKnob.setBounds (464, gy + 22, 84, 84);

    resolutionLabel.setBounds (580, 504, 280, 16);
    resolutionBox.setBounds (580, 520, 276, 26);
    channelsLabel.setBounds (580, 548, 280, 16);
    channelsBox.setBounds (580, 564, 276, 26);
    autoOn.setBounds (580, 594, 276, 22);
    autoStatus.setBounds (580, 614, 288, 14);
}
