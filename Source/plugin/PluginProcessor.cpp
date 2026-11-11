#include "PluginProcessor.h"
#include "PluginEditor.h"

RackAttackProcessor::RackAttackProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool RackAttackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void RackAttackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // no sound of its own: the RackAttack plays through its own outputs
    buffer.clear();
}

juce::AudioProcessorEditor* RackAttackProcessor::createEditor()
{
    return new RackAttackEditor (*this);
}

void RackAttackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("RackAttackEditor");
    xml.setAttribute ("version", Updater::currentVersion());
    copyXmlToBinary (xml, dest);
}

void RackAttackProcessor::setStateInformation (const void* data, int size)
{
    // Nothing to restore yet: the ports and settings are global. Kept so that the
    // sequencer's patterns can be saved with the DAW project later.
    juce::ignoreUnused (getXmlFromBinary (data, size));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackAttackProcessor();
}
