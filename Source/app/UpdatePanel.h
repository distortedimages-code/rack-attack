// The window that says, as the editor opens, that a newer version is out and what it brings, with the version
// you have; or, the first time a new version opens, what it brought. Over the whole window, like the measuring
// assistant, in the pixel look.

#pragma once

#include <JuceHeader.h>
#include "../plugin/Updater.h"

class UpdatePanel : public juce::Component,
                    private juce::ChangeListener
{
public:
    explicit UpdatePanel (Updater&);
    ~UpdatePanel() override;

    void showAvailable (const std::vector<Updater::News>&, int versionsNewer);   // newest first
    void showNews (const Updater::News&);                                      // this version's
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // the window behind stays untouchable

private:
    enum class Mode { available, news };
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int drawContent (juce::Graphics*, juce::Rectangle<int> area) const;   // the height it takes
    void refreshButtons();
    void close();

    Updater& updater;
    Mode mode = Mode::available;
    std::vector<Updater::News> news;
    int more = 0;                         // versions newer than this one that aren't told
    juce::String latest;
    juce::Rectangle<int> card, content;

    juce::TextButton install, later { "Plus tard" }, skip { "Ignorer cette version" }, readAll { "Tout lire" }, ok { "OK" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdatePanel)
};
