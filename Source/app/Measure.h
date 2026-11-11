// Measuring a real RackAttack, so the simulator can follow it.
//
// The Bench plays a test sound on one of the unit's sounds (in its edit buffer, never
// stored), changes one parameter at a time by SysEx, records what comes out of the
// unit through the audio interface, and finds: the latency and the noise, the pitch of
// each Pitch value, detune, the note of a tuned sound, how far the pitch modulation
// goes, the volume, velocity and sustain curves, whether Envelope 1 sets the level,
// the attack, decay and release times and the decay shapes, the filter frequency of
// each cutoff value, from where it rings by itself and how much each resonance value
// lifts, the filter types, how far the cutoff modulation goes, the crack and LFO
// speeds, and the HH and Crash samples.
//
// It talks to the world through a Rig: the LiveRig sends through the Engine and
// records an audio input; the SynthRig plays the simulator itself (for the tests,
// and to try the procedure without a unit).
//
// The result is a Calibration for the simulator and a report of everything measured,
// saved together with the recordings in a .ramesure file (a zip).

#pragma once

#include <JuceHeader.h>

#include "../sim/RaSynth.h"

class Engine;

namespace measure
{
//==============================================================================
// A recording that runs all the time: positions count samples since it started.
class Ring
{
public:
    explicit Ring (int log2Size = 22);           // 4 M samples: 87 s at 48 kHz
    void reset();
    void write (const float* x, int n);           // one writer
    std::int64_t written() const                  { return pos.load (std::memory_order_acquire); }
    int read (std::int64_t from, int count, std::vector<float>& out) const;   // what is there, from "from"

private:
    std::vector<float> buf;
    std::int64_t mask;
    std::atomic<std::int64_t> pos { 0 };
};

struct Rig
{
    virtual ~Rig() = default;
    virtual double sampleRate() const = 0;
    virtual void send (const juce::MidiMessage&) = 0;           // now
    virtual std::int64_t mark() = 0;                             // the recording position now
    virtual void wait (double ms) = 0;                           // time passes, the recording goes on
    virtual int read (std::int64_t from, int count, std::vector<float>& out) = 0;   // mono; what is recorded
    virtual bool shouldStop() = 0;                               // cancelled
    virtual bool interrupted()   { return false; }               // the unit was touched since the last call
    virtual bool wantsPause()    { return false; }               // the pause button (once per press)

    // The part being measured is stopped, and done again after this returns true;
    // false: stop everything. Why: 0 pause asked, 1 the unit sent something (touched),
    // 2 the level changed during the part.
    enum Why { asked = 0, touched = 1, levelChanged = 2 };
    virtual bool pause (int /*why*/)   { return true; }
};

struct Result
{
    bool ok = false;
    juce::String message;                     // what went wrong, or a summary
    juce::StringArray warnings;
    ra::synth::Calibration calibration;
    juce::var report;                         // every measurement, for checking later
    struct Recording { juce::String file; std::vector<float> samples; double rate = 48000.0; };
    std::vector<Recording> recordings;
    juce::Time when = juce::Time::getCurrentTime();
};

//==============================================================================
class Bench
{
public:
    // channel: the global channel (drum notes from baseNote); freeChannel: a channel
    // no other sound of the kit listens to, for the tuned test (0 = skip it).
    Bench (Rig&, int deviceId, int channel, int baseNote, int freeChannel, int scratchSound = 23);

    // steps done, about how many in all, what is measured now (French, for the screen)
    std::function<void (int, int, const juce::String&)> onProgress;

    Result run();                             // blocking, on the measuring thread
    void tone();                              // three test notes (the unit's sound 24 is changed)

    static constexpr int expectedSteps = 255;

private:
    struct Shot
    {
        std::vector<float> x;                 // starts `lead` samples before the note sounds
        int noteOffAt = -1;                   // where the note off is heard, in x
    };
    struct Play
    {
        Play (double hold = 250, double after = 60) : holdMs (hold), afterMs (after) {}
        double holdMs, afterMs;
        int velocity = 100, note = -1, channel = -1;
        std::function<bool (const Shot&)> holdUntil;    // checked while held, up to holdMs
        std::function<bool (const Shot&)> afterUntil;   // checked after the note off, up to afterMs
    };

    void reset (std::initializer_list<std::pair<int, int>> changes);
    void set (int address, int value);
    Shot play (const Play&);
    Shot playLoud (const Play&);              // turns the volume down if it clips
    void check();
    void step();
    template <typename Fn> void group (const juce::String& what, Fn&& fn);
    void warn (const juce::String&);

    double ms (int samples) const             { return (double) samples * 1000.0 / sr; }
    int at (double milliseconds) const        { return (int) std::lround (milliseconds * sr / 1000.0); }
    std::vector<float> envelope (const std::vector<float>& x) const;   // amplitude per ms
    double amplitude (const Shot&, double fromMs, double toMs) const;
    double frequency (const Shot&, double fromMs, double toMs) const;

    bool start (Result&);
    void pitches (Result&);
    void tuning (Result&);
    void levels (Result&);
    void attacks (Result&);
    void falls (Result&);
    void filter (Result&);
    void speeds (Result&);
    void samples (Result&);

    Rig& rig;
    const int device, channel, baseNote, freeChannel, sound;
    std::array<int, ra::synth::soundBytes> state {};
    double sr = 48000.0, noise = 1e-5, refHz = 700.0;
    int latency = 0, lead = 0, refPitch = 100, envWindow = 128;
    int done = 0;
    bool touched = false, clipped = false;
    juce::String what;
    juce::DynamicObject::Ptr report;
    Result* result = nullptr;
};

//==============================================================================
// Plays the simulator as if it were a unit plugged in: a latency, an input gain,
// the AC coupling of the interface and a little noise.
class SynthRig : public Rig
{
public:
    SynthRig (const ra::synth::Calibration& truth, int channel, int baseNote,
              double sampleRate = 48000.0, int latencySamples = 211, bool realTime = false);

    double sampleRate() const override        { return rate; }
    void send (const juce::MidiMessage&) override;
    std::int64_t mark() override              { return now; }
    void wait (double ms) override;
    int read (std::int64_t from, int count, std::vector<float>& out) override;
    bool shouldStop() override                { return stop.load(); }
    bool interrupted() override               { return touch.exchange (false); }
    bool wantsPause() override                { return pauseWanted.exchange (false); }
    bool pause (int why) override;

    std::atomic<bool> stop { false }, touch { false }, pauseWanted { false };
    std::atomic<int> pausedFor { -1 };
    bool waitWhenPaused = false;              // in the app: wait for resume(); in the tests: go on
    void resume()                             { pausedFor = -1; }
    double speed = 1.0;                       // real time only: faster than real time
    std::atomic<float> level { 0.0f };        // recent peak of what is "recorded"
    float gain = 0.5f, noiseLevel = 3e-5f;

private:
    ra::synth::Synth synth;
    double rate;
    int lat;
    bool realTime;
    std::int64_t now = 0;
    double owed = 0.0, sleepOwed = 0.0;
    Ring ring;
    std::vector<float> delay;                 // the latency
    int delayPos = 0;
    float hpX = 0.0f, hpY = 0.0f, hpA = 0.999f;
    juce::Random random { 7 };
};

//==============================================================================
// The real thing: the Engine's MIDI output, and an audio input recorded by a
// Recorder (an audio callback the measuring window adds to its device manager).
class Recorder : public juce::AudioIODeviceCallback
{
public:
    Recorder();
    void setChannels (juce::uint32 mask)      { channelMask = mask; }   // the inputs to record (bit 0 = input 1)
    juce::uint32 getChannels() const          { return channelMask.load(); }
    double sampleRate() const                 { return rate.load(); }
    bool running() const                      { return active.load(); }
    std::int64_t position() const;            // now, between two callbacks
    const Ring& recording() const             { return ring; }
    float peak (int inputChannel) const;      // 0..1 since the last call, per input (up to 8)
    float takePeak (int inputChannel);
    int numInputs() const                     { return inputs.load(); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    Ring ring;
    std::atomic<juce::uint32> channelMask { 3 };
    std::atomic<double> rate { 48000.0 }, blockMs { 0.0 };
    std::atomic<std::int64_t> blockEnd { 0 };
    std::atomic<bool> active { false };
    std::atomic<int> inputs { 0 };
    std::array<std::atomic<float>, 8> peaks;
    std::vector<float> mono;
};

class LiveRig : public Rig
{
public:
    LiveRig (Engine&, Recorder&);
    double sampleRate() const override        { return recorder.sampleRate(); }
    void send (const juce::MidiMessage&) override;
    std::int64_t mark() override              { return recorder.position(); }
    void wait (double ms) override;
    int read (std::int64_t from, int count, std::vector<float>& out) override;
    bool shouldStop() override                { return stop.load(); }
    bool interrupted() override;
    bool wantsPause() override                { return pauseWanted.exchange (false); }
    bool pause (int why) override;            // waits for resume() or stop

    std::atomic<bool> stop { false }, pauseWanted { false };
    std::atomic<int> pausedFor { -1 };        // -1 running, else the Why (shown by the window)
    void resume()                             { pausedFor = -1; }

private:
    Engine& engine;
    Recorder& recorder;
    int seen = 0;
};

//==============================================================================
// The measurement file
juce::File defaultFolder();                                   // Documents/RackAttack/Mesures
juce::File save (const Result&, const juce::File& folder,     // returns the file written (or {})
                 const juce::String& prefix = "RackAttack-mesures-");
bool load (const juce::File&, ra::synth::Calibration&, juce::String& error);

// Helpers, public for the tests
double frequencyOf (const std::vector<float>&, int from, int count, double sampleRate);
double peakFrequency (const std::vector<float>&, int from, int count, double sampleRate,
                      double minHz, double maxHz, double* purity = nullptr);
double rmsOf (const std::vector<float>&, int from, int count);
}
