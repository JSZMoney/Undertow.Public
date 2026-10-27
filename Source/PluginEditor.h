#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
class UndertowLook : public juce::LookAndFeel_V4
{
public:
    UndertowLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
// Rotary knob with a title above and an editable value below.
class Knob : public juce::Component
{
public:
    enum class Size { Mini, Small, Medium, Large };

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title, Size, bool edgeColour = false);
    void resized() override;
    static juce::Point<int> sizeFor (Size);

    juce::Slider slider;

private:
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
    Size size;
};

// Two-position switch with a label on each side (works for bool or 2-choice parameters).
class Switch : public juce::Component, public juce::SettableTooltipClient
{
public:
    Switch (juce::RangedAudioParameter&, const juce::String& leftText, const juce::String& rightText, bool leftMeansOne, bool edgeColour = false);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool isLeft() const { return (value > 0.5f) == leftMeansOne; }
    juce::String leftText, rightText;
    bool leftMeansOne, edge;
    float value = 0.0f;
    juce::ParameterAttachment attachment;
};

// Round LED push button (Solo / Edge power)
class RoundButton : public juce::Component
{
public:
    RoundButton (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& tooltip, bool edgeColour = false);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;
private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attach;
};

// Horizontal LED-ladder meter, -42..0 dBFS
class LedLadder : public juce::Component
{
public:
    explicit LedLadder (int numSegments) : segments (numSegments) {}
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
    static float dbToPos (float db) { return juce::jlimit (0.0f, 1.0f, (db + 42.0f) / 42.0f); }
private:
    int segments;
    float db = -100.0f;
};

// Small activity LED: shows which input regions have material to fold down
class SignalLed : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
private:
    float glow = 0.0f;
};

// Live oscilloscope of the generated subharmonic
class SubScope : public juce::Component
{
public:
    explicit SubScope (undertow::SubEngine& e) : engine (e) {}
    void paint (juce::Graphics&) override;
private:
    undertow::SubEngine& engine;
    float displayPeak = 0.0f;
};

//==============================================================================
// The whole interface, laid out at a fixed 1000 x 760 design size.
class UndertowView : public juce::Component, private juce::Timer
{
public:
    explicit UndertowView (UndertowProcessor&);
    ~UndertowView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void placeKnob (Knob&, Knob::Size, int centreX, int topY);

    UndertowProcessor& proc;
    UndertowLook look;
    juce::TooltipWindow tooltips { this, 600 };

    // Global
    Switch synthInput;
    Knob gainIn, gainOut, mix, monoMaker, width;
    LedLadder inL { 18 }, inR { 18 };

    // Sub
    Knob subMaster;
    std::array<std::unique_ptr<Knob>, 3> levels, trims;
    std::array<std::unique_ptr<RoundButton>, 3> solos;
    std::array<std::unique_ptr<LedLadder>, 3> bandMeters;
    std::array<SignalLed, 3> leds;
    SubScope scope;

    // Edge + filter + output
    RoundButton edgePower;
    Switch edgeMode;
    Knob squeeze, drive, lowCut, highCut, edgeLevel, tightPunch, lowEnd;
    LedLadder outL { 18 }, outR { 18 };
    RoundButton limitBtn;
    juce::Rectangle<int> limitCaption;
    float limitGrDb = 0.0f;

    bool lastEdgeOn = false;
    juce::Rectangle<int> globalPanel, subPanel, edgePanel, freqGroup;
    std::array<juce::Rectangle<int>, 3> bandCells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowView)
};

//==============================================================================
// The plugin window: hosts the view and scales it to whatever size the window is dragged to.
class UndertowEditor : public juce::AudioProcessorEditor
{
public:
    explicit UndertowEditor (UndertowProcessor&);
    void resized() override;

private:
    UndertowProcessor& proc;
    UndertowView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowEditor)
};
