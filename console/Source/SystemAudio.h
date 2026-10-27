#pragma once

// Taps the Mac's audio (whole system or chosen apps) and plays it back out through a
// callback that can process it. Nothing is installed: the tap and its private aggregate
// device only exist while this process is running and Power is on.
//
// No JUCE in here, so the Mac implementation can be plain Objective-C++.

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sysaudio
{

struct OutputDevice
{
    std::string uid, name;
};

struct AudioApp
{
    std::string id;       // main bundle ID (helper processes are grouped under it)
    std::string name;     // display name
    bool playing = false; // producing audio right now
    bool running = true;  // false = remembered selection, not currently running
};

// Processes the tapped audio. prepare/released run on the message thread with audio
// stopped; render runs on the real-time audio thread and must not allocate or block.
class RenderCallback
{
public:
    virtual ~RenderCallback() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void render (float* left, float* right, int numFrames) noexcept = 0; // in place
    virtual void released() = 0;
};

enum class Source { wholeSystem, selectedApps };

struct Config
{
    bool power = false;
    Source source = Source::wholeSystem;
    std::vector<std::string> selectedApps; // AudioApp::id values
    std::string outputUid;                 // empty = follow the system default output
};

class Engine
{
public:
    // Message thread only (all methods).
    static std::unique_ptr<Engine> create (RenderCallback&);
    virtual ~Engine() = default;

    virtual void setConfig (const Config&) = 0;          // rebuilds the tap if needed
    virtual Config getConfig() const = 0;
    virtual std::vector<OutputDevice> outputDevices() = 0;
    virtual std::string defaultOutputName() = 0;
    virtual std::vector<AudioApp> audioApps() = 0;     // apps using audio + remembered selections
    virtual std::string status() const = 0;            // one line for the UI
    virtual bool isRunning() const = 0;                // audio is flowing through the rack

    // Called (message thread) when devices, apps or status change.
    std::function<void()> onChange;
};

} // namespace sysaudio
