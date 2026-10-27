#pragma once

// Out-of-process plug-in scanning.
//
// Scanning loads every plug-in on the Mac, and some crash or hang while being loaded or
// unloaded (e.g. a timer still firing after unload). So each plug-in is scanned in a
// separate copy of this app running as a hidden worker. If the worker crashes or a plug-in
// takes too long, the worker is restarted, that plug-in is skipped (listed as failed in the
// scanner), and the scan carries on. The main app never loads plug-ins it isn't using.

#include <JuceHeader.h>

namespace scanner
{

// Command-line marker that turns a launch of this app into a scan worker.
inline constexpr const char* workerProcessUID = "undertowrackpluginscanner";

// How long one plug-in file may take before it's skipped (shells like WaveShell can be slow).
inline constexpr juce::uint32 perPluginTimeoutMs = 90 * 1000;

// Runs in the worker process.
class Worker final : private juce::ChildProcessWorker,
                     private juce::AsyncUpdater
{
public:
    Worker();
    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;
    void handleAsyncUpdate() override;

    juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock&);
    void sendResults (const juce::OwnedArray<juce::PluginDescription>&);

    std::mutex mutex;
    std::queue<juce::MemoryBlock> pending;
    juce::AudioPluginFormatManager formatManager;
};

// Used by the main app's KnownPluginList.
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    OutOfProcessScanner();
    ~OutOfProcessScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>&,
                             const juce::String& fileOrIdentifier) override;
    void scanFinished() override;

private:
    class Coordinator;
    std::unique_ptr<Coordinator> coordinator;
};

} // namespace scanner
