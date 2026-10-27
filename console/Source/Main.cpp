#include <JuceHeader.h>
#include "MainComponent.h"
#include "PluginScanner.h"

class UndertowRackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    // true so the hidden scan worker (a second copy of this app) can start.
    // Opening the app again from Finder still just brings the open one forward.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Launched as the plug-in scan worker? Then no window, no Dock icon.
        auto worker = std::make_unique<scanner::Worker>();
        if (worker->initialiseFromCommandLine (commandLine, scanner::workerProcessUID))
        {
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            scanWorker = std::move (worker);
            return;
        }
        worker.reset();

        // ~/Library/Application Support/Undertow Rack/Undertow Rack.settings
        juce::PropertiesFile::Options options;
        options.applicationName = "Undertow Rack";
        options.folderName = "Undertow Rack";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (options);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        mainWindow.reset(); // stops processing, saves settings
        settings.reset();
        scanWorker.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::PropertiesFile& settings)
            : DocumentWindow (name, juce::Colour (0xff15181b),
                              DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (settings), true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        // Closing the window quits, which turns processing off and restores normal audio.
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<scanner::Worker> scanWorker;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (UndertowRackApplication)
