#include "RaKit.h"

#include <algorithm>

namespace ra
{
std::vector<Bytes> splitSysex (const Bytes& data)
{
    std::vector<Bytes> out;
    Bytes current;
    bool inside = false;

    for (auto b : data)
    {
        if (b == kSox)
        {
            current.assign (1, b);
            inside = true;
        }
        else if (inside)
        {
            current.push_back (b);
            if (b == kEox)
            {
                out.push_back (current);
                inside = false;
            }
        }
    }
    return out;
}

//==============================================================================
Kit::Kit()
    : program ((size_t) kProgramDataSize, 0)
{
    for (auto& s : sounds)  s.assign ((size_t) kSoundDataSize, 0);
    for (auto& e : effects) e.assign ((size_t) kEffectDataSize, 0);
    for (int i = 0; i < 16; ++i)
        program[(size_t) (1 + i)] = ' ';
}

namespace
{
    int valueAt (const Bytes& b, int address)
    {
        return address >= 0 && address < (int) b.size() ? b[(size_t) address] : 0;
    }

    bool setAt (Bytes& b, int address, int value)
    {
        if (address < 0 || address >= (int) b.size())
            return false;
        b[(size_t) address] = (std::uint8_t) (value & 0x7F);
        return true;
    }

    // Data of a dump: payload after CMD and the location bytes, if the size is right.
    std::optional<Bytes> dumpData (const Parsed& p, int locationBytes, int size)
    {
        if ((int) p.payload.size() != 1 + locationBytes + size)
            return std::nullopt;
        return Bytes (p.payload.begin() + 1 + locationBytes, p.payload.end());
    }
}

Kit::Change Kit::apply (const Parsed& p)
{
    Change c;
    if (! p.isRackAttack || p.check == Parsed::Check::bad)
        return c;

    const auto& pl = p.payload;

    switch (p.command)
    {
        case cmd::SNDD:
            if (pl.size() >= 3 && pl[1] == kEditBuffer && pl[2] < numSounds)
                if (auto d = dumpData (p, 2, kSoundDataSize))
                {
                    sounds[pl[2]] = *d;
                    soundRead[pl[2]] = true;
                    c = { Change::What::sound, pl[2], -1 };
                }
            break;

        case cmd::EFXD:
            if (pl.size() >= 3 && pl[1] == kEditBuffer && pl[2] < numEffects)
                if (auto d = dumpData (p, 2, kEffectDataSize))
                {
                    effects[pl[2]] = *d;
                    effectRead[pl[2]] = true;
                    c = { Change::What::effect, pl[2], -1 };
                }
            break;

        case cmd::PRGD:
            // The location bytes of a program dump are not known for sure: any is taken
            // as "the program being played", and kept to send the program back the same way.
            if (auto d = dumpData (p, 2, kProgramDataSize))
            {
                program = *d;
                programLocation.assign (pl.begin() + 1, pl.begin() + 3);
                programRead = true;
                c = { Change::What::program, -1, -1 };
            }
            break;

        case cmd::SNDP:
            if (pl.size() == 6 && pl[1] == kEditBuffer && pl[2] < numSounds)
            {
                int address = pl[3] * 128 + pl[4];
                if (setAt (sounds[pl[2]], address, pl[5]))
                    c = { Change::What::sound, pl[2], address };
            }
            break;

        case cmd::EFXP:
            if (pl.size() == 6 && pl[1] == kEditBuffer && pl[2] < numEffects)
            {
                int address = pl[3] * 128 + pl[4];
                if (setAt (effects[pl[2]], address, pl[5]))
                    c = { Change::What::effect, pl[2], address };
            }
            break;

        case cmd::PRGP:
            if (pl.size() == 5 && pl[1] == kEditBuffer)
            {
                int address = pl[2] * 128 + pl[3];
                if (setAt (program, address, pl[4]))
                    c = { Change::What::program, -1, address };
            }
            break;

        default:
            break;
    }
    return c;
}

int Kit::programValue (int a) const          { return valueAt (program, a); }
int Kit::soundValue (int s, int a) const     { return s >= 0 && s < numSounds ? valueAt (sounds[(size_t) s], a) : 0; }
int Kit::effectValue (int e, int a) const    { return e >= 0 && e < numEffects ? valueAt (effects[(size_t) e], a) : 0; }
void Kit::setProgramValue (int a, int v)       { setAt (program, a, v); }
void Kit::setSoundValue (int s, int a, int v)  { if (s >= 0 && s < numSounds) setAt (sounds[(size_t) s], a, v); }
void Kit::setEffectValue (int e, int a, int v) { if (e >= 0 && e < numEffects) setAt (effects[(size_t) e], a, v); }

std::string Kit::name() const
{
    std::string n;
    for (int i = 1; i <= 16; ++i)
    {
        auto ch = (char) program[(size_t) i];
        n += (ch >= 32 && ch < 127) ? ch : ' ';
    }
    while (! n.empty() && n.back() == ' ')
        n.pop_back();
    return n;
}

void Kit::setName (const std::string& name)
{
    for (int i = 0; i < 16; ++i)
    {
        char ch = i < (int) name.size() ? name[(size_t) i] : ' ';
        program[(size_t) (1 + i)] = (std::uint8_t) ((ch >= 32 && ch < 127) ? ch : ' ');
    }
}

bool Kit::isComplete() const   { return blocksRead() == 1 + numSounds + numEffects; }

int Kit::blocksRead() const
{
    return (programRead ? 1 : 0)
         + (int) std::count (soundRead.begin(), soundRead.end(), true)
         + (int) std::count (effectRead.begin(), effectRead.end(), true);
}

void Kit::setSoundData (int i, const Bytes& data)
{
    if (i >= 0 && i < numSounds && (int) data.size() == kSoundDataSize)
    {
        sounds[(size_t) i] = data;
        soundRead[(size_t) i] = true;
    }
}

//==============================================================================
Bytes Kit::programDump (std::uint8_t device) const
{
    Bytes payload { cmd::PRGD };
    payload.insert (payload.end(), programLocation.begin(), programLocation.end());
    payload.insert (payload.end(), program.begin(), program.end());
    return build (device, payload);
}

Bytes Kit::soundDump (std::uint8_t device, int sound) const
{
    Bytes payload { cmd::SNDD, kEditBuffer, (std::uint8_t) sound };
    payload.insert (payload.end(), sounds[(size_t) sound].begin(), sounds[(size_t) sound].end());
    return build (device, payload);
}

Bytes Kit::effectDump (std::uint8_t device, int effect) const
{
    Bytes payload { cmd::EFXD, kEditBuffer, (std::uint8_t) effect };
    payload.insert (payload.end(), effects[(size_t) effect].begin(), effects[(size_t) effect].end());
    return build (device, payload);
}

std::vector<Bytes> Kit::allDumps (std::uint8_t device) const
{
    std::vector<Bytes> out { programDump (device) };
    for (int i = 0; i < numSounds; ++i)  out.push_back (soundDump (device, i));
    for (int i = 0; i < numEffects; ++i) out.push_back (effectDump (device, i));
    return out;
}

Bytes Kit::soundToSyx (const Bytes& soundData)
{
    Bytes payload { cmd::SNDD, kEditBuffer, 0x00 };
    payload.insert (payload.end(), soundData.begin(), soundData.end());
    return build (0, payload);
}

std::optional<Bytes> Kit::soundFromSyx (const Bytes& fileData)
{
    for (auto& m : splitSysex (fileData))
    {
        auto p = parse (m);
        if (p.isRackAttack && p.command == cmd::SNDD)
            if (auto d = dumpData (p, 2, kSoundDataSize))
                return d;
    }
    return std::nullopt;
}

Bytes Kit::toSyx() const
{
    Bytes out;
    for (auto& m : allDumps (0))
        out.insert (out.end(), m.begin(), m.end());
    return out;
}

std::optional<Kit> Kit::fromSyx (const Bytes& fileData)
{
    Kit k;
    bool any = false;
    for (auto& m : splitSysex (fileData))
        any = k.apply (parse (m)).what != Change::What::nothing || any;
    if (! any)
        return std::nullopt;
    return k;
}

std::vector<std::string> Kit::differences (const Kit& o) const
{
    std::vector<std::string> d;
    auto two = [] (int n) { return (n < 10 ? "0" : "") + std::to_string (n); };

    if (program != o.program)
        d.push_back ("programme");
    for (int i = 0; i < numSounds; ++i)
        if (sounds[(size_t) i] != o.sounds[(size_t) i])
            d.push_back ("son " + two (i + 1));
    for (int i = 0; i < numEffects; ++i)
        if (effects[(size_t) i] != o.effects[(size_t) i])
            d.push_back ("FX" + std::to_string (i + 1));
    return d;
}
}
