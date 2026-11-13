#include "MorphControl.h"

#include "Text.h"

MorphControl::MorphControl (Engine& e) : engine (e)
{
    morph.reset (engine.getKit());
    engine.addListener (this);
    engine.editHook = [this] (bool effect, int index, int address, int value) { return takeEdit (effect, index, address, value); };
    engine.shownKitHook = [this] { return shownKit(); };
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (50);
}

MorphControl::~MorphControl()
{
    engine.editHook = nullptr;
    engine.shownKitHook = nullptr;
    engine.removeListener (this);
}

void MorphControl::changed()
{
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        stateDirty = true;
    }
    sendChangeMessage();
}

//==============================================================================
// The fader

void MorphControl::setPosition (double p)
{
    p = juce::jlimit (0.0, 1.0, p);
    if (juce::exactlyEqual (p, morph.position()))
        return;
    morph.setPosition (p);
    if (morph.differs())
        applySoon();
    changed();
}

void MorphControl::glideTo (double p)
{
    target = juce::jlimit (0.0, 1.0, p);
    if (glideMs <= 0)
        setPosition (target);
    else
        changed();          // the knob shows where it goes; the timer takes the sound there
}

void MorphControl::setGlideMs (int ms)
{
    ms = juce::jlimit (0, 2000, ms);
    if (ms == glideMs)
        return;
    glideMs = ms;
    if (onGlideChanged) onGlideChanged (ms);
    changed();
}

//==============================================================================
// A and B

void MorphControl::arm (std::optional<Side> side)
{
    if (side == armed)
        return;
    armed = side;
    shownDirty = true;
    {
        // the pages show the end armed (or the kit heard again)
        const juce::ScopedValueSetter<bool> busy (applying, true);
        engine.notifyKitChanged ({ ra::Kit::Change::What::whole, -1, -1 });
    }
    changed();
}

void MorphControl::loadKit (Side side, const ra::Kit& kit)
{
    morph.load (side, kit);
    shownDirty = true;
    applySoon();
    changed();
}

void MorphControl::loadSound (Side side, int sound, const ra::Bytes& data)
{
    morph.loadSound (side, sound, data);
    shownDirty = true;
    applySoon();
    changed();
}

void MorphControl::copyHeardTo (Side side)   { morph.copyBlendTo (side); shownDirty = true; applySoon(); changed(); }
void MorphControl::swap()                    { morph.swap(); shownDirty = true; applySoon(); changed(); }

void MorphControl::flatten()
{
    const double p = morph.position();
    morph.reset (engine.getKit());
    morph.setPosition (p);
    shownDirty = true;
    changed();
}

bool MorphControl::loadPreset (const std::vector<std::uint8_t>& bytes)
{
    if (! morph.restore (bytes))
        return false;
    target = morph.position();
    restored = false;
    shownDirty = true;
    applySoon();
    changed();
    if (onUserPosition) onUserPosition (target);
    return true;
}

//==============================================================================
// The unit

void MorphControl::applySoon()
{
    // not in the middle of a reading or a sending: its answers would mix with the blend
    if (engine.isBusy())
        reapply = true;
    else
        apply();
}

void MorphControl::apply()
{
    // the settings that differ from the blend go to the unit: a sound with many of them as one dump
    // (quicker than its parameters one by one), the others one by one
    const auto& kit = engine.getKit();
    const juce::ScopedValueSetter<bool> busy (applying, true);
    for (int s = 0; s < ra::Kit::numSounds; ++s)
    {
        if (! kit.hasSound (s))
            continue;
        std::vector<int> diff;
        for (int a = 0; a < ra::kSoundDataSize; ++a)
            if (kit.soundValue (s, a) != morph.sound (s, a))
                diff.push_back (a);
        if (diff.size() > 16)
        {
            ra::Bytes data ((size_t) ra::kSoundDataSize);
            for (int a = 0; a < ra::kSoundDataSize; ++a)
                data[(size_t) a] = (std::uint8_t) morph.sound (s, a);
            engine.loadSoundQuietly (s, data);
        }
        else
            for (auto a : diff)
                engine.setSoundParameter (s, a, morph.sound (s, a));
    }
    for (int e = 0; e < ra::Kit::numEffects; ++e)
    {
        if (! kit.hasEffect (e))
            continue;
        for (int a = 0; a < ra::kEffectDataSize; ++a)
            if (kit.effectValue (e, a) != morph.effect (e, a))
                engine.setEffectParameter (e, a, morph.effect (e, a));
    }
}

void MorphControl::sendHeard (bool effect, int index, int address)
{
    const auto& kit = engine.getKit();
    const juce::ScopedValueSetter<bool> busy (applying, true);
    if (effect)
    {
        if (kit.hasEffect (index) && kit.effectValue (index, address) != morph.effect (index, address))
            engine.setEffectParameter (index, address, morph.effect (index, address));
    }
    else if (kit.hasSound (index) && kit.soundValue (index, address) != morph.sound (index, address))
        engine.setSoundParameter (index, address, morph.sound (index, address));
}

bool MorphControl::takeEdit (bool effect, int index, int address, int value)
{
    // a page's edit with an end armed: into that end; the unit gets what the fader makes of it
    const auto& kit = engine.getKit();
    if (applying || ! armed || ! (effect ? kit.hasEffect (index) : kit.hasSound (index)))
        return false;
    if (effect)
    {
        morph.setEffectEnd (*armed, index, address, value);
        if (! shownDirty) shown.setEffectValue (index, address, morph.effectEnd (*armed, index, address));
    }
    else
    {
        morph.setSoundEnd (*armed, index, address, value);
        if (! shownDirty) shown.setSoundValue (index, address, morph.end (*armed, index, address));
    }
    sendHeard (effect, index, address);
    {
        const juce::ScopedValueSetter<bool> busy (applying, true);
        engine.notifyKitChanged ({ effect ? ra::Kit::Change::What::effect : ra::Kit::Change::What::sound, index, address });
    }
    changed();
    return true;
}

const ra::Kit* MorphControl::shownKit()
{
    if (! armed)
        return nullptr;
    if (shownDirty)
    {
        const auto& kit = engine.getKit();
        shown = kit;
        for (int s = 0; s < ra::Kit::numSounds; ++s)
            if (kit.hasSound (s))
            {
                ra::Bytes data ((size_t) ra::kSoundDataSize);
                for (int a = 0; a < ra::kSoundDataSize; ++a)
                    data[(size_t) a] = (std::uint8_t) morph.end (*armed, s, a);
                shown.setSoundData (s, data);
            }
        for (int e = 0; e < ra::Kit::numEffects; ++e)
            if (kit.hasEffect (e))
                for (int a = 0; a < ra::kEffectDataSize; ++a)
                    shown.setEffectValue (e, a, morph.effectEnd (*armed, e, a));
        shownDirty = false;
    }
    return &shown;
}

void MorphControl::kitChanged (const ra::Kit::Change& c)
{
    using What = ra::Kit::Change::What;
    if (c.what == What::program || c.what == What::whole || c.address < 0)
        shownDirty = true;          // the kit's own settings, or whole blocks
    if (applying)
        return;
    const auto& kit = engine.getKit();
    // one setting changed (a page, the unit's knobs): into the armed end, else at the position
    if (c.what == What::sound && c.address >= 0)
    {
        if (armed)
        {
            morph.setSoundEnd (*armed, c.index, c.address, kit.soundValue (c.index, c.address));
            shownDirty = true;
            sendHeard (false, c.index, c.address);
        }
        else
            morph.editSound (c.index, c.address, kit.soundValue (c.index, c.address));
        changed();
        return;
    }
    if (c.what == What::effect && c.address >= 0)
    {
        if (armed)
        {
            morph.setEffectEnd (*armed, c.index, c.address, kit.effectValue (c.index, c.address));
            shownDirty = true;
            sendHeard (true, c.index, c.address);
        }
        else
            morph.editEffect (c.index, c.address, kit.effectValue (c.index, c.address));
        changed();
        return;
    }
    // whole sounds or effects: taken once the unit is done (a kit comes as 29 dumps)
    if (c.what == What::sound && c.index >= 0 && c.index < ra::Kit::numSounds)
        dumpedSounds[(size_t) c.index] = dumped = true;
    else if (c.what == What::effect && c.index >= 0 && c.index < ra::Kit::numEffects)
        dumpedEffects[(size_t) c.index] = dumped = true;
    else if (c.what == What::whole)
    {
        dumpedSounds.fill (true);
        dumpedEffects.fill (true);
        dumped = true;
    }
}

void MorphControl::taskFinished (const juce::String&, bool, const juce::String&)
{
    if (morph.differs() && ! dumped)
        reapply = true;      // what was held back during the task
}

void MorphControl::takeDumps()
{
    dumped = false;
    const auto& kit = engine.getKit();
    shownDirty = true;
    if (restored)
    {
        // a project's morph: the unit gets the blend back
        dumpedSounds.fill (false);
        dumpedEffects.fill (false);
        reapply = true;
        return;
    }
    const double p = morph.position();
    if (! morph.differs() && ! armed)
    {
        // no morph going: A and B follow the unit
        morph.reset (kit);
        morph.setPosition (p);
    }
    else
    {
        // into the armed end; else at an end into that end, in between into both (the kit heard is then the
        // one that arrived)
        const bool intoA = armed ? *armed == Side::a : p < 1.0;
        const bool intoB = armed ? *armed == Side::b : p > 0.0;
        for (int s = 0; s < ra::Kit::numSounds; ++s)
            if (dumpedSounds[(size_t) s] && kit.hasSound (s))
            {
                if (intoA) morph.loadSound (Side::a, s, kit.soundData (s));
                if (intoB) morph.loadSound (Side::b, s, kit.soundData (s));
                for (int a : { 0x58, 0x5A, 0x5B })
                    morph.setSoundEnd (Side::a, s, a, kit.soundValue (s, a));   // the routing as the unit has it
            }
        for (int e = 0; e < ra::Kit::numEffects; ++e)
            if (dumpedEffects[(size_t) e] && kit.hasEffect (e))
            {
                ra::Bytes data ((size_t) ra::kEffectDataSize);
                for (int a = 0; a < ra::kEffectDataSize; ++a)
                    data[(size_t) a] = (std::uint8_t) kit.effectValue (e, a);
                if (intoA) morph.loadEffect (Side::a, e, data);
                if (intoB) morph.loadEffect (Side::b, e, data);
            }
        if (armed)
            reapply = true;          // what the fader makes of it
    }
    dumpedSounds.fill (false);
    dumpedEffects.fill (false);
    changed();
}

//==============================================================================
void MorphControl::timerCallback()
{
    // a project's morph
    {
        juce::String text;
        {
            const juce::SpinLock::ScopedLockType sl (stateLock);
            if (hasIncoming)
            {
                text = incoming;
                hasIncoming = false;
            }
        }
        juce::MemoryOutputStream out;
        if (text.isNotEmpty() && juce::Base64::convertFromBase64 (out, text))
        {
            std::vector<std::uint8_t> bytes ((const std::uint8_t*) out.getData(), (const std::uint8_t*) out.getData() + out.getDataSize());
            if (morph.restore (bytes))
            {
                target = morph.position();
                restored = morph.differs();     // waits for the unit's kit, if it is not read yet
                reapply = true;
                dumped = false;
                shownDirty = true;
                changed();
                if (onUserPosition) onUserPosition (target);
            }
        }
    }

    // the host's automation, then the glide
    const double host = hostPosition.exchange (-1.0);
    if (host >= 0.0)
        glideTo (host);
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 100.0, now - lastTick);
    lastTick = now;
    const double p = morph.position();
    if (! juce::exactlyEqual (p, target))
    {
        if (glideMs <= 0)
            setPosition (target);
        else
        {
            // three time constants in the glide time: there at 95 %, then the last bit at once
            const double k = 1.0 - std::exp (-dt * 3.0 / glideMs);
            double next = p + (target - p) * k;
            if (std::abs (target - next) < 0.003)
                next = target;
            setPosition (next);
        }
    }

    if (! engine.isBusy())
    {
        if (dumped)
            takeDumps();
        if (reapply)
        {
            reapply = false;
            if (morph.differs())
                apply();
            if (restored && engine.getKit().isComplete())
                restored = false;               // the unit has the project's blend
            changed();
        }
    }

    bool dirty;
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        dirty = stateDirty;
    }
    if (dirty)
        toString();                         // for a host that asks from another thread
}

juce::String MorphControl::toString()
{
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        const auto bytes = morph.differs() ? morph.save() : std::vector<std::uint8_t>();
        const auto text = bytes.empty() ? juce::String() : juce::Base64::toBase64 (bytes.data(), bytes.size());
        const juce::SpinLock::ScopedLockType sl (stateLock);
        state = text;
        stateDirty = false;
        return state;
    }
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return state;         // as the message thread last wrote it
}

void MorphControl::fromString (const juce::String& text)
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    incoming = text;
    hasIncoming = text.isNotEmpty();
}
