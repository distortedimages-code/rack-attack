#include "SimDevice.h"

SimDevice::SimDevice (Engine& engine)
{
    unit.engine = &engine;
    unit.useDemoSounds();
    synth.setGlobalChannel (unit.channel);
    queue.reserve (1024);
    pending.reserve (1024);
    snapshot();
}

void SimDevice::snapshot()
{
    // under unitLock
    std::array<std::array<std::uint8_t, ra::synth::soundBytes>, ra::synth::numSounds> s {};
    std::array<std::array<std::uint8_t, ra::synth::fxBytes>, ra::synth::numFx> f {};
    for (int i = 0; i < ra::synth::numSounds; ++i)
    {
        auto d = unit.edit.soundData (i);
        if (d.size() >= (size_t) ra::synth::soundBytes)
            std::copy (d.begin(), d.begin() + ra::synth::soundBytes, s[(size_t) i].begin());
    }
    for (int e = 0; e < ra::synth::numFx; ++e)
        for (int a = 0; a < ra::synth::fxBytes; ++a)
            f[(size_t) e][(size_t) a] = (std::uint8_t) juce::jlimit (0, 127, unit.edit.effectValue (e, a));
    const juce::SpinLock::ScopedLockType sl (queueLock);
    snapSounds = s;
    snapFx = f;
    snapReady = true;
}

void SimDevice::receive (const juce::MidiMessage& m, double atMs)
{
    bool wholeKit = false, toSynth = false;
    {
        const juce::ScopedLock sl (unitLock);
        unit.receive (m);
        if (m.isSysEx())
        {
            const auto* d = m.getSysExData();
            const int size = m.getSysExDataSize();
            const int command = size >= 4 ? d[3] : -1;    // after F0: 3E 11 ID CMD
            if (command == ra::cmd::SNDP || command == ra::cmd::EFXP)
                toSynth = true;                            // one value: at its time (locks)
            else if (command == ra::cmd::SNDD || command == ra::cmd::EFXD || command == ra::cmd::PRGD
                     || command == ra::cmd::PRGP)
                wholeKit = true;
        }
        else if (m.isProgramChange())
            wholeKit = true;
        else
            toSynth = m.isNoteOnOrOff() || m.isController() || m.isChannelPressure() || m.isPitchWheel();

        if (wholeKit)
            snapshot();
    }
    if (toSynth)
    {
        const auto* raw = m.getRawData();
        const juce::SpinLock::ScopedLockType sl (queueLock);
        if (queue.size() > 8192)
            queue.erase (queue.begin(), queue.begin() + 4096);   // nobody is rendering: forget the oldest
        queue.push_back ({ atMs, std::vector<std::uint8_t> (raw, raw + m.getRawDataSize()) });
    }
}

void SimDevice::setCalibration (const ra::synth::Calibration& c)
{
    auto copy = std::make_unique<ra::synth::Calibration> (c);
    const juce::SpinLock::ScopedLockType sl (queueLock);
    calibration = c;
    pendingCalibration = std::move (copy);
}

ra::synth::Calibration SimDevice::getCalibration() const
{
    const juce::SpinLock::ScopedLockType sl (queueLock);
    return calibration;
}

void SimDevice::prepare (double sr)
{
    sampleRate = sr;
    synth.prepare (sr);
}

void SimDevice::render (juce::AudioBuffer<float>& buffer, int numSamples, double tempo)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    {
        const juce::SpinLock::ScopedTryLockType sl (queueLock);
        if (sl.isLocked())
        {
            if (pendingCalibration != nullptr)
            {
                synth.setCalibration (*pendingCalibration);
                pendingCalibration.reset();
            }
            if (snapReady)
            {
                for (int i = 0; i < ra::synth::numSounds; ++i)
                    synth.setSound (i, snapSounds[(size_t) i].data());
                for (int e = 0; e < ra::synth::numFx; ++e)
                    synth.setEffect (e, snapFx[(size_t) e].data());
                snapReady = false;
            }
            for (auto& t : queue)
                pending.push_back (std::move (t));
            queue.clear();
        }
    }
    std::stable_sort (pending.begin(), pending.end(), [] (const Timed& a, const Timed& b) { return a.ms < b.ms; });

    synth.setTempo (tempo);
    if (buffer.getNumChannels() == 0)
        return;
    if ((int) monoRight.size() < numSamples)
        monoRight.resize ((size_t) numSamples);
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : monoRight.data();
    const double blockMs = numSamples * 1000.0 / sampleRate;

    int pos = 0;
    size_t used = 0;
    for (; used < pending.size(); ++used)
    {
        const auto& t = pending[used];
        if (t.ms >= now + blockMs)
            break;
        const int at = juce::jlimit (0, numSamples, (int) std::lround ((t.ms - now) * sampleRate / 1000.0));
        if (at > pos)
        {
            synth.render (left + pos, right + pos, at - pos);
            pos = at;
        }
        synth.handleMidi (t.bytes.data(), (int) t.bytes.size());
    }
    pending.erase (pending.begin(), pending.begin() + (long) used);
    if (pos < numSamples)
        synth.render (left + pos, right + pos, numSamples - pos);
    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    voices = synth.activeVoices();
}
