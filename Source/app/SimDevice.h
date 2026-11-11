// The RackAttack in software, for playing without the unit: the pretend unit that
// answers the plug-in's requests (the same as in the tests: 50 programs, an edit
// buffer, dumps), and the synthesizer that plays its sounds through the plug-in's
// audio output.
//
// The Engine hands every message with the time it must sound at; notes and
// parameter changes (the sequencer's locks) reach the synthesizer at that time, to
// the sample, inside render(). Dumps and program changes copy the whole kit over.

#pragma once

#include <JuceHeader.h>

#include "../sim/RaSynth.h"
#include "FakeRackAttack.h"

class SimDevice
{
public:
    explicit SimDevice (Engine&);

    // any thread
    void receive (const juce::MidiMessage&, double atMs);
    void setCalibration (const ra::synth::Calibration&);
    ra::synth::Calibration getCalibration() const;

    // the audio thread
    void prepare (double sampleRate);
    void render (juce::AudioBuffer<float>&, int numSamples, double tempo);

    int activeVoices() const            { return voices.load(); }

private:
    void snapshot();                    // the unit's kit -> the synthesizer, at the next block

    FakeRackAttack unit;
    juce::CriticalSection unitLock;

    struct Timed { double ms; std::vector<std::uint8_t> bytes; };
    juce::SpinLock queueLock;
    std::vector<Timed> queue;
    std::vector<Timed> pending;         // audio thread only
    std::vector<float> monoRight;

    // a whole kit waiting to be copied in
    std::array<std::array<std::uint8_t, ra::synth::soundBytes>, ra::synth::numSounds> snapSounds {};
    std::array<std::array<std::uint8_t, ra::synth::fxBytes>, ra::synth::numFx> snapFx {};
    bool snapReady = false;
    std::unique_ptr<ra::synth::Calibration> pendingCalibration;
    ra::synth::Calibration calibration = ra::synth::Calibration::defaults();

    ra::synth::Synth synth;
    double sampleRate = 44100.0;
    std::atomic<int> voices { 0 };
};
