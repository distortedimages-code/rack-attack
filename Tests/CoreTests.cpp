// Tests of the SysEx core, against messages known to work with a real RackAttack.

#include "../Source/core/RaFactory.h"
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

    // Waldorf's files: a Standard MIDI File of SysEx, a program with sounds, patterns and effects, stored as P07
    {
        auto dump = [] (std::uint8_t cmdByte, std::uint8_t loc, int size, std::uint8_t fill)
        {
            Bytes payload { cmdByte, 0x30, loc };
            for (int i = 0; i < size; ++i)
                payload.push_back ((std::uint8_t) ((fill + i) & 0x7F));
            return build (0, payload);
        };
        std::vector<Bytes> msgs;
        {
            Bytes prg { cmd::PRGD, 0x30, 0x00 };
            const char* name = "Test Kit        ";
            prg.push_back (0);
            for (int i = 0; i < 16; ++i) prg.push_back ((std::uint8_t) name[i]);
            while ((int) prg.size() < 3 + kProgramDataSize) prg.push_back (0);
            msgs.push_back (build (0, prg));
        }
        for (int s = 0; s < 24; ++s) msgs.push_back (dump (cmd::SNDD, (std::uint8_t) s, kSoundDataSize, (std::uint8_t) s));
        InternalPattern ip;
        ip.mode = 2; ip.length = 12; ip.stepClock = 7; ip.noteLength = 4; ip.swing = 3; ip.group = 1;
        ip.velocity = { 40, 80, 120 };
        ip.steps[0] = 3; ip.steps[3] = 1; ip.steps[6] = 2; ip.steps[20] = 3;   // the last one past the length
        for (int s = 0; s < 24; ++s)
        {
            Bytes payload { cmd::PATD, 0x30, (std::uint8_t) s };
            auto d = (s == 5 ? ip : InternalPattern()).toData();
            payload.insert (payload.end(), d.begin(), d.end());
            msgs.push_back (build (0, payload));
        }
        for (int e = 0; e < 4; ++e) msgs.push_back (dump (cmd::EFXD, (std::uint8_t) e, kEffectDataSize, (std::uint8_t) (10 * e)));
        msgs.push_back (build (0, { 0x07, 0x01, 0x30, 0x06 }));

        // into a MIDI file: a header, one track, a tempo meta event, then the SysEx events
        Bytes trk { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0x90, 0x24, 0x64 };
        for (auto& m : msgs)
        {
            trk.push_back (0x00);
            trk.push_back (0xF0);
            const auto n = (std::uint32_t) (m.size() - 1);
            if (n >= 128) trk.push_back ((std::uint8_t) (0x80 | (n >> 7)));
            trk.push_back ((std::uint8_t) (n & 0x7F));
            trk.insert (trk.end(), m.begin() + 1, m.end());
        }
        trk.insert (trk.end(), { 0x00, 0xFF, 0x2F, 0x00 });
        Bytes smf { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k' };
        const auto len = (std::uint32_t) trk.size();
        for (int sh : { 24, 16, 8, 0 }) smf.push_back ((std::uint8_t) (len >> sh));
        smf.insert (smf.end(), trk.begin(), trk.end());

        CHECK (isSmf (smf) && ! isSmf (msgs[0]));
        const auto read = smfSysex (smf);
        CHECK (read.size() == msgs.size() && read.front() == msgs.front() && read.back() == msgs.back());
        const auto bank = readBank (read);
        CHECK (bank.size() == 1 && bank[0].number == 6 && bank[0].hasKit());
        const auto& k = bank[0].kit;
        CHECK (k.isComplete() && k.name() == "Test Kit" && k.patternsRead() == 24);
        CHECK (k.soundValue (3, 0) == 3 && k.effectValue (2, 1) == 21);
        CHECK (k.getProgramLocation() == Bytes ({ 0x00, kEditBuffer }));
        auto back = InternalPattern::fromData (k.patternData (5));
        CHECK (back && back->mode == 2 && back->length == 12 && back->stepClock == 7 && back->group == 1 && back->swing == 3);
        CHECK (back->hits() == 3 && back->steps[20] == 3 && back->velocity[1] == 80);
        CHECK (back->toData() == ip.toData());
        // the kit file keeps the patterns, and they go to the edit buffer
        auto again = Kit::fromSyx (k.toSyx());
        CHECK (again && again->patternsRead() == 24 && again->patternData (5) == k.patternData (5));
        CHECK (k.allDumps (0).size() == 29 + 24 && parse (k.patternDump (0, 5)).payload[1] == kEditBuffer);

        // a file of patterns alone
        std::vector<Bytes> only (read.begin() + 25, read.begin() + 49);
        const auto pats = readBank (only);
        CHECK (pats.size() == 1 && ! pats[0].hasKit() && pats[0].kit.patternsRead() == 24);

        // into the sequencer's track, and back
        seq::Track t;
        t.name = "Caisse";
        bool approx = true;
        internalToTrack (*back, t, &approx);
        CHECK (! approx && t.name == "Caisse" && t.length == 12 && t.division == seq::Division::eighth);
        CHECK (t.steps[0].on && t.steps[0].velocity == 120 && t.steps[3].velocity == 40 && t.steps[6].velocity == 80);
        CHECK (! t.steps[1].on && ! t.steps[20].on && t.steps[0].length == 4);     // 1/16 notes on 1/8 steps: half a step
        const auto out = trackToInternal (t, 1);
        CHECK (out.length == 12 && out.stepClock == 7 && out.mode == 1 && out.hits() == 3);
        CHECK (out.velocity[0] == 40 && out.velocity[1] == 80 && out.velocity[2] == 120 && out.steps[0] == 3 && out.steps[3] == 1);
        CHECK (swingPercent (0) == 50 && swingPercent (1) == 58 && swingPercent (5) == 75 && internalSwing (67) == 3);
        CHECK (std::string (stepClockName (4)) == "1/16" && stepClockBeats (10) == 1.0);
    }

    if (failures == 0)
        std::printf ("CoreTests: all passed\n");
    return failures == 0 ? 0 : 1;
}
