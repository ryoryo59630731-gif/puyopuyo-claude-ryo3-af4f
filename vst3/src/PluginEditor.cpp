#include "PluginEditor.h"

using namespace sasari;
using namespace sasari::ui;

namespace
{
juce::String jp (const char* utf8) { return juce::String::fromUTF8 (utf8); }

// Preset menu item IDs: factory presets are 1..N (program index + 1).
constexpr int kUserPresetId = 101, kMaxUserPresetsShown = 800, kNoUserPresetId = 950, kSavePresetId = 1001,
              kDeletePresetId = 1002;

const char* targetName (int f)
{
    static const char* n[] = { "歪みギター", "さ行", "ハイハット", "カスタム" };
    return n[f];
}

const char* targetHintText (int f)
{
    static const char* h[] = {
        "歪んだギターの「ジャリジャリ・チリチリ」。持続する音なので、ゆっくり反応して滑らかに下げます（目安 2〜6 kHz）。",
        "ボーカルの「サ・シ・ス・セ・ソ」「ツ」。一瞬で出て消えるので素早く反応します（女性 5.5〜11 kHz、男性 4〜8 kHz が目安）。",
        "ハイハット・シンバルの「チッ」「シャーン」。素早く反応します（目安 8〜16 kHz）。",
        "自由に帯域と反応速度（ATTACK / RELEASE）を決められます。初期値は鼻にかかった声（600〜1200 Hz）。1.6 kHz より下を使うときは「解析範囲」を 400 Hz〜 にしてください。",
    };
    return h[f];
}

void drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> r, bool labels)
{
    g.setFont (juce::FontOptions (10.0f));
    for (auto [f, label] : { std::pair { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" },
                             { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } })
    {
        const float x = FreqAxis::x (f, r.getX(), r.getRight());
        g.setColour (juce::Colour (Colours::stroke));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        if (labels)
        {
            g.setColour (juce::Colour (Colours::dim));
            g.drawText (label, juce::Rectangle<float> (x - 15, r.getBottom() + 1, 30, 12), juce::Justification::centred);
        }
    }
}
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
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (Colours::accent));
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
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    if (b.getToggleState())
    {
        g.setColour (b.findColour (juce::TextButton::buttonOnColourId).withAlpha (alpha));
        g.fillRoundedRectangle (r, 7.0f);
    }
    else
    {
        g.setColour (juce::Colour (Colours::panel).brighter (over ? 0.08f : 0.0f).withAlpha (alpha));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (juce::Colour (Colours::stroke));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour ((b.getToggleState() ? juce::Colour (0xFF0B1A19) : juce::Colour (Colours::text)).withAlpha (b.isEnabled() ? 1.0f : 0.4f));
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

// ============================================================================ SpectrumDisplay

namespace
{
/** Which detector signal the reference view shows: the one the selected target listens to. */
int viewDetector (const Settings& s)
{
    const auto ch = s.focus[(size_t) juce::jlimit (0, kNumFocus - 1, s.selected)].channel;
    return ch == ChannelMode::Mid ? (int) Det::M : (ch == ChannelMode::Side ? (int) Det::S : (int) Det::LR);
}
} // namespace

SpectrumDisplay::SpectrumDisplay (SasariProcessor& p) : proc (p)
{
    in.fill (-120.0f);
    out.fill (-120.0f);
    level.fill (-120.0f);
    const char* names[] = { "スペクトラム", "基準レベル" };
    const char* tips[] = { "入力と出力の周波数ごとの大きさ、削った量", "帯域ごとの今のレベル・基準レベル（普段のレベル）・固定の推定値" };
    for (int i = 0; i < 2; ++i)
    {
        viewButtons[i].setButtonText (jp (names[i]));
        viewButtons[i].setTooltip (jp (tips[i]));
        viewButtons[i].onClick = [this, i] {
            proc.spectrumView = i;
            repaint();
        };
        addAndMakeVisible (viewButtons[i]);
    }
}

void SpectrumDisplay::resized()
{
    const int w = 96, x0 = getWidth() / 2 - w;
    for (int i = 0; i < 2; ++i)
        viewButtons[i].setBounds (x0 + i * w, 1, w - 2, 20);
}

void SpectrumDisplay::tick()
{
    auto& m = proc.engine.meters;
    for (int i = 0; i < Engine::kSpecPoints; ++i)
    {
        // Fast rise, slow fall, for a readable analyser.
        const float a = m.specIn[(size_t) i].load(), b = m.specOut[(size_t) i].load();
        in[(size_t) i] = a > in[(size_t) i] ? a : juce::jmax (a, in[(size_t) i] - 1.2f);
        out[(size_t) i] = b > out[(size_t) i] ? b : juce::jmax (b, out[(size_t) i] - 1.2f);
    }
    const int d = viewDetector (proc.currentSettings());
    for (int b = 0; b < kMaxBands; ++b)
    {
        const float v = m.levelDb[(size_t) d][(size_t) b].load();
        level[(size_t) b] = v > level[(size_t) b] ? v : juce::jmax (v, level[(size_t) b] - 0.8f);
    }
    for (int i = 0; i < 2; ++i)
        viewButtons[i].setToggleState (proc.spectrumView == i, juce::dontSendNotification);
    repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    if (proc.spectrumView == 1)
        paintReference (g);
    else
        paintSpectrum (g);
}

void SpectrumDisplay::paintSpectrum (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave, s.extendedRange);
    const auto plan = makePlan (s, layout);

    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (Colours::display));
    g.fillRoundedRectangle (area, 6.0f);
    const auto r = juce::Rectangle<float>::leftTopRightBottom (area.getX() + 10, area.getY() + 22, area.getRight() - 10,
                                                               area.getBottom() - 14);
    auto x = [&] (float f) { return FreqAxis::x (f, r.getX(), r.getRight()); };
    auto y = [&] (float db) { return r.getY() + juce::jlimit (0.0f, 1.0f, -db / 90.0f) * r.getHeight(); };

    // Region the engine does not analyse.
    g.setColour (juce::Colour (0x66000000));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), x (layout.lowLimit()), r.getBottom()));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x (layout.highLimit()), r.getY(), r.getRight(), r.getBottom()));

    bool anySplit = false;
    for (int b = 0; b < layout.numBands; ++b)
    {
        const int mo = plan.mid[(size_t) b].owner, so = plan.side[(size_t) b].owner;
        anySplit = anySplit || mo != so;
        const float l0 = x (layout.low (b)) + 0.5f, r0 = x (layout.high (b)) - 0.5f;
        if (mo == so)
        {
            if (mo >= 0)
            {
                g.setColour (Colours::focus (mo).withAlpha (0.13f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l0, r.getY(), r0, r.getBottom()));
            }
            continue;
        }
        // Different targets on Mid (lower half) and Side (upper half).
        const float midY = r.getCentreY();
        if (so >= 0)
        {
            g.setColour (Colours::focus (so).withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l0, r.getY(), r0, midY));
        }
        if (mo >= 0)
        {
            g.setColour (Colours::focus (mo).withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l0, midY, r0, r.getBottom()));
        }
    }
    drawFreqGrid (g, r, true);
    if (anySplit)
    {
        g.setColour (juce::Colour (Colours::dim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (jp ("色の上半分＝Side／下半分＝Mid"), r.withTrimmedRight (4).withHeight (14), juce::Justification::topRight);
    }
    g.setColour (juce::Colour (Colours::stroke).withAlpha (0.6f));
    for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
        g.drawHorizontalLine ((int) y (db), r.getX(), r.getRight());

    // Spectra (input grey, output white) and the cut between them (red).
    const int np = Engine::kSpecPoints;
    auto px = [&] (int i) {
        return x (Engine::kSpecLowHz * std::pow (Engine::kSpecHighHz / Engine::kSpecLowHz, ((float) i + 0.5f) / (float) np));
    };
    for (int i = 0; i < np; ++i)
    {
        const float d = in[(size_t) i] - out[(size_t) i];
        if (d > 0.5f && in[(size_t) i] > -90.0f)
        {
            g.setColour (juce::Colour (Colours::red).withAlpha (juce::jlimit (0.25f, 0.85f, d / 12.0f)));
            const float w = (r.getWidth() / (float) np) * 0.9f;
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (px (i) - w / 2, y (in[(size_t) i]), px (i) + w / 2, y (out[(size_t) i])));
        }
    }
    juce::Path pin, pout;
    for (int i = 0; i < np; ++i)
    {
        const juce::Point<float> a (px (i), y (in[(size_t) i])), b (px (i), y (out[(size_t) i]));
        if (i == 0) { pin.startNewSubPath (a); pout.startNewSubPath (b); }
        else { pin.lineTo (a); pout.lineTo (b); }
    }
    g.setColour (juce::Colour (0xFF8C96A6));
    g.strokePath (pin, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (pout, juce::PathStrokeType (1.5f));

    // Selected target outline + label.
    std::array<int, kMaxBands> idx {};
    const int sel = s.selected;
    const auto& sf = s.focus[(size_t) sel];
    const int n = coveredBands (sf, layout, idx);
    const float l = x (layout.low (idx[0])), rr = x (layout.high (idx[(size_t) n - 1]));
    if (s.listen == Listen::Filter)
    {
        g.setColour (juce::Colour (0x990B0D11));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), l, r.getBottom()));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (rr, r.getY(), r.getRight(), r.getBottom()));
    }
    g.setColour (Colours::focus (sel));
    if (! outsideLayout (sf, layout))
        g.drawRect (juce::Rectangle<float>::leftTopRightBottom (l, r.getY(), rr, r.getBottom()), 1.5f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (jp (targetName (sel)) + "  " + params::hzText (juce::jmin (sf.lowHz, sf.highHz), 0) + " - "
                    + params::hzText (juce::jmax (sf.lowHz, sf.highHz), 0),
                juce::Rectangle<float> (area.getX() + 10, area.getY() + 3, 260, 16), juce::Justification::centredLeft);

    float maxGr = 0;
    for (int b = 0; b < proc.engine.meters.numBands.load(); ++b)
        maxGr = juce::jmax (maxGr, proc.engine.meters.grDb[(size_t) b].load());
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (juce::Colour (0xFF8C96A6));
    g.drawText (jp ("灰＝入力  白＝出力  赤＝削った量"), r.withTrimmedLeft (4).withHeight (14), juce::Justification::topLeft);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (Colours::red));
    g.drawText (jp ("最大リダクション ") + juce::String (-maxGr, 1) + " dB",
                juce::Rectangle<float> (area.getRight() - 190, area.getY() + 3, 180, 16), juce::Justification::centredRight);

    if (s.shelfOn)
    {
        const float zeroY = y (-10.0f);
        auto gy = [&] (float db) { return zeroY - db * 2.0f; };
        juce::Path p;
        p.startNewSubPath (r.getX(), zeroY);
        const float fx = x (s.shelfHz);
        p.lineTo (fx - 12, zeroY);
        p.cubicTo (fx, zeroY, fx, gy (s.shelfDb), fx + 12, gy (s.shelfDb));
        p.lineTo (r.getRight(), gy (s.shelfDb));
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colour (0xFFE2E8F0));
        g.fillPath (dashed);
    }
}

void SpectrumDisplay::paintReference (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave, s.extendedRange);
    const auto plan = makePlan (s, layout);
    auto& m = proc.engine.meters;
    const int d = viewDetector (s);
    const int nb = juce::jmin (layout.numBands, m.numBands.load());

    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (Colours::display));
    g.fillRoundedRectangle (area, 6.0f);
    const auto r = juce::Rectangle<float>::leftTopRightBottom (area.getX() + 36, area.getY() + 24, area.getRight() - 10,
                                                               area.getBottom() - 14);
    auto x = [&] (float f) { return FreqAxis::x (f, r.getX(), r.getRight()); };
    constexpr float topDb = 0.0f, bottomDb = -90.0f; // the top ~15 dB holds the legend and the summary
    auto y = [&] (float db) { return r.getY() + juce::jlimit (0.0f, 1.0f, (topDb - db) / (topDb - bottomDb)) * r.getHeight(); };
    auto signedDb = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };

    g.setColour (juce::Colour (0x66000000));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), r.getY(), x (layout.lowLimit()), r.getBottom()));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x (layout.highLimit()), r.getY(), r.getRight(), r.getBottom()));
    const auto& owners = d == (int) Det::S ? plan.side : plan.mid;
    for (int b = 0; b < nb; ++b)
        if (const int o = owners[(size_t) b].owner; o >= 0)
        {
            g.setColour (Colours::focus (o).withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x (layout.low (b)) + 0.5f, r.getY(), x (layout.high (b)) - 0.5f, r.getBottom()));
        }
    drawFreqGrid (g, r, true);
    g.setFont (juce::FontOptions (10.0f));
    for (float db = -20.0f; db >= -80.0f; db -= 10.0f)
    {
        g.setColour (juce::Colour (Colours::stroke).withAlpha (0.6f));
        g.drawHorizontalLine ((int) y (db), r.getX(), r.getRight());
        g.setColour (juce::Colour (Colours::dim));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (area.getX() + 2, y (db) - 7, r.getX() - area.getX() - 5, 14),
                    juce::Justification::centredRight);
    }

    // Nothing to show until the host has sent audio through the plugin.
    const bool hasData = nb > 0 && std::isfinite (m.refDb[(size_t) d][0].load()) && std::isfinite (m.estimateDb[(size_t) d][0].load());
    if (! hasData)
    {
        g.setColour (juce::Colour (Colours::dim));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (jp ("まだ音が届いていません。再生すると、帯域ごとの今のレベルと基準レベルが表示されます"), r,
                    juce::Justification::centred);
    }

    // Per band: current level (bar, red while being cut), fixed estimate (dashed), reference in use (yellow).
    juce::Path refPath, estPath;
    for (int b = 0; hasData && b < nb; ++b)
    {
        const float l0 = x (layout.low (b)) + 1.0f, r0 = x (layout.high (b)) - 1.0f;
        const float lv = level[(size_t) b];
        const float rf = m.refDb[(size_t) d][(size_t) b].load(), est = m.estimateDb[(size_t) d][(size_t) b].load();
        if (lv > bottomDb)
        {
            const bool cutting = m.grDb[(size_t) b].load() > 0.2f;
            g.setColour (cutting ? juce::Colour (Colours::red).withAlpha (0.6f) : juce::Colour (0xFF8C96A6).withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l0, y (lv), r0, r.getBottom()));
        }
        if (b == 0)
        {
            refPath.startNewSubPath (l0, y (rf));
            estPath.startNewSubPath (l0, y (est));
        }
        else
        {
            refPath.lineTo (l0, y (rf));
            estPath.lineTo (l0, y (est));
        }
        refPath.lineTo (r0, y (rf));
        estPath.lineTo (r0, y (est));
    }
    juce::Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, estPath, dashes, 2);
    g.setColour (juce::Colour (0xFFB8C0CC).withAlpha (0.9f));
    g.fillPath (dashed);
    g.setColour (juce::Colour (Colours::warn));
    g.strokePath (refPath, juce::PathStrokeType (2.5f));

    // Selected target outline.
    std::array<int, kMaxBands> idx {};
    const int sel = s.selected;
    const auto& sf = s.focus[(size_t) sel];
    const int n = coveredBands (sf, layout, idx);
    if (! outsideLayout (sf, layout))
    {
        g.setColour (Colours::focus (sel));
        g.drawRect (juce::Rectangle<float>::leftTopRightBottom (x (layout.low (idx[0])), r.getY(), x (layout.high (idx[(size_t) n - 1])), r.getBottom()), 1.5f);
    }

    // Header: which reference is in use, and which signal is shown.
    const char* modeNames[] = { "固定の推定値", "オート（曲に追従）", "学習して固定" };
    juce::String mode = jp ("基準レベル：") + jp (modeNames[juce::jlimit (0, 2, (int) s.reference)]);
    if (s.reference == Reference::Learned)
        mode << (m.profileValid.load() ? jp ("（学習済み）") : jp ("（未学習）"));
    g.setColour (juce::Colour (Colours::warn));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (mode, juce::Rectangle<float> (area.getX() + 10, area.getY() + 3, 270, 16), juce::Justification::centredLeft);
    const char* detNames[] = { "表示：L/R の大きい方", "表示：Mid（中央）", "表示：Side（左右の広がり）" };
    g.setColour (juce::Colour (Colours::dim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (jp (detNames[d]), juce::Rectangle<float> (area.getRight() - 200, area.getY() + 3, 190, 16), juce::Justification::centredRight);

    // Legend and the summary for the selected target: how far the reference moved from the estimate,
    // and how far the sound is above its reference right now.
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (jp ("棒＝今のレベル（赤＝削っている）  黄線＝基準レベル  点線＝固定の推定値"), r.withTrimmedLeft (4).withHeight (13),
                juce::Justification::topLeft);
    float sumMove = 0.0f, minMove = 1.0e9f, maxMove = -1.0e9f, sumOver = 0.0f;
    int cnt = 0, cntOver = 0;
    for (int i = 0; hasData && i < n; ++i)
    {
        const int b = idx[(size_t) i];
        if (b >= nb)
            continue;
        const float rf = m.refDb[(size_t) d][(size_t) b].load();
        const float mv = rf - m.estimateDb[(size_t) d][(size_t) b].load();
        sumMove += mv;
        minMove = juce::jmin (minMove, mv);
        maxMove = juce::jmax (maxMove, mv);
        ++cnt;
        if (level[(size_t) b] > -100.0f)
        {
            sumOver += level[(size_t) b] - rf;
            ++cntOver;
        }
    }
    juce::String summary = jp ("「") + jp (targetName (sel)) + jp ("」の帯域：");
    if (! hasData)
        summary << jp ("再生すると表示されます");
    else if (cnt == 0 || outsideLayout (sf, layout))
        summary << jp ("解析範囲の外です");
    else
    {
        if (s.reference == Reference::Fixed)
            summary << jp ("基準＝固定の推定値（黄線と点線は同じ）");
        else
            summary << jp ("基準は推定値より 平均 ") << signedDb (sumMove / (float) cnt) << " dB  (" << signedDb (minMove) << jp (" 〜 ")
                    << signedDb (maxMove) << ")";
        if (cntOver > 0)
            summary << jp ("   今のレベルは基準より 平均 ") << signedDb (sumOver / (float) cntOver) << " dB";
    }
    g.setColour (juce::Colour (Colours::text));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (summary, r.withTrimmedLeft (4).withTrimmedTop (13).withHeight (15), juce::Justification::topLeft);
}

// ============================================================================ ProminenceDisplay

void ProminenceDisplay::tick()
{
    for (int b = 0; b < kMaxBands; ++b)
        over[(size_t) b] = proc.engine.meters.overDb[(size_t) b].load();
    repaint();
}

void ProminenceDisplay::paint (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave, s.extendedRange);
    auto& meters = proc.engine.meters;
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (Colours::display));
    g.fillRoundedRectangle (area, 6.0f);
    const auto r = juce::Rectangle<float>::leftTopRightBottom (area.getX() + 10, area.getY() + 4, area.getRight() - 10, area.getBottom() - 4);
    auto x = [&] (float f) { return FreqAxis::x (f, r.getX(), r.getRight()); };
    // -12 dB (bottom) .. +18 dB (top)
    auto y = [&] (float db) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 12.0f) / 30.0f) * r.getHeight(); };

    drawFreqGrid (g, r, false);
    const int nb = juce::jmin (layout.numBands, proc.engine.meters.numBands.load());
    for (int b = 0; b < nb; ++b)
    {
        const float l = x (layout.low (b)) + 1.0f, rr = x (layout.high (b)) - 1.0f;
        const int owner = meters.owner[(size_t) b].load();
        const float margin = meters.marginDb[(size_t) b].load();
        const float v = over[(size_t) b];
        if (v > -90.0f)
        {
            const bool above = owner >= 0 && v >= margin;
            g.setColour (above ? Colours::focus (owner) : juce::Colour (Colours::dim).withAlpha (owner >= 0 ? 0.6f : 0.25f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (l, y (v), rr, r.getBottom()));
        }
        if (owner >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawHorizontalLine ((int) y (margin), l, rr);
        }
    }
    g.setColour (juce::Colour (Colours::dim));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (jp ("突出度（バー）としきい値（白線）"), r.withTrimmedLeft (4).withHeight (12), juce::Justification::topLeft);
}

// ============================================================================ HistoryDisplay

HistoryDisplay::HistoryDisplay (SasariProcessor& p) : proc (p), image (juce::Image::ARGB, 240, 64, true) {}

void HistoryDisplay::tick()
{
    // Scroll one column (30 per second -> 8 s), then draw the newest reduction per row.
    const int w = image.getWidth(), h = image.getHeight();
    image.moveImageSection (0, 0, 1, 0, w - 1, h);
    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave, s.extendedRange);
    auto& m = proc.engine.meters;
    const int nb = juce::jmin (layout.numBands, m.numBands.load());
    for (int py = 0; py < h; ++py)
    {
        const float f = FreqAxis::hi * std::pow (FreqAxis::lo / FreqAxis::hi, ((float) py + 0.5f) / (float) h);
        juce::Colour c (0x00000000);
        for (int b = 0; b < nb; ++b)
            if (f >= layout.low (b) && f < layout.high (b))
            {
                const float grv = m.grDb[(size_t) b].load();
                const int owner = m.owner[(size_t) b].load();
                if (grv > 0.2f && owner >= 0)
                    c = Colours::focus (owner).interpolatedWith (juce::Colour (Colours::red), 0.5f)
                            .withAlpha (juce::jlimit (0.5f, 1.0f, 0.4f + grv / 10.0f));
                break;
            }
        image.setPixelAt (w - 1, py, c);
    }
    repaint();
}

void HistoryDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (Colours::display));
    g.fillRoundedRectangle (area, 6.0f);
    const auto r = area.reduced (8.0f, 4.0f).withTrimmedLeft (60.0f);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (image, r, juce::RectanglePlacement::stretchToFit);
    g.setColour (juce::Colour (Colours::stroke));
    for (float f : { 1000.0f, 10000.0f })
    {
        const float yy = r.getY() + std::log (FreqAxis::hi / f) / std::log (FreqAxis::hi / FreqAxis::lo) * r.getHeight();
        g.drawHorizontalLine ((int) yy, r.getX(), r.getRight());
    }
    g.setColour (juce::Colour (Colours::dim));
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText (jp ("削った履歴\n（直近8秒）"), juce::Rectangle<int> ((int) area.getX() + 6, (int) area.getY() + 4, 58, (int) area.getHeight() - 8),
                      juce::Justification::centredLeft, 3);
    g.drawText ("10k", juce::Rectangle<float> (r.getRight() - 30, r.getY() + r.getHeight() * 0.13f - 6, 28, 12), juce::Justification::centredRight);
    g.drawText ("1k", juce::Rectangle<float> (r.getRight() - 30, r.getY() + r.getHeight() * 0.57f - 6, 28, 12), juce::Justification::centredRight);
}

// ============================================================================ CurveDisplay

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto s = proc.currentSettings();
    const auto& f = s.focus[(size_t) s.selected];
    const float thr = marginDb (f.processing), ratio = ratioFor (f.intensity), knee = kneeDb (s.sharpness);

    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (Colours::display));
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
    g.drawText (jp ("突出度 dB →"), juce::Rectangle<float> (left, bottom + 4, right - left, 14), juce::Justification::centredRight);
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
    g.drawText (juce::String (ratio, 1) + ":1  " + (thr >= 0 ? "+" : "") + juce::String (juce::roundToInt (thr)) + " dB",
                juce::Rectangle<float> (left, top + 2, right - left - 4, 16), juce::Justification::centredRight);
}

// ============================================================================ PresetDialog

PresetDialog::PresetDialog()
{
    setInterceptsMouseClicks (true, true);
    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (Colours::text));
    warning.setFont (juce::FontOptions (12.0f));
    warning.setColour (juce::Label::textColourId, juce::Colour (Colours::warn));
    warning.setJustificationType (juce::Justification::topLeft);
    nameEditor.setFont (juce::FontOptions (15.0f));
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (Colours::display));
    nameEditor.setColour (juce::TextEditor::textColourId, juce::Colour (Colours::text));
    nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (Colours::stroke));
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Colours::accent));
    nameEditor.setInputRestrictions (60);
    nameEditor.onTextChange = [this] { updateWarning(); };
    nameEditor.onReturnKey = [this] { okButton.triggerClick(); };
    nameEditor.onEscapeKey = [this] { close(); };
    cancelButton.setButtonText (jp ("キャンセル"));
    cancelButton.onClick = [this] { close(); };
    okButton.onClick = [this] {
        if (asksName)
        {
            const auto name = SasariProcessor::legalPresetName (nameEditor.getText());
            if (name.isEmpty())
            {
                warning.setText (jp ("名前を入れてください"), juce::dontSendNotification);
                return;
            }
            if (saveAction && ! saveAction (name))
            {
                warning.setText (jp ("保存できませんでした（保存先：%APPDATA%\\SasariOsaeru\\Presets）"), juce::dontSendNotification);
                return;
            }
        }
        else if (confirmAction)
            confirmAction();
        close();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &title, &warning, &nameEditor, &okButton, &cancelButton })
        addAndMakeVisible (*c);
}

void PresetDialog::askName (const juce::String& suggestion, std::function<bool (const juce::String&)> exists,
                            std::function<bool (const juce::String&)> onSave)
{
    asksName = true;
    nameExists = std::move (exists);
    saveAction = std::move (onSave);
    confirmAction = nullptr;
    title.setText (jp ("今の設定をプリセットとして保存"), juce::dontSendNotification);
    okButton.setButtonText (jp ("保存"));
    nameEditor.setVisible (true);
    nameEditor.setText (suggestion, juce::dontSendNotification);
    nameEditor.selectAll();
    updateWarning();
    open();
    nameEditor.grabKeyboardFocus();
}

void PresetDialog::askConfirm (const juce::String& message, const juce::String& okText, std::function<void()> onOk)
{
    asksName = false;
    confirmAction = std::move (onOk);
    title.setText (message, juce::dontSendNotification);
    okButton.setButtonText (okText);
    nameEditor.setVisible (false);
    warning.setText (jp ("この操作は元に戻せません"), juce::dontSendNotification);
    open();
}

void PresetDialog::updateWarning()
{
    if (! asksName)
        return;
    const auto name = SasariProcessor::legalPresetName (nameEditor.getText());
    juce::String text;
    if (name.isNotEmpty() && name != nameEditor.getText().trim())
        text << jp ("保存名：") << name << jp ("（ファイル名に使えない文字を除きました）  ");
    if (name.isNotEmpty() && nameExists && nameExists (name))
        text << jp ("同じ名前のプリセットがあります。保存すると上書きします");
    warning.setText (text, juce::dontSendNotification);
}

void PresetDialog::open()
{
    setVisible (true);
    toFront (false);
    resized();
}

void PresetDialog::close()
{
    setVisible (false);
    saveAction = nullptr;
    confirmAction = nullptr;
}

juce::Rectangle<int> PresetDialog::panelBounds() const { return getLocalBounds().withSizeKeepingCentre (440, 176); }

void PresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xAA05070A));
    const auto p = panelBounds().toFloat();
    g.setColour (juce::Colour (Colours::panel));
    g.fillRoundedRectangle (p, 10.0f);
    g.setColour (juce::Colour (Colours::accent).withAlpha (0.6f));
    g.drawRoundedRectangle (p, 10.0f, 1.5f);
}

void PresetDialog::resized()
{
    auto p = panelBounds().reduced (18, 14);
    title.setBounds (p.removeFromTop (24));
    p.removeFromTop (6);
    if (asksName)
        nameEditor.setBounds (p.removeFromTop (32));
    p.removeFromTop (6);
    auto buttons = p.removeFromBottom (30);
    cancelButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    okButton.setBounds (buttons.removeFromRight (110));
    warning.setBounds (p);
}

// ============================================================================ Editor

SasariEditor::SasariEditor (SasariProcessor& p)
    : AudioProcessorEditor (p), proc (p), curve (p), spectrum (p), prominence (p), history (p)
{
    setLookAndFeel (&lnf);
    auto& state = proc.apvts;
    addAndMakeVisible (body);
    body.painter = [this] (juce::Graphics& g) { paintBody (g); };
    body.addMouseListener (this, true); // one undo transaction per mouse gesture

    presetBox.setTextWhenNothingSelected (jp ("プリセット"));
    presetBox.setTooltip (jp ("標準プリセットと自作プリセット。「今の設定を保存…」で名前を付けて保存できます"));
    presetBox.beforePopup = [this] { rebuildPresetMenu(); };
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= proc.getNumPrograms())
            proc.setCurrentProgram (id - 1);
        else if (id >= kUserPresetId && id < kUserPresetId + userPresetNames.size())
            proc.loadUserPreset (userPresetNames[id - kUserPresetId]);
        else if (id == kSavePresetId)
            openSaveDialog();
        else if (id == kDeletePresetId)
            openDeleteDialog();
        shownProgram = -2; // show the current preset again (also after the two actions above)
        updatePresetBox();
    };
    body.addAndMakeVisible (presetBox);
    rebuildPresetMenu(); // shows the preset restored with the project / chosen in the host

    for (int i = 0; i < 2; ++i)
    {
        abButtons[i].setButtonText (i == 0 ? "A" : "B");
        abButtons[i].onClick = [this, i] { proc.switchToSlot (i); };
        abButtons[i].setTooltip (jp ("設定 A / B を切り替えて聴き比べる"));
        body.addAndMakeVisible (abButtons[i]);
    }
    abCopy.onClick = [this] { proc.copySlotToOther(); };
    body.addAndMakeVisible (abCopy);
    undoButton.setButtonText (jp ("元に戻す"));
    redoButton.setButtonText (jp ("やり直す"));
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    body.addAndMakeVisible (undoButton);
    body.addAndMakeVisible (redoButton);

    const char* listenNames[] = { "Normal", "Filter", "Delta" };
    for (int i = 0; i < 3; ++i)
    {
        listenButtons[i].setButtonText (listenNames[i]);
        listenButtons[i].onClick = [this, i] { setChoice (params::listen, i); };
        body.addAndMakeVisible (listenButtons[i]);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &curve, &spectrum, &prominence, &history })
        body.addAndMakeVisible (*c);

    for (int f = 0; f < kNumFocus; ++f)
    {
        targetButtons[f].setColour (juce::TextButton::buttonOnColourId, Colours::focus (f));
        targetButtons[f].onClick = [this, f] { setChoice (params::selected, f); };
        body.addAndMakeVisible (targetButtons[f]);
    }
    body.addAndMakeVisible (targetOn);
    const char* channelNames[] = { "ステレオ", "Mid", "Side" };
    const char* channelTips[] = { "左右の大きい方で検出し、左右同じ量だけ下げます（定位は変わりません）",
                                  "中央の音（リードボーカルなど）だけで検出し、中央だけを下げます",
                                  "左右の広がり（ステレオのシンバル・ダブリングなど）だけで検出し、広がりだけを下げます" };
    for (int i = 0; i < 3; ++i)
    {
        channelButtons[i].setButtonText (jp (channelNames[i]));
        channelButtons[i].setTooltip (jp (channelTips[i]));
        channelButtons[i].onClick = [this, i] { setChoice (params::id (attachedTarget, "ch"), i); };
        body.addAndMakeVisible (channelButtons[i]);
    }
    channelLabel.setText (jp ("チャンネル"), juce::dontSendNotification);
    channelLabel.setFont (juce::FontOptions (11.0f));
    channelLabel.setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
    body.addAndMakeVisible (channelLabel);
    for (auto* k : { &lowKnob, &highKnob, &procKnob, &intKnob, &attackKnob, &releaseKnob, &sharpKnob, &mixKnob,
                     &rangeKnob, &outKnob, &selectivityKnob, &shelfFreqKnob, &shelfGainKnob })
        body.addAndMakeVisible (*k);
    attackKnob.attach (state, params::customAttack);
    releaseKnob.attach (state, params::customRelease);

    for (auto* l : { &targetHint, &rangeWarning })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setJustificationType (juce::Justification::topLeft);
        l->setMinimumHorizontalScale (1.0f);
        body.addAndMakeVisible (*l);
    }
    targetHint.setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
    rangeWarning.setColour (juce::Label::textColourId, juce::Colour (Colours::warn));
    rangeWarning.setText (jp ("⚠ この帯域は解析範囲の外です。「解析範囲」を 400 Hz〜 にしてください。"), juce::dontSendNotification);

    sharpKnob.attach (state, params::sharpness);
    mixKnob.attach (state, params::mix);
    rangeKnob.attach (state, params::range);
    outKnob.attach (state, params::output);
    selectivityKnob.attach (state, params::selectivity);
    selectivityKnob.slider.setTooltip (jp ("0%＝普段より大きくなったら反応／100%＝周りの帯域より突出したら反応"));
    shelfFreqKnob.attach (state, params::shelfFreq);
    shelfGainKnob.attach (state, params::shelfGain);

    shelfOn.setButtonText (jp ("高域シェルフ"));
    shelfOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::shelfOn, shelfOn);
    body.addAndMakeVisible (shelfOn);

    referenceBox.addItemList ({ jp ("固定の推定値"), jp ("オート（曲に追従）"), jp ("学習して固定") }, 1);
    resolutionBox.addItemList ({ jp ("1/3 オクターブ"), jp ("1/6 オクターブ") }, 1);
    analysisBox.addItemList ({ jp ("1.6〜16 kHz（遅延 11 ms）"), jp ("400 Hz〜16 kHz（21 ms）") }, 1);
    referenceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::reference, referenceBox);
    resolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::resolution, resolutionBox);
    analysisAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::analysis, analysisBox);
    for (auto* b : { &referenceBox, &resolutionBox, &analysisBox })
        body.addAndMakeVisible (*b);
    const std::pair<juce::Label*, const char*> labels[] = {
        { &referenceLabel, "基準レベル" }, { &resolutionLabel, "検出の細かさ" }, { &analysisLabel, "解析範囲" }
    };
    for (auto [l, text] : labels)
    {
        l->setText (jp (text), juce::dontSendNotification);
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
        body.addAndMakeVisible (*l);
    }
    learnButton.setButtonText (jp ("Learn（8秒）"));
    learnButton.setTooltip (jp ("再生しながら押すと、8秒間で各帯域の普段のレベルを覚えて固定します"));
    learnButton.onClick = [this] { proc.startLearn(); };
    body.addAndMakeVisible (learnButton);
    learnStatus.setFont (juce::FontOptions (11.0f));
    learnStatus.setColour (juce::Label::textColourId, juce::Colour (Colours::dim));
    body.addAndMakeVisible (learnStatus);
    body.addChildComponent (presetDialog); // last, so it is drawn over everything when shown

    attachSelectedTarget();
    layoutBody();

    // Read the remembered size before the resize limits trigger resized() (which stores the scale).
    const float scale = juce::jlimit (0.7f, 1.6f, proc.editorScale);
    setResizable (true, true);
    setResizeLimits (designWidth * 7 / 10, designHeight * 7 / 10, designWidth * 16 / 10, designHeight * 16 / 10);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (juce::roundToInt (designWidth * scale), juce::roundToInt (designHeight * scale));
    constructed = true;
    startTimerHz (30);
}

SasariEditor::~SasariEditor()
{
    stopTimer();
    body.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void SasariEditor::mouseDown (const juce::MouseEvent&) { proc.undoManager.beginNewTransaction(); }

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
    attackKnob.setVisible (f == kCustom);
    releaseKnob.setVisible (f == kCustom);
    targetOnAttachment.reset();
    targetOn.setButtonText (jp ("「") + jp (targetName (f)) + jp ("」を抑える"));
    targetOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id (f, "on"), targetOn);
    targetHint.setText (jp (targetHintText (f)), juce::dontSendNotification);
}

// Shows the current preset (factory or user); once a sound setting differs from it (knob, undo, A/B,
// automation) the box says so and has no selection, so picking the same preset again re-applies it.
void SasariEditor::updatePresetBox()
{
    const auto user = proc.getCurrentUserPreset();
    const int program = user.isEmpty() ? proc.getCurrentProgram() : -1;
    const bool modified = ! proc.currentPresetMatchesParameters();
    if (program == shownProgram && user == shownUser && modified == shownModified)
        return;
    shownProgram = program;
    shownUser = user;
    shownModified = modified;
    const auto name = user.isEmpty() ? proc.getProgramName (program) : user;
    if (modified)
        presetBox.setText (jp ("変更あり：") + name, juce::dontSendNotification);
    else if (user.isEmpty())
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
    else
        presetBox.setText (name, juce::dontSendNotification); // selects the item when the menu lists it
}

void SasariEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading (jp ("標準プリセット"));
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.addSectionHeading (jp ("自作プリセット"));
    userPresetNames = proc.getUserPresetNames();
    userPresetNames.removeRange (kMaxUserPresetsShown, userPresetNames.size());
    for (int i = 0; i < userPresetNames.size(); ++i)
        presetBox.addItem (userPresetNames[i], kUserPresetId + i);
    if (userPresetNames.isEmpty())
        presetBox.addItem (jp ("（まだありません）"), kNoUserPresetId);
    presetBox.setItemEnabled (kNoUserPresetId, false);
    presetBox.addSeparator();
    presetBox.addItem (jp ("今の設定を保存…"), kSavePresetId);
    if (const auto current = proc.getCurrentUserPreset(); current.isNotEmpty() && proc.userPresetExists (current))
        presetBox.addItem (jp ("「") + current + jp ("」を削除…"), kDeletePresetId);
    shownProgram = -2;
    updatePresetBox();
}

void SasariEditor::openSaveDialog()
{
    presetDialog.askName (proc.getCurrentUserPreset(),
                          [this] (const juce::String& name) { return proc.userPresetExists (name); },
                          [this] (const juce::String& name) {
                              const bool ok = proc.saveUserPreset (name);
                              shownProgram = -2;
                              updatePresetBox();
                              return ok;
                          });
}

void SasariEditor::openDeleteDialog()
{
    const auto name = proc.getCurrentUserPreset();
    if (name.isEmpty())
        return;
    presetDialog.askConfirm (jp ("自作プリセット「") + name + jp ("」を削除しますか？"), jp ("削除"), [this, name] {
        proc.deleteUserPreset (name);
        shownProgram = -2;
        updatePresetBox();
    });
}

void SasariEditor::timerCallback()
{
    if (choice (params::selected) != attachedTarget)
        attachSelectedTarget();
    updatePresetBox();

    const auto s = proc.currentSettings();
    const Layout layout (s.bandsPerOctave, s.extendedRange);
    for (int f = 0; f < kNumFocus; ++f)
    {
        static const char* chShort[] = { "", " / Mid", " / Side" };
        targetButtons[f].setButtonText (jp (targetName (f)) + "\n" + (s.focus[(size_t) f].enabled ? "ON" : "OFF")
                                        + chShort[(int) s.focus[(size_t) f].channel]);
        targetButtons[f].setToggleState (f == s.selected, juce::dontSendNotification);
    }
    for (int i = 0; i < 3; ++i)
        listenButtons[i].setToggleState ((int) s.listen == i, juce::dontSendNotification);
    const int active = proc.getActiveSlot();
    for (int i = 0; i < 2; ++i)
        abButtons[i].setToggleState (i == active, juce::dontSendNotification);
    abCopy.setButtonText (active == 0 ? jp ("A→B") : jp ("B→A"));
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());

    const auto& sf = s.focus[(size_t) s.selected];
    for (int i = 0; i < 3; ++i)
    {
        channelButtons[i].setToggleState ((int) sf.channel == i, juce::dontSendNotification);
        channelButtons[i].setColour (juce::TextButton::buttonOnColourId, Colours::focus (s.selected));
        channelButtons[i].setEnabled (sf.enabled);
    }
    for (auto* k : { &lowKnob, &highKnob, &procKnob, &intKnob, &attackKnob, &releaseKnob })
        k->slider.setEnabled (sf.enabled);
    rangeWarning.setVisible (sf.enabled && outsideLayout (sf, layout));
    shelfFreqKnob.slider.setEnabled (s.shelfOn);
    shelfGainKnob.slider.setEnabled (s.shelfOn);

    auto& m = proc.engine.meters;
    const float progress = m.learnProgress.load();
    juce::String status;
    if (progress >= 0.0f)
        status = jp ("学習中… ") + juce::String (juce::roundToInt (progress * 100.0f)) + " %";
    else if (s.reference == Reference::Learned)
        status = m.profileValid.load() ? jp ("学習済み（固定中）") : jp ("未学習：再生しながら Learn を押してください");
    else if (s.reference == Reference::Auto)
        status = m.learnedSeconds.load() < 10.0f ? jp ("オート：学習中… ") + juce::String (juce::roundToInt (m.learnedSeconds.load())) + jp (" 秒")
                                                  : jp ("オート：追従中");
    else
        status = jp ("固定の推定値を使用中");
    learnStatus.setText (status, juce::dontSendNotification);

    spectrum.tick();
    prominence.tick();
    history.tick();
    curve.repaint();
}

void SasariEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Colours::bg));
    g.setColour (juce::Colour (Colours::text));
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText (jp ("刺さりを抑えるソフト"), 14, 8, 260, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (Colours::dim));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("Sasari Osaeru 2.1  -  harshness / sibilance suppressor", 14, 34, 320, 14, juce::Justification::centredLeft);

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title) {
        g.setColour (juce::Colour (Colours::panel));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colour (Colours::stroke));
        g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);
        g.setColour (juce::Colour (Colours::dim));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 10, r.getY() + 4, r.getWidth() - 20, 16, juce::Justification::centredLeft);
    };
    panel ({ 12, 388, 976, 192 }, jp ("ターゲット（抑えたい音の種類）"));
    panel ({ 12, 586, 976, 146 }, jp ("共通 ／ 高域シェルフ ／ 検出"));
}

void SasariEditor::layoutBody()
{
    body.setBounds (0, 0, designWidth, designHeight);

    presetBox.setBounds (300, 12, 190, 28);
    abButtons[0].setBounds (500, 12, 34, 28);
    abButtons[1].setBounds (536, 12, 34, 28);
    abCopy.setBounds (572, 12, 52, 28);
    undoButton.setBounds (634, 12, 76, 28);
    redoButton.setBounds (712, 12, 76, 28);
    for (int i = 0; i < 3; ++i)
        listenButtons[i].setBounds (798 + i * 64, 12, 62, 28);

    curve.setBounds (12, 54, 230, 256);
    spectrum.setBounds (252, 54, 736, 190);
    prominence.setBounds (252, 248, 736, 62);
    history.setBounds (12, 316, 976, 66);

    for (int f = 0; f < kNumFocus; ++f)
        targetButtons[f].setBounds (22 + f * 118, 410, 114, 40);
    targetOn.setBounds (600, 410, 300, 26);
    channelLabel.setBounds (600, 440, 70, 24);
    for (int i = 0; i < 3; ++i)
        channelButtons[i].setBounds (672 + i * 92, 438, 90, 28);
    rangeWarning.setBounds (600, 470, 380, 30);
    targetHint.setBounds (600, 500, 380, 76);
    const int ky = 460;
    Knob* targetKnobs[] = { &lowKnob, &highKnob, &procKnob, &intKnob, &attackKnob, &releaseKnob };
    for (int i = 0; i < 6; ++i)
        targetKnobs[i]->setBounds (22 + i * 94, ky, 90, 112);

    const int gy = 608;
    Knob* commonKnobs[] = { &sharpKnob, &mixKnob, &rangeKnob, &outKnob, &selectivityKnob };
    for (int i = 0; i < 5; ++i)
        commonKnobs[i]->setBounds (18 + i * 82, gy, 80, 108);
    shelfOn.setBounds (432, 598, 160, 24);
    shelfFreqKnob.setBounds (428, gy + 20, 80, 96);
    shelfGainKnob.setBounds (508, gy + 20, 80, 96);

    const int cx = 604, cw = 186;
    referenceLabel.setBounds (cx, 596, cw, 14);
    referenceBox.setBounds (cx, 610, cw - 6, 24);
    resolutionLabel.setBounds (cx + cw, 596, cw, 14);
    resolutionBox.setBounds (cx + cw, 610, cw - 6, 24);
    analysisLabel.setBounds (cx, 638, cw, 14);
    analysisBox.setBounds (cx, 652, cw - 6, 24);
    learnButton.setBounds (cx + cw, 650, 120, 28);
    learnStatus.setBounds (cx, 684, 2 * cw, 28);
    presetDialog.setBounds (0, 0, designWidth, designHeight);
}

void SasariEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    body.setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        proc.editorScale = scale;
}
