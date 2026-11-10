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
    int id = 5, current = 3;
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
                k.setSoundData (s, d);
            }
            for (int e = 0; e < ra::Kit::numEffects; ++e)
                k.setEffectValue (e, 0, (p + e) % 9);
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
                g[2 + 0x07] = 3;                               // global channel
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
