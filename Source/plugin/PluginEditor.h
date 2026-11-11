// The plug-in window: the app's main component, laid out at one size (ui::designWidth x
// ui::designHeight) and zoomed as a whole. Its size is locked: only the zoom changes it.

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
        main.onZoom = [this] (float z) { applyZoom (z); };
        setResizable (false, false);
        applyZoom (main.getZoom());
    }

    void resized() override   { main.setBounds (0, 0, ui::designWidth, ui::designHeight); }

private:
    void applyZoom (float z)
    {
        main.setTransform (juce::AffineTransform::scale (z));
        setSize (juce::roundToInt ((float) ui::designWidth * z), juce::roundToInt ((float) ui::designHeight * z));
    }

    MainComponent main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAttackEditor)
};
