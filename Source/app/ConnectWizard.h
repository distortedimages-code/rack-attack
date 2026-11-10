// "Brancher le RackAttack": three screens over the window. 1. the MIDI ports,
// 2. two settings on the unit, 3. finding it and reading the kit.

#pragma once

#include <JuceHeader.h>

#include "Engine.h"
#include "Widgets.h"

class ConnectWizard : public juce::Component,
                      private Engine::Listener
{
public:
    struct Host
    {
        virtual ~Host() = default;
        virtual juce::String currentInput() = 0;
        virtual juce::String currentOutput() = 0;
        virtual void chooseInput (const juce::String& identifier) = 0;
        virtual void chooseOutput (const juce::String& identifier) = 0;
        virtual void wizardFinished (bool connected) = 0;
    };

    ConnectWizard (Engine&, Host&);
    ~ConnectWizard() override;

    void start();   // back to screen 1

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // the window behind stays untouchable

private:
    void connectionChanged (Engine::Connection) override;
    void progressChanged (int done, int total, const juce::String& what) override;
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;

    void showStep (int step);
    void fillPorts();
    void updateNext();

    Engine& engine;
    Host& host;
    int step = 0;
    bool connected = false;
    juce::Rectangle<int> cardArea;

    juce::Label stepLabel, title, body;
    juce::TextButton back { "Retour" }, next { "Suivant" }, later { "Plus tard" };

    // 1. ports
    juce::Label inLabel, outLabel;
    juce::ComboBox inBox, outBox;
    juce::TextButton refresh { "Rafraîchir"_fr };
    juce::StringArray inIds, outIds;

    // 2. settings on the unit
    juce::ToggleButton ctlDone, prgDone;

    // 3. connecting
    ui::StatusLine result;
    juce::Label help;
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    juce::TextButton retry { "Réessayer"_fr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectWizard)
};
