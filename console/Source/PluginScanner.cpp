// Adapted from the out-of-process scanner in JUCE's AudioPluginHost example.
#include "PluginScanner.h"

namespace scanner
{

//==============================================================================
// Worker side
Worker::Worker()
{
    formatManager.addDefaultFormats();
}

void Worker::handleMessageFromCoordinator (const juce::MemoryBlock& mb)
{
    if (mb.isEmpty())
        return;

    const std::lock_guard<std::mutex> lock (mutex);

    // Formats that can't scan here (they need the message thread) return nothing; queue them.
    if (const auto results = doScan (mb); ! results.isEmpty())
    {
        sendResults (results);
    }
    else
    {
        pending.emplace (mb);
        triggerAsyncUpdate();
    }
}

void Worker::handleConnectionLost()
{
    juce::JUCEApplicationBase::quit();
}

void Worker::handleAsyncUpdate()
{
    for (;;)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (pending.empty())
            return;
        sendResults (doScan (pending.front()));
        pending.pop();
    }
}

juce::OwnedArray<juce::PluginDescription> Worker::doScan (const juce::MemoryBlock& block)
{
    juce::MemoryInputStream stream { block, false };
    const auto formatName = stream.readString();
    const auto identifier = stream.readString();

    juce::PluginDescription pd;
    pd.fileOrIdentifier = identifier;
    pd.uniqueId = pd.deprecatedUid = 0;

    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formatManager.getFormats())
        if (f->getName() == formatName)
            format = f;

    juce::OwnedArray<juce::PluginDescription> results;

    if (format != nullptr
        && (juce::MessageManager::getInstance()->isThisTheMessageThread()
            || format->requiresUnblockedMessageThreadDuringCreation (pd)))
    {
        format->findAllTypesForFile (results, identifier);
    }

    return results;
}

void Worker::sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
{
    juce::XmlElement xml ("LIST");
    for (auto* desc : results)
        xml.addChildElement (desc->createXml().release());

    const auto str = xml.toString();
    sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
}

//==============================================================================
// Main-app side
class OutOfProcessScanner::Coordinator final : private juce::ChildProcessCoordinator
{
public:
    Coordinator()
    {
        launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                        workerProcessUID, 0, 0);
    }

    ~Coordinator() override { killWorkerProcess(); }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    bool isLaunched() const { return launched; }

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 }, [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = gotResult = false;
        return { state, std::move (result) };
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        result = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    bool launched = false;
    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> result;
    bool connectionLost = false, gotResult = false;
};

OutOfProcessScanner::OutOfProcessScanner() = default;
OutOfProcessScanner::~OutOfProcessScanner() = default;

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    // Returning false marks this plug-in as failed (skipped) and starts a fresh worker.
    if (coordinator == nullptr)
        coordinator = std::make_unique<Coordinator>();

    if (! coordinator->isLaunched())
    {
        coordinator.reset();
        return false;
    }

    juce::MemoryBlock block;
    {
        juce::MemoryOutputStream stream { block, true };
        stream.writeString (format.getName());
        stream.writeString (fileOrIdentifier);
    }

    if (! coordinator->sendMessageToWorker (block))
    {
        coordinator.reset();
        return false;
    }

    const auto start = juce::Time::getMillisecondCounter();

    for (;;)
    {
        if (shouldExit())
            return true;

        auto response = coordinator->getResponse();

        if (response.state == Coordinator::State::timeout)
        {
            if (juce::Time::getMillisecondCounter() - start > perPluginTimeoutMs)
            {
                coordinator.reset(); // kills the stuck worker
                return false;
            }
            continue;
        }

        if (response.state == Coordinator::State::connectionLost)
        {
            coordinator.reset(); // the plug-in crashed the worker
            return false;
        }

        if (response.xml != nullptr)
            for (const auto* item : response.xml->getChildIterator())
                if (auto desc = std::make_unique<juce::PluginDescription>(); desc->loadFromXml (*item))
                    result.add (std::move (desc));

        return true;
    }
}

void OutOfProcessScanner::scanFinished()
{
    coordinator.reset();
}

} // namespace scanner
