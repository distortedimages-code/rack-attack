// A RackAttack program ("kit") as data: program settings, 24 sounds, 4 effects.
// Kept in step with the unit from its dumps and parameter changes, turned back
// into dumps to send, and saved as .syx files (the dumps end to end).
// No JUCE inside.

#pragma once

#include <array>

#include "RaSysex.h"

namespace ra
{
// Splits data holding several SysEx messages end to end (a .syx file).
std::vector<Bytes> splitSysex (const Bytes& data);

class Kit
{
public:
    static constexpr int numSounds = 24, numEffects = 4;

    Kit();

    // What changed when a message was applied
    struct Change
    {
        enum class What { nothing, program, sound, effect, whole };
        What what = What::nothing;
        int index = -1;       // sound or effect number
        int address = -1;     // -1 = the whole block
    };

    // Applies a dump or a parameter change of the edit buffer (BUFN 20h).
    // Other locations and other messages are ignored.
    Change apply (const Parsed& message);

    //==========================================================================
    int programValue (int address) const;
    int soundValue (int sound, int address) const;
    int effectValue (int effect, int address) const;
    void setProgramValue (int address, int value);
    void setSoundValue (int sound, int address, int value);
    void setEffectValue (int effect, int address, int value);

    std::string name() const;                       // 16 characters, trailing spaces removed
    void setName (const std::string& name);         // keeps printable ASCII, pads to 16

    bool hasProgram() const                  { return programRead; }
    bool hasSound (int i) const              { return i >= 0 && i < numSounds && soundRead[(size_t) i]; }
    bool hasEffect (int i) const             { return i >= 0 && i < numEffects && effectRead[(size_t) i]; }
    bool isComplete() const;
    int blocksRead() const;                  // 0..29

    const Bytes& soundData (int i) const     { return sounds[(size_t) i]; }
    void setSoundData (int i, const Bytes& data);   // exactly kSoundDataSize bytes

    //==========================================================================
    // Dumps addressed to the edit buffer, ready to send.
    Bytes programDump (std::uint8_t device) const;
    Bytes soundDump (std::uint8_t device, int sound) const;
    Bytes effectDump (std::uint8_t device, int effect) const;
    std::vector<Bytes> allDumps (std::uint8_t device) const;   // program, 24 sounds, 4 effects

    // A single sound as its own dump (sound files), and back.
    static Bytes soundToSyx (const Bytes& soundData);
    static std::optional<Bytes> soundFromSyx (const Bytes& fileData);

    // Kit files: the 29 dumps end to end. Missing blocks are allowed when reading.
    Bytes toSyx() const;
    static std::optional<Kit> fromSyx (const Bytes& fileData);

    // Differences with another kit, in words (empty = identical). For checks after a send.
    std::vector<std::string> differences (const Kit& other) const;

private:
    Bytes program;
    std::array<Bytes, numSounds> sounds;
    std::array<Bytes, numEffects> effects;
    Bytes programLocation { 0x00, kEditBuffer };   // as the unit sent it
    bool programRead = false;
    std::array<bool, numSounds> soundRead {};
    std::array<bool, numEffects> effectRead {};
};

// The simulator's playable kit for program p (0..49): named "SIM 01".., 24 drum and
// tuned sounds, reverb, delay, chorus and overdrive. Only sounds, effects and the name change.
void fillDemoKit (Kit&, int program);
}
