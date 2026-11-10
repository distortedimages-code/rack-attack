// Tests of the SysEx core, against messages known to work with a real RackAttack.

#include "../Source/core/RaSysex.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static void checkEq (const std::string& got, const std::string& want, int line)
{
    if (got != want)
    {
        std::printf ("FAILED line %d:\n  got:  %s\n  want: %s\n", line, got.c_str(), want.c_str());
        ++failures;
    }
}
#define CHECK_EQ(got, want) checkEq ((got), (want), __LINE__)

int main()
{
    using namespace ra;

    // Two Cutoff messages captured on the Electra One forum (Dec. 2024), accepted by the unit.
    CHECK_EQ (hex (soundParameter (0, 0, 0x27, 1)),    "F0 3E 11 00 20 20 00 00 27 01 68 F7");
    CHECK_EQ (hex (soundParameter (0, 0, 0x27, 0x7F)), "F0 3E 11 00 20 20 00 00 27 7F 66 F7");

    // Requests in ra-editor's formats
    CHECK_EQ (hex (globalRequest (0)),     "F0 3E 11 00 04 00 04 F7");
    CHECK_EQ (hex (programRequest (0)),    "F0 3E 11 00 01 00 20 21 F7");
    CHECK_EQ (hex (soundRequest (0, 23)),  "F0 3E 11 00 00 20 17 37 F7");
    CHECK_EQ (hex (effectRequest (5, 3)),  "F0 3E 11 05 05 20 03 28 F7");

    // Parsing and checksum status
    {
        auto p = parse (soundParameter (0, 2, 0x27, 64));
        CHECK (p.isRackAttack);
        CHECK (p.command == cmd::SNDP);
        CHECK (p.check == Parsed::Check::ok);

        auto joker = soundParameter (0, 2, 0x27, 64);
        joker[joker.size() - 2] = 0x7F;
        CHECK (parse (joker).check == Parsed::Check::joker);

        auto bad = soundParameter (0, 2, 0x27, 64);
        bad[bad.size() - 2] = 0x01;
        CHECK (parse (bad).check == Parsed::Check::bad);

        CHECK (! parse ({ 0xF0, 0x43, 0x10, 0xF7 }).isRackAttack);
        CHECK (! parse ({ 0xF0, 0x3E, 0x11, 0x00 }).complete);
    }

    // Names
    CHECK_EQ (std::string (commandName (cmd::SNDP)), "SNDP");
    CHECK_EQ (std::string (commandName (cmd::PATD)), "PATD");
    CHECK_EQ (std::string (commandName (cmd::MODR)), "MODR");
    CHECK (commandName (0x02) == nullptr);
    CHECK (commandName (0x42) == nullptr);
    CHECK_EQ (std::string (parameterName (Object::sound, 0x27)), "Cutoff");
    CHECK (parameterName (Object::sound, 0x60) == nullptr);
    CHECK_EQ (valueText (Object::sound, 0x26, 1), "1 (Low)");
    CHECK_EQ (valueText (Object::sound, 0x6C, 2), "2 (Latch)");
    CHECK_EQ (valueText (Object::program, 0x01, 'K'), "75 'K'");

    // Descriptions for the journal
    CHECK_EQ (describe (soundParameter (0, 2, 0x27, 64)), "SNDP son 03 (tampon) · 27 Cutoff = 64 · checksum OK");
    CHECK_EQ (describe (soundParameter (0, 9, 0x62, 5)), "SNDP son 10 (tampon) · 62 ??? = 5 · checksum OK");
    CHECK_EQ (describe (build (0, { cmd::GLBP, 0x00, 0x11, 0x01 })), "GLBP · 11 Ctl Send = 1 (SysEx) · checksum OK");
    CHECK_EQ (describe (build (0, { cmd::EFXP, 0x20, 0x01, 0x00, 0x00, 0x07 })), "EFXP FX2 (tampon) · 00 FX Type = 7 (Reverb) · checksum OK");
    CHECK_EQ (describe (soundRequest (0, 0)), "SNDR son · 20 00 · checksum OK");
    CHECK_EQ (describe (build (0, { cmd::PATR, 0x20, 0x00 })), "PATR pattern · 20 00 · checksum OK");

    {
        ra::Bytes dump { cmd::SNDD, 0x20, 0x04 };
        dump.resize (3 + kSoundDataSize, 0);
        CHECK_EQ (describe (build (0, dump)), "SNDD son 05 (tampon) · 109 octets de données · checksum OK");
        dump.pop_back();
        CHECK_EQ (describe (build (0, dump)), "SNDD son 05 (tampon) · 108 octets de données (attendu 109) · checksum OK");
    }

    // Parameter changes of unknown format are still taken apart (PAH PAL VALUE at the end)
    {
        auto pc = parameterChange (parse (build (0, { cmd::PATP, 0x20, 0x03, 0x00, 0x05, 0x02 })));
        CHECK (pc.has_value());
        CHECK (pc->address == 5 && pc->value == 2);
        CHECK_EQ (hex (pc->location), "20 03");
    }

    // Address map
    {
        AddressMap map;
        map.add (*parameterChange (parse (soundParameter (0, 0, 0x27, 10))));
        map.add (*parameterChange (parse (soundParameter (0, 4, 0x27, 90))));
        map.add (*parameterChange (parse (soundParameter (0, 4, 0x61, 3))));
        CHECK (map.size() == 2);
        auto* e = map.find (cmd::SNDP, 0x27);
        CHECK (e != nullptr && e->minValue == 10 && e->maxValue == 90 && e->count == 2);
        auto t = map.table();
        CHECK (t.find ("Cutoff") != std::string::npos);
        CHECK (t.find ("???") != std::string::npos);
    }

    // Hex input
    CHECK_EQ (hex (*parseHex ("f0 3e 11 00 20")), "F0 3E 11 00 20");
    CHECK_EQ (hex (*parseHex ("0x07,0x04, 0x20 0x00")), "07 04 20 00");
    CHECK_EQ (hex (*parseHex ("072000")), "07 20 00");
    CHECK_EQ (hex (*parseHex ("7 4 20")), "07 04 20");
    CHECK (! parseHex ("07 zz").has_value());

    if (failures == 0)
        std::printf ("CoreTests: all passed\n");
    return failures == 0 ? 0 : 1;
}
