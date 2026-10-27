#pragma once

#include <JuceHeader.h>
#include "Rack.h"
#include "SystemAudio.h"

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener,
                            private juce::ListBoxModel
{
public:
    explicit MainComponent (juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void saveSettings();

private:
    struct SlotRow
    {
        juce::Label number;
        juce::TextButton choose, edit { "Edit" }, bypass { "Bypass" };
    };

    // UI actions
    void updateSourceView();
    void setPower (bool on);
    void applyConfig();
    void refreshFromEngine();
    void refreshOutputs();
    void refreshApps();
    void refreshSlot (int slot);
    void showSlotMenu (int slot);
    void openEditor (int slot);
    void closeEditor (int slot);
    void showScanner();
    void markDirty() { dirty = true; }

    // Timer / ChangeListener / ListBoxModel
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    juce::PropertiesFile& settings;
    Rack rack;
    std::unique_ptr<sysaudio::Engine> engine; // after rack: destroyed first
    sysaudio::Config config;
    bool dirty = false;
    int ticksSinceSave = 0;

    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label title { {}, "Undertow Rack" }, status;
    juce::TextButton power { "Power Off" };

    juce::Label sourceLabel { {}, "Source" }, outputLabel { {}, "Output" };
    juce::ComboBox sourceBox, outputBox;
    juce::ListBox appList { "Apps", this };
    juce::Label wholeSystemNote;
    std::vector<sysaudio::AudioApp> apps;
    static constexpr int appListHeight = 170, wholeNoteHeight = 40;
    std::vector<sysaudio::OutputDevice> shownOutputs;
    std::string shownDefaultName;

    juce::Label pluginsLabel { {}, "Plug-ins" };
    std::array<SlotRow, Rack::numSlots> slotRows;

    juce::Label volumeLabel { {}, "Volume" };
    juce::Slider volume;
    juce::TextButton scanButton { "Scan for plug-ins..." };

    std::array<std::unique_ptr<juce::DocumentWindow>, Rack::numSlots> editorWindows;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
