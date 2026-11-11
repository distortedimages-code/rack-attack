// The plug-in (AU, VST3, and the app through JUCE's standalone wrapper). It makes no sound:
// it talks to the RackAttack through its own MIDI ports (like OXIde), and keeps the link,
// the kit and the journal while its window is closed.

#pragma once

#include <JuceHeader.h>

#include "../app/AppContext.h"

class RackAttackProcessor : public juce::AudioProcessor
{
public:
    RackAttackProcessor();
    ~RackAttackProcessor() override = default;

    AppContext& getContext()   { return context; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    AppContext context;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAttackProcessor)
};
