#include "Sequencer.h"

namespace
{
    constexpr double kLookaheadMs = 40.0;     // planned this far ahead
    constexpr double kLeadMs = 4.2;           // one parameter message on the cable (12 bytes)
    constexpr double kRate = 10000.0;         // sample rate of the timed buffers: 0.1 ms
    constexpr double kHostStaleMs = 400.0;    // the DAW stopped calling: it stopped
    constexpr double kJumpPpq = 0.1;          // further than this from where we expected: a jump
    constexpr double kStartToleranceMs = 15.0;
    constexpr int kTunedChannel = 0x58, kLowKey = 0x5A, kHighKey = 0x5B;

    double msPerPpq (double bpm)   { return 60000.0 / juce::jmax (1.0, bpm); }
}

Sequencer::Sequencer (Engine& e) : engine (e)
{
    lastHit.fill (-1.0);
    lastHitKeyboard.fill (false);
    for (auto& u : upcoming)
        u.reserve (32);
    events.reserve (512);

    engine.onIncomingNote = [this] (const juce::MidiMessage& m) { keyboardNote (m, false); };
    engine.addListener (this);
    refreshFromKit();
    HighResolutionTimer::startTimer (2);
}

Sequencer::~Sequencer()
{
    HighResolutionTimer::stopTimer();
    engine.removeListener (this);
    engine.onIncomingNote = nullptr;
    if (running)
        halt (juce::Time::getMillisecondCounterHiRes());
}

//==============================================================================
juce::String Sequencer::saveToString() const
{
    std::vector<std::uint8_t> data;
    read ([&] (const ra::seq::Pattern& p) { data = p.save(); });
    return juce::Base64::toBase64 (data.data(), data.size());
}

bool Sequencer::loadFromString (const juce::String& text)
{
    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, text))
        return false;
    auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
    std::vector<std::uint8_t> data (bytes, bytes + decoded.getDataSize());
    bool ok = false;
    edit ([&] (ra::seq::Pattern& p) { ok = p.load (data); });
    return ok;
}

//==============================================================================
void Sequencer::play()
{
    ownPlaying = true;
}

void Sequencer::stop()
{
    ownPlaying = false;
}

double Sequencer::getTempo() const
{
    if (followingHost.load())
        return shownBpm.load();
    double t = 120.0;
    read ([&] (const ra::seq::Pattern& p) { t = p.tempo; });
    return t;
}

double Sequencer::playheadPpq() const
{
    auto ppq = shownPpq.load();
    if (ppq < 0.0)
        return -1.0;
    return ppq + (juce::Time::getMillisecondCounterHiRes() - shownMs.load()) / msPerPpq (shownBpm.load());
}

//==============================================================================
void Sequencer::hostTransport (bool playing, double ppq, double bpm)
{
    const juce::SpinLock::ScopedTryLockType sl (hostLock);
    if (! sl.isLocked())
        return;                     // the 2 ms thread is reading: the next block will do
    hostPlaying = playing;
    hostPpqValue = ppq;
    hostBpmValue = bpm > 0.0 ? bpm : 120.0;
    hostStampMs = juce::Time::getMillisecondCounterHiRes();
    ++hostCount;
}

void Sequencer::hostNote (const juce::MidiMessage& m)
{
    if (! m.isNoteOnOrOff() || m.getRawDataSize() < 3)
        return;
    const auto scope = noteFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return;
    auto index = scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2;
    auto* raw = m.getRawData();
    noteData[(size_t) index] = { raw[0], raw[1], raw[2] };
}

void Sequencer::refreshFromKit()
{
    ra::seq::Context c;
    c.channel = juce::jlimit (1, 16, engine.channel.load() <= 0 ? 1 : engine.channel.load());
    c.baseNote = engine.baseNote.load();
    const auto& kit = engine.getKit();
    for (int s = 0; s < ra::seq::numTracks; ++s)
    {
        c.soundKnown[(size_t) s] = kit.hasSound (s);
        if (! kit.hasSound (s))
            continue;
        for (int a = 0; a < ra::seq::numSoundParameters; ++a)
            c.sound[(size_t) s][(size_t) a] = (std::uint8_t) juce::jlimit (0, 127, kit.soundValue (s, a));
        auto tuned = kit.soundValue (s, kTunedChannel);
        c.tunedChannel[(size_t) s] = (std::uint8_t) (tuned >= 1 && tuned <= 16 ? tuned : (tuned == 17 ? c.channel : 0));
    }
    const juce::SpinLock::ScopedLockType sl (lock);
    context = c;
}

void Sequencer::kitChanged (const ra::Kit::Change& c)
{
    // one parameter turned: just that value (knobs move often)
    if (c.what == ra::Kit::Change::What::sound && c.address >= 0 && c.address < ra::seq::numSoundParameters
        && c.address != kTunedChannel && c.index >= 0 && c.index < ra::seq::numTracks)
    {
        const auto& kit = engine.getKit();
        if (kit.hasSound (c.index))
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            context.sound[(size_t) c.index][(size_t) c.address] = (std::uint8_t) juce::jlimit (0, 127, kit.soundValue (c.index, c.address));
            return;
        }
    }
    refreshFromKit();
}

//==============================================================================
void Sequencer::tick (double now)
{
    // notes from the DAW: on to the unit, and flash
    {
        const auto scope = noteFifo.read (noteFifo.getNumReady());
        for (int b = 0; b < 2; ++b)
        {
            auto start = b == 0 ? scope.startIndex1 : scope.startIndex2;
            auto size = b == 0 ? scope.blockSize1 : scope.blockSize2;
            for (int i = 0; i < size; ++i)
            {
                auto& d = noteData[(size_t) (start + i)];
                keyboardNote (juce::MidiMessage (d[0], d[1], d[2]), true);
            }
        }
    }

    // which clock, and where it is
    bool host = false;
    double ppqNow = 0.0, bpm = 120.0;
    {
        const juce::SpinLock::ScopedLockType sl (hostLock);
        host = hostPlaying && now - hostStampMs < kHostStaleMs;
        if (host && hostCount != seenHostCount)
        {
            // follow the DAW smoothly: a small correction each block, a jump when far off
            const bool fresh = ! running || ! drivenByHost || std::abs (hostBpmValue - anchorBpm) > 0.01;
            const double predicted = anchorPpq + (hostStampMs - anchorMs) / msPerPpq (anchorBpm);
            const double error = hostPpqValue - predicted;
            if (fresh || std::abs (error) > kJumpPpq)
            {
                if (running && drivenByHost && std::abs (error) > kJumpPpq)
                    halt (now);   // loop or moved playhead: plan again from there
                anchorPpq = hostPpqValue;
            }
            else
                anchorPpq = predicted + error * 0.1;
            anchorMs = hostStampMs;
            anchorBpm = hostBpmValue;
            seenHostCount = hostCount;
        }
        if (host)
        {
            bpm = anchorBpm;
            ppqNow = anchorPpq + (now - anchorMs) / msPerPpq (bpm);
        }
    }

    if (host)
    {
        if (running && ! drivenByHost)
            halt (now);           // the DAW takes over from the own clock
        ownPlaying = false;
    }
    else if (ownPlaying.load())
    {
        double tempo = 120.0;
        read ([&] (const ra::seq::Pattern& p) { tempo = p.tempo; });
        if (running && drivenByHost)
            halt (now);
        if (! running)
        {
            ownStartMs = now;
            ownStartPpq = 0.0;     // the own clock starts at the top of the pattern
            ownBpm = tempo;
        }
        else if (std::abs (tempo - ownBpm) > 1e-6)
        {
            ownStartPpq += (now - ownStartMs) / msPerPpq (ownBpm);
            ownStartMs = now;
            ownBpm = tempo;
        }
        bpm = ownBpm;
        ppqNow = ownStartPpq + (now - ownStartMs) / msPerPpq (bpm);
    }
    else
    {
        if (running)
            halt (now);
        return;
    }

    // on a (re)start, plan from a little before the playhead: a step that has just
    // begun is played right away rather than skipped
    double fromPpq = ppqNow;
    if (! running)
    {
        player.reset();
        fromPpq = ppqNow - kStartToleranceMs / msPerPpq (bpm);
    }

    events.clear();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        player.render (pattern, context, fromPpq, ppqNow + kLookaheadMs / msPerPpq (bpm), kLeadMs / msPerPpq (bpm), events);
    }
    send (events, ppqNow, now, bpm);

    running = true;
    drivenByHost = host;
    followingHost = host;
    lastPpq = ppqNow;
    shownBpm = bpm;
    shownMs = now;
    shownPpq = juce::jmax (0.0, ppqNow);

    if (now - loadWindowStart >= 500.0)
    {
        // 31250 baud: 3125 bytes per second
        loadPercent = juce::roundToInt (loadBytes / ((now - loadWindowStart) / 1000.0) / 3125.0 * 100.0);
        loadBytes = 0;
        loadWindowStart = now;
    }
}

void Sequencer::halt (double now)
{
    events.clear();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        player.stop (context, lastPpq, events);
    }
    engine.clearTimed();
    for (auto& e : events)
        e.ppq = lastPpq;          // all at once, now
    send (events, lastPpq, now, shownBpm.load());

    running = false;
    followingHost = false;
    shownPpq = -1.0;
    loadPercent = 0;
    const juce::SpinLock::ScopedLockType sl (hitLock);
    for (auto& u : upcoming)
        u.clear();
}

void Sequencer::send (const std::vector<ra::seq::Event>& ev, double ppqNow, double now, double bpm)
{
    if (ev.empty())
        return;

    juce::MidiBuffer buffer;
    const auto device = (std::uint8_t) engine.deviceId.load();
    const auto perPpq = msPerPpq (bpm);
    for (auto& e : ev)
    {
        const double at = juce::jmax (now, now + (e.ppq - ppqNow) * perPpq);
        const int position = (int) std::lround ((at - now) * kRate / 1000.0);
        switch (e.kind)
        {
            case ra::seq::Event::Kind::parameter:
            {
                auto sx = ra::soundParameter (device, e.track, e.address, e.value);
                buffer.addEvent (juce::MidiMessage::createSysExMessage (sx.data() + 1, (int) sx.size() - 2), position);
                loadBytes += (int) sx.size();
                break;
            }
            case ra::seq::Event::Kind::noteOn:
                buffer.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), position);
                loadBytes += 3;
                addHit (e.track, at, false);
                break;
            case ra::seq::Event::Kind::noteOff:
                buffer.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), position);
                loadBytes += 3;
                break;
        }
    }
    engine.sendTimed (buffer, now, kRate);
}

//==============================================================================
void Sequencer::keyboardNote (const juce::MidiMessage& m, bool forward)
{
    int channel = m.getChannel();
    const int note = m.getNoteNumber();

    std::array<std::uint8_t, ra::seq::numTracks> tuned {};
    std::array<std::uint8_t, ra::seq::numTracks> low {}, high {};
    int global = 1, base = 36;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        tuned = context.tunedChannel;
        global = context.channel;
        base = context.baseNote;
        for (size_t s = 0; s < (size_t) ra::seq::numTracks; ++s)
        {
            low[s] = context.sound[s][kLowKey];
            high[s] = context.sound[s][kHighKey];
        }
    }

    bool isTunedChannel = false;
    for (auto t : tuned)
        isTunedChannel = isTunedChannel || (t != 0 && t == channel && t != global);

    if (forward)
    {
        // from the DAW: the drum notes go on the unit's channel, melodies keep theirs
        if (! isTunedChannel)
            channel = global;
        auto out = m;
        out.setChannel (channel);
        engine.sendQuiet (out);
    }

    if (! m.isNoteOn())
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (channel == global && note >= base && note < base + ra::seq::numTracks)
        addHit (note - base, now, true);
    for (int s = 0; s < ra::seq::numTracks; ++s)
        if (tuned[(size_t) s] != 0 && tuned[(size_t) s] == channel && channel != global
            && (high[(size_t) s] <= low[(size_t) s] || (note >= low[(size_t) s] && note <= high[(size_t) s])))
            addHit (s, now, true);
}

void Sequencer::addHit (int track, double ms, bool keyboard)
{
    if (track < 0 || track >= ra::seq::numTracks)
        return;
    const juce::SpinLock::ScopedLockType sl (hitLock);
    auto& u = upcoming[(size_t) track];
    if (keyboard)
    {
        lastHit[(size_t) track] = ms;
        lastHitKeyboard[(size_t) track] = true;
    }
    else if (u.size() < 32)
        u.push_back (ms);
}

double Sequencer::lastHitMs (int track, bool& fromKeyboard) const
{
    if (track < 0 || track >= ra::seq::numTracks)
        return -1.0;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const juce::SpinLock::ScopedLockType sl (hitLock);
    auto& u = upcoming[(size_t) track];
    for (auto it = u.begin(); it != u.end();)
    {
        if (*it <= now)
        {
            if (*it >= lastHit[(size_t) track] || lastHitKeyboard[(size_t) track])
            {
                lastHit[(size_t) track] = *it;
                lastHitKeyboard[(size_t) track] = false;
            }
            it = u.erase (it);
        }
        else
            ++it;
    }
    fromKeyboard = lastHitKeyboard[(size_t) track];
    return lastHit[(size_t) track];
}
