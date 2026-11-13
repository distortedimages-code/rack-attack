// The window: page buttons, the update button and the connection light at the top, the
// pages, a journal that can be shown on the right, a status bar, and the connection
// assistant over everything when needed. Everything it shows lives in the AppContext.

#pragma once

#include <JuceHeader.h>

#include "AppContext.h"
#include "ConnectWizard.h"
#include "MeasureWizard.h"
#include "MorphFader.h"
#include "Pages.h"
#include "Pixel.h"
#include "SeqPage.h"
#include "ToolsPage.h"

class MainComponent : public juce::Component,
                      private Engine::Listener,
                      private ConnectWizard::Host,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (AppContext&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The zoom of the whole window (the editor applies it), remembered.
    std::function<void (float)> onZoom;
    float getZoom() const              { return zoom; }

private:
    struct Light : public juce::Component
    {
        Engine::Connection state = Engine::Connection::noPorts;
        bool simulation = false;
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
    juce::String currentInput() override    { return ctx.getInput(); }
    juce::String currentOutput() override   { return ctx.getOutput(); }
    void chooseInput (const juce::String& identifier) override    { leaveSimulation(); ctx.chooseInput (identifier); }
    void chooseOutput (const juce::String& identifier) override   { leaveSimulation(); ctx.chooseOutput (identifier); }
    void leaveSimulation()                                         { if (ctx.isSimulation()) { ctx.setSimulation (false); light.simulation = false; light.repaint(); } }
    void wizardFinished (bool connected) override;
    void startSimulation() override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &ctx.morph)
            morphChanged();
        else
            refreshUpdate();
    }
    void morphChanged();                 // an end armed: the page framed in yellow, a word in the status bar
    void paintOverChildren (juce::Graphics&) override;
    std::optional<MorphControl::Side> shownArmed;
    void timerCallback() override
    {
        flushJournal();
        if (light.state == Engine::Connection::searching)
            light.repaint();
    }

    void showZoomMenu();
    void runAudit();
    void showPage (int index);
    void showWizard();
    void showMeasure();
    void chooseMeasures();
    void refreshUpdate();
    void showUpdateDialog();
    void flushJournal();

    AppContext& ctx;
    Session& session;
    Engine& engine;
    Library& library;

    px::Look lookAndFeel;

    SeqPage seqPage { engine, ctx.sequencer };
    KitPage kitPage { engine };
    SoundPage soundPage { engine, library };
    EffectsPage effectsPage { engine };
    LibraryPage libraryPage { engine, library, ctx.morph, [this] { return soundPage.getSelectedSound(); } };
    ToolsPage toolsPage { engine, ctx.updater, [this] { ctx.saveSettings(); } };
    static constexpr int numPages = 6;
    std::array<juce::Component*, numPages> pages {};
    juce::OwnedArray<juce::TextButton> pageButtons;
    int page = 0;

    Light light;
    juce::TextButton updateButton, journalButton { "Journal" }, zoomButton;
    float zoom = 1.0f;
    bool journalOpen = false;
    juce::StringArray journalLines;    // the last lines, put on screen only while the journal is open
    bool journalDirty = false;
    struct Backing : public juce::Component       // under the journal, which opens over the page
    {
        void paint (juce::Graphics& g) override
        {
            g.fillAll (px::black);
            px::dither (g, getLocalBounds().removeFromLeft (3), px::faint, 3);
        }
    } journalBacking;
    juce::TextEditor journal, noteField;
    juce::TextButton noteButton { "Noter" }, zipButton { "Préparer le zip"_fr }, folderButton { "Dossier" };

    MorphFader morphFader { ctx.morph };
    ui::StatusLine statusLine;
    double progress = 0.0;
    juce::ProgressBar bar { progress };

    ConnectWizard wizard { engine, *this };
    MeasureWizard measureWizard { engine };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
