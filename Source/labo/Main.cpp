// RackAttack Labo: a small app to record how a Waldorf RackAttack answers, so
// that the unknown parts of its SysEx protocol can be filled in (docs/SYSEX.md).

#include <JuceHeader.h>

#include "LaboComponent.h"

class LaboApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "RackAttack Labo"; }
    const juce::String getApplicationVersion() override    { return RA_VERSION; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override   { window.reset(); }
    void systemRequestedQuit() override   { quit(); }

private:
    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff1c1f1d), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new LaboComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 640, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override   { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (LaboApplication)
