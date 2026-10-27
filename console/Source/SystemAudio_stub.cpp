// Non-Mac stand-in so the rest of the app builds and can be tested elsewhere.
#include "SystemAudio.h"

namespace sysaudio
{
namespace
{
class StubEngine final : public Engine
{
public:
    explicit StubEngine (RenderCallback& cb) : callback (cb) {}

    ~StubEngine() override
    {
        if (prepared)
            callback.released();
    }

    void setConfig (const Config& c) override
    {
        config = c;
        if (config.power && ! prepared)
        {
            callback.prepare (48000.0, 512);
            prepared = true;
        }
        else if (! config.power && prepared)
        {
            callback.released();
            prepared = false;
        }
        if (onChange)
            onChange();
    }

    Config getConfig() const override { return config; }
    std::vector<OutputDevice> outputDevices() override { return { { "stub", "Test output" } }; }
    std::string defaultOutputName() override { return "Test output"; }
    std::vector<AudioApp> audioApps() override { return { { "com.example.player", "Example Player", false, true } }; }

    std::string status() const override
    {
        return config.power ? "System audio capture needs macOS 14.2 or later." : "Off";
    }

    bool isRunning() const override { return false; }

private:
    RenderCallback& callback;
    Config config;
    bool prepared = false;
};
} // namespace

std::unique_ptr<Engine> Engine::create (RenderCallback& cb)
{
    return std::make_unique<StubEngine> (cb);
}

} // namespace sysaudio
