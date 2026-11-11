// A pretend RackAttack for the tests and the demo mode (RA_DEMO=1): Sysex ID 5,
// 50 stored programs, Program Change, an edit buffer that takes dumps and
// parameter changes, and pattern dumps for one form of request.

#pragma once

#include <JuceHeader.h>

#include <array>

#include "Engine.h"

namespace fake_detail
{
    inline std::string two (int n)   { return (n < 10 ? "0" : "") + std::to_string (n); }
}

struct FakeRackAttack
{
    Engine* engine = nullptr;
    int id = 5, current = 3, channel = 3;   // channel: the unit's global channel
    bool silent = false, acceptsSoundDumps = true, ignoresProgramChange = false;
    std::array<ra::Kit, 50> stored;
    ra::Kit edit;
    std::atomic<int> sysexIn { 0 }, soundParams { 0 }, notesOn { 0 };

    FakeRackAttack()
    {
        for (int p = 0; p < 50; ++p)
        {
            auto& k = stored[(size_t) p];
            k.setName ("PROG " + fake_detail::two (p + 1));
            k.setProgramValue (0x11, 0);   // native
            for (int s = 0; s < ra::Kit::numSounds; ++s)
            {
                ra::Bytes d ((size_t) ra::kSoundDataSize, 0);
                d[1] = (std::uint8_t) (s % 12);      // shape
                d[0x27] = (std::uint8_t) p;          // cutoff = program number
                d[0x35] = (std::uint8_t) (100 + s % 20);
                // modulation amounts are bipolar: 64 is 0 %, as on the unit
                for (int a : { 0x05, 0x06, 0x0A, 0x0B, 0x14, 0x15, 0x1E, 0x1F, 0x2A, 0x2B, 0x2C, 0x2F, 0x30,
                               0x36, 0x37, 0x3B, 0x3C, 0x52, 0x59 })
                    d[(size_t) a] = 64;
                // a few varied values, so the screens have something to show
                auto put = [&d] (int a, int v) { d[(size_t) a] = (std::uint8_t) juce::jlimit (0, 127, v); };
                put (0x02, 30 + s * 3);      put (0x03, (s * 7) % 40);   put (0x09, (s * 11) % 60);
                put (0x11, 50 + s);          put (0x12, (s * 5) % 30);
                put (0x1A, 110);             put (0x1B, 40 + s * 2);     put (0x1C, (s * 9) % 70);
                put (0x1D, (s % 3) * 45);    put (0x24, 50 + s * 2);     put (0x25, 30 + s * 3);
                put (0x26, 1 + s % 4);       put (0x28, 30 + s * 3);     put (0x29, s * 4);
                put (0x3A, 64 + (s % 5 - 2) * 20);
                put (0x3F, (s % 3) * 12);    put (0x40, 30 + s * 3);     put (0x41, 40 + (s % 4) * 15);
                put (0x42, 30 + s * 2);      put (0x43, 40 + s * 3);
                put (0x45, 5);               put (0x46, 60);             put (0x47, 30);  put (0x48, 50);  put (0x49, 64);
                put (0x4B, 30 + s * 3);      put (0x4C, s % 6);          put (0x4F, 20 + s);  put (0x50, (s + 2) % 6);
                put (0x52, (s * 13) % 100);  put (0x57, (s * 6) % 90);   put (0x56, s % 4);   put (0x55, s % 9);
                put (0x58, s >= 20 ? 2 : 0); put (0x59, 64);             put (0x5A, 36);      put (0x5B, 72);
                k.setSoundData (s, d);
            }
            for (int e = 0; e < ra::Kit::numEffects; ++e)
                k.setEffectValue (e, 0, (p + e) % 9);
        }
        edit = stored[(size_t) current];
    }

    // Playable sounds for the simulator (the tests keep the plain values above): a drum
    // kit made for the simulator's default numbers, a little different in each program.
    void useDemoSounds()
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
        for (int p = 0; p < 50; ++p)
        {
            auto& k = stored[(size_t) p];
            k.setName ("SIM " + fake_detail::two (p + 1));
            for (int s = 0; s < ra::Kit::numSounds; ++s)
            {
                const auto& r = kit[s];
                ra::Bytes d ((size_t) ra::kSoundDataSize, 64);
                auto put = [&d] (int a, int v) { d[(size_t) a] = (std::uint8_t) juce::jlimit (0, 127, v); };
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
        edit = stored[(size_t) current];
    }

    static juce::MidiMessage sysex (const ra::Bytes& m)
    {
        return juce::MidiMessage::createSysExMessage (m.data() + 1, (int) m.size() - 2);
    }

    void answer (const ra::Bytes& m)   { engine->injectForTest (sysex (m)); }

    void receive (const juce::MidiMessage& m)
    {
        if (silent) return;
        if (m.isNoteOn())        { ++notesOn; return; }
        if (m.isProgramChange()) { if (! ignoresProgramChange) { current = m.getProgramChangeNumber(); edit = stored[(size_t) current]; } return; }
        if (! m.isSysEx())       return;

        ++sysexIn;
        ra::Bytes raw (m.getRawData(), m.getRawData() + m.getRawDataSize());
        auto p = ra::parse (raw);
        if (! p.isRackAttack || p.check == ra::Parsed::Check::bad || (p.device != id && p.device != ra::kBroadcast))
            return;

        const auto& pl = p.payload;
        auto dev = (std::uint8_t) id;
        switch (p.command)
        {
            case ra::cmd::GLBR:
            {
                ra::Bytes g { ra::cmd::GLBD, 0x00 };
                g.resize (2 + ra::kGlobalDataSize, 0);
                g[2 + 0x07] = (std::uint8_t) channel;          // global channel
                g[2 + 0x08] = (std::uint8_t) id;
                g[2 + 0x0C] = (std::uint8_t) current;          // program being played
                answer (ra::build (dev, g));
                break;
            }
            case ra::cmd::PRGR: answer (edit.programDump (dev)); break;
            case ra::cmd::SNDR: if (pl.size() == 3) answer (edit.soundDump (dev, pl[2])); break;
            case ra::cmd::EFXR: if (pl.size() == 3) answer (edit.effectDump (dev, pl[2])); break;
            case ra::cmd::PATR:
                if (pl.size() == 3 && pl[1] == ra::kEditBuffer)
                {
                    ra::Bytes d { ra::cmd::PATD, pl[1], pl[2] };
                    d.resize (3 + 37, 0);
                    answer (ra::build (dev, d));
                }
                break;
            case ra::cmd::SNDP: ++soundParams; edit.apply (p); break;
            case ra::cmd::SNDD: if (acceptsSoundDumps) edit.apply (p); break;
            default: edit.apply (p); break;   // EFXD, EFXP, PRGP
        }
    }
};
