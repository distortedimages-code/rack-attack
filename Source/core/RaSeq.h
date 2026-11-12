// The sequencer as data and as a player, with no JUCE inside.
//
// A pattern has 24 tracks, one per sound of the kit. A track has up to 128 steps
// and its own length and speed. It is played as steps (one trig per step),
// as a Euclidean rhythm (hits spread over N steps, the steps keep their settings),
// or as notes (a melody on the sound's Tuned Channel).
//
// A step can lock sound parameters ("p-locks"): the values are sent as SysEx
// just before the note and put back to the sound's own value at the next trig
// of that track that doesn't lock them (as on Elektron machines).
//
// The player works in quarter notes (PPQ, like a DAW). It is asked for the
// events up to a time a little ahead, and gives each event its own time, so the
// caller can send them on time.

#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ra::seq
{
constexpr int numTracks = 24;
constexpr int maxSteps = 128;
constexpr int stepsPerPage = 16;
constexpr int maxLocksPerStep = 8;
constexpr int numSoundParameters = 109;       // SDAT bytes, the addresses a step can lock
constexpr int microSteps = 24;                // micro timing unit: 1/24 of a step
constexpr int maxNameBytes = 16;

enum class Mode : std::uint8_t { steps = 0, euclid = 1, notes = 2 };

enum class Division : std::uint8_t { thirtySecond = 0, sixteenth, eighth, quarter, sixteenthTriplet, eighthTriplet };
constexpr int numDivisions = 6;
double stepsPerBeat (Division);
const char* divisionName (Division);          // "1/32", "1/16"...

struct Lock
{
    std::uint8_t address = 0;                 // sound parameter, 0..108
    std::uint8_t value = 0;                   // 0..127
};

struct Step
{
    bool on = false;
    std::uint8_t velocity = 100;              // 1..127
    std::uint8_t note = 60;                   // notes mode: MIDI note
    std::uint8_t length = 4;                  // gate, in eighths of a step: 1..64
    std::int8_t micro = 0;                    // -11..+11, in 1/24 of a step
    std::uint8_t probability = 100;           // 0..100 %
    std::uint8_t condA = 0, condB = 0;        // plays at loop A of every B (B = 0: always)
    std::uint8_t retrig = 1;                  // hits spread over the step: 1..8
    std::vector<Lock> locks;                  // at most maxLocksPerStep, one per address

    const Lock* findLock (int address) const;
    bool setLock (int address, int value);    // false if the step already has too many
    void clearLock (int address);
    bool isDefault() const;                   // nothing to save
    std::string conditionText() const;        // "1:2", "" when always
};

struct Euclid
{
    std::uint8_t steps = 16;                  // 1..maxSteps
    std::uint8_t hits = 4;                    // 0..steps
    std::int8_t rotation = 0;                 // turns the pattern to the right
};

// Hits spread as evenly as possible over the steps (Bjorklund's result), the
// first hit on step 0 before rotation.
std::vector<bool> euclidean (int steps, int hits, int rotation);

struct Track
{
    Mode mode = Mode::steps;
    std::uint8_t length = 16;                 // steps mode and notes mode: 1..maxSteps
    Division division = Division::sixteenth;
    bool mute = false;
    std::string name;                         // given by the user, UTF-8, empty = "SON 01"...
    Euclid euclid;
    std::array<Step, maxSteps> steps {};

    int activeLength() const;                 // euclid mode: euclid.steps
    bool isOn (int index) const;              // a trig at this step, mode taken into account
    std::vector<bool> trigs() const;          // isOn() for the whole active length
    void printEuclid();                       // the Euclidean hits become steps, mode -> steps
    int countTrigs() const;
    int countLocks() const;
};

struct Pattern
{
    std::array<Track, numTracks> tracks {};
    double tempo = 120.0;                     // own clock, when no DAW drives
    std::uint8_t swing = 50;                  // 50..75 %

    std::vector<std::uint8_t> save() const;
    bool load (const std::vector<std::uint8_t>& data);   // false (and unchanged) if not a pattern
};

// The sequencer's OPTIONS: whole-track and whole-pattern edits.
void clearSteps (Track&);                     // no trig, no lock, every step back to its defaults
void clearLocks (Track&);                     // the locks only
void resetTrack (Track&);                     // like new (16 steps, 1/16, steps mode), the name kept
void shiftSteps (Track&, int by);             // turns the steps of the active length (+1: one to the right)
void fillEvery (Track&, int every);           // a trig every `every` steps over the active length, from the first
void fillDemoPattern (Pattern&);              // the demo groove (demo mode, the web version)

//==============================================================================
// What the player needs to know about the unit and the kit.
struct Context
{
    int channel = 1;                          // global channel, 1..16
    int baseNote = 36;                        // note of sound 1 (native mode)
    std::array<std::uint8_t, numTracks> tunedChannel {};   // per sound: 0 = none, 1..16
    std::array<std::array<std::uint8_t, numSoundParameters>, numTracks> sound {};   // the kit's own values
    std::array<bool, numTracks> soundKnown {};             // false: locked values are not put back
};

struct Event
{
    enum class Kind : std::uint8_t { parameter, noteOn, noteOff };
    Kind kind = Kind::noteOn;
    double ppq = 0.0;                         // when, in quarter notes
    int track = 0;
    int channel = 1;
    int note = 0, velocity = 0;               // notes
    int address = 0, value = 0;               // parameters (of sound `track`)
};

class Player
{
public:
    explicit Player (std::uint32_t seed = 1);

    // Plans every trig whose first event (locks included, each sent `leadPpq`
    // before the next) comes before `untilPpq`, starting at `fromPpq` the first
    // time or after a reset, else where the last call stopped. Appends to `out`,
    // in time order per track.
    void render (const Pattern&, const Context&, double fromPpq, double untilPpq, double leadPpq,
                 std::vector<Event>& out);

    // The transport stopped or jumped: note offs for the notes that may still
    // sound, and the locked parameters put back. The next render starts afresh.
    void stop (const Context&, double atPpq, std::vector<Event>& out);

    void reset();
    bool isRunning() const   { return running; }

private:
    void renderTrack (int t, const Pattern&, const Context&, double untilPpq, double leadPpq, std::vector<Event>& out);

    bool running = false;
    double startPpq = 0.0;
    std::array<std::int64_t, numTracks> next {};          // absolute step to plan next
    std::array<double, numTracks> nextSpeed {};           // the steps per beat `next` is counted in
    std::array<std::vector<std::uint8_t>, numTracks> locked {};   // addresses holding a locked value
    // every address a lock was sent for since the start: on a stop, the unit may hold one
    // even when the plan already put it back (the putting back, planned ahead, is dropped)
    std::array<std::vector<std::uint8_t>, numTracks> touched {};
    struct Sounding { double offPpq; int channel, note; };
    std::vector<Sounding> sounding;
    std::mt19937 rng;
};
}
