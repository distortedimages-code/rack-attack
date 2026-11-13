#include "PluginProcessor.h"
#include "PluginEditor.h"

RackAttackProcessor::RackAttackProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // the morph fader, from kit A (0) to kit B (1): the host can automate it, and hears of it when it is moved
    morphParameter = new juce::AudioParameterFloat (juce::ParameterID { "morph", 1 }, "Morph", 0.0f, 1.0f, 0.0f);
    addParameter (morphParameter);
    morphParameter->addListener (this);
    auto& morph = context.morph;
    morph.onUserPosition = [this] (double p)
    {
        if (std::abs (morphParameter->get() - (float) p) > 1.0e-6f)
            morphParameter->setValueNotifyingHost ((float) p);
    };
    morph.onGestureStart = [this] { morphParameter->beginChangeGesture(); };
    morph.onGestureEnd = [this] { morphParameter->endChangeGesture(); };
}

RackAttackProcessor::~RackAttackProcessor()
{
    morphParameter->removeListener (this);
    context.morph.onUserPosition = nullptr;
    context.morph.onGestureStart = nullptr;
    context.morph.onGestureEnd = nullptr;
}

bool RackAttackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void RackAttackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // With the unit, no sound of its own: the RackAttack plays through its own outputs.
    // In simulation, the RackAttack in software plays here.
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

    if (auto* sim = context.simulator())
    {
        if (std::abs (simRate - getSampleRate()) > 1.0 && getSampleRate() > 0.0)
        {
            simRate = getSampleRate();
            sim->prepare (simRate);
        }
        sim->render (buffer, buffer.getNumSamples(), playing ? bpm : seq.getTempo());
    }
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
    const auto morph = context.morph.toString();     // only while A and B differ
    if (morph.isNotEmpty())
        xml.setAttribute ("morph", morph);
    copyXmlToBinary (xml, dest);
}

void RackAttackProcessor::setStateInformation (const void* data, int size)
{
    // The ports and settings are global; the pattern and the morph belong to the project (a project with no
    // morph leaves the unit's kit alone).
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasAttribute ("pattern"))
            context.sequencer.loadFromString (xml->getStringAttribute ("pattern"));
        if (xml->hasAttribute ("morph"))
            context.morph.fromString (xml->getStringAttribute ("morph"));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackAttackProcessor();
}
