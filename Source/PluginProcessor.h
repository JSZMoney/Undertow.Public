#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SubEngine.h"

namespace ParamIDs
{
    inline const juce::String tightPunch { "tightPunch" };
    inline const juce::String trim[3]  { "trimLow",  "trimMid",  "trimHigh" };
    inline const juce::String level[3] { "levelLow", "levelMid", "levelHigh" };
    inline const juce::String solo[3]  { "soloLow",  "soloMid",  "soloHigh" };
    inline const juce::String subMaster { "subMaster" };
    inline const juce::String gainIn { "gainIn" };
    inline const juce::String mix { "mix" };
    inline const juce::String monoMaker { "monoMaker" };
    inline const juce::String width { "width" };
    inline const juce::String lowEnd { "lowEnd" };
    inline const juce::String midMode { "midMode" };
    inline const juce::String edgeOn { "edgeOn" };
    inline const juce::String edgeMode { "edgeMode" };
    inline const juce::String squeeze { "squeeze" };
    inline const juce::String drive { "drive" };
    inline const juce::String edgeLowCut { "edgeLowCut" };
    inline const juce::String edgeHighCut { "edgeHighCut" };
    inline const juce::String edgeLevel { "edgeLevel" };
    inline const juce::String output { "output" };
    inline const juce::String limit { "limit" };
}

class UndertowProcessor : public juce::AudioProcessor
{
public:
    UndertowProcessor();
    ~UndertowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    undertow::SubEngine engine;

private:
    undertow::Params readParams() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowProcessor)
};
