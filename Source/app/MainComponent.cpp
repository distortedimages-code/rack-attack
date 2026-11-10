#include "MainComponent.h"

void MainComponent::Light::paint (juce::Graphics& g)
{
    juce::Colour c = ui::dim;
    juce::String t = "Non branché — cliquer ici"_fr;
    switch (state)
    {
        case Engine::Connection::connected: c = ui::accent;  t = "RackAttack connecté"_fr; break;
        case Engine::Connection::searching: c = ui::warning; t = "Recherche…"_fr; break;
        case Engine::Connection::noAnswer:  c = ui::bad;     t = "Pas de réponse — cliquer ici"_fr; break;
        case Engine::Connection::noPorts:   break;
    }
    auto r = getLocalBounds().toFloat();
    g.setColour (ui::panelLight);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (c);
    g.fillEllipse (r.getX() + 12.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
    g.setColour (ui::text);
    g.setFont (ui::font (14.0f));
    g.drawText (t, getLocalBounds().withTrimmedLeft (32).withTrimmedRight (10), juce::Justification::centredLeft);
}

MainComponent::MainComponent()
{
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    lookAndFeel.setColour (juce::ResizableWindow::backgroundColourId, ui::background);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.3f));
    lookAndFeel.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141614));
    lookAndFeel.setColour (juce::TextEditor::textColourId, ui::text);
    lookAndFeel.setColour (juce::Label::textColourId, ui::text);
    lookAndFeel.setColour (juce::Slider::thumbColourId, ui::accent);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, ui::accent);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, ui::panelLight);
    lookAndFeel.setColour (juce::ListBox::backgroundColourId, ui::panelLight);
    setLookAndFeel (&lookAndFeel);

    juce::PropertiesFile::Options options;
    options.applicationName = "RackAttack Editor";
    options.folderName = "RackAttack";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    pages = { &kitPage, &soundPage, &effectsPage, &libraryPage, &toolsPage };
    const juce::String names[5] = { "Kit", "Son", "Effets", "Bibliothèque"_fr, "Outils" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = pageButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (3001);
        b->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
        addChildComponent (pages[(size_t) i]);
    }

    kitPage.onEditSound = [this] (int s)
    {
        soundPage.selectSound (s);
        showPage (1);
    };

    light.onClick = [this] { showWizard(); };
    addAndMakeVisible (light);

    journalButton.setClickingTogglesState (true);
    journalButton.onClick = [this]
    {
        journalOpen = journalButton.getToggleState();
        journalDirty = true;
        resized();
        flushJournal();
    };
    addAndMakeVisible (journalButton);

    journal.setMultiLine (true);
    journal.setReadOnly (true);
    journal.setCaretVisible (false);
    journal.setScrollbarsShown (true);
    journal.setFont (ui::mono (11.5f));
    noteField.setTextToShowWhenEmpty ("Une remarque ? Écris-la ici"_fr, juce::Colours::grey);
    noteField.onReturnKey = [this] { engine.userNote (noteField.getText()); noteField.clear(); };
    noteButton.onClick = [this] { engine.userNote (noteField.getText()); noteField.clear(); };
    folderButton.onClick = [this] { session.getFolder().startAsProcess(); };
    zipButton.onClick = [this]
    {
        session.saveText ("carte.txt", engine.getAddressMap().table());
        auto zip = session.makeZip();
        if (zip == juce::File())
            statusLine.show ("Le zip n'a pas pu être créé."_fr, ui::bad);
        else
        {
            statusLine.show ("Zip prêt : envoie-le moi dans la conversation."_fr, ui::accent);
            zip.revealToUser();
        }
    };
    for (auto* c : std::initializer_list<juce::Component*> { &journal, &noteField, &noteButton, &zipButton, &folderButton })
        addChildComponent (c);

    addAndMakeVisible (statusLine);
    bar.setColour (juce::ProgressBar::foregroundColourId, ui::accent);
    addChildComponent (bar);

    addChildComponent (wizard);

    engine.addListener (this);

    // settings
    engine.deviceId = settings->getIntValue ("id", 0);
    engine.channel = settings->getIntValue ("channel", 1);
    engine.baseNote = settings->getIntValue ("baseNote", 36);
    toolsPage.refreshSettings();
    inId = settings->getValue ("in");
    outId = settings->getValue ("out");
    bool inOk = false, outOk = false;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (d.identifier == inId) inOk = engine.openInput (inId).isEmpty();
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == outId) outOk = engine.openOutput (outId).isEmpty();

    engine.userNote ("RackAttack Editor "_fr + RA_VERSION + " sur " + juce::SystemStats::getOperatingSystemName());

    setSize (1400, 900);
    showPage (0);
    startTimer (250);

    if (juce::SystemStats::getEnvironmentVariable ("RA_DEMO", {}) == "1")
    {
        demo = std::make_unique<FakeRackAttack>();
        demo->engine = &engine;
        engine.pauseScale = 0.1;
        engine.setTestOutput ([this] (const juce::MidiMessage& m) { demo->receive (m); });
        showPage (juce::SystemStats::getEnvironmentVariable ("RA_PAGE", "0").getIntValue());
        if (juce::SystemStats::getEnvironmentVariable ("RA_WIZARD", {}) == "1")
            showWizard();
        else
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe != nullptr) safe->engine.connect();
            });
    }
    else if (settings->getBoolValue ("setupDone", false) && inOk && outOk)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr) safe->engine.connect();
        });
    else
        showWizard();
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.stopPlayer();
    engine.setTestOutput (nullptr);
    engine.removeListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::showPage (int index)
{
    page = juce::jlimit (0, 4, index);
    for (int i = 0; i < 5; ++i)
    {
        pages[(size_t) i]->setVisible (i == page);
        pageButtons[i]->setToggleState (i == page, juce::dontSendNotification);
    }
}

void MainComponent::showWizard()
{
    wizard.setVisible (true);
    wizard.toFront (true);
    wizard.start();
}

void MainComponent::chooseInput (const juce::String& identifier)
{
    inId = identifier;
    engine.openInput (identifier);
    saveSettings();
}

void MainComponent::chooseOutput (const juce::String& identifier)
{
    outId = identifier;
    engine.openOutput (identifier);
    saveSettings();
}

void MainComponent::wizardFinished (bool connected)
{
    wizard.setVisible (false);
    if (connected)
        settings->setValue ("setupDone", true);
    saveSettings();
    showPage (0);
}

void MainComponent::saveSettings()
{
    settings->setValue ("in", inId);
    settings->setValue ("out", outId);
    settings->setValue ("id", engine.deviceId.load());
    settings->setValue ("channel", engine.channel.load());
    settings->setValue ("baseNote", engine.baseNote.load());
    settings->saveIfNeeded();
}

//==============================================================================
void MainComponent::journalLine (const juce::String& line)
{
    journalLines.add (line);
    if (journalLines.size() > 600)
        journalLines.removeRange (0, journalLines.size() - 500);
    journalDirty = true;
}

void MainComponent::flushJournal()
{
    if (! journalDirty || ! journalOpen)
        return;
    journalDirty = false;
    journal.setText (journalLines.joinIntoString ("\n"), juce::dontSendNotification);
    journal.moveCaretToEnd();
}

void MainComponent::statusChanged (const juce::String& s)
{
    statusLine.show (s, ui::text);
}

void MainComponent::connectionChanged (Engine::Connection c)
{
    light.state = c;
    light.repaint();
    if (c == Engine::Connection::connected)
    {
        saveSettings();          // the Sysex ID and channel found
        toolsPage.refreshSettings();
    }
}

void MainComponent::progressChanged (int done, int total, const juce::String& what)
{
    if (total <= 0)
        return;
    progress = (double) done / total;
    bar.setVisible (done < total);
    bar.setTextToDisplay (what);
}

void MainComponent::taskFinished (const juce::String&, bool ok, const juce::String& message)
{
    bar.setVisible (false);
    statusLine.show (message, ok ? ui::accent : ui::bad);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);
    g.setColour (ui::accent);
    g.setFont (ui::font (20.0f, true));
    g.drawText ("RackAttack", 16, 0, 140, 56, juce::Justification::centredLeft);
    g.setColour (ui::panelLight);
    g.fillRect (0, 56, getWidth(), 1);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    wizard.setBounds (r);

    auto top = r.removeFromTop (56).reduced (12, 10);
    top.removeFromLeft (140);
    for (auto* b : pageButtons)
    {
        b->setBounds (top.removeFromLeft (b->getButtonText().length() > 6 ? 140 : 100));
        top.removeFromLeft (6);
    }
    journalButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (10);
    light.setBounds (top.removeFromRight (280));

    auto bottom = r.removeFromBottom (34).reduced (12, 4);
    bar.setBounds (bottom.removeFromRight (360));
    bottom.removeFromRight (10);
    statusLine.setBounds (bottom);

    for (auto* c : std::initializer_list<juce::Component*> { &journal, &noteField, &noteButton, &zipButton, &folderButton })
        c->setVisible (journalOpen);
    if (journalOpen)
    {
        auto side = r.removeFromRight (420).reduced (8, 8);
        auto buttons = side.removeFromBottom (30);
        zipButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (6);
        folderButton.setBounds (buttons.removeFromLeft (90));
        side.removeFromBottom (6);
        auto noteRow = side.removeFromBottom (30);
        noteButton.setBounds (noteRow.removeFromRight (70));
        noteRow.removeFromRight (6);
        noteField.setBounds (noteRow);
        side.removeFromBottom (6);
        journal.setBounds (side);
    }

    for (auto* p : pages)
        p->setBounds (r);
}
