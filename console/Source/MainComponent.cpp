#include "MainComponent.h"

namespace
{
juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

const juce::Colour accent { 0xff3fb8af };
const juce::Colour panel { 0xff1e2226 };
const juce::Colour background { 0xff15181b };
const juce::Colour dimText { 0xff9aa3ab };

juce::File appDataFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Undertow Rack");
    dir.createDirectory();
    return dir;
}

// A window that hosts a plug-in editor or the scanner; asks its owner to delete it.
class ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& name, juce::Component* content, bool resizable, std::function<void()> onClose)
        : DocumentWindow (name, background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          closeHandler (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (resizable, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // Let the owner delete us after this call returns
        juce::MessageManager::callAsync ([handler = closeHandler] { if (handler) handler(); });
    }

private:
    std::function<void()> closeHandler;
};
} // namespace

//==============================================================================
MainComponent::MainComponent (juce::PropertiesFile& s) : settings (s)
{
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    lookAndFeel.setColour (juce::ResizableWindow::backgroundColourId, background);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, panel);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, panel);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
    lookAndFeel.setColour (juce::ListBox::backgroundColourId, panel);
    lookAndFeel.setColour (juce::Slider::thumbColourId, accent);
    lookAndFeel.setColour (juce::Slider::trackColourId, accent.darker (0.5f));
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.darker (0.4f));
    setLookAndFeel (&lookAndFeel);

    // Saved plug-in list and rack
    if (auto xml = settings.getXmlValue ("knownPlugins"))
        rack.knownPlugins.recreateFromXml (*xml);
    rack.knownPlugins.addChangeListener (this);

    rack.onSlotAboutToChange = [this] (int slot) { closeEditor (slot); };
    rack.onSlotChanged = [this] (int slot) { refreshSlot (slot); markDirty(); };

    if (auto xml = settings.getXmlValue ("rack"))
        rack.restoreState (*xml);

    // Header
    title.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    addAndMakeVisible (title);

    power.setClickingTogglesState (true);
    power.setColour (juce::TextButton::buttonOnColourId, accent);
    power.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    power.onClick = [this] { setPower (power.getToggleState()); };
    addAndMakeVisible (power);

    status.setFont (juce::Font (juce::FontOptions (13.0f)));
    status.setColour (juce::Label::textColourId, dimText);
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    // Source
    for (auto* l : { &sourceLabel, &outputLabel, &pluginsLabel, &volumeLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        addAndMakeVisible (*l);
    }

    sourceBox.addItem ("Whole system", 1);
    sourceBox.addItem ("Selected apps", 2);
    sourceBox.onChange = [this] {
        config.source = sourceBox.getSelectedId() == 2 ? sysaudio::Source::selectedApps
                                                       : sysaudio::Source::wholeSystem;
        updateSourceView();
        applyConfig();
    };
    addAndMakeVisible (sourceBox);

    appList.setRowHeight (26);
    appList.setOutlineThickness (0);
    addChildComponent (appList);

    wholeSystemNote.setText ("Everything you hear goes through the rack. "
                             "Choose \"Selected apps\" to process only some apps.",
                             juce::dontSendNotification);
    wholeSystemNote.setColour (juce::Label::textColourId, dimText);
    wholeSystemNote.setJustificationType (juce::Justification::topLeft);
    addChildComponent (wholeSystemNote);

    // Output
    outputBox.onChange = [this] {
        const int id = outputBox.getSelectedId();
        if (id == 1)
            config.outputUid.clear();
        else if (id >= 2 && id - 2 < (int) shownOutputs.size())
            config.outputUid = shownOutputs[(size_t) (id - 2)].uid;
        applyConfig();
    };
    addAndMakeVisible (outputBox);

    // Slots
    for (int i = 0; i < Rack::numSlots; ++i)
    {
        auto& row = slotRows[(size_t) i];
        row.number.setText (juce::String (i + 1), juce::dontSendNotification);
        row.number.setJustificationType (juce::Justification::centred);
        row.number.setColour (juce::Label::textColourId, dimText);
        row.choose.onClick = [this, i] { showSlotMenu (i); };
        row.edit.onClick = [this, i] { openEditor (i); };
        row.bypass.setClickingTogglesState (true);
        row.bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc9853a));
        row.bypass.onClick = [this, i] { rack.setBypassed (i, slotRows[(size_t) i].bypass.getToggleState()); };
        addAndMakeVisible (row.number);
        addAndMakeVisible (row.choose);
        addAndMakeVisible (row.edit);
        addAndMakeVisible (row.bypass);
        refreshSlot (i);
    }

    // Volume
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    volume.setRange (Rack::minGainDb, 6.0, 0.1);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.textFromValueFunction = [] (double v) {
        return v <= Rack::minGainDb ? juce::String ("Muted") : juce::String (v, 1) + " dB";
    };
    volume.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    volume.setValue (rack.getMasterGainDb(), juce::dontSendNotification);
    volume.updateText();
    volume.onValueChange = [this] {
        rack.setMasterGainDb ((float) volume.getValue());
        markDirty();
    };
    addAndMakeVisible (volume);

    scanButton.onClick = [this] { showScanner(); };
    addAndMakeVisible (scanButton);

    // Engine, with the saved settings
    config.power = settings.getBoolValue ("power", false);
    config.source = settings.getIntValue ("source", 0) == 1 ? sysaudio::Source::selectedApps
                                                             : sysaudio::Source::wholeSystem;
    config.outputUid = settings.getValue ("output").toStdString();
    for (auto& id : juce::StringArray::fromLines (settings.getValue ("apps")))
        if (id.isNotEmpty())
            config.selectedApps.push_back (id.toStdString());

    engine = sysaudio::Engine::create (rack);
    engine->onChange = [this] { refreshFromEngine(); };

    power.setToggleState (config.power, juce::dontSendNotification);
    sourceBox.setSelectedId (config.source == sysaudio::Source::selectedApps ? 2 : 1, juce::dontSendNotification);
    engine->setConfig (config);
    refreshFromEngine();
    updateSourceView();
    startTimer (1000);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSettings();

    engine.reset(); // stops the tap: normal audio comes back
    rack.knownPlugins.removeChangeListener (this);
    rack.onSlotAboutToChange = nullptr;
    rack.onSlotChanged = nullptr;
    for (auto& w : editorWindows)
        w.reset();
    scannerWindow.reset();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (panel.brighter (0.08f));
    g.drawHorizontalLine (pluginsLabel.getY() - 8, 16.0f, (float) getWidth() - 16.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (36);
    power.setBounds (header.removeFromRight (90).reduced (0, 2));
    title.setBounds (header);

    status.setBounds (area.removeFromTop (40));
    area.removeFromTop (6);

    auto row = area.removeFromTop (28);
    sourceLabel.setBounds (row.removeFromLeft (70));
    sourceBox.setBounds (row);
    area.removeFromTop (8);

    auto listArea = area.removeFromTop (appList.isVisible() ? appListHeight : wholeNoteHeight);
    appList.setBounds (listArea);
    wholeSystemNote.setBounds (listArea.withHeight (50));
    area.removeFromTop (10);

    row = area.removeFromTop (28);
    outputLabel.setBounds (row.removeFromLeft (70));
    outputBox.setBounds (row);
    area.removeFromTop (24);

    pluginsLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    for (auto& r : slotRows)
    {
        auto line = area.removeFromTop (32);
        r.number.setBounds (line.removeFromLeft (28));
        r.bypass.setBounds (line.removeFromRight (70));
        line.removeFromRight (6);
        r.edit.setBounds (line.removeFromRight (56));
        line.removeFromRight (6);
        r.choose.setBounds (line);
        area.removeFromTop (6);
    }

    area.removeFromTop (8);
    row = area.removeFromTop (28);
    volumeLabel.setBounds (row.removeFromLeft (70));
    volume.setBounds (row);

    area.removeFromTop (12);
    scanButton.setBounds (area.removeFromTop (28).removeFromLeft (180));
}

//==============================================================================
void MainComponent::updateSourceView()
{
    const bool apps = config.source == sysaudio::Source::selectedApps;
    appList.setVisible (apps);
    wholeSystemNote.setVisible (! apps);
    setSize (500, apps ? 600 : 600 - appListHeight + wholeNoteHeight);
    resized();
}

void MainComponent::setPower (bool on)
{
    config.power = on;
    applyConfig();
}

void MainComponent::applyConfig()
{
    engine->setConfig (config);
    markDirty();
    refreshFromEngine();
}

void MainComponent::refreshFromEngine()
{
    if (engine == nullptr)
        return;

    status.setText (utf8 (engine->status()), juce::dontSendNotification);
    power.setButtonText (config.power ? "Power On" : "Power Off");
    status.setColour (juce::Label::textColourId, engine->isRunning() ? accent : dimText);
    refreshOutputs();
    refreshApps();
}

void MainComponent::refreshOutputs()
{
    auto devices = engine->outputDevices();
    auto defaultName = engine->defaultOutputName();

    const bool same = defaultName == shownDefaultName && devices.size() == shownOutputs.size()
                   && std::equal (devices.begin(), devices.end(), shownOutputs.begin(),
                                  [] (auto& a, auto& b) { return a.uid == b.uid && a.name == b.name; });
    if (same && outputBox.getNumItems() > 0)
        return;

    shownOutputs = std::move (devices);
    shownDefaultName = defaultName;

    outputBox.clear (juce::dontSendNotification);
    outputBox.addItem ("System default" + (defaultName.empty() ? juce::String() : " (" + utf8 (defaultName) + ")"), 1);
    outputBox.addSeparator();

    int selected = 1;
    for (size_t i = 0; i < shownOutputs.size(); ++i)
    {
        outputBox.addItem (utf8 (shownOutputs[i].name), (int) i + 2);
        if (shownOutputs[i].uid == config.outputUid)
            selected = (int) i + 2;
    }

    if (! config.outputUid.empty() && selected == 1) // remembered device is unplugged
    {
        outputBox.addItem ("(missing device)", 1000);
        outputBox.setItemEnabled (1000, false);
        selected = 1000;
    }
    outputBox.setSelectedId (selected, juce::dontSendNotification);
}

void MainComponent::refreshApps()
{
    apps = engine->audioApps();
    appList.updateContent();
    appList.repaint();
}

//==============================================================================
int MainComponent::getNumRows()
{
    return juce::jmax (1, (int) apps.size());
}

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (apps.empty())
    {
        if (row == 0)
        {
            g.setColour (dimText);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("No apps are using audio yet. Start playback in an app and it appears here.",
                        10, 0, width - 20, height, juce::Justification::centredLeft, true);
        }
        return;
    }

    if (! juce::isPositiveAndBelow (row, (int) apps.size()))
        return;

    auto& app = apps[(size_t) row];
    const bool ticked = std::find (config.selectedApps.begin(), config.selectedApps.end(), app.id)
                     != config.selectedApps.end();

    juce::Rectangle<float> box (10.0f, (float) height * 0.5f - 7.0f, 14.0f, 14.0f);
    g.setColour (ticked ? accent : dimText);
    if (ticked)
    {
        g.fillRoundedRectangle (box, 3.0f);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
        tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
        g.setColour (juce::Colours::black);
        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }
    else
    {
        g.drawRoundedRectangle (box, 3.0f, 1.2f);
    }

    g.setColour (app.running ? juce::Colours::white : dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (utf8 (app.name), 34, 0, width - 140, height, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (12.0f));
    if (app.playing)
    {
        g.setColour (accent);
        g.drawText ("playing", width - 100, 0, 90, height, juce::Justification::centredRight);
    }
    else if (! app.running)
    {
        g.setColour (dimText);
        g.drawText ("not running", width - 100, 0, 90, height, juce::Justification::centredRight);
    }
}

void MainComponent::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) apps.size()))
        return;

    const auto& id = apps[(size_t) row].id;
    auto& sel = config.selectedApps;
    if (auto it = std::find (sel.begin(), sel.end(), id); it != sel.end())
        sel.erase (it);
    else
        sel.push_back (id);

    applyConfig();
}

//==============================================================================
void MainComponent::refreshSlot (int slot)
{
    auto& row = slotRows[(size_t) slot];
    auto* plugin = rack.getPlugin (slot);

    juce::String text;
    if (rack.isLoading (slot))
        text = "Loading " + rack.getLoadingName (slot) + "...";
    else if (plugin != nullptr)
        text = plugin->getName();
    else
        text = "(empty) - click to choose";

    row.choose.setButtonText (text);
    row.edit.setEnabled (plugin != nullptr);
    row.bypass.setEnabled (plugin != nullptr);
    row.bypass.setToggleState (rack.isBypassed (slot), juce::dontSendNotification);
}

void MainComponent::showSlotMenu (int slot)
{
    const auto types = rack.knownPlugins.getTypes();
    const auto* current = rack.getPlugin (slot);

    juce::PopupMenu menu;
    if (types.isEmpty())
        menu.addItem (-1, "No plug-ins found yet. Scan first.", false);
    else
        juce::KnownPluginList::addToMenu (menu, types, juce::KnownPluginList::sortByManufacturer,
                                          current != nullptr ? current->getPluginDescription().createIdentifierString()
                                                             : juce::String());
    menu.addSeparator();
    menu.addItem (1, "Remove", current != nullptr || rack.isLoading (slot));
    menu.addItem (2, "Scan for plug-ins...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotRows[(size_t) slot].choose),
        [this, slot, types] (int result) {
            if (result == 1)
                rack.removePlugin (slot);
            else if (result == 2)
                showScanner();
            else if (auto index = juce::KnownPluginList::getIndexChosenByMenu (types, result); index >= 0)
            {
                juce::Component::SafePointer<MainComponent> safe (this);
                rack.loadPlugin (slot, types.getReference (index), [safe, name = types.getReference (index).name] (const juce::String& error) {
                    if (error.isNotEmpty() && safe != nullptr)
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                "Couldn't load " + name, error);
                });
            }
        });
}

void MainComponent::openEditor (int slot)
{
    auto* plugin = rack.getPlugin (slot);
    if (plugin == nullptr)
        return;

    if (auto& w = editorWindows[(size_t) slot])
    {
        w->toFront (true);
        return;
    }

    juce::AudioProcessorEditor* editor = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*plugin);

    juce::Component::SafePointer<MainComponent> safe (this);
    editorWindows[(size_t) slot] = std::make_unique<ToolWindow> (
        juce::String (slot + 1) + ": " + plugin->getName(), editor, editor->isResizable(),
        [safe, slot] { if (safe != nullptr) safe->closeEditor (slot); });
}

void MainComponent::closeEditor (int slot)
{
    editorWindows[(size_t) slot].reset();
}

void MainComponent::showScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto* list = new juce::PluginListComponent (rack.formatManager, rack.knownPlugins,
                                                appDataFolder().getChildFile ("scan-crash-marker"),
                                                &settings, true);
    list->setSize (640, 480);

    juce::Component::SafePointer<MainComponent> safe (this);
    scannerWindow = std::make_unique<ToolWindow> ("Plug-ins", list, true,
        [safe] { if (safe != nullptr) safe->scannerWindow.reset(); });
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    markDirty(); // plug-in list changed
}

void MainComponent::timerCallback()
{
    // Apps start and stop playing all the time; keep the list and status fresh.
    refreshFromEngine();

    // Plug-in knob changes don't notify us, so also save every 15 s.
    if (dirty || ++ticksSinceSave >= 15)
        saveSettings();
}

void MainComponent::saveSettings()
{
    dirty = false;
    ticksSinceSave = 0;
    settings.setValue ("power", config.power);
    settings.setValue ("source", config.source == sysaudio::Source::selectedApps ? 1 : 0);
    settings.setValue ("output", juce::String (config.outputUid));

    juce::StringArray ids;
    for (auto& id : config.selectedApps)
        ids.add (id);
    settings.setValue ("apps", ids.joinIntoString ("\n"));

    if (auto xml = rack.saveState())
        settings.setValue ("rack", xml.get());
    if (auto xml = rack.knownPlugins.createXml())
        settings.setValue ("knownPlugins", xml.get());

    settings.saveIfNeeded();
}
