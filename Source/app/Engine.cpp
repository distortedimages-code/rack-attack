#include "Engine.h"
#include "Text.h"

namespace
{
    constexpr int kAnyRackAttack = 0x100;
    constexpr int kKitBlocks = 1 + ra::Kit::numSounds + ra::Kit::numEffects;

    juce::String twoDigits (int n)   { return juce::String (n).paddedLeft ('0', 2); }

    juce::String hexLines (const ra::Bytes& b)
    {
        juce::String s;
        for (size_t i = 0; i < b.size(); i += 32)
        {
            ra::Bytes part (b.begin() + (long) i, b.begin() + (long) std::min (b.size(), i + 32));
            s << "\n                " << juce::String (ra::hex (part));
        }
        return s;
    }

    juce::String dumpName (const ra::Parsed& p)
    {
        auto* n = ra::commandName (p.command);
        juce::String name = n != nullptr ? n : "CMD" + juce::String (ra::hex2 (p.command));
        auto obj = ra::objectOf (p.command);
        if ((obj == ra::Object::sound || obj == ra::Object::effect) && p.payload.size() >= 3)
            name << (obj == ra::Object::sound ? "-son" : "-fx") << twoDigits (p.payload[2] + 1);
        return name;
    }

    // Every program read with the same name: the unit did not follow the Program Changes.
    bool allTheSame (const juce::StringArray& names)
    {
        for (auto& n : names)
            if (n != names[0])
                return false;
        return names.size() > 1;
    }

    juce::String programChangeHelp()
    {
        return "Le RackAttack est resté sur le même programme : vérifie Rx PrgChg = On (Global/Utility) "_fr
               "et le canal (Outils)."_fr;
    }

    juce::String fileSafe (const juce::String& s)
    {
        auto t = s.trim().replaceCharacters ("/\\:*?\"<>|", "---------");
        return t.isEmpty() ? juce::String ("sans nom") : t;
    }
}

Engine::Engine (Session& s) : session (s)
{
    for (int i = 0; i < 50; ++i)
        programNames.add ({});
    Timer::startTimer (10);
    HighResolutionTimer::startTimer (1);
}

Engine::~Engine()
{
    HighResolutionTimer::stopTimer();
    Timer::stopTimer();
    if (input != nullptr)
        input->stop();
}

//==============================================================================
juce::String Engine::openInput (const juce::String& identifier)
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
    if (identifier.isNotEmpty())
    {
        input = juce::MidiInput::openDevice (identifier, this);
        if (input == nullptr)
            return "Impossible d'ouvrir l'entrée MIDI."_fr;
        input->start();
        log ("Entrée MIDI : "_fr + input->getName());
    }
    if (! hasInput() || ! hasOutput())
        setConnection (Connection::noPorts);
    return {};
}

juce::String Engine::openOutput (const juce::String& identifier)
{
    {
        const juce::ScopedLock sl (outLock);
        output.reset();
    }
    if (identifier.isNotEmpty())
    {
        auto out = juce::MidiOutput::openDevice (identifier);
        if (out == nullptr)
            return "Impossible d'ouvrir la sortie MIDI."_fr;
        log ("Sortie MIDI : " + out->getName());
        out->startBackgroundThread();      // for the sequencer's timed messages
        const juce::ScopedLock sl (outLock);
        output = std::move (out);
    }
    if (! hasInput() || ! hasOutput())
        setConnection (Connection::noPorts);
    return {};
}

bool Engine::hasOutput() const
{
    const juce::ScopedLock sl (outLock);
    return output != nullptr || testOutput != nullptr || timedOutput != nullptr;
}

void Engine::sendNow (const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (outLock);
    if (timedOutput != nullptr)
        timedOutput (m, juce::Time::getMillisecondCounterHiRes());
    else if (testOutput != nullptr)
        testOutput (m);
    else if (output != nullptr)
        output->sendMessageNow (m);
}

void Engine::sendTimed (const juce::MidiBuffer& buffer, double startMs, double samplesPerSecond)
{
    const juce::ScopedLock sl (outLock);
    if (timedOutput != nullptr)
    {
        for (const auto meta : buffer)
            timedOutput (meta.getMessage(), startMs + meta.samplePosition * 1000.0 / samplesPerSecond);
    }
    else if (testOutput != nullptr)
    {
        for (const auto meta : buffer)
            testOutput (meta.getMessage());
    }
    else if (output != nullptr)
        output->sendBlockOfMessages (buffer, startMs, samplesPerSecond);
}

void Engine::clearTimed()
{
    const juce::ScopedLock sl (outLock);
    if (output != nullptr)
        output->clearAllPendingMessages();
}

void Engine::setTimedOutput (std::function<void (const juce::MidiMessage&, double)> sink)
{
    {
        const juce::ScopedLock sl (outLock);
        timedOutput = std::move (sink);
    }
    if (! hasInput() || ! hasOutput())
        setConnection (Connection::noPorts);
}

void Engine::setTestOutput (std::function<void (const juce::MidiMessage&)> sink)
{
    const juce::ScopedLock sl (outLock);
    testOutput = std::move (sink);
}

//==============================================================================
void Engine::log (const juce::String& text, bool showInJournal)
{
    session.line (text);
    if (showInJournal)
    {
        // on screen: the description only; the bytes stay in the journal file
        auto line = Session::timeStamp() + "  " + text.upToFirstOccurrenceOf ("\n", false, false);
        listeners.call ([&] (Listener& l) { l.journalLine (line); });
    }
}

void Engine::status (const juce::String& text)
{
    listeners.call ([&] (Listener& l) { l.statusChanged (text); });
}

void Engine::progress (int done, int total, const juce::String& what)
{
    listeners.call ([&] (Listener& l) { l.progressChanged (done, total, what); });
}

void Engine::finishTask (const juce::String& task, bool ok, const juce::String& message)
{
    log ((ok ? juce::String() : "Problème : "_fr) + message);
    status (message);
    listeners.call ([&] (Listener& l) { l.taskFinished (task, ok, message); });
}

void Engine::setConnection (Connection c)
{
    if (connection == c)
        return;
    connection = c;
    listeners.call ([c] (Listener& l) { l.connectionChanged (c); });
}

void Engine::userNote (const juce::String& text)
{
    if (text.trim().isNotEmpty())
        log ("NOTE : " + text.trim());
}

void Engine::sendSysex (const ra::Bytes& sysex, const juce::String& why, bool showInJournal)
{
    if (! hasOutput())
    {
        log ("Pas de sortie MIDI choisie : rien n'est parti."_fr);
        return;
    }
    if (sysex.size() < 2 || sysex.front() != ra::kSox || sysex.back() != ra::kEox)
    {
        log ("Message refusé : un SysEx commence par F0 et finit par F7."_fr);
        return;
    }
    sendNow (juce::MidiMessage::createSysExMessage (sysex.data() + 1, (int) sysex.size() - 2));
    log ("→ "_fr + juce::String (ra::describe (sysex)) + (why.isNotEmpty() ? "  [" + why + "]" : juce::String())
         + hexLines (sysex), showInJournal);
}

void Engine::sendShort (const juce::MidiMessage& m, bool shouldLog)
{
    sendNow (m);
    log ("→ "_fr + m.getDescription(), shouldLog);
}

void Engine::playSound (int sound)
{
    int note = juce::jlimit (0, 127, baseNote.load() + sound);
    int ch = channel.load();
    sendNow (juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100));
    const juce::ScopedLock sl (playerLock);
    noteOffs.push_back ({ note, ch, juce::Time::getMillisecondCounterHiRes() + 80.0 });
}

bool Engine::hasSoundDump (int sound) const
{
    const juce::ScopedLock sl (dumpLock);
    return sound >= 0 && sound < 24 && ! soundDumps[(size_t) sound].empty();
}

void Engine::resendSoundDump (int sound)
{
    ra::Bytes dump;
    {
        const juce::ScopedLock sl (dumpLock);
        if (sound >= 0 && sound < 24)
            dump = soundDumps[(size_t) sound];
    }
    if (dump.empty())
    {
        log ("Pas encore de dump pour le son "_fr + twoDigits (sound + 1) + " : lis le kit d'abord.");
        return;
    }
    sendSysex (dump, "renvoi du son tel que lu");
}

//==============================================================================
// Steps

Engine::Step Engine::request (const ra::Bytes& sysex, int expect, const juce::String& label, int timeoutMs,
                              std::function<void (const ra::Parsed*)> after)
{
    Step s;
    s.kind = Step::Kind::request;
    s.sysex = sysex;
    s.expect = expect;
    s.ms = timeoutMs;
    s.label = label;
    s.after = std::move (after);
    return s;
}

Engine::Step Engine::callStep (std::function<void()> fn)
{
    Step s;
    s.kind = Step::Kind::call;
    s.call = std::move (fn);
    return s;
}

void Engine::cancel()
{
    bool wasBusy = isBusy();
    steps.clear();
    waiting = false;
    waitingAfter = nullptr;
    capturing = false;
    if (wasBusy)
        finishTask ("cancel", false, "Arrêté."_fr);
}

void Engine::runSteps()
{
    auto now = juce::Time::getMillisecondCounterHiRes();

    if (waiting && now > deadline)
    {
        waiting = false;
        auto after = std::move (waitingAfter);
        waitingAfter = nullptr;
        if (waitingFor >= 0)
        {
            log ("   pas de réponse ("_fr + waitingLabel + ")");
            if (++timeouts >= 2 && connection == Connection::connected)
                setConnection (Connection::noAnswer);
        }
        if (after)
            after (nullptr);
    }

    while (! waiting && ! steps.empty())
    {
        auto s = std::move (steps.front());
        steps.pop_front();

        switch (s.kind)
        {
            case Step::Kind::request:
                if (! hasOutput())
                {
                    steps.clear();
                    capturing = false;
                    finishTask ("ports", false, "Pas de sortie MIDI choisie."_fr);
                    return;
                }
                sendSysex (s.sysex, s.label, ! s.quiet);
                waiting = true;
                waitingFor = s.expect;
                waitingLabel = s.label;
                waitingAfter = std::move (s.after);
                probeIndex = s.probe;
                deadline = now + s.ms;
                break;

            case Step::Kind::send:
                sendSysex (s.sysex, s.label, ! s.quiet);
                break;

            case Step::Kind::sendShort:
                sendShort (s.shortMessage, ! s.quiet);
                break;

            case Step::Kind::pause:
                waiting = true;
                waitingFor = -1;
                waitingAfter = nullptr;
                deadline = now + s.ms * pauseScale;
                break;

            case Step::Kind::call:
                if (s.call)
                    s.call();
                break;
        }
    }
}

//==============================================================================
// Connection and kit

void Engine::connect()
{
    if (! hasOutput() || ! hasInput())
    {
        setConnection (Connection::noPorts);
        finishTask ("connect", false, "Choisis l'entrée et la sortie MIDI."_fr);
        return;
    }

    steps.clear();
    waiting = false;
    waitingAfter = nullptr;
    capturing = false;
    timeouts = 0;
    setConnection (Connection::searching);
    status ("Je cherche le RackAttack…"_fr);

    auto found = [this] (const ra::Parsed& p)
    {
        deviceId = p.device;
        // GLBD data (ra-editor layout): 07 = global channel (0 = omni), 0C = program being played
        if (p.payload.size() > 2 + 0x0C)
        {
            int ch = p.payload[2 + 0x07];
            if (ch >= 1 && ch <= 16)
                channel = ch;
            int prog = p.payload[2 + 0x0C];
            if (prog < 50)
                currentProgram = prog;
        }
        timeouts = 0;
        setConnection (Connection::connected);
        log ("RackAttack trouvé : Sysex ID "_fr + juce::String (p.device) + ", canal " + juce::String (channel.load()));
        queueKitRead (false, [this]
        {
            finishTask ("connect", kit.isComplete(),
                        kit.isComplete() ? "Connecté. Kit « "_fr + juce::String (kit.name()) + " » lu."_fr
                                         : "Connecté, mais le kit n'a pas été lu en entier ("_fr + juce::String (kit.blocksRead())
                                               + " / 29). Réessaie avec « Relire »."_fr);
        });
    };

    auto id = (std::uint8_t) deviceId.load();
    queue (request (ra::globalRequest (id), ra::cmd::GLBD, "recherche, ID " + juce::String (id), 1500,
                    [this, found] (const ra::Parsed* p)
    {
        if (p != nullptr)
        {
            found (*p);
            return;
        }
        steps.push_front (request (ra::globalRequest (ra::kBroadcast), ra::cmd::GLBD, "recherche, tous les ID", 1500,
                                   [this, found] (const ra::Parsed* p2)
        {
            if (p2 != nullptr)
                found (*p2);
            else
            {
                steps.clear();
                setConnection (Connection::noAnswer);
                finishTask ("connect", false, "Le RackAttack ne répond pas."_fr);
            }
        }));
    }));
}

void Engine::queueKitRead (bool toCapture, std::function<void()> whenDone)
{
    auto count = std::make_shared<int> (0);
    auto dev = (std::uint8_t) deviceId.load();
    auto what = toCapture ? juce::String ("Lecture du programme") : juce::String ("Lecture du kit");

    queue (callStep ([this, toCapture]
    {
        capturing = toCapture;
        if (toCapture)
            capture = ra::Kit();
    }));

    auto counted = [this, count, what] (const ra::Parsed*)
    {
        progress (++*count, kKitBlocks, what);
    };

    queue (request (ra::programRequest (dev), ra::cmd::PRGD, "programme", 1200, counted));
    for (int i = 0; i < ra::Kit::numSounds; ++i)
        queue (request (ra::soundRequest (dev, i), ra::cmd::SNDD, "son " + twoDigits (i + 1), 1200, counted));
    for (int i = 0; i < ra::Kit::numEffects; ++i)
        queue (request (ra::effectRequest (dev, i), ra::cmd::EFXD, "FX" + juce::String (i + 1), 1200, counted));

    queue (callStep ([this, whenDone]
    {
        if (whenDone)
            whenDone();
        capturing = false;
    }));
}

void Engine::readKit()
{
    queueKitRead (false, [this]
    {
        finishTask ("kit", kit.isComplete(),
                    kit.isComplete() ? "Kit « "_fr + juce::String (kit.name()) + " » lu."_fr
                                     : "Kit lu en partie ("_fr + juce::String (kit.blocksRead()) + " / 29)."_fr);
    });
}

//==============================================================================
// Editing

void Engine::queueParameter (ra::Bytes withoutValue, int value)
{
    for (auto& p : pendingParams)
        if (p.first == withoutValue)
        {
            p.second = value;
            return;
        }
    pendingParams.emplace_back (std::move (withoutValue), value);
}

void Engine::flushParameters()
{
    // two per 10 ms tick: about 200 per second, two thirds of a MIDI cable
    for (int n = 0; n < 2 && ! pendingParams.empty(); ++n)
    {
        auto [payload, value] = pendingParams.front();
        pendingParams.erase (pendingParams.begin());
        payload.push_back ((std::uint8_t) (value & 0x7F));
        sendSysex (ra::build ((std::uint8_t) deviceId.load(), payload), {}, false);
    }
}

void Engine::setSoundParameter (int sound, int address, int value)
{
    if (sound < 0 || sound >= ra::Kit::numSounds)
        return;
    kit.setSoundValue (sound, address, value);
    queueParameter ({ ra::cmd::SNDP, ra::kEditBuffer, (std::uint8_t) sound, (std::uint8_t) (address >> 7), (std::uint8_t) (address & 0x7F) }, value);
    ra::Kit::Change c { ra::Kit::Change::What::sound, sound, address };
    listeners.call ([&] (Listener& l) { l.kitChanged (c); });
}

void Engine::setProgramParameter (int address, int value)
{
    kit.setProgramValue (address, value);
    queueParameter ({ ra::cmd::PRGP, ra::kEditBuffer, (std::uint8_t) (address >> 7), (std::uint8_t) (address & 0x7F) }, value);
    ra::Kit::Change c { ra::Kit::Change::What::program, -1, address };
    listeners.call ([&] (Listener& l) { l.kitChanged (c); });
}

void Engine::setEffectParameter (int effect, int address, int value)
{
    if (effect < 0 || effect >= ra::Kit::numEffects)
        return;
    kit.setEffectValue (effect, address, value);
    queueParameter ({ ra::cmd::EFXP, ra::kEditBuffer, (std::uint8_t) effect, (std::uint8_t) (address >> 7), (std::uint8_t) (address & 0x7F) }, value);
    ra::Kit::Change c { ra::Kit::Change::What::effect, effect, address };
    listeners.call ([&] (Listener& l) { l.kitChanged (c); });
}

void Engine::setKitName (const juce::String& name)
{
    auto text = name.toStdString();
    for (int i = 0; i < 16; ++i)
    {
        int ch = i < (int) text.size() ? (unsigned char) text[(size_t) i] : ' ';
        if (ch < 32 || ch > 126) ch = ' ';
        if (kit.programValue (1 + i) != ch)
            setProgramParameter (1 + i, ch);
    }
}

//==============================================================================
// Programs and library

void Engine::selectProgram (int program)
{
    if (program < 0 || program >= 50)
        return;
    Step pc;
    pc.kind = Step::Kind::sendShort;
    pc.shortMessage = juce::MidiMessage::programChange (channel.load(), program);
    queue (pc);
    queue (callStep ([this, program] { currentProgram = program; }));
    Step wait; wait.kind = Step::Kind::pause; wait.ms = 400; queue (wait);
    readKit();
}

void Engine::readProgramNames()
{
    int back = currentProgram;
    auto dev = (std::uint8_t) deviceId.load();

    queue (callStep ([this] { capturing = true; }));
    for (int p = 0; p < 50; ++p)
    {
        Step pc;
        pc.kind = Step::Kind::sendShort;
        pc.shortMessage = juce::MidiMessage::programChange (channel.load(), p);
        pc.quiet = true;
        queue (pc);
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 350; queue (wait);
        queue (request (ra::programRequest (dev), ra::cmd::PRGD, "nom P" + twoDigits (p + 1), 1200, [this, p] (const ra::Parsed* r)
        {
            if (r != nullptr)
            {
                ra::Kit tmp;
                tmp.apply (*r);
                programNames.set (p, juce::String (tmp.name()));
                listeners.call ([] (Listener& l) { l.programNamesChanged(); });
            }
            progress (p + 1, 50, "Noms des programmes");
        }));
    }
    queue (callStep ([this] { capturing = false; }));

    if (back >= 0)
    {
        Step pc;
        pc.kind = Step::Kind::sendShort;
        pc.shortMessage = juce::MidiMessage::programChange (channel.load(), back);
        queue (pc);
        queue (callStep ([this, back] { currentProgram = back; }));
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 400; queue (wait);
    }
    else
        queue (callStep ([this] { currentProgram = 49; }));

    queueKitRead (false, [this, back]
    {
        int found = 0;
        for (auto& n : programNames)
            if (n.isNotEmpty()) ++found;
        if (found == 50 && allTheSame (programNames))
        {
            finishTask ("names", false, programChangeHelp());
            return;
        }
        finishTask ("names", found > 0, juce::String (found) + " noms lus sur 50."
                    + (back < 0 ? " Le RackAttack est resté sur le programme 50."_fr : juce::String()));
    });
}

void Engine::backupAllPrograms (const juce::File& folder)
{
    folder.createDirectory();
    int back = currentProgram;

    for (int p = 0; p < 50; ++p)
    {
        Step pc;
        pc.kind = Step::Kind::sendShort;
        pc.shortMessage = juce::MidiMessage::programChange (channel.load(), p);
        queue (pc);
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 500; queue (wait);
        queueKitRead (true, [this, p, folder]
        {
            auto name = juce::String (capture.name());
            programNames.set (p, name);
            auto file = folder.getChildFile ("P" + twoDigits (p + 1) + " - " + fileSafe (name) + ".syx");
            auto data = capture.toSyx();
            file.replaceWithData (data.data(), data.size());
            if (! capture.isComplete())
                log ("P"_fr + twoDigits (p + 1) + " : seulement " + juce::String (capture.blocksRead()) + " blocs sur 29.");
            listeners.call ([] (Listener& l) { l.programNamesChanged(); });
            progress (p + 1, 50, "Sauvegarde des 50 programmes");
        });
    }

    if (back >= 0)
    {
        Step pc;
        pc.kind = Step::Kind::sendShort;
        pc.shortMessage = juce::MidiMessage::programChange (channel.load(), back);
        queue (pc);
        queue (callStep ([this, back] { currentProgram = back; }));
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 400; queue (wait);
    }
    else
        queue (callStep ([this] { currentProgram = 49; }));

    queueKitRead (false, [this, folder, back]
    {
        juce::StringArray lines;
        for (int p = 0; p < 50; ++p)
            lines.add ("P" + twoDigits (p + 1) + "  " + programNames[p]);
        folder.getChildFile ("noms.txt").replaceWithText (lines.joinIntoString ("\n"));
        int files = folder.getNumberOfChildFiles (juce::File::findFiles, "*.syx");
        if (allTheSame (programNames))
        {
            finishTask ("backup", false, programChangeHelp());
            return;
        }
        finishTask ("backup", files == 50, juce::String (files) + " programmes sauvegardés dans "_fr + folder.getFileName()
                    + (back < 0 ? ". Le RackAttack est resté sur le programme 50."_fr : juce::String (".")));
    });
}

void Engine::sendKit (const ra::Kit& k)
{
    auto dev = (std::uint8_t) deviceId.load();

    // Program settings one by one (the location bytes of a whole program dump are not
    // known for sure, and a wrong one could write into the unit's memory).
    for (int a = 0; a < ra::kProgramDataSize; ++a)
        if (! kit.hasProgram() || kit.programValue (a) != k.programValue (a))
        {
            Step s;
            s.kind = Step::Kind::send;
            s.sysex = ra::build (dev, { ra::cmd::PRGP, ra::kEditBuffer, (std::uint8_t) (a >> 7), (std::uint8_t) (a & 0x7F),
                                        (std::uint8_t) k.programValue (a) });
            s.quiet = true;
            queue (s);
        }

    for (int i = 0; i < ra::Kit::numSounds; ++i)
    {
        Step s;
        s.kind = Step::Kind::send;
        s.sysex = k.soundDump (dev, i);
        s.label = "envoi son " + twoDigits (i + 1);
        s.quiet = true;
        queue (s);
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 60; queue (wait);
    }
    for (int i = 0; i < ra::Kit::numEffects; ++i)
    {
        Step s;
        s.kind = Step::Kind::send;
        s.sysex = k.effectDump (dev, i);
        s.label = "envoi FX" + juce::String (i + 1);
        s.quiet = true;
        queue (s);
        Step wait; wait.kind = Step::Kind::pause; wait.ms = 40; queue (wait);
    }

    queueKitRead (false, [this, k]
    {
        auto diff = kit.differences (k);
        if (diff.empty())
            finishTask ("sendKit", true, "Kit envoyé et vérifié. Pour le garder sur la machine : Shift + Store."_fr);
        else
        {
            juce::StringArray parts;
            for (auto& d : diff) parts.add (d);
            finishTask ("sendKit", false, "Kit envoyé, mais la machine a autre chose pour : "_fr + parts.joinIntoString (", "));
        }
    });
}

void Engine::sendSound (const ra::Bytes& soundData, int slot)
{
    if (slot < 0 || slot >= ra::Kit::numSounds || (int) soundData.size() != ra::kSoundDataSize)
        return;
    auto dev = (std::uint8_t) deviceId.load();
    ra::Bytes payload { ra::cmd::SNDD, ra::kEditBuffer, (std::uint8_t) slot };
    payload.insert (payload.end(), soundData.begin(), soundData.end());

    Step s;
    s.kind = Step::Kind::send;
    s.sysex = ra::build (dev, payload);
    s.label = "envoi dans le son " + twoDigits (slot + 1);
    queue (s);
    Step wait; wait.kind = Step::Kind::pause; wait.ms = 80; queue (wait);
    queue (request (ra::soundRequest (dev, slot), ra::cmd::SNDD, "vérification du son "_fr + twoDigits (slot + 1), 1200,
                    [this, slot, soundData] (const ra::Parsed* r)
    {
        bool ok = r != nullptr && kit.soundData (slot) == soundData;
        finishTask ("sendSound", ok, ok ? "Son envoyé dans le son "_fr + twoDigits (slot + 1) + " et vérifié."_fr
                                        : "Le son "_fr + twoDigits (slot + 1) + " n'a pas pu être vérifié."_fr);
    }));
}

//==============================================================================
// Test reads (tools page)

void Engine::readGlobals()
{
    queue (request (ra::globalRequest ((std::uint8_t) deviceId.load()), ra::cmd::GLBD, "globaux", 2000));
}

void Engine::readAllForTest()
{
    readGlobals();
    readKit();
}

void Engine::probePatterns (int sound)
{
    auto dev = (std::uint8_t) deviceId.load();
    auto s = (std::uint8_t) juce::jlimit (0, 23, sound);
    const std::vector<ra::Bytes> candidates {
        { ra::cmd::PATR, ra::kEditBuffer, s },
        { ra::cmd::PATR, s },
        { ra::cmd::PATR, 0x00, s },
        { ra::cmd::PATR, s, ra::kEditBuffer },
        { ra::cmd::PATR, ra::kEditBuffer },
        { ra::cmd::PATR, 0x00 },
        { ra::cmd::PATR },
    };
    bool known = workingProbe >= 0 && workingProbe < (int) candidates.size();
    for (int n = 0; n < (int) candidates.size(); ++n)
        if (! known || n == workingProbe)
        {
            auto step = request (ra::build (dev, candidates[(size_t) n]), kAnyRackAttack,
                                 "sonde pattern " + juce::String (n + 1) + " (son " + twoDigits (s + 1) + ")", 800);
            step.probe = n;
            queue (step);
        }
    queue (callStep ([this] { finishTask ("patterns", workingProbe >= 0, workingProbe >= 0 ? "Pattern lu."
                                                                                       : "Aucune réponse aux demandes de pattern."_fr); }));
}

void Engine::probeLocations()
{
    auto dev = (std::uint8_t) deviceId.load();
    const std::vector<std::pair<ra::Bytes, juce::String>> candidates {
        { { ra::cmd::PRGR, 0x00, 0x00 }, "PRGR 00 00 (programme 1 en mémoire ?)"_fr },
        { { ra::cmd::PRGR, 0x01, 0x00 }, "PRGR 01 00 (programme 2 en mémoire ?)"_fr },
        { { ra::cmd::PRGR, 0x00, 0x01 }, "PRGR 00 01" },
        { { ra::cmd::PRGR, ra::kEditBuffer, 0x00 }, "PRGR 20 00" },
        { { ra::cmd::PRGR, 0x01, ra::kEditBuffer }, "PRGR 01 20" },
        { { ra::cmd::SNDR, 0x00, 0x00 }, "SNDR 00 00 (son 1 du programme 1 ?)" },
        { { ra::cmd::SNDR, 0x01, 0x00 }, "SNDR 01 00 (son 1 du programme 2 ?)" },
        { { ra::cmd::EFXR, 0x00, 0x00 }, "EFXR 00 00" },
        { { ra::cmd::GLBR }, "GLBR sans emplacement" },
    };
    // the answers must not land in the kit being played
    queue (callStep ([this] { capturing = true; capture = ra::Kit(); }));
    for (auto& [c, label] : candidates)
        queue (request (ra::build (dev, c), kAnyRackAttack, "sonde " + label, 800));
    queue (callStep ([this] { capturing = false; finishTask ("locations", true, "Sondes d'emplacement terminées."_fr); }));
}

//==============================================================================
// Incoming

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    auto* raw = m.getRawData();
    auto size = m.getRawDataSize();
    if (size <= 0)
        return;
    if (raw[0] == 0xF8) { ++clockCount; return; }
    if (raw[0] == 0xFE) { ++sensingCount; return; }
    if (m.isNoteOnOrOff() && onIncomingNote != nullptr)
        onIncomingNote (m);

    {
        const juce::ScopedLock sl (inLock);
        pending.emplace_back (raw, raw + size);
    }
    triggerAsyncUpdate();
}

void Engine::handleAsyncUpdate()
{
    std::vector<ra::Bytes> batch;
    {
        const juce::ScopedLock sl (inLock);
        batch.swap (pending);
    }
    for (auto& m : batch)
        process (m);
    runSteps();
}

void Engine::process (const ra::Bytes& raw)
{
    if (raw.front() != ra::kSox)
    {
        auto m = juce::MidiMessage (raw.data(), (int) raw.size());
        log ("← "_fr + m.getDescription() + "   (" + juce::String (ra::hex (raw)) + ")", ! m.isNoteOnOrOff());
        return;
    }

    auto p = ra::parse (raw);
    auto action = ra::actionOf (p.command);
    bool isParameter = p.isRackAttack && action == ra::Action::parameter;
    log ("← "_fr + juce::String (ra::describe (raw)) + hexLines (raw), ! isParameter);

    if (! p.isRackAttack)
        return;

    timeouts = 0;

    if (action == ra::Action::dump)
    {
        session.saveDump (raw, dumpName (p));
        if (p.command == ra::cmd::SNDD && p.payload.size() >= 3 && p.payload[2] < 24 && ! capturing)
        {
            const juce::ScopedLock sl (dumpLock);
            soundDumps[p.payload[2]] = raw;
        }
        if (p.command == ra::cmd::GLBD)
            lastGlobals = raw;
        if (p.command == ra::cmd::PATD && probeIndex >= 0)
            workingProbe = probeIndex;
    }

    if (isParameter)
    {
        if (auto c = ra::parameterChange (p))
        {
            addressMap.add (*c);
            mapDirty = true;
            listeners.call ([] (Listener& l) { l.addressMapChanged(); });
        }
    }

    // Dumps go to the program being saved while a backup runs; the front panel
    // (parameter changes) always goes to the kit being played.
    if (capturing && ! isParameter)
        capture.apply (p);
    else
    {
        auto change = kit.apply (p);
        if (change.what != ra::Kit::Change::What::nothing)
            listeners.call ([&] (Listener& l) { l.kitChanged (change); });
    }

    if (waiting && (waitingFor == kAnyRackAttack || waitingFor == p.command))
    {
        waiting = false;
        auto after = std::move (waitingAfter);
        waitingAfter = nullptr;
        if (after)
            after (&p);
    }
}

void Engine::clearAddressMap()
{
    addressMap.clear();
    mapDirty = true;
    log ("Carte des adresses effacée."_fr);
    listeners.call ([] (Listener& l) { l.addressMapChanged(); });
}

//==============================================================================
void Engine::timerCallback()
{
    // answers already received are handled before any time-out is decided
    handleUpdateNowIfNeeded();

    juce::StringArray lines;
    {
        const juce::ScopedLock sl (linesLock);
        lines.swapWith (playerLines);
    }
    for (auto& l : lines)
        log (l);

    flushParameters();
    runSteps();

    auto now = juce::Time::getMillisecondCounterHiRes();
    if (mapDirty && now - lastMapSave > 1000.0)
    {
        session.saveText ("carte.txt", addressMap.table());
        mapDirty = false;
        lastMapSave = now;
    }
}

//==============================================================================
// Player

void Engine::playerLine (const juce::String& text)
{
    const juce::ScopedLock sl (linesLock);
    playerLines.add (text);
}

void Engine::setRhythm (bool on, double bpm, int stepsPerBeat, int sound)
{
    const juce::ScopedLock sl (playerLock);
    rhythmOn = on;
    stepMs = 60000.0 / juce::jlimit (20.0, 400.0, bpm) / juce::jmax (1, stepsPerBeat);
    rhythmSound = juce::jlimit (0, 23, sound);
    nextStepMs = 0;
    playerLine (on ? "Rythme de test : son " + twoDigits (rhythmSound + 1) + ", " + juce::String (bpm, 1) + " BPM, "
                       + juce::String (stepsPerBeat) + " coups par temps"
                   : "Rythme de test arrêté."_fr);
}

void Engine::setBurst (int rate, int sound, int address)
{
    const juce::ScopedLock sl (playerLock);
    burstRate = juce::jlimit (0, 1000, rate);
    burstSound = juce::jlimit (0, 23, sound);
    burstAddress = juce::jlimit (0, 0x3FFF, address);
    nextBurstMs = 0;
    burstValue = 0;
    burstDir = 4;
    auto* pn = ra::parameterName (ra::Object::sound, burstAddress);
    playerLine (rate > 0 ? "Rafale : " + juce::String (rate) + " messages/s sur le son " + twoDigits (burstSound + 1)
                             + ", adresse " + ra::hex2 (burstAddress) + (pn != nullptr ? " (" + juce::String (pn) + ")" : juce::String())
                         : "Rafale arrêtée."_fr);
}

void Engine::setResendLoop (int sound, int intervalMs)
{
    if (intervalMs > 0 && ! hasSoundDump (sound))
    {
        log ("Pas encore de dump pour le son "_fr + twoDigits (sound + 1) + " : lis le kit d'abord.");
        return;
    }
    const juce::ScopedLock sl (playerLock);
    resendSound = juce::jlimit (0, 23, sound);
    resendInterval = juce::jmax (0, intervalMs);
    nextResendMs = 0;
    playerLine (intervalMs > 0 ? "Renvoi du son " + twoDigits (resendSound + 1) + " toutes les " + juce::String (intervalMs) + " ms"
                               : "Renvoi du son arrêté."_fr);
}

void Engine::startCcSweep (int firstCc, int lastCc, int note)
{
    const juce::ScopedLock sl (playerLock);
    sweepOn = true;
    sweepCc = juce::jlimit (0, 127, firstCc);
    sweepLast = juce::jlimit (sweepCc, 127, lastCc);
    sweepNote = juce::jlimit (0, 127, note);
    sweepPhase = 0;
    sweepNextMs = 0;
    playerLine ("Balayage CC " + juce::String (sweepCc) + " à "_fr + juce::String (sweepLast) + " sur le canal "
                + juce::String (channel.load()) + ", note " + juce::String (sweepNote));
}

void Engine::stopPlayer()
{
    const juce::ScopedLock sl (playerLock);
    if (rhythmOn || burstRate > 0 || resendInterval > 0 || sweepOn)
        playerLine ("Tout est arrêté."_fr);
    rhythmOn = false;
    burstRate = 0;
    resendInterval = 0;
    sweepOn = false;
}

void Engine::hiResTimerCallback()
{
    const juce::ScopedLock sl (playerLock);
    auto now = juce::Time::getMillisecondCounterHiRes();
    auto ch = channel.load();
    auto dev = (std::uint8_t) deviceId.load();

    for (auto it = noteOffs.begin(); it != noteOffs.end();)
    {
        if (now >= it->at)
        {
            sendNow (juce::MidiMessage::noteOff (it->channel, it->note));
            it = noteOffs.erase (it);
        }
        else
            ++it;
    }

    if (rhythmOn)
    {
        if (nextStepMs <= 0.0) nextStepMs = now;
        if (now >= nextStepMs)
        {
            int note = juce::jlimit (0, 127, baseNote.load() + rhythmSound);
            sendNow (juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100));
            noteOffs.push_back ({ note, ch, now + 40.0 });
            ++notesSent;
            nextStepMs += stepMs;
            if (now - nextStepMs > 200.0) nextStepMs = now + stepMs;
        }
    }

    if (burstRate > 0)
    {
        if (nextBurstMs <= 0.0) nextBurstMs = now;
        if (now - nextBurstMs > 100.0) nextBurstMs = now;
        while (now >= nextBurstMs)
        {
            burstValue += burstDir;
            if (burstValue >= 127) { burstValue = 127; burstDir = -4; }
            if (burstValue <= 0)   { burstValue = 0;   burstDir = 4; }
            auto m = ra::soundParameter (dev, burstSound, burstAddress, burstValue);
            sendNow (juce::MidiMessage::createSysExMessage (m.data() + 1, (int) m.size() - 2));
            ++burstSent;
            nextBurstMs += 1000.0 / burstRate;
        }
    }

    if (resendInterval > 0)
    {
        if (nextResendMs <= 0.0) nextResendMs = now;
        if (now >= nextResendMs)
        {
            ra::Bytes dump;
            {
                const juce::ScopedLock dl (dumpLock);
                dump = soundDumps[(size_t) resendSound];
            }
            if (! dump.empty())
            {
                sendNow (juce::MidiMessage::createSysExMessage (dump.data() + 1, (int) dump.size() - 2));
                ++resent;
            }
            nextResendMs = now + resendInterval;
        }
    }

    if (sweepOn && now >= sweepNextMs)
    {
        int value = sweepPhase == 0 ? 0 : 127;
        sendNow (juce::MidiMessage::controllerEvent (ch, sweepCc, value));
        sendNow (juce::MidiMessage::noteOn (ch, sweepNote, (juce::uint8) 100));
        noteOffs.push_back ({ sweepNote, ch, now + 40.0 });
        playerLine ("CC " + juce::String (sweepCc) + " = " + juce::String (value) + " puis note " + juce::String (sweepNote));
        sweepNextMs = now + 500.0;

        if (sweepPhase == 0)
            sweepPhase = 1;
        else
        {
            sweepPhase = 0;
            if (++sweepCc > sweepLast)
            {
                sweepOn = false;
                playerLine ("Balayage CC terminé."_fr);
            }
        }
    }

    if (now >= nextSummaryMs)
    {
        if (burstSent > 0 || resent > 0)
            playerLine ("   dernière seconde : "_fr + juce::String (notesSent) + " notes, " + juce::String (burstSent)
                        + " changements de paramètre, "_fr + juce::String (resent) + " sons renvoyés"_fr);
        notesSent = burstSent = resent = 0;
        nextSummaryMs = now + 1000.0;
    }
}
