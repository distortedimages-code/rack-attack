#include "RaFactory.h"

#include <algorithm>
#include <cmath>

namespace ra
{
using seq::Division;
using seq::Mode;
using seq::Track;
using seq::maxSteps;
using seq::numDivisions;

namespace
{
    bool readVlq (const Bytes& d, std::size_t& i, std::size_t end, std::uint32_t& v)
    {
        v = 0;
        for (int n = 0; n < 4; ++n)
        {
            if (i >= end)
                return false;
            const auto b = d[i++];
            v = (v << 7) | (b & 0x7F);
            if (b < 0x80)
                return true;
        }
        return false;
    }

    std::uint32_t be32 (const Bytes& d, std::size_t i)
    {
        return (std::uint32_t) d[i] << 24 | (std::uint32_t) d[i + 1] << 16 | (std::uint32_t) d[i + 2] << 8 | d[i + 3];
    }
}

bool isSmf (const Bytes& f)
{
    return f.size() >= 14 && f[0] == 'M' && f[1] == 'T' && f[2] == 'h' && f[3] == 'd';
}

std::vector<Bytes> smfSysex (const Bytes& d)
{
    std::vector<Bytes> out;
    if (! isSmf (d))
        return out;
    std::size_t i = 8 + be32 (d, 4);
    while (i + 8 <= d.size())
    {
        const bool track = d[i] == 'M' && d[i + 1] == 'T' && d[i + 2] == 'r' && d[i + 3] == 'k';
        const std::size_t len = be32 (d, i + 4), start = i + 8, end = std::min (d.size(), start + len);
        i = start + len;
        if (! track)
            continue;
        std::size_t j = start;
        std::uint8_t running = 0;
        Bytes pending;                                   // a SysEx sent in packets (F0 ... then F7 ...)
        while (j < end)
        {
            std::uint32_t delta = 0, n = 0;
            if (! readVlq (d, j, end, delta) || j >= end)
                break;
            const auto status = d[j];
            if (status == 0xF0 || status == 0xF7)
            {
                if (! readVlq (d, ++j, end, n) || j + n > end)
                    break;
                if (status == 0xF0)
                    pending.assign (1, 0xF0);
                pending.insert (pending.end(), d.begin() + (long) j, d.begin() + (long) (j + n));
                j += n;
                if (! pending.empty() && pending.front() == 0xF0 && pending.back() == 0xF7)
                {
                    out.push_back (pending);
                    pending.clear();
                }
            }
            else if (status == 0xFF)
            {
                if (j + 1 >= end || ! readVlq (d, j += 2, end, n))
                    break;
                j += n;
            }
            else
            {
                if (status & 0x80)
                {
                    running = status;
                    ++j;
                }
                if (running == 0)
                    break;
                const int hi = running & 0xF0;
                j += (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            }
        }
    }
    return out;
}

//==============================================================================
std::optional<InternalPattern> InternalPattern::fromData (const Bytes& d)
{
    if ((int) d.size() != kPatternDataSize)
        return std::nullopt;
    InternalPattern p;
    p.mode = d[0];
    p.length = std::clamp ((int) d[1] + 1, 1, 128);
    p.stepClock = d[2];
    p.noteLength = d[3];
    p.swing = d[4];
    p.startWhen = d[5];
    p.startWhere = d[6];
    p.velocity = { std::max (1, (int) d[7]), std::max (1, (int) d[8]), std::max (1, (int) d[9]) };
    p.group = d[10];
    for (int i = 0; i < 4; ++i)
        p.reserved[(size_t) i] = d[(size_t) (11 + i)];
    for (int i = 0; i < 128; ++i)
        p.steps[(size_t) i] = (std::uint8_t) std::min (3, (int) d[(size_t) (15 + i)]);
    return p;
}

Bytes InternalPattern::toData() const
{
    Bytes d ((size_t) kPatternDataSize, 0);
    auto b = [] (int v) { return (std::uint8_t) std::clamp (v, 0, 127); };
    d[0] = b (mode);
    d[1] = b (length - 1);
    d[2] = b (stepClock);
    d[3] = b (noteLength);
    d[4] = b (swing);
    d[5] = b (startWhen);
    d[6] = b (startWhere);
    d[7] = b (velocity[0]);
    d[8] = b (velocity[1]);
    d[9] = b (velocity[2]);
    d[10] = b (group);
    for (int i = 0; i < 4; ++i)
        d[(size_t) (11 + i)] = reserved[(size_t) i];
    for (int i = 0; i < 128; ++i)
        d[(size_t) (15 + i)] = (std::uint8_t) std::min (3, (int) steps[(size_t) i]);
    return d;
}

int InternalPattern::hits() const
{
    return (int) std::count_if (steps.begin(), steps.begin() + std::clamp (length, 1, 128), [] (std::uint8_t s) { return s != 0; });
}

//==============================================================================
namespace
{
    struct ClockValue { const char* name; double beats; };
    const ClockValue clocks[numStepClocks] = {
        { "1/64T", 1.0 / 24 }, { "1/32T", 1.0 / 12 }, { "1/32", 1.0 / 8 },  { "1/16T", 1.0 / 6 }, { "1/16", 1.0 / 4 },
        { "1/8T", 1.0 / 3 },   { "1/16.", 3.0 / 8 },  { "1/8", 1.0 / 2 },   { "1/4T", 2.0 / 3 },  { "1/8.", 3.0 / 4 },
        { "1/4", 1.0 },        { "1/2T", 4.0 / 3 },   { "1/4.", 3.0 / 2 },  { "1/2", 2.0 },       { "1/1T", 8.0 / 3 },
        { "1/2.", 3.0 },       { "1 mesure", 4.0 },   { "1,5 mesure", 6.0 }, { "2 mesures", 8.0 }, { "3 mesures", 12.0 },
        { "4 mesures", 16.0 }, { "6 mesures", 24.0 }, { "8 mesures", 32.0 },
    };

    // the sequencer's divisions and the Step Clk value each one is
    const std::array<std::pair<Division, int>, numDivisions> divisionClock { {
        { Division::thirtySecond, 2 }, { Division::sixteenthTriplet, 3 }, { Division::sixteenth, 4 },
        { Division::eighthTriplet, 5 }, { Division::eighth, 7 }, { Division::quarter, 10 } } };
}

const char* stepClockName (int v)   { return v >= 0 && v < numStepClocks ? clocks[v].name : "?"; }
double stepClockBeats (int v)       { return clocks[std::clamp (v, 0, numStepClocks - 1)].beats; }

int swingPercent (int s)            { return s <= 0 ? 50 : (int) std::lround (58.333 + (std::min (s, 5) - 1) * 4.1667); }
int internalSwing (int percent)     { return percent < 56 ? 0 : std::clamp ((int) std::lround ((percent - 58.333) / 4.1667) + 1, 1, 5); }

void internalToTrack (const InternalPattern& p, Track& t, bool* approximate)
{
    const auto name = t.name;
    seq::resetTrack (t);
    t.name = name;
    t.mode = Mode::steps;
    t.length = (std::uint8_t) std::clamp (p.length, 1, maxSteps);

    // the nearest division (by ratio)
    const double beats = stepClockBeats (p.stepClock);
    auto best = divisionClock[2];
    double bestErr = 1e9;
    for (auto dc : divisionClock)
    {
        const double err = std::abs (std::log (stepClockBeats (dc.second) / beats));
        if (err < bestErr) { bestErr = err; best = dc; }
    }
    t.division = best.first;
    if (approximate != nullptr)
        *approximate = best.second != p.stepClock;

    const int gate = std::clamp ((int) std::lround (stepClockBeats (p.noteLength) / beats * 8.0), 1, 64);
    for (int i = 0; i < t.length; ++i)
    {
        const int s = p.steps[(size_t) i];
        auto& step = t.steps[(size_t) i];
        step.on = s > 0;
        if (s > 0)
            step.velocity = (std::uint8_t) std::clamp (p.velocity[(size_t) (s - 1)], 1, 127);
        step.length = (std::uint8_t) gate;
    }
}

InternalPattern trackToInternal (const Track& t, int mode)
{
    InternalPattern p;
    p.mode = mode;
    const auto trigs = t.trigs();
    p.length = std::clamp ((int) trigs.size(), 1, 128);
    p.stepClock = 4;
    for (auto dc : divisionClock)
        if (dc.first == t.division)
            p.stepClock = dc.second;
    p.noteLength = p.stepClock;

    // three velocities: those of the track when it has three or fewer, else the usual 64 / 96 / 127
    std::vector<int> used;
    for (int i = 0; i < p.length; ++i)
        if (trigs[(size_t) i])
        {
            const int v = t.steps[(size_t) i].velocity;
            if (std::find (used.begin(), used.end(), v) == used.end())
                used.push_back (v);
        }
    std::sort (used.begin(), used.end());
    if (! used.empty() && used.size() <= 3)
    {
        while (used.size() < 3)
            used.push_back (used.back());
        p.velocity = { used[0], used[1], used[2] };
    }
    for (int i = 0; i < p.length; ++i)
    {
        if (! trigs[(size_t) i])
            continue;
        const int v = t.mode == Mode::euclid ? 100 : t.steps[(size_t) i].velocity;
        int pick = 2;
        for (int k = 0; k < 3; ++k)
            if (std::abs (p.velocity[(size_t) k] - v) < std::abs (p.velocity[(size_t) pick] - v))
                pick = k;
        p.steps[(size_t) i] = (std::uint8_t) (pick + 1);
    }
    return p;
}

//==============================================================================
std::vector<BankProgram> readBank (const std::vector<Bytes>& sysex)
{
    std::vector<BankProgram> out;
    BankProgram* current = nullptr;
    auto begin = [&]
    {
        out.emplace_back();
        current = &out.back();
    };

    for (const auto& m : sysex)
    {
        auto p = parse (m);
        if (! p.isRackAttack || p.payload.size() < 3)
            continue;
        const auto c = p.command;
        if (c == cmd::PRGD)
            begin();
        else if (c == 0x07)
        {
            // "store buffer 0x30 into program N": the program just sent is number N
            if (current != nullptr && p.payload.size() >= 4)
                current->number = p.payload[3];
            continue;
        }
        else if (c != cmd::SNDD && c != cmd::EFXD && c != cmd::PATD)
            continue;
        if (current == nullptr || (c == cmd::PATD && current->kit.hasPattern (p.payload[2])
                                   && ! current->kit.hasProgram()))
            begin();                                     // patterns alone, or a new set of them

        // the dumps go to the edit buffer of a kit
        auto payload = p.payload;
        payload[1] = kEditBuffer;
        current->kit.apply (parse (build (0, payload)));
        if (c == cmd::PRGD)
            current->kit.setProgramLocation ({ 0x00, kEditBuffer });   // as the app sends it to the unit
    }
    return out;
}
}
