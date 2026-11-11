// End-to-end tests of the app engine and library against a fake RackAttack:
// Sysex ID 5, 50 stored programs, Program Change, an edit buffer that takes
// dumps and parameter changes. Runs in a temporary HOME.

#include <JuceHeader.h>

#include "../Source/app/Engine.h"
#include "../Source/app/Sequencer.h"
#include "../Source/app/FakeRackAttack.h"
#include "../Source/app/Library.h"
#include "../Source/app/Session.h"
#include "../Source/app/Text.h"

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

namespace
{
    struct Collector : Engine::Listener
    {
        juce::StringArray lines;
        int mapChanges = 0, kitChanges = 0;
        juce::String lastTask, lastMessage;
        bool lastOk = false;
        Engine::Connection connection = Engine::Connection::noPorts;

        void journalLine (const juce::String& l) override   { lines.add (l); }
        void addressMapChanged() override                  { ++mapChanges; }
        void kitChanged (const ra::Kit::Change&) override   { ++kitChanges; }
        void connectionChanged (Engine::Connection c) override   { connection = c; }
        void taskFinished (const juce::String& t, bool ok, const juce::String& m) override
        {
            lastTask = t; lastOk = ok; lastMessage = m;
        }

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
            runFor (10);
        return done();
    }

    bool waitTask (Collector& c, const juce::String& task, int timeoutMs)
    {
        c.lastTask = {};
        return runUntil ([&] { return c.lastTask == task; }, timeoutMs);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    // start from an empty library: only inside the temporary HOME of the tests
    {
        auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        if (home.getFileName() == "test-home")
            home.getChildFile ("Documents").getChildFile ("RackAttack").deleteRecursively();
    }

    Session session;
    Engine engine (session);
    engine.pauseScale = 0.05;
    Collector out;
    FakeRackAttack fake;
    fake.engine = &engine;
    engine.addListener (&out);
    engine.setTestOutput ([&fake] (const juce::MidiMessage& m) { fake.receive (m); });

    std::printf ("session: %s\n", session.getFolder().getFullPathName().toRawUTF8());
    CHECK (session.getFolder().getParentDirectory().getFileName() == "Journal");

    // 1. Connect: the unit has ID 5, the app starts with 0 and finds it by broadcast
    engine.connect();
    CHECK (waitTask (out, "connect", 15000));
    CHECK (out.lastOk);
    CHECK (engine.getConnection() == Engine::Connection::connected);
    CHECK (out.connection == Engine::Connection::connected);
    CHECK (engine.deviceId.load() == 5);
    CHECK (engine.channel.load() == 3);
    CHECK (engine.getCurrentProgram() == 3);
    CHECK (engine.getKit().isComplete());
    CHECK (engine.getKit().name() == "PROG 04");
    CHECK (engine.getKit().soundValue (7, 0x27) == 3);
    CHECK (out.lastMessage.contains ("PROG 04"));

    // 2. The front panel: a knob turned on the unit updates the kit
    int before = out.kitChanges;
    fake.answer (ra::soundParameter (5, 2, 0x27, 120));
    runFor (100);
    CHECK (engine.getKit().soundValue (2, 0x27) == 120);
    CHECK (out.kitChanges == before + 1);
    CHECK (engine.getAddressMap().size() == 1);

    // 3. Editing from the app: sent at a safe rate, only the latest value of a knob
    fake.soundParams = 0;
    for (int v = 0; v < 60; ++v)
        engine.setSoundParameter (4, 0x27, v);
    engine.setSoundParameter (4, 0x28, 33);
    runFor (300);
    CHECK (fake.edit.soundValue (4, 0x27) == 59);
    CHECK (fake.edit.soundValue (4, 0x28) == 33);
    CHECK (fake.soundParams.load() <= 4);
    engine.setKitName ("Nouveau");
    runFor (300);
    CHECK (fake.edit.name() == "Nouveau");
    CHECK (engine.getKit().name() == "Nouveau");

    // 4. Changing program
    engine.selectProgram (10);
    CHECK (waitTask (out, "kit", 10000));
    CHECK (fake.current == 10);
    CHECK (engine.getCurrentProgram() == 10);
    CHECK (engine.getKit().name() == "PROG 11");

    // 5. Program names: goes through the 50 programs and comes back to program 11
    engine.readProgramNames();
    CHECK (waitTask (out, "names", 60000));
    CHECK (out.lastOk);
    CHECK (engine.getProgramNames()[0] == "PROG 01");
    CHECK (engine.getProgramNames()[49] == "PROG 50");
    CHECK (fake.current == 10);
    CHECK (engine.getKit().name() == "PROG 11");

    // 5b. A unit that ignores Program Change: said plainly, not 50 copies of the same kit
    fake.ignoresProgramChange = true;
    engine.readProgramNames();
    CHECK (waitTask (out, "names", 60000));
    CHECK (! out.lastOk);
    CHECK (out.lastMessage.contains ("Rx PrgChg"));
    fake.ignoresProgramChange = false;

    // 6. Backup of the 50 programs, then back to program 11
    Library library;
    auto folder = library.newBackupFolder();
    engine.backupAllPrograms (folder);
    CHECK (waitTask (out, "backup", 120000));
    CHECK (out.lastOk);
    CHECK (folder.getNumberOfChildFiles (juce::File::findFiles, "*.syx") == 50);
    CHECK (folder.getChildFile ("noms.txt").loadFileAsString().contains ("P50  PROG 50"));
    CHECK (fake.current == 10);
    CHECK (engine.getKit().name() == "PROG 11");
    auto p20 = Library::loadKit (folder.getChildFile ("P20 - PROG 20.syx"));
    CHECK (p20.has_value() && p20->isComplete());
    CHECK (p20.has_value() && p20->soundValue (5, 0x27) == 19);
    CHECK (library.kits().size() == 50);

    // 7. Sending a kit into the edit buffer, checked by reading back
    if (p20.has_value())
    {
        engine.sendKit (*p20);
        CHECK (waitTask (out, "sendKit", 15000));
        CHECK (out.lastOk);
        CHECK (fake.edit.name() == "PROG 20");
        CHECK (fake.current == 10);                       // nothing stored, still program 11 on the unit
        CHECK (fake.stored[10].name() == "PROG 11");
        CHECK (engine.getKit().differences (*p20).empty());
    }

    // 8. Sounds: save, list, load into another slot
    auto soundFile = library.saveSound (engine.getKit().soundData (5), "Kick test");
    CHECK (soundFile.existsAsFile());
    CHECK (library.sounds().size() == 1);
    auto snd = Library::loadSound (soundFile);
    CHECK (snd.has_value());
    if (snd.has_value())
    {
        engine.sendSound (*snd, 9);
        CHECK (waitTask (out, "sendSound", 10000));
        CHECK (out.lastOk);
        CHECK (fake.edit.soundData (9) == *snd);

        fake.acceptsSoundDumps = false;   // a unit that would ignore sound dumps: reported
        ra::Bytes other (*snd);
        other[0x27] = 1;
        engine.sendSound (other, 9);
        CHECK (waitTask (out, "sendSound", 10000));
        CHECK (! out.lastOk);
        fake.acceptsSoundDumps = true;
    }

    auto kitFile = library.saveKit (engine.getKit(), "Mon kit");
    CHECK (kitFile.getParentDirectory().getFileName() == "Kits");
    CHECK (library.kits()[0].title == "Mon kit");

    // 9. Pattern probes: the right form is remembered for the next time
    engine.probePatterns (4);
    CHECK (waitTask (out, "patterns", 15000));
    CHECK (out.lastOk);
    int sent = fake.sysexIn.load();
    engine.probePatterns (5);
    CHECK (waitTask (out, "patterns", 5000));
    CHECK (fake.sysexIn.load() == sent + 1);

    // 10. Player: a 200 messages/s burst and a 120 BPM rhythm in sixteenths, for one second
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

    // 10b. The sequencer: a kick on each beat and a lock on step 5, sent just before its
    //      note and put back when the clock stops.
    {
        std::vector<juce::MidiMessage> seqSent;
        engine.setTestOutput ([&] (const juce::MidiMessage& m) { seqSent.push_back (m); fake.receive (m); });
        const int base = engine.getKit().soundValue (0, 0x27);
        {
            Sequencer seq (engine);
            seq.stopTimerForTest();
            seq.refreshFromKit();
            seq.edit ([] (ra::seq::Pattern& p)
            {
                p.tempo = 120.0;
                for (int i : { 0, 4, 8, 12 })
                    p.tracks[0].steps[(size_t) i].on = true;
                p.tracks[0].steps[4].setLock (0x27, 101);
            });
            CHECK (! seq.isPlaying());
            seq.play();
            double t = 1000.0;
            for (int i = 0; i < 400; ++i, t += 2.0)     // 800 ms at 120 BPM: steps 1 and 5
                seq.tickForTest (t);
            CHECK (seq.isPlaying());
            seq.stop();
            seq.tickForTest (t);
            CHECK (! seq.isPlaying());
        }
        auto isLock = [] (const juce::MidiMessage& m, int value)
        {
            if (! m.isSysEx() || m.getSysExDataSize() < 10)
                return false;
            auto* d = m.getSysExData();
            return d[3] == ra::cmd::SNDP && d[5] == 0 && d[7] == 0x27 && (value < 0 || d[8] == value);   // after F0: 3E 11 ID CMD BUFN SON PAH PAL VAL
        };
        std::vector<int> order;   // 1 = note of sound 1, 2 = lock 101, 3 = put back
        for (auto& m : seqSent)
        {
            if (m.isNoteOn() && m.getNoteNumber() == 36 && m.getChannel() == engine.channel.load()) order.push_back (1);
            else if (isLock (m, 101)) order.push_back (2);
            else if (isLock (m, base)) order.push_back (3);
        }
        CHECK ((order == std::vector<int> { 1, 2, 1, 3 }));
        if (order != std::vector<int> { 1, 2, 1, 3 })
            for (auto o : order) std::printf ("  order %d\n", o);

        // Following the DAW (real time, blocks every 5 ms): a kick per beat, then the
        // DAW loops back to the start and the kick of beat 1 comes again.
        seqSent.clear();
        {
            Sequencer seq (engine);
            seq.stopTimerForTest();
            seq.refreshFromKit();
            seq.edit ([] (ra::seq::Pattern& p)
            {
                for (int i : { 0, 4, 8, 12 })
                    p.tracks[0].steps[(size_t) i].on = true;
            });
            auto run = [&] (double fromPpq, double ms)
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                for (;;)
                {
                    const auto now = juce::Time::getMillisecondCounterHiRes();
                    if (now - start > ms)
                        break;
                    seq.hostTransport (true, fromPpq + (now - start) / 500.0, 120.0);
                    seq.tickForTest (now);
                    juce::Thread::sleep (5);
                }
            };
            run (0.0, 1150.0);      // beats 1, 2, 3
            CHECK (seq.isFollowingHost());
            run (0.0, 300.0);       // looped back: beat 1 again
            seq.hostTransport (false, 0.0, 120.0);
            seq.tickForTest (juce::Time::getMillisecondCounterHiRes());
            CHECK (! seq.isPlaying());
        }
        int kicks = 0;
        for (auto& m : seqSent)
            kicks += m.isNoteOn() && m.getNoteNumber() == 36 ? 1 : 0;
        std::printf ("following the DAW: %d kicks\n", kicks);
        CHECK (kicks == 4);
        engine.setTestOutput ([&fake] (const juce::MidiMessage& m) { fake.receive (m); });
    }

    // 11. The unit goes quiet: the connection shows it
    fake.silent = true;
    engine.readKit();
    CHECK (runUntil ([&] { return out.connection == Engine::Connection::noAnswer; }, 10000));
    engine.cancel();
    fake.silent = false;

    // 12. Notes and the zip to send
    engine.userNote ("Software 1.04");
    CHECK (out.contains ("NOTE : Software 1.04"));
    runFor (1200);
    auto zip = session.makeZip();
    CHECK (zip.existsAsFile());
    CHECK (session.getFolder().getChildFile ("carte.txt").loadFileAsString().contains ("Cutoff"));

    engine.removeListener (&out);

    if (failures == 0)
        std::printf ("EngineTests: all passed\n");
    return failures == 0 ? 0 : 1;
}
