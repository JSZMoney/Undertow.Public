// macOS implementation: Core Audio process taps (macOS 14.2+).
//
// While Power is on:
//   tap  (whole system minus this app, or a mixdown of the chosen apps; muted while tapped)
//    -> private aggregate device (main sub-device = the real output, plus the tap)
//    -> IOProc: read tap, run the rack, write the result to the output's first two channels.
// Turning Power off or quitting destroys the tap and aggregate, so normal audio comes back.
// Nothing here is installed or written outside this process.

#import <AppKit/AppKit.h>
#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <libproc.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "SystemAudio.h"

namespace sysaudio
{
namespace
{
//==============================================================================
// Small property helpers
AudioObjectPropertyAddress addr (AudioObjectPropertySelector sel,
                                 AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    return { sel, scope, kAudioObjectPropertyElementMain };
}

template <typename T>
bool getProp (AudioObjectID obj, AudioObjectPropertySelector sel, T& out,
              AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    auto a = addr (sel, scope);
    UInt32 size = sizeof (T);
    return AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &out) == noErr;
}

std::vector<AudioObjectID> getIDList (AudioObjectID obj, AudioObjectPropertySelector sel,
                                      AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    auto a = addr (sel, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (obj, &a, 0, nullptr, &size) != noErr || size == 0)
        return {};
    std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));
    if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, ids.data()) != noErr)
        return {};
    ids.resize (size / sizeof (AudioObjectID));
    return ids;
}

NSString* getString (AudioObjectID obj, AudioObjectPropertySelector sel)
{
    CFStringRef s = nullptr;
    if (! getProp (obj, sel, s) || s == nullptr)
        return nil;
    return (__bridge_transfer NSString*) s;
}

std::string str (NSString* s) { return s != nil ? std::string (s.UTF8String) : std::string(); }

int streamCount (AudioObjectID dev, AudioObjectPropertyScope scope)
{
    return (int) getIDList (dev, kAudioDevicePropertyStreams, scope).size();
}

AudioObjectID deviceForUID (NSString* uid)
{
    if (uid.length == 0)
        return kAudioObjectUnknown;
    auto a = addr (kAudioHardwarePropertyTranslateUIDToDevice);
    CFStringRef cfUid = (__bridge CFStringRef) uid;
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 size = sizeof (dev);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, sizeof (cfUid), &cfUid, &size, &dev) != noErr)
        return kAudioObjectUnknown;
    return dev;
}

AudioObjectID defaultOutputDevice()
{
    AudioObjectID dev = kAudioObjectUnknown;
    getProp (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, dev);
    return dev;
}

// Property changes that can make us rebuild
const AudioObjectPropertySelector systemSelectors[] = { kAudioHardwarePropertyDefaultOutputDevice,
                                                        kAudioHardwarePropertyDevices,
                                                        kAudioHardwarePropertyProcessObjectList };
const AudioObjectPropertySelector deviceSelectors[] = { kAudioDevicePropertyNominalSampleRate,
                                                        kAudioDevicePropertyDeviceIsAlive };

NSString* const aggregateUIDPrefix = @"com.undertowaudio.rack.aggregate.";

//==============================================================================
// Audio client processes, grouped into apps (Chrome's helpers count as Chrome, etc.)
struct Proc
{
    AudioObjectID object = kAudioObjectUnknown;
    pid_t pid = 0;
    bool output = false;
    std::string appId, appName;
    bool regularApp = false;
};

std::vector<Proc> listProcesses()
{
    // Visible (Dock) apps, by bundle ID
    std::map<std::string, std::string> regular;
    for (NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications)
        if (app.activationPolicy == NSApplicationActivationPolicyRegular && app.bundleIdentifier.length > 0)
            regular[str (app.bundleIdentifier)] = str (app.localizedName ?: app.bundleIdentifier);

    const pid_t me = getpid();
    std::vector<Proc> result;

    for (auto obj : getIDList (kAudioObjectSystemObject, kAudioHardwarePropertyProcessObjectList))
    {
        Proc p;
        p.object = obj;
        getProp (obj, kAudioProcessPropertyPID, p.pid);
        if (p.pid == me)
            continue;

        UInt32 running = 0;
        getProp (obj, kAudioProcessPropertyIsRunningOutput, running);
        p.output = running != 0;

        const std::string bundle = str (getString (obj, kAudioProcessPropertyBundleID));

        if (! bundle.empty())
        {
            if (auto it = regular.find (bundle); it != regular.end())
            {
                p.appId = it->first;
                p.appName = it->second;
                p.regularApp = true;
            }
            else
            {
                // Helper processes: com.google.Chrome.helper -> com.google.Chrome
                size_t best = 0;
                for (const auto& [rid, rname] : regular)
                    if (rid.size() > best && bundle.size() > rid.size()
                        && bundle.compare (0, rid.size(), rid) == 0 && bundle[rid.size()] == '.')
                    {
                        best = rid.size();
                        p.appId = rid;
                        p.appName = rname;
                        p.regularApp = true;
                    }

                if (p.appId.empty() && bundle.rfind ("com.apple.WebKit", 0) == 0)
                {
                    p.appId = "com.apple.WebKit";
                    p.appName = "Safari & web content";
                }
            }

            if (p.appId.empty())
            {
                NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: p.pid];
                p.appId = bundle;
                const std::string name = str (app.localizedName);
                p.appName = name.empty() ? bundle : name;
            }
        }
        else
        {
            char name[PROC_PIDPATHINFO_MAXSIZE] = {};
            if (proc_name (p.pid, name, sizeof (name)) <= 0)
                std::snprintf (name, sizeof (name), "pid %d", (int) p.pid);
            p.appId = std::string ("process:") + name;
            p.appName = name;
        }

        result.push_back (std::move (p));
    }
    return result;
}

AudioObjectID ownProcessObject()
{
    auto a = addr (kAudioHardwarePropertyTranslatePIDToProcessObject);
    pid_t pid = getpid();
    AudioObjectID obj = kAudioObjectUnknown;
    UInt32 size = sizeof (obj);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, sizeof (pid), &pid, &size, &obj) == noErr
        && obj != kAudioObjectUnknown)
        return obj;

    for (auto o : getIDList (kAudioObjectSystemObject, kAudioHardwarePropertyProcessObjectList))
    {
        pid_t p = 0;
        if (getProp (o, kAudioProcessPropertyPID, p) && p == pid)
            return o;
    }
    return kAudioObjectUnknown;
}

std::string displayNameForBundle (const std::string& bundleId)
{
    if (bundleId == "com.apple.WebKit")
        return "Safari & web content";
    NSURL* url = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier: @(bundleId.c_str())];
    if (url != nil)
        return str ([NSFileManager.defaultManager displayNameAtPath: url.path].stringByDeletingPathExtension);
    if (bundleId.rfind ("process:", 0) == 0)
        return bundleId.substr (8);
    return bundleId;
}

std::string formatRate (double sr)
{
    char buf[32];
    if (std::fmod (sr, 1000.0) == 0.0)
        std::snprintf (buf, sizeof (buf), "%d kHz", (int) (sr / 1000.0));
    else
        std::snprintf (buf, sizeof (buf), "%.1f kHz", sr / 1000.0);
    return buf;
}

//==============================================================================
class MacEngine final : public Engine
{
public:
    explicit MacEngine (RenderCallback& cb) : callback (cb)
    {
        left.resize (maxScratch);
        right.resize (maxScratch);

        // Blocks hold a weak handle, so they never touch a deleted engine
        std::weak_ptr<MacEngine*> weak = alive;
        systemListener = ^(UInt32, const AudioObjectPropertyAddress*) {
            if (auto engine = weak.lock(); engine && *engine)
                (*engine)->scheduleCheck (false);
        };

        for (auto sel : systemSelectors)
        {
            auto a = addr (sel);
            AudioObjectAddPropertyListenerBlock (kAudioObjectSystemObject, &a, dispatch_get_main_queue(), systemListener);
        }

        deviceListener = ^(UInt32, const AudioObjectPropertyAddress*) {
            if (auto engine = weak.lock(); engine && *engine)
                (*engine)->scheduleCheck (true);
        };
    }

    ~MacEngine() override
    {
        *alive = nullptr;
        alive.reset();

        for (auto sel : systemSelectors)
        {
            auto a = addr (sel);
            AudioObjectRemovePropertyListenerBlock (kAudioObjectSystemObject, &a, dispatch_get_main_queue(), systemListener);
        }

        teardown();
    }

    //==========================================================================
    void setConfig (const Config& c) override
    {
        const bool same = c.power == config.power && c.source == config.source
                       && c.selectedApps == config.selectedApps && c.outputUid == config.outputUid;
        config = c;
        if (! same)
            apply();
    }

    Config getConfig() const override { return config; }

    std::vector<OutputDevice> outputDevices() override
    {
        std::vector<OutputDevice> list;
        for (auto dev : getIDList (kAudioObjectSystemObject, kAudioHardwarePropertyDevices))
        {
            NSString* uid = getString (dev, kAudioDevicePropertyDeviceUID);
            if (uid == nil || [uid hasPrefix: aggregateUIDPrefix] || streamCount (dev, kAudioObjectPropertyScopeOutput) == 0)
                continue;
            list.push_back ({ str (uid), str (getString (dev, kAudioObjectPropertyName)) });
        }
        return list;
    }

    std::string defaultOutputName() override
    {
        return str (getString (defaultOutputDevice(), kAudioObjectPropertyName));
    }

    std::vector<AudioApp> audioApps() override
    {
        std::map<std::string, AudioApp> apps;
        std::set<std::string> selected (config.selectedApps.begin(), config.selectedApps.end());

        for (auto& p : listProcesses())
        {
            if (! (p.regularApp || p.output || selected.count (p.appId) > 0))
                continue;
            auto& app = apps[p.appId];
            app.id = p.appId;
            app.name = p.appName;
            app.running = true;
            app.playing = app.playing || p.output;
        }

        for (auto& id : config.selectedApps)
            if (apps.count (id) == 0)
                apps[id] = { id, displayNameForBundle (id), false, false };

        std::vector<AudioApp> list;
        for (auto& [id, app] : apps)
            list.push_back (app);
        std::sort (list.begin(), list.end(), [] (const AudioApp& a, const AudioApp& b) {
            return [@(a.name.c_str()) localizedCaseInsensitiveCompare: @(b.name.c_str())] == NSOrderedAscending;
        });
        return list;
    }

    std::string status() const override { return statusText; }
    bool isRunning() const override { return running; }

private:
    //==========================================================================
    AudioObjectID resolveOutput (bool& usedFallback) const
    {
        usedFallback = false;
        if (! config.outputUid.empty())
        {
            auto dev = deviceForUID (@(config.outputUid.c_str()));
            if (dev != kAudioObjectUnknown)
                return dev;
            usedFallback = true;
        }
        return defaultOutputDevice();
    }

    std::set<AudioObjectID> selectedProcessObjects() const
    {
        std::set<std::string> selected (config.selectedApps.begin(), config.selectedApps.end());
        std::set<AudioObjectID> objs;
        for (auto& p : listProcesses())
            if (selected.count (p.appId) > 0)
                objs.insert (p.object);
        return objs;
    }

    // Something changed in the system: rebuild only if it affects us.
    void scheduleCheck (bool forceRebuild)
    {
        pendingForce = pendingForce || forceRebuild;
        if (checkPending)
            return;
        checkPending = true;

        std::weak_ptr<MacEngine*> weak = alive;
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (auto engine = weak.lock(); engine && *engine)
                (*engine)->check();
        });
    }

    void check()
    {
        checkPending = false;
        const bool deviceChanged = pendingForce;
        pendingForce = false;

        if (config.power)
        {
            bool fallback = false;
            bool rebuild = resolveOutput (fallback) != builtOutput;

            // The output's sample rate changed, or it went away
            if (! rebuild && deviceChanged && running)
            {
                Float64 rate = 0;
                UInt32 isAlive = 1;
                getProp (builtOutput, kAudioDevicePropertyDeviceIsAlive, isAlive);
                rebuild = isAlive == 0
                       || (getProp (builtOutput, kAudioDevicePropertyNominalSampleRate, rate) && rate != builtRate);
            }

            if (! rebuild && config.source == Source::selectedApps)
                rebuild = selectedProcessObjects() != builtProcesses;
            if (rebuild)
                apply();
        }

        if (onChange)
            onChange();
    }

    //==========================================================================
    void apply()
    {
        teardown();
        if (config.power)
            build();
        else
            statusText = "Off";
        if (onChange)
            onChange();
    }

    void fail (const std::string& message)
    {
        teardown();
        statusText = message;
    }

    void build()
    {
        bool fallback = false;
        const AudioObjectID output = resolveOutput (fallback);
        builtOutput = output;
        builtProcesses.clear();

        if (output == kAudioObjectUnknown)
            return fail ("No output device found.");

        NSString* outputUID = getString (output, kAudioDevicePropertyDeviceUID);
        NSString* outputName = getString (output, kAudioObjectPropertyName) ?: @"output";
        if (outputUID == nil)
            return fail ("Couldn't read the output device.");

        // 1. The tap
        CATapDescription* desc = nil;
        std::string what;

        if (config.source == Source::wholeSystem)
        {
            const AudioObjectID me = ownProcessObject();
            if (me == kAudioObjectUnknown)
                return fail ("Couldn't identify this app to Core Audio (needed to avoid feedback). Try again.");
            desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: @[ @(me) ]];
            what = "Whole system";
        }
        else
        {
            if (config.selectedApps.empty())
            {
                statusText = "No apps selected. Tick the apps to process below.";
                return;
            }
            builtProcesses = selectedProcessObjects();
            if (builtProcesses.empty())
            {
                statusText = "Waiting for a selected app to make sound...";
                return;
            }
            NSMutableArray<NSNumber*>* objs = [NSMutableArray array];
            for (auto o : builtProcesses)
                [objs addObject: @(o)];
            desc = [[CATapDescription alloc] initStereoMixdownOfProcesses: objs];
            what = config.selectedApps.size() == 1 ? "1 app" : std::to_string (config.selectedApps.size()) + " apps";
        }

        desc.name = @"Undertow Rack";
        desc.UUID = [NSUUID UUID];
        desc.muteBehavior = CATapMutedWhenTapped; // original audio is silent only while we read it
        if ([desc respondsToSelector: NSSelectorFromString (@"setPrivateTap:")])
            [desc setValue: @YES forKey: @"privateTap"];

        OSStatus err = AudioHardwareCreateProcessTap (desc, &tap);
        if (err != noErr || tap == kAudioObjectUnknown)
        {
            tap = kAudioObjectUnknown;
            return fail ("Couldn't start the audio tap (error " + std::to_string ((int) err)
                         + "). Allow Undertow Rack in System Settings > Privacy & Security > "
                           "Screen & System Audio Recording.");
        }

        NSString* tapUID = getString (tap, kAudioTapPropertyUID) ?: desc.UUID.UUIDString;

        // 2. Private aggregate device: real output + the tap
        NSString* aggUID = [aggregateUIDPrefix stringByAppendingString: NSUUID.UUID.UUIDString];
        NSDictionary* aggDesc = @{
            @(kAudioAggregateDeviceNameKey): @"Undertow Rack",
            @(kAudioAggregateDeviceUIDKey): aggUID,
            @(kAudioAggregateDeviceMainSubDeviceKey): outputUID,
            @(kAudioAggregateDeviceIsPrivateKey): @YES,
            @(kAudioAggregateDeviceIsStackedKey): @NO,
            @(kAudioAggregateDeviceTapAutoStartKey): @YES,
            @(kAudioAggregateDeviceSubDeviceListKey): @[ @{ @(kAudioSubDeviceUIDKey): outputUID } ],
            @(kAudioAggregateDeviceTapListKey): @[ @{
                @(kAudioSubTapUIDKey): tapUID,
                @(kAudioSubTapDriftCompensationKey): @YES,
            } ],
        };

        err = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) aggDesc, &aggregate);
        if (err != noErr || aggregate == kAudioObjectUnknown)
        {
            aggregate = kAudioObjectUnknown;
            return fail ("Couldn't create the audio route (error " + std::to_string ((int) err) + ").");
        }

        for (int i = 0; i < 100; ++i) // up to ~1 s
        {
            UInt32 isAlive = 0;
            if (getProp (aggregate, kAudioDevicePropertyDeviceIsAlive, isAlive) && isAlive != 0)
                break;
            usleep (10000);
        }

        // 3. Format and layout. Tap buffers come after the output device's own input streams.
        Float64 rate = 0;
        if (! getProp (aggregate, kAudioDevicePropertyNominalSampleRate, rate) || rate <= 0)
            getProp (output, kAudioDevicePropertyNominalSampleRate, rate);
        if (rate <= 0)
            rate = 48000.0;

        UInt32 frames = 512;
        getProp (aggregate, kAudioDevicePropertyBufferFrameSize, frames);
        chunk = std::clamp ((int) frames, 32, maxScratch);
        tapBufferIndex = (UInt32) streamCount (output, kAudioObjectPropertyScopeInput);

        builtRate = 0; // the output's own rate, to spot changes later
        getProp (output, kAudioDevicePropertyNominalSampleRate, builtRate);
        callback.prepare (rate, chunk);
        prepared = true;

        // 4. Run
        err = AudioDeviceCreateIOProcID (aggregate, &MacEngine::ioProc, this, &procID);
        if (err != noErr || procID == nullptr)
        {
            procID = nullptr;
            return fail ("Couldn't start audio (error " + std::to_string ((int) err) + ").");
        }

        err = AudioDeviceStart (aggregate, procID);
        if (err != noErr)
            return fail ("Couldn't start audio (error " + std::to_string ((int) err) + ").");

        // Rebuild if the output's sample rate changes or it goes away
        listenedDevice = output;
        for (auto sel : deviceSelectors)
        {
            auto a = addr (sel);
            AudioObjectAddPropertyListenerBlock (listenedDevice, &a, dispatch_get_main_queue(), deviceListener);
        }

        running = true;
        statusText = what + " \xe2\x86\x92 " + str (outputName) + " \xc2\xb7 " + formatRate (rate)
                   + (fallback ? " (chosen output not found; using default)" : "");
    }

    void teardown()
    {
        running = false;

        if (listenedDevice != kAudioObjectUnknown)
        {
            for (auto sel : deviceSelectors)
            {
                auto a = addr (sel);
                AudioObjectRemovePropertyListenerBlock (listenedDevice, &a, dispatch_get_main_queue(), deviceListener);
            }
            listenedDevice = kAudioObjectUnknown;
        }

        if (aggregate != kAudioObjectUnknown && procID != nullptr)
        {
            AudioDeviceStop (aggregate, procID);
            AudioDeviceDestroyIOProcID (aggregate, procID);
        }
        procID = nullptr;

        if (aggregate != kAudioObjectUnknown)
            AudioHardwareDestroyAggregateDevice (aggregate);
        aggregate = kAudioObjectUnknown;

        if (tap != kAudioObjectUnknown)
            AudioHardwareDestroyProcessTap (tap);
        tap = kAudioObjectUnknown;

        if (prepared)
            callback.released();
        prepared = false;
    }

    //==========================================================================
    // Real-time thread
    static OSStatus ioProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                            const AudioTimeStamp*, AudioBufferList* output, const AudioTimeStamp*, void* ctx)
    {
        static_cast<MacEngine*> (ctx)->process (input, output);
        return noErr;
    }

    void process (const AudioBufferList* in, AudioBufferList* out) noexcept
    {
        if (out == nullptr || out->mNumberBuffers == 0)
            return;

        for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
            if (out->mBuffers[b].mData != nullptr)
                std::memset (out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);

        const auto& first = out->mBuffers[0];
        const int frames = (int) (first.mDataByteSize / (sizeof (float) * std::max<UInt32> (1, first.mNumberChannels)));

        for (int offset = 0; offset < frames; offset += chunk)
        {
            const int n = std::min (chunk, frames - offset);
            readTap (in, offset, n);
            callback.render (left.data(), right.data(), n);
            writeOutput (out, offset, n);
        }
    }

    void readTap (const AudioBufferList* in, int offset, int n) noexcept
    {
        std::fill (left.begin(), left.begin() + n, 0.0f);
        std::fill (right.begin(), right.begin() + n, 0.0f);

        if (in == nullptr || in->mNumberBuffers <= tapBufferIndex)
            return;

        const auto& b = in->mBuffers[tapBufferIndex];
        const auto* data = static_cast<const float*> (b.mData);
        const int ch = (int) std::max<UInt32> (1, b.mNumberChannels);
        const int avail = (int) (b.mDataByteSize / (sizeof (float) * (UInt32) ch));
        if (data == nullptr)
            return;

        const float* second = nullptr; // tap delivered as two mono buffers
        int secondAvail = 0;
        if (ch == 1 && in->mNumberBuffers > tapBufferIndex + 1)
        {
            const auto& b2 = in->mBuffers[tapBufferIndex + 1];
            second = static_cast<const float*> (b2.mData);
            secondAvail = (int) (b2.mDataByteSize / (sizeof (float) * std::max<UInt32> (1, b2.mNumberChannels)));
        }

        for (int i = 0; i < n; ++i)
        {
            const int idx = offset + i;
            if (idx >= avail)
                break;
            if (ch >= 2)
            {
                left[(size_t) i] = data[idx * ch];
                right[(size_t) i] = data[idx * ch + 1];
            }
            else
            {
                left[(size_t) i] = data[idx];
                right[(size_t) i] = (second != nullptr && idx < secondAvail) ? second[idx] : data[idx];
            }
        }
    }

    void writeOutput (AudioBufferList* out, int offset, int n) noexcept
    {
        auto& b0 = out->mBuffers[0];
        auto* d0 = static_cast<float*> (b0.mData);
        const int ch0 = (int) std::max<UInt32> (1, b0.mNumberChannels);
        const int avail0 = (int) (b0.mDataByteSize / (sizeof (float) * (UInt32) ch0));
        if (d0 == nullptr)
            return;

        float* d1 = nullptr; // right channel in a second mono buffer
        int avail1 = 0;
        if (ch0 == 1 && out->mNumberBuffers > 1)
        {
            auto& b1 = out->mBuffers[1];
            d1 = static_cast<float*> (b1.mData);
            avail1 = (int) (b1.mDataByteSize / (sizeof (float) * std::max<UInt32> (1, b1.mNumberChannels)));
            if (b1.mNumberChannels > 1)
                d1 = nullptr; // unexpected layout: keep it simple, left only
        }

        for (int i = 0; i < n; ++i)
        {
            const int idx = offset + i;
            if (idx < avail0)
            {
                if (ch0 >= 2)
                {
                    d0[idx * ch0] = left[(size_t) i];
                    d0[idx * ch0 + 1] = right[(size_t) i];
                }
                else
                {
                    d0[idx] = d1 != nullptr ? left[(size_t) i] : 0.5f * (left[(size_t) i] + right[(size_t) i]);
                }
            }
            if (d1 != nullptr && idx < avail1)
                d1[idx] = right[(size_t) i];
        }
    }

    //==========================================================================
    static constexpr int maxScratch = 4096;

    RenderCallback& callback;
    Config config;
    std::string statusText = "Off";
    bool running = false, prepared = false;
    bool checkPending = false, pendingForce = false;

    AudioObjectID tap = kAudioObjectUnknown, aggregate = kAudioObjectUnknown;
    AudioObjectID builtOutput = kAudioObjectUnknown, listenedDevice = kAudioObjectUnknown;
    Float64 builtRate = 0;
    std::set<AudioObjectID> builtProcesses;
    AudioDeviceIOProcID procID = nullptr;

    // Read on the audio thread; only changed while audio is stopped
    int chunk = 512;
    UInt32 tapBufferIndex = 0;
    std::vector<float> left, right;

    std::shared_ptr<MacEngine*> alive = std::make_shared<MacEngine*> (this);
    AudioObjectPropertyListenerBlock systemListener = nil, deviceListener = nil;
};

} // namespace

std::unique_ptr<Engine> Engine::create (RenderCallback& cb)
{
    return std::make_unique<MacEngine> (cb);
}

} // namespace sysaudio
