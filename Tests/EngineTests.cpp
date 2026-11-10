// End-to-end test of the Labo engine against a fake RackAttack that answers
// requests in the formats of docs/SYSEX.md. Runs in a temporary HOME.

#include <JuceHeader.h>

#include "../Source/labo/Engine.h"
#include "../Source/labo/Session.h"
#include "../Source/labo/Text.h"

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

namespace
{
    // Answers like a RackAttack would (dump formats as ra-editor reads them).
    struct FakeRackAttack
    {
        Engine* engine = nullptr;
        std::atomic<int> sysexIn { 0 }, soundParams { 0 }, notesOn { 0 }, programChanges { 0 };

        static juce::MidiMessage sysex (const ra::Bytes& m)
        {
            return juce::MidiMessage::createSysExMessage (m.data() + 1, (int) m.size() - 2);
        }

        void receive (const juce::MidiMessage& m)
        {
            if (m.isNoteOn())        { ++notesOn; return; }
            if (m.isProgramChange()) { ++programChanges; return; }
            if (! m.isSysEx())       return;

            ++sysexIn;
            ra::Bytes raw (m.getRawData(), m.getRawData() + m.getRawDataSize());
            auto p = ra::parse (raw);
            if (! p.isRackAttack || p.check != ra::Parsed::Check::ok)
                return;

            auto answer = [this] (ra::Bytes payload, int dataSize)
            {
                for (int i = 0; i < dataSize; ++i)
                    payload.push_back ((std::uint8_t) (i & 0x7F));
                engine->injectForTest (sysex (ra::build (0, payload)));
            };

            const auto& pl = p.payload;
            switch (p.command)
            {
                case ra::cmd::SNDR: answer ({ ra::cmd::SNDD, pl[1], pl[2] }, ra::kSoundDataSize); break;
                case ra::cmd::PRGR: answer ({ ra::cmd::PRGD, pl[1], pl[2] }, ra::kProgramDataSize); break;
                case ra::cmd::EFXR: answer ({ ra::cmd::EFXD, pl[1], pl[2] }, ra::kEffectDataSize); break;
                case ra::cmd::GLBR: answer ({ ra::cmd::GLBD, 0x00 }, ra::kGlobalDataSize); break;
                case ra::cmd::PATR:
                    if (pl.size() == 3 && pl[1] == ra::kEditBuffer)   // only one of the probes is "right"
                        answer ({ ra::cmd::PATD, pl[1], pl[2] }, 37);
                    break;
                case ra::cmd::SNDP: ++soundParams; break;
                default: break;
            }
        }
    };

    struct Collector : Engine::Listener
    {
        juce::StringArray lines;
        int mapChanges = 0;
        juce::String lastStatus;
        void journalLine (const juce::String& l) override   { lines.add (l); }
        void addressMapChanged() override                  { ++mapChanges; }
        void statusChanged (const juce::String& s) override { lastStatus = s; }

        bool contains (const juce::String& text) const
        {
            for (auto& l : lines)
                if (l.contains (text))
                    return true;
            return false;
        }
    };

    void runFor (int ms)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    bool runUntil (std::function<bool()> done, int timeoutMs)
    {
        auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done() && juce::Time::getMillisecondCounter() < end)
            runFor (20);
        return done();
    }

    int countFiles (const juce::File& folder, const juce::String& pattern)
    {
        return folder.getNumberOfChildFiles (juce::File::findFiles, pattern);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    Session session;
    Engine engine (session);
    Collector out;
    FakeRackAttack fake;
    fake.engine = &engine;
    engine.setListener (&out);
    engine.setTestOutput ([&fake] (const juce::MidiMessage& m) { fake.receive (m); });

    std::printf ("session: %s\n", session.getFolder().getFullPathName().toRawUTF8());
    CHECK (session.getFolder().getParentDirectory().getFileName() == "RackAttack Labo");

    // 1. Read everything: 1 global + 1 program + 24 sounds + 4 effects
    engine.readAll();
    CHECK (runUntil ([&] { return ! engine.isReading(); }, 10000));
    auto received = session.getFolder().getChildFile ("recus");
    CHECK (countFiles (received, "*.syx") == 30);
    CHECK (countFiles (received, "*SNDD-son*.syx") == 24);
    CHECK (countFiles (received, "*EFXD-fx*.syx") == 4);
    CHECK (received.getChildFile ("0001-GLBD.syx").existsAsFile());
    CHECK (out.contains ("SNDD son 24 (tampon) · 109 octets de données · checksum OK"_fr));
    CHECK (out.contains ("GLBD"));
    CHECK (! out.contains ("pas de réponse"_fr));
    CHECK (out.lastStatus == "Terminé."_fr);
    CHECK (session.getFolder().getChildFile ("tout-recu.syx").getSize() == 118 * 24 + 27 * 4 + (6 + 199 + 2) + (7 + 40 + 2));
    CHECK (engine.hasSoundDump (0) && engine.hasSoundDump (23));

    // 2. Front panel knobs arrive as parameter changes and fill the address map
    engine.injectForTest (FakeRackAttack::sysex (ra::soundParameter (0, 2, 0x27, 10)));
    engine.injectForTest (FakeRackAttack::sysex (ra::soundParameter (0, 2, 0x27, 120)));
    engine.injectForTest (FakeRackAttack::sysex (ra::soundParameter (0, 2, 0x63, 4)));
    runFor (100);
    CHECK (engine.getAddressMap().size() == 2);
    CHECK (out.mapChanges == 3);
    CHECK (out.contains ("63 ??? = 4"));
    runFor (1200);   // the map is written to disk at most once a second
    CHECK (session.getFolder().getChildFile ("carte.txt").loadFileAsString().contains ("Cutoff"));

    // 3. Pattern probes: one answers, the others time out and the queue goes on
    engine.probePatterns (4);
    CHECK (runUntil ([&] { return ! engine.isReading(); }, 15000));
    CHECK (countFiles (received, "*PATD*.syx") == 1);
    CHECK (out.contains ("pas de réponse (sonde pattern 2"_fr));
    CHECK (out.contains ("Sondes pattern terminées."_fr));

    // 4. Player: a 200 messages/s burst and a 120 BPM rhythm in sixteenths, for one second
    fake.soundParams = 0;
    fake.notesOn = 0;
    engine.setBurst (200, 0, 0x27);
    engine.setRhythm (true, 120.0, 4, 0);
    runFor (1000);
    engine.stopPlayer();
    int params = fake.soundParams.load(), notes = fake.notesOn.load();
    std::printf ("burst: %d parameter changes, %d notes in one second\n", params, notes);
    CHECK (params >= 120 && params <= 260);   // wide margins: build machines can be slow
    CHECK (notes >= 5 && notes <= 11);
    runFor (100);
    CHECK (out.contains ("changements de paramètre"_fr));

    // 5. Re-sending a sound read from the unit sends it back unchanged
    int before = fake.sysexIn.load();
    engine.resendSoundDump (3);
    CHECK (fake.sysexIn.load() == before + 1);

    // 6. Notes and the zip to send
    engine.userNote ("Software 1.04");
    CHECK (out.contains ("NOTE : Software 1.04"));
    auto zip = session.makeZip();
    CHECK (zip.existsAsFile());
    {
        juce::ZipFile z (zip);
        CHECK (z.getNumEntries() >= 33);   // journal, carte, tout-recu, 31 dumps
        CHECK (z.getIndexOfFileName (session.getFolder().getFileName() + "/journal.txt") >= 0);
    }
    CHECK (session.getFolder().getChildFile ("journal.txt").loadFileAsString().contains ("NOTE : Software 1.04"));

    engine.setListener (nullptr);

    if (failures == 0)
        std::printf ("EngineTests: all passed\n");
    return failures == 0 ? 0 : 1;
}
