// Everything one instance of the plug-in (or the app) keeps while its window is closed:
// the session journal, the link to the RackAttack, the sequencer, the library, the settings,
// the updater.
// The window (MainComponent) only shows it.

#pragma once

#include <JuceHeader.h>

#include "../plugin/Updater.h"
#include "Engine.h"
#include "FakeRackAttack.h"
#include "Library.h"
#include "Sequencer.h"
#include "Session.h"

class AppContext
{
public:
    AppContext();
    ~AppContext();

    juce::PropertiesFile& settings()   { return *settingsFile; }

    // Opens the ports of last time and looks for the unit. Called when the window opens the
    // first time, not when the instance is created: hosts create and destroy plug-ins while
    // scanning, and that must not touch the MIDI ports. Does nothing the second time.
    void start();

    // MIDI ports, remembered for the next time
    juce::String getInput() const      { return inId; }
    juce::String getOutput() const     { return outId; }
    void chooseInput (const juce::String& identifier);
    void chooseOutput (const juce::String& identifier);

    bool isSetUp() const;              // the connection assistant was completed once
    void setSetUp (bool done);
    void saveSettings();               // ports, Sysex ID, channel, note of sound 1
    bool isDemo() const                { return demo != nullptr; }

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    Session session;
    Engine engine { session };
    Sequencer sequencer { engine };
    Library library;
    Updater updater { [this]() -> juce::PropertiesFile& { return *settingsFile; } };

private:
    std::unique_ptr<FakeRackAttack> demo;   // RA_DEMO=1 only: a pretend unit, for checking the screens
    juce::String inId, outId;
    bool started = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AppContext)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppContext)
};
