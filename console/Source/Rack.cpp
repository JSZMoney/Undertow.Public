#include "Rack.h"

#include <cmath>

Rack::Rack()
{
    formatManager.addDefaultFormats(); // AU + VST3 (JUCE_PLUGINHOST_AU / _VST3)
    midi.ensureSize (2048);
}

Rack::~Rack()
{
    for (int i = 0; i < numSlots; ++i)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        slots[(size_t) i].plugin.reset();
    }
}

//==============================================================================
void Rack::configure (juce::AudioPluginInstance& p, juce::AudioBuffer<float>& scratch, int& ins, int& outs) const
{
    // Ask for stereo in/out on the main buses, with any side-chain buses off. If the plug-in
    // refuses, keep whatever it wants; render() adapts to odd channel counts.
    auto tryLayout = [&p] (bool disableExtras) {
        auto layout = p.getBusesLayout();
        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo()
                                                        : (disableExtras ? juce::AudioChannelSet::disabled()
                                                                         : layout.inputBuses[i]);
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo()
                                                         : (disableExtras ? juce::AudioChannelSet::disabled()
                                                                          : layout.outputBuses[i]);
        return p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout);
    };

    if (! tryLayout (true))
        tryLayout (false);

    p.setNonRealtime (false);
    p.prepareToPlay (sampleRate, blockSize);

    ins = p.getTotalNumInputChannels();
    outs = p.getTotalNumOutputChannels();
    scratch.setSize (juce::jmax (1, ins, outs), blockSize, false, true, false);
}

void Rack::prepare (double newSampleRate, int maxBlockSize)
{
    // Audio is stopped while this runs, so the blocking lock never waits on the audio thread.
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = newSampleRate;
    blockSize = juce::jmax (16, maxBlockSize);

    for (auto& s : slots)
        if (s.plugin != nullptr)
            configure (*s.plugin, s.scratch, s.ins, s.outs);

    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (targetGain.load());
}

void Rack::released()
{
}

//==============================================================================
void Rack::render (float* left, float* right, int numFrames) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    {
        const juce::SpinLock::ScopedTryLockType tl (lock);

        if (tl.isLocked()) // otherwise a slot is being swapped: pass this block through dry
        {
            for (int offset = 0; offset < numFrames; offset += blockSize)
            {
                const int n = juce::jmin (blockSize, numFrames - offset);
                float* l = left + offset;
                float* r = right + offset;

                for (auto& s : slots)
                {
                    if (s.plugin == nullptr || s.bypassed.load (std::memory_order_relaxed))
                        continue;

                    const int numCh = s.scratch.getNumChannels();
                    if (numCh == 0 || s.scratch.getNumSamples() < n)
                        continue;

                    // A view of the preallocated scratch: no allocation here.
                    juce::AudioBuffer<float> buffer (s.scratch.getArrayOfWritePointers(), numCh, n);
                    buffer.clear();

                    if (s.ins >= 2)
                    {
                        buffer.copyFrom (0, 0, l, n);
                        buffer.copyFrom (1, 0, r, n);
                    }
                    else if (s.ins == 1)
                    {
                        buffer.copyFrom (0, 0, l, n, 0.5f);
                        buffer.addFrom (0, 0, r, n, 0.5f);
                    }

                    midi.clear();
                    s.plugin->processBlock (buffer, midi);

                    if (s.outs >= 2)
                    {
                        juce::FloatVectorOperations::copy (l, buffer.getReadPointer (0), n);
                        juce::FloatVectorOperations::copy (r, buffer.getReadPointer (1), n);
                    }
                    else if (s.outs == 1)
                    {
                        juce::FloatVectorOperations::copy (l, buffer.getReadPointer (0), n);
                        juce::FloatVectorOperations::copy (r, buffer.getReadPointer (0), n);
                    }
                }
            }
        }
    }

    // Master volume, and never pass NaN/inf or wild values to the speakers
    gain.setTargetValue (targetGain.load (std::memory_order_relaxed));
    for (int i = 0; i < numFrames; ++i)
    {
        const float g = gain.getNextValue();
        float a = left[i] * g, b = right[i] * g;
        left[i] = std::isfinite (a) ? juce::jlimit (-1.0f, 1.0f, a) : 0.0f;
        right[i] = std::isfinite (b) ? juce::jlimit (-1.0f, 1.0f, b) : 0.0f;
    }
}

//==============================================================================
void Rack::loadPlugin (int slot, const juce::PluginDescription& desc, LoadCallback onDone,
                       const juce::MemoryBlock* state)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];
    const int token = ++s.loadToken;
    s.loading = true;
    s.pendingDesc = desc;
    s.pendingState = state != nullptr ? *state : juce::MemoryBlock();
    changed (slot);

    juce::WeakReference<Rack> weak (this);

    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [weak, slot, token, onDone] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto* self = weak.get();
            if (self == nullptr)
                return;

            auto& sl = self->slots[(size_t) slot];
            if (token != sl.loadToken)
                return; // replaced or removed while loading

            sl.loading = false;

            if (instance == nullptr)
            {
                self->changed (slot);
                if (onDone)
                    onDone (error.isNotEmpty() ? error : juce::String ("The plug-in couldn't be loaded."));
                return;
            }

            if (sl.pendingState.getSize() > 0)
                instance->setStateInformation (sl.pendingState.getData(), (int) sl.pendingState.getSize());
            sl.pendingState.reset();

            juce::AudioBuffer<float> scratch;
            int ins = 0, outs = 0;
            self->configure (*instance, scratch, ins, outs);

            if (self->onSlotAboutToChange)
                self->onSlotAboutToChange (slot);

            std::unique_ptr<juce::AudioPluginInstance> old;
            {
                const juce::SpinLock::ScopedLockType lk (self->lock);
                old = std::move (sl.plugin);
                sl.plugin = std::move (instance);
                std::swap (sl.scratch, scratch);
                sl.ins = ins;
                sl.outs = outs;
            }

            if (old != nullptr)
                old->releaseResources();
            old.reset();

            self->changed (slot);
            if (onDone)
                onDone ({});
        });
}

void Rack::removePlugin (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;

    auto& s = slots[(size_t) slot];
    ++s.loadToken;
    s.loading = false;
    s.pendingState.reset();

    if (s.plugin != nullptr)
    {
        if (onSlotAboutToChange)
            onSlotAboutToChange (slot);

        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::SpinLock::ScopedLockType lk (lock);
            old = std::move (s.plugin);
            s.ins = s.outs = 0;
        }
        old->releaseResources();
    }

    changed (slot);
}

juce::AudioPluginInstance* Rack::getPlugin (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].plugin.get() : nullptr;
}

bool Rack::isLoading (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].loading;
}

juce::String Rack::getLoadingName (int slot) const
{
    return isLoading (slot) ? slots[(size_t) slot].pendingDesc.name : juce::String();
}

void Rack::setBypassed (int slot, bool b)
{
    if (juce::isPositiveAndBelow (slot, numSlots))
    {
        slots[(size_t) slot].bypassed = b;
        changed (slot);
    }
}

bool Rack::isBypassed (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].bypassed.load();
}

void Rack::setMasterGainDb (float db)
{
    masterDb = juce::jlimit (minGainDb, 6.0f, db);
    targetGain = masterDb <= minGainDb ? 0.0f : juce::Decibels::decibelsToGain (masterDb);
}

void Rack::changed (int slot)
{
    if (onSlotChanged)
        onSlotChanged (slot);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Rack::saveState() const
{
    auto xml = std::make_unique<juce::XmlElement> ("RACK");
    xml->setAttribute ("gainDb", (double) masterDb);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        auto* e = xml->createNewChildElement ("SLOT");
        e->setAttribute ("index", i);
        e->setAttribute ("bypass", s.bypassed.load());

        if (s.plugin != nullptr)
        {
            e->addChildElement (s.plugin->getPluginDescription().createXml().release());
            juce::MemoryBlock mb;
            s.plugin->getStateInformation (mb);
            e->setAttribute ("state", mb.toBase64Encoding());
        }
        else if (s.loading) // still loading: keep what we were asked to load
        {
            e->addChildElement (s.pendingDesc.createXml().release());
            if (s.pendingState.getSize() > 0)
                e->setAttribute ("state", s.pendingState.toBase64Encoding());
        }
    }
    return xml;
}

void Rack::restoreState (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("RACK"))
        return;

    setMasterGainDb ((float) xml.getDoubleAttribute ("gainDb", 0.0));

    for (auto* e : xml.getChildWithTagNameIterator ("SLOT"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (i, numSlots))
            continue;

        setBypassed (i, e->getBoolAttribute ("bypass", false));

        juce::PluginDescription desc;
        if (auto* d = e->getChildByName ("PLUGIN"); d != nullptr && desc.loadFromXml (*d))
        {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (e->getStringAttribute ("state"));
            loadPlugin (i, desc, {}, &mb);
        }
        else
        {
            removePlugin (i);
        }
    }
}
