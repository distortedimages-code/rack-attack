// Tests of the SysEx core, against messages known to work with a real RackAttack.

#include "../Source/core/RaKit.h"
#include "../Source/core/RaMorph.h"
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

    // Value lists for the interface
    CHECK (valueNames (Object::sound, 0x01).size() == 12);
    CHECK (valueNames (Object::sound, 0x27).empty());

    // Kit model: dumps and parameter changes of the edit buffer
    {
        Kit kit;
        CHECK (kit.blocksRead() == 0 && kit.name().empty());

        Bytes sound { cmd::SNDD, kEditBuffer, 0x05 };
        for (int i = 0; i < kSoundDataSize; ++i) sound.push_back ((std::uint8_t) i);
        auto c = kit.apply (parse (build (0, sound)));
        CHECK (c.what == Kit::Change::What::sound && c.index == 5 && c.address == -1);
        CHECK (kit.soundValue (5, 0x27) == 0x27 && kit.hasSound (5));

        c = kit.apply (parse (soundParameter (0, 5, 0x27, 99)));
        CHECK (c.what == Kit::Change::What::sound && c.address == 0x27);
        CHECK (kit.soundValue (5, 0x27) == 99);

        // another location (not the edit buffer): ignored
        CHECK (kit.apply (parse (soundParameter (0, 5, 0x27, 1, 0x00))).what == Kit::Change::What::nothing);
        // wrong size: ignored
        sound.pop_back();
        CHECK (kit.apply (parse (build (0, sound))).what == Kit::Change::What::nothing);

        Bytes prog { cmd::PRGD, 0x00, 0x20 };
        for (int i = 0; i < kProgramDataSize; ++i) prog.push_back (0);
        const char* name = "Kick Mafia";
        for (int i = 0; i < 16; ++i) prog[(size_t) (3 + 1 + i)] = (std::uint8_t) (i < 10 ? name[i] : ' ');
        CHECK (kit.apply (parse (build (0, prog))).what == Kit::Change::What::program);
        CHECK_EQ (kit.name(), "Kick Mafia");
        CHECK (kit.apply (parse (build (0, { cmd::PRGP, 0x20, 0x00, 0x01, 'Q' }))).address == 1);
        CHECK_EQ (kit.name(), "Qick Mafia");
        kit.setName ("Zap");
        CHECK_EQ (kit.name(), "Zap");

        CHECK (kit.apply (parse (build (0, { cmd::EFXP, 0x20, 0x02, 0x00, 0x00, 7 }))).what == Kit::Change::What::effect);
        CHECK (kit.effectValue (2, 0) == 7);

        // Dumps to send back: same formats as received
        auto dump5 = kit.soundDump (0, 5);
        CHECK_EQ (hex (Bytes (dump5.begin(), dump5.begin() + 7)), "F0 3E 11 00 10 20 05");
        CHECK (dump5.size() == 118);
        CHECK (kit.programDump (0).size() == 4 + 3 + 40 + 2);
        CHECK (kit.allDumps (0).size() == 29);

        // Files: round trip
        auto file = kit.toSyx();
        CHECK (splitSysex (file).size() == 29);
        auto back = Kit::fromSyx (file);
        CHECK (back.has_value() && back->isComplete());
        CHECK (back->differences (kit).empty());
        back->setSoundValue (3, 0x27, 1);
        auto diff = back->differences (kit);
        CHECK (diff.size() == 1 && diff[0] == "son 04");
        CHECK (! Kit::fromSyx ({ 0x01, 0x02 }).has_value());

        auto soundFile = Kit::soundToSyx (kit.soundData (5));
        auto soundBack = Kit::soundFromSyx (soundFile);
        CHECK (soundBack.has_value() && *soundBack == kit.soundData (5));
    }

    // Morph: two kits and a fader
    {
        Kit k1, k2;
        fillDemoKit (k1, 0);
        fillDemoKit (k2, 7);
        k1.setSoundValue (2, 0x27, 20);  k2.setSoundValue (2, 0x27, 100);    // cutoff: a number
        k1.setSoundValue (2, 0x01, 1);   k2.setSoundValue (2, 0x01, 3);      // shape: a list
        k1.setEffectValue (0, 2, 10);    k2.setEffectValue (0, 2, 90);
        Morph m;
        m.reset (k1);
        CHECK (! m.differs());
        m.load (Morph::Side::b, k2);
        CHECK (m.differs());
        CHECK (Morph::isList (false, 0x01) && ! Morph::isList (false, 0x27));

        m.setPosition (0.0);  CHECK (m.sound (2, 0x27) == 20);  CHECK (m.sound (2, 0x01) == 1);
        m.setPosition (1.0);  CHECK (m.sound (2, 0x27) == 100); CHECK (m.sound (2, 0x01) == 3);
        m.setPosition (0.25); CHECK (m.sound (2, 0x27) == 40);  CHECK (m.sound (2, 0x01) == 1);   // a list waits for half way
        m.setPosition (0.5);  CHECK (m.sound (2, 0x27) == 60);  CHECK (m.sound (2, 0x01) == 3);
        CHECK (m.effect (0, 2) == 50);

        // editing in the middle: both ends move by as much, the blend lands on the value
        m.editSound (2, 0x27, 70);
        CHECK (m.sound (2, 0x27) == 70);
        CHECK (m.end (Morph::Side::a, 2, 0x27) == 30 && m.end (Morph::Side::b, 2, 0x27) == 110);
        // at a quarter: A moves three times as much as B
        m.setPosition (0.25);
        const int a0 = m.end (Morph::Side::a, 2, 0x27), b0 = m.end (Morph::Side::b, 2, 0x27);
        m.editSound (2, 0x27, m.sound (2, 0x27) + 10);
        const int da = m.end (Morph::Side::a, 2, 0x27) - a0, db = m.end (Morph::Side::b, 2, 0x27) - b0;
        CHECK (da == 12 && db == 4);
        // at an end, only that end
        m.setPosition (0.0);
        m.editSound (2, 0x28, 5);
        CHECK (m.end (Morph::Side::a, 2, 0x28) == 5 && m.end (Morph::Side::b, 2, 0x28) == k2.soundValue (2, 0x28));
        // a list edited between the ends: the end being heard
        m.setPosition (0.75);
        m.editSound (2, 0x01, 0);
        CHECK (m.end (Morph::Side::b, 2, 0x01) == 0 && m.end (Morph::Side::a, 2, 0x01) == 1);

        // the blend into a kit; save and back
        Kit heard = k1;
        m.blendInto (heard);
        CHECK (heard.soundValue (2, 0x27) == m.sound (2, 0x27) && heard.effectValue (0, 2) == m.effect (0, 2));
        CHECK (heard.name() == k1.name());
        Morph back;
        CHECK (back.restore (m.save()));
        CHECK (back.position() == 0.75 && back.sound (2, 0x27) == m.sound (2, 0x27) && back.end (Morph::Side::a, 2, 0x28) == 5);
        CHECK (! back.restore ({ 1, 2, 3 }));
        // a sound loaded into one end, the ends swapped, the blend copied to an end
        back.loadSound (Morph::Side::a, 5, k2.soundData (5));
        CHECK (back.end (Morph::Side::a, 5, 0x27) == k2.soundValue (5, 0x27));
        back.swap();
        CHECK (back.end (Morph::Side::b, 5, 0x27) == k2.soundValue (5, 0x27));
        const int heardBefore = back.sound (2, 0x27);
        back.copyBlendTo (Morph::Side::a);
        CHECK (back.end (Morph::Side::a, 2, 0x27) == heardBefore);
        // the value already heard again (an echo of the unit): the ends stay where they are
        back.load (Morph::Side::b, k2);
        back.setPosition (0.3);
        const int ea = back.end (Morph::Side::a, 2, 0x27), eb = back.end (Morph::Side::b, 2, 0x27);
        back.editSound (2, 0x27, back.sound (2, 0x27));
        CHECK (back.end (Morph::Side::a, 2, 0x27) == ea && back.end (Morph::Side::b, 2, 0x27) == eb);
        // an effect loaded into one end
        Bytes fx ((size_t) kEffectDataSize, 9);
        back.loadEffect (Morph::Side::b, 1, fx);
        CHECK (back.effectEnd (Morph::Side::b, 1, 3) == 9);
        // one end edited alone (armed): heard in proportion to the fader
        back.setSoundEnd (Morph::Side::a, 2, 0x27, 0);
        back.setSoundEnd (Morph::Side::b, 2, 0x27, 100);
        back.setPosition (0.25);
        CHECK (back.sound (2, 0x27) == 25);
        back.setSoundEnd (Morph::Side::a, 2, 0x27, 40);
        CHECK (back.end (Morph::Side::b, 2, 0x27) == 100 && back.sound (2, 0x27) == 55);
        // the MIDI routing stays the same in A and B
        back.setSoundEnd (Morph::Side::a, 4, 0x5A, 30);
        CHECK (back.end (Morph::Side::b, 4, 0x5A) == 30);
        Kit routed = k2;
        routed.setSoundValue (4, 0x5A, 90);
        back.load (Morph::Side::b, routed);
        CHECK (back.end (Morph::Side::b, 4, 0x5A) == 30 && back.end (Morph::Side::b, 4, 0x27) == k2.soundValue (4, 0x27));
    }

    if (failures == 0)
        std::printf ("CoreTests: all passed\n");
    return failures == 0 ? 0 : 1;
}
