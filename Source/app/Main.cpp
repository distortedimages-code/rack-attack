// RackAttack Editor: front panel, kit and library for the Waldorf RackAttack.

#include <JuceHeader.h>

#include "MainComponent.h"

class RackAttackApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "RackAttack Editor"; }
    const juce::String getApplicationVersion() override    { return RA_VERSION; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override              { window.reset(); }
    void systemRequestedQuit() override   { quit(); }

private:
    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff1b1e1c), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1180, 740, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override   { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (RackAttackApplication)
