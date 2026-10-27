#include <JuceHeader.h>
#include "MainComponent.h"

class UndertowRackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
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

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (UndertowRackApplication)
