#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour bg       { 0xff12151c };
    const juce::Colour panel    { 0xff1b2029 };
    const juce::Colour panelHi  { 0xff252b36 };
    const juce::Colour text     { 0xffd9dee7 };
    const juce::Colour dim      { 0xff7d8696 };
    const juce::Colour accent   { 0xffff9f2e };   // amber
    const juce::Colour accent2  { 0xff35c4b5 };   // teal (edge section)
    const juce::Colour track    { 0xff2f3643 };
}

//==============================================================================
UndertowLook::UndertowLook()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::dim);
    setColour (juce::ComboBox::backgroundColourId, Colours::panelHi);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::track);
    setColour (juce::ComboBox::arrowColourId, Colours::accent2);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent2.withAlpha (0.3f));
}

void UndertowLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.getProperties().getWithDefault ("edge", false) ? Colours::accent2 : Colours::accent;
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.12f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = start + pos * (end - start);

    juce::Path trackArc;
    trackArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colours::track);
    g.strokePath (trackArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (s.isEnabled() && pos > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, angle, true);
        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float capR = arcR - lineW * 1.6f;
    g.setGradientFill (juce::ColourGradient (Colours::panelHi.brighter (0.25f), c.x, c.y - capR, Colours::panel.darker (0.4f), c.x, c.y + capR, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capR + 3.0f, 3.0f, capR * 0.45f, 1.5f);
    g.setColour (s.isEnabled() ? Colours::text : Colours::dim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void UndertowLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto on = b.getToggleState();
    const auto accent = b.getProperties().getWithDefault ("edge", false) ? Colours::accent2 : Colours::accent;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (on ? accent.withAlpha (0.9f) : (highlighted ? Colours::panelHi.brighter (0.1f) : Colours::panelHi));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? accent : Colours::track);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? Colours::bg : Colours::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void UndertowLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (Colours::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::track);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (Colours::accent2);
    g.fillPath (arrow);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& title, bool big)
    : attach (s, id, slider), isBig (big)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, (double) s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);

    label.setText (title.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (big ? 12.0f : 10.5f, juce::Font::bold));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (isBig ? 16 : 14));
    slider.setBounds (r);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : attach (s, id, button)
{
    button.setButtonText (text);
    addAndMakeVisible (button);
}

//==============================================================================
static float toDb (float lin) { return lin > 1e-6f ? 20.0f * std::log10 (lin) : -120.0f; }

void LevelMeter::setLevel (float lin)
{
    const float newDb = toDb (lin);
    db = newDb > db ? newDb : juce::jmax (newDb, db - 1.5f);   // ~45 dB/s fall-off at 30 fps
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::bg);
    g.fillRoundedRectangle (r, 2.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);   // -60 .. +6 dBFS
    auto fill = r.reduced (2.0f);
    fill = fill.removeFromBottom (fill.getHeight() * norm);
    g.setGradientFill (juce::ColourGradient (Colours::accent2, 0, r.getBottom(), juce::Colours::red, 0, r.getY(), false));
    g.fillRect (fill);
    // 0 dBFS tick
    const float y0 = r.getBottom() - 2.0f - (r.getHeight() - 4.0f) * (60.0f / 66.0f);
    g.setColour (Colours::dim.withAlpha (0.6f));
    g.drawHorizontalLine ((int) y0, r.getX(), r.getRight());
}

void SignalLed::setLevel (float lin)
{
    // Lights from -48 dBFS (dim) to -12 dBFS (full): shows which bands actually have material to fold down.
    const float target = juce::jlimit (0.0f, 1.0f, (toDb (lin) + 48.0f) / 36.0f);
    glow = target > glow ? target : glow * 0.85f;
    repaint();
}

void SignalLed::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (10.0f, 10.0f);
    if (glow > 0.02f)
    {
        g.setColour (Colours::accent.withAlpha (glow * 0.35f));
        g.fillEllipse (r.expanded (4.0f));
    }
    g.setColour (Colours::track.interpolatedWith (Colours::accent, glow));
    g.fillEllipse (r);
}

//==============================================================================
UndertowEditor::UndertowEditor (UndertowProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      tightPunch (p.apvts, ParamIDs::tightPunch, "Tight Punch", true),
      midMode (p.apvts, ParamIDs::midMode, "MONO SUB"),
      subMaster (p.apvts, ParamIDs::subMaster, "Subharmonics", true),
      original (p.apvts, ParamIDs::original, "Original"),
      output (p.apvts, ParamIDs::output, "Output"),
      edgeOn (p.apvts, ParamIDs::edgeOn, "EDGE"),
      squeeze (p.apvts, ParamIDs::squeeze, "Squeeze"),
      drive (p.apvts, ParamIDs::drive, "Drive"),
      lowCut (p.apvts, ParamIDs::edgeLowCut, "Low Cut"),
      highCut (p.apvts, ParamIDs::edgeHighCut, "High Cut"),
      edgeLevel (p.apvts, ParamIDs::edgeLevel, "Level")
{
    setLookAndFeel (&look);

    addAndMakeVisible (tightPunch);
    addAndMakeVisible (midMode);
    midMode.button.setTooltip ("Generate subs from the mid (L+R) signal only, so the low end stays centred");

    for (int b = 0; b < 3; ++b)
    {
        trims[b]  = std::make_unique<Knob> (p.apvts, ParamIDs::trim[b], "Trim");
        levels[b] = std::make_unique<Knob> (p.apvts, ParamIDs::level[b], "Sub", true);
        solos[b]  = std::make_unique<Toggle> (p.apvts, ParamIDs::solo[b], "SOLO");
        addAndMakeVisible (*trims[b]);
        addAndMakeVisible (*levels[b]);
        addAndMakeVisible (*solos[b]);
        addAndMakeVisible (bandMeters[b]);
        addAndMakeVisible (leds[b]);
    }

    addAndMakeVisible (subMaster);
    addAndMakeVisible (original);
    addAndMakeVisible (output);
    addAndMakeVisible (outMeter);

    for (auto* k : { &squeeze, &drive, &lowCut, &highCut, &edgeLevel })
    {
        k->slider.getProperties().set ("edge", true);
        addAndMakeVisible (*k);
    }
    edgeOn.button.getProperties().set ("edge", true);
    addAndMakeVisible (edgeOn);
    edgeMode.addItemList ({ "Smooth", "Harsh" }, 1);
    addAndMakeVisible (edgeMode);
    edgeModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ParamIDs::edgeMode, edgeMode);

    setSize (940, 480);
    startTimerHz (30);
}

UndertowEditor::~UndertowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void UndertowEditor::timerCallback()
{
    for (int b = 0; b < 3; ++b)
    {
        leds[b].setLevel (proc.engine.meterBandIn[b].exchange (0.0f));
        bandMeters[b].setLevel (proc.engine.meterBandOut[b].exchange (0.0f));
    }
    outMeter.setLevel (proc.engine.meterOut.exchange (0.0f));

    const bool edgeEnabled = edgeOn.button.getToggleState();
    for (auto* k : { &squeeze, &drive, &lowCut, &highCut, &edgeLevel })
        k->setAlpha (edgeEnabled ? 1.0f : 0.45f);
    edgeMode.setAlpha (edgeEnabled ? 1.0f : 0.45f);
}

void UndertowEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // Header
    auto header = getLocalBounds().removeFromTop (48).reduced (18, 0);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("UNDERTOW", header, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("3-band subharmonic synthesizer", header.withTrimmedLeft (140), juce::Justification::centredLeft);

    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.fillRoundedRectangle (r.toFloat().removeFromTop (3.0f).reduced (8.0f, 0.0f), 1.5f);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.withHeight (26).withTrimmedLeft (12), juce::Justification::centredLeft);
    };

    drawPanel (inputArea, "INPUT", Colours::accent);
    drawPanel (bandArea, "SUBHARMONIC BANDS", Colours::accent);
    drawPanel (masterArea, "MIX", Colours::accent);
    drawPanel (edgeArea, "EDGE", Colours::accent2);

    // Band names + ranges
    const char* names[3]  { "LOW", "MID", "HIGH" };
    const char* ranges[3] { "24-36 Hz", "36-56 Hz", "56-110 Hz" };
    const int colW = bandArea.getWidth() / 3;
    for (int b = 0; b < 3; ++b)
    {
        auto col = bandArea.withTrimmedTop (26).withX (bandArea.getX() + b * colW).withWidth (colW);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (names[b], col.removeFromTop (18), juce::Justification::centred);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (ranges[b], col.removeFromTop (14), juce::Justification::centred);
        if (b > 0)
        {
            g.setColour (Colours::track);
            g.drawVerticalLine (bandArea.getX() + b * colW, (float) bandArea.getY() + 30.0f, (float) bandArea.getBottom() - 12.0f);
        }
    }
}

void UndertowEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (36);
    const int gap = 10;

    inputArea  = r.removeFromLeft (130); r.removeFromLeft (gap);
    edgeArea   = r.removeFromRight (250); r.removeFromRight (gap);
    masterArea = r.removeFromRight (140); r.removeFromRight (gap);
    bandArea   = r;

    // Input
    {
        auto a = inputArea.withTrimmedTop (34).reduced (10, 8);
        tightPunch.setBounds (a.removeFromTop (130));
        a.removeFromTop (16);
        midMode.setBounds (a.removeFromTop (28));
    }

    // Bands
    {
        const int colW = bandArea.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = bandArea.withX (bandArea.getX() + b * colW).withWidth (colW).withTrimmedTop (62).reduced (10, 8);
            leds[b].setBounds (col.removeFromTop (16));
            col.removeFromTop (4);
            auto meterCol = col.removeFromRight (12);
            col.removeFromRight (6);
            trims[b]->setBounds (col.removeFromTop (78).withSizeKeepingCentre (70, 78));
            col.removeFromTop (6);
            levels[b]->setBounds (col.removeFromTop (120));
            col.removeFromTop (10);
            solos[b]->setBounds (col.removeFromTop (26).withSizeKeepingCentre (64, 26));
            bandMeters[b].setBounds (meterCol.withTrimmedTop (82).withTrimmedBottom (36));
        }
    }

    // Mix
    {
        auto a = masterArea.withTrimmedTop (30).reduced (10, 6);
        outMeter.setBounds (a.removeFromRight (10).withTrimmedTop (8).withTrimmedBottom (8));
        a.removeFromRight (4);
        subMaster.setBounds (a.removeFromTop (126));
        a.removeFromTop (14);
        original.setBounds (a.removeFromTop (100));
        a.removeFromTop (8);
        output.setBounds (a.removeFromTop (100));
    }

    // Edge
    {
        auto a = edgeArea.withTrimmedTop (34).reduced (12, 6);
        auto top = a.removeFromTop (28);
        edgeOn.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (10);
        edgeMode.setBounds (top);
        a.removeFromTop (14);
        const int kw = a.getWidth() / 2, kh = 104;
        auto row1 = a.removeFromTop (kh);
        squeeze.setBounds (row1.removeFromLeft (kw));
        drive.setBounds (row1);
        a.removeFromTop (6);
        auto row2 = a.removeFromTop (kh);
        lowCut.setBounds (row2.removeFromLeft (kw));
        highCut.setBounds (row2);
        a.removeFromTop (4);
        edgeLevel.setBounds (a.withSizeKeepingCentre (kw, juce::jmin (a.getHeight(), kh)));
    }
}
