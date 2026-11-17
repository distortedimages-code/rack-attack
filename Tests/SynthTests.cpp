// Tests of the software RackAttack: pitch, envelopes, the filter ringing at 440 Hz at
// cutoff 64 (as the manual says), the crack, XOR groups, parameter changes by SysEx,
// and every effect giving a finite, non-silent output.

#include "../Source/core/RaSysex.h"
#include "../Source/sim/RaSynth.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using namespace ra::synth;

static constexpr double sr = 48000.0;

static std::array<std::uint8_t, soundBytes> plainSound()
{
    std::array<std::uint8_t, soundBytes> s {};
    s.fill (64);
    s[0x01] = 1; s[0x02] = 90; s[0x03] = 0; s[0x04] = 2; s[0x09] = 0;          // Osc 1: sine
    s[0x10] = 1; s[0x11] = 90; s[0x12] = 0; s[0x13] = 2;
    for (int a : { 0x07, 0x08, 0x0C, 0x0D, 0x16, 0x17, 0x20, 0x21, 0x2D, 0x2E, 0x31, 0x32, 0x38, 0x39, 0x3D, 0x3E })
        s[(size_t) a] = 0;                                                     // sources: Env1 / Velocity
    s[0x1A] = 127; s[0x1B] = 0; s[0x1C] = 0; s[0x1D] = 0; s[0x24] = 64; s[0x25] = 3;
    s[0x26] = 0; s[0x27] = 127; s[0x28] = 0; s[0x29] = 0;                      // filter bypass
    s[0x35] = 127;
    s[0x3F] = 0; s[0x40] = 40; s[0x41] = 0; s[0x42] = 0; s[0x43] = 0;
    s[0x45] = 0; s[0x46] = 127; s[0x47] = 127; s[0x48] = 0; s[0x49] = 0;       // Env 2 (the amp's): sustain full
    s[0x4C] = 0; s[0x4D] = 0; s[0x4E] = 0; s[0x50] = 0; s[0x51] = 0; s[0x53] = 0;
    s[0x55] = 1; s[0x56] = 0; s[0x57] = 0; s[0x58] = 0; s[0x5A] = 0; s[0x5B] = 127; s[0x5E] = 0;
    return s;
}

static void note (Synth& syn, int channel, int n, int vel)
{
    std::uint8_t m[3] = { (std::uint8_t) (0x90 | (channel - 1)), (std::uint8_t) n, (std::uint8_t) vel };
    syn.handleMidi (m, 3);
}

static std::vector<float> run (Synth& syn, double seconds)
{
    const int n = (int) (sr * seconds);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; i += 256)
        syn.render (l.data() + i, r.data() + i, std::min (256, n - i));
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
    return l;
}

// frequency from upward zero crossings, skipping the start
static double frequency (const std::vector<float>& x, int skip)
{
    int first = -1, last = -1, count = 0;
    double firstPos = 0, lastPos = 0;
    for (size_t i = (size_t) skip + 1; i < x.size(); ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            const double pos = (double) (i - 1) + (double) (-x[i - 1] / (x[i] - x[i - 1]));
            if (first < 0) { first = (int) i; firstPos = pos; }
            last = (int) i; lastPos = pos; ++count;
        }
    if (count < 2) return 0.0;
    return (count - 1) / ((lastPos - firstPos) / sr);
}

static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

int main()
{
    const auto cal = Calibration::defaults();

    // Pitch: a sine at Pitch 90 plays the calibrated frequency
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        syn.setSound (0, s.data());
        note (syn, 1, 36, 127);
        auto x = run (syn, 0.5);
        const double f = frequency (x, 2000);
        std::printf ("pitch 90: %.2f Hz (calibration %.2f Hz)\n", f, cal.pitchHz[90]);
        CHECK (std::abs (f - cal.pitchHz[90]) / cal.pitchHz[90] < 0.01);
        CHECK (syn.activeVoices() == 1);
    }

    // Envelope: decay to silence with sustain 0, then the voice ends
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x47] = 0;
        s[0x46] = 40;                     // short decay
        syn.setSound (0, s.data());
        note (syn, 1, 36, 127);
        const double decayMs = cal.decayMs[40];
        auto x = run (syn, decayMs / 1000.0 + 0.2);
        const double early = rms (x, 0, (size_t) (sr * 0.005));
        const double late = rms (x, (size_t) (sr * (decayMs / 1000.0 + 0.05)), x.size());
        std::printf ("decay 40 (%.1f ms): start %.3f, after %.6f\n", decayMs, early, late);
        CHECK (early > 0.1 && late < 1e-4);
        CHECK (syn.activeVoices() == 0);
    }

    // Filter: as measured on a unit, it never rings by itself (no input, resonance 120: silence); with a calibration
    // that has a self-oscillation point, past it the low-pass rings at the cutoff
    for (const bool rings : { false, true })
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto c = Calibration::defaults();
        if (rings)
        {
            c.selfOscillation = 114.0f;
            for (int v = 0; v < 128; ++v)
                c.resonanceK[(size_t) v] = 2.0f * (1.0f - (float) v / c.selfOscillation);
        }
        syn.setCalibration (c);
        auto s = plainSound();
        s[0x1A] = 0;                      // no oscillator: the filter alone
        s[0x26] = 1; s[0x27] = 64; s[0x28] = 120;
        syn.setSound (0, s.data());
        note (syn, 1, 36, 127);
        auto x = run (syn, 1.0);
        const double f = frequency (x, 24000), level = rms (x, 24000, x.size());
        std::printf ("resonance 120 %s, cutoff 64 (%.1f Hz): %.1f Hz, level %.4f\n", rings ? "past self-oscillation" : "as measured",
                     c.cutoffHz[64], f, level);
        if (rings)
            CHECK (std::abs (f - c.cutoffHz[64]) < 25.0 && level > 0.01);
        else
            CHECK (level < 1e-4);
    }

    // Crack: Length 3 at 100 Hz gives three dips, then a steady level
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x01] = 2; s[0x02] = 110;       // a fast square, so the level is easy to follow
        s[0x1D] = 127;
        int speed = 0;
        for (int v = 0; v < 128; ++v) if (std::abs (cal.crackHz[(size_t) v] - 100.0f) < std::abs (cal.crackHz[(size_t) speed] - 100.0f)) speed = v;
        s[0x24] = (std::uint8_t) speed; s[0x25] = 3;
        syn.setSound (0, s.data());
        note (syn, 1, 36, 127);
        auto x = run (syn, 0.2);
        const double period = sr / cal.crackHz[(size_t) speed];
        const double during = rms (x, (size_t) (period * 0.6), (size_t) (period * 0.95));   // late in a dip
        const double after = rms (x, (size_t) (period * 4), (size_t) (period * 6));
        std::printf ("crack %.0f Hz: inside a dip %.3f, after %.3f\n", cal.crackHz[(size_t) speed], during, after);
        CHECK (during < after * 0.7);
    }

    // XOR: two sounds of the same group cut each other
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x5E] = 1;
        syn.setSound (0, s.data());
        syn.setSound (1, s.data());
        note (syn, 1, 36, 127);
        run (syn, 0.05);
        note (syn, 1, 37, 127);
        run (syn, 0.05);
        CHECK (syn.activeVoices() == 1);
    }

    // SysEx: a parameter change for sound 1 reaches the voice (a lock)
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        syn.setSound (0, s.data());
        auto msg = ra::soundParameter (5, 0, 0x02, 100);
        syn.handleMidi (msg.data(), (int) msg.size());
        note (syn, 1, 36, 127);
        auto x = run (syn, 0.5);
        const double f = frequency (x, 2000);
        std::printf ("after SysEx pitch 100: %.1f Hz (calibration %.1f)\n", f, cal.pitchHz[100]);
        CHECK (std::abs (f - cal.pitchHz[100]) / cal.pitchHz[100] < 0.01);
    }

    // Tuned: a sound on channel 2 plays the notes, an octave up = twice the frequency
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x58] = 2;
        syn.setSound (5, s.data());
        note (syn, 2, cal.tunedRootNote + 12, 127);
        auto x = run (syn, 0.5);
        const double f = frequency (x, 2000);
        CHECK (std::abs (f - 2.0 * cal.pitchHz[90]) / (2.0 * cal.pitchHz[90]) < 0.01);
    }

    // Every effect: finite and not silent
    {
        for (int type = 1; type <= 8; ++type)
        {
            Synth syn;
            syn.prepare (sr);
            syn.setGlobalChannel (1);
            auto s = plainSound();
            s[0x47] = 0; s[0x46] = 50;
            s[0x55] = 0; s[0x57] = 127; s[0x56] = 0;
            syn.setSound (0, s.data());
            std::array<std::uint8_t, fxBytes> fx {};
            fx.fill (64);
            fx[0] = (std::uint8_t) type; fx[1] = 1; fx[11] = 0;
            syn.setEffect (0, fx.data());
            note (syn, 1, 36, 127);
            auto x = run (syn, 1.0);
            bool finite = true;
            for (float v : x) finite = finite && std::isfinite (v);
            const double tail = rms (x, (size_t) (sr * 0.5), x.size());
            std::printf ("effect %d: tail %.5f\n", type, tail);
            CHECK (finite);
            CHECK (rms (x, 0, x.size()) > 0.001);
        }
    }

    // A free-running LFO (Sync Off) runs on its own: at Speed 0 (one cycle in about 200 s) the pitch it
    // modulates stays almost the same from one note to the next, not a random value per note
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x05] = 72; s[0x07] = 2;                       // Osc 1 pitch from LFO 1 (a few semitones)
        s[0x4B] = 0; s[0x4C] = 0; s[0x4D] = 0;           // LFO 1: Speed 0, Sine, Sync Off
        syn.setSound (0, s.data());
        syn.setClock (37.0);
        std::vector<double> f;
        for (int k = 0; k < 6; ++k)
        {
            note (syn, 1, 36, 127);
            auto x = run (syn, 0.25);
            f.push_back (frequency (x, (int) (sr * 0.05)));
        }
        // a slow, smooth drift: every step the same way and about the same size, none bigger than 0.5 %
        bool smooth = true;
        const double first = f[1] - f[0];
        for (size_t k = 1; k < f.size(); ++k)
        {
            const double step = f[k] - f[k - 1];
            smooth = smooth && step * first > 0.0 && std::abs (step) < f[k] * 0.005
                     && std::abs (step - first) < std::abs (first) * 0.5;
        }
        std::printf ("free LFO at speed 0: %.2f, %.2f ... %.2f Hz over six notes\n", f[0], f[1], f.back());
        CHECK (smooth);
        CHECK (std::abs (Synth::freeLfoPhase (0.5, 3.0) - 0.5) < 1e-9);
    }

    // FX Select changed by a parameter message: the effect chosen is heard at once (no need to touch Dry Out)
    {
        Synth syn;
        syn.prepare (sr);
        syn.setGlobalChannel (1);
        auto s = plainSound();
        s[0x47] = 0; s[0x46] = 10;                          // a click
        s[0x55] = 1; s[0x56] = 0; s[0x57] = 64;
        syn.setSound (0, s.data());
        std::array<std::uint8_t, fxBytes> none {}, delay {};
        delay.fill (64);
        delay[0] = 5; delay[1] = 1; delay[5] = 60; delay[6] = 70; delay[12] = 0;
        syn.setEffect (0, none.data());
        syn.setEffect (1, delay.data());
        note (syn, 1, 36, 127);
        auto before = run (syn, 1.0);
        auto sx = ra::soundParameter (0, 0, 0x56, 1);
        syn.handleMidi (sx.data(), (int) sx.size());
        note (syn, 1, 36, 127);
        auto after = run (syn, 1.0);
        const double tailBefore = rms (before, (size_t) (sr * 0.2), before.size());
        const double tailAfter = rms (after, (size_t) (sr * 0.2), after.size());
        // the same sound with FX 2 from the start
        Synth ref;
        ref.prepare (sr);
        ref.setGlobalChannel (1);
        s[0x56] = 1;
        ref.setSound (0, s.data());
        ref.setEffect (0, none.data());
        ref.setEffect (1, delay.data());
        note (ref, 1, 36, 127);
        auto direct = run (ref, 1.0);
        const double tailDirect = rms (direct, (size_t) (sr * 0.2), direct.size());
        std::printf ("fx select by message: echoes %.5f on FX1 (none), %.5f on FX2 (delay), %.5f set from the start\n",
                     tailBefore, tailAfter, tailDirect);
        CHECK (tailBefore < 1e-5);
        CHECK (tailAfter > 1e-4 && std::abs (tailAfter - tailDirect) < tailDirect * 0.2);
    }

    // FX Send Mix: a balance. At 0 dry only, in the middle dry and effect, at the top the effect only
    // (a long predelay keeps the effect out of the first 300 ms, so what is heard there is the dry)
    {
        auto early = [] (int send)
        {
            Synth syn;
            syn.prepare (sr);
            syn.setGlobalChannel (1);
            auto s = plainSound();
            s[0x55] = 0; s[0x57] = (std::uint8_t) send; s[0x56] = 0;
            syn.setSound (0, s.data());
            std::array<std::uint8_t, fxBytes> fx {};
            fx.fill (64);
            fx[0] = 7; fx[1] = 1; fx[4] = 64; fx[5] = 127;
            syn.setEffect (0, fx.data());
            note (syn, 1, 36, 127);
            auto x = run (syn, 0.3);
            return rms (x, (size_t) (sr * 0.01), (size_t) (sr * 0.08));
        };
        const double none = early (0), half = early (64), most = early (96), all = early (127);
        std::printf ("fx send: dry heard %.4f at 0, %.4f at 64, %.4f at 96, %.4f at 127\n", none, half, most, all);
        CHECK (std::abs (half - none) < none * 0.05);
        CHECK (most < none * 0.6 && most > none * 0.3);
        CHECK (all < none * 0.02);
    }

    // Reverb: Predelay (4th setting, byte 05) holds it back; Decay (3rd, byte 04) doesn't
    {
        auto wet = [] (int decay, int predelay)
        {
            Synth syn;
            syn.prepare (sr);
            syn.setGlobalChannel (1);
            auto s = plainSound();
            s[0x47] = 0; s[0x46] = 10;                       // a click
            s[0x55] = 0; s[0x57] = 127; s[0x56] = 0;
            syn.setSound (0, s.data());
            std::array<std::uint8_t, fxBytes> fx {};
            fx.fill (64);
            fx[0] = 7; fx[1] = 1; fx[4] = (std::uint8_t) decay; fx[5] = (std::uint8_t) predelay;
            syn.setEffect (0, fx.data());
            note (syn, 1, 36, 127);
            auto x = run (syn, 0.6);
            return std::make_pair (rms (x, (size_t) (sr * 0.06), (size_t) (sr * 0.25)), rms (x, (size_t) (sr * 0.33), (size_t) (sr * 0.55)));
        };
        const auto now = wet (127, 0), held = wet (127, 127), shortTail = wet (0, 0);
        std::printf ("reverb: at once %.5f, predelay %.5f then %.5f, short decay %.5f\n", now.first, held.first, held.second, shortTail.first);
        CHECK (held.first < now.first * 0.3);
        CHECK (held.second > held.first * 3.0);
        CHECK (shortTail.first > now.first * 0.3);
    }

    // The noise and the imitated metals make sound
    {
        for (int shape : { 5, 6, 7, 8 })
        {
            Synth syn;
            syn.prepare (sr);
            syn.setGlobalChannel (1);
            auto s = plainSound();
            s[0x01] = (std::uint8_t) shape; s[0x02] = 64;
            syn.setSound (0, s.data());
            note (syn, 1, 36, 127);
            auto x = run (syn, 0.1);
            CHECK (rms (x, 0, x.size()) > 0.01);
        }
    }

    if (failures == 0) std::printf ("SynthTests: all passed\n");
    else std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
