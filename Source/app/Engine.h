// The app's link to the RackAttack: MIDI ports, finding the unit, keeping the kit
// model in step both ways, sending parameter changes at a safe rate, library
// transfers, and a small test player (rhythm, parameter bursts, CC sweep) on a
// 1 ms thread. Everything except the player runs on the message thread.

#pragma once

#include <JuceHeader.h>

#include <deque>

#include "../core/RaKit.h"
#include "../core/RaSysex.h"
#include "Session.h"

class Engine : private juce::MidiInputCallback,
               private juce::AsyncUpdater,
               private juce::Timer,
               private juce::HighResolutionTimer
{
public:
    enum class Connection { noPorts, searching, connected, noAnswer };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void journalLine (const juce::String&) {}                 // already timestamped
        virtual void addressMapChanged() {}
        virtual void statusChanged (const juce::String&) {}
        virtual void connectionChanged (Connection) {}
        virtual void kitChanged (const ra::Kit::Change&) {}
        virtual void progressChanged (int /*done*/, int /*total*/, const juce::String& /*what*/) {}
        virtual void taskFinished (const juce::String& /*task*/, bool /*ok*/, const juce::String& /*message*/) {}
        virtual void programNamesChanged() {}
    };

    explicit Engine (Session&);
    ~Engine() override;

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    //==========================================================================
    // Ports. An empty identifier closes. Returns an error text, empty if fine.
    juce::String openInput (const juce::String& identifier);
    juce::String openOutput (const juce::String& identifier);
    bool hasOutput() const;
    bool hasInput() const               { return input != nullptr || testOutput != nullptr || timedOutput != nullptr; }

    std::atomic<int> deviceId { 0 };     // RackAttack Sysex ID (found by connect())
    std::atomic<int> channel { 1 };      // 1..16: notes, programme changes, CC
    std::atomic<int> baseNote { 36 };    // note of sound 1 (native mode)
    double pauseScale = 1.0;             // tests only: shorter waits between program changes

    //==========================================================================
    // Finding the unit: asks with the current ID, then with the broadcast ID; the
    // answer gives the real ID. Then reads the kit being played. Task "connect".
    void connect();
    Connection getConnection() const    { return connection; }

    // The kit being played on the unit (edit buffer), kept in step.
    const ra::Kit& getKit() const       { return kit; }
    void readKit();                                         // task "kit"

    // Editing: updates the model and sends the change (at most ~200 per second,
    // the latest value of each parameter only).
    void setSoundParameter (int sound, int address, int value);
    void setProgramParameter (int address, int value);
    void setEffectParameter (int effect, int address, int value);
    void setKitName (const juce::String& name);

    // Programs on the unit
    void selectProgram (int program);                      // 0..49: Program Change, then reads the kit
    int getCurrentProgram() const       { return currentProgram; }   // -1 = not known
    const juce::StringArray& getProgramNames() const   { return programNames; }   // 50, empty = not read
    void readProgramNames();                                // task "names"
    void backupAllPrograms (const juce::File& folder);      // task "backup": one .syx per program

    // Library transfers into the edit buffer, checked by reading back.
    void sendKit (const ra::Kit& k);                        // task "sendKit"
    void sendSound (const ra::Bytes& soundData, int slot);  // task "sendSound"

    bool isBusy() const                 { return waiting || ! steps.empty(); }
    void cancel();

    //==========================================================================
    // Test reads (tools page)
    void readGlobals();
    void readAllForTest();
    void probePatterns (int sound);
    void probeLocations();

    // Sending
    void sendSysex (const ra::Bytes& sysex, const juce::String& why = {}, bool showInJournal = true);
    void sendShort (const juce::MidiMessage& m, bool log = true);
    void playSound (int sound);                     // one note, sound 0..23

    // The sequencer: messages with their own time (sample positions at the given rate
    // after startMs, on the ms counter), sent on time by the output's own thread; and
    // dropping those not sent yet. Any thread. Not written to the journal.
    void sendTimed (const juce::MidiBuffer& buffer, double startMs, double samplesPerSecond);
    void clearTimed();
    void sendQuiet (const juce::MidiMessage& m)     { sendNow (m); }

    // Messages coming from the unit other than clock and sensing (the measurement
    // pauses when someone touches it). Parameter changes of echoSound don't count.
    std::atomic<int> unitMessages { 0 };
    std::atomic<int> echoSound { -1 };

    // Notes coming from the RackAttack (a keyboard plugged into it), on the MIDI
    // input's thread. Set before the input is opened.
    std::function<void (const juce::MidiMessage&)> onIncomingNote;
    void resendSoundDump (int sound);               // the sound as last read, unchanged
    int freeTunedChannel (int exceptSound) const;   // a MIDI channel no sound listens to (not the global one); 0 = none
    bool hasSoundDump (int sound) const;

    // Player (1 ms thread)
    void setRhythm (bool on, double bpm, int stepsPerBeat, int sound);
    void setBurst (int messagesPerSecond, int sound, int address);   // 0 = off; triangle 0..127
    void setResendLoop (int sound, int intervalMs);                   // 0 = off
    void startCcSweep (int firstCc, int lastCc, int note);
    void stopPlayer();

    //==========================================================================
    const ra::AddressMap& getAddressMap() const   { return addressMap; }
    void clearAddressMap();
    void userNote (const juce::String& text);       // a remark typed or clicked by the user

    // Tests only: a virtual output that receives everything sent, and a way to
    // feed messages as if they came from the RackAttack.
    void setTestOutput (std::function<void (const juce::MidiMessage&)> sink);
    // The simulator: every message with the time (ms counter) it must sound at.
    void setTimedOutput (std::function<void (const juce::MidiMessage&, double)> sink);
    void injectForTest (const juce::MidiMessage& m)   { handleIncomingMidiMessage (nullptr, m); }

private:
    struct Step
    {
        enum class Kind { request, send, sendShort, pause, call };
        Kind kind = Kind::pause;
        ra::Bytes sysex;
        juce::MidiMessage shortMessage;
        int expect = -1;            // command expected back; 0x100 = any RackAttack message
        int ms = 0;
        juce::String label;
        bool quiet = false;         // not shown in the on-screen journal (still in the file)
        int probe = -1;             // pattern probe number
        std::function<void (const ra::Parsed*)> after;   // answer, or nullptr on time-out
        std::function<void()> call;
    };

    Step request (const ra::Bytes& sysex, int expect, const juce::String& label, int timeoutMs = 1200,
                  std::function<void (const ra::Parsed*)> after = {});
    Step callStep (std::function<void()> fn);
    void queue (Step s)                 { steps.push_back (std::move (s)); }
    void queueKitRead (bool toCapture, std::function<void()> whenDone);
    void runSteps();
    void finishTask (const juce::String& task, bool ok, const juce::String& message);
    void progress (int done, int total, const juce::String& what);
    void setConnection (Connection c);

    void process (const ra::Bytes& raw);
    void log (const juce::String& text, bool showInJournal = true);
    void status (const juce::String& text);
    void sendNow (const juce::MidiMessage& m);   // any thread
    void flushParameters();
    void queueParameter (ra::Bytes withoutValue, int value);

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void hiResTimerCallback() override;
    void playerLine (const juce::String& text);  // from the 1 ms thread

    Session& session;
    juce::ListenerList<Listener> listeners;

    juce::CriticalSection outLock;
    std::unique_ptr<juce::MidiOutput> output;
    std::function<void (const juce::MidiMessage&)> testOutput;
    std::function<void (const juce::MidiMessage&, double)> timedOutput;
    std::unique_ptr<juce::MidiInput> input;

    juce::CriticalSection inLock;
    std::vector<ra::Bytes> pending;
    std::atomic<int> clockCount { 0 }, sensingCount { 0 };

    // step queue (message thread)
    std::deque<Step> steps;
    bool waiting = false;
    int waitingFor = -1;
    double deadline = 0;
    juce::String waitingLabel;
    std::function<void (const ra::Parsed*)> waitingAfter;
    int probeIndex = -1, workingProbe = -1;
    int timeouts = 0;

    // model
    ra::Kit kit, capture;
    bool capturing = false;
    Connection connection = Connection::noPorts;
    int currentProgram = -1;
    juce::StringArray programNames;
    ra::Bytes lastGlobals;

    // parameter changes waiting to go out (latest value per parameter)
    std::vector<std::pair<ra::Bytes, int>> pendingParams;

    ra::AddressMap addressMap;
    bool mapDirty = false;
    double lastMapSave = 0;

    mutable juce::CriticalSection dumpLock;   // the player reads the sound dumps
    std::array<ra::Bytes, 24> soundDumps;

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
