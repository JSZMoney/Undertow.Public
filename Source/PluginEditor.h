#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class UndertowLook : public juce::LookAndFeel_V4
{
public:
    UndertowLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& title, bool big = false);
    void resized() override;
    juce::Slider slider;
private:
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
    bool isBig;
};

class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;
private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attach;
};

class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
private:
    float db = -100.0f;
};

class SignalLed : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;
private:
    float glow = 0.0f;
};

//==============================================================================
class UndertowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UndertowEditor (UndertowProcessor&);
    ~UndertowEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    UndertowProcessor& proc;
    UndertowLook look;

    Knob tightPunch;
    Toggle midMode;

    std::array<std::unique_ptr<Knob>, 3> trims, levels;
    std::array<std::unique_ptr<Toggle>, 3> solos;
    std::array<LevelMeter, 3> bandMeters;
    std::array<SignalLed, 3> leds;

    Knob subMaster, original, output;
    LevelMeter outMeter;

    Toggle edgeOn;
    juce::ComboBox edgeMode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> edgeModeAttach;
    Knob squeeze, drive, lowCut, highCut, edgeLevel;

    juce::Rectangle<int> inputArea, bandArea, masterArea, edgeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowEditor)
};
