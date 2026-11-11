#include "ToolsPage.h"

#include "Pixel.h"

namespace
{
    void fillSounds (juce::ComboBox& box)
    {
        for (int i = 0; i < 24; ++i)
            box.addItem ("Son " + ui::twoDigits (i + 1), i + 1);
        box.setSelectedId (1, juce::dontSendNotification);
    }

    struct Rows
    {
        juce::Rectangle<int> area;
        juce::Rectangle<int> next (int height = 28)
        {
            auto r = area.removeFromTop (height);
            area.removeFromTop (8);
            return r;
        }
    };

    struct Page : public juce::Component
    {
        explicit Page (Engine& e) : engine (e) {}
        void paint (juce::Graphics& g) override   { g.fillAll (ui::panel); }

        juce::Label& text (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::topLeft);
            l.setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (l);
            return l;
        }

        Engine& engine;
    };

    //==========================================================================
    struct ReadPage : public Page
    {
        explicit ReadPage (Engine& e) : Page (e)
        {
            text (intro, "Demandes brutes au RackAttack. Chaque réponse est enregistrée dans le dossier recus/ de la session."_fr);
            for (auto* b : { &all, &globals, &stop })
                addAndMakeVisible (b);
            all.onClick     = [this] { engine.readAllForTest(); };
            globals.onClick = [this] { engine.readGlobals(); };
            stop.onClick    = [this] { engine.cancel(); };
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (40));
            auto row = r.next (32);
            all.setBounds (row.removeFromLeft (200));
            row.removeFromLeft (8);
            globals.setBounds (row.removeFromLeft (120));
            stop.setBounds (r.next().withWidth (200));
        }

        juce::Label intro;
        juce::TextButton all { "Globaux + kit" }, globals { "Globaux" }, stop { "Arrêter"_fr };
    };

    //==========================================================================
    struct PanelPage : public Page
    {
        explicit PanelPage (Engine& e) : Page (e)
        {
            text (intro, "Chaque réglage envoyé par la machine (Ctl. send = SysEx). « ??? » = adresse encore inconnue."_fr);
            view.setMultiLine (true);
            view.setReadOnly (true);
            view.setCaretVisible (false);
            view.setFont (ui::mono (12.5f));
            addAndMakeVisible (view);
            addAndMakeVisible (clear);
            clear.onClick = [this] { engine.clearAddressMap(); };
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (24));
            clear.setBounds (r.area.removeFromBottom (28).withWidth (180));
            r.area.removeFromBottom (8);
            view.setBounds (r.area);
        }

        juce::Label intro;
        juce::TextEditor view;
        juce::TextButton clear { "Effacer la carte" };
    };

    //==========================================================================
    struct PatternPage : public Page
    {
        explicit PatternPage (Engine& e) : Page (e)
        {
            text (intro, "Le format des pas d'un pattern est inconnu. Ces boutons envoient plusieurs formes de demande "
                         "et enregistrent les réponses. Ce ne sont que des demandes : rien n'est modifié."_fr);
            fillSounds (sound);
            addAndMakeVisible (sound);
            addAndMakeVisible (probe);
            addAndMakeVisible (locations);
            probe.onClick = [this] { engine.probePatterns (sound.getSelectedId() - 1); };
            locations.onClick = [this] { engine.probeLocations(); };
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (56));
            auto row = r.next();
            sound.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            probe.setBounds (row.removeFromLeft (220));
            locations.setBounds (r.next().withWidth (220));
        }

        juce::Label intro;
        juce::ComboBox sound;
        juce::TextButton probe { "Demander le pattern" }, locations { "Sondes d'emplacement" };
    };

    //==========================================================================
    struct PlayPage : public Page
    {
        explicit PlayPage (Engine& e) : Page (e)
        {
            fillSounds (sound);
            addAndMakeVisible (sound);
            addAndMakeVisible (listen);
            listen.onClick = [this] { engine.playSound (soundIndex()); };

            text (rhythmLabel, "Rythme de test");
            bpm.setRange (40.0, 240.0, 1.0);
            bpm.setValue (120.0, juce::dontSendNotification);
            bpm.setTextValueSuffix (" BPM");
            addAndMakeVisible (bpm);
            division.addItem ("croches", 2);
            division.addItem ("doubles", 4);
            division.setSelectedId (4, juce::dontSendNotification);
            addAndMakeVisible (division);
            rhythm.setClickingTogglesState (true);
            addAndMakeVisible (rhythm);
            auto updateRhythm = [this] { engine.setRhythm (rhythm.getToggleState(), bpm.getValue(), division.getSelectedId(), soundIndex()); };
            rhythm.onClick = updateRhythm;
            bpm.onDragEnd = [this, updateRhythm] { if (rhythm.getToggleState()) updateRhythm(); };
            division.onChange = [this, updateRhythm] { if (rhythm.getToggleState()) updateRhythm(); };

            text (burstLabel, "Rafale de changements (valeur qui monte et descend)");
            for (int rate : { 0, 25, 50, 100, 150, 200, 260 })
                burst.addItem (rate == 0 ? juce::String ("Off") : juce::String (rate) + " messages/s", rate + 1);
            burst.setSelectedId (1, juce::dontSendNotification);
            addAndMakeVisible (burst);
            text (addressLabel, "Adresse (hex)");
            address.setText ("35");
            address.setInputRestrictions (4, "0123456789abcdefABCDEF");
            addAndMakeVisible (address);
            text (addressName, "Volume");
            auto updateBurst = [this]
            {
                int adr = address.getText().getHexValue32();
                auto* pn = ra::parameterName (ra::Object::sound, adr);
                addressName.setText (pn != nullptr ? pn : "???", juce::dontSendNotification);
                engine.setBurst (burst.getSelectedId() - 1, soundIndex(), adr);
            };
            burst.onChange = updateBurst;
            address.onReturnKey = updateBurst;

            text (resendLabel, "Renvoyer le son tel qu'il a été lu (dump complet)"_fr);
            addAndMakeVisible (resendOnce);
            resendOnce.onClick = [this] { engine.resendSoundDump (soundIndex()); };
            resendLoop.addItem ("Boucle : off", 1);
            for (int ms : { 1000, 500, 250, 125 })
                resendLoop.addItem ("toutes les " + juce::String (ms) + " ms", ms);
            resendLoop.setSelectedId (1, juce::dontSendNotification);
            addAndMakeVisible (resendLoop);
            resendLoop.onChange = [this]
            {
                int id = resendLoop.getSelectedId();
                engine.setResendLoop (soundIndex(), id > 1 ? id : 0);
            };

            addAndMakeVisible (stopAll);
            stopAll.onClick = [this]
            {
                engine.stopPlayer();
                rhythm.setToggleState (false, juce::dontSendNotification);
                burst.setSelectedId (1, juce::dontSendNotification);
                resendLoop.setSelectedId (1, juce::dontSendNotification);
            };

            sound.onChange = [this, updateRhythm, updateBurst]
            {
                if (rhythm.getToggleState()) updateRhythm();
                if (burst.getSelectedId() > 1) updateBurst();
            };
        }

        int soundIndex() const   { return sound.getSelectedId() - 1; }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            auto row = r.next();
            sound.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            listen.setBounds (row.removeFromLeft (110));

            rhythmLabel.setBounds (r.next (20));
            row = r.next();
            rhythm.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            division.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            bpm.setBounds (row.removeFromLeft (220));

            burstLabel.setBounds (r.next (20));
            row = r.next();
            burst.setBounds (row.removeFromLeft (160));
            row.removeFromLeft (16);
            addressLabel.setBounds (row.removeFromLeft (100));
            address.setBounds (row.removeFromLeft (60));
            row.removeFromLeft (8);
            addressName.setBounds (row);

            resendLabel.setBounds (r.next (20));
            row = r.next();
            resendOnce.setBounds (row.removeFromLeft (180));
            row.removeFromLeft (8);
            resendLoop.setBounds (row.removeFromLeft (180));

            r.next (6);
            stopAll.setBounds (r.next (32).withWidth (180));
        }

        juce::ComboBox sound, division, burst, resendLoop;
        juce::TextButton listen { "Écouter"_fr }, rhythm { "Rythme" }, resendOnce { "Renvoyer le son lu" },
                         stopAll { "Tout arrêter"_fr };
        juce::Slider bpm { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextEditor address;
        juce::Label rhythmLabel, burstLabel, addressLabel, addressName, resendLabel;
    };

    //==========================================================================
    struct CcPage : public Page
    {
        explicit CcPage (Engine& e) : Page (e)
        {
            text (intro, "Le plugin Attack d'époque se pilotait par CC 12 à 59 sur les canaux 1 à 12. "_fr
                         "Pour chaque CC : valeur 0 + une note, puis 127 + une note, une demi-seconde d'écart. "_fr
                         "À essayer avec le programme en mode MIDI Plug-in."_fr);
            for (auto* s : { &first, &last, &note })
            {
                s->setRange (0, 127, 1);
                addAndMakeVisible (s);
            }
            first.setValue (12, juce::dontSendNotification);
            last.setValue (59, juce::dontSendNotification);
            note.setValue (36, juce::dontSendNotification);
            text (firstLabel, "Premier CC");
            text (lastLabel, "Dernier CC");
            text (noteLabel, "Note");
            addAndMakeVisible (start);
            addAndMakeVisible (stop);
            start.onClick = [this] { engine.startCcSweep ((int) first.getValue(), (int) last.getValue(), (int) note.getValue()); };
            stop.onClick = [this] { engine.stopPlayer(); };
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (72));
            using LabelAndSlider = std::pair<juce::Label*, juce::Slider*>;
            for (auto [label, slider] : { LabelAndSlider { &firstLabel, &first }, LabelAndSlider { &lastLabel, &last },
                                          LabelAndSlider { &noteLabel, &note } })
            {
                auto row = r.next();
                label->setBounds (row.removeFromLeft (100));
                slider->setBounds (row.removeFromLeft (150));
            }
            auto row = r.next (32);
            start.setBounds (row.removeFromLeft (180));
            row.removeFromLeft (8);
            stop.setBounds (row.removeFromLeft (120));
        }

        juce::Label intro, firstLabel, lastLabel, noteLabel;
        juce::Slider first { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft },
                     last { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft },
                     note { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
        juce::TextButton start { "Lancer le balayage" }, stop { "Arrêter"_fr };
    };

    //==========================================================================
    struct FreePage : public Page
    {
        explicit FreePage (Engine& e) : Page (e)
        {
            text (warning, "Attention : ici tout part tel quel, y compris des commandes qui peuvent effacer ou écrire en mémoire. "_fr
                           "À n'utiliser que si je te donne un message précis à tester."_fr);
            warning.setColour (juce::Label::textColourId, ui::warning);
            bytes.setFont (ui::mono (14.0f));
            bytes.setTextToShowWhenEmpty ("ex. 04 00", juce::Colours::grey);
            addAndMakeVisible (bytes);
            wrap.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (wrap);
            addAndMakeVisible (send);
            send.onClick = [this]
            {
                auto parsed = ra::parseHex (bytes.getText().toStdString());
                if (! parsed || parsed->empty())
                {
                    result.setText ("Octets illisibles : écris-les en hexadécimal, séparés par des espaces."_fr, juce::dontSendNotification);
                    return;
                }
                auto msg = wrap.getToggleState() ? ra::build ((std::uint8_t) engine.deviceId.load(), *parsed) : *parsed;
                result.setText ("Envoyé : "_fr + juce::String (ra::hex (msg)), juce::dontSendNotification);
                engine.sendSysex (msg, "envoi libre");
            };
            text (result, {});
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            warning.setBounds (r.next (56));
            bytes.setBounds (r.next());
            wrap.setBounds (r.next (24));
            send.setBounds (r.next().withWidth (120));
            result.setBounds (r.next (60));
        }

        juce::Label warning, result;
        juce::TextEditor bytes;
        juce::ToggleButton wrap { "Ajouter F0 3E 11 <ID> devant, le checksum et F7 derrière"_fr };
        juce::TextButton send { "Envoyer" };
    };
}

//==============================================================================
ToolsPage::ToolsPage (Engine& e, Updater& u, std::function<void()> changed)
    : engine (e), updater (u), settingsChanged (std::move (changed))
{
    updateLabel.setColour (juce::Label::textColourId, ui::dim);
    addAndMakeVisible (updateLabel);
    addAndMakeVisible (checkButton);
    addAndMakeVisible (autoCheck);
    checkButton.onClick = [this] { updater.checkNow(); };
    ui::styleBig (installButton, true);
    installButton.onClick = [this] { if (installUpdate) installUpdate(); };
    addChildComponent (installButton);
    addAndMakeVisible (simulationToggle);
    simulationToggle.setColour (juce::ToggleButton::textColourId, px::yellow);
    simulationToggle.onClick = [this]
    {
        if (setSimulation)
            setSimulation (simulationToggle.getToggleState());
        refreshSettings();
    };
    for (auto* b : { &measureButton, &loadButton, &forgetButton })
        addAndMakeVisible (b);
    measureButton.setColour (juce::TextButton::textColourOffId, px::yellow);
    measureButton.onClick = [this] { if (openMeasure) openMeasure(); };
    loadButton.onClick = [this] { if (loadMeasures) loadMeasures(); };
    forgetButton.onClick = [this] { if (forgetMeasures) forgetMeasures(); refreshSettings(); };
    measuresLabel.setColour (juce::Label::textColourId, ui::dim);
    addAndMakeVisible (measuresLabel);
    autoCheck.onClick = [this] { updater.setAutoCheck (autoCheck.getToggleState()); };
    updater.changes.addChangeListener (this);
    refreshUpdate();

    intro.setText ("Outils de test. Tu n'en as pas besoin pour jouer : ils servent à compléter la carte du protocole."_fr,
                   juce::dontSendNotification);
    intro.setColour (juce::Label::textColourId, ui::dim);
    addAndMakeVisible (intro);

    idLabel.setText ("Sysex ID", juce::dontSendNotification);
    channelLabel.setText ("Canal", juce::dontSendNotification);
    baseLabel.setText ("Note du son 1", juce::dontSendNotification);
    for (auto* l : { &idLabel, &channelLabel, &baseLabel })
    {
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    for (int i = 0; i <= 127; ++i)
        idBox.addItem (i == 127 ? juce::String ("127 (tous)") : juce::String (i), i + 1);
    for (int i = 1; i <= 16; ++i)
        channelBox.addItem (juce::String (i), i);
    baseNote.setRange (0, 127, 1);
    for (auto* c : std::initializer_list<juce::Component*> { &idBox, &channelBox, &baseNote, &baseName })
        addAndMakeVisible (c);

    idBox.onChange = [this] { engine.deviceId = idBox.getSelectedId() - 1; if (settingsChanged) settingsChanged(); };
    channelBox.onChange = [this] { engine.channel = channelBox.getSelectedId(); if (settingsChanged) settingsChanged(); };
    baseNote.onValueChange = [this]
    {
        engine.baseNote = (int) baseNote.getValue();
        baseName.setText (juce::MidiMessage::getMidiNoteName ((int) baseNote.getValue(), true, true, 3), juce::dontSendNotification);
        if (settingsChanged) settingsChanged();
    };

    auto colour = ui::panel;
    tabs.addTab ("Lire", colour, new ReadPage (engine), true);
    auto* panel = new PanelPage (engine);
    mapView = &panel->view;
    tabs.addTab ("Carte", colour, panel, true);
    tabs.addTab ("Patterns", colour, new PatternPage (engine), true);
    tabs.addTab ("Jeu", colour, new PlayPage (engine), true);
    tabs.addTab ("CC", colour, new CcPage (engine), true);
    tabs.addTab ("Envoi libre", colour, new FreePage (engine), true);
    tabs.setTabBarDepth (32);
    addAndMakeVisible (tabs);

    engine.addListener (this);
    refreshSettings();
    startTimer (300);
}

ToolsPage::~ToolsPage()
{
    updater.changes.removeChangeListener (this);
    stopTimer();
    engine.removeListener (this);
}

void ToolsPage::refreshSettings()
{
    simulationToggle.setToggleState (isSimulation && isSimulation(), juce::dontSendNotification);
    measuresLabel.setText ("Le simulateur suit : "_fr + (measuresSource ? measuresSource() : juce::String()), juce::dontSendNotification);
    idBox.setSelectedId (engine.deviceId.load() + 1, juce::dontSendNotification);
    channelBox.setSelectedId (engine.channel.load(), juce::dontSendNotification);
    baseNote.setValue (engine.baseNote.load(), juce::dontSendNotification);
    baseName.setText (juce::MidiMessage::getMidiNoteName (engine.baseNote.load(), true, true, 3), juce::dontSendNotification);
}

void ToolsPage::refreshUpdate()
{
    updateLabel.setText ("RackAttack Editor " + Updater::currentVersion() + "   ·   "_fr + updater.getStatusText(), juce::dontSendNotification);
    autoCheck.setToggleState (updater.getAutoCheck(), juce::dontSendNotification);
    installButton.setVisible (updater.getState() == Updater::State::Available);
    installButton.setButtonText ("Installer la "_fr + updater.getRelease().version);
    resized();
    checkButton.setEnabled (updater.getState() != Updater::State::Disabled && updater.getState() != Updater::State::Checking
                            && updater.getState() != Updater::State::Installing);
}

void ToolsPage::timerCallback()
{
    if (mapDirty && mapView != nullptr && isShowing())
    {
        mapDirty = false;
        auto& map = engine.getAddressMap();
        mapView->setText (map.empty() ? "(rien reçu pour l'instant : tourne un potard de la machine)"_fr
                                      : juce::String (map.table()));
    }
}

void ToolsPage::paint (juce::Graphics& g)   { g.fillAll (ui::background); }

void ToolsPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    idLabel.setBounds (top.removeFromLeft (70));        top.removeFromLeft (6);
    idBox.setBounds (top.removeFromLeft (100));         top.removeFromLeft (16);
    channelLabel.setBounds (top.removeFromLeft (50));   top.removeFromLeft (6);
    channelBox.setBounds (top.removeFromLeft (64));     top.removeFromLeft (16);
    baseLabel.setBounds (top.removeFromLeft (100));     top.removeFromLeft (6);
    baseNote.setBounds (top.removeFromLeft (100));      top.removeFromLeft (6);
    baseName.setBounds (top.removeFromLeft (50));
    r.removeFromTop (8);
    auto up = r.removeFromTop (28);
    checkButton.setBounds (up.removeFromLeft (200));
    up.removeFromLeft (10);
    autoCheck.setBounds (up.removeFromLeft (290));
    if (installButton.isVisible())
    {
        up.removeFromLeft (10);
        installButton.setBounds (up.removeFromLeft (180));
    }
    up.removeFromLeft (10);
    updateLabel.setBounds (up);
    r.removeFromTop (6);
    simulationToggle.setBounds (r.removeFromTop (28).removeFromLeft (620));
    r.removeFromTop (6);
    auto sim = r.removeFromTop (30);
    measureButton.setBounds (sim.removeFromLeft (240));
    sim.removeFromLeft (10);
    loadButton.setBounds (sim.removeFromLeft (220));
    sim.removeFromLeft (10);
    forgetButton.setBounds (sim.removeFromLeft (170));
    sim.removeFromLeft (12);
    measuresLabel.setBounds (sim);
    r.removeFromTop (6);
    intro.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    tabs.setBounds (r);
}
