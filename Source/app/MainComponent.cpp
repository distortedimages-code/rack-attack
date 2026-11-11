#include "MainComponent.h"

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
    auto r = getLocalBounds();
    g.setColour (px::cell);
    g.fillRect (r);
    px::frameRect (g, r, px::frame, 2);
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
    for (auto* comp : std::initializer_list<juce::Component*> { &journal, &noteField, &noteButton, &zipButton, &folderButton })
        addChildComponent (comp);

    addAndMakeVisible (statusLine);
    addChildComponent (bar);

    addChildComponent (wizard);

    engine.addListener (this);
    ctx.updater.changes.addChangeListener (this);

    setSize (1400, 900);
    showPage (juce::SystemStats::getEnvironmentVariable ("RA_PAGE", "0").getIntValue());
    startTimer (250);

    ctx.start();   // the first time a window opens: ports of last time, then find the unit
    if (! ctx.isSetUp() || juce::SystemStats::getEnvironmentVariable ("RA_WIZARD", {}) == "1")
        showWizard();

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
    bool show = up.getState() == Updater::State::Available || up.getState() == Updater::State::Installed;
    updateButton.setButtonText (up.getState() == Updater::State::Installed ? "Redémarre ton logiciel"_fr
                                                                           : "Mise à jour "_fr + up.getRelease().version);
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

    auto top = r.removeFromTop (56).reduced (12, 10);
    top.removeFromLeft (200);
    for (auto* b : pageButtons)
    {
        b->setBounds (top.removeFromLeft (b->getButtonText().length() > 6 ? 140 : 90));
        top.removeFromLeft (6);
    }
    journalButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (10);
    light.setBounds (top.removeFromRight (270));
    top.removeFromRight (10);
    if (updateButton.isVisible())
        updateButton.setBounds (top.removeFromRight (200));

    auto bottom = r.removeFromBottom (34).reduced (12, 4);
    bar.setBounds (bottom.removeFromRight (360));
    bottom.removeFromRight (10);
    statusLine.setBounds (bottom);

    for (auto* comp : std::initializer_list<juce::Component*> { &journal, &noteField, &noteButton, &zipButton, &folderButton })
        comp->setVisible (journalOpen);
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
