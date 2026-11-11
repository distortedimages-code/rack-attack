#include "MainComponent.h"

#include "LayoutAudit.h"

void MainComponent::Light::paint (juce::Graphics& g)
{
    juce::Colour c = px::dim;
    juce::String t = "Non branché — clic ici"_fr;
    switch (state)
    {
        case Engine::Connection::connected: c = px::white;  t = "RackAttack connecté"_fr; break;
        case Engine::Connection::searching: c = px::yellow; t = "Recherche…"_fr; break;
        case Engine::Connection::noAnswer:  c = px::red;    t = "Pas de réponse — clic ici"_fr; break;
        case Engine::Connection::noPorts:   break;
    }
    if (simulation)
    {
        c = px::yellow;
        t = "SIMULATION";
    }
    auto r = getLocalBounds();
    {
        // too narrow (an update button next to it): the short version
        const auto f = px::text (24.0f);
        if (juce::GlyphArrangement::getStringWidth (f, t) > (float) (getWidth() - 42))
            t = t.upToFirstOccurrenceOf (juce::String::fromUTF8 (" \xe2\x80\x94"), false, false)
                 .replace ("RackAttack connect", "Connect");
    }
    g.setColour (px::cell);
    g.fillRect (r);
    px::frameRect (g, r, simulation ? px::yellow : px::frame, 2);
    // a square light; blinks while searching
    const bool on = state != Engine::Connection::searching || (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
    if (on)
    {
        g.setColour (c);
        g.fillRect (r.getX() + 12, r.getCentreY() - 6, 12, 12);
    }
    px::drawText (g, t, r.withTrimmedLeft (34).withTrimmedRight (8), juce::Justification::centredLeft, px::text (24.0f), px::white);
}

MainComponent::MainComponent (AppContext& c)
    : ctx (c), session (c.session), engine (c.engine), library (c.library)
{
    setLookAndFeel (&lookAndFeel);

    pages = { &seqPage, &kitPage, &soundPage, &effectsPage, &libraryPage, &toolsPage };
    const juce::String names[numPages] = { "Séquenceur"_fr, "Kit", "Son", "Effets", "Bibliothèque"_fr, "Outils" };
    for (int i = 0; i < numPages; ++i)
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
        showPage (2);
    };

    light.onClick = [this] { showWizard(); };
    light.state = engine.getConnection();
    light.simulation = ctx.isSimulation();
    toolsPage.isSimulation = [this] { return ctx.isSimulation(); };
    toolsPage.setSimulation = [this] (bool on)
    {
        ctx.setSimulation (on);
        light.simulation = ctx.isSimulation();
        light.repaint();
        if (! on && ! ctx.isSetUp())
            showWizard();
    };
    addAndMakeVisible (light);

    ui::styleBig (updateButton, true);
    updateButton.onClick = [this] { showUpdateDialog(); };
    addChildComponent (updateButton);

    journalButton.setClickingTogglesState (true);
    journalButton.onClick = [this]
    {
        journalOpen = journalButton.getToggleState();
        journalDirty = true;
        resized();
        flushJournal();
    };
    addAndMakeVisible (journalButton);

    // the zoom: as remembered, else the largest that fits the screen at 100 % or less
    {
        float z = (float) ctx.settings().getDoubleValue ("zoom", 0.0);
        if (z <= 0.0f)
        {
            z = 1.0f;
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                if ((float) d->userArea.getWidth() < (float) ui::designWidth || (float) d->userArea.getHeight() < (float) ui::designHeight + 60.0f)
                    z = 0.75f;
        }
        zoom = juce::jlimit (0.75f, 2.0f, z);
    }
    zoomButton.setButtonText (juce::String (juce::roundToInt (zoom * 100.0f)) + " %");
    zoomButton.onClick = [this] { showZoomMenu(); };
    addAndMakeVisible (zoomButton);

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
    for (auto* comp : std::initializer_list<juce::Component*> { &journalBacking, &journal, &noteField, &noteButton, &zipButton, &folderButton })
    {
        addChildComponent (comp);
        comp->getProperties().set ("overlay", true);      // over the page, on purpose
    }

    addAndMakeVisible (statusLine);
    addChildComponent (bar);

    addChildComponent (wizard);
    addChildComponent (measureWizard);
    measureWizard.onClose = [this] { measureWizard.setVisible (false); toolsPage.refreshSettings(); };
    measureWizard.useMeasures = [this] (const juce::File& f, juce::String& error)
    {
        const bool ok = ctx.useMeasures (f, error);
        toolsPage.refreshSettings();
        return ok;
    };
    measureWizard.isSimulation = [this] { return ctx.isSimulation(); };
    measureWizard.stopSequencer = [this] { ctx.sequencer.stop(); };
    measureWizard.settings = [this] { return &ctx.settings(); };
    toolsPage.openMeasure = [this] { showMeasure(); };
    toolsPage.installUpdate = [this] { showUpdateDialog(); };
    toolsPage.loadMeasures = [this] { chooseMeasures(); };
    toolsPage.forgetMeasures = [this] { ctx.forgetMeasures(); };
    toolsPage.measuresSource = [this] { return ctx.measuresSource(); };

    engine.addListener (this);
    ctx.updater.changes.addChangeListener (this);

    setSize (ui::designWidth, ui::designHeight);
    showPage (juce::SystemStats::getEnvironmentVariable ("RA_PAGE", "0").getIntValue());
    startTimer (250);

    ctx.start();   // the first time a window opens: ports of last time, then find the unit
    if (! ctx.isSetUp() || juce::SystemStats::getEnvironmentVariable ("RA_WIZARD", {}) == "1")
        showWizard();

    // the layout check (RA_AUDIT=1): every page, every assistant screen, then quit
    if (juce::SystemStats::getEnvironmentVariable ("RA_AUDIT", {}).isNotEmpty())
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->runAudit();
        });

    // screenshots: open the measuring assistant (RA_MEASURE=1), or run its try (RA_MEASURE=run)
    const auto measureEnv = juce::SystemStats::getEnvironmentVariable ("RA_MEASURE", {});
    if (measureEnv.isNotEmpty())
    {
        showMeasure();
        if (measureEnv != "1")
            measureWizard.demo (measureEnv);
    }

    refreshUpdate();
    ctx.updater.checkIfDue();
}

MainComponent::~MainComponent()
{
    stopTimer();
    ctx.updater.changes.removeChangeListener (this);
    engine.removeListener (this);
    setLookAndFeel (nullptr);
}

void MainComponent::showPage (int index)
{
    page = juce::jlimit (0, numPages - 1, index);
    for (int i = 0; i < numPages; ++i)
    {
        pages[(size_t) i]->setVisible (i == page);
        pageButtons[i]->setToggleState (i == page, juce::dontSendNotification);
    }
    if (page == 0)
        seqPage.grabKeyboardFocus();
}

void MainComponent::showWizard()
{
    wizard.setVisible (true);
    wizard.toFront (true);
    wizard.start();
}

void MainComponent::showZoomMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Taille de la fenêtre"_fr);
    juce::Rectangle<int> screen;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        screen = d->userArea;
    for (int i = 0; i < (int) std::size (ui::zoomSteps); ++i)
    {
        const float z = ui::zoomSteps[i];
        const bool fits = screen.isEmpty() || ((float) ui::designWidth * z <= (float) screen.getWidth()
                                               && (float) ui::designHeight * z + 40.0f <= (float) screen.getHeight());
        m.addItem (i + 1, juce::String (juce::roundToInt (z * 100.0f)) + " %" + (fits ? juce::String() : "  (plus grand que l'écran)"_fr),
                   true, std::abs (z - zoom) < 0.01f);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton),
                     [safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;
        safe->zoom = ui::zoomSteps[r - 1];
        safe->ctx.settings().setValue ("zoom", (double) safe->zoom);
        safe->ctx.settings().saveIfNeeded();
        safe->zoomButton.setButtonText (juce::String (juce::roundToInt (safe->zoom * 100.0f)) + " %");
        if (safe->onZoom)
            safe->onZoom (safe->zoom);
    });
}

void MainComponent::runAudit()
{
    juce::StringArray report;
    auto check = [&] (const juce::String& where)
    {
        resized();
        for (auto& line : audit::run (*this))
            report.add (where + " : " + line);
    };
    const juce::String names[numPages] = { "Séquenceur"_fr, "Kit", "Son", "Effets", "Bibliothèque"_fr, "Outils" };
    for (int i = 0; i < numPages; ++i)
    {
        showPage (i);
        check (names[i]);
    }
    showPage (0);
    updateButton.setButtonText ("Mettre à jour"_fr);
    updateButton.setVisible (true);
    check ("Mise à jour disponible");
    updateButton.setVisible (false);
    journalButton.setToggleState (true, juce::sendNotification);
    check ("Journal ouvert");
    journalButton.setToggleState (false, juce::sendNotification);

    wizard.setVisible (true);
    wizard.toFront (false);
    wizard.start();
    check ("Branchement 1/3");
    wizard.setVisible (false);

    showMeasure();
    check ("Mesure 1/5");
    for (auto screen : { "audio", "ready", "run", "done" })
    {
        measureWizard.demo (screen);
        check ("Mesure " + juce::String (screen));
    }
    measureWizard.setVisible (false);

    const auto file = juce::File (juce::SystemStats::getEnvironmentVariable ("RA_AUDIT", {}));
    const auto text = report.isEmpty() ? juce::String ("Rien ne déborde."_fr) : report.joinIntoString ("\n");
    if (file.getParentDirectory().isDirectory() && file.getFileName().isNotEmpty() && file.getFileName() != "1")
        file.replaceWithText (text);
    std::printf ("%s\n%d problème(s)\n", text.toRawUTF8(), report.size());
    std::fflush (stdout);
    juce::JUCEApplicationBase::quit();
}

void MainComponent::showMeasure()
{
    measureWizard.setVisible (true);
    measureWizard.toFront (true);
    measureWizard.start();
}

void MainComponent::chooseMeasures()
{
    chooser = std::make_unique<juce::FileChooser> ("Mesures d'un RackAttack", measure::defaultFolder(), "*.ramesure");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        if (ctx.useMeasures (file, error))
            statusLine.show ("Le simulateur suit maintenant : "_fr + ctx.measuresSource(), ui::accent);
        else
            statusLine.show (error, ui::bad);
        toolsPage.refreshSettings();
    });
}

void MainComponent::startSimulation()
{
    ctx.setSimulation (true);
    ctx.setSetUp (true);
    wizard.setVisible (false);
    light.simulation = true;
    light.repaint();
    toolsPage.refreshSettings();
}

void MainComponent::wizardFinished (bool connected)
{
    wizard.setVisible (false);
    ctx.setSetUp (connected);
    showPage (0);
}

//==============================================================================
// Updates

void MainComponent::refreshUpdate()
{
    auto& up = ctx.updater;
    bool show = up.getState() == Updater::State::Available || up.getState() == Updater::State::Installed
             || juce::SystemStats::getEnvironmentVariable ("RA_FAKE_UPDATE", {}) == "1";     // screenshots
    updateButton.setButtonText (up.getState() == Updater::State::Installed ? "Redémarrer"_fr : "Mettre à jour"_fr);
    updateButton.setTooltip (up.getStatusText());
    updateButton.setVisible (show);
    resized();
}

void MainComponent::showUpdateDialog()
{
    auto& up = ctx.updater;
    const auto& rel = up.getRelease();
    auto opts = juce::MessageBoxOptions().withAssociatedComponent (this);

    if (up.getState() == Updater::State::Installed)
    {
        juce::AlertWindow::showAsync (opts.withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("Mise à jour installée"_fr).withMessage (up.getStatusText()).withButton ("OK"), nullptr);
        return;
    }
    if (rel.version.isEmpty())
        return;

    auto notes = rel.notes.trim();
    if (notes.length() > 1800)
        notes = notes.substring (0, 1800) + "...";
    const bool available = up.getState() == Updater::State::Available;
   #if JUCE_MAC
    const juce::String installText = "Installer maintenant";
   #else
    const juce::String installText = "Ouvrir la page de téléchargement"_fr;
   #endif

    auto box = opts.withIconType (juce::MessageBoxIconType::InfoIcon)
                   .withTitle ("RackAttack Editor " + rel.version)
                   .withMessage ((notes.isEmpty() ? "Pas de notes pour cette version."_fr : notes)
                                 + (available ? "\n\nTu as la version "_fr + Updater::currentVersion()
                                                    + ". La nouvelle remplace le plug-in et l'appli sur ce Mac ; "
                                                      "macOS peut demander ton mot de passe. Quitte et rouvre ton logiciel ensuite."_fr
                                              : juce::String()));
    if (available)
        box = box.withButton (installText).withButton ("Plus tard").withButton ("Ignorer cette version");
    else
        box = box.withButton ("OK");

    juce::AlertWindow::showAsync (box, [safe = juce::Component::SafePointer<MainComponent> (this), available] (int r)
    {
        if (safe == nullptr || ! available)
            return;
        auto& u = safe->ctx.updater;
        if (r == 1)      u.install();
        else if (r == 3) u.skipThisVersion();
    });
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
    light.simulation = ctx.isSimulation();
    light.repaint();
    if (c == Engine::Connection::connected)
    {
        ctx.saveSettings();          // the Sysex ID and channel found
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
    g.fillAll (px::black);
    auto title = juce::Rectangle<int> (16, 0, 200, 56);
    const auto f = px::label (13.0f, true);
    auto put = [&] (const juce::String& t, juce::Colour c)
    {
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, t)) + 2;
        px::drawText (g, t, title.removeFromLeft (w), juce::Justification::centredLeft, f, c);
    };
    put ("RACK", px::white);
    put (juce::String::fromUTF8 ("·"), px::red);
    put ("ATTACK", px::white);
    px::dither (g, { 0, 54, getWidth(), 2 }, px::faint, 3);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    wizard.setBounds (r);
    measureWizard.setBounds (r);

    // the top bar: the page buttons as wide as their names, then from the right the journal,
    // the zoom, the update when there is one, and the light gets what is left
    auto top = r.removeFromTop (56).reduced (12, 10);
    top.removeFromLeft (184);
    auto widthOf = [this, &top] (juce::TextButton& b)
    {
        const auto f = lookAndFeel.getTextButtonFont (b, top.getHeight() - 3);
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, b.getButtonText())) + 30;
    };
    for (auto* b : pageButtons)
    {
        b->setBounds (top.removeFromLeft (juce::jmax (72, widthOf (*b))));
        top.removeFromLeft (6);
    }
    top.removeFromLeft (4);
    journalButton.setBounds (top.removeFromRight (88));
    top.removeFromRight (8);
    zoomButton.setBounds (top.removeFromRight (76));
    top.removeFromRight (8);
    if (updateButton.isVisible())
    {
        updateButton.setBounds (top.removeFromRight (juce::jmin (widthOf (updateButton), top.getWidth() - 150)));
        top.removeFromRight (8);
    }
    light.setBounds (top);

    auto bottom = r.removeFromBottom (34).reduced (12, 4);
    bar.setBounds (bottom.removeFromRight (360));
    bottom.removeFromRight (10);
    statusLine.setBounds (bottom);

    for (auto* p : pages)
        p->setBounds (r);

    // the journal opens over the right of the page (the page keeps its size)
    for (auto* comp : std::initializer_list<juce::Component*> { &journalBacking, &journal, &noteField, &noteButton, &zipButton, &folderButton })
        comp->setVisible (journalOpen);
    if (journalOpen)
    {
        auto side = r.removeFromRight (420);
        journalBacking.setBounds (side);
        side = side.reduced (8, 8).withTrimmedLeft (4);
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
}
