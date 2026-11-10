// Waldorf RackAttack SysEx: building, checking and describing messages.
// No JUCE inside: shared by the Labo app, the future plugin and the tests.
// Protocol notes and sources: docs/SYSEX.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra
{
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kSox = 0xF0, kEox = 0xF7;
constexpr std::uint8_t kWaldorf = 0x3E;
constexpr std::uint8_t kRackAttack = 0x11;
constexpr std::uint8_t kBroadcast = 0x7F;   // device id accepted by every unit
constexpr std::uint8_t kEditBuffer = 0x20;  // BUFN of the program being played / edited

namespace cmd
{
    // Request 0x, Dump 1x, Parameter change 2x, Inquiry 3x
    constexpr std::uint8_t SNDR = 0x00, SNDD = 0x10, SNDP = 0x20, SNDQ = 0x30;
    constexpr std::uint8_t PRGR = 0x01, PRGD = 0x11, PRGP = 0x21, PRGQ = 0x31;
    constexpr std::uint8_t PATR = 0x03, PATD = 0x13, PATP = 0x23, PATQ = 0x33;
    constexpr std::uint8_t GLBR = 0x04, GLBD = 0x14, GLBP = 0x24, GLBQ = 0x34;
    constexpr std::uint8_t EFXR = 0x05, EFXD = 0x15, EFXP = 0x25, EFXQ = 0x35;
    constexpr std::uint8_t MODR = 0x07, MODD = 0x17, MODP = 0x27, MODQ = 0x37;
}

// Data sizes of the dumps (ra-editor, to confirm on the unit).
constexpr int kSoundDataSize = 109;
constexpr int kProgramDataSize = 40;
constexpr int kEffectDataSize = 18;
constexpr int kGlobalDataSize = 199;

// Sum of the bytes from CMD to the last data byte, truncated to 7 bits.
std::uint8_t checksum (const std::uint8_t* data, std::size_t size);
inline std::uint8_t checksum (const Bytes& b) { return checksum (b.data(), b.size()); }

// F0 3E 11 <device> <payload> <checksum> F7. The payload starts with CMD.
Bytes build (std::uint8_t device, const Bytes& payload);

// Requests and parameter changes, in the formats ra-editor uses with the unit.
Bytes globalRequest (std::uint8_t device);
Bytes programRequest (std::uint8_t device, std::uint8_t program = 0, std::uint8_t buffer = kEditBuffer);
Bytes soundRequest (std::uint8_t device, int sound, std::uint8_t buffer = kEditBuffer);       // sound 0..23
Bytes effectRequest (std::uint8_t device, int effect, std::uint8_t buffer = kEditBuffer);     // effect 0..3
Bytes soundParameter (std::uint8_t device, int sound, int address, int value, std::uint8_t buffer = kEditBuffer);

// What a SysEx message (F0 .. F7) is.
struct Parsed
{
    enum class Check { none, ok, joker, bad };

    bool isSysex = false;        // starts with F0
    bool complete = false;       // ends with F7
    bool isRackAttack = false;   // F0 3E 11
    std::uint8_t device = 0;
    std::uint8_t command = 0;
    Bytes payload;               // CMD .. last data byte (no checksum)
    std::uint8_t sentChecksum = 0;
    std::uint8_t expectedChecksum = 0;
    Check check = Check::none;
};

Parsed parse (const Bytes& sysex);

enum class Object { sound, program, pattern, global, effect, mode, unknown };
enum class Action { request, dump, parameter, inquiry, unknown };

Object objectOf (std::uint8_t command);
Action actionOf (std::uint8_t command);
const char* commandName (std::uint8_t command);   // "SNDP", or nullptr if unknown

// Bytes between CMD and the data / parameter address, per known format.
// Returns -1 when the format is unknown (patterns, mode commands).
int locationSize (std::uint8_t command);

// Expected size of the data in a dump (after the location bytes), -1 if unknown.
int dumpDataSize (std::uint8_t command);

// Parameter names (front panel spelling) and value texts, from docs/SYSEX.md.
const char* parameterName (Object object, int address);           // nullptr if unknown
std::string valueText (Object object, int address, int value);    // enum text, else the number
std::vector<std::string> valueNames (Object object, int address); // the choices of an enum, empty if a number

// A parameter change (xxxP) taken apart. Works on unknown formats too: the last
// three payload bytes are taken as PAH PAL VALUE, everything before as location.
struct ParameterChange
{
    std::uint8_t command = 0;
    Bytes location;
    int address = 0;   // PAH * 128 + PAL
    int value = 0;
};

std::optional<ParameterChange> parameterChange (const Parsed& p);

// One line in French for the journal, e.g. "SNDP son 03 · 27 Cutoff = 64 · checksum OK".
std::string describe (const Bytes& sysex);

std::string hex (const Bytes& bytes);
std::string hex2 (int value);   // "0A"

// "f0 3e 11 00 20" / "F03E1100" / "0x20, 0x21" -> bytes. Empty optional on a bad character.
std::optional<Bytes> parseHex (const std::string& text);

//==============================================================================
// Everything the front panel (Ctl Send = SysEx) or anybody sent as parameter
// changes, gathered per command and address: the way to find the unknown addresses.
class AddressMap
{
public:
    struct Entry
    {
        int minValue = 127, maxValue = 0, lastValue = 0, count = 0;
        Bytes lastLocation;
    };

    void add (const ParameterChange& change);
    void clear()                     { entries.clear(); }
    bool empty() const               { return entries.empty(); }
    std::size_t size() const         { return entries.size(); }
    const Entry* find (std::uint8_t command, int address) const;

    // Fixed-width table, one line per address, unknown addresses marked "???".
    std::string table() const;

private:
    std::map<std::pair<std::uint8_t, int>, Entry> entries;
};
}
