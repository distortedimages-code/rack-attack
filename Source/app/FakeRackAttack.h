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
        for (int p = 0; p < 50; ++p)
            ra::fillDemoKit (stored[(size_t) p], p);
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
