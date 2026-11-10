// The Labo's link to the RackAttack: MIDI ports, read sequences, capture of the
// front panel, and a small player (test rhythm, parameter bursts, CC sweep) on a
// 1 ms timer thread. Ports, reads and listener calls: message thread.

#pragma once

#include <JuceHeader.h>

#include <deque>

#include "../core/RaSysex.h"
#include "Session.h"

class Engine : private juce::MidiInputCallback,
               private juce::AsyncUpdater,
               private juce::Timer,
               private juce::HighResolutionTimer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void journalLine (const juce::String& line) = 0;   // already timestamped
        virtual void addressMapChanged() = 0;
        virtual void statusChanged (const juce::String& status) = 0;
    };

    explicit Engine (Session&);
    ~Engine() override;

    void setListener (Listener* l)   { listener = l; }

    //==========================================================================
    // Ports. An empty identifier closes. Returns an error text, empty if fine.
    juce::String openInput (const juce::String& identifier);
    juce::String openOutput (const juce::String& identifier);
    bool hasOutput() const;

    // Settings
    std::atomic<int> deviceId { 0 };     // RackAttack Sysex ID, 127 = broadcast
    std::atomic<int> channel { 1 };      // 1..16, notes / programme changes / CC
    std::atomic<int> baseNote { 36 };    // note of sound 1 (native mode)

    //==========================================================================
    // Reading (queued; each request waits for its answer or a time-out)
    void readGlobals();
    void readProgram();
    void readSounds();
    void readEffects();
    void readAll();
    void readAllPrograms();               // Program Change 1..50, then everything of each
    void probePatterns (int sound);       // candidate formats of PATR
    void probeLocations();                // PRGR / SNDR with other location bytes
    void cancelReads();
    bool isReading() const                { return waiting || ! steps.empty(); }

    //==========================================================================
    // Sending
    void sendSysex (const ra::Bytes& sysex, const juce::String& why = {});
    void sendShort (const juce::MidiMessage& m, bool log = true);
    void playSound (int sound);                     // one note, sound 0..23
    void resendSoundDump (int sound);               // the last dump received for it, unchanged
    bool hasSoundDump (int sound) const;

    //==========================================================================
    // Player (1 ms thread)
    void setRhythm (bool on, double bpm, int stepsPerBeat, int sound);
    void setBurst (int messagesPerSecond, int sound, int address);   // 0 = off; triangle 0..127
    void setResendLoop (int sound, int intervalMs);                   // 0 = off
    void startCcSweep (int firstCc, int lastCc, int note);
    void stopPlayer();

    //==========================================================================
    const ra::AddressMap& getAddressMap() const   { return addressMap; }
    void clearAddressMap();
    void userNote (const juce::String& text);       // a remark typed by the user

    // Tests only: a virtual output that receives everything sent, and a way to
    // feed messages as if they came from the RackAttack.
    void setTestOutput (std::function<void (const juce::MidiMessage&)> sink);
    void injectForTest (const juce::MidiMessage& m)   { handleIncomingMidiMessage (nullptr, m); }

private:
    struct Step
    {
        enum class Kind { sendAndWait, sendShort, pause, tag, done };
        Kind kind = Kind::pause;
        ra::Bytes sysex;
        juce::MidiMessage shortMessage;
        int expect = -1;            // command expected back; 0x100 = any RackAttack message
        int ms = 0;
        juce::String label;
    };

    void queue (Step s);
    void queueRead (const ra::Bytes& request, int expect, const juce::String& label, int timeoutMs = 1200);
    void queueSoundsAndEffects();
    void runSteps();
    void process (const ra::Bytes& raw);
    void log (const juce::String& text);
    void status (const juce::String& text);
    void sendNow (const juce::MidiMessage& m);   // any thread

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void hiResTimerCallback() override;
    void playerLine (const juce::String& text);  // from the 1 ms thread

    Session& session;
    Listener* listener = nullptr;

    juce::CriticalSection outLock;
    std::unique_ptr<juce::MidiOutput> output;
    std::function<void (const juce::MidiMessage&)> testOutput;
    std::unique_ptr<juce::MidiInput> input;

    juce::CriticalSection inLock;
    std::vector<ra::Bytes> pending;
    std::atomic<int> clockCount { 0 }, sensingCount { 0 };

    // read queue (message thread)
    std::deque<Step> steps;
    bool waiting = false;
    int waitingFor = -1;
    double deadline = 0;
    juce::String waitingLabel, currentTag;
    int stepsTotal = 0, stepsDone = 0;

    ra::AddressMap addressMap;
    bool mapDirty = false;
    double lastMapSave = 0;

    mutable juce::CriticalSection dumpLock;
    std::array<ra::Bytes, 24> soundDumps;
    ra::Bytes lastGlobals;

    // player (guarded by playerLock, read on the 1 ms thread)
    juce::CriticalSection playerLock;
    bool rhythmOn = false;
    double stepMs = 125.0, nextStepMs = 0;
    int rhythmSound = 0;
    int burstRate = 0, burstSound = 0, burstAddress = 0x27, burstValue = 0, burstDir = 4;
    double nextBurstMs = 0;
    int resendSound = 0, resendInterval = 0;
    double nextResendMs = 0;
    bool sweepOn = false;
    int sweepCc = 12, sweepLast = 59, sweepPhase = 0, sweepNote = 36;
    double sweepNextMs = 0;
    struct NoteOff { int note, channel; double at; };
    std::vector<NoteOff> noteOffs;
    int notesSent = 0, burstSent = 0, resent = 0;
    double nextSummaryMs = 0;

    juce::CriticalSection linesLock;
    juce::StringArray playerLines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
