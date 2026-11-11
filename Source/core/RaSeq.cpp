#include "RaSeq.h"

#include <algorithm>
#include <cmath>

namespace ra::seq
{
namespace
{
    template <typename T> T clampTo (int v, int lo, int hi)   { return (T) std::max (lo, std::min (hi, v)); }

    std::int64_t floorDiv (std::int64_t a, std::int64_t b)
    {
        auto q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0)))
            --q;
        return q;
    }

    std::int64_t positiveMod (std::int64_t a, std::int64_t b)   { return ((a % b) + b) % b; }
}

double stepsPerBeat (Division d)
{
    switch (d)
    {
        case Division::thirtySecond:      return 8.0;
        case Division::sixteenth:         return 4.0;
        case Division::eighth:            return 2.0;
        case Division::quarter:           return 1.0;
        case Division::sixteenthTriplet:  return 6.0;
        case Division::eighthTriplet:     return 3.0;
    }
    return 4.0;
}

const char* divisionName (Division d)
{
    switch (d)
    {
        case Division::thirtySecond:      return "1/32";
        case Division::sixteenth:         return "1/16";
        case Division::eighth:            return "1/8";
        case Division::quarter:           return "1/4";
        case Division::sixteenthTriplet:  return "1/16T";
        case Division::eighthTriplet:     return "1/8T";
    }
    return "1/16";
}

//==============================================================================
const Lock* Step::findLock (int address) const
{
    for (auto& l : locks)
        if (l.address == address)
            return &l;
    return nullptr;
}

bool Step::setLock (int address, int value)
{
    if (address < 0 || address >= numSoundParameters)
        return false;
    for (auto& l : locks)
        if (l.address == address)
        {
            l.value = clampTo<std::uint8_t> (value, 0, 127);
            return true;
        }
    if ((int) locks.size() >= maxLocksPerStep)
        return false;
    locks.push_back ({ (std::uint8_t) address, clampTo<std::uint8_t> (value, 0, 127) });
    return true;
}

void Step::clearLock (int address)
{
    locks.erase (std::remove_if (locks.begin(), locks.end(), [address] (const Lock& l) { return l.address == address; }),
                 locks.end());
}

bool Step::isDefault() const
{
    const Step d;
    return on == d.on && velocity == d.velocity && note == d.note && length == d.length && micro == d.micro
        && probability == d.probability && condA == d.condA && condB == d.condB && retrig == d.retrig && locks.empty();
}

std::string Step::conditionText() const
{
    if (condB == 0)
        return {};
    return std::to_string (condA) + ":" + std::to_string (condB);
}

//==============================================================================
std::vector<bool> euclidean (int steps, int hits, int rotation)
{
    steps = std::max (1, std::min (maxSteps, steps));
    hits = std::max (0, std::min (steps, hits));
    std::vector<bool> base ((size_t) steps, false), out ((size_t) steps, false);
    for (int i = 0; i < steps; ++i)
        base[(size_t) i] = hits > 0 && ((i * hits) % steps) < hits;
    for (int i = 0; i < steps; ++i)
        out[(size_t) positiveMod (i + rotation, steps)] = base[(size_t) i];
    return out;
}

//==============================================================================
int Track::activeLength() const
{
    return mode == Mode::euclid ? std::max (1, (int) euclid.steps) : std::max (1, (int) length);
}

bool Track::isOn (int index) const
{
    if (index < 0 || index >= activeLength())
        return false;
    if (mode == Mode::euclid)
        return euclidean (euclid.steps, euclid.hits, euclid.rotation)[(size_t) index];
    return steps[(size_t) index].on;
}

std::vector<bool> Track::trigs() const
{
    if (mode == Mode::euclid)
        return euclidean (euclid.steps, euclid.hits, euclid.rotation);
    std::vector<bool> out ((size_t) activeLength());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = steps[i].on;
    return out;
}

void Track::printEuclid()
{
    auto t = euclidean (euclid.steps, euclid.hits, euclid.rotation);
    for (size_t i = 0; i < t.size(); ++i)
        steps[i].on = t[i];
    length = euclid.steps;
    mode = Mode::steps;
}

int Track::countTrigs() const
{
    int n = 0;
    for (bool b : trigs())
        n += b ? 1 : 0;
    return n;
}

int Track::countLocks() const
{
    int n = 0;
    for (int i = 0; i < activeLength(); ++i)
        n += (int) steps[(size_t) i].locks.size();
    return n;
}

//==============================================================================
// Saved form: "RASQ", version, then the pattern settings, then per track its
// settings and only the steps that differ from a fresh step.

namespace
{
    constexpr std::uint8_t kVersion = 1;

    struct Reader
    {
        const std::vector<std::uint8_t>& d;
        size_t pos = 0;
        bool ok = true;
        int u8()
        {
            if (pos >= d.size()) { ok = false; return 0; }
            return d[pos++];
        }
        int u16()   { int hi = u8(); return (hi << 8) | u8(); }
    };
}

std::vector<std::uint8_t> Pattern::save() const
{
    std::vector<std::uint8_t> out { 'R', 'A', 'S', 'Q', kVersion };
    auto u8 = [&] (int v) { out.push_back ((std::uint8_t) (v & 0xFF)); };
    auto u16 = [&] (int v) { u8 (v >> 8); u8 (v); };

    u16 ((int) std::lround (std::max (20.0, std::min (300.0, tempo)) * 10.0));
    u8 (swing);
    u8 (numTracks);
    for (auto& tr : tracks)
    {
        u8 ((int) tr.mode);
        u8 (tr.length);
        u8 ((int) tr.division);
        u8 (tr.mute ? 1 : 0);
        const auto nameBytes = std::min ((int) tr.name.size(), maxNameBytes);
        u8 (nameBytes);
        for (int i = 0; i < nameBytes; ++i)
            u8 ((unsigned char) tr.name[(size_t) i]);
        u8 (tr.euclid.steps);
        u8 (tr.euclid.hits);
        u8 (tr.euclid.rotation + 128);
        int count = 0;
        for (auto& s : tr.steps)
            count += s.isDefault() ? 0 : 1;
        u8 (count);
        for (int i = 0; i < maxSteps; ++i)
        {
            auto& s = tr.steps[(size_t) i];
            if (s.isDefault())
                continue;
            u8 (i);
            u8 (s.on ? 1 : 0);
            u8 (s.velocity);
            u8 (s.note);
            u8 (s.length);
            u8 (s.micro + 128);
            u8 (s.probability);
            u8 (s.condA);
            u8 (s.condB);
            u8 (s.retrig);
            u8 ((int) s.locks.size());
            for (auto& l : s.locks)
            {
                u8 (l.address);
                u8 (l.value);
            }
        }
    }
    return out;
}

bool Pattern::load (const std::vector<std::uint8_t>& data)
{
    if (data.size() < 5 || data[0] != 'R' || data[1] != 'A' || data[2] != 'S' || data[3] != 'Q' || data[4] > kVersion)
        return false;

    Reader r { data, 5 };
    Pattern p;
    p.tempo = std::max (20.0, std::min (300.0, r.u16() / 10.0));
    p.swing = clampTo<std::uint8_t> (r.u8(), 50, 75);
    int tracksSaved = r.u8();
    for (int t = 0; t < tracksSaved && r.ok; ++t)
    {
        Track tr;
        tr.mode = (Mode) std::min (2, r.u8());
        tr.length = clampTo<std::uint8_t> (r.u8(), 1, maxSteps);
        tr.division = (Division) std::min (numDivisions - 1, r.u8());
        tr.mute = r.u8() != 0;
        const int nameBytes = r.u8();
        for (int i = 0; i < nameBytes && r.ok; ++i)
            tr.name.push_back ((char) r.u8());
        tr.euclid.steps = clampTo<std::uint8_t> (r.u8(), 1, maxSteps);
        tr.euclid.hits = clampTo<std::uint8_t> (r.u8(), 0, tr.euclid.steps);
        tr.euclid.rotation = clampTo<std::int8_t> (r.u8() - 128, -maxSteps, maxSteps - 1);
        int count = r.u8();
        for (int k = 0; k < count && r.ok; ++k)
        {
            int index = r.u8();
            Step s;
            s.on = r.u8() != 0;
            s.velocity = clampTo<std::uint8_t> (r.u8(), 1, 127);
            s.note = clampTo<std::uint8_t> (r.u8(), 0, 127);
            s.length = clampTo<std::uint8_t> (r.u8(), 1, 64);
            s.micro = clampTo<std::int8_t> (r.u8() - 128, -11, 11);
            s.probability = clampTo<std::uint8_t> (r.u8(), 0, 100);
            s.condA = clampTo<std::uint8_t> (r.u8(), 0, 8);
            s.condB = clampTo<std::uint8_t> (r.u8(), 0, 8);
            s.retrig = clampTo<std::uint8_t> (r.u8(), 1, 8);
            int locks = r.u8();
            for (int l = 0; l < locks && r.ok; ++l)
            {
                int address = r.u8();
                int value = r.u8();
                s.setLock (address, value);
            }
            if (index >= 0 && index < maxSteps && t < numTracks)
                tr.steps[(size_t) index] = s;
        }
        if (t < numTracks)
            p.tracks[(size_t) t] = tr;
    }
    if (! r.ok)
        return false;
    *this = p;
    return true;
}

//==============================================================================
Player::Player (std::uint32_t seed) : rng (seed)
{
    reset();
}

void Player::reset()
{
    running = false;
    next.fill (-1);
    for (auto& l : locked)
        l.clear();
    for (auto& l : touched)
        l.clear();
    sounding.clear();
}

void Player::render (const Pattern& p, const Context& ctx, double fromPpq, double untilPpq, double leadPpq,
                     std::vector<Event>& out)
{
    if (! running)
    {
        running = true;
        startPpq = fromPpq;
        next.fill (-1);
    }

    const auto first = out.size();

    for (int t = 0; t < numTracks; ++t)
        renderTrack (t, p, ctx, untilPpq, leadPpq, out);

    // note offs that fall in the window; forget the ones long gone
    for (auto& s : sounding)
        if (s.offPpq >= 0.0 && s.offPpq < untilPpq)
        {
            out.push_back ({ Event::Kind::noteOff, s.offPpq, 0, s.channel, s.note, 0, 0, 0 });
            s.offPpq = -1.0 - s.offPpq;     // marked as sent, time kept (negative)
        }
    sounding.erase (std::remove_if (sounding.begin(), sounding.end(),
                                    [fromPpq] (const Sounding& s) { return s.offPpq < 0.0 && (-1.0 - s.offPpq) < fromPpq; }),
                    sounding.end());

    std::stable_sort (out.begin() + (long) first, out.end(), [] (const Event& a, const Event& b) { return a.ppq < b.ppq; });
}

void Player::renderTrack (int t, const Pattern& p, const Context& ctx, double untilPpq, double leadPpq,
                          std::vector<Event>& out)
{
    const auto& tr = p.tracks[(size_t) t];
    const double spb = stepsPerBeat (tr.division);
    const double stepPpq = 1.0 / spb;
    const int length = tr.activeLength();
    const auto trigs = tr.trigs();
    const double swingShift = 2.0 * stepPpq * (std::max (50, std::min (75, (int) p.swing)) / 100.0 - 0.5);

    auto& n = next[(size_t) t];
    if (n < 0)
        n = (std::int64_t) std::ceil (startPpq * spb - 1e-9);

    for (int guard = 0; guard < 4096; ++guard)
    {
        const auto index = (int) positiveMod (n, length);
        const auto loop = floorDiv (n, length);
        const auto& s = tr.steps[(size_t) index];
        const bool on = trigs[(size_t) index] && ! tr.mute;

        double when = (double) n * stepPpq + s.micro * stepPpq / microSteps;
        if (positiveMod (n, 2) == 1)
            when += swingShift;

        // parameters to send before the note: put back what this step doesn't lock, then its locks
        std::vector<std::pair<int, int>> params;
        if (on)
        {
            for (auto address : locked[(size_t) t])
                if (s.findLock (address) == nullptr && ctx.soundKnown[(size_t) t])
                    params.emplace_back (address, ctx.sound[(size_t) t][address]);
            for (auto& l : s.locks)
                params.emplace_back (l.address, l.value);
        }

        if (when - leadPpq * (double) params.size() >= untilPpq)
            break;
        ++n;

        if (! on)
            continue;
        if (s.condB > 0 && positiveMod (loop, s.condB) != positiveMod (s.condA - 1, s.condB))
            continue;
        if (s.probability < 100 && (int) (rng() % 100u) >= s.probability)
            continue;

        for (size_t k = 0; k < params.size(); ++k)
            out.push_back ({ Event::Kind::parameter, when - leadPpq * (double) (params.size() - k), t, 0, 0, 0,
                             params[k].first, params[k].second });
        auto& held = locked[(size_t) t];
        held.clear();
        auto& sent = touched[(size_t) t];
        for (auto& l : s.locks)
        {
            held.push_back (l.address);
            if (std::find (sent.begin(), sent.end(), l.address) == sent.end())
                sent.push_back (l.address);
        }

        int channel = ctx.channel, note = ctx.baseNote + t;
        if (tr.mode == Mode::notes && ctx.tunedChannel[(size_t) t] >= 1 && ctx.tunedChannel[(size_t) t] <= 16)
        {
            channel = ctx.tunedChannel[(size_t) t];
            note = s.note;
        }
        note = std::max (0, std::min (127, note));

        const int hits = std::max (1, std::min (8, (int) s.retrig));
        const double hitPpq = stepPpq / hits;
        double gate = std::max (1, (int) s.length) * stepPpq / 8.0;
        if (hits > 1)
            gate = std::min (gate, hitPpq * 0.9);

        for (int h = 0; h < hits; ++h)
        {
            const double at = when + h * hitPpq;
            // the same note still held from before: let it go just before
            for (auto& snd : sounding)
                if (snd.offPpq >= 0.0 && snd.channel == channel && snd.note == note && snd.offPpq > at - 1e-6)
                {
                    out.push_back ({ Event::Kind::noteOff, at - 1e-6, t, channel, note, 0, 0, 0 });
                    snd.offPpq = -1.0 - (at - 1e-6);
                }
            out.push_back ({ Event::Kind::noteOn, at, t, channel, note, std::max (1, std::min (127, (int) s.velocity)), 0, 0 });
            sounding.push_back ({ at + gate, channel, note });
        }
    }
}

void Player::stop (const Context& ctx, double atPpq, std::vector<Event>& out)
{
    for (auto& s : sounding)
    {
        const double offAt = s.offPpq >= 0.0 ? s.offPpq : -1.0 - s.offPpq;
        if (s.offPpq >= 0.0 || offAt >= atPpq)
            out.push_back ({ Event::Kind::noteOff, atPpq, 0, s.channel, s.note, 0, 0, 0 });
    }
    for (int t = 0; t < numTracks; ++t)
        if (ctx.soundKnown[(size_t) t])
        {
            auto addresses = touched[(size_t) t];
            for (auto a : locked[(size_t) t])
                if (std::find (addresses.begin(), addresses.end(), a) == addresses.end())
                    addresses.push_back (a);
            for (auto address : addresses)
                out.push_back ({ Event::Kind::parameter, atPpq, t, 0, 0, 0, address, ctx.sound[(size_t) t][address] });
        }
    reset();
}
}
