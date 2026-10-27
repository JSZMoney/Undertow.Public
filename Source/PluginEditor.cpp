#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour bg        { 0xff0d1015 };
    const juce::Colour panelTop  { 0xff222933 };
    const juce::Colour panelBot  { 0xff181d24 };
    const juce::Colour border    { 0xff2e3642 };
    const juce::Colour text      { 0xffdde3ec };
    const juce::Colour dim       { 0xff8a95a5 };
    const juce::Colour accent    { 0xffffa53a };   // amber
    const juce::Colour edge      { 0xff3cc9b9 };   // teal (Edge section)
    const juce::Colour track     { 0xff313945 };
    const juce::Colour knobHi    { 0xff3a424f };
    const juce::Colour knobLo    { 0xff12161c };
}

static constexpr int kW = 1000, kH = 760;

//==============================================================================
UndertowLook::UndertowLook()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, Colours::dim);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::knobLo);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelTop);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::border);
}

juce::Label* UndertowLook::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.5f));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, Colours::text);
    return l;
}

void UndertowLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const bool isEdge = s.getProperties().getWithDefault ("edge", false);
    const auto size = (Knob::Size) (int) s.getProperties().getWithDefault ("size", 2);
    const auto accent = isEdge ? Colours::edge : Colours::accent;

    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto c = bounds.getCentre();
    const float R = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle = start + pos * (end - start);

    if (size == Knob::Size::Mini)
    {
        // small trim knob with a slot, like a trimmer screw
        const float r = R - 1.0f;
        g.setGradientFill (juce::ColourGradient (Colours::knobHi.brighter (0.2f), c.x - r, c.y - r, Colours::knobLo, c.x + r, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.0f);
        juce::Path slot;
        slot.addRoundedRectangle (-r * 0.75f, -1.2f, r * 1.5f, 2.4f, 1.2f);
        g.setColour (accent.withAlpha (0.85f));
        g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        return;
    }

    // Ring of dots that light up to the current value
    const int numDots = size == Knob::Size::Large ? 13 : 11;
    const float dotR = size == Knob::Size::Large ? 2.6f : 2.1f;
    const float ringR = R - dotR - 1.0f;
    for (int i = 0; i < numDots; ++i)
    {
        const float t = (float) i / (float) (numDots - 1);
        const float a = start + t * (end - start);
        const auto p = c.getPointOnCircumference (ringR, a);
        const bool lit = s.isEnabled() && t <= pos + 0.001f && pos > 0.0005f;
        if (lit)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillEllipse (p.x - dotR * 2, p.y - dotR * 2, dotR * 4, dotR * 4);
        }
        g.setColour (lit ? accent : Colours::track.brighter (0.15f));
        g.fillEllipse (p.x - dotR, p.y - dotR, dotR * 2, dotR * 2);
    }

    // Knob body
    const float r = ringR - dotR * 2.6f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - r, c.y - r + 3.0f, r * 2, r * 2);                       // drop shadow
    g.setGradientFill (juce::ColourGradient (Colours::knobHi, c.x - r * 0.6f, c.y - r, Colours::knobLo, c.x + r * 0.6f, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    const float ir = r * 0.78f;
    g.setGradientFill (juce::ColourGradient (Colours::knobLo.brighter (0.15f), c.x, c.y - ir, Colours::knobHi.darker (0.2f), c.x, c.y + ir, false));
    g.fillEllipse (c.x - ir, c.y - ir, ir * 2, ir * 2);                          // concave face
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, r * 2 - 1, r * 2 - 1, 1.0f);

    // Pointer
    juce::Path pointer;
    const float pw = size == Knob::Size::Large ? 4.0f : 3.0f;
    pointer.addRoundedRectangle (-pw * 0.5f, -r + 3.0f, pw, r * 0.42f, pw * 0.5f);
    g.setColour (s.isEnabled() ? accent : Colours::dim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void UndertowLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    const auto accent = b.getProperties().getWithDefault ("edge", false) ? Colours::edge : Colours::accent;
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);

    if (on)
    {
        g.setColour (accent.withAlpha (0.3f));
        g.fillEllipse (r.expanded (3.0f));
    }
    g.setGradientFill (juce::ColourGradient (on ? accent.brighter (0.3f) : Colours::knobHi.brighter (highlighted ? 0.25f : 0.1f), r.getX(), r.getY(),
                                             on ? accent.darker (0.3f) : Colours::knobLo, r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
}

//==============================================================================
juce::Point<int> Knob::sizeFor (Size s)
{
    switch (s)
    {
        case Size::Mini:   return { 26, 26 };
        case Size::Small:  return { 96, 108 };
        case Size::Medium: return { 112, 124 };
        case Size::Large:  return { 176, 190 };
    }
    return { 112, 124 };
}

Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& title, Size sz, bool edgeColour)
    : attach (s, id, slider), size (sz)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.getProperties().set ("size", (int) sz);
    slider.getProperties().set ("edge", edgeColour);
    auto* param = s.getParameter (id);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.setPopupDisplayEnabled (sz == Size::Mini, true, nullptr);
    if (sz == Size::Mini)
    {
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setTooltip (param->getName (64));
    }
    else
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    }
    addAndMakeVisible (slider);

    if (sz != Size::Mini)
    {
        label.setText (title, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Colours::text.withAlpha (0.85f));
        label.setFont (juce::FontOptions (sz == Size::Large ? 15.0f : 13.0f));
        addAndMakeVisible (label);
    }
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (size != Size::Mini) label.setBounds (r.removeFromTop (size == Size::Large ? 20 : 17));
    slider.setBounds (r);
}

//==============================================================================
Switch::Switch (juce::RangedAudioParameter& p, const juce::String& l, const juce::String& r, bool leftOne, bool edgeColour)
    : leftText (l), rightText (r), leftMeansOne (leftOne), edge (edgeColour),
      attachment (p, [this] (float v) { value = v; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Switch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float pillW = 52.0f, pillH = 24.0f;
    auto pill = juce::Rectangle<float> (pillW, pillH).withCentre (r.getCentre());
    const auto accent = edge ? Colours::edge : Colours::accent;

    g.setFont (juce::FontOptions (13.5f));
    g.setColour (isLeft() ? Colours::text : Colours::dim);
    g.drawText (leftText, r.withRight (pill.getX() - 10.0f), juce::Justification::centredRight);
    g.setColour (! isLeft() ? Colours::text : Colours::dim);
    g.drawText (rightText, r.withLeft (pill.getRight() + 10.0f), juce::Justification::centredLeft);

    g.setColour (Colours::knobLo);
    g.fillRoundedRectangle (pill, pillH * 0.5f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (pill, pillH * 0.5f, 1.0f);

    const float d = pillH - 6.0f;
    auto thumb = juce::Rectangle<float> (d, d).withCentre ({ isLeft() ? pill.getX() + 3.0f + d * 0.5f : pill.getRight() - 3.0f - d * 0.5f, pill.getCentreY() });
    g.setGradientFill (juce::ColourGradient (accent.brighter (0.3f), thumb.getX(), thumb.getY(), accent.darker (0.35f), thumb.getRight(), thumb.getBottom(), false));
    g.fillEllipse (thumb);
}

void Switch::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    attachment.setValueAsCompleteGesture (value > 0.5f ? 0.0f : 1.0f);
}

//==============================================================================
RoundButton::RoundButton (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& tip, bool edgeColour)
    : attach (s, id, button)
{
    button.getProperties().set ("edge", edgeColour);
    button.setTooltip (tip);
    button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (button);
}

//==============================================================================
static float toDb (float lin) { return lin > 1e-7f ? 20.0f * std::log10 (lin) : -140.0f; }

void LedLadder::setLevel (float lin)
{
    const float n = toDb (lin);
    db = n > db ? n : juce::jmax (n, db - 1.2f);   // ~36 dB/s release at 30 fps
    repaint();
}

void LedLadder::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float gap = 2.0f;
    const float segW = (r.getWidth() - gap * (float) (segments - 1)) / (float) segments;
    const float d = juce::jmin (segW, r.getHeight());
    const float pos = dbToPos (db);

    for (int i = 0; i < segments; ++i)
    {
        const float segPos = (float) (i + 1) / (float) segments;
        const float segDb = segPos * 42.0f - 42.0f;
        const auto col = segDb > -3.0f ? juce::Colour (0xffff4a3d) : segDb > -12.0f ? juce::Colour (0xffffd23a) : juce::Colour (0xff3ee07a);
        const bool lit = pos >= segPos - 0.5f / (float) segments;
        auto seg = juce::Rectangle<float> (r.getX() + (float) i * (segW + gap), r.getY(), segW, r.getHeight()).withSizeKeepingCentre (d, d);
        if (lit)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (seg.expanded (2.0f));
        }
        g.setColour (lit ? col : col.withMultipliedBrightness (0.22f).withMultipliedSaturation (0.6f));
        g.fillEllipse (seg);
    }
}

void SignalLed::setLevel (float lin)
{
    // Lights from -48 dBFS (dim) to -12 dBFS (full)
    const float target = juce::jlimit (0.0f, 1.0f, (toDb (lin) + 48.0f) / 36.0f);
    glow = target > glow ? target : glow * 0.85f;
    repaint();
}

void SignalLed::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    if (glow > 0.02f)
    {
        g.setColour (Colours::accent.withAlpha (glow * 0.4f));
        g.fillEllipse (r.expanded (3.5f));
    }
    g.setColour (Colours::track.interpolatedWith (Colours::accent, glow));
    g.fillEllipse (r);
}

void SubScope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::knobLo);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    auto plot = r.reduced (10.0f, 8.0f);
    g.setColour (Colours::track.withAlpha (0.6f));
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 8.0f), plot.getY(), plot.getBottom());

    constexpr int n = undertow::SubEngine::kScopeSize;
    const int w = engine.scopeWrite.load (std::memory_order_acquire);
    std::array<float, (size_t) n> buf {};
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        buf[(size_t) i] = engine.scope[(size_t) ((w + i) % n)].load (std::memory_order_relaxed);
        peak = std::max (peak, std::abs (buf[(size_t) i]));
    }
    // Auto-zoom so quiet subs are visible, without ever drawing a waveform clipped that isn't.
    displayPeak = peak > displayPeak ? peak : displayPeak * 0.97f + peak * 0.03f;
    const float scale = 0.9f / std::max (displayPeak, 0.003f);

    juce::Path p;
    for (int i = 0; i < n; ++i)
    {
        const float v = buf[(size_t) i] * scale;
        const float px = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        const float py = plot.getCentreY() - v * plot.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
    }
    const bool over = peak >= 1.0f;
    const auto col = over ? juce::Colour (0xffff4a3d) : Colours::accent;
    g.setColour (col.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

    g.setFont (juce::FontOptions (10.5f));
    g.setColour (over ? col : Colours::dim.withAlpha (0.8f));
    const juce::String lvl = peak < 1e-5f ? juce::String ("-inf") : juce::String (20.0f * std::log10 (peak), 1);
    g.drawText ("peak " + lvl + " dBFS" + (over ? "  OVER" : ""), r.reduced (10.0f, 6.0f), juce::Justification::topRight);
    g.setColour (Colours::dim.withAlpha (0.8f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("SUB OUT", r.reduced (10.0f, 6.0f), juce::Justification::topLeft);
}

//==============================================================================
UndertowView::UndertowView (UndertowProcessor& p)
    : proc (p),
      synthInput (*p.apvts.getParameter (ParamIDs::midMode), "Mid", "Stereo", true),
      gainIn (p.apvts, ParamIDs::gainIn, "Gain In", Knob::Size::Medium),
      gainOut (p.apvts, ParamIDs::output, "Gain Out", Knob::Size::Medium),
      mix (p.apvts, ParamIDs::mix, "Mix", Knob::Size::Medium),
      monoMaker (p.apvts, ParamIDs::monoMaker, "Mono Maker", Knob::Size::Medium),
      width (p.apvts, ParamIDs::width, "Stereo Width", Knob::Size::Medium),
      subMaster (p.apvts, ParamIDs::subMaster, "Subharmonics", Knob::Size::Large),
      scope (p.engine),
      edgePower (p.apvts, ParamIDs::edgeOn, "Edge on/off", true),
      edgeMode (*p.apvts.getParameter (ParamIDs::edgeMode), "Smooth", "Harsh", false, true),
      squeeze (p.apvts, ParamIDs::squeeze, "Squeeze", Knob::Size::Medium, true),
      drive (p.apvts, ParamIDs::drive, "Drive", Knob::Size::Medium, true),
      lowCut (p.apvts, ParamIDs::edgeLowCut, "Low Cut", Knob::Size::Medium, true),
      highCut (p.apvts, ParamIDs::edgeHighCut, "High Cut", Knob::Size::Medium, true),
      edgeLevel (p.apvts, ParamIDs::edgeLevel, "Edge Level", Knob::Size::Small, true),
      tightPunch (p.apvts, ParamIDs::tightPunch, "Tight Punch", Knob::Size::Medium),
      lowEnd (p.apvts, ParamIDs::lowEnd, "Low End", Knob::Size::Medium),
      limitBtn (p.apvts, ParamIDs::limit, "Safety limiter: keeps the output from going over -0.3 dBFS")
{
    setLookAndFeel (&look);

    for (auto* c : std::initializer_list<juce::Component*> { &synthInput, &gainIn, &gainOut, &mix, &monoMaker, &width, &inL, &inR,
                                                             &subMaster, &scope, &edgePower, &edgeMode, &squeeze, &drive, &lowCut,
                                                             &highCut, &edgeLevel, &tightPunch, &lowEnd, &outL, &outR, &limitBtn })
        addAndMakeVisible (*c);

    synthInput.setTooltip ("Mid: subs are made from L+R and stay centred.  Stereo: separate subs per side.");
    monoMaker.slider.setTooltip ("Everything below this frequency is made mono");
    tightPunch.slider.setTooltip ("Resonant high-pass before synthesis: tightens boomy lows, adds punch");
    lowEnd.slider.setTooltip ("Low shelf (100 Hz) on the original signal");

    const char* ranges[3] { "24-36 Hz", "36-56 Hz", "56-80 Hz" };
    for (size_t b = 0; b < 3; ++b)
    {
        levels[b] = std::make_unique<Knob> (p.apvts, ParamIDs::level[b], ranges[b], Knob::Size::Small);
        trims[b]  = std::make_unique<Knob> (p.apvts, ParamIDs::trim[b], "", Knob::Size::Mini);
        solos[b]  = std::make_unique<RoundButton> (p.apvts, ParamIDs::solo[b], "Solo this band (mutes the original)");
        bandMeters[b] = std::make_unique<LedLadder> (10);
        addAndMakeVisible (*levels[b]);
        addAndMakeVisible (*trims[b]);
        addAndMakeVisible (*solos[b]);
        addAndMakeVisible (*bandMeters[b]);
        addAndMakeVisible (leds[b]);
        leds[b].setTooltip ("Input activity: lights when there is material in this band to fold down");
    }

    setSize (kW, kH);
    startTimerHz (30);
}

UndertowView::~UndertowView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void UndertowView::timerCallback()
{
    auto& e = proc.engine;
    for (size_t b = 0; b < 3; ++b)
    {
        leds[b].setLevel (e.meterBandIn[b].exchange (0.0f));
        bandMeters[b]->setLevel (e.meterBandOut[b].exchange (0.0f));
    }
    inL.setLevel (e.meterIn[0].exchange (0.0f));
    inR.setLevel (e.meterIn[1].exchange (0.0f));
    outL.setLevel (e.meterOut[0].exchange (0.0f));
    {
        const float gr = e.meterLimitGR.exchange (0.0f);
        const float grDb = gr > 0.0f ? -20.0f * std::log10 (1.0f - gr) : 0.0f;
        limitGrDb = grDb > limitGrDb ? grDb : limitGrDb * 0.9f;
        repaint (limitCaption);
    }
    outR.setLevel (e.meterOut[1].exchange (0.0f));
    scope.repaint();

    const bool edgeOn = edgePower.button.getToggleState();
    if (edgeOn != lastEdgeOn) { lastEdgeOn = edgeOn; repaint (edgePower.getBounds().translated (0, 22).withHeight (12).expanded (6, 0)); }
    for (auto* k : { &squeeze, &drive, &lowCut, &highCut, &edgeLevel })
        k->setAlpha (edgeOn ? 1.0f : 0.4f);
    edgeMode.setAlpha (edgeOn ? 1.0f : 0.4f);
}

//==============================================================================
static void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
{
    const auto rf = r.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (rf.translated (0.0f, 3.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (Colours::panelTop, rf.getX(), rf.getY(), Colours::panelBot, rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle (rf, 12.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (rf.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle (rf.withHeight (3.0f).reduced (24.0f, 0.0f).translated (0.0f, 1.0f), 1.5f);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), r.withHeight (46), juce::Justification::centred);
    g.setColour (Colours::border);
    g.drawHorizontalLine (r.getY() + 46, rf.getX() + 18.0f, rf.getRight() - 18.0f);
}

static void drawDivider (juce::Graphics& g, juce::Rectangle<int> panel, int y, const juce::String& text)
{
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 20.0f;
    const float cx = (float) panel.getCentreX();
    g.setColour (Colours::border.brighter (0.2f));
    g.drawHorizontalLine (y, (float) panel.getX() + 18.0f, cx - tw * 0.5f);
    g.drawHorizontalLine (y, cx + tw * 0.5f, (float) panel.getRight() - 18.0f);
    g.setColour (Colours::text.withAlpha (0.9f));
    g.drawText (text, juce::Rectangle<float> (cx - tw * 0.5f, (float) y - 10.0f, tw, 20.0f), juce::Justification::centred);
}

static void drawCaption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, float size = 13.5f)
{
    g.setColour (Colours::text.withAlpha (0.85f));
    g.setFont (juce::FontOptions (size));
    g.drawText (text, r, juce::Justification::centred);
}

static void drawMeterScale (juce::Graphics& g, juce::Rectangle<int> meter, int y)
{
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.5f));
    for (int db : { -40, -30, -20, -12, -6, -3, 0 })
    {
        const float x = (float) meter.getX() + (float) meter.getWidth() * LedLadder::dbToPos ((float) db);
        g.drawText (juce::String (-db), juce::Rectangle<float> (x - 15.0f, (float) y, 30.0f, 12.0f), juce::Justification::centred);
    }
}

void UndertowView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // Global
    drawPanel (g, globalPanel, "Global", Colours::accent);
    drawCaption (g, { globalPanel.getX(), globalPanel.getY() + 56, globalPanel.getWidth(), 18 }, "Synthesis Input");
    drawDivider (g, globalPanel, globalPanel.getY() + 400, "M/S");
    drawDivider (g, globalPanel, globalPanel.getBottom() - 92, "Input");

    // Sub
    drawPanel (g, subPanel, "Sub", Colours::accent);
    {
        const auto fg = freqGroup.toFloat();
        g.setColour (Colours::border.brighter (0.25f));
        g.drawRoundedRectangle (fg, 10.0f, 1.0f);
        const juce::String cap ("Synthesized Frequency");
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), cap) + 22.0f;
        auto capBox = juce::Rectangle<float> (fg.getCentreX() - tw * 0.5f, fg.getY() - 10.0f, tw, 20.0f);
        g.setGradientFill (juce::ColourGradient (Colours::panelTop, 0.0f, (float) subPanel.getY(), Colours::panelBot, 0.0f, (float) subPanel.getBottom(), false));
        g.fillRect (capBox);
        g.setColour (Colours::text.withAlpha (0.9f));
        g.drawText (cap, capBox, juce::Justification::centred);

        for (size_t b = 0; b < 3; ++b)
        {
            const auto cell = bandCells[b];
            g.setColour (Colours::dim);
            g.setFont (juce::FontOptions (10.5f));
            const int captionY = trims[b]->getBottom() + 2;
            g.drawText ("TRIM", trims[b]->getBounds().withY (captionY).withHeight (12).expanded (8, 0), juce::Justification::centred);
            g.drawText ("SOLO", solos[b]->getBounds().withY (captionY).withHeight (12).expanded (8, 0), juce::Justification::centred);
            juce::ignoreUnused (cell);
        }
    }
    {
        // wordmark
        auto wm = juce::Rectangle<int> (subPanel.getX(), scope.getBottom() + 8, subPanel.getWidth(), subPanel.getBottom() - scope.getBottom() - 12);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText ("UNDERTOW", wm.removeFromTop (wm.getHeight() / 2 + 4), juce::Justification::centredBottom);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("subharmonic synthesizer", wm, juce::Justification::centredTop);
    }

    // Edge
    drawPanel (g, edgePanel, "Edge", Colours::edge);
    drawCaption (g, { edgePanel.getX(), edgePanel.getY() + 56, edgePanel.getWidth(), 18 }, "Mode");
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (edgePower.button.getToggleState() ? "ON" : "OFF", edgePower.getBounds().translated (0, 22).withHeight (12).expanded (6, 0), juce::Justification::centred);
    drawDivider (g, edgePanel, edgePanel.getY() + 500, "Filter");
    drawDivider (g, edgePanel, edgePanel.getBottom() - 92, "Output");
    {
        // limiter button + caption sit on the right end of the Output divider
        g.setColour (Colours::panelBot);
        g.fillRect (limitCaption.getUnion (limitBtn.getBounds()).expanded (4, 0));
        const bool on = limitBtn.button.getToggleState();
        const bool active = on && limitGrDb > 0.1f;
        g.setColour (active ? Colours::accent : (on ? Colours::text.withAlpha (0.8f) : Colours::dim));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (active ? "LIMIT -" + juce::String (limitGrDb, 1) : juce::String ("LIMIT"), limitCaption, juce::Justification::centredRight);
    }

    // L/R labels + scales for the I/O meters
    g.setColour (Colours::text.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (auto* m : { &inL, &outL }) g.drawText ("L", m->getBounds().withX (m->getX() - 20).withWidth (14), juce::Justification::centred);
    for (auto* m : { &inR, &outR }) g.drawText ("R", m->getBounds().withX (m->getX() - 20).withWidth (14), juce::Justification::centred);
    drawMeterScale (g, inR.getBounds(), inR.getBottom() + 5);
    drawMeterScale (g, outR.getBounds(), outR.getBottom() + 5);
}

void UndertowView::placeKnob (Knob& k, Knob::Size s, int cx, int top)
{
    const auto sz = Knob::sizeFor (s);
    k.setBounds (cx - sz.x / 2, top, sz.x, sz.y);
}

void UndertowView::resized()
{
    const int m = 10, gap = 10, sideW = 290;
    const int ph = kH - 2 * m;
    globalPanel = { m, m, sideW, ph };
    subPanel    = { globalPanel.getRight() + gap, m, kW - 2 * m - 2 * gap - 2 * sideW, ph };
    edgePanel   = { subPanel.getRight() + gap, m, sideW, ph };

    auto placeMeters = [] (juce::Rectangle<int> panel, LedLadder& l, LedLadder& r)
    {
        const int x = panel.getX() + 40, w = panel.getWidth() - 62;
        l.setBounds (x, panel.getBottom() - 70, w, 11);
        r.setBounds (x, panel.getBottom() - 52, w, 11);
    };

    // ---- Global
    {
        const auto P = globalPanel;
        const int cx = P.getCentreX(), y = P.getY();
        synthInput.setBounds (P.getX() + 20, y + 78, P.getWidth() - 40, 30);
        placeKnob (gainIn,  Knob::Size::Medium, cx - 68, y + 124);
        placeKnob (gainOut, Knob::Size::Medium, cx + 68, y + 124);
        placeKnob (mix,     Knob::Size::Medium, cx,      y + 260);
        placeKnob (monoMaker, Knob::Size::Medium, cx - 68, y + 420);
        placeKnob (width,     Knob::Size::Medium, cx + 68, y + 420);
        placeMeters (P, inL, inR);
    }

    // ---- Sub
    {
        const auto P = subPanel;
        const int cx = P.getCentreX(), y = P.getY();
        placeKnob (subMaster, Knob::Size::Large, cx, y + 54);
        freqGroup = { P.getX() + 14, y + 262, P.getWidth() - 28, 306 };

        const int cellW = 156, cellH = 128;
        bandCells[2] = { cx - cellW / 2, y + 280, cellW, cellH };                        // High (56–110) on top
        bandCells[0] = { freqGroup.getX() + 10, y + 428, cellW, cellH };                 // Low (24–36)
        bandCells[1] = { freqGroup.getRight() - 10 - cellW, y + 428, cellW, cellH };     // Mid (36–56)

        for (size_t b = 0; b < 3; ++b)
        {
            const auto c = bandCells[b];
            placeKnob (*levels[b], Knob::Size::Small, c.getCentreX(), c.getY());
            const int knobCy = c.getY() + 17 + 36;
            trims[b]->setBounds (c.getX(), knobCy - 13, 26, 26);
            solos[b]->setBounds (c.getRight() - 22, knobCy - 10, 20, 20);
            leds[b].setBounds (c.getRight() - 24, c.getY() + 2, 14, 14);
            bandMeters[b]->setBounds (c.getCentreX() - 52, c.getY() + 113, 104, 9);
        }

        scope.setBounds (P.getX() + 14, freqGroup.getBottom() + 12, P.getWidth() - 28, 96);
    }

    // ---- Edge
    {
        const auto P = edgePanel;
        const int cx = P.getCentreX(), y = P.getY();
        edgePower.setBounds (P.getRight() - 46, y + 13, 22, 22);
        edgeMode.setBounds (P.getX() + 20, y + 78, P.getWidth() - 40, 30);
        placeKnob (squeeze, Knob::Size::Medium, cx - 68, y + 124);
        placeKnob (drive,   Knob::Size::Medium, cx + 68, y + 124);
        placeKnob (lowCut,  Knob::Size::Medium, cx - 68, y + 254);
        placeKnob (highCut, Knob::Size::Medium, cx + 68, y + 254);
        placeKnob (edgeLevel, Knob::Size::Small, cx,     y + 382);
        placeKnob (tightPunch, Knob::Size::Medium, cx - 68, y + 514);
        placeKnob (lowEnd,     Knob::Size::Medium, cx + 68, y + 514);
        placeMeters (P, outL, outR);
        limitBtn.setBounds (P.getRight() - 40, P.getBottom() - 102, 20, 20);
        limitCaption = { P.getRight() - 112, P.getBottom() - 102, 68, 20 };
    }
}

//==============================================================================
static constexpr const char* kUiWidthProp = "uiWidth";

UndertowEditor::UndertowEditor (UndertowProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);   // corner drag handle
    setResizeLimits (kW * 7 / 10, kH * 7 / 10, kW * 16 / 10, kH * 16 / 10);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);

    // Reopen at the size it was last left at
    const int w = juce::jlimit (kW * 7 / 10, kW * 16 / 10, (int) p.apvts.state.getProperty (kUiWidthProp, kW));
    setSize (w, juce::roundToInt ((double) w * kH / kW));
}

void UndertowEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, kW, kH);
    proc.apvts.state.setProperty (kUiWidthProp, getWidth(), nullptr);
}
