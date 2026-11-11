#include "ConnectWizard.h"

ConnectWizard::ConnectWizard (Engine& e, Host& h) : engine (e), host (h)
{
    stepLabel.setFont (ui::font (14.0f));
    stepLabel.setColour (juce::Label::textColourId, ui::accent);
    title.setFont (ui::font (28.0f, true));
    body.setFont (ui::font (18.0f));
    body.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &stepLabel, &title, &body })
        addAndMakeVisible (l);

    for (auto* b : { &back, &next, &later })
        addAndMakeVisible (b);
    ui::styleBig (next, true);
    back.onClick = [this] { showStep (step - 1); };
    next.onClick = [this]
    {
        if (step < 2)
            showStep (step + 1);
        else
            host.wizardFinished (connected);
    };
    later.onClick = [this] { host.wizardFinished (connected); };

    // 1. ports
    inLabel.setText ("Entrée MIDI (ce qui vient du RackAttack)"_fr, juce::dontSendNotification);
    outLabel.setText ("Sortie MIDI (ce qui va vers le RackAttack)", juce::dontSendNotification);
    for (auto* l : { &inLabel, &outLabel })
    {
        l->setFont (ui::font (15.0f));
        l->setColour (juce::Label::textColourId, ui::dim);
        addChildComponent (l);
    }
    for (auto* b : { &inBox, &outBox })
        addChildComponent (b);
    addChildComponent (refresh);
    refresh.onClick = [this] { fillPorts(); };
    inBox.onChange = [this]
    {
        int i = inBox.getSelectedId() - 2;
        host.chooseInput (i >= 0 && i < inIds.size() ? inIds[i] : juce::String());
        updateNext();
    };
    outBox.onChange = [this]
    {
        int i = outBox.getSelectedId() - 2;
        host.chooseOutput (i >= 0 && i < outIds.size() ? outIds[i] : juce::String());
        updateNext();
    };

    // 2. settings
    ctlDone.setButtonText ("Global/Utility, page « Ctl. send | receive » : Ctl. send sur SysEx, receive sur On"_fr);
    prgDone.setButtonText ("Page « Tx PrgChg | Rx PrgChg » : Rx PrgChg sur On"_fr);
    for (auto* t : { &ctlDone, &prgDone })
    {
        t->setColour (juce::ToggleButton::textColourId, ui::text);
        t->onClick = [this] { updateNext(); };
        addChildComponent (t);
    }

    // 3. connecting
    result.setFont (ui::font (22.0f, true));
    addChildComponent (result);
    help.setFont (ui::font (16.0f));
    help.setJustificationType (juce::Justification::topLeft);
    help.setColour (juce::Label::textColourId, ui::dim);
    addChildComponent (help);
    bar.setColour (juce::ProgressBar::foregroundColourId, ui::accent);
    addChildComponent (bar);
    addChildComponent (retry);
    retry.onClick = [this] { showStep (2); };

    engine.addListener (this);
}

ConnectWizard::~ConnectWizard()   { engine.removeListener (this); }

void ConnectWizard::start()
{
    showStep (0);
}

void ConnectWizard::fillPorts()
{
    auto fill = [] (juce::ComboBox& box, juce::StringArray& ids, const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& keep)
    {
        box.clear (juce::dontSendNotification);
        ids.clear();
        box.addItem ("Choisir…"_fr, 1);
        int selected = 1;
        for (auto& d : devices)
        {
            ids.add (d.identifier);
            box.addItem (d.name, ids.size() + 1);
            if (d.identifier == keep)
                selected = ids.size() + 1;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    };
    fill (inBox, inIds, juce::MidiInput::getAvailableDevices(), host.currentInput());
    fill (outBox, outIds, juce::MidiOutput::getAvailableDevices(), host.currentOutput());
    updateNext();
}

void ConnectWizard::updateNext()
{
    bool ready = true;
    if (step == 0) ready = inBox.getSelectedId() > 1 && outBox.getSelectedId() > 1;
    if (step == 1) ready = true;   // the ticks are a help, not a lock
    if (step == 2) ready = connected;
    next.setEnabled (ready);
    next.setButtonText (step == 2 ? juce::String ("Commencer") : juce::String ("Suivant"));
    ui::styleBig (next, step != 1 || (ctlDone.getToggleState() && prgDone.getToggleState()));
    repaint();
}

void ConnectWizard::showStep (int s)
{
    step = juce::jlimit (0, 2, s);
    stepLabel.setText ("BRANCHER LE RACKATTACK   ·   "_fr + juce::String (step + 1) + " / 3", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &inLabel, &outLabel, &inBox, &outBox, &refresh })
        c->setVisible (step == 0);
    ctlDone.setVisible (step == 1);
    prgDone.setVisible (step == 1);
    for (auto* c : std::initializer_list<juce::Component*> { &result, &help, &bar, &retry })
        c->setVisible (false);
    back.setVisible (step > 0);

    if (step == 0)
    {
        title.setText ("Les câbles MIDI"_fr, juce::dontSendNotification);
        body.setText ("Relie le MIDI OUT du RackAttack à l'entrée de ton interface MIDI, et son MIDI IN à la sortie. "_fr
                      "Puis choisis-les ici."_fr, juce::dontSendNotification);
        fillPorts();
    }
    else if (step == 1)
    {
        title.setText ("Deux réglages sur la machine"_fr, juce::dontSendNotification);
        body.setText ("Ils permettent à l'appli de suivre les potards du RackAttack et de changer de programme. "_fr
                      "Coche quand c'est fait."_fr, juce::dontSendNotification);
    }
    else
    {
        title.setText ("Connexion", juce::dontSendNotification);
        body.setText ({}, juce::dontSendNotification);
        connected = false;
        progress = 0.0;
        result.setVisible (true);
        bar.setVisible (true);
        result.show ("Je cherche le RackAttack…"_fr, ui::text);
        engine.connect();
    }
    updateNext();
    resized();
}

void ConnectWizard::connectionChanged (Engine::Connection c)
{
    if (step == 2 && c == Engine::Connection::connected)
        result.show ("RackAttack trouvé. Je lis le kit en cours…"_fr, ui::text);
}

void ConnectWizard::progressChanged (int done, int total, const juce::String&)
{
    if (step == 2 && total > 0)
        progress = (double) done / total;
}

void ConnectWizard::taskFinished (const juce::String& task, bool ok, const juce::String& message)
{
    if (step != 2 || task != "connect")
        return;

    connected = engine.getConnection() == Engine::Connection::connected;
    progress = ok ? 1.0 : progress;

    if (connected)
    {
        result.show (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93  ")) + message, ui::accent);
        help.setText (ok ? "Tout est prêt."_fr : juce::String(), juce::dontSendNotification);
    }
    else
    {
        result.show (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x97  ")) + message, ui::bad);
        help.setText ("À vérifier :\n"_fr
                      "  •  le RackAttack est allumé\n"_fr
                      "  •  les deux câbles MIDI (OUT vers l'entrée, IN vers la sortie)\n"_fr
                      "  •  les ports choisis à l'étape 1"_fr, juce::dontSendNotification);
        retry.setVisible (true);
    }
    help.setVisible (true);
    updateNext();
    resized();
}

void ConnectWizard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.85f));
    g.setColour (ui::panel);
    g.fillRect (cardArea);
    px::frameRect (g, cardArea, px::white, 3);

    // three dots
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i <= step ? ui::accent : ui::panelLight);
        g.fillRect (cardArea.getRight() - 90 + i * 22, cardArea.getY() + 30, 12, 12);
    }
}

void ConnectWizard::resized()
{
    cardArea = getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 40), juce::jmin (500, getHeight() - 40));
    auto r = cardArea.reduced (36, 24);

    stepLabel.setBounds (r.removeFromTop (24));
    title.setBounds (r.removeFromTop (44));
    r.removeFromTop (6);

    auto buttons = r.removeFromBottom (44);
    next.setBounds (buttons.removeFromRight (180));
    buttons.removeFromRight (10);
    back.setBounds (buttons.removeFromLeft (120));
    later.setBounds (buttons.removeFromRight (120));
    r.removeFromBottom (12);

    if (step == 0)
    {
        body.setBounds (r.removeFromTop (60));
        r.removeFromTop (10);
        inLabel.setBounds (r.removeFromTop (22));
        inBox.setBounds (r.removeFromTop (36));
        r.removeFromTop (14);
        outLabel.setBounds (r.removeFromTop (22));
        outBox.setBounds (r.removeFromTop (36));
        r.removeFromTop (14);
        refresh.setBounds (r.removeFromTop (30).removeFromLeft (130));
    }
    else if (step == 1)
    {
        body.setBounds (r.removeFromTop (60));
        r.removeFromTop (16);
        ctlDone.setBounds (r.removeFromTop (44));
        r.removeFromTop (8);
        prgDone.setBounds (r.removeFromTop (44));
    }
    else
    {
        body.setBounds ({});
        result.setBounds (r.removeFromTop (40));
        r.removeFromTop (10);
        bar.setBounds (r.removeFromTop (16));
        r.removeFromTop (16);
        auto retryRow = r.removeFromBottom (36);
        retry.setBounds (retryRow.removeFromLeft (140));
        help.setBounds (r);
    }
}
