// Waldorf's own files for the RackAttack (factory sound sets, bonus kits, patterns), and the internal patterns of
// the sounds (the unit's Pattern Play).
//
// Waldorf ships its sounds as Standard MIDI Files full of SysEx: per program, a program dump, the 24 sound dumps,
// the 24 pattern dumps and the 4 effect dumps (all to buffer 0x30), then an order to store that buffer into a
// program. The pattern dump (PATD) is 15 settings and 128 steps, found by reading the 3,600 patterns of the
// factory sets:
//   0 Pattern Mode (as sound byte 0x6C: Off, On, Latch, Toggle, One Shot)   1 Length - 1 (0..127)
//   2 Step Clk   3 Note Length (the same list of note values)                4 Swing (Off, 58.3 .. 75 %)
//   5 Start When   6 Start Where (Current, Step 1)                            7..9 Vel. 1, 2, 3 (64, 96, 127)
//   10 Group (Off, Trigger A, B, C, A+B+C)   11..14 0
//   15..142 the steps: 0 none, 1..3 Vel. 1..3
// The note values of Step Clk and Note Length go from 1/64T to 8 bars; the list below is reconstructed from the
// factory patterns (16 steps of value 4 make a bar, 8 of value 7, 32 of value 2...) and may be off for the rare
// long values.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "RaKit.h"
#include "RaSeq.h"

namespace ra
{
// The SysEx messages of a Standard MIDI File (F0 .. F7 each), in order. Empty when it is not an SMF.
std::vector<Bytes> smfSysex (const Bytes& file);
bool isSmf (const Bytes& file);

struct InternalPattern
{
    int mode = 0, length = 16, stepClock = 4, noteLength = 4, swing = 0, startWhen = 1, startWhere = 0, group = 0;
    std::array<int, 3> velocity { 64, 96, 127 };
    std::array<std::uint8_t, 128> steps {};           // 0 none, 1..3 Vel. 1..3
    std::array<std::uint8_t, 4> reserved {};

    static std::optional<InternalPattern> fromData (const Bytes& data);   // kPatternDataSize bytes
    Bytes toData() const;
    int hits() const;                                 // steps with a note, within the length
    bool isEmpty() const                              { return hits() == 0; }
};

// Step Clk / Note Length: the name ("1/16") and the length in beats (quarter notes) of each value.
constexpr int numStepClocks = 23;
const char* stepClockName (int value);
double stepClockBeats (int value);

// The sequencer's track from an internal pattern (steps, velocities, length, note value, gate), and back.
// approximate: the note value had no exact match among the sequencer's divisions.
void internalToTrack (const InternalPattern&, seq::Track&, bool* approximate = nullptr);
InternalPattern trackToInternal (const seq::Track&, int mode);
int swingPercent (int internalSwing);                 // 50 (Off) .. 75
int internalSwing (int percent);

// A file's programs: each with its kit (sounds, effects, the patterns it has) and its number when the file
// stores it. A file of patterns alone gives one "program" without a program dump.
struct BankProgram
{
    Kit kit;
    int number = -1;                                  // 0..49: where the file stores it, -1 unknown
    bool hasKit() const   { return kit.hasProgram() || kit.blocksRead() > 0; }
};
std::vector<BankProgram> readBank (const std::vector<Bytes>& sysex);
}
