#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static juce::String dbText (float v, int) { return v <= undertow::kMinDb ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }
static juce::String hzText (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
static juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

// Band level dial (0..1) to dB: gain = 4 * x^2, so 50% = 0 dB and 100% = +12 dB.
static float bandPercentToDb (float x)
{
    const float g = 4.0f * x * x;
    return g <= 0.001f ? undertow::kMinDb : 20.0f * std::log10 (g);
}

UndertowProcessor::UndertowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UNDERTOW", createLayout())
{
}

APVTS::ParameterLayout UndertowProcessor::createLayout()
{
    APVTS::ParameterLayout layout;
    using namespace juce;
    auto dbAttr = AudioParameterFloatAttributes().withStringFromValueFunction (dbText).withLabel ("dB");
    auto hzAttr = AudioParameterFloatAttributes().withStringFromValueFunction (hzText).withLabel ("Hz");
    auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction (pctText);

    auto dbRange = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi, 0.1f); };
    auto hzRange = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi, 0.1f); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };

    int v = 1;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tightPunch, v }, "Tight Punch",
        hzRange (10.0f, 250.0f), 10.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return x <= 12.0f ? String ("Off") : hzText (x, 0); })));

    const char* names[3] { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::trim[b], v }, String (names[b]) + " Trim", dbRange (0.0f, 18.0f), 0.0f, dbAttr));
        // Shown as a percentage like the original. Audio taper: 0% = off, 25% ≈ -12 dB,
        // 50% = 0 dB (default), 100% = +12 dB.
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::level[b], v }, String (names[b]) + " Sub",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pctAttr));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::solo[b], v }, String (names[b]) + " Solo", false));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::subMaster, v }, "Subharmonics",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pctAttr));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::midMode, v }, "Synthesis Input Mid", true));

    // Global
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::gainIn, v }, "Gain In", dbRange (-24.0f, 24.0f), 0.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, v }, "Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::monoMaker, v }, "Mono Maker",
        hzRange (20.0f, 500.0f), 20.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return x <= 20.5f ? String ("Off") : hzText (x, 0); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::width, v }, "Stereo Width", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lowEnd, v }, "Low End", dbRange (-12.0f, 12.0f), 0.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::edgeOn, v }, "Edge On", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::edgeMode, v }, "Edge Mode", StringArray { "Smooth", "Harsh" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::squeeze, v }, "Squeeze", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::drive, v }, "Drive", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, pctAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::edgeLowCut, v }, "Edge Low Cut", hzRange (20.0f, 1000.0f), 30.0f, hzAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::edgeHighCut, v }, "Edge High Cut", hzRange (200.0f, 20000.0f), 5000.0f, hzAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::edgeLevel, v }, "Edge Level", dbRange (undertow::kMinDb, 6.0f), -12.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, v }, "Gain Out", dbRange (-24.0f, 12.0f), 0.0f, dbAttr));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::limit, v }, "Safety Limiter", true));
    return layout;
}

bool UndertowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

undertow::Params UndertowProcessor::readParams() const
{
    auto f = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    undertow::Params p;
    p.tightPunchHz = f (ParamIDs::tightPunch);
    for (int b = 0; b < 3; ++b)
    {
        p.trimDb[b] = f (ParamIDs::trim[b]);
        p.levelDb[b] = bandPercentToDb (f (ParamIDs::level[b]));
        p.solo[b] = f (ParamIDs::solo[b]) > 0.5f;
    }
    p.subMasterDb = bandPercentToDb (f (ParamIDs::subMaster));
    p.gainInDb = f (ParamIDs::gainIn);
    p.mix = f (ParamIDs::mix);
    p.monoMakerHz = f (ParamIDs::monoMaker);
    p.width = f (ParamIDs::width);
    p.lowEndDb = f (ParamIDs::lowEnd);
    p.midMode = f (ParamIDs::midMode) > 0.5f;
    p.edgeOn = f (ParamIDs::edgeOn) > 0.5f;
    p.edgeMode = (int) f (ParamIDs::edgeMode);
    p.squeeze = f (ParamIDs::squeeze);
    p.drive = f (ParamIDs::drive);
    p.edgeLowCutHz = f (ParamIDs::edgeLowCut);
    p.edgeHighCutHz = f (ParamIDs::edgeHighCut);
    p.edgeLevelDb = f (ParamIDs::edgeLevel);
    p.outputDb = f (ParamIDs::output);
    p.limit = f (ParamIDs::limit) > 0.5f;
    return p;
}

void UndertowProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());   // the limiter's 2 ms look-ahead
}

void UndertowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void UndertowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void UndertowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* UndertowProcessor::createEditor() { return new UndertowEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UndertowProcessor(); }
