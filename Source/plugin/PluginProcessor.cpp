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

void RackAttackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // no sound of its own: the RackAttack plays through its own outputs
    buffer.clear();

    // the DAW's transport drives the sequencer while it plays
    auto& seq = context.sequencer;
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
        }
    seq.hostTransport (playing, ppq, bpm);

    // notes played into the plug-in go on to the RackAttack
    for (const auto meta : midi)
        seq.hostNote (meta.getMessage());
    midi.clear();
}

juce::AudioProcessorEditor* RackAttackProcessor::createEditor()
{
    return new RackAttackEditor (*this);
}

void RackAttackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("RackAttackEditor");
    xml.setAttribute ("version", Updater::currentVersion());
    xml.setAttribute ("pattern", context.sequencer.saveToString());
    copyXmlToBinary (xml, dest);
}

void RackAttackProcessor::setStateInformation (const void* data, int size)
{
    // The ports and settings are global; the pattern belongs to the project.
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasAttribute ("pattern"))
            context.sequencer.loadFromString (xml->getStringAttribute ("pattern"));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackAttackProcessor();
}
