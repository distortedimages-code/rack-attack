#include "LaboComponent.h"
#include "Text.h"

namespace
{
    const juce::Colour kBackground (0xff1c1f1d);
    const juce::Colour kPanel (0xff262a27);
    const juce::Colour kAccent (0xff7fbf3f);    // RackAttack green
    const juce::Colour kText (0xffe8ece6);

    juce::String twoDigits (int n)   { return juce::String (n).paddedLeft ('0', 2); }

    void fillSounds (juce::ComboBox& box)
    {
        for (int i = 0; i < 24; ++i)
            box.addItem ("Son " + twoDigits (i + 1), i + 1);
        box.setSelectedId (1, juce::dontSendNotification);
    }

    // Lays rows out from the top of an area.
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
        void paint (juce::Graphics& g) override   { g.fillAll (kPanel); }

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
    const char* kGuide =
        u8"RackAttack Labo enregistre ce que ton RackAttack répond, pour compléter la carte de son protocole "
        u8"(docs/SYSEX.md du dépôt).\n\n"
        u8"Il ne touche jamais à la mémoire de la machine : il demande des dumps, change des réglages du programme "
        u8"en cours (jamais sauvegardés) et renvoie des sons tels qu'il les a lus. Seul l'onglet Envoi libre peut tout envoyer.\n\n"
        u8"Tout est enregistré dans Documents / RackAttack Labo. À la fin, clique « Préparer le zip » et envoie-moi le fichier dans la conversation.\n\n"
        u8"AVANT DE COMMENCER, sur le RackAttack (menu Global) :\n"
        u8"  • Sysex ID = 0, Ctl Send = SysEx, Ctl Receive = On, Rx PrgChange = On.\n"
        u8"  • Écris la version du logiciel (Software) et le canal global (Global Chn) dans le champ Notes, puis « Noter ».\n"
        u8"En haut de la fenêtre : l'entrée et la sortie MIDI reliées au RackAttack, l'ID 0 et le même canal que Global Chn.\n\n"
        u8"1. LIRE (tests T2, T3)\n"
        u8"   « Tout lire ». Le journal doit montrer GLBD, PRGD, 24 SNDD et 4 EFXD, avec « checksum OK ».\n\n"
        u8"2. FAÇADE (T4, T5)\n"
        u8"   Tourne les potards de la machine, une section après l'autre, du minimum au maximum. Chaque réglage "
        u8"apparaît dans la carte. Pour chaque ligne « ??? », écris dans Notes ce que tu viens de tourner "
        u8"(par exemple « Pattern : Length »), puis « Noter ». Pareil pour les réglages de programme, d'effet et globaux.\n\n"
        u8"3. PATTERNS (T6)\n"
        u8"   Sur la machine, son 1 : un pattern avec seulement le pas 1 en Vel. 1. Note-le, puis « Essayer les demandes de pattern ». "
        u8"Recommence avec : le pas 2 en Vel. 2 ; le pas 1 en Vel. 1 et le pas 3 en Vel. 3 ; une longueur de 17 ; une longueur de 128. "
        u8"Note à chaque fois ce que tu as fait. Puis une fois « Sondes d'emplacement ».\n\n"
        u8"4. JEU (T7, T8, T13)\n"
        u8"   Choisis un son qui dure un peu et lance le rythme. Puis la rafale à 25, 50, 100, 200 et 260 messages par seconde, "
        u8"une dizaine de secondes chacune. Note ce que tu entends : le filtre bouge-t-il en continu ? des notes sautent ? du retard ?\n"
        u8"   Ensuite « Renvoyer le son lu » en boucle (500 puis 250 ms) pendant que ça joue, et note.\n\n"
        u8"5. CC (T9)\n"
        u8"   Passe le programme en mode MIDI Plug-in sur la machine, lance le balayage des CC 12 à 59 et note ceux qui changent "
        u8"quelque chose (le journal donne l'heure de chaque CC). Remets le mode Native après.\n\n"
        u8"6. 50 PROGRAMMES (T11, T12)\n"
        u8"   « Lire les 50 programmes » (2 à 3 minutes). C'est aussi une sauvegarde complète de tes kits.\n\n"
        u8"7. « Préparer le zip » et envoie-le moi.";

    struct GuidePage : public Page
    {
        explicit GuidePage (Engine& e) : Page (e)
        {
            view.setMultiLine (true);
            view.setReadOnly (true);
            view.setScrollbarsShown (true);
            view.setCaretVisible (false);
            view.setFont (juce::Font (juce::FontOptions (14.0f)));
            view.setText (juce::String::fromUTF8 (kGuide));
            addAndMakeVisible (view);
        }
        void resized() override   { view.setBounds (getLocalBounds().reduced (8)); }
        juce::TextEditor view;
    };

    //==========================================================================
    struct ReadPage : public Page
    {
        explicit ReadPage (Engine& e) : Page (e)
        {
            text (intro, "Demande des dumps au RackAttack. Chaque réponse est enregistrée dans le dossier recus/ de la session."_fr);
            for (auto* b : { &all, &globals, &program, &sounds, &effects, &programs, &stop })
                addAndMakeVisible (b);
            all.onClick      = [this] { engine.readAll(); };
            globals.onClick  = [this] { engine.readGlobals(); };
            program.onClick  = [this] { engine.readProgram(); };
            sounds.onClick   = [this] { engine.readSounds(); };
            effects.onClick  = [this] { engine.readEffects(); };
            programs.onClick = [this] { engine.readAllPrograms(); };
            stop.onClick     = [this] { engine.cancelReads(); };
            text (programsNote, "Lire les 50 programmes : passe par Program Change 1 à 50 (Rx PrgChange doit être sur On), "_fr
                                "lit tout à chaque fois, puis revient au programme de départ. 2 à 3 minutes."_fr);
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (40));
            all.setBounds (r.next (34).withWidth (220));
            auto row = r.next();
            for (auto* b : { &globals, &program, &sounds, &effects })
                b->setBounds (row.removeFromLeft (110)), row.removeFromLeft (8);
            r.next (10);
            programs.setBounds (r.next (34).withWidth (220));
            programsNote.setBounds (r.next (56));
            stop.setBounds (r.next().withWidth (220));
        }

        juce::Label intro, programsNote;
        juce::TextButton all { "Tout lire" }, globals { "Globaux" }, program { "Programme" },
                         sounds { "24 sons" }, effects { "4 effets" },
                         programs { "Lire les 50 programmes" }, stop { "Arrêter la lecture"_fr };
    };

    //==========================================================================
    struct PanelPage : public Page
    {
        explicit PanelPage (Engine& e) : Page (e)
        {
            text (intro, "Avec Ctl Send = SysEx, chaque potard tourné sur la machine arrive ici. "_fr
                         "« ??? » = adresse encore inconnue : note ce que tu as tourné."_fr);
            view.setMultiLine (true);
            view.setReadOnly (true);
            view.setCaretVisible (false);
            view.setFont (LaboComponent::mono (12.5f));
            addAndMakeVisible (view);
            addAndMakeVisible (clear);
            clear.onClick = [this] { engine.clearAddressMap(); };
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (40));
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
            text (intro, u8"Le format des pas d'un pattern est inconnu. Ces boutons envoient plusieurs formes possibles de la "
                         "demande de pattern (PATR) et enregistrent toutes les réponses. Ce ne sont que des demandes : rien n'est modifié."_fr);
            fillSounds (sound);
            addAndMakeVisible (sound);
            addAndMakeVisible (probe);
            addAndMakeVisible (locations);
            probe.onClick = [this] { engine.probePatterns (sound.getSelectedId() - 1); };
            locations.onClick = [this] { engine.probeLocations(); };
            text (steps, u8"Sur la machine, pour le son choisi :\n"
                         u8"  1. seulement le pas 1 en Vel. 1\n"
                         u8"  2. seulement le pas 2 en Vel. 2\n"
                         u8"  3. pas 1 en Vel. 1 + pas 3 en Vel. 3\n"
                         u8"  4. longueur 17\n"
                         u8"  5. longueur 128\n"
                         "Après chaque changement : écris-le dans Notes, « Noter », puis « Essayer les demandes de pattern »."_fr);
            text (locNote, "Sondes d'emplacement : cherche comment demander un programme ou un son rangé en mémoire "_fr
                           u8"sans passer par Program Change.");
        }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            intro.setBounds (r.next (56));
            auto row = r.next();
            sound.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            probe.setBounds (row.removeFromLeft (260));
            steps.setBounds (r.next (130));
            locations.setBounds (r.next().withWidth (220));
            locNote.setBounds (r.next (40));
        }

        juce::Label intro, steps, locNote;
        juce::ComboBox sound;
        juce::TextButton probe { "Essayer les demandes de pattern" }, locations { "Sondes d'emplacement" };
    };

    //==========================================================================
    struct PlayPage : public Page
    {
        PlayPage (Engine& e, juce::Component& baseLabelRef, juce::Component& baseNoteRef, juce::Component& baseNameRef)
            : Page (e), baseLabel (baseLabelRef), baseNote (baseNoteRef), baseName (baseNameRef)
        {
            addAndMakeVisible (baseLabel);
            addAndMakeVisible (baseNote);
            addAndMakeVisible (baseName);
            fillSounds (sound);
            addAndMakeVisible (sound);
            addAndMakeVisible (listen);
            listen.onClick = [this] { engine.playSound (soundIndex()); };

            text (rhythmLabel, "Rythme de test");
            bpm.setRange (40.0, 240.0, 1.0);
            bpm.setValue (120.0, juce::dontSendNotification);
            bpm.setTextValueSuffix (" BPM");
            addAndMakeVisible (bpm);
            division.addItem (u8"croches", 2);
            division.addItem (u8"doubles", 4);
            division.setSelectedId (4, juce::dontSendNotification);
            addAndMakeVisible (division);
            rhythm.setClickingTogglesState (true);
            addAndMakeVisible (rhythm);
            auto updateRhythm = [this] { engine.setRhythm (rhythm.getToggleState(), bpm.getValue(), division.getSelectedId(), soundIndex()); };
            rhythm.onClick = updateRhythm;
            bpm.onDragEnd = [this, updateRhythm] { if (rhythm.getToggleState()) updateRhythm(); };
            division.onChange = [this, updateRhythm] { if (rhythm.getToggleState()) updateRhythm(); };

            text (burstLabel, u8"Rafale de changements (SNDP, valeur qui monte et descend)");
            for (int rate : { 0, 25, 50, 100, 150, 200, 260 })
                burst.addItem (rate == 0 ? juce::String ("Off") : juce::String (rate) + " messages/s", rate + 1);
            burst.setSelectedId (1, juce::dontSendNotification);
            addAndMakeVisible (burst);
            text (addressLabel, "Adresse (hex)");
            address.setText ("27");
            address.setInputRestrictions (4, "0123456789abcdefABCDEF");
            addAndMakeVisible (address);
            text (addressName, "Cutoff");
            auto updateBurst = [this]
            {
                int adr = address.getText().getHexValue32();
                auto* pn = ra::parameterName (ra::Object::sound, adr);
                addressName.setText (pn != nullptr ? pn : "???", juce::dontSendNotification);
                engine.setBurst (burst.getSelectedId() - 1, soundIndex(), adr);
            };
            burst.onChange = updateBurst;
            address.onReturnKey = updateBurst;
            address.onFocusLost = [this, updateBurst] { if (burst.getSelectedId() > 1) updateBurst(); };

            text (resendLabel, "Renvoyer le son tel qu'il a été lu (dump complet, 118 octets)"_fr);
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

            text (hint, "Les notes partent sur le canal du haut, à partir de la note du son 1 (mode MIDI Native). "_fr
                        "« Écouter » pour vérifier que c'est le bon son."_fr);
        }

        int soundIndex() const   { return sound.getSelectedId() - 1; }

        void resized() override
        {
            Rows r { getLocalBounds().reduced (12) };
            auto row = r.next();
            sound.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            listen.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (16);
            baseLabel.setBounds (row.removeFromLeft (100));
            row.removeFromLeft (6);
            baseNote.setBounds (row.removeFromLeft (100));
            row.removeFromLeft (6);
            baseName.setBounds (row.removeFromLeft (50));
            hint.setBounds (r.next (40));

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

        juce::Component& baseLabel;
        juce::Component& baseNote;
        juce::Component& baseName;
        juce::ComboBox sound, division, burst, resendLoop;
        juce::TextButton listen { "Écouter"_fr }, rhythm { "Rythme" }, resendOnce { "Renvoyer le son lu" },
                         stopAll { "Tout arrêter"_fr };
        juce::Slider bpm { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextEditor address;
        juce::Label hint, rhythmLabel, burstLabel, addressLabel, addressName, resendLabel;
    };

    //==========================================================================
    struct CcPage : public Page
    {
        explicit CcPage (Engine& e) : Page (e)
        {
            text (intro, "Le plugin Attack d'époque se pilotait par CC 12 à 59 sur les canaux 1 à 12. "_fr
                         "Si le RackAttack les comprend en mode Plug-in, les p-locks coûteraient 3 octets au lieu de 12.\n"_fr
                         "Pour chaque CC : valeur 0 + une note, puis 127 + une note, une demi-seconde d'écart."_fr);
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
        explicit FreePage (Engine& e, std::function<int()> deviceIdFn) : Page (e), deviceId (std::move (deviceIdFn))
        {
            text (warning, u8"Attention : ici tout part tel quel, y compris des commandes qui peuvent effacer le programme en cours "
                           "ou écrire en mémoire. À n'utiliser que si je te donne un message précis à tester."_fr);
            warning.setColour (juce::Label::textColourId, juce::Colour (0xffffb347));
            bytes.setFont (LaboComponent::mono (14.0f));
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
                auto msg = wrap.getToggleState() ? ra::build ((std::uint8_t) deviceId(), *parsed) : *parsed;
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

        std::function<int()> deviceId;
        juce::Label warning, result;
        juce::TextEditor bytes;
        juce::ToggleButton wrap { "Ajouter F0 3E 11 <ID> devant, le checksum et F7 derrière"_fr };
        juce::TextButton send { "Envoyer" };
    };
}

//==============================================================================
juce::Font LaboComponent::mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

LaboComponent::LaboComponent()
{
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.3f));
    lookAndFeel.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141614));
    lookAndFeel.setColour (juce::TextEditor::textColourId, kText);
    lookAndFeel.setColour (juce::Label::textColourId, kText);
    lookAndFeel.setColour (juce::Slider::thumbColourId, kAccent);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, kAccent);
    setLookAndFeel (&lookAndFeel);

    juce::PropertiesFile::Options options;
    options.applicationName = "RackAttack Labo";
    options.folderName = "RackAttack Labo";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (options);

    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    label (inLabel, "Entrée"_fr);
    label (outLabel, "Sortie");
    label (idLabel, "Sysex ID");
    label (channelLabel, "Canal");
    baseLabel.setText ("Note du son 1", juce::dontSendNotification);
    baseLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* b : { &inBox, &outBox, &idBox, &channelBox })
        addAndMakeVisible (b);
    for (int i = 0; i <= 127; ++i)
        idBox.addItem (i == 127 ? juce::String ("127 (tous)") : juce::String (i), i + 1);
    for (int i = 1; i <= 16; ++i)
        channelBox.addItem (juce::String (i), i);

    baseNote.setRange (0, 127, 1);
    addAndMakeVisible (refreshButton);

    inBox.onChange = [this]
    {
        int i = inBox.getSelectedId() - 2;
        auto err = engine.openInput (i >= 0 && i < inIds.size() ? inIds[i] : juce::String());
        if (err.isNotEmpty()) journalLine (Session::timeStamp() + "  " + err);
        saveSettings();
    };
    outBox.onChange = [this]
    {
        int i = outBox.getSelectedId() - 2;
        auto err = engine.openOutput (i >= 0 && i < outIds.size() ? outIds[i] : juce::String());
        if (err.isNotEmpty()) journalLine (Session::timeStamp() + "  " + err);
        saveSettings();
    };
    idBox.onChange = [this] { engine.deviceId = idBox.getSelectedId() - 1; saveSettings(); };
    channelBox.onChange = [this] { engine.channel = channelBox.getSelectedId(); saveSettings(); };
    baseNote.onValueChange = [this]
    {
        engine.baseNote = (int) baseNote.getValue();
        baseNoteName.setText (juce::MidiMessage::getMidiNoteName ((int) baseNote.getValue(), true, true, 3), juce::dontSendNotification);
        saveSettings();
    };
    refreshButton.onClick = [this] { refreshPorts(); };

    // Pages
    auto colour = kPanel;
    tabs.addTab ("Guide", colour, new GuidePage (engine), true);
    tabs.addTab ("Lire", colour, new ReadPage (engine), true);
    auto* panel = new PanelPage (engine);
    mapView = &panel->view;
    tabs.addTab ("Façade"_fr, colour, panel, true);
    tabs.addTab ("Patterns", colour, new PatternPage (engine), true);
    tabs.addTab ("Jeu", colour, new PlayPage (engine, baseLabel, baseNote, baseNoteName), true);
    tabs.addTab ("CC", colour, new CcPage (engine), true);
    tabs.addTab ("Envoi libre", colour, new FreePage (engine, [this] { return engine.deviceId.load(); }), true);
    tabs.setTabBarDepth (32);
    addAndMakeVisible (tabs);

    statusLabel.setFont (mono (12.0f));
    addAndMakeVisible (statusLabel);

    journal.setMultiLine (true);
    journal.setReadOnly (true);
    journal.setCaretVisible (false);
    journal.setScrollbarsShown (true);
    journal.setFont (mono (12.0f));
    addAndMakeVisible (journal);

    noteField.setTextToShowWhenEmpty ("Notes : ce que tu as fait, ce que tu entends… puis Entrée ou « Noter »"_fr, juce::Colours::grey);
    noteField.onReturnKey = [this] { engine.userNote (noteField.getText()); noteField.clear(); };
    addAndMakeVisible (noteField);
    noteButton.onClick = [this] { engine.userNote (noteField.getText()); noteField.clear(); };
    folderButton.onClick = [this] { session.getFolder().startAsProcess(); };
    zipButton.onClick = [this]
    {
        session.saveText ("carte.txt", engine.getAddressMap().table());
        auto zip = session.makeZip();
        if (zip == juce::File())
            journalLine (Session::timeStamp() + "  Le zip n'a pas pu être créé."_fr);
        else
        {
            engine.userNote ("zip préparé : "_fr + zip.getFileName());
            zip.revealToUser();
        }
    };
    clearButton.onClick = [this] { journal.clear(); };
    for (auto* b : { &noteButton, &folderButton, &zipButton, &clearButton })
        addAndMakeVisible (b);
    zipButton.setColour (juce::TextButton::buttonColourId, kAccent.darker (0.5f));

    engine.setListener (this);
    applySettings();

    engine.userNote (juce::String ("RackAttack Labo ") + RA_VERSION + " sur " + juce::SystemStats::getOperatingSystemName()
                     + " · session : "_fr + session.getFolder().getFullPathName());

    setSize (1240, 800);
    startTimer (300);
}

LaboComponent::~LaboComponent()
{
    stopTimer();
    engine.stopPlayer();
    engine.setListener (nullptr);
    setLookAndFeel (nullptr);
}

void LaboComponent::refreshPorts()
{
    auto fill = [] (juce::ComboBox& box, juce::StringArray& ids, const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& keep)
    {
        box.clear (juce::dontSendNotification);
        ids.clear();
        box.addItem ("— aucune —"_fr, 1);
        int selected = 1;
        for (auto& d : devices)
        {
            ids.add (d.identifier);
            box.addItem (d.name, ids.size() + 1);
            if (d.identifier == keep)
                selected = ids.size() + 1;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
        return selected;
    };

    auto keepIn = settings->getValue ("in"), keepOut = settings->getValue ("out");
    if (fill (inBox, inIds, juce::MidiInput::getAvailableDevices(), keepIn) > 1)
        engine.openInput (keepIn);
    if (fill (outBox, outIds, juce::MidiOutput::getAvailableDevices(), keepOut) > 1)
        engine.openOutput (keepOut);
}

void LaboComponent::applySettings()
{
    idBox.setSelectedId (settings->getIntValue ("id", 0) + 1, juce::dontSendNotification);
    channelBox.setSelectedId (settings->getIntValue ("channel", 1), juce::dontSendNotification);
    baseNote.setValue (settings->getIntValue ("baseNote", 36), juce::dontSendNotification);
    engine.deviceId = idBox.getSelectedId() - 1;
    engine.channel = channelBox.getSelectedId();
    engine.baseNote = (int) baseNote.getValue();
    baseNoteName.setText (juce::MidiMessage::getMidiNoteName ((int) baseNote.getValue(), true, true, 3), juce::dontSendNotification);
    refreshPorts();
}

void LaboComponent::saveSettings()
{
    auto in = inBox.getSelectedId() - 2, out = outBox.getSelectedId() - 2;
    settings->setValue ("in", in >= 0 && in < inIds.size() ? inIds[in] : juce::String());
    settings->setValue ("out", out >= 0 && out < outIds.size() ? outIds[out] : juce::String());
    settings->setValue ("id", idBox.getSelectedId() - 1);
    settings->setValue ("channel", channelBox.getSelectedId());
    settings->setValue ("baseNote", (int) baseNote.getValue());
    settings->saveIfNeeded();
}

//==============================================================================
void LaboComponent::journalLine (const juce::String& line)
{
    if (journal.getTotalNumChars() > 600000)
    {
        auto keep = journal.getText().getLastCharacters (300000);
        journal.setText ("… (début du journal dans journal.txt)\n"_fr + keep.fromFirstOccurrenceOf ("\n", false, false));
    }
    journal.moveCaretToEnd();
    journal.insertTextAtCaret (line + "\n");
}

void LaboComponent::timerCallback()
{
    if (mapDirty && mapView != nullptr)
    {
        mapDirty = false;
        auto& map = engine.getAddressMap();
        mapView->setText (map.empty() ? juce::String ("(rien reçu pour l'instant : tourne un potard de la machine)"_fr)
                                      : juce::String (map.table()));
    }
}

void LaboComponent::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kAccent);
    g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    g.drawText ("RackAttack Labo", 12, 0, 200, 52, juce::Justification::centredLeft);
}

void LaboComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (52).reduced (8, 12);
    top.removeFromLeft (170);
    inLabel.setBounds (top.removeFromLeft (52));       top.removeFromLeft (6);
    inBox.setBounds (top.removeFromLeft (220));        top.removeFromLeft (12);
    outLabel.setBounds (top.removeFromLeft (52));      top.removeFromLeft (6);
    outBox.setBounds (top.removeFromLeft (220));       top.removeFromLeft (8);
    refreshButton.setBounds (top.removeFromLeft (96)); top.removeFromLeft (16);
    idLabel.setBounds (top.removeFromLeft (64));       top.removeFromLeft (6);
    idBox.setBounds (top.removeFromLeft (100));        top.removeFromLeft (12);
    channelLabel.setBounds (top.removeFromLeft (50));  top.removeFromLeft (6);
    channelBox.setBounds (top.removeFromLeft (64));

    r.reduce (8, 0);
    r.removeFromBottom (8);
    auto left = r.removeFromLeft (juce::jmax (480, r.getWidth() / 2 - 40));
    r.removeFromLeft (8);
    statusLabel.setBounds (left.removeFromBottom (24));
    tabs.setBounds (left);

    auto files = r.removeFromBottom (32);
    clearButton.setBounds (files.removeFromLeft (160));
    zipButton.setBounds (files.removeFromRight (160));     files.removeFromRight (6);
    folderButton.setBounds (files.removeFromRight (160));
    r.removeFromBottom (6);
    auto notes = r.removeFromBottom (32);
    noteButton.setBounds (notes.removeFromRight (90));     notes.removeFromRight (6);
    noteField.setBounds (notes);
    r.removeFromBottom (8);
    journal.setBounds (r);
}
