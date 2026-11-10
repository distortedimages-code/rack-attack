#include "Engine.h"
#include "Text.h"

namespace
{
    constexpr int kAnyRackAttack = 0x100;

    juce::String twoDigits (int n)   { return juce::String (n).paddedLeft ('0', 2); }

    juce::String hexLines (const ra::Bytes& b)
    {
        // 32 bytes per line, indented under the description
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
}

Engine::Engine (Session& s) : session (s)
{
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
    if (identifier.isEmpty())
        return {};

    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
        return "Impossible d'ouvrir l'entrée MIDI."_fr;
    input->start();
    log ("Entrée MIDI : "_fr + input->getName());
    return {};
}

juce::String Engine::openOutput (const juce::String& identifier)
{
    {
        const juce::ScopedLock sl (outLock);
        output.reset();
    }
    if (identifier.isEmpty())
        return {};

    auto out = juce::MidiOutput::openDevice (identifier);
    if (out == nullptr)
        return "Impossible d'ouvrir la sortie MIDI.";
    log ("Sortie MIDI : " + out->getName());
    const juce::ScopedLock sl (outLock);
    output = std::move (out);
    return {};
}

bool Engine::hasOutput() const
{
    const juce::ScopedLock sl (outLock);
    return output != nullptr || testOutput != nullptr;
}

void Engine::sendNow (const juce::MidiMessage& m)
{
    const juce::ScopedLock sl (outLock);
    if (testOutput != nullptr)
        testOutput (m);
    else if (output != nullptr)
        output->sendMessageNow (m);
}

void Engine::setTestOutput (std::function<void (const juce::MidiMessage&)> sink)
{
    const juce::ScopedLock sl (outLock);
    testOutput = std::move (sink);
}

//==============================================================================
void Engine::log (const juce::String& text)
{
    session.line (text);
    if (listener != nullptr)
        listener->journalLine (Session::timeStamp() + "  " + text);
}

void Engine::status (const juce::String& text)
{
    if (listener != nullptr)
        listener->statusChanged (text);
}

void Engine::userNote (const juce::String& text)
{
    if (text.trim().isNotEmpty())
        log ("NOTE : " + text.trim());
}

void Engine::sendSysex (const ra::Bytes& sysex, const juce::String& why)
{
    if (! hasOutput())
    {
        log ("Pas de sortie MIDI choisie : rien n'est parti.");
        return;
    }
    if (sysex.size() < 2 || sysex.front() != ra::kSox || sysex.back() != ra::kEox)
    {
        log ("Message refusé : un SysEx commence par F0 et finit par F7."_fr);
        return;
    }
    sendNow (juce::MidiMessage::createSysExMessage (sysex.data() + 1, (int) sysex.size() - 2));
    log ("→ "_fr + juce::String (ra::describe (sysex)) + (why.isNotEmpty() ? "  [" + why + "]" : juce::String())
         + hexLines (sysex));
}

void Engine::sendShort (const juce::MidiMessage& m, bool shouldLog)
{
    sendNow (m);
    if (shouldLog)
        log ("→ "_fr + m.getDescription());
}

void Engine::playSound (int sound)
{
    int note = juce::jlimit (0, 127, baseNote.load() + sound);
    int ch = channel.load();
    sendShort (juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100));
    const juce::ScopedLock sl (playerLock);
    noteOffs.push_back ({ note, ch, juce::Time::getMillisecondCounterHiRes() + 60.0 });
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
        log ("Pas encore de dump pour le son " + twoDigits (sound + 1) + " : lis les sons d'abord.");
        return;
    }
    sendSysex (dump, "renvoi du son tel que lu");
}

//==============================================================================
// Read queue

void Engine::queue (Step s)
{
    steps.push_back (std::move (s));
    ++stepsTotal;
}

void Engine::queueRead (const ra::Bytes& request, int expect, const juce::String& label, int timeoutMs)
{
    Step s;
    s.kind = Step::Kind::sendAndWait;
    s.sysex = request;
    s.expect = expect;
    s.ms = timeoutMs;
    s.label = label;
    queue (std::move (s));
}

void Engine::queueSoundsAndEffects()
{
    auto dev = (std::uint8_t) deviceId.load();
    for (int i = 0; i < 24; ++i)
        queueRead (ra::soundRequest (dev, i), ra::cmd::SNDD, "son " + twoDigits (i + 1));
    for (int i = 0; i < 4; ++i)
        queueRead (ra::effectRequest (dev, i), ra::cmd::EFXD, "FX" + juce::String (i + 1));
}

void Engine::readGlobals()   { queueRead (ra::globalRequest ((std::uint8_t) deviceId.load()), ra::cmd::GLBD, "globaux", 2000); }
void Engine::readProgram()   { queueRead (ra::programRequest ((std::uint8_t) deviceId.load()), ra::cmd::PRGD, "programme"); }

void Engine::readSounds()
{
    auto dev = (std::uint8_t) deviceId.load();
    for (int i = 0; i < 24; ++i)
        queueRead (ra::soundRequest (dev, i), ra::cmd::SNDD, "son " + twoDigits (i + 1));
}

void Engine::readEffects()
{
    auto dev = (std::uint8_t) deviceId.load();
    for (int i = 0; i < 4; ++i)
        queueRead (ra::effectRequest (dev, i), ra::cmd::EFXD, "FX" + juce::String (i + 1));
}

void Engine::readAll()
{
    Step t; t.kind = Step::Kind::tag; t.label = {}; queue (t);
    readGlobals();
    readProgram();
    queueSoundsAndEffects();
}

void Engine::readAllPrograms()
{
    int activeProgram = -1;
    {
        const juce::ScopedLock sl (dumpLock);
        // GLBD: F0 3E 11 dev 14 loc <data>; "active program" at data 0x0C (ra-editor)
        if (lastGlobals.size() > 6 + 0x0C + 2)
            activeProgram = lastGlobals[6 + 0x0C];
    }

    auto dev = (std::uint8_t) deviceId.load();
    for (int p = 0; p < 50; ++p)
    {
        Step t; t.kind = Step::Kind::tag; t.label = "P" + twoDigits (p + 1); queue (t);
        Step pc; pc.kind = Step::Kind::sendShort; pc.shortMessage = juce::MidiMessage::programChange (channel.load(), p);
        pc.label = "programme " + twoDigits (p + 1); queue (pc);
        Step w; w.kind = Step::Kind::pause; w.ms = 500; queue (w);
        queueRead (ra::programRequest (dev), ra::cmd::PRGD, "P" + twoDigits (p + 1) + " programme");
        queueSoundsAndEffects();
    }

    Step t; t.kind = Step::Kind::tag; queue (t);
    if (activeProgram >= 0 && activeProgram < 50)
    {
        Step back; back.kind = Step::Kind::sendShort;
        back.shortMessage = juce::MidiMessage::programChange (channel.load(), activeProgram);
        back.label = "retour au programme " + twoDigits (activeProgram + 1);
        queue (back);
    }
    Step d; d.kind = Step::Kind::done; d.label = "Les 50 programmes sont lus (dossier recus/, fichiers P01 à P50)."_fr; queue (d);
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
    int n = 0;
    for (auto& c : candidates)
        queueRead (ra::build (dev, c), kAnyRackAttack, "sonde pattern " + juce::String (++n) + " (son " + twoDigits (s + 1) + ")", 800);
    Step d; d.kind = Step::Kind::done; d.label = "Sondes pattern terminées."_fr; queue (d);
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
    for (auto& [c, label] : candidates)
        queueRead (ra::build (dev, c), kAnyRackAttack, "sonde " + label, 800);
    Step d; d.kind = Step::Kind::done; d.label = "Sondes d'emplacement terminées."_fr; queue (d);
}

void Engine::cancelReads()
{
    if (! isReading())
        return;
    steps.clear();
    waiting = false;
    currentTag = {};
    stepsTotal = stepsDone = 0;
    log ("Lecture arrêtée."_fr);
    status ({});
}

void Engine::runSteps()
{
    auto now = juce::Time::getMillisecondCounterHiRes();

    if (waiting && now > deadline)
    {
        waiting = false;
        if (waitingFor >= 0)
            log ("   pas de réponse ("_fr + waitingLabel + ")");
    }

    while (! waiting && ! steps.empty())
    {
        auto s = std::move (steps.front());
        steps.pop_front();
        ++stepsDone;

        switch (s.kind)
        {
            case Step::Kind::sendAndWait:
                if (! hasOutput())
                {
                    log ("Pas de sortie MIDI choisie : lecture annulée."_fr);
                    steps.clear();
                    stepsTotal = stepsDone = 0;
                    status ({});
                    return;
                }
                sendSysex (s.sysex, s.label);
                waiting = true;
                waitingFor = s.expect;
                waitingLabel = s.label;
                deadline = now + s.ms;
                break;

            case Step::Kind::sendShort:
                sendShort (s.shortMessage);
                break;

            case Step::Kind::pause:
                waiting = true;
                waitingFor = -1;
                deadline = now + s.ms;
                break;

            case Step::Kind::tag:
                currentTag = s.label;
                break;

            case Step::Kind::done:
                log (s.label);
                break;
        }
    }

    if (steps.empty() && ! waiting)
    {
        if (stepsTotal > 0)
        {
            stepsTotal = stepsDone = 0;
            currentTag = {};
            status ("Terminé."_fr);
        }
    }
    else
        status ("Lecture : " + juce::String (stepsDone) + " / " + juce::String (stepsTotal)
                + (waitingLabel.isNotEmpty() && waiting ? "  (" + waitingLabel + ")" : juce::String()));
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
        log ("← "_fr + m.getDescription() + "   (" + juce::String (ra::hex (raw)) + ")");
        return;
    }

    auto p = ra::parse (raw);
    log ("← "_fr + juce::String (ra::describe (raw)) + hexLines (raw));

    if (! p.isRackAttack)
        return;

    auto action = ra::actionOf (p.command);

    if (action == ra::Action::dump)
    {
        session.saveDump (raw, (currentTag.isNotEmpty() ? currentTag + "-" : juce::String()) + dumpName (p));

        const juce::ScopedLock sl (dumpLock);
        if (p.command == ra::cmd::SNDD && p.payload.size() >= 3 && p.payload[2] < 24)
            soundDumps[p.payload[2]] = raw;
        if (p.command == ra::cmd::GLBD)
            lastGlobals = raw;
    }

    if (action == ra::Action::parameter)
        if (auto c = ra::parameterChange (p))
        {
            addressMap.add (*c);
            mapDirty = true;
            if (listener != nullptr)
                listener->addressMapChanged();
        }

    if (waiting && (waitingFor == kAnyRackAttack || waitingFor == p.command))
        waiting = false;
}

void Engine::clearAddressMap()
{
    addressMap.clear();
    mapDirty = true;
    log ("Carte des adresses effacée."_fr);
    if (listener != nullptr)
        listener->addressMapChanged();
}

//==============================================================================
void Engine::timerCallback()
{
    juce::StringArray lines;
    {
        const juce::ScopedLock sl (linesLock);
        lines.swapWith (playerLines);
    }
    for (auto& l : lines)
        log (l);

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
        log ("Pas encore de dump pour le son " + twoDigits (sound + 1) + " : lis les sons d'abord.");
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
            if (now - nextStepMs > 200.0) nextStepMs = now + stepMs;   // fell behind: start again
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
        // per CC: value 0 + note, then 500 ms later value 127 + note, then next CC 500 ms later
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
