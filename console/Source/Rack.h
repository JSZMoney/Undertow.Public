#pragma once

#include <JuceHeader.h>
#include "SystemAudio.h"

// Three plug-in slots in series plus a master volume.
// Everything except render() runs on the message thread. render() runs on the audio thread:
// it only try-locks, so if a slot is being swapped it lets that block pass through dry.
class Rack final : public sysaudio::RenderCallback
{
public:
    static constexpr int numSlots = 3;

    Rack();
    ~Rack() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // RenderCallback
    void prepare (double sampleRate, int maxBlockSize) override;
    void render (float* left, float* right, int numFrames) noexcept override;
    void released() override;

    // Slots
    using LoadCallback = std::function<void (const juce::String& error)>;
    void loadPlugin (int slot, const juce::PluginDescription&, LoadCallback onDone = {},
                     const juce::MemoryBlock* state = nullptr);
    void removePlugin (int slot);
    juce::AudioPluginInstance* getPlugin (int slot) const;
    bool isLoading (int slot) const;
    juce::String getLoadingName (int slot) const;
    void setBypassed (int slot, bool);
    bool isBypassed (int slot) const;

    void setMasterGainDb (float db);
    float getMasterGainDb() const { return masterDb; }
    static constexpr float minGainDb = -60.0f; // at the minimum the output is silent

    // Called just before a slot's plug-in is replaced or removed (close its window here),
    // and after any change.
    std::function<void (int slot)> onSlotAboutToChange, onSlotChanged;

    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement&);

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioBuffer<float> scratch;
        int ins = 0, outs = 0;
        std::atomic<bool> bypassed { false };
        int loadToken = 0;
        bool loading = false;
        juce::PluginDescription pendingDesc; // while loading
        juce::MemoryBlock pendingState;
    };

    void configure (juce::AudioPluginInstance&, juce::AudioBuffer<float>& scratch, int& ins, int& outs) const;
    void changed (int slot);

    std::array<Slot, numSlots> slots;
    juce::SpinLock lock;
    juce::MidiBuffer midi;

    double sampleRate = 48000.0;
    int blockSize = 512;

    std::atomic<float> targetGain { 1.0f };
    float masterDb = 0.0f;
    juce::SmoothedValue<float> gain { 1.0f };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Rack)
    JUCE_DECLARE_NON_COPYABLE (Rack)
};
