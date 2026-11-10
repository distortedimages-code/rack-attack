// End-to-end tests of the app engine and library against a fake RackAttack:
// Sysex ID 5, 50 stored programs, Program Change, an edit buffer that takes
// dumps and parameter changes. Runs in a temporary HOME.

#include <JuceHeader.h>

#include "../Source/app/Engine.h"
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
