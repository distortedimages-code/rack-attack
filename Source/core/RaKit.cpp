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

//==============================================================================
// The simulator's playable kits (demo mode, the web version): a drum kit made for the
// simulator's default numbers, a little different in each of the 50 programs.
void fillDemoKit (Kit& k, int p)
{
    struct Recipe
    {
        const char* name;
        int shape1, pitch1, shape2, pitch2, mix1, mix2, fm;
        int filter, cutoff, reso;
        int attack, decay, sustain, release;
        int pitchEnv;          // Env 2 -> pitch of both oscillators (64 = none)
        int crack, crackSpeed, crackLength;
        int send, xorGroup, tuned;
    };
    static const Recipe kit[24] = {
        { "Kick",       1, 76, 0, 76, 127,   0,  0,  0, 127,  0,  0, 72,  0,  0, 92,   0,  0, 0,   0, 0, 0 },
        { "Kick 2",     1, 78, 2, 66, 127,  40,  0,  1,  60, 30,  0, 66,  0,  0, 96,   0,  0, 0,   0, 0, 0 },
        { "Snare",      0, 86, 5, 70, 100, 110,  0,  1, 104, 20,  0, 62,  0,  0, 76,   0,  0, 0,  30, 0, 0 },
        { "Clap",       5, 64, 5, 64, 120,   0,  0,  2,  92, 40,  0, 64,  0,  0, 64, 127, 69, 3,  45, 0, 0 },
        { "Rim",        2, 96, 1, 99, 100,  60,  0,  2, 100, 50,  0, 34,  0,  0, 64,   0,  0, 0,   0, 0, 0 },
        { "Hat ferme",  6, 64, 5, 90, 120,  30,  0,  3,  96, 10,  0, 50,  0,  0, 64,   0,  0, 0,   0, 1, 0 },
        { "Hat ouvert", 7, 64, 5, 90, 120,  30,  0,  3,  94, 10,  0, 84,  0,  0, 64,   0,  0, 0,  10, 1, 0 },
        { "Crash",      8, 64, 5, 96, 100,  10,  0,  3,  86,  0,  0, 98,  0,  0, 64,   0,  0, 0,  20, 0, 0 },
        { "Tom grave",  1, 80, 0, 80, 127,   0,  0,  0, 127,  0,  0, 76,  0,  0, 78,   0,  0, 0,  10, 0, 0 },
        { "Tom medium", 1, 83, 0, 83, 127,   0,  0,  0, 127,  0,  0, 74,  0,  0, 78,   0,  0, 0,  10, 0, 0 },
        { "Tom aigu",   1, 86, 0, 86, 127,   0,  0,  0, 127,  0,  0, 72,  0,  0, 78,   0,  0, 0,  10, 0, 0 },
        { "Cloche",     2, 95, 2, 98, 100, 100,  0,  2,  97, 50,  0, 60,  0,  0, 64,   0,  0, 0,   0, 0, 0 },
        { "Clave",      1, 100, 0, 100, 127,  0,  0,  0, 127,  0,  0, 36,  0,  0, 64,   0,  0, 0,   0, 0, 0 },
        { "Shaker",     5, 80, 5, 80, 110,   0,  0,  3, 104, 10, 25, 46,  0,  0, 64,   0,  0, 0,   0, 0, 0 },
        { "Conga",      1, 88, 0, 88, 127,   0,  0,  0, 127,  0,  0, 62,  0,  0, 74,   0,  0, 0,   0, 0, 0 },
        { "Bongo",      1, 92, 0, 92, 127,   0,  0,  0, 127,  0,  0, 56,  0,  0, 74,   0,  0, 0,   0, 0, 0 },
        { "Zap",        3, 95, 0, 95, 120,   0,  0,  1, 110, 60,  0, 70,  0,  0, 127,  0,  0, 0,  20, 0, 0 },
        { "Bruit",      5, 64, 5, 64, 110,   0,  0,  1,  80, 85,  0, 90,  0,  0, 64,   0,  0, 0,  40, 0, 0 },
        { "FM 1",       1, 88, 1, 95, 120,   0, 60,  0, 127,  0,  0, 70,  0,  0, 64,   0,  0, 0,  20, 0, 0 },
        { "FM 2",       1, 84, 1, 103, 120,  0, 100, 1, 104, 20,  0, 60,  0,  0, 64,   0,  0, 0,  20, 0, 0 },
        { "Basse",      3, 88, 2, 88, 110,  50,  0,  1,  62, 70,  0, 80, 40, 30, 64,   0,  0, 0,   0, 0, 2 },
        { "Lead",       2, 96, 3, 96, 100,  80,  0,  1,  92, 40,  0, 76, 60, 40, 64,   0,  0, 0,  30, 0, 0 },
        { "Nappe",      3, 92, 3, 92, 100, 100,  0,  1,  80, 20, 60, 100, 90, 80, 64,  0,  0, 0,  60, 0, 0 },
        { "Stab",       3, 92, 3, 92, 100, 100,  0,  1,  88, 30,  0, 66,  0,  0, 64,   0,  0, 0,  40, 0, 0 },
    };
    auto two = [] (int n) { return std::string (n < 10 ? "0" : "") + std::to_string (n); };
    k.setName ("SIM " + two (p + 1));
    for (int s = 0; s < ra::Kit::numSounds; ++s)
    {
        const auto& r = kit[s];
        ra::Bytes d ((size_t) ra::kSoundDataSize, 64);
        auto put = [&d] (int a, int v) { d[(size_t) a] = (std::uint8_t) std::clamp (v, 0, 127); };
        const int shift = (p % 5) - 2;                     // each program a little higher or lower
        put (0x01, r.shape1); put (0x02, r.pitch1 + shift); put (0x03, 0); put (0x04, 2);
        put (0x10, r.shape2); put (0x11, r.pitch2 + shift); put (0x12, s >= 21 ? 8 : 0); put (0x13, 2);
        put (0x09, r.fm);
        for (int a : { 0x07, 0x0C, 0x16, 0x20, 0x2D, 0x31, 0x38, 0x3D })  put (a, 0);   // mod sources: Env 1
        for (int a : { 0x08, 0x0D, 0x17, 0x21, 0x2E, 0x32, 0x39, 0x3E, 0x53 }) put (a, 0);   // controls: velocity
        put (0x05, r.pitchEnv); put (0x07, 1);             // Env 2 -> pitch
        put (0x14, r.pitchEnv); put (0x16, 1);
        put (0x1A, r.mix1); put (0x1B, r.mix2); put (0x1C, 0);
        put (0x1D, r.crack); put (0x24, r.crackSpeed); put (0x25, r.crackLength);
        put (0x26, r.filter); put (0x27, r.cutoff); put (0x28, r.reso); put (0x29, 0); put (0x2A, 64);
        if (s == 20) { put (0x2B, 90); put (0x2D, 1); }   // the bass: Env 2 opens the filter
        put (0x35, 108); put (0x3A, 64 + ((s % 5) - 2) * 6);
        put (0x37, 90);                                    // a little velocity on the level
        put (0x3F, r.attack); put (0x40, r.decay); put (0x41, r.sustain); put (0x42, r.release); put (0x43, 20);
        put (0x45, 0); put (0x46, 40); put (0x47, 0); put (0x48, 0); put (0x49, 0);
        put (0x4B, 60); put (0x4C, 0); put (0x4D, 0); put (0x4E, 0);
        put (0x4F, 40); put (0x50, 1); put (0x51, 0); put (0x52, 64);
        put (0x55, 1); put (0x56, 0); put (0x57, r.send); put (0x58, r.tuned);
        put (0x59, 64); put (0x5A, 0); put (0x5B, 127); put (0x5E, r.xorGroup);
        put (0x6C, 0);
        k.setSoundData (s, d);
    }
    // FX 1 reverb, FX 2 delay, FX 3 chorus, FX 4 overdrive
    const int types[4] = { 7, 5, 1, 4 };
    for (int e = 0; e < ra::Kit::numEffects; ++e)
    {
        k.setEffectValue (e, 0, types[e]);
        k.setEffectValue (e, 1, 1);
        for (int a = 2; a < 13; ++a)
            k.setEffectValue (e, a, 64);
    }
    k.setEffectValue (1, 5, 96);   // delay length
    k.setEffectValue (1, 6, 50);   // delay feedback
    k.setEffectValue (1, 12, 0);   // no sync
}
}
