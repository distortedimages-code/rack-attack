// "Outils": the test tools of the Labo (raw reads, the map of addresses seen,
// pattern probes, rhythm and parameter bursts, CC sweep, free SysEx) and the
// MIDI settings (Sysex ID, channel, note of sound 1), and the updates.

#pragma once

#include <JuceHeader.h>

#include "../plugin/Updater.h"
#include "Engine.h"
#include "Widgets.h"

class ToolsPage : public juce::Component,
                  private Engine::Listener,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    ToolsPage (Engine&, Updater&, std::function<void()> settingsChanged);
    ~ToolsPage() override;

    void refreshSettings();   // shows the engine's ID, channel and note of sound 1

    std::function<bool()> isSimulation;
    std::function<void (bool)> setSimulation;
    std::function<void()> openMeasure;                      // the measuring assistant
    std::function<void()> installUpdate;                    // the update dialog
    std::function<void()> loadMeasures, forgetMeasures;     // file chooser / back to the manual
    std::function<juce::String()> measuresSource;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void addressMapChanged() override   { mapDirty = true; }
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refreshUpdate(); }
    void timerCallback() override;
    void refreshUpdate();

    Engine& engine;
    Updater& updater;
    std::function<void()> settingsChanged;

    juce::Label updateLabel;
    juce::TextButton checkButton { "Vérifier les mises à jour"_fr }, installButton { "Installer" };
    juce::ToggleButton autoCheck { "Vérifier une fois par jour"_fr };
    juce::ToggleButton simulationToggle { "Mode simulation : le RackAttack en logiciel, sans la machine" };
    juce::TextButton measureButton { "Mesurer mon RackAttack..." }, loadButton { "Charger des mesures..." },
                     forgetButton { "Revenir au manuel" };
    juce::Label measuresLabel;

    juce::Label idLabel, channelLabel, baseLabel, baseName, intro;
    juce::ComboBox idBox, channelBox;
    juce::Slider baseNote { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextEditor* mapView = nullptr;
    bool mapDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolsPage)
};
