// Main window content of RackAttack Labo: MIDI ports at the top, test pages on
// the left, the journal on the right.

#pragma once

#include <JuceHeader.h>

#include "Engine.h"
#include "Session.h"
#include "Text.h"

class LaboComponent : public juce::Component,
                      private Engine::Listener,
                      private juce::Timer
{
public:
    LaboComponent();
    ~LaboComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Font mono (float size = 13.0f);

private:
    void journalLine (const juce::String& line) override;
    void addressMapChanged() override   { mapDirty = true; }
    void statusChanged (const juce::String& s) override   { statusLabel.setText (s, juce::dontSendNotification); }
    void timerCallback() override;

    void refreshPorts();
    void applySettings();
    void saveSettings();

    juce::LookAndFeel_V4 lookAndFeel;
    Session session;
    Engine engine { session };
    std::unique_ptr<juce::PropertiesFile> settings;

    juce::Label inLabel, outLabel, idLabel, channelLabel, baseLabel, statusLabel;
    juce::ComboBox inBox, outBox, idBox, channelBox;
    juce::Slider baseNote { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label baseNoteName;
    juce::TextButton refreshButton { "Rafraîchir"_fr };
    juce::StringArray inIds, outIds;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    juce::TextEditor journal, noteField;
    juce::TextButton noteButton { "Noter" }, folderButton { "Ouvrir le dossier" },
                     zipButton { "Préparer le zip"_fr }, clearButton { "Effacer l'affichage" };

    juce::TextEditor* mapView = nullptr;   // lives in the Façade page
    bool mapDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaboComponent)
};
