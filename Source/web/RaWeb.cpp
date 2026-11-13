// The web version's engine: the plug-in's own kit, pattern, player and simulator, behind a small C interface
// for JavaScript (compiled to WebAssembly by tools/web/build_wasm.sh).
//
// The same module is loaded twice. On the page, the "model": the kit and the pattern being edited, and the
// player, asked every few milliseconds for what to play a little ahead, which it gives as timed MIDI messages
// (notes, and the SysEx parameter changes of the locks), exactly as the plug-in sends them. In the audio
// worklet, the "synth": the simulator, which plays those messages at their sample.
//
// Strings and byte blocks go through one scratch buffer (ra_scratch); JavaScript copies them out at once.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../core/RaKit.h"
#include "../core/RaMorph.h"
#include "../core/RaSeq.h"
#include "../core/RaSysex.h"
#include "../sim/RaSynth.h"

#define RA_EXPORT(name) extern "C" __attribute__ ((export_name (#name)))

namespace
{
    using namespace ra::seq;

    std::vector<std::uint8_t> scratch;

    int toScratch (const std::uint8_t* data, std::size_t size)
    {
        scratch.assign (data, data + size);
        scratch.push_back (0);
        return (int) size;
    }
    int toScratch (const std::string& s)       { return toScratch ((const std::uint8_t*) s.data(), s.size()); }
    int toScratch (const ra::Bytes& b)         { return toScratch (b.data(), b.size()); }

    constexpr int kTunedChannel = 0x58;

    struct Model
    {
        ra::Kit kit;
        Pattern pattern;
        Player player;
        Context context;
        std::vector<Event> events;
        std::uint8_t device = ra::kBroadcast;
        int channel = 1;
        ra::Morph morph;          // kits A and B, as the plug-in's fader (the kit above is the blend heard)
        int armed = -1;           // -1, or the end edits go into (0 A, 1 B)
    };
    Model* model = nullptr;

    struct SynthState
    {
        ra::synth::Synth synth;
        std::vector<float> left, right;
    };
    SynthState* synthState = nullptr;

    Track* trackAt (int t)              { return model != nullptr && t >= 0 && t < numTracks ? &model->pattern.tracks[(size_t) t] : nullptr; }
    Step* stepAt (int t, int s)
    {
        auto* tr = trackAt (t);
        return tr != nullptr && s >= 0 && s < maxSteps ? &tr->steps[(size_t) s] : nullptr;
    }

    using Side = ra::Morph::Side;
    Side sideOf (int i)   { return i == 1 ? Side::b : Side::a; }

    // the kit heard -> the blend: the messages for the unit (and the simulator) one after the other in `out`
    void blendInto (ra::Bytes& out)
    {
        auto& m = model->morph;
        auto& kit = model->kit;
        for (int s = 0; s < ra::Kit::numSounds; ++s)
        {
            if (! kit.hasSound (s))
                continue;
            std::vector<int> diff;
            for (int a = 0; a < ra::kSoundDataSize; ++a)
                if (kit.soundValue (s, a) != m.sound (s, a))
                    diff.push_back (a);
            if (diff.size() > 16)
            {
                // many settings: the whole sound at once, as the plug-in does
                ra::Bytes data ((size_t) ra::kSoundDataSize);
                for (int a = 0; a < ra::kSoundDataSize; ++a)
                    data[(size_t) a] = (std::uint8_t) m.sound (s, a);
                kit.setSoundData (s, data);
                ra::Bytes payload { ra::cmd::SNDD, ra::kEditBuffer, (std::uint8_t) s };
                payload.insert (payload.end(), data.begin(), data.end());
                const auto msg = ra::build (model->device, payload);
                out.insert (out.end(), msg.begin(), msg.end());
            }
            else
                for (auto a : diff)
                {
                    kit.setSoundValue (s, a, m.sound (s, a));
                    const auto msg = ra::soundParameter (model->device, s, a, m.sound (s, a));
                    out.insert (out.end(), msg.begin(), msg.end());
                }
        }
        for (int e = 0; e < ra::Kit::numEffects; ++e)
        {
            if (! kit.hasEffect (e))
                continue;
            for (int a = 0; a < ra::kEffectDataSize; ++a)
                if (kit.effectValue (e, a) != m.effect (e, a))
                {
                    kit.setEffectValue (e, a, m.effect (e, a));
                    const auto msg = ra::build (model->device, { ra::cmd::EFXP, ra::kEditBuffer, (std::uint8_t) e, (std::uint8_t) (a >> 7),
                                                                 (std::uint8_t) (a & 127), (std::uint8_t) m.effect (e, a) });
                    out.insert (out.end(), msg.begin(), msg.end());
                }
        }
    }

    // something from the unit (its knobs, or dumps while its kit is read): into the armed end, else at the
    // position; whole sounds into the end heard (or both in between); with no morph, A and B follow it
    void morphTakes (const ra::Kit::Change& c)
    {
        using What = ra::Kit::Change::What;
        auto& m = model->morph;
        const auto& kit = model->kit;
        if (c.what == What::sound && c.address >= 0)
        {
            if (model->armed >= 0) m.setSoundEnd (sideOf (model->armed), c.index, c.address, kit.soundValue (c.index, c.address));
            else m.editSound (c.index, c.address, kit.soundValue (c.index, c.address));
            return;
        }
        if (c.what == What::effect && c.address >= 0)
        {
            if (model->armed >= 0) m.setEffectEnd (sideOf (model->armed), c.index, c.address, kit.effectValue (c.index, c.address));
            else m.editEffect (c.index, c.address, kit.effectValue (c.index, c.address));
            return;
        }
        if (c.what != What::sound && c.what != What::effect && c.what != What::whole)
            return;
        if (! m.differs() && model->armed < 0)
        {
            const double p = m.position();
            m.reset (kit);
            m.setPosition (p);
            return;
        }
        const double p = m.position();
        const bool intoA = model->armed >= 0 ? model->armed == 0 : p < 1.0;
        const bool intoB = model->armed >= 0 ? model->armed == 1 : p > 0.0;
        auto takeSound = [&] (int s)
        {
            if (! kit.hasSound (s)) return;
            if (intoA) m.loadSound (Side::a, s, kit.soundData (s));
            if (intoB) m.loadSound (Side::b, s, kit.soundData (s));
            for (int a : { 0x58, 0x5A, 0x5B })
                m.setSoundEnd (Side::a, s, a, kit.soundValue (s, a));
        };
        auto takeEffect = [&] (int e)
        {
            if (! kit.hasEffect (e)) return;
            ra::Bytes d;
            for (int a = 0; a < ra::kEffectDataSize; ++a)
                d.push_back ((std::uint8_t) std::clamp (kit.effectValue (e, a), 0, 127));
            if (intoA) m.loadEffect (Side::a, e, d);
            if (intoB) m.loadEffect (Side::b, e, d);
        };
        if (c.what == What::sound) takeSound (c.index);
        else if (c.what == What::effect) takeEffect (c.index);
        else
        {
            for (int s = 0; s < ra::Kit::numSounds; ++s) takeSound (s);
            for (int e = 0; e < ra::Kit::numEffects; ++e) takeEffect (e);
        }
    }

    void refreshContext()
    {
        // as Sequencer::refreshFromKit: the kit's own values (put back after a lock), Tuned Channels
        Context c;
        c.channel = std::clamp (model->channel, 1, 16);
        c.baseNote = 36;
        for (int s = 0; s < numTracks; ++s)
        {
            c.soundKnown[(size_t) s] = model->kit.hasSound (s);
            if (! model->kit.hasSound (s))
                continue;
            for (int a = 0; a < numSoundParameters; ++a)
                c.sound[(size_t) s][(size_t) a] = (std::uint8_t) std::clamp (model->kit.soundValue (s, a), 0, 127);
            const int tuned = model->kit.soundValue (s, kTunedChannel);
            c.tunedChannel[(size_t) s] = (std::uint8_t) (tuned >= 1 && tuned <= 16 ? tuned : (tuned == 17 ? c.channel : 0));
        }
        model->context = c;
    }

    // planned events -> scratch, one record each: f64 ppq, i32 track, i32 kind (0 parameter, 1 on, 2 off),
    // i32 size, the MIDI bytes, padded to 4
    int eventsToScratch()
    {
        scratch.clear();
        auto put = [] (const void* p, std::size_t n) { auto* b = (const std::uint8_t*) p; scratch.insert (scratch.end(), b, b + n); };
        for (auto& e : model->events)
        {
            ra::Bytes bytes;
            int kind = 0;
            switch (e.kind)
            {
                case Event::Kind::parameter:
                    bytes = ra::soundParameter (model->device, e.track, e.address, e.value);
                    break;
                case Event::Kind::noteOn:
                    kind = 1;
                    bytes = { (std::uint8_t) (0x90 | ((e.channel - 1) & 15)), (std::uint8_t) (e.note & 127), (std::uint8_t) (e.velocity & 127) };
                    break;
                case Event::Kind::noteOff:
                    kind = 2;
                    bytes = { (std::uint8_t) (0x80 | ((e.channel - 1) & 15)), (std::uint8_t) (e.note & 127), 0 };
                    break;
            }
            while (scratch.size() % 8 != 0)
                scratch.push_back (0);
            const double ppq = e.ppq;
            const std::int32_t head[3] = { e.track, kind, (std::int32_t) bytes.size() };
            put (&ppq, sizeof (ppq));
            put (head, sizeof (head));
            put (bytes.data(), bytes.size());
        }
        while (scratch.size() % 8 != 0)
            scratch.push_back (0);
        return (int) model->events.size();
    }
}

//==============================================================================
// Memory

RA_EXPORT (ra_scratch) std::uint8_t* raScratch()             { return scratch.data(); }
RA_EXPORT (ra_scratch_size) int raScratchSize()               { return (int) scratch.size(); }
RA_EXPORT (ra_alloc) void* raAlloc (int size)                 { return new std::uint8_t[(size_t) std::max (1, size)]; }
RA_EXPORT (ra_free) void raFree (void* p)                     { delete[] (std::uint8_t*) p; }

//==============================================================================
// The model: kit and pattern

RA_EXPORT (ra_model_init) void raModelInit (int program)
{
    delete model;
    model = new Model();
    ra::fillDemoKit (model->kit, std::clamp (program, 0, 49));
    fillDemoPattern (model->pattern);
    model->morph.reset (model->kit);
    refreshContext();
}

RA_EXPORT (ra_set_device) void raSetDevice (int device, int channel)
{
    model->device = (std::uint8_t) std::clamp (device, 0, 127);
    model->channel = std::clamp (channel, 1, 16);
    refreshContext();
}

RA_EXPORT (ra_kit_name) int raKitName()                       { return toScratch (model->kit.name()); }
RA_EXPORT (ra_kit_set_name) void raKitSetName (const char* n) { model->kit.setName (n); }
RA_EXPORT (ra_kit_has_sound) int raKitHasSound (int s)        { return model->kit.hasSound (s) ? 1 : 0; }
RA_EXPORT (ra_kit_blocks_read) int raKitBlocksRead()          { return model->kit.blocksRead(); }
RA_EXPORT (ra_sound_get) int raSoundGet (int s, int a)        { return model->kit.soundValue (s, a); }
RA_EXPORT (ra_fx_get) int raFxGet (int e, int a)              { return model->kit.effectValue (e, a); }
RA_EXPORT (ra_program_get) int raProgramGet (int a)           { return model->kit.programValue (a); }

// one sound value changed here: the kit, the player's base values, and the SysEx to send (in scratch)
RA_EXPORT (ra_sound_set) int raSoundSet (int s, int a, int v)
{
    v = std::clamp (v, 0, 127);
    model->kit.setSoundValue (s, a, v);
    if (s >= 0 && s < numTracks && a >= 0 && a < numSoundParameters)
    {
        model->context.sound[(size_t) s][(size_t) a] = (std::uint8_t) v;
        if (a == kTunedChannel)
            refreshContext();
    }
    return toScratch (ra::soundParameter (model->device, s, a, v));
}

RA_EXPORT (ra_fx_set) int raFxSet (int e, int a, int v)
{
    v = std::clamp (v, 0, 127);
    model->kit.setEffectValue (e, a, v);
    // as the plug-in's Engine sends it
    return toScratch (ra::build (model->device, { ra::cmd::EFXP, ra::kEditBuffer, (std::uint8_t) e,
                                                  (std::uint8_t) (a >> 7), (std::uint8_t) (a & 127), (std::uint8_t) v }));
}

RA_EXPORT (ra_sound_data) int raSoundData (int s)
{
    const auto& d = model->kit.soundData (s);
    return toScratch (d);
}

RA_EXPORT (ra_fx_data) int raFxData (int e)
{
    ra::Bytes d;
    for (int a = 0; a < ra::kEffectDataSize; ++a)
        d.push_back ((std::uint8_t) std::clamp (model->kit.effectValue (e, a), 0, 127));
    return toScratch (d);
}

//==============================================================================
// The morph (as the plug-in's fader): A, B, the position; edits into the armed end or at the position.
// The functions that change what is heard give back the messages to send (in scratch, one after the other).

RA_EXPORT (ra_morph_differs) int raMorphDiffers()                 { return model->morph.differs() ? 1 : 0; }
RA_EXPORT (ra_morph_position) double raMorphPosition()            { return model->morph.position(); }
RA_EXPORT (ra_morph_armed) int raMorphArmed()                     { return model->armed; }
RA_EXPORT (ra_morph_arm) void raMorphArm (int side)               { model->armed = side == 0 || side == 1 ? side : -1; }
RA_EXPORT (ra_morph_end) int raMorphEnd (int side, int s, int a)  { return model->morph.end (sideOf (side), s, a); }

// what the pages show: the end armed, else the kit heard
RA_EXPORT (ra_shown_sound) int raShownSound (int s, int a)
{
    return model->armed >= 0 && model->kit.hasSound (s) ? model->morph.end (sideOf (model->armed), s, a) : model->kit.soundValue (s, a);
}
RA_EXPORT (ra_shown_fx) int raShownFx (int e, int a)
{
    return model->armed >= 0 && model->kit.hasEffect (e) ? model->morph.effectEnd (sideOf (model->armed), e, a) : model->kit.effectValue (e, a);
}

static int blendToScratch()
{
    ra::Bytes out;
    blendInto (out);
    refreshContext();
    return toScratch (out);
}

RA_EXPORT (ra_morph_set_position) int raMorphSetPosition (double p)  { model->morph.setPosition (p); return blendToScratch(); }
RA_EXPORT (ra_morph_apply) int raMorphApply()                         { return blendToScratch(); }
RA_EXPORT (ra_morph_copy_heard) int raMorphCopyHeard (int side)       { model->morph.copyBlendTo (sideOf (side)); return blendToScratch(); }
RA_EXPORT (ra_morph_swap) int raMorphSwap()                           { model->morph.swap(); return blendToScratch(); }
RA_EXPORT (ra_morph_flatten) void raMorphFlatten()
{
    const double p = model->morph.position();
    model->morph.reset (model->kit);
    model->morph.setPosition (p);
}
RA_EXPORT (ra_morph_load_demo) int raMorphLoadDemo (int side, int program)
{
    ra::Kit k;
    ra::fillDemoKit (k, std::clamp (program, 0, 49));
    model->morph.load (sideOf (side), k);
    return blendToScratch();
}
RA_EXPORT (ra_morph_load_kit) int raMorphLoadKit (int side, const std::uint8_t* data, int size)
{
    auto k = ra::Kit::fromSyx (ra::Bytes (data, data + size));
    if (! k) return -1;
    model->morph.load (sideOf (side), *k);
    return blendToScratch();
}
RA_EXPORT (ra_morph_save) int raMorphSave()                           { return toScratch (model->morph.save()); }
RA_EXPORT (ra_morph_restore) int raMorphRestore (const std::uint8_t* data, int size)
{
    if (! model->morph.restore (std::vector<std::uint8_t> (data, data + size)))
        return -1;
    return blendToScratch();
}

// an edit from the page: into the armed end (heard as far as the fader goes there), else at the position
RA_EXPORT (ra_sound_edit) int raSoundEdit (int s, int a, int v)
{
    v = std::clamp (v, 0, 127);
    if (model->armed >= 0 && model->kit.hasSound (s))
        model->morph.setSoundEnd (sideOf (model->armed), s, a, v);
    else
    {
        model->morph.editSound (s, a, v);
        model->kit.setSoundValue (s, a, v);       // exactly what was asked, heard at once
    }
    ra::Bytes out;
    const int before = model->kit.soundValue (s, a);
    if (model->armed >= 0)
    {
        const int heard = model->morph.sound (s, a);
        if (heard != before)
        {
            model->kit.setSoundValue (s, a, heard);
            out = ra::soundParameter (model->device, s, a, heard);
        }
    }
    else
        out = ra::soundParameter (model->device, s, a, v);
    refreshContext();
    return toScratch (out);
}

RA_EXPORT (ra_fx_edit) int raFxEdit (int e, int a, int v)
{
    v = std::clamp (v, 0, 127);
    int heard = v;
    if (model->armed >= 0 && model->kit.hasEffect (e))
    {
        model->morph.setEffectEnd (sideOf (model->armed), e, a, v);
        heard = model->morph.effect (e, a);
    }
    else
        model->morph.editEffect (e, a, v);
    if (model->kit.effectValue (e, a) == heard && model->armed >= 0)
        return toScratch (ra::Bytes());
    model->kit.setEffectValue (e, a, heard);
    return toScratch (ra::build (model->device, { ra::cmd::EFXP, ra::kEditBuffer, (std::uint8_t) e,
                                                  (std::uint8_t) (a >> 7), (std::uint8_t) (a & 127), (std::uint8_t) heard }));
}

// a message from the unit (dumps while reading its kit): 0 nothing, 1 program, 2 sound, 3 effect, 4 whole
RA_EXPORT (ra_kit_apply) int raKitApply (const std::uint8_t* data, int size)
{
    ra::Bytes b (data, data + size);
    const auto p = ra::parse (b);
    if (! p.isRackAttack || p.check == ra::Parsed::Check::bad)
        return 0;
    const auto change = model->kit.apply (p);
    if (change.what != ra::Kit::Change::What::nothing)
    {
        morphTakes (change);
        refreshContext();
    }
    return (int) change.what;
}

// the unit's answer to the global request: device id * 256 + channel, -1 if it isn't one
RA_EXPORT (ra_parse_global) int raParseGlobal (const std::uint8_t* data, int size)
{
    ra::Bytes b (data, data + size);
    const auto p = ra::parse (b);
    if (! p.isRackAttack || p.command != ra::cmd::GLBD || p.payload.size() <= 2 + 0x07)
        return -1;
    const int ch = p.payload[2 + 0x07];
    return p.device * 256 + (ch >= 1 && ch <= 16 ? ch : 1);
}

RA_EXPORT (ra_msg_global_request) int raMsgGlobalRequest()          { return toScratch (ra::globalRequest (ra::kBroadcast)); }
RA_EXPORT (ra_msg_program_request) int raMsgProgramRequest()        { return toScratch (ra::programRequest (model->device)); }
RA_EXPORT (ra_msg_sound_request) int raMsgSoundRequest (int s)      { return toScratch (ra::soundRequest (model->device, s)); }
RA_EXPORT (ra_msg_effect_request) int raMsgEffectRequest (int e)    { return toScratch (ra::effectRequest (model->device, e)); }
RA_EXPORT (ra_msg_sound_dump) int raMsgSoundDump (int s)            { return toScratch (model->kit.soundDump (model->device, s)); }
RA_EXPORT (ra_msg_effect_dump) int raMsgEffectDump (int e)          { return toScratch (model->kit.effectDump (model->device, e)); }
RA_EXPORT (ra_msg_program_dump) int raMsgProgramDump()              { return toScratch (model->kit.programDump (model->device)); }

// names from the protocol notes: object 0 sound, 4 effect; one per line for the choices
RA_EXPORT (ra_param_name) int raParamName (int object, int a)
{
    const char* n = ra::parameterName ((ra::Object) object, a);
    return toScratch (std::string (n != nullptr ? n : ""));
}

RA_EXPORT (ra_value_names) int raValueNames (int object, int a)
{
    std::string all;
    for (auto& n : ra::valueNames ((ra::Object) object, a))
        all += n + "\n";
    return toScratch (all);
}

//==============================================================================
// The pattern

RA_EXPORT (ra_tempo) double raTempo()                 { return model->pattern.tempo; }
RA_EXPORT (ra_set_tempo) void raSetTempo (double t)   { model->pattern.tempo = std::clamp (t, 30.0, 300.0); }
RA_EXPORT (ra_swing) int raSwing()                    { return model->pattern.swing; }
RA_EXPORT (ra_set_swing) void raSetSwing (int s)      { model->pattern.swing = (std::uint8_t) std::clamp (s, 50, 75); }

// track fields: 0 mode, 1 length, 2 division, 3 mute, 4 euclid steps, 5 euclid hits, 6 euclid rotation,
// 7 active length, 8 trigs, 9 locks
RA_EXPORT (ra_track_get) int raTrackGet (int t, int field)
{
    auto* tr = trackAt (t);
    if (tr == nullptr) return 0;
    switch (field)
    {
        case 0: return (int) tr->mode;
        case 1: return tr->length;
        case 2: return (int) tr->division;
        case 3: return tr->mute ? 1 : 0;
        case 4: return tr->euclid.steps;
        case 5: return tr->euclid.hits;
        case 6: return tr->euclid.rotation;
        case 7: return tr->activeLength();
        case 8: return tr->countTrigs();
        case 9: return tr->countLocks();
        default: return 0;
    }
}

RA_EXPORT (ra_track_set) void raTrackSet (int t, int field, int v)
{
    auto* tr = trackAt (t);
    if (tr == nullptr) return;
    switch (field)
    {
        case 0: tr->mode = (Mode) std::clamp (v, 0, 2); break;
        case 1: tr->length = (std::uint8_t) std::clamp (v, 1, maxSteps); break;
        case 2: tr->division = (Division) std::clamp (v, 0, numDivisions - 1); break;
        case 3: tr->mute = v != 0; break;
        case 4: tr->euclid.steps = (std::uint8_t) std::clamp (v, 1, maxSteps); tr->euclid.hits = std::min (tr->euclid.hits, tr->euclid.steps); break;
        case 5: tr->euclid.hits = (std::uint8_t) std::clamp (v, 0, (int) tr->euclid.steps); break;
        case 6: tr->euclid.rotation = (std::int8_t) std::clamp (v, -maxSteps + 1, maxSteps - 1); break;
        default: break;
    }
}

RA_EXPORT (ra_track_name) int raTrackName (int t)     { auto* tr = trackAt (t); return toScratch (tr != nullptr ? tr->name : std::string()); }
RA_EXPORT (ra_track_set_name) void raTrackSetName (int t, const char* n)
{
    if (auto* tr = trackAt (t))
    {
        std::string s (n);
        if ((int) s.size() > maxNameBytes)
            s.resize ((size_t) maxNameBytes);
        tr->name = s;
    }
}

// 1 when the track plays at this step (its mode taken into account)
RA_EXPORT (ra_track_is_on) int raTrackIsOn (int t, int s)    { auto* tr = trackAt (t); return tr != nullptr && tr->isOn (s) ? 1 : 0; }

// step fields: 0 on, 1 velocity, 2 note, 3 length, 4 micro, 5 probability, 6 condA, 7 condB, 8 retrig, 9 locks
RA_EXPORT (ra_step_get) int raStepGet (int t, int s, int field)
{
    auto* st = stepAt (t, s);
    if (st == nullptr) return 0;
    switch (field)
    {
        case 0: return st->on ? 1 : 0;
        case 1: return st->velocity;
        case 2: return st->note;
        case 3: return st->length;
        case 4: return st->micro;
        case 5: return st->probability;
        case 6: return st->condA;
        case 7: return st->condB;
        case 8: return st->retrig;
        case 9: return (int) st->locks.size();
        default: return 0;
    }
}

RA_EXPORT (ra_step_set) void raStepSet (int t, int s, int field, int v)
{
    auto* st = stepAt (t, s);
    if (st == nullptr) return;
    switch (field)
    {
        case 0: st->on = v != 0; break;
        case 1: st->velocity = (std::uint8_t) std::clamp (v, 1, 127); break;
        case 2: st->note = (std::uint8_t) std::clamp (v, 0, 127); break;
        case 3: st->length = (std::uint8_t) std::clamp (v, 1, 64); break;
        case 4: st->micro = (std::int8_t) std::clamp (v, -11, 11); break;
        case 5: st->probability = (std::uint8_t) std::clamp (v, 0, 100); break;
        case 6: st->condA = (std::uint8_t) std::clamp (v, 0, 8); break;
        case 7: st->condB = (std::uint8_t) std::clamp (v, 0, 8); break;
        case 8: st->retrig = (std::uint8_t) std::clamp (v, 1, 8); break;
        default: break;
    }
}

// the locked value of a parameter at a step, -1 when it isn't locked; the i-th lock's address
RA_EXPORT (ra_lock_get) int raLockGet (int t, int s, int a)  { auto* st = stepAt (t, s); auto* l = st != nullptr ? st->findLock (a) : nullptr; return l != nullptr ? l->value : -1; }
RA_EXPORT (ra_lock_address) int raLockAddress (int t, int s, int i)
{
    auto* st = stepAt (t, s);
    return st != nullptr && i >= 0 && i < (int) st->locks.size() ? st->locks[(size_t) i].address : -1;
}
RA_EXPORT (ra_lock_set) int raLockSet (int t, int s, int a, int v)  { auto* st = stepAt (t, s); return st != nullptr && st->setLock (a, std::clamp (v, 0, 127)) ? 1 : 0; }
RA_EXPORT (ra_lock_clear) void raLockClear (int t, int s, int a)    { if (auto* st = stepAt (t, s)) st->clearLock (a); }

// OPTIONS: 0 clear steps, 1 clear locks, 2 reset (INIT), 3 shift by arg, 4 fill every arg, 5 print the euclid hits
RA_EXPORT (ra_track_op) void raTrackOp (int t, int op, int arg)
{
    auto* tr = trackAt (t);
    if (tr == nullptr) return;
    switch (op)
    {
        case 0: clearSteps (*tr); break;
        case 1: clearLocks (*tr); break;
        case 2: resetTrack (*tr); break;
        case 3: shiftSteps (*tr, arg); break;
        case 4: fillEvery (*tr, arg); break;
        case 5: tr->printEuclid(); break;
        default: break;
    }
}

// the pattern as the plug-in saves it (undo, the browser's storage), and back
RA_EXPORT (ra_pattern_save) int raPatternSave()                     { return toScratch (model->pattern.save()); }
RA_EXPORT (ra_pattern_load) int raPatternLoad (const std::uint8_t* data, int size)
{
    return model->pattern.load (std::vector<std::uint8_t> (data, data + size)) ? 1 : 0;
}
RA_EXPORT (ra_kit_save) int raKitSave()                             { return toScratch (model->kit.toSyx()); }
RA_EXPORT (ra_kit_load) int raKitLoad (const std::uint8_t* data, int size)
{
    auto k = ra::Kit::fromSyx (ra::Bytes (data, data + size));
    if (! k) return 0;
    model->kit = *k;
    model->morph.reset (model->kit);
    refreshContext();
    return 1;
}

//==============================================================================
// The player

RA_EXPORT (ra_play_reset) void raPlayReset()        { model->player.reset(); }

// what to play from where the last call stopped (or fromPpq after a reset) until untilPpq, as
// timed MIDI in scratch; returns the number of records
RA_EXPORT (ra_plan) int raPlan (double fromPpq, double untilPpq, double leadPpq)
{
    model->events.clear();
    model->player.render (model->pattern, model->context, fromPpq, untilPpq, leadPpq, model->events);
    return eventsToScratch();
}

// stopping: the notes still on, and the locked values put back
RA_EXPORT (ra_stop) int raStop (double atPpq)
{
    model->events.clear();
    model->player.stop (model->context, atPpq, model->events);
    return eventsToScratch();
}

// a note for sound s from the screen (a voice tapped): its channel and note, as the plug-in plays it
RA_EXPORT (ra_note_for) int raNoteFor (int s, int noteIfTuned)
{
    const int tuned = model->context.tunedChannel[(size_t) std::clamp (s, 0, numTracks - 1)];
    if (tuned != 0)
        return (tuned - 1) * 256 + std::clamp (noteIfTuned, 0, 127);
    return (model->context.channel - 1) * 256 + std::clamp (model->context.baseNote + s, 0, 127);
}

//==============================================================================
// The simulator (in the audio worklet)

RA_EXPORT (ra_synth_init) void raSynthInit (double sampleRate, int maxBlock)
{
    delete synthState;
    synthState = new SynthState();
    synthState->synth.prepare (sampleRate);
    synthState->left.assign ((size_t) std::max (1, maxBlock), 0.0f);
    synthState->right.assign ((size_t) std::max (1, maxBlock), 0.0f);
}
RA_EXPORT (ra_synth_set_sound) void raSynthSetSound (int s, const std::uint8_t* d)   { synthState->synth.setSound (s, d); }
RA_EXPORT (ra_synth_set_fx) void raSynthSetFx (int e, const std::uint8_t* d)         { synthState->synth.setEffect (e, d); }
RA_EXPORT (ra_synth_channel) void raSynthChannel (int ch)                            { synthState->synth.setGlobalChannel (ch); }
RA_EXPORT (ra_synth_tempo) void raSynthTempo (double bpm)                            { synthState->synth.setTempo (bpm); }
RA_EXPORT (ra_synth_midi) void raSynthMidi (const std::uint8_t* d, int size)         { synthState->synth.handleMidi (d, size); }
RA_EXPORT (ra_synth_all_off) void raSynthAllOff()                                    { synthState->synth.allNotesOff(); }
RA_EXPORT (ra_synth_level) float raSynthLevel (int s)                                { return synthState->synth.soundLevel (s); }
RA_EXPORT (ra_synth_voices) int raSynthVoices()                                      { return synthState->synth.activeVoices(); }
RA_EXPORT (ra_synth_left) float* raSynthLeft()                                       { return synthState->left.data(); }
RA_EXPORT (ra_synth_right) float* raSynthRight()                                     { return synthState->right.data(); }
RA_EXPORT (ra_synth_render) void raSynthRender (int n)
{
    n = std::clamp (n, 0, (int) synthState->left.size());
    synthState->synth.render (synthState->left.data(), synthState->right.data(), n);
}
