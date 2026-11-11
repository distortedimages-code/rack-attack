#include "Measure.h"

#include "Engine.h"
#include "Text.h"

#include <complex>
#include <numeric>

namespace measure
{
using ra::synth::Calibration;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    struct Stopped {};
    struct Redo { int why; };

    // sound parameter addresses (docs/SYSEX.md)
    enum Address : int
    {
        o1Shape = 0x01, o1Pitch, o1Detune, o1Start, o1PitchMod, o1PitchCtrl, o1PitchModSrc, o1PitchCtrlSrc,
        o1Fm, o1FmMod, o1FmCtrl, o1FmModSrc, o1FmCtrlSrc,
        o2Shape = 0x10, o2Pitch, o2Detune, o2Start, o2PitchMod, o2PitchCtrl, o2PitchModSrc, o2PitchCtrlSrc,
        mixO1 = 0x1A, mixO2, mixRing, mixCrack, mixO2Mod, mixO2Ctrl, mixO2ModSrc, mixO2CtrlSrc,
        crackSpeed = 0x24, crackLength,
        fType = 0x26, fCutoff, fReso, fDrive, fKeytrack, fMod1, fCtrl1, fMod1Src, fCtrl1Src, fMod2, fCtrl2, fMod2Src, fCtrl2Src,
        aVolume = 0x35, aVolMod, aVolCtrl, aVolModSrc, aVolCtrlSrc, aPan, aPanMod, aPanCtrl, aPanModSrc, aPanCtrlSrc,
        env1 = 0x3F, env2 = 0x45,
        l1Speed = 0x4B, l1Shape, l1Sync, l1OneShot, l2Speed, l2Shape, l2Sync, l2Mod, l2ModSrc,
        dryOut = 0x55, fxSelect, fxSend, tunedChannel, transpose, lowKey, highKey,
        xorGroup = 0x5E, patternMode = 0x6C
    };
    enum { attackOf = 0, decayOf = 1, sustainOf = 2, releaseOf = 3, shapeOf = 4 };

    // The test sound: a plain sine on Osc 1 (pitch set later), nothing modulated,
    // no filter, Envelope 1 open (attack 0, sustain full, short release), centred
    // on Out 1/2, no effect, no pattern.
    const std::vector<std::pair<int, int>>& baseSound()
    {
        static const std::vector<std::pair<int, int>> base {
            { o1Shape, 1 }, { o1Pitch, 100 }, { o1Detune, 0 }, { o1Start, 0 }, { o1PitchMod, 64 }, { o1PitchCtrl, 64 },
            { o1PitchModSrc, 0 }, { o1PitchCtrlSrc, 0 }, { o1Fm, 0 }, { o1FmMod, 64 }, { o1FmCtrl, 64 },
            { o1FmModSrc, 0 }, { o1FmCtrlSrc, 0 },
            { o2Shape, 1 }, { o2Pitch, 64 }, { o2Detune, 0 }, { o2Start, 0 }, { o2PitchMod, 64 }, { o2PitchCtrl, 64 },
            { o2PitchModSrc, 0 }, { o2PitchCtrlSrc, 0 },
            { mixO1, 64 }, { mixO2, 0 }, { mixRing, 0 }, { mixCrack, 0 }, { mixO2Mod, 64 }, { mixO2Ctrl, 64 },
            { mixO2ModSrc, 0 }, { mixO2CtrlSrc, 0 }, { crackSpeed, 64 }, { crackLength, 3 },
            { fType, 0 }, { fCutoff, 127 }, { fReso, 0 }, { fDrive, 0 }, { fKeytrack, 64 },
            { fMod1, 64 }, { fCtrl1, 64 }, { fMod1Src, 0 }, { fCtrl1Src, 0 },
            { fMod2, 64 }, { fCtrl2, 64 }, { fMod2Src, 0 }, { fCtrl2Src, 0 },
            { aVolume, 100 }, { aVolMod, 64 }, { aVolCtrl, 64 }, { aVolModSrc, 0 }, { aVolCtrlSrc, 0 },
            { aPan, 64 }, { aPanMod, 64 }, { aPanCtrl, 64 }, { aPanModSrc, 0 }, { aPanCtrlSrc, 0 },
            { env1 + attackOf, 0 }, { env1 + decayOf, 127 }, { env1 + sustainOf, 127 }, { env1 + releaseOf, 10 }, { env1 + shapeOf, 0 },
            { env2 + attackOf, 0 }, { env2 + decayOf, 127 }, { env2 + sustainOf, 127 }, { env2 + releaseOf, 10 }, { env2 + shapeOf, 0 },
            { l1Speed, 64 }, { l1Shape, 0 }, { l1Sync, 0 }, { l1OneShot, 0 },
            { l2Speed, 64 }, { l2Shape, 0 }, { l2Sync, 0 }, { l2Mod, 64 }, { l2ModSrc, 0 },
            { dryOut, 1 }, { fxSelect, 0 }, { fxSend, 0 }, { tunedChannel, 0 }, { transpose, 64 },
            { lowKey, 0 }, { highKey, 127 }, { xorGroup, 0 }, { patternMode, 0 }
        };
        return base;
    }

    //==========================================================================
    // Signal helpers

    void fft (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * pi / (double) len;
            for (size_t i = 0; i < n; i += len)
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const std::complex<double> w (std::cos (ang * (double) k), std::sin (ang * (double) k));
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                }
        }
    }

    int pow2Above (int n)
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    // Power spectrum averaged over Hann windows of n samples, half overlapping.
    std::vector<double> welch (const std::vector<float>& x, int from, int count, int n)
    {
        std::vector<double> power ((size_t) n / 2 + 1, 0.0);
        from = std::max (0, from);
        count = std::min (count, (int) x.size() - from);
        if (count < n)
            n = std::max (16, count);
        int frames = 0;
        std::vector<std::complex<double>> a;
        for (int s = from; s + n <= from + count; s += n / 2)
        {
            double mean = 0.0;
            for (int i = 0; i < n; ++i)
                mean += x[(size_t) (s + i)];
            mean /= n;
            a.assign ((size_t) pow2Above (n), {});
            for (int i = 0; i < n; ++i)
                a[(size_t) i] = (x[(size_t) (s + i)] - mean) * (0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1)));
            fft (a);
            for (size_t k = 0; k < power.size() && k < a.size(); ++k)
                power[k] += std::norm (a[k]);
            ++frames;
        }
        if (frames > 0)
            for (auto& p : power)
                p /= frames;
        return power;
    }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    //==========================================================================
    // Tables from measured points

    using Points = std::vector<std::pair<int, double>>;

    // Removes points far from the line through their neighbours (in the log domain).
    Points clean (Points p, double tolerance = 0.3)
    {
        std::sort (p.begin(), p.end());
        p.erase (std::remove_if (p.begin(), p.end(), [] (const auto& q) { return ! (q.second > 0.0) || ! std::isfinite (q.second); }), p.end());
        if (p.size() < 4)
            return p;
        Points out;
        for (size_t i = 0; i < p.size(); ++i)
        {
            if (i == 0 || i + 1 == p.size())
            {
                out.push_back (p[i]);
                continue;
            }
            const double t = (double) (p[i].first - p[i - 1].first) / (double) std::max (1, p[i + 1].first - p[i - 1].first);
            const double guess = std::log (p[i - 1].second) * (1.0 - t) + std::log (p[i + 1].second) * t;
            if (std::abs (std::log (p[i].second) - guess) < tolerance)
                out.push_back (p[i]);
        }
        // the ends: against the next two
        auto endOk = [] (const auto& a, const auto& b, const auto& c, double tol)
        {
            const double slope = (std::log (c.second) - std::log (b.second)) / (double) std::max (1, c.first - b.first);
            return std::abs (std::log (a.second) - (std::log (b.second) + slope * (double) (a.first - b.first))) < tol;
        };
        if (out.size() >= 4 && ! endOk (out[0], out[1], out[2], tolerance * 1.5))
            out.erase (out.begin());
        if (out.size() >= 4 && ! endOk (out.back(), out[out.size() - 2], out[out.size() - 3], tolerance * 1.5))
            out.pop_back();
        return out;
    }

    // Interpolated in the log domain, carried on past the ends with their slope.
    std::array<float, 128> logTable (const Points& raw, const std::array<float, 128>& fallback, size_t minPoints = 3)
    {
        auto p = clean (raw);
        if (p.size() < minPoints)
            return fallback;
        auto slopeOver = [&p] (size_t a, size_t b)
        {
            // least squares of log(y) over v, points a..b-1
            double sv = 0, sy = 0, svv = 0, svy = 0;
            const double n = (double) (b - a);
            for (size_t i = a; i < b; ++i)
            {
                const double v = p[i].first, y = std::log (p[i].second);
                sv += v; sy += y; svv += v * v; svy += v * y;
            }
            const double d = n * svv - sv * sv;
            return std::abs (d) < 1e-9 ? 0.0 : (n * svy - sv * sy) / d;
        };
        const size_t k = std::min<size_t> (4, p.size());
        const double low = slopeOver (0, k), high = slopeOver (p.size() - k, p.size());
        std::array<float, 128> t {};
        for (int v = 0; v < 128; ++v)
        {
            double y;
            if (v <= p.front().first)
                y = std::log (p.front().second) + low * (v - p.front().first);
            else if (v >= p.back().first)
                y = std::log (p.back().second) + high * (v - p.back().first);
            else
            {
                size_t i = 1;
                while (p[i].first < v)
                    ++i;
                const double f = (double) (v - p[i - 1].first) / (double) (p[i].first - p[i - 1].first);
                y = std::log (p[i - 1].second) * (1.0 - f) + std::log (p[i].second) * f;
            }
            t[(size_t) v] = (float) std::exp (y);
        }
        return t;
    }

    // Straight lines between the points, flat past the ends.
    std::array<float, 128> linTable (Points p, const std::array<float, 128>& fallback, size_t minPoints = 3)
    {
        std::sort (p.begin(), p.end());
        if (p.size() < minPoints)
            return fallback;
        std::array<float, 128> t {};
        for (int v = 0; v < 128; ++v)
        {
            double y;
            if (v <= p.front().first) y = p.front().second;
            else if (v >= p.back().first) y = p.back().second;
            else
            {
                size_t i = 1;
                while (p[i].first < v)
                    ++i;
                const double f = (double) (v - p[i - 1].first) / (double) (p[i].first - p[i - 1].first);
                y = p[i - 1].second * (1.0 - f) + p[i].second * f;
            }
            t[(size_t) v] = (float) y;
        }
        return t;
    }

    int nearest (const std::array<float, 128>& t, double target)
    {
        int best = 0;
        for (int v = 1; v < 128; ++v)
            if (std::abs (std::log (t[(size_t) v] / target)) < std::abs (std::log (t[(size_t) best] / target)))
                best = v;
        return best;
    }

    juce::var toVar (const Points& p)
    {
        juce::Array<juce::var> a;
        for (auto& q : p)
            a.add (juce::Array<juce::var> { q.first, q.second });
        return a;
    }

    template <typename T>
    juce::var toVar (const T& values)
    {
        juce::Array<juce::var> a;
        for (auto v : values)
            a.add ((double) v);
        return a;
    }

    std::vector<int> everyEight (int from = 0)
    {
        std::vector<int> v;
        for (int x = from; x < 127; x += 8)
            v.push_back (x);
        v.push_back (127);
        return v;
    }
}

//==============================================================================
double rmsOf (const std::vector<float>& x, int from, int count)
{
    from = std::max (0, from);
    count = std::min (count, (int) x.size() - from);
    if (count <= 0)
        return 0.0;
    double mean = 0.0, sum = 0.0;
    for (int i = 0; i < count; ++i)
        mean += x[(size_t) (from + i)];
    mean /= count;
    for (int i = 0; i < count; ++i)
    {
        const double d = x[(size_t) (from + i)] - mean;
        sum += d * d;
    }
    return std::sqrt (sum / count);
}

double peakFrequency (const std::vector<float>& x, int from, int count, double sr, double minHz, double maxHz, double* purity)
{
    from = std::max (0, from);
    count = std::min ({ count, (int) x.size() - from, 32768 });
    if (purity != nullptr)
        *purity = 0.0;
    if (count < 64)
        return 0.0;
    const int n = pow2Above (count) * 2;
    std::vector<std::complex<double>> a ((size_t) n);
    double mean = 0.0;
    for (int i = 0; i < count; ++i)
        mean += x[(size_t) (from + i)];
    mean /= count;
    for (int i = 0; i < count; ++i)
        a[(size_t) i] = (x[(size_t) (from + i)] - mean) * (0.5 - 0.5 * std::cos (2.0 * pi * i / (count - 1)));
    fft (a);
    const int lo = std::max (2, (int) std::floor (minHz * n / sr));
    const int hi = std::min (n / 2 - 2, (int) std::ceil (maxHz * n / sr));
    if (hi <= lo)
        return 0.0;
    int best = lo;
    std::vector<double> mags;
    for (int k = lo; k <= hi; ++k)
    {
        mags.push_back (std::abs (a[(size_t) k]));
        if (std::abs (a[(size_t) k]) > std::abs (a[(size_t) best]))
            best = k;
    }
    const double l0 = std::log (std::abs (a[(size_t) best - 1]) + 1e-30);
    const double l1 = std::log (std::abs (a[(size_t) best]) + 1e-30);
    const double l2 = std::log (std::abs (a[(size_t) best + 1]) + 1e-30);
    const double den = l0 - 2.0 * l1 + l2;
    const double d = std::abs (den) < 1e-12 ? 0.0 : juce::jlimit (-0.5, 0.5, 0.5 * (l0 - l2) / den);
    if (purity != nullptr)
        *purity = std::abs (a[(size_t) best]) / std::max (1e-30, median (mags));
    return ((double) best + d) * sr / n;
}

double frequencyOf (const std::vector<float>& x, int from, int count, double sr)
{
    from = std::max (0, from);
    count = std::min (count, (int) x.size() - from);
    if (count < 64)
        return 0.0;
    const double coarse = peakFrequency (x, from, count, sr, 2.0, sr * 0.49);
    if (coarse <= 0.0 || coarse > sr / 6.0)
        return coarse;

    // refined with the rising zero crossings (with some hysteresis)
    double mean = 0.0;
    for (int i = 0; i < count; ++i)
        mean += x[(size_t) (from + i)];
    mean /= count;
    const double thr = 0.3 * rmsOf (x, from, count);
    bool low = false;
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (int i = 1; i < count; ++i)
    {
        const double a = x[(size_t) (from + i - 1)] - mean, b = x[(size_t) (from + i)] - mean;
        if (b < -thr)
            low = true;
        if (low && a < 0.0 && b >= 0.0)
        {
            const double t = (double) (i - 1) + a / (a - b);
            if (first < 0.0)
                first = t;
            last = t;
            ++crossings;
            low = false;
        }
    }
    if (crossings < 3 || last <= first)
        return coarse;
    const double fine = (double) (crossings - 1) * sr / (last - first);
    return std::abs (fine / coarse - 1.0) < 0.04 ? fine : coarse;
}

//==============================================================================
Ring::Ring (int log2Size)
    : buf ((size_t) 1 << log2Size, 0.0f), mask (((std::int64_t) 1 << log2Size) - 1)
{
}

void Ring::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    pos.store (0, std::memory_order_release);
}

void Ring::write (const float* x, int n)
{
    auto p = pos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        buf[(size_t) ((p + i) & mask)] = x[i];
    pos.store (p + n, std::memory_order_release);
}

int Ring::read (std::int64_t from, int count, std::vector<float>& out) const
{
    const auto end = written();
    const auto begin = std::max<std::int64_t> (0, end - (mask + 1) + 4096);
    if (from < begin)
    {
        count -= (int) (begin - from);
        from = begin;
    }
    count = (int) std::max<std::int64_t> (0, std::min<std::int64_t> (count, end - from));
    out.resize ((size_t) count);
    for (int i = 0; i < count; ++i)
        out[(size_t) i] = buf[(size_t) ((from + i) & mask)];
    return count;
}

//==============================================================================
Bench::Bench (Rig& r, int deviceId, int ch, int note, int free, int scratch)
    : rig (r), device (deviceId), channel (juce::jlimit (1, 16, ch)), baseNote (note),
      freeChannel (free == ch ? 0 : free), sound (scratch)
{
    state.fill (-1);
}

void Bench::check()
{
    if (rig.shouldStop())
        throw Stopped();
    if (rig.wantsPause())
        throw Redo { Rig::asked };
    if (rig.interrupted())
        throw Redo { Rig::touched };
}

void Bench::warn (const juce::String& text)
{
    if (result != nullptr)
        result->warnings.add (text);
}

void Bench::step()
{
    ++done;
    if (onProgress)
        onProgress (done, expectedSteps, what);
}

void Bench::set (int address, int value)
{
    value = juce::jlimit (0, 127, value);
    if (state[(size_t) address] == value)
        return;
    state[(size_t) address] = value;
    const auto sysex = ra::soundParameter ((std::uint8_t) device, sound, address, value);
    rig.send (juce::MidiMessage (sysex.data(), (int) sysex.size()));
    rig.wait (6);                             // about the rate the unit takes them at
}

void Bench::reset (std::initializer_list<std::pair<int, int>> changes)
{
    std::array<int, ra::synth::soundBytes> want;
    want.fill (-1);
    for (auto& [a, v] : baseSound())
        want[(size_t) a] = v;
    want[o1Pitch] = refPitch;
    for (auto& [a, v] : changes)
        want[(size_t) a] = v;
    for (int a = 0; a < ra::synth::soundBytes; ++a)
        if (want[(size_t) a] >= 0)
            set (a, want[(size_t) a]);
    rig.wait (40);
}

Bench::Shot Bench::play (const Play& p)
{
    check();
    const int note = p.note >= 0 ? p.note : baseNote + sound;
    const int ch = p.channel > 0 ? p.channel : channel;
    rig.wait (30);
    const auto startAt = rig.mark();
    rig.send (juce::MidiMessage::noteOn (ch, note, (juce::uint8) juce::jlimit (1, 127, p.velocity)));

    auto msSince = [this] (std::int64_t from) { return (double) (rig.mark() - from) * 1000.0 / sr; };
    auto sofar = [this, startAt] (std::int64_t offAt)
    {
        Shot s;
        const auto from = startAt + latency;
        rig.read (from, (int) std::max<std::int64_t> (0, rig.mark() - from), s.x);
        s.noteOffAt = offAt >= 0 ? (int) (offAt - startAt) + lead : -1;
        return s;
    };

    if (p.holdUntil)
    {
        while (msSince (startAt) < p.holdMs)
        {
            rig.wait (50);
            check();
            if (p.holdUntil (sofar (-1)))
                break;
        }
    }
    else
        rig.wait (p.holdMs);

    const auto offAt = rig.mark();
    rig.send (juce::MidiMessage::noteOff (ch, note));
    if (p.afterUntil)
    {
        while (msSince (offAt) < p.afterMs)
        {
            rig.wait (50);
            check();
            if (p.afterUntil (sofar (offAt)))
                break;
        }
    }
    else
        rig.wait (p.afterMs);

    rig.wait (ms (latency) + 20.0);       // what was played reaches the recording
    auto s = sofar (offAt);
    float peak = 0.0f;
    for (auto v : s.x)
        peak = std::max (peak, std::abs (v));
    if (peak > 0.985f)
        clipped = true;
    step();
    return s;
}

Bench::Shot Bench::playLoud (const Play& p)
{
    for (int tries = 0;; ++tries)
    {
        clipped = false;
        auto s = play (p);
        if (! clipped || tries == 3 || state[aVolume] <= 20)
            return s;
        set (aVolume, state[aVolume] - 24);
        rig.wait (30);
    }
}

std::vector<float> Bench::envelope (const std::vector<float>& x) const
{
    std::vector<double> sum (x.size() + 1, 0.0);
    for (size_t i = 0; i < x.size(); ++i)
        sum[i + 1] = sum[i] + (double) x[i] * x[i];
    const int msCount = (int) (ms ((int) x.size()));
    std::vector<float> e ((size_t) std::max (0, msCount));
    const int half = std::max (1, envWindow / 2);
    for (int k = 0; k < msCount; ++k)
    {
        const int c = at (k);
        const int a = std::max (0, c - half), b = std::min ((int) x.size(), c + half);
        e[(size_t) k] = b > a ? (float) std::sqrt (2.0 * (sum[(size_t) b] - sum[(size_t) a]) / (b - a)) : 0.0f;
    }
    return e;
}

double Bench::amplitude (const Shot& s, double fromMs, double toMs) const
{
    const int a = lead + at (fromMs);
    return rmsOf (s.x, a, at (toMs - fromMs)) * std::sqrt (2.0);
}

double Bench::frequency (const Shot& s, double fromMs, double toMs) const
{
    const int a = lead + at (fromMs), n = at (toMs - fromMs);
    if (rmsOf (s.x, a, n) < noise * 10.0)
        return 0.0;
    const double f = frequencyOf (s.x, a, n, sr);
    return f >= 20.0 && f <= sr * 0.45 ? f : 0.0;
}

template <typename Fn>
void Bench::group (const juce::String& label, Fn&& fn)
{
    const int doneBefore = done;
    for (int drifts = 0;;)
    {
        what = label;
        touched = false;
        rig.interrupted();
        int why = -1;
        try
        {
            fn();
            if (rig.interrupted())
                why = Rig::touched;
            else if (touched)
                why = Rig::levelChanged;
        }
        catch (const Redo& redo)
        {
            why = redo.why;
        }
        if (why < 0)
            return;
        if (why == Rig::levelChanged && ++drifts == 3)
        {
            warn (label + " : le niveau a bougé pendant cette partie, à vérifier."_fr);
            return;
        }
        rig.send (juce::MidiMessage::allNotesOff (channel));
        if (freeChannel > 0)
            rig.send (juce::MidiMessage::allNotesOff (freeChannel));
        if (! rig.pause (why))
            throw Stopped();
        check();
        done = doneBefore;
        state.fill (-1);                      // someone may have turned a knob: send everything again
    }
}

//==============================================================================
Result Bench::run()
{
    Result r;
    result = &r;
    report = new juce::DynamicObject();
    r.calibration = Calibration::defaults();
    sr = rig.sampleRate();
    lead = at (2.0);

    try
    {
        what = "On prépare le son de mesure"_fr;
        reset ({});
        if (start (r))
        {
            pitches (r);
            tuning (r);
            levels (r);
            attacks (r);
            falls (r);
            filter (r);
            speeds (r);
            samples (r);
            r.ok = true;
            r.calibration.source = ("mesures du " + r.when.formatted ("%d/%m/%Y")).toStdString();
            r.message = "Tout est mesuré."_fr;
        }
    }
    catch (const Stopped&)
    {
        r.ok = false;
        r.message = "Mesure arrêtée."_fr;
    }

    rig.send (juce::MidiMessage::allNotesOff (channel));
    if (freeChannel > 0)
        rig.send (juce::MidiMessage::allNotesOff (freeChannel));

    report->setProperty ("sampleRate", sr);
    report->setProperty ("latencyMs", ms (latency + lead));
    report->setProperty ("noiseDb", juce::Decibels::gainToDecibels (noise));
    report->setProperty ("warnings", r.warnings);
    report->setProperty ("date", r.when.toISO8601 (true));
   #if defined (RA_VERSION)
    report->setProperty ("app", RA_VERSION);
   #endif
    r.report = juce::var (report.get());
    result = nullptr;
    return r;
}

void Bench::tone()
{
    sr = rig.sampleRate();
    lead = at (2.0);
    state.fill (-1);
    try
    {
        reset ({});
        for (int pitch : { 96, 100, 104 })
        {
            set (o1Pitch, pitch);
            play ({ 300, 120 });
        }
    }
    catch (...) {}
    rig.send (juce::MidiMessage::allNotesOff (channel));
}

//==============================================================================
// Silence, then a few notes: how long they take to come back, and how loud.
bool Bench::start (Result& r)
{
    group ("On écoute le silence"_fr, [&]
    {
        rig.wait (150);
        const auto from = rig.mark();
        rig.wait (600);
        std::vector<float> x;
        rig.read (from, (int) (rig.mark() - from), x);
        noise = std::max (1e-6, rmsOf (x, 0, (int) x.size()));
    });

    std::vector<double> delays;
    float peak = 0.0f;
    group ("On mesure le temps de réponse"_fr, [&]
    {
        delays.clear();
        peak = 0.0f;
        latency = 0;
        const int saveLead = lead;
        lead = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto s = play ({ 160, 150 });
            float pk = 0.0f;
            for (auto v : s.x)
                pk = std::max (pk, std::abs (v));
            peak = std::max (peak, pk);
            const float thr = std::max ((float) noise * 8.0f, pk * 0.2f);
            for (size_t k = 0; k < s.x.size(); ++k)
                if (std::abs (s.x[k]) > thr)
                {
                    delays.push_back ((double) k);
                    break;
                }
        }
        lead = saveLead;
    });

    report->setProperty ("noiseRms", noise);
    report->setProperty ("peak", peak);
    report->setProperty ("delays", toVar (delays));
    if (peak < noise * 20.0f || delays.size() < 4)
    {
        r.message = "Le plugin n'entend pas le RackAttack : vérifie les câbles, l'entrée choisie et le gain de ta carte son."_fr;
        return false;
    }
    if (peak > 0.97f)
    {
        r.message = "Le son arrive trop fort et sature : baisse un peu le gain d'entrée de ta carte son, puis recommence."_fr;
        return false;
    }
    if (peak < 0.02f)
        warn ("Le son arrive très faiblement : les mesures seront moins précises (monte le gain d'entrée)."_fr);
    const double spread = *std::max_element (delays.begin(), delays.end()) - *std::min_element (delays.begin(), delays.end());
    if (ms ((int) spread) > 8.0)
        warn ("Le temps de réponse varie de "_fr + juce::String (ms ((int) spread), 1) + " ms : les temps courts seront moins précis."_fr);
    latency = std::max (0, (int) median (delays) - lead);
    return true;
}

//==============================================================================
void Bench::pitches (Result& r)
{
    Points pts;
    group ("On mesure les hauteurs des notes", [&]
    {
        pts.clear();
        reset ({});
        for (int v = 0; v <= 128; v += 4)
        {
            const int value = std::min (127, v);
            set (o1Pitch, value);
            const double f = frequency (play ({ 260, 40 }), 45, 230);
            if (f > 0.0)
                pts.push_back ({ value, f });
        }
    });
    report->setProperty ("pitch", toVar (pts));
    auto& c = r.calibration;
    if (clean (pts).size() < 8)
        warn ("Les hauteurs n'ont pas pu être bien mesurées : on garde celles du manuel."_fr);
    c.pitchHz = logTable (pts, c.pitchHz, 8);

    refPitch = nearest (c.pitchHz, 700.0);
    group ("On accorde la note de référence"_fr, [&]
    {
        reset ({});
        const double f = frequency (play ({ 300, 40 }), 50, 280);
        refHz = f > 0.0 ? f : c.pitchHz[(size_t) refPitch];
    });
    envWindow = std::max (16, (int) std::lround (2.0 * sr / refHz));
    report->setProperty ("refPitch", refPitch);
    report->setProperty ("refHz", refHz);
}

//==============================================================================
// Detune, how far the pitch modulation goes, and the note of a tuned sound.
void Bench::tuning (Result& r)
{
    auto& c = r.calibration;
    auto semis = [] (double f, double f0) { return 12.0 * std::log2 (f / f0); };

    double detune = 0, mod = 0, root = 0, slope = 0;
    bool haveDetune = false, haveMod = false, haveRoot = false;
    group ("On mesure le désaccord et la modulation"_fr, [&]
    {
        reset ({});
        const double f0 = frequency (play ({ 300, 40 }), 50, 280);
        set (o1Detune, 127);
        const double f1 = frequency (play ({ 300, 40 }), 50, 280);
        set (o1Detune, 0);
        haveDetune = f0 > 0.0 && f1 > 0.0;
        if (haveDetune)
            detune = semis (f1, f0);

        // Envelope 2 stays at its top: the pitch moves by half of the full depth
        reset ({ { o1PitchMod, 96 }, { o1PitchModSrc, 1 } });
        const double fm = frequency (play ({ 300, 40 }), 80, 280);
        haveMod = f0 > 0.0 && fm > 0.0;
        if (haveMod)
            mod = semis (fm, f0) / 0.5;

        haveRoot = false;
        if (freeChannel > 0 && f0 > 0.0)
        {
            reset ({ { tunedChannel, freeChannel } });
            std::vector<double> roots, offsets;
            for (int note : { 48, 60, 72 })
            {
                Play p { 300, 40 };
                p.note = note;
                p.channel = freeChannel;
                const double f = frequency (play (p), 50, 280);
                if (f > 0.0)
                {
                    roots.push_back (note - semis (f, f0));
                    offsets.push_back (semis (f, f0));
                }
            }
            reset ({});
            if (roots.size() == 3)
            {
                haveRoot = true;
                root = median (roots);
                slope = (offsets[2] - offsets[0]) / 24.0;
            }
        }
    });

    report->setProperty ("detuneSemitones", detune);
    report->setProperty ("pitchModSemitones", mod);
    report->setProperty ("tunedRoot", root);
    report->setProperty ("tunedSemitonesPerNote", slope);
    if (haveDetune && detune > 0.05 && detune < 13.0) c.detuneSemitones = (float) detune;
    if (haveMod && mod > 1.0 && mod < 130.0) c.pitchModSemitones = (float) mod;
    if (haveRoot && std::abs (slope - 1.0) < 0.1) c.tunedRootNote = (int) std::lround (root);
    else if (freeChannel > 0) warn ("Le son accordé (Tuned) n'a pas répondu comme prévu : note de référence non mesurée."_fr);
}

//==============================================================================
// Volume, velocity, sustain: amplitudes of a steady sine.
void Bench::levels (Result& r)
{
    auto& c = r.calibration;
    Points volume, velocity, sustain;
    double env1Left = 1.0;
    group ("On mesure les volumes", [&]
    {
        volume.clear(); velocity.clear(); sustain.clear();
        reset ({});
        const double ref = amplitude (play ({ 260, 40 }), 60, 230);

        for (int v : everyEight())
        {
            set (aVolume, v);
            volume.push_back ({ v, amplitude (play ({ 260, 40 }), 60, 230) });
        }
        set (aVolume, 100);

        for (int vel : { 1, 16, 32, 48, 64, 80, 96, 112, 127 })
        {
            Play p { 260, 40 };
            p.velocity = vel;
            velocity.push_back ({ vel, amplitude (play (p), 60, 230) });
        }

        // Envelope 1 falling at once to nothing: does the sound stop?
        reset ({ { env1 + decayOf, 0 }, { env1 + sustainOf, 0 } });
        env1Left = amplitude (play ({ 500, 40 }), 300, 480) / std::max (1e-9, ref);

        if (env1Left < 0.1)
            for (int s : everyEight())
            {
                set (env1 + sustainOf, s);
                sustain.push_back ({ s, amplitude (play ({ 640, 40 }), 480, 620) });
            }

        reset ({});
        const double again = amplitude (play ({ 260, 40 }), 60, 230);
        if (std::abs (again / std::max (1e-9, ref) - 1.0) > 0.12)
            touched = true;
    });

    report->setProperty ("volume", toVar (volume));
    report->setProperty ("velocity", toVar (velocity));
    report->setProperty ("sustain", toVar (sustain));
    report->setProperty ("env1Left", env1Left);

    auto normalise = [] (Points p)
    {
        const double top = p.empty() ? 1.0 : std::max (1e-9, p.back().second);
        for (auto& q : p)
            q.second /= top;
        return p;
    };
    auto floor = [this] (Points p, double ref)
    {
        for (auto& q : p)
            if (q.second < noise * 3.0 / std::max (1e-9, ref))
                q.second = 0.0;
        return p;
    };

    if (volume.size() >= 8 && volume.back().second > noise * 20.0)
        c.volumeGain = linTable (floor (normalise (volume), volume.back().second), c.volumeGain);

    if (velocity.size() == 9 && velocity.back().second > noise * 20.0)
    {
        auto vel = normalise (velocity);
        c.velocityToAmp = vel.front().second < 0.85;
        if (c.velocityToAmp)
            c.velocityGain = linTable (floor (vel, velocity.back().second), c.velocityGain);
    }

    c.env1ToAmp = env1Left < 0.1;
    if (! c.env1ToAmp)
        warn ("L'enveloppe 1 ne semble pas régler le volume : les enveloppes ne sont pas mesurées."_fr);
    if (sustain.size() >= 8 && sustain.back().second > noise * 20.0)
    {
        c.sustainLevel = linTable (floor (normalise (sustain), sustain.back().second), c.sustainLevel);
        c.sustainLevel[0] = 0.0f;
    }
}

//==============================================================================
void Bench::attacks (Result& r)
{
    auto& c = r.calibration;
    if (! c.env1ToAmp)
        return;
    Points pts;
    bool instant = false;
    group ("On mesure les attaques", [&]
    {
        pts.clear();
        reset ({});
        const double ref = amplitude (play ({ 260, 40 }), 60, 230);
        for (int v : everyEight())
        {
            set (env1 + attackOf, v);
            Play p { 7000, 40 };
            p.holdUntil = [this, ref] (const Shot& s)
            {
                const auto e = envelope (s.x);
                return std::any_of (e.begin(), e.end(), [ref] (float a) { return a >= 0.95 * ref; });
            };
            const auto e = envelope (play (p).x);
            const double leadMs = ms (lead);
            auto cross = [&e, leadMs] (double level)
            {
                for (size_t k = 1; k < e.size(); ++k)
                    if (e[k] >= level)
                        return (double) k - 1.0 + (level - e[k - 1]) / std::max (1e-9f, e[k] - e[k - 1]) - leadMs;
                return -1.0;
            };
            // the middle of the rise, where the envelope's window doesn't blur it
            const double t25 = cross (0.25 * ref), t75 = cross (0.75 * ref);
            if (t25 < 0.0 || t75 < 0.0)
                continue;                               // longer than we waited
            const double total = (t75 - t25) / 0.5;
            if (v == 0)
                instant = total < 2.0;
            if (total >= 2.0)
                pts.push_back ({ v, total });
        }
        set (env1 + attackOf, 0);
    });
    report->setProperty ("attack", toVar (pts));
    if (clean (pts).size() >= 4)
    {
        c.attackMs = logTable (pts, c.attackMs, 4);
        if (instant)
            c.attackMs[0] = 0.0f;
    }
    else
        warn ("Les attaques n'ont pas pu être mesurées."_fr);
}

//==============================================================================
// Decay times, the decay shapes, release times.
void Bench::falls (Result& r)
{
    auto& c = r.calibration;
    if (! c.env1ToAmp)
        return;

    // the moment the envelope falls under a level, looking from "fromMs", in ms after
    // "originMs" (the note on or the note off)
    auto fallsUnder = [] (const std::vector<float>& e, double level, double fromMs, double originMs)
    {
        for (size_t k = (size_t) std::max (1.0, std::ceil (fromMs)); k < e.size(); ++k)
            if (e[k] < level)
                return (double) k - 1.0 + (e[k - 1] - level) / std::max (1e-9f, e[k - 1] - e[k]) - originMs;
        return -1.0;
    };

    Points decay, release;
    std::vector<std::vector<float>> shapes;
    double ref = 0.0, endMs = 0.0;
    int d0 = 64;
    group ("On mesure les chutes", [&]
    {
        decay.clear(); release.clear(); shapes.clear();
        reset ({});
        ref = amplitude (play ({ 260, 40 }), 60, 230);
        const double leadMs = ms (lead);

        reset ({ { env1 + sustainOf, 0 } });
        for (int v : everyEight())
        {
            set (env1 + decayOf, v);
            Play p { 7000, 40 };
            p.holdUntil = [this, ref, leadMs, &fallsUnder] (const Shot& s) { return fallsUnder (envelope (s.x), 0.4 * ref, leadMs + 1.0, leadMs) >= 0.0; };
            const double t = fallsUnder (envelope (play (p).x), 0.5 * ref, leadMs + 1.0, leadMs);
            if (t >= 0.0)
                decay.push_back ({ v, t });
        }

        // the shapes, on a decay of about a second
        d0 = 64;
        double best = 1e9;
        for (auto& [v, t] : decay)
            if (std::abs (std::log (std::max (0.1, t) / 120.0)) < best)
            {
                best = std::abs (std::log (std::max (0.1, t) / 120.0));
                d0 = v;
            }
        set (env1 + decayOf, d0);
        for (int i = 0; i < 9; ++i)
        {
            const int shape = (int) std::lround (i * 127.0 / 8.0);
            set (env1 + shapeOf, shape);
            Play p { 8000, 40 };
            p.holdUntil = [this, ref, leadMs, &fallsUnder] (const Shot& s)
            {
                const auto e = envelope (s.x);
                const double t = fallsUnder (e, 0.004 * ref, leadMs + 1.0, leadMs);
                return t >= 0.0 && (double) e.size() > leadMs + t + 60.0;
            };
            shapes.push_back (envelope (play (p).x));
        }
        set (env1 + shapeOf, 0);

        // release, from a full sustain
        reset ({});
        for (int v : everyEight (8))
        {
            set (env1 + releaseOf, v);
            Play p { 220, 7000 };
            p.afterUntil = [this, ref, &fallsUnder] (const Shot& s) { return fallsUnder (envelope (s.x), 0.4 * ref, ms (s.noteOffAt) + 1.0, ms (s.noteOffAt)) >= 0.0; };
            const auto s = play (p);
            const double t = fallsUnder (envelope (s.x), 0.5 * ref, ms (s.noteOffAt), ms (s.noteOffAt));
            if (t >= 0.0)
                release.push_back ({ v, t });
        }
        set (env1 + releaseOf, 10);

        const double again = amplitude (play ({ 260, 40 }), 60, 230);
        if (std::abs (again / std::max (1e-9, ref) - 1.0) > 0.12)
            touched = true;
    });

    report->setProperty ("decayHalf", toVar (decay));
    report->setProperty ("releaseHalf", toVar (release));
    report->setProperty ("shapeDecayValue", d0);

    // The length of a decay: when every shape has fallen under 1 %
    const double leadMs = ms (lead);
    endMs = 0.0;
    bool shapesOk = shapes.size() == 9;
    for (auto& e : shapes)
    {
        double t = -1.0;
        for (size_t k = (size_t) leadMs + 1; k < e.size(); ++k)
            if (e[k] < 0.01 * ref)
            {
                bool stays = true;
                for (size_t j = k; j < std::min (e.size(), k + 20); ++j)
                    stays = stays && e[j] < 0.01 * ref;
                if (stays) { t = (double) k - leadMs; break; }
            }
        if (t < 0.0)
            shapesOk = false;
        endMs = std::max (endMs, t);
    }
    if (! shapesOk || endMs < 5.0)
    {
        warn ("Les formes de chute n'ont pas pu être mesurées."_fr);
        return;
    }

    c.fallCurves.clear();
    juce::Array<juce::var> curvesVar;
    for (auto& e : shapes)
    {
        std::array<float, 64> curve {};
        float lowest = 1.0f;
        for (int k = 1; k < 64; ++k)               // (at 0 the envelope only half sees the note)
        {
            const double t = leadMs + endMs * k / 63.0;
            const size_t i = (size_t) t;
            const double f = t - (double) i;
            double a = i + 1 < e.size() ? e[i] * (1.0 - f) + e[i + 1] * f : 0.0;
            lowest = std::min (lowest, (float) juce::jlimit (0.0, 1.0, a / ref));
            curve[(size_t) k] = lowest;
        }
        curve[0] = 1.0f;
        curve[63] = 0.0f;
        c.fallCurves.push_back (curve);
        curvesVar.add (toVar (curve));
    }
    report->setProperty ("fallCurves", curvesVar);
    report->setProperty ("fallLengthMs", endMs);

    // where the first curve (shape 0) crosses half
    double u50 = 0.0;
    for (int k = 1; k < 64; ++k)
        if (c.fallCurves[0][(size_t) k] < 0.5f)
        {
            const float a = c.fallCurves[0][(size_t) k - 1], b = c.fallCurves[0][(size_t) k];
            u50 = ((double) k - 1.0 + (a - 0.5) / std::max (1e-6f, a - b)) / 63.0;
            break;
        }
    if (u50 <= 0.0)
        return;
    for (auto& p : decay) p.second /= u50;
    for (auto& p : release) p.second /= u50;
    if (clean (decay).size() >= 4)
        c.decayMs = logTable (decay, c.decayMs, 4);
    else
        warn ("Les temps de chute n'ont pas pu être mesurés."_fr);
    if (clean (release).size() >= 4)
        c.releaseMs = logTable (release, c.releaseMs, 4);
    else
        warn ("Les temps de relâchement n'ont pas pu être mesurés."_fr);
}

//==============================================================================
void Bench::filter (Result& r)
{
    auto& c = r.calibration;
    const int fftSize = 8192;
    Points cutoff, rings;
    int threshold = -1;
    std::vector<std::pair<int, double>> damping;
    juce::Array<juce::var> types;
    double modDepth = 0.0;

    group ("On mesure le filtre", [&]
    {
        cutoff.clear(); rings.clear(); damping.clear(); types.clear();
        threshold = -1;

        // 1. the frequency it rings at, for each cutoff (a whisper of noise to start it)
        reset ({ { o1Shape, 5 }, { o1Pitch, 64 }, { mixO1, 4 }, { fType, 1 }, { fReso, 127 }, { aVolume, 70 } });
        for (int v : everyEight())
        {
            set (fCutoff, v);
            const auto s = playLoud ({ 420, 40 });
            double purity = 0.0;
            const double f = peakFrequency (s.x, lead + at (120), at (290), sr, 20.0, sr * 0.45, &purity);
            if (purity > 20.0 && f >= 25.0 && f <= sr * 0.45)
                cutoff.push_back ({ v, f });
        }
    });
    report->setProperty ("cutoff", toVar (cutoff));
    if (clean (cutoff).size() >= 6)
        c.cutoffHz = logTable (cutoff, c.cutoffHz, 6);
    else
        warn ("Les fréquences du filtre n'ont pas pu être mesurées : on garde celles du manuel."_fr);

    group ("On cherche quand le filtre siffle", [&]
    {
        rings.clear();
        reset ({ { o1Shape, 5 }, { o1Pitch, 64 }, { mixO1, 4 }, { fType, 1 }, { fReso, 90 }, { aVolume, 70 },
                 { fCutoff, nearest (c.cutoffHz, 1000.0) } });
        for (int v = 90; v <= 127; v += 2)
        {
            set (fReso, std::min (127, v));
            rings.push_back ({ std::min (127, v), rmsOf (playLoud ({ 480, 40 }).x, lead + at (260), at (200)) });
        }
        double jump = 0.0;
        for (size_t i = 1; i < rings.size(); ++i)
        {
            const double ratio = rings[i].second / std::max (1e-9, rings[i - 1].second);
            if (ratio > jump)
            {
                jump = ratio;
                threshold = rings[i].first;
            }
        }
        if (jump < 3.0)
            threshold = -1;
    });
    report->setProperty ("ringing", toVar (rings));
    report->setProperty ("selfOscillation", threshold);
    if (threshold > 0)
        c.selfOscillation = (float) threshold;
    else
        warn ("On n'a pas trouvé à partir de quand le filtre siffle tout seul."_fr);

    // 2. how much each resonance lifts, against the noise heard without the filter
    group ("On mesure la résonance"_fr, [&]
    {
        damping.clear();
        const int cv = nearest (c.cutoffHz, 1500.0);
        const double fc = c.cutoffHz[(size_t) cv];
        reset ({ { o1Shape, 5 }, { o1Pitch, 64 }, { mixO1, 24 }, { fType, 0 }, { fReso, 0 }, { aVolume, 90 }, { fCutoff, cv } });
        const auto plain = welch (playLoud ({ 1750, 40 }).x, lead + at (100), at (1600), fftSize);
        const double binHz = sr / fftSize;
        auto band = [&] (const std::vector<double>& p, double f0, double f1)
        {
            double a = 0.0, b = 0.0;
            for (int k = std::max (1, (int) (f0 / binHz)); k <= std::min ((int) p.size() - 1, (int) (f1 / binHz)); ++k)
            {
                a += p[(size_t) k];
                b += plain[(size_t) k];
            }
            return b > 0.0 ? std::sqrt (a / b) : 0.0;
        };
        set (fType, 1);
        const int top = (int) c.selfOscillation - 2;
        for (int v = 0; v <= top; v += 8)
        {
            set (fReso, v);
            const auto p = welch (playLoud ({ 1750, 40 }).x, lead + at (100), at (1600), fftSize);
            const double g = band (p, fc / 24.0, fc / 10.0);           // under the cutoff: the level that goes through
            const double h = band (p, fc * 0.97, fc * 1.03);
            double peak = 0.0;
            for (double f = fc * 0.6; f <= fc * 1.6; f *= 1.01)
                peak = std::max (peak, band (p, f * 0.985, f * 1.015));
            if (g <= 0.0 || h <= 0.0)
                continue;
            double k;
            const double m = peak / g;
            if (m > 1.3)
                k = std::sqrt (std::max (0.0, 2.0 - 2.0 * std::sqrt (std::max (0.0, 1.0 - 1.0 / (m * m)))));
            else
                k = g / h;
            damping.push_back ({ v, juce::jlimit (0.005, 2.5, k) });
        }

        // the filter types, for the record
        set (fReso, 64);
        for (int type = 0; type < 8; ++type)
        {
            set (fType, type);
            const auto p = welch (playLoud ({ 1250, 40 }).x, lead + at (100), at (1100), fftSize);
            juce::Array<juce::var> bands;
            for (int b = 0; b < 32; ++b)
            {
                const double f = 50.0 * std::pow (320.0, b / 31.0);
                bands.add (juce::Decibels::gainToDecibels (band (p, f / 1.1, f * 1.1), -120.0));
            }
            types.add (bands);
        }

        // 3. how far a modulation moves the cutoff: Envelope 2 at its top, a quarter of the depth
        reset ({ { o1Shape, 5 }, { o1Pitch, 64 }, { mixO1, 4 }, { fType, 1 }, { fReso, 127 }, { aVolume, 70 },
                 { fCutoff, nearest (c.cutoffHz, 300.0) } });
        double p0 = 0.0, p1 = 0.0;
        const double f0 = peakFrequency (playLoud ({ 420, 40 }).x, lead + at (120), at (290), sr, 20.0, sr * 0.45, &p0);
        reset ({ { o1Shape, 5 }, { o1Pitch, 64 }, { mixO1, 4 }, { fType, 1 }, { fReso, 127 }, { aVolume, 70 },
                 { fCutoff, nearest (c.cutoffHz, 300.0) }, { fMod1, 80 }, { fMod1Src, 1 } });
        const double f1 = peakFrequency (playLoud ({ 420, 40 }).x, lead + at (120), at (290), sr, 20.0, sr * 0.45, &p1);
        modDepth = p0 > 20.0 && p1 > 20.0 ? 12.0 * std::log2 (f1 / f0) / 0.25 : 0.0;
    });

    report->setProperty ("damping", toVar (damping));
    report->setProperty ("filterTypesDb", types);
    report->setProperty ("cutoffModSemitones", modDepth);
    if (modDepth > 2.0 && modDepth < 200.0)
        c.cutoffModSemitones = (float) modDepth;

    if (damping.size() >= 4)
    {
        const float thr = c.selfOscillation;
        Points pts (damping.begin(), damping.end());
        pts.push_back ({ (int) thr, 0.0 });
        const auto below = linTable (pts, c.resonanceK, 4);
        for (int v = 0; v < 128; ++v)
            c.resonanceK[(size_t) v] = (float) v < thr ? below[(size_t) v] : 2.0f * (1.0f - (float) v / thr);
    }
    else
    {
        warn ("La résonance n'a pas pu être mesurée."_fr);
        for (int v = 0; v < 128; ++v)
            c.resonanceK[(size_t) v] = 2.0f * (1.0f - (float) v / c.selfOscillation);
    }
}

//==============================================================================
void Bench::speeds (Result& r)
{
    auto& c = r.calibration;
    Points crack, lfo;
    group ("On mesure le crack", [&]
    {
        crack.clear();
        // a still wave (a square too slow to move): what comes out is the crack itself
        reset ({ { o1Shape, 2 }, { o1Pitch, 0 }, { o1Start, 0 }, { mixCrack, 127 }, { crackLength, 127 } });
        for (int v : everyEight())
        {
            set (crackSpeed, v);
            const double f = frequency (play ({ 520, 40 }), 60, 480);
            if (f > 0.0 && f < sr * 0.4)
                crack.push_back ({ v, f });
        }
    });
    report->setProperty ("crack", toVar (crack));
    if (clean (crack).size() >= 5)
        c.crackHz = logTable (crack, c.crackHz, 5);
    else
        warn ("La vitesse du crack n'a pas pu être mesurée."_fr);

    group ("On mesure les LFO", [&]
    {
        lfo.clear();
        reset ({ { aVolMod, 127 }, { aVolModSrc, 2 } });
        double expected = 0.0, last = 0.0;
        for (int v = 127; v >= 0; v -= 8)
        {
            const double durMs = expected > 0.0 ? juce::jlimit (700.0, 4500.0, 4.5 / expected * 1000.0) : 1500.0;
            if (expected > 0.0 && expected < 0.8)
                break;
            set (l1Speed, v);
            const auto e = envelope (play ({ durMs, 40 }).x);
            const int from = (int) ms (lead) + 100, count = (int) e.size() - from - 60;
            double f = count > 200 ? frequencyOf (e, from, count, 1000.0) : 0.0;
            const double minHz = 2.5 / (count / 1000.0);
            if (f < minHz || f > 150.0)
                f = 0.0;
            if (f > 0.0)
            {
                lfo.push_back ({ v, f });
                expected = last > 0.0 ? f * f / last : f / 1.6;
                last = f;
            }
            else if (expected > 0.0)
                expected /= 1.6;
            else
                expected = 0.5;
        }
        set (l1Speed, 64);
    });
    report->setProperty ("lfo", toVar (lfo));
    if (clean (lfo).size() >= 4)
        c.lfoHz = logTable (lfo, c.lfoHz, 4);
    else
        warn ("La vitesse des LFO n'a pas pu être mesurée."_fr);
}

//==============================================================================
// The HH and Crash samples: recorded at two pitches.
void Bench::samples (Result& r)
{
    auto& c = r.calibration;
    struct Take { std::vector<float> x; double t90 = 0.0; };
    std::array<std::array<Take, 2>, 3> takes;
    double ref = 0.0;
    group ("On enregistre les cymbales", [&]
    {
        reset ({});
        ref = amplitude (play ({ 260, 40 }), 60, 230);
        for (int shape = 0; shape < 3; ++shape)
            for (int i = 0; i < 2; ++i)
            {
                reset ({ { o1Shape, 6 + shape }, { o1Pitch, i == 0 ? 64 : 80 } });
                Play p { 6000, 60 };
                p.holdUntil = [this] (const Shot& s)
                {
                    const auto e = envelope (s.x);
                    if (e.size() < 400)
                        return false;
                    const float top = *std::max_element (e.begin(), e.end());
                    const float tail = *std::max_element (e.end() - 150, e.end());
                    return tail < std::max (0.002f * top, (float) noise * 3.0f);
                };
                auto s = play (p);

                // from the note, to the end of the sound
                auto& x = s.x;
                x.erase (x.begin(), x.begin() + std::min ((int) x.size(), lead));
                float top = 0.0f;
                for (auto v : x)
                    top = std::max (top, std::abs (v));
                int end = (int) x.size();
                while (end > 1 && std::abs (x[(size_t) end - 1]) < 0.001f * top)
                    --end;
                end = std::min ((int) x.size(), end + at (10.0));
                x.resize ((size_t) end);
                const int fade = std::min (end, at (5.0));
                for (int k = 0; k < fade; ++k)
                    x[(size_t) (end - 1 - k)] *= (float) k / (float) fade;

                double total = 0.0, run = 0.0;
                for (auto v : x)
                    total += (double) v * v;
                for (size_t k = 0; k < x.size(); ++k)
                {
                    run += (double) x[k] * x[k];
                    if (run >= 0.9 * total)
                    {
                        takes[(size_t) shape][(size_t) i].t90 = ms ((int) k);
                        break;
                    }
                }
                takes[(size_t) shape][(size_t) i].x = std::move (x);
            }
        reset ({});
    });

    static const char* names[] = { "hh-ferme", "hh-ouvert", "crash" };
    std::vector<double> steps;
    juce::Array<juce::var> durations;
    for (int shape = 0; shape < 3; ++shape)
    {
        auto& a = takes[(size_t) shape][0];
        auto& b = takes[(size_t) shape][1];
        durations.add (juce::Array<juce::var> { a.t90, b.t90 });
        if (a.t90 > 5.0 && b.t90 > 5.0)
            steps.push_back (12.0 * std::log2 (a.t90 / b.t90) / 16.0);
        for (int i = 0; i < 2; ++i)
            if (! takes[(size_t) shape][(size_t) i].x.empty())
                r.recordings.push_back ({ juce::String (names[shape]) + (i == 0 ? "" : "-80") + ".wav",
                                          takes[(size_t) shape][(size_t) i].x, sr });
    }
    const double perStep = juce::jlimit (0.0, 3.0, median (steps));
    report->setProperty ("sampleT90Ms", durations);
    report->setProperty ("sampleSemitonesPerStep", perStep);

    if (ref <= noise * 20.0)
    {
        warn ("Les cymbales n'ont pas pu être enregistrées."_fr);
        return;
    }
    for (int shape = 0; shape < 3; ++shape)
    {
        auto& x = takes[(size_t) shape][0].x;
        if (x.size() < 64)
            continue;
        auto& sample = c.samples[shape];
        sample.data.resize (x.size());
        for (size_t k = 0; k < x.size(); ++k)
            sample.data[k] = (float) (x[k] / ref);   // the level of a full sine
        sample.rate = sr;
        sample.rootPitch = 64;
        sample.semitonesPerStep = (float) perStep;
    }
}

//==============================================================================
SynthRig::SynthRig (const Calibration& truth, int channel, int baseNote, double sampleRate, int latencySamples, bool rt)
    : rate (sampleRate), lat (std::max (1, latencySamples)), realTime (rt)
{
    synth.prepare (rate);
    synth.setCalibration (truth);
    synth.setGlobalChannel (channel);
    synth.setBaseNote (baseNote);
    delay.assign ((size_t) lat, 0.0f);
    hpA = (float) std::exp (-2.0 * pi * 5.0 / rate);     // the interface's AC coupling, about 5 Hz
}

void SynthRig::send (const juce::MidiMessage& m)
{
    synth.handleMidi (m.getRawData(), m.getRawDataSize());
}

void SynthRig::wait (double ms)
{
    owed += ms * rate / 1000.0;
    int n = (int) owed;
    owed -= n;
    float L[256], R[256], out[256];
    while (n > 0)
    {
        const int block = std::min (n, 256);
        synth.render (L, R, block);
        float peak = level.load() * 0.9f;
        for (int i = 0; i < block; ++i)
        {
            const float x = (L[i] + R[i]) * 0.5f * gain + noiseLevel * (random.nextFloat() * 2.0f - 1.0f);
            const float y = hpA * (hpY + x - hpX);
            hpX = x;
            hpY = y;
            out[i] = delay[(size_t) delayPos];
            delay[(size_t) delayPos] = y;
            delayPos = (delayPos + 1) % lat;
            peak = std::max (peak, std::abs (out[i]));
        }
        ring.write (out, block);
        level = peak;
        now += block;
        n -= block;
        if (realTime)
        {
            if (stop.load())
                return;
            sleepOwed += block * 1000.0 / rate / std::max (0.1, speed);
            if (sleepOwed >= 5.0)
            {
                juce::Thread::sleep ((int) sleepOwed);
                sleepOwed -= (int) sleepOwed;
            }
        }
    }
}

bool SynthRig::pause (int why)
{
    if (! waitWhenPaused)
        return true;
    pausedFor = why;
    level = 0.0f;
    while (pausedFor.load() >= 0)
    {
        if (stop.load())
            return false;
        juce::Thread::sleep (50);
    }
    pauseWanted = false;
    touch = false;
    return ! stop.load();
}

int SynthRig::read (std::int64_t from, int count, std::vector<float>& out)
{
    return ring.read (from, count, out);
}

//==============================================================================
Recorder::Recorder()
{
    for (auto& p : peaks)
        p = 0.0f;
}

std::int64_t Recorder::position() const
{
    const auto end = blockEnd.load();
    const double since = juce::Time::getMillisecondCounterHiRes() - blockMs.load();
    const auto ahead = (std::int64_t) juce::jlimit (0.0, rate.load() * 0.05, since * rate.load() / 1000.0);
    return end + ahead;
}

float Recorder::peak (int ch) const
{
    return ch >= 0 && ch < (int) peaks.size() ? peaks[(size_t) ch].load() : 0.0f;
}

float Recorder::takePeak (int ch)
{
    return ch >= 0 && ch < (int) peaks.size() ? peaks[(size_t) ch].exchange (0.0f) : 0.0f;
}

void Recorder::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    rate = d != nullptr ? d->getCurrentSampleRate() : 48000.0;
    inputs = d != nullptr ? d->getActiveInputChannels().countNumberOfSetBits() : 0;
    mono.assign ((size_t) std::max (512, d != nullptr ? d->getCurrentBufferSizeSamples() * 2 : 4096), 0.0f);
    ring.reset();
    blockEnd = 0;
    blockMs = juce::Time::getMillisecondCounterHiRes();
    active = true;
}

void Recorder::audioDeviceStopped()
{
    active = false;
}

void Recorder::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                 int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], n);
    if ((int) mono.size() < n)
        return;                              // never happens: sized at start

    const auto use = channelMask.load();
    std::fill (mono.begin(), mono.begin() + n, 0.0f);
    int used = 0;
    for (int c = 0; c < numIn; ++c)
    {
        if (in[c] == nullptr)
            continue;
        if (c < (int) peaks.size())
        {
            const float pk = juce::FloatVectorOperations::findMaximum (in[c], n);
            const float lo = juce::FloatVectorOperations::findMinimum (in[c], n);
            const float a = std::max (pk, -lo);
            if (a > peaks[(size_t) c].load())
                peaks[(size_t) c] = a;
        }
        if (c < 32 && (use >> c) & 1u)
        {
            juce::FloatVectorOperations::add (mono.data(), in[c], n);
            ++used;
        }
    }
    if (used > 1)
        juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) used, n);
    ring.write (mono.data(), n);
    blockEnd = ring.written();
    blockMs = juce::Time::getMillisecondCounterHiRes();
}

//==============================================================================
LiveRig::LiveRig (Engine& e, Recorder& r)
    : engine (e), recorder (r), seen (e.unitMessages.load())
{
}

void LiveRig::send (const juce::MidiMessage& m)
{
    engine.sendQuiet (m);
}

void LiveRig::wait (double ms)
{
    const double until = juce::Time::getMillisecondCounterHiRes() + ms;
    for (;;)
    {
        const double left = until - juce::Time::getMillisecondCounterHiRes();
        if (left <= 0.0 || stop.load())
            return;
        juce::Thread::sleep ((int) std::max (1.0, std::min (left, 20.0)));
    }
}

int LiveRig::read (std::int64_t from, int count, std::vector<float>& out)
{
    return recorder.recording().read (from, count, out);
}

bool LiveRig::interrupted()
{
    const int now = engine.unitMessages.load();
    const bool changed = now != seen;
    seen = now;
    return changed;
}

bool LiveRig::pause (int why)
{
    pausedFor = why;
    while (pausedFor.load() >= 0)
    {
        if (stop.load())
            return false;
        juce::Thread::sleep (50);
    }
    seen = engine.unitMessages.load();
    pauseWanted = false;
    return ! stop.load();
}

//==============================================================================
// The file: calibration.json, report.json, the recordings as 32-bit float WAV.

namespace
{
    template <size_t N>
    juce::var table (const std::array<float, N>& t) { return toVar (t); }

    template <size_t N>
    bool readTable (const juce::var& v, std::array<float, N>& t)
    {
        auto* a = v.getArray();
        if (a == nullptr || a->size() != (int) N)
            return false;
        for (size_t i = 0; i < N; ++i)
            t[i] = (float) (double) a->getReference ((int) i);
        return true;
    }

    const char* sampleFiles[] = { "hh-ferme.wav", "hh-ouvert.wav", "crash.wav" };

    juce::var calibrationVar (const Calibration& c)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("format", 1);
        o->setProperty ("source", juce::String (c.source));
        o->setProperty ("pitchHz", table (c.pitchHz));
        o->setProperty ("cutoffHz", table (c.cutoffHz));
        o->setProperty ("attackMs", table (c.attackMs));
        o->setProperty ("decayMs", table (c.decayMs));
        o->setProperty ("releaseMs", table (c.releaseMs));
        o->setProperty ("lfoHz", table (c.lfoHz));
        o->setProperty ("crackHz", table (c.crackHz));
        o->setProperty ("volumeGain", table (c.volumeGain));
        o->setProperty ("velocityGain", table (c.velocityGain));
        o->setProperty ("sustainLevel", table (c.sustainLevel));
        o->setProperty ("resonanceK", table (c.resonanceK));
        juce::Array<juce::var> curves;
        for (auto& curve : c.fallCurves)
            curves.add (table (curve));
        o->setProperty ("fallCurves", curves);
        o->setProperty ("pitchModSemitones", c.pitchModSemitones);
        o->setProperty ("detuneSemitones", c.detuneSemitones);
        o->setProperty ("cutoffModSemitones", c.cutoffModSemitones);
        o->setProperty ("fmMaxIndex", c.fmMaxIndex);
        o->setProperty ("selfOscillation", c.selfOscillation);
        o->setProperty ("tunedRootNote", c.tunedRootNote);
        o->setProperty ("env1ToAmp", c.env1ToAmp);
        o->setProperty ("velocityToAmp", c.velocityToAmp);
        juce::Array<juce::var> samples;
        for (int i = 0; i < 3; ++i)
        {
            auto* s = new juce::DynamicObject();
            s->setProperty ("file", c.samples[i].data.empty() ? juce::String() : juce::String (sampleFiles[i]));
            s->setProperty ("rate", c.samples[i].rate);
            s->setProperty ("rootPitch", c.samples[i].rootPitch);
            s->setProperty ("semitonesPerStep", c.samples[i].semitonesPerStep);
            samples.add (juce::var (s));
        }
        o->setProperty ("samples", samples);
        return juce::var (o);
    }

    juce::MemoryBlock wav (const std::vector<float>& x, double rate)
    {
        juce::MemoryBlock block;
        {
            auto stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), rate, 1, 32, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                const float* channels[] = { x.data() };
                writer->writeFromFloatArrays (channels, 1, (int) x.size());
            }
        }
        return block;
    }

    bool readWav (juce::ZipFile& zip, const juce::String& name, std::vector<float>& x, double& rate)
    {
        const int index = zip.getIndexOfFileName (name);
        if (index < 0)
            return false;
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (index));
        if (in == nullptr)
            return false;
        juce::MemoryBlock data;
        in->readIntoMemoryBlock (data);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (new juce::MemoryInputStream (data, true), true));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > 20 * 192000)
            return false;
        juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false);
        x.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
        rate = reader->sampleRate;
        return true;
    }

    juce::String text (juce::ZipFile& zip, const juce::String& name)
    {
        const int index = zip.getIndexOfFileName (name);
        if (index < 0)
            return {};
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (index));
        return in != nullptr ? in->readEntireStreamAsString() : juce::String();
    }
}

juce::File defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("RackAttack Editor").getChildFile ("Mesures");
}

juce::File save (const Result& r, const juce::File& folder, const juce::String& prefix)
{
    if (! folder.createDirectory())
        return {};
    const auto file = folder.getNonexistentChildFile (prefix + r.when.formatted ("%Y-%m-%d-%Hh%M"), ".ramesure", false);

    juce::ZipFile::Builder zip;
    auto addText = [&zip] (const juce::String& name, const juce::String& content)
    {
        zip.addEntry (new juce::MemoryInputStream (content.toRawUTF8(), content.getNumBytesAsUTF8(), true), 9, name, juce::Time::getCurrentTime());
    };
    addText ("calibration.json", juce::JSON::toString (calibrationVar (r.calibration)));
    addText ("report.json", juce::JSON::toString (r.report));
    addText ("LISEZMOI.txt",
             "Mesures d'un Waldorf RackAttack faites avec RackAttack Editor, le " + r.when.formatted ("%d/%m/%Y à %H:%M"_fr) + ".\n\n"
             "Pour que le simulateur sonne comme cette machine : dans RackAttack Editor, Outils > Charger des mesures...\n\n"
             "Ce fichier contient des enregistrements des sons HH et Crash de la machine : garde-le pour toi.\n");
    for (int i = 0; i < 3; ++i)
        if (! r.calibration.samples[i].data.empty())
        {
            auto block = wav (r.calibration.samples[i].data, r.calibration.samples[i].rate);
            zip.addEntry (new juce::MemoryInputStream (block, true), 0, sampleFiles[i], juce::Time::getCurrentTime());
        }
    for (auto& rec : r.recordings)
    {
        auto block = wav (rec.samples, rec.rate);
        zip.addEntry (new juce::MemoryInputStream (block, true), 0, "enregistrements/" + rec.file, juce::Time::getCurrentTime());
    }

    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
        return {};
    out.flush();
    return file;
}

bool load (const juce::File& file, Calibration& c, juce::String& error)
{
    juce::ZipFile zip (file);
    const auto json = juce::JSON::parse (text (zip, "calibration.json"));
    if (! json.isObject())
    {
        error = "Ce fichier n'est pas un fichier de mesures.";
        return false;
    }
    Calibration out = Calibration::defaults();
    bool ok = readTable (json["pitchHz"], out.pitchHz) && readTable (json["cutoffHz"], out.cutoffHz)
           && readTable (json["attackMs"], out.attackMs) && readTable (json["decayMs"], out.decayMs)
           && readTable (json["releaseMs"], out.releaseMs) && readTable (json["lfoHz"], out.lfoHz)
           && readTable (json["crackHz"], out.crackHz) && readTable (json["volumeGain"], out.volumeGain)
           && readTable (json["velocityGain"], out.velocityGain) && readTable (json["sustainLevel"], out.sustainLevel)
           && readTable (json["resonanceK"], out.resonanceK);
    if (! ok)
    {
        error = "Le fichier de mesures est incomplet.";
        return false;
    }
    if (auto* curves = json["fallCurves"].getArray(); curves != nullptr && curves->size() >= 2)
        for (auto& v : *curves)
        {
            std::array<float, 64> curve {};
            if (readTable (v, curve))
                out.fallCurves.push_back (curve);
        }
    if (out.fallCurves.size() < 2)
        out.fallCurves.clear();

    auto number = [&json] (const char* name, float fallback) { return json.hasProperty (name) ? (float) (double) json[name] : fallback; };
    out.pitchModSemitones = number ("pitchModSemitones", out.pitchModSemitones);
    out.detuneSemitones = number ("detuneSemitones", out.detuneSemitones);
    out.cutoffModSemitones = number ("cutoffModSemitones", out.cutoffModSemitones);
    out.fmMaxIndex = number ("fmMaxIndex", out.fmMaxIndex);
    out.selfOscillation = number ("selfOscillation", out.selfOscillation);
    out.tunedRootNote = (int) number ("tunedRootNote", (float) out.tunedRootNote);
    out.env1ToAmp = json.hasProperty ("env1ToAmp") ? (bool) json["env1ToAmp"] : out.env1ToAmp;
    out.velocityToAmp = json.hasProperty ("velocityToAmp") ? (bool) json["velocityToAmp"] : out.velocityToAmp;
    out.source = json["source"].toString().toStdString();

    if (auto* samples = json["samples"].getArray(); samples != nullptr)
        for (int i = 0; i < std::min (3, samples->size()); ++i)
        {
            const auto& s = samples->getReference (i);
            const auto name = s["file"].toString();
            auto& sample = out.samples[i];
            sample.rootPitch = juce::jlimit (0, 127, (int) s["rootPitch"]);
            sample.semitonesPerStep = s.hasProperty ("semitonesPerStep") ? (float) (double) s["semitonesPerStep"] : -1.0f;
            if (name.isNotEmpty() && ! readWav (zip, name, sample.data, sample.rate))
                sample.data.clear();
        }
    c = std::move (out);
    return true;
}
}
