// The sequencer of the plug-in: the pattern, its two clocks (the DAW's transport,
// or its own when the DAW is stopped or in the app), and the sending of notes and
// p-locks to the RackAttack on time, through the Engine's MIDI output.
//
// Everything is planned on a 2 ms thread a little ahead (40 ms) and handed to the
// MIDI output with its own time, so the timing does not depend on the DAW's audio
// buffer. The DAW's position is followed smoothly; a jump (loop, moving the
// playhead) restarts the plan from the new place.
//
// Notes played into the plug-in from the DAW (a keyboard on the track) go on to
// the RackAttack; they and the notes the unit itself sends back make the voices
// flash on the screen.

#pragma once

#include <JuceHeader.h>

#include "../core/RaSeq.h"
#include "Engine.h"

class Sequencer : private juce::HighResolutionTimer,
                  private Engine::Listener
{
public:
    explicit Sequencer (Engine&);
    ~Sequencer() override;

    //==========================================================================
    // The pattern, read or changed under a lock: keep the work inside short.
    template <typename Fn> void edit (Fn&& fn)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            fn (pattern);
        }
        ++revision;
    }

    template <typename Fn> void read (Fn&& fn) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        fn (pattern);
    }

    int getRevision() const              { return revision.load(); }   // changes at each edit

    juce::String saveToString() const;   // base64
    bool loadFromString (const juce::String&);

    //==========================================================================
    // Own clock: the app, or the plug-in while the DAW is stopped.
    void play();
    void stop();
    void togglePlay()                    { if (ownPlaying.load()) stop(); else play(); }
    bool isPlaying() const               { return shownPpq.load() >= 0.0 || ownPlaying.load(); }
    bool isFollowingHost() const         { return followingHost.load(); }
    double getTempo() const;             // the DAW's while it drives, else the pattern's

    // The playhead in quarter notes, -1 when stopped (for the screen).
    double playheadPpq() const;

    //==========================================================================
    // From the plug-in's audio thread (no lock held for long, no allocation).
    void hostTransport (bool playing, double ppq, double bpm);
    void hostNote (const juce::MidiMessage&);

    // The kit changed: base values put back after locks, Tuned Channels, channel.
    void refreshFromKit();

    //==========================================================================
    // For the screen: when a voice last played (ms counter, -1 = never) and whether
    // it came from a keyboard; the share of the MIDI cable used by the sequencer.
    double lastHitMs (int track, bool& fromKeyboard) const;
    int midiLoadPercent() const          { return loadPercent.load(); }

    // Tests: run the 2 ms work by hand, with a fake clock.
    void tickForTest (double nowMs)      { tick (nowMs); }
    void stopTimerForTest()              { HighResolutionTimer::stopTimer(); }

private:
    void hiResTimerCallback() override   { tick (juce::Time::getMillisecondCounterHiRes()); }
    void kitChanged (const ra::Kit::Change&) override;
    void connectionChanged (Engine::Connection) override   { refreshFromKit(); }
    void tick (double nowMs);
    void halt (double nowMs);
    void send (const std::vector<ra::seq::Event>&, double ppqNow, double nowMs, double bpm);
    void keyboardNote (const juce::MidiMessage&, bool forward);
    void addHit (int track, double ms, bool keyboard);

    Engine& engine;

    mutable juce::SpinLock lock;         // pattern and context
    ra::seq::Pattern pattern;
    ra::seq::Context context;
    std::atomic<int> revision { 0 };

    // planning (2 ms thread only)
    ra::seq::Player player;
    bool running = false, drivenByHost = false;
    double ownStartMs = 0.0, ownStartPpq = 0.0, ownBpm = 120.0;
    double anchorPpq = 0.0, anchorMs = 0.0, anchorBpm = 120.0;
    int seenHostCount = -1;
    double lastPpq = 0.0;
    double loadWindowStart = 0.0;
    int loadBytes = 0;
    std::vector<ra::seq::Event> events;

    // from the audio thread
    juce::SpinLock hostLock;
    bool hostPlaying = false;
    double hostPpqValue = 0.0, hostBpmValue = 120.0, hostStampMs = 0.0;
    int hostCount = 0;

    std::atomic<bool> ownPlaying { false }, followingHost { false };
    std::atomic<double> shownPpq { -1.0 }, shownMs { 0.0 }, shownBpm { 120.0 };
    std::atomic<int> loadPercent { 0 };

    // notes from the DAW (single producer: the audio thread)
    juce::AbstractFifo noteFifo { 256 };
    std::array<std::array<juce::uint8, 3>, 256> noteData {};

    // voices that played, for the flashes
    mutable juce::SpinLock hitLock;
    mutable std::array<std::vector<double>, ra::seq::numTracks> upcoming {};
    mutable std::array<double, ra::seq::numTracks> lastHit {};
    mutable std::array<bool, ra::seq::numTracks> lastHitKeyboard {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sequencer)
};
