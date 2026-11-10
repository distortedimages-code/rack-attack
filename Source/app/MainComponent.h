// The window: page buttons and the connection light at the top, the pages, a
// journal that can be shown on the right, a status bar, and the connection
// assistant over everything when needed.

#pragma once

#include <JuceHeader.h>

#include "ConnectWizard.h"
#include "FakeRackAttack.h"
#include "Engine.h"
#include "Library.h"
#include "Pages.h"
#include "Session.h"
#include "ToolsPage.h"

class MainComponent : public juce::Component,
                      private Engine::Listener,
                      private ConnectWizard::Host,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Light : public juce::Component
    {
        Engine::Connection state = Engine::Connection::noPorts;
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override   { if (onClick) onClick(); }
    };

    // Engine::Listener
    void journalLine (const juce::String&) override;
    void statusChanged (const juce::String&) override;
    void connectionChanged (Engine::Connection) override;
    void progressChanged (int done, int total, const juce::String& what) override;
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;

    // ConnectWizard::Host
    juce::String currentInput() override    { return inId; }
    juce::String currentOutput() override   { return outId; }
    void chooseInput (const juce::String& identifier) override;
    void chooseOutput (const juce::String& identifier) override;
    void wizardFinished (bool connected) override;

    void timerCallback() override   { flushJournal(); }
    void showPage (int index);
    void showWizard();
    void saveSettings();

    juce::LookAndFeel_V4 lookAndFeel;
    Session session;
    Engine engine { session };
    Library library;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::String inId, outId;

    KitPage kitPage { engine };
    SoundPage soundPage { engine, library };
    EffectsPage effectsPage { engine };
    LibraryPage libraryPage { engine, library, [this] { return soundPage.getSelectedSound(); } };
    ToolsPage toolsPage { engine, session, [this] { saveSettings(); } };
    std::array<juce::Component*, 5> pages {};
    juce::OwnedArray<juce::TextButton> pageButtons;
    int page = 0;

    Light light;
    juce::TextButton journalButton { "Journal" };
    bool journalOpen = false;
    juce::StringArray journalLines;    // the last lines, put on screen only while the journal is open
    bool journalDirty = false;
    void flushJournal();
    juce::TextEditor journal, noteField;
    juce::TextButton noteButton { "Noter" }, zipButton { "Préparer le zip"_fr }, folderButton { "Dossier" };

    ui::StatusLine statusLine;
    double progress = 0.0;
    juce::ProgressBar bar { progress };

    ConnectWizard wizard { engine, *this };
    std::unique_ptr<FakeRackAttack> demo;   // RA_DEMO=1 only: a pretend unit, for checking the screens

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
