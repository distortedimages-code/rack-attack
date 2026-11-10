// "Outils": the test tools of the Labo (raw reads, the map of addresses seen,
// pattern probes, rhythm and parameter bursts, CC sweep, free SysEx) and the
// MIDI settings (Sysex ID, channel, note of sound 1).

#pragma once

#include <JuceHeader.h>

#include "Engine.h"
#include "Session.h"
#include "Widgets.h"

class ToolsPage : public juce::Component,
                  private Engine::Listener,
                  private juce::Timer
{
public:
    ToolsPage (Engine&, Session&, std::function<void()> settingsChanged);
    ~ToolsPage() override;

    void refreshSettings();   // shows the engine's ID, channel and note of sound 1

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void addressMapChanged() override   { mapDirty = true; }
    void timerCallback() override;

    Engine& engine;
    Session& session;
    std::function<void()> settingsChanged;

    juce::Label idLabel, channelLabel, baseLabel, baseName, intro;
    juce::ComboBox idBox, channelBox;
    juce::Slider baseNote { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextEditor* mapView = nullptr;
    bool mapDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolsPage)
};
