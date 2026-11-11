// The plug-in window: the app's main component, resizable.

#pragma once

#include <JuceHeader.h>

#include "../app/MainComponent.h"
#include "PluginProcessor.h"

class RackAttackEditor : public juce::AudioProcessorEditor
{
public:
    explicit RackAttackEditor (RackAttackProcessor& p)
        : AudioProcessorEditor (p), main (p.getContext())
    {
        addAndMakeVisible (main);
        setResizable (true, true);
        setResizeLimits (1180, 740, 3000, 2000);
        setSize (1320, 860);
    }

    void resized() override   { main.setBounds (getLocalBounds()); }

private:
    MainComponent main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAttackEditor)
};
