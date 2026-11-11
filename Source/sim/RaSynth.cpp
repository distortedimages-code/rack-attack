#include "RaSynth.h"

#include "../core/RaSysex.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ra::synth
{
namespace
{
    constexpr float pi = 3.14159265358979f;
    constexpr float twoPi = 2.0f * pi;

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
        xorGroup = 0x5E
    };

    inline float uni (std::uint8_t v)   { return (float) v / 127.0f; }
    inline float bip (std::uint8_t v)   { return ((float) v - 64.0f) / 64.0f; }
    inline float fromSemis (float s)    { return std::exp2 (s / 12.0f); }
    inline int juce_clampVel (float v)  { return std::clamp ((int) std::lround (v * 127.0f), 0, 127); }

    struct Rng
    {
        std::uint32_t s = 0x9E3779B9u;
        float next()                                     // -1..1
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (s & 0xFFFFFF) / 8388608.0f - 1.0f;
        }
    };

    inline float polyBlep (double t, double dt)
    {
        if (dt <= 0.0)
            return 0.0f;
        if (t < dt)       { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    // Decay and release, from 1 down to 0 as u goes 0 -> 1. Shape: exponential,
    // then linear, then inverse exponential, then a cosine-like mix (manual).
    float fall (float u, float shape)
    {
        u = std::clamp (u, 0.0f, 1.0f);
        static const float e5 = std::exp (-5.0f), E5 = std::exp (5.0f);
        const float fexp = (std::exp (-5.0f * u) - e5) / (1.0f - e5);
        const float flin = 1.0f - u;
        const float finv = 1.0f - (std::exp (5.0f * u) - 1.0f) / (E5 - 1.0f);
        const float fcos = 0.5f + 0.5f * std::cos (pi * u);
        if (shape < 1.0f / 3.0f)  { const float t = shape * 3.0f;          return fexp + (flin - fexp) * t; }
        if (shape < 2.0f / 3.0f)  { const float t = (shape - 1.0f / 3.0f) * 3.0f; return flin + (finv - flin) * t; }
        const float t = (shape - 2.0f / 3.0f) * 3.0f;
        return finv + (fcos - finv) * t;
    }

    // LFO sync values (Off, Key, 8 bars ... 1/64t): one cycle in quarter notes
    double syncBeats (int index)
    {
        static const double plain[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
        if (index >= 2 && index <= 11) return plain[index - 2];
        if (index >= 12 && index <= 17) return plain[index - 12 + 4] * 1.5;
        if (index >= 18 && index <= 23) return plain[index - 18 + 4] * 2.0 / 3.0;
        return 1.0;
    }

    float softClip (float x)   { return std::tanh (x); }

    //==========================================================================
    // Effects

    struct DelayLine
    {
        std::vector<float> buf;
        int pos = 0;
        void resize (int n)                { buf.assign ((size_t) std::max (4, n), 0.0f); pos = 0; }
        void push (float x)                { buf[(size_t) pos] = x; pos = (pos + 1) % (int) buf.size(); }
        float read (float delaySamples) const
        {
            const int n = (int) buf.size();
            delaySamples = std::clamp (delaySamples, 1.0f, (float) n - 2.0f);
            float rp = (float) pos - delaySamples;
            while (rp < 0.0f) rp += (float) n;
            const int i0 = (int) rp;
            const float f = rp - (float) i0;
            const float a = buf[(size_t) i0], b = buf[(size_t) ((i0 + 1) % n)];
            return a + (b - a) * f;
        }
        void clear()                       { std::fill (buf.begin(), buf.end(), 0.0f); }
    };

    struct OnePole
    {
        float z = 0.0f, a = 1.0f;
        void setLowpass (float hz, float sr)   { a = 1.0f - std::exp (-twoPi * std::clamp (hz, 1.0f, sr * 0.49f) / sr); }
        float lp (float x)                     { z += a * (x - z); return z; }
        float hp (float x)                     { return x - lp (x); }
    };

    struct Comb
    {
        std::vector<float> buf; int pos = 0; float store = 0.0f;
        void resize (int n)   { buf.assign ((size_t) std::max (8, n), 0.0f); pos = 0; store = 0.0f; }
        float process (float x, float feedback, float damp)
        {
            float out = buf[(size_t) pos];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t) pos] = x + store * feedback;
            pos = (pos + 1) % (int) buf.size();
            return out;
        }
    };

    struct Allpass
    {
        std::vector<float> buf; int pos = 0;
        void resize (int n)   { buf.assign ((size_t) std::max (8, n), 0.0f); pos = 0; }
        float process (float x, float g)
        {
            float b = buf[(size_t) pos];
            float out = -x * g + b;
            buf[(size_t) pos] = x + b * g;
            pos = (pos + 1) % (int) buf.size();
            return out;
        }
    };

    struct Effect
    {
        std::array<std::uint8_t, fxBytes> p {};
        float sr = 44100.0f;
        double tempo = 120.0;

        DelayLine dl, dr;
        OnePole lp1, lp2, hp1, hp2;
        double lfo = 0.0;
        float fbL = 0.0f, fbR = 0.0f;
        float apState[2][8] {};
        float hold = 0.0f, holdR = 0.0f, holdCount = 0.0f;
        Comb combs[2][8];
        Allpass alls[2][4];
        DelayLine pre;
        bool pingLeft = true;

        int type() const              { return std::min ((int) p[0], 8); }
        float par (int i) const       { return uni (p[(size_t) (1 + i)]); }   // Param 1..11 at 02..0C
        std::uint8_t raw (int i) const { return p[(size_t) (1 + i)]; }

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            dl.resize ((int) (sr * 1.6f));
            dr.resize ((int) (sr * 1.6f));
            pre.resize ((int) (sr * 0.32f));
            static const int combLen[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int apLen[4] = { 556, 441, 341, 225 };
            const float scale = sr / 44100.0f * 1.6f;   // room up to "20 m"
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < 8; ++i) combs[c][i].resize ((int) ((float) (combLen[i] + c * 23) * scale));
                for (int i = 0; i < 4; ++i) alls[c][i].resize ((int) ((float) (apLen[i] + c * 23) * scale));
            }
            reset();
        }

        void reset()
        {
            dl.clear(); dr.clear(); pre.clear();
            fbL = fbR = 0.0f;
            std::memset (apState, 0, sizeof (apState));
            for (auto& c : combs) for (auto& x : c) std::fill (x.buf.begin(), x.buf.end(), 0.0f);
            for (auto& c : alls) for (auto& x : c) std::fill (x.buf.begin(), x.buf.end(), 0.0f);
        }

        float delayLengthMs (std::uint8_t v) const   { return 1.4f * std::pow (1480.0f / 1.4f, (float) v / 127.0f); }

        void process (const float* in, float* outL, float* outR, int n)
        {
            const int t = type();
            switch (t)
            {
                case 1: chorus (in, outL, outR, n, false); break;
                case 2: chorus (in, outL, outR, n, true); break;
                case 3: phaser (in, outL, outR, n); break;
                case 4: overdrive (in, outL, outR, n); break;
                case 5: delay (in, outL, outR, n); break;
                case 6: modDelay (in, outL, outR, n); break;
                case 7: reverb (in, outL, outR, n); break;
                case 8: lofi (in, outL, outR, n); break;
                default: for (int i = 0; i < n; ++i) { outL[i] += in[i]; outR[i] += in[i]; } break;
            }
        }

        void chorus (const float* in, float* L, float* R, int n, bool flanger)
        {
            const float rate = 0.05f * std::pow (200.0f, par (0));          // ~0.05..10 Hz
            const float depth = par (1);
            const float base = flanger ? 0.5f + 2.0f * depth : 5.0f + 25.0f * par (3);   // ms
            const float swing = flanger ? 0.4f + 4.0f * depth : 1.0f + 6.0f * depth;
            const float fb = flanger ? par (4) * 0.92f * (raw (8) != 0 ? -1.0f : 1.0f) : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                lfo += rate / sr;
                if (lfo >= 1.0) lfo -= 1.0;
                const float s = std::sin (twoPi * (float) lfo), c = std::cos (twoPi * (float) lfo);
                const float a = dl.read ((base + swing * (0.5f + 0.5f * s)) * sr / 1000.0f);
                const float b = dr.read ((base + swing * (0.5f + 0.5f * c)) * sr / 1000.0f);
                dl.push (in[i] + a * fb);
                dr.push (in[i] + b * fb);
                L[i] += flanger ? (in[i] + a) * 0.5f : a;
                R[i] += flanger ? (in[i] + b) * 0.5f : b;
            }
        }

        void phaser (const float* in, float* L, float* R, int n)
        {
            const float rate = 0.05f * std::pow (200.0f, par (0));
            const float depth = par (1) * 3.0f;                              // octaves
            const float fb = par (4) * 0.9f * (raw (8) != 0 ? -1.0f : 1.0f);
            const float center = 100.0f * std::pow (50.0f, par (5));
            const float spacing = 1.0f + par (6) * 1.5f;
            for (int i = 0; i < n; ++i)
            {
                lfo += rate / sr;
                if (lfo >= 1.0) lfo -= 1.0;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float ph = (float) lfo + (ch == 1 ? 0.25f : 0.0f);
                    const float f0 = center * std::exp2 (depth * std::sin (twoPi * ph));
                    float x = in[i] + (ch == 0 ? fbL : fbR) * fb;
                    float f = f0;
                    for (int s = 0; s < 6; ++s)
                    {
                        const float tn = std::tan (pi * std::min (f, sr * 0.45f) / sr);
                        const float a = (tn - 1.0f) / (tn + 1.0f);
                        const float y = a * x + apState[ch][s];
                        apState[ch][s] = x - a * y;
                        x = y;
                        f *= spacing;
                    }
                    if (ch == 0) { fbL = x; L[i] += (in[i] + x) * 0.5f; }
                    else         { fbR = x; R[i] += (in[i] + x) * 0.5f; }
                }
            }
        }

        void overdrive (const float* in, float* L, float* R, int n)
        {
            const float drive = 1.0f + par (1) * 30.0f;
            const float post = par (2) * 1.5f;
            lp1.setLowpass (200.0f * std::pow (100.0f, par (5)), sr);
            for (int i = 0; i < n; ++i)
            {
                const float y = lp1.lp (softClip (in[i] * drive)) * post;
                L[i] += y;
                R[i] += y;
            }
        }

        void delay (const float* in, float* L, float* R, int n)
        {
            float ms = delayLengthMs (raw (3));
            const int sync = raw (10);
            if (sync > 0)
                ms = (float) (syncBeats (std::min (23, sync + 1)) * 60000.0 / std::max (20.0, tempo));
            const float d = std::min (ms, 1480.0f) * sr / 1000.0f;
            const float fb = par (4) * 0.95f * (raw (8) != 0 ? -1.0f : 1.0f);
            const bool autoPan = raw (9) != 0;
            lp1.setLowpass (300.0f * std::pow (60.0f, par (5)), sr);
            lp2.setLowpass (300.0f * std::pow (60.0f, par (5)), sr);
            for (int i = 0; i < n; ++i)
            {
                const float a = dl.read (d), b = dr.read (d);
                if (autoPan)
                {
                    // ping-pong: the left feeds the right and back
                    dl.push (in[i] + lp1.lp (b) * fb);
                    dr.push (lp2.lp (a) * fb);
                }
                else
                {
                    dl.push (in[i] + lp1.lp (a) * fb);
                    dr.push (in[i] + lp2.lp (b) * fb);
                }
                L[i] += a;
                R[i] += b;
            }
        }

        void modDelay (const float* in, float* L, float* R, int n)
        {
            const float rate = 0.009f * std::pow (10.0f / 0.009f, par (0));
            const float depth = par (1);
            float ms = delayLengthMs (raw (3));
            if (raw (2) > 0)
                ms = (float) (syncBeats (std::min (23, (int) raw (2) + 1)) * 60000.0 / std::max (20.0, tempo));
            const float fb = bip (raw (4)) * 0.95f;
            const float spread = bip (raw (7));
            hp1.setLowpass (20.0f * std::pow (50.0f, par (5)), sr);
            hp2.setLowpass (20.0f * std::pow (50.0f, par (5)), sr);
            lp1.setLowpass (500.0f * std::pow (40.0f, par (6)), sr);
            lp2.setLowpass (500.0f * std::pow (40.0f, par (6)), sr);
            for (int i = 0; i < n; ++i)
            {
                lfo += rate / sr;
                if (lfo >= 1.0) lfo -= 1.0;
                const float m = std::sin (twoPi * (float) lfo) * depth * 0.2f;
                const float a = dl.read (std::min (ms * (1.0f + m), 1480.0f) * sr / 1000.0f);
                const float b = dr.read (std::min (ms * (1.0f - m) * (1.0f + spread * 0.1f), 1480.0f) * sr / 1000.0f);
                dl.push (in[i] + lp1.lp (hp1.hp (a)) * fb);
                dr.push (in[i] + lp2.lp (hp2.hp (b)) * fb);
                L[i] += a;
                R[i] += b;
            }
        }

        void reverb (const float* in, float* L, float* R, int n)
        {
            const float size = 0.3f + 0.7f * par (0);
            const float decay = 0.7f + 0.28f * par (2);
            const float predelay = par (3) * 300.0f * sr / 1000.0f;
            const float damp = 0.05f + 0.6f * par (9);
            const float diffusion = 0.3f + 0.45f * par (8);
            hp1.setLowpass (20.0f * std::pow (25.0f, par (5)), sr);
            hp2.setLowpass (20.0f * std::pow (25.0f, par (5)), sr);
            lp1.setLowpass (1000.0f * std::pow (18.0f, par (6)), sr);
            lp2.setLowpass (1000.0f * std::pow (18.0f, par (6)), sr);
            const float fb = decay * (0.8f + 0.2f * size);
            for (int i = 0; i < n; ++i)
            {
                pre.push (in[i] * 0.3f);
                const float x = pre.read (std::max (1.0f, predelay));
                float out[2] = { 0.0f, 0.0f };
                for (int c = 0; c < 2; ++c)
                {
                    float s = 0.0f;
                    const int used = 4 + (int) (size * 4.0f);
                    for (int k = 0; k < used && k < 8; ++k)
                        s += combs[c][k].process (x, fb, damp);
                    for (auto& a : alls[c])
                        s = a.process (s, diffusion);
                    out[c] = s;
                }
                L[i] += lp1.lp (hp1.hp (out[0]));
                R[i] += lp2.lp (hp2.hp (out[1]));
            }
        }

        void lofi (const float* in, float* L, float* R, int n)
        {
            const float rate = 44100.0f * std::pow (2.6f / 44100.0f, par (4));
            const float drive = 1.0f + par (5) * 20.0f;
            const float gain = par (6) * 1.5f;
            for (int i = 0; i < n; ++i)
            {
                holdCount += rate / sr;
                if (holdCount >= 1.0f)
                {
                    holdCount -= std::floor (holdCount);
                    hold = softClip (in[i] * drive);
                }
                L[i] += hold * gain;
                R[i] += hold * gain;
            }
        }
    };
}

//==============================================================================
Calibration Calibration::defaults()
{
    Calibration c;
    for (int v = 0; v < 128; ++v)
    {
        const float t = (float) v / 127.0f;
        c.pitchHz[(size_t) v] = 0.0079f * std::pow (18700.0f / 0.0079f, t);   // manual: 0.0079 Hz .. 18.7 kHz
        c.cutoffHz[(size_t) v] = 440.0f * std::exp2 (((float) v - 64.0f) / 12.0f);   // manual: 64 rings at 440 Hz
        c.attackMs[(size_t) v] = v == 0 ? 0.0f : 0.5f * std::exp2 (t * 14.0f);
        c.decayMs[(size_t) v] = 1.0f * std::exp2 (t * 14.0f);
        c.releaseMs[(size_t) v] = 1.0f * std::exp2 (t * 14.0f);
        c.lfoHz[(size_t) v] = 0.02f * std::exp2 (t * 11.0f);
        c.crackHz[(size_t) v] = std::pow (5000.0f, t);                             // manual: 1 Hz .. 5 kHz
        c.volumeGain[(size_t) v] = t * t;
        c.velocityGain[(size_t) v] = t * t;
        c.sustainLevel[(size_t) v] = t;
        c.resonanceK[(size_t) v] = 2.0f * (1.0f - (float) v / c.selfOscillation);
    }
    return c;
}

//==============================================================================
struct Synth::Impl
{
    struct Env
    {
        enum class Stage { idle, attack, decay, sustain, release } stage = Stage::idle;
        float level = 0.0f, from = 0.0f;
        double pos = 0.0, len = 1.0;
    };

    struct Lfo
    {
        double phase = 0.0;
        float value = 0.0f, held = 0.0f, prev = 0.0f, next = 0.0f;
        bool done = false;
    };

    struct Osc
    {
        double phase = 0.0;
        float held = 0.0f, lp = 0.0f, hpz = 0.0f, metal[6] {};
        double samplePos = 0.0;
        float out = 0.0f;
    };

    struct Voice
    {
        bool active = false, held = false, tuned = false;
        int sound = 0, note = 0, channel = 1;
        float velocity = 1.0f, keyOffset = 0.0f;
        std::uint64_t age = 0;
        float fade = 1.0f, fadeStep = 0.0f;
        Osc o1, o2;
        Env e1, e2;
        Lfo l1, l2;
        double crackPhase = 0.0;
        int crackCycles = 0;
        float ic1 = 0.0f, ic2 = 0.0f, shHold = 0.0f, shCount = 0.0f;
        float metalEnv = 1.0f;
        // per control block
        float f1 = 0.0f, f2 = 0.0f, fmIndex = 0.0f, o2Level = 0.0f, g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float level = 0.0f, panL = 0.7071f, panR = 0.7071f, shelfGain = 1.0f;
        int counter = 0;
    };

    double sr = 44100.0, tempo = 120.0;
    Calibration cal = Calibration::defaults();
    std::array<std::array<std::uint8_t, soundBytes>, numSounds> snd {};
    std::array<Effect, numFx> fx;
    std::array<Voice, maxVoices> voices;
    std::array<float, numSounds> levels {};
    int globalChannel = 0, baseNote = 36;
    std::uint64_t clock = 0;
    Rng rng;
    std::array<std::array<float, 128>, 16> cc {};      // controllers per channel
    std::array<float, 16> pressure {};
    std::vector<float> fxIn[numFx], tmpL, tmpR;

    Impl()
    {
        for (auto& s : snd)
        {
            s.fill (64);
            s[o1Shape] = 1; s[o1Pitch] = 80; s[o2Shape] = 0; s[o2Pitch] = 80; s[o1Fm] = 0; s[o1Start] = 2; s[o2Start] = 2;
            s[mixO1] = 127; s[mixO2] = 0; s[mixRing] = 0; s[mixCrack] = 0; s[crackSpeed] = 64; s[crackLength] = 3;
            s[fType] = 0; s[fCutoff] = 127; s[fReso] = 0; s[fDrive] = 0;
            s[aVolume] = 100;
            s[env1] = 0; s[env1 + 1] = 70; s[env1 + 2] = 0; s[env1 + 3] = 0; s[env1 + 4] = 0;
            s[env2] = 0; s[env2 + 1] = 60; s[env2 + 2] = 0; s[env2 + 3] = 0; s[env2 + 4] = 0;
            s[l1Sync] = 0; s[l1OneShot] = 0; s[l2Sync] = 0; s[l2ModSrc] = 0;
            s[dryOut] = 1; s[fxSelect] = 0; s[fxSend] = 0; s[tunedChannel] = 0; s[lowKey] = 0; s[highKey] = 127; s[xorGroup] = 0;
            s[o1PitchModSrc] = 0; s[o1PitchCtrlSrc] = 0; s[o1FmModSrc] = 0; s[o1FmCtrlSrc] = 0;
            s[o2PitchModSrc] = 0; s[o2PitchCtrlSrc] = 0; s[mixO2ModSrc] = 0; s[mixO2CtrlSrc] = 0;
            s[fMod1Src] = 0; s[fCtrl1Src] = 0; s[fMod2Src] = 0; s[fCtrl2Src] = 0;
            s[aVolModSrc] = 0; s[aVolCtrlSrc] = 0; s[aPanModSrc] = 0; s[aPanCtrlSrc] = 0;
            s[l1Shape] = 0; s[l2Shape] = 0;
        }
        for (auto& c : cc) c.fill (0.0f);
        for (auto& f : fx) f.p.fill (0);
    }

    //==========================================================================
    float ctrlValue (const Voice& v, std::uint8_t src) const
    {
        const int ch = std::clamp (v.channel - 1, 0, 15);
        switch (src)
        {
            case 0:  return v.velocity;
            case 1:  return cc[(size_t) ch][1];
            case 2:  return pressure[(size_t) ch];
            case 3:  return cc[(size_t) ch][2];
            case 4:  return cc[(size_t) ch][4];
            case 5:  return ((float) v.note - 64.0f) / 64.0f;
            default: return 0.0f;
        }
    }

    float modValue (const Voice& v, std::uint8_t src) const
    {
        switch (src)
        {
            case 0:  return v.e1.level;
            case 1:  return v.e2.level;
            case 2:  return v.l1.value;
            case 3:  return v.l2.value;
            default: return 0.0f;
        }
    }

    float msToSamples (float ms) const   { return ms * (float) sr / 1000.0f; }

    void envStart (Env& e, const std::uint8_t* p)
    {
        e.from = e.level;
        e.pos = 0.0;
        const float a = cal.attackMs[p[0]];
        if (a <= 0.0f)
        {
            e.level = 1.0f;
            e.stage = Env::Stage::decay;
            e.len = std::max (1.0f, msToSamples (cal.decayMs[p[1]]));
        }
        else
        {
            e.stage = Env::Stage::attack;
            e.len = std::max (1.0f, msToSamples (a));
        }
    }

    void envRelease (Env& e, const std::uint8_t* p)
    {
        if (e.stage == Env::Stage::idle || e.stage == Env::Stage::release)
            return;
        if (p[3] == 0)
            return;                         // "Release fully left: ignored"
        e.from = e.level;
        e.pos = 0.0;
        e.len = std::max (1.0f, msToSamples (cal.releaseMs[p[3]]));
        e.stage = Env::Stage::release;
    }

    float fallAt (float u, float shape) const
    {
        const int n = (int) cal.fallCurves.size();
        if (n < 2)
            return fall (u, shape);
        u = std::clamp (u, 0.0f, 1.0f);
        const float pos = std::clamp (shape, 0.0f, 1.0f) * (float) (n - 1);
        const int i = std::min (n - 2, (int) pos);
        const float t = pos - (float) i;
        auto at = [u] (const std::array<float, 64>& c)
        {
            const float x = u * 63.0f;
            const int k = std::min (62, (int) x);
            return c[(size_t) k] + (c[(size_t) k + 1] - c[(size_t) k]) * (x - (float) k);
        };
        return at (cal.fallCurves[(size_t) i]) * (1.0f - t) + at (cal.fallCurves[(size_t) i + 1]) * t;
    }

    void envStep (Env& e, const std::uint8_t* p)
    {
        const float sustain = cal.sustainLevel[p[2]];
        const float shape = uni (p[4]);
        switch (e.stage)
        {
            case Env::Stage::idle: e.level = 0.0f; return;
            case Env::Stage::attack:
                e.level = e.from + (1.0f - e.from) * (float) (e.pos / e.len);
                if (++e.pos >= e.len)
                {
                    e.level = 1.0f;
                    e.stage = Env::Stage::decay;
                    e.pos = 0.0;
                    e.len = std::max (1.0f, msToSamples (cal.decayMs[p[1]]));
                }
                return;
            case Env::Stage::decay:
                e.level = sustain + (1.0f - sustain) * fallAt ((float) (e.pos / e.len), shape);
                if (++e.pos >= e.len)
                {
                    e.level = sustain;
                    e.stage = sustain <= 0.0f ? Env::Stage::idle : Env::Stage::sustain;
                }
                return;
            case Env::Stage::sustain: e.level = sustain; return;
            case Env::Stage::release:
                e.level = e.from * fallAt ((float) (e.pos / e.len), shape);
                if (++e.pos >= e.len)
                {
                    e.level = 0.0f;
                    e.stage = Env::Stage::idle;
                }
                return;
        }
    }

    float lfoWave (int shape, Lfo& l, double phase)
    {
        const float p = (float) phase;
        switch (shape)
        {
            case 0: return std::cos (twoPi * p);
            case 1: return 4.0f * std::abs (p - 0.5f) - 1.0f;
            case 2: return p < 0.5f ? 1.0f : -1.0f;
            case 3: return 1.0f - 2.0f * p;
            case 4: { const float t = p * p * (3.0f - 2.0f * p); return l.prev + (l.next - l.prev) * t; }
            default: return l.held;
        }
    }

    void lfoStep (Voice& v, Lfo& l, bool first)
    {
        const auto& s = snd[(size_t) v.sound];
        const int speed = first ? s[l1Speed] : s[l2Speed];
        const int shape = std::min (5, (int) (first ? s[l1Shape] : s[l2Shape]));
        const int sync = first ? s[l1Sync] : s[l2Sync];
        double hz = cal.lfoHz[(size_t) speed];
        if (sync >= 2)
            hz = tempo / 60.0 / syncBeats (sync);
        if (! first)
            hz *= std::exp2 (bip (s[l2Mod]) * ctrlValue (v, s[l2ModSrc]) * 4.0f);
        const bool oneShot = first && s[l1OneShot] != 0;
        if (! l.done)
        {
            l.phase += hz / sr;
            if (l.phase >= 1.0)
            {
                if (oneShot) { l.phase = 1.0; l.done = true; }
                else
                {
                    l.phase -= std::floor (l.phase);
                    l.prev = l.next;
                    l.next = rng.next();
                    l.held = rng.next();
                }
            }
        }
        l.value = lfoWave (shape, l, std::min (l.phase, 0.99999));
    }

    void lfoStart (Voice& v, Lfo& l, bool first)
    {
        const auto& s = snd[(size_t) v.sound];
        const int sync = first ? s[l1Sync] : s[l2Sync];
        l.done = false;
        l.prev = rng.next(); l.next = rng.next(); l.held = rng.next();
        // Key: start at the top of the wave; otherwise free running (a random phase)
        l.phase = sync == 1 || (first && s[l1OneShot] != 0) ? 0.0 : (double) (rng.next() * 0.5f + 0.5f);
    }

    static double startPhase (int shape, int start, double previous)
    {
        if (start == 3)
            return previous;                // Free
        // Peak+ / Peak- / Zero for each wave as drawn here
        if (shape == 3)                     // saw: falls from +1 to -1
            return start == 0 ? 0.0 : start == 1 ? 0.999 : 0.5;
        return start == 0 ? 0.0 : start == 1 ? 0.5 : 0.25;
    }

    //==========================================================================
    void noteOn (int channel, int note, int velocity)
    {
        const float vel = (float) velocity / 127.0f;
        if ((globalChannel == 0 || channel == globalChannel) && note >= baseNote && note < baseNote + numSounds)
            startVoice (note - baseNote, channel, note, vel, false);

        for (int s = 0; s < numSounds; ++s)
        {
            const int tc = snd[(size_t) s][tunedChannel];
            if (tc == 0)
                continue;
            const int want = tc == 17 ? globalChannel : tc;
            if (want != 0 && want != channel)
                continue;
            const int lo = std::min (snd[(size_t) s][lowKey], snd[(size_t) s][highKey]);
            const int hi = std::max (snd[(size_t) s][lowKey], snd[(size_t) s][highKey]);
            if (note < lo || note > hi)
                continue;
            startVoice (s, channel, note, vel, true);
        }
    }

    void noteOff (int channel, int note)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note && v.channel == channel)
            {
                v.held = false;
                const auto* p = snd[(size_t) v.sound].data();
                envRelease (v.e1, p + env1);
                envRelease (v.e2, p + env2);
            }
    }

    void choke (Voice& v)
    {
        if (v.active && v.fadeStep <= 0.0f)
            v.fadeStep = 1.0f / (0.004f * (float) sr);
    }

    void startVoice (int sound, int channel, int note, float vel, bool tuned)
    {
        const auto& s = snd[(size_t) sound];
        const int group = s[xorGroup];
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            if (group != 0 && snd[(size_t) v.sound][xorGroup] == group)
                choke (v);                                  // XOR: the group cuts itself
            else if (! tuned && ! v.tuned && v.sound == sound)
                choke (v);                                  // a drum sound plays once at a time
        }

        // a free voice, or the oldest
        Voice* pick = nullptr;
        for (auto& v : voices)
            if (! v.active) { pick = &v; break; }
        if (pick == nullptr)
        {
            pick = &voices[0];
            for (auto& v : voices)
                if (v.age < pick->age) pick = &v;
        }
        auto& v = *pick;
        const double p1 = v.o1.phase, p2 = v.o2.phase;
        v = Voice();
        v.active = true;
        v.held = true;
        v.tuned = tuned;
        v.sound = sound;
        v.channel = channel;
        v.note = note;
        v.velocity = vel;
        v.age = ++clock;
        v.keyOffset = tuned ? (float) (note - cal.tunedRootNote) + ((float) s[transpose] - 64.0f) : 0.0f;
        v.o1.phase = startPhase (s[o1Shape], s[o1Start], p1);
        v.o2.phase = startPhase (s[o2Shape], s[o2Start], p2);
        envStart (v.e1, s.data() + env1);
        envStart (v.e2, s.data() + env2);
        lfoStart (v, v.l1, true);
        lfoStart (v, v.l2, false);
        v.counter = 0;
    }

    //==========================================================================
    // Control rate: pitches, filter coefficients, levels
    void control (Voice& v)
    {
        const auto& s = snd[(size_t) v.sound];

        auto pitch = [&] (int pitchAddr, int detuneAddr, int modAddr, int ctrlAddr, int modSrcAddr, int ctrlSrcAddr)
        {
            float semis = v.keyOffset;
            semis += bip (s[(size_t) modAddr]) * modValue (v, s[(size_t) modSrcAddr]) * cal.pitchModSemitones;
            semis += bip (s[(size_t) ctrlAddr]) * ctrlValue (v, s[(size_t) ctrlSrcAddr]) * cal.pitchModSemitones;
            semis += uni (s[(size_t) detuneAddr]) * cal.detuneSemitones;   // detune: up to 200 cents
            return cal.pitchHz[s[(size_t) pitchAddr]] * fromSemis (semis);
        };
        v.f1 = pitch (o1Pitch, o1Detune, o1PitchMod, o1PitchCtrl, o1PitchModSrc, o1PitchCtrlSrc);
        v.f2 = pitch (o2Pitch, o2Detune, o2PitchMod, o2PitchCtrl, o2PitchModSrc, o2PitchCtrlSrc);
        v.fmIndex = std::max (0.0f, uni (s[o1Fm]) + bip (s[o1FmMod]) * modValue (v, s[o1FmModSrc])
                                                  + bip (s[o1FmCtrl]) * ctrlValue (v, s[o1FmCtrlSrc])) * cal.fmMaxIndex;
        v.o2Level = std::clamp (uni (s[mixO2]) * (1.0f + bip (s[mixO2Mod]) * modValue (v, s[mixO2ModSrc])
                                                       + bip (s[mixO2Ctrl]) * ctrlValue (v, s[mixO2CtrlSrc])), 0.0f, 2.0f);

        // filter
        const int type = std::min (7, (int) s[fType]);
        float semis = 0.0f;
        semis += (bip (s[fMod1]) * modValue (v, s[fMod1Src]) + bip (s[fCtrl1]) * ctrlValue (v, s[fCtrl1Src])
                  + bip (s[fMod2]) * modValue (v, s[fMod2Src]) + bip (s[fCtrl2]) * ctrlValue (v, s[fCtrl2Src])) * cal.cutoffModSemitones;
        semis += ((float) s[fKeytrack] - 64.0f) * 0.03125f * ((float) v.note - 64.0f);   // -200..+196 %, around E3
        const float fc = std::clamp (cal.cutoffHz[s[fCutoff]] * fromSemis (semis), 8.0f, (float) sr * 0.45f);
        v.g = std::tan (pi * fc / (float) sr);
        const float reso = (float) s[fReso];
        if (type == 5 || type == 6)
        {
            v.k = type == 5 ? 1.414f : 0.7f;
            const float db = type == 5 ? std::abs (reso - 64.0f) / 64.0f * 12.0f : (reso - 64.0f) / 64.0f * 12.0f;
            v.shelfGain = std::pow (10.0f, db / 20.0f);
        }
        else
            v.k = cal.resonanceK[s[fReso]];                       // 0 at the self-oscillation point, below 0 past it
        v.a1 = 1.0f / (1.0f + v.g * (v.g + std::max (v.k, -0.05f)));
        v.a2 = v.g * v.a1;
        v.a3 = v.g * v.a2;

        // level (Envelope 1 is applied every sample) and place
        v.level = cal.volumeGain[s[aVolume]] * (cal.velocityToAmp ? cal.velocityGain[(size_t) juce_clampVel (v.velocity)] : 1.0f)
                * std::clamp (1.0f + bip (s[aVolMod]) * modValue (v, s[aVolModSrc]) + bip (s[aVolCtrl]) * ctrlValue (v, s[aVolCtrlSrc]), 0.0f, 2.0f);
        float pan = bip (s[aPan]) + bip (s[aPanMod]) * modValue (v, s[aPanModSrc]) + bip (s[aPanCtrl]) * ctrlValue (v, s[aPanCtrlSrc]);
        pan = std::clamp (pan, -1.0f, 1.0f);
        const int out = s[dryOut];
        const bool mono = ! (out == 1 || out == 4 || out == 7);     // a mono output ignores the pan
        v.panL = mono ? 0.7071f : std::cos ((pan + 1.0f) * pi * 0.25f);
        v.panR = mono ? 0.7071f : std::sin ((pan + 1.0f) * pi * 0.25f);
    }

    float oscSample (Osc& o, int shape, int pitchValue, float freq, float fmIn, float fmIndex, bool isOsc1)
    {
        const double dt = (double) freq * (1.0 + (isOsc1 ? (double) (fmIn * fmIndex) : 0.0)) / sr;
        float out = 0.0f;
        switch (shape)
        {
            case 0: out = 4.0f * std::abs ((float) o.phase - 0.5f) - 1.0f; break;
            case 1: out = std::cos (twoPi * (float) o.phase); break;
            case 2:
            {
                const double adt = std::abs (dt);
                out = o.phase < 0.5 ? 1.0f : -1.0f;
                out += polyBlep (o.phase, adt);
                out -= polyBlep (std::fmod (o.phase + 0.5, 1.0), adt);
                break;
            }
            case 3:
                out = 1.0f - 2.0f * (float) o.phase;
                out += polyBlep (o.phase, std::abs (dt));
                break;
            case 4: out = o.held; break;
            case 5:
            {
                // white noise; Pitch below the middle takes highs off, above takes lows off
                const float w = rng.next();
                const float tilt = ((float) pitchValue - 64.0f) / 64.0f;
                o.lp += (0.05f + 0.95f * std::clamp (1.0f + tilt, 0.0f, 1.0f)) * (w - o.lp);
                const float x = tilt < 0.0f ? o.lp : w;
                o.hpz += (0.02f + 0.6f * std::max (0.0f, tilt)) * (x - o.hpz);
                out = tilt > 0.0f ? (x - o.hpz) * (1.0f + tilt) : x;
                break;
            }
            case 6: case 7: case 8:
            {
                const auto& sample = cal.samples[shape - 6];
                const float ratio = sample.semitonesPerStep < 0.0f
                    ? freq / std::max (1e-6f, cal.pitchHz[(size_t) sample.rootPitch])
                    : freq / std::max (1e-6f, cal.pitchHz[(size_t) pitchValue])
                        * fromSemis ((float) (pitchValue - sample.rootPitch) * sample.semitonesPerStep);
                if (! sample.data.empty())
                {
                    const auto idx = (size_t) o.samplePos;
                    if (idx + 1 < sample.data.size())
                    {
                        const float f = (float) (o.samplePos - (double) idx);
                        out = sample.data[idx] + (sample.data[idx + 1] - sample.data[idx]) * f;
                        o.samplePos += (double) ratio * sample.rate / sr;
                    }
                    break;
                }
                // no recording: six detuned squares and noise, high-passed, with the sample's own decay
                static const float ratios[6] = { 1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f };
                const float base = 400.0f * std::clamp (freq / std::max (1e-6f, cal.pitchHz[64]), 0.05f, 8.0f);
                float sum = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    o.metal[i] += base * ratios[i] / (float) sr;
                    if (o.metal[i] >= 1.0f) o.metal[i] -= 1.0f;
                    sum += o.metal[i] < 0.5f ? 1.0f : -1.0f;
                }
                sum = sum / 6.0f * 0.6f + rng.next() * (shape == 8 ? 0.7f : 0.4f);
                o.hpz += 0.55f * (sum - o.hpz);
                const float decayMs = shape == 6 ? 60.0f : shape == 7 ? 380.0f : 1100.0f;
                o.samplePos += 1.0;
                out = (sum - o.hpz) * std::exp (-(float) o.samplePos / msToSamples (decayMs)) * 0.8f;
                break;
            }
            default: out = 0.0f; break;      // external input: not simulated
        }

        o.phase += dt;
        if (o.phase >= 1.0 || o.phase < 0.0)
        {
            o.phase -= std::floor (o.phase);
            o.held = rng.next();
        }
        o.out = out;
        return out;
    }

    float filterSample (Voice& v, float x)
    {
        const auto& s = snd[(size_t) v.sound];
        const int type = std::min (7, (int) s[fType]);
        // the mixer and the drive saturate into the filter
        x = softClip (x * (1.0f + uni (s[fDrive]) * 7.0f) * 0.8f);
        if (type == 0)
            return x;
        if (type == 7)
        {
            // S&H: a lower sample rate, set by the resonance
            const float rate = 44100.0f * std::pow (2.6f / 44100.0f, uni (s[fReso]));
            v.shCount += rate / (float) sr;
            if (v.shCount >= 1.0f)
            {
                v.shCount -= std::floor (v.shCount);
                v.shHold = x;
            }
            return v.shHold;
        }
        const float k = v.k;
        const float v3 = x - v.ic2;
        const float v1 = v.a1 * v.ic1 + v.a2 * v3;
        const float v2 = v.ic2 + v.a2 * v.ic1 + v.a3 * v3;
        v.ic1 = 2.0f * v1 - v.ic1;
        v.ic2 = 2.0f * v2 - v.ic2;
        if (k < 0.0f)
        {
            // ringing on its own: a gentle limit on both states keeps it a sine at the cutoff
            const float m = 1.0f / std::sqrt (1.0f + 0.02f * (v.ic1 * v.ic1 + v.ic2 * v.ic2));
            v.ic1 *= m;
            v.ic2 *= m;
            if (std::abs (v.ic1) + std::abs (v.ic2) < 1e-7f) v.ic1 = 1e-4f;
        }
        const float lp = v2, bp = v1, hp = x - std::max (k, 0.0f) * v1 - v2;
        switch (type)
        {
            case 1: return lp;
            case 2: return bp * std::max (k, 0.15f);
            case 3: return hp;
            case 4: return x - std::max (k, 0.0f) * v1;
            case 5:
            {
                const bool high = s[fReso] < 64;
                return x + (v.shelfGain - 1.0f) * (high ? hp : lp);
            }
            case 6: return x + (v.shelfGain - 1.0f) * bp * k;
            default: return x;
        }
    }

    void renderVoice (Voice& v, float* L, float* R, int n)
    {
        const auto& s = snd[(size_t) v.sound];
        const int shape1 = s[o1Shape], shape2 = s[o2Shape];
        const float m1 = uni (s[mixO1]), ring = uni (s[mixRing]), crack = uni (s[mixCrack]);
        const float crackHz = cal.crackHz[s[crackSpeed]];
        const int crackLen = s[crackLength] >= 50 ? 1 << 30 : std::max (1, (int) s[crackLength]);
        const int fxi = std::min ((int) s[fxSelect], numFx - 1);
        const float send = uni (s[fxSend]);
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            envStep (v.e1, s.data() + env1);
            envStep (v.e2, s.data() + env2);
            lfoStep (v, v.l1, true);
            lfoStep (v, v.l2, false);
            if (v.counter-- <= 0)
            {
                control (v);
                v.counter = 15;
            }

            const float b = oscSample (v.o2, shape2, s[o2Pitch], v.f2, 0.0f, 0.0f, false);
            const float a = oscSample (v.o1, shape1, s[o1Pitch], v.f1, b, v.fmIndex, true);
            float x = a * m1 + b * v.o2Level + a * b * ring;

            // crack: a falling sawtooth on the level, a number of times, then steady
            if (crack > 0.0f && v.crackCycles < crackLen)
            {
                const float saw = 1.0f - (float) v.crackPhase;
                x *= (1.0f - crack) + crack * saw;
                v.crackPhase += crackHz / sr;
                if (v.crackPhase >= 1.0)
                {
                    v.crackPhase -= 1.0;
                    ++v.crackCycles;
                }
            }

            float y = filterSample (v, x);
            if (v.fadeStep > 0.0f)
            {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f)
                {
                    v.active = false;
                    break;
                }
                y *= v.fade;
            }
            const float amp = v.level * (cal.env1ToAmp ? v.e1.level : 1.0f);
            const float wet = y * amp;
            L[i] += wet * v.panL;
            R[i] += wet * v.panR;
            fxIn[fxi][(size_t) i] += wet * send;
            peak = std::max (peak, std::abs (wet));

            const bool envDone = cal.env1ToAmp ? v.e1.stage == Env::Stage::idle : (v.e1.stage == Env::Stage::idle && v.e2.stage == Env::Stage::idle);
            if (envDone && i > 0)
            {
                v.active = false;
                break;
            }
        }
        levels[(size_t) v.sound] = std::max (levels[(size_t) v.sound] * 0.97f, peak);
    }

    void render (float* L, float* R, int n)
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        for (auto& f : fxIn)
        {
            if ((int) f.size() < n) f.resize ((size_t) n);
            std::fill (f.begin(), f.begin() + n, 0.0f);
        }
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, L, R, n);
        for (int e = 0; e < numFx; ++e)
        {
            if (fx[(size_t) e].type() == 0)
                continue;
            fx[(size_t) e].tempo = tempo;
            fx[(size_t) e].process (fxIn[e].data(), L, R, n);   // the effects' tails keep running
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = softClip (L[i] * 0.9f);
            R[i] = softClip (R[i] * 0.9f);
        }
    }

    //==========================================================================
    void handleMidi (const std::uint8_t* d, int size)
    {
        if (size <= 0)
            return;
        const int status = d[0] & 0xF0, channel = (d[0] & 0x0F) + 1;
        if (d[0] == 0xF0)
        {
            ra::Bytes raw (d, d + size);
            auto p = ra::parse (raw);
            if (! p.isRackAttack || p.check == ra::Parsed::Check::bad)
                return;
            const auto& pl = p.payload;
            switch (p.command)
            {
                // the payload starts with the command: CMD BUFN INDEX ...
                case ra::cmd::SNDP:   // CMD BUFN SOUND PAH PAL VAL
                    if (pl.size() >= 6 && pl[2] < numSounds)
                    {
                        const int addr = (pl[3] << 7) | pl[4];
                        if (addr < soundBytes) snd[pl[2]][(size_t) addr] = pl[5];
                    }
                    break;
                case ra::cmd::SNDD:   // CMD BUFN SOUND + data
                    if (pl.size() >= 3 + (size_t) soundBytes && pl[2] < numSounds)
                        std::copy (pl.begin() + 3, pl.begin() + 3 + soundBytes, snd[pl[2]].begin());
                    break;
                case ra::cmd::EFXP:   // CMD BUFN FX PAH PAL VAL
                    if (pl.size() >= 6 && pl[2] < numFx)
                    {
                        const int addr = (pl[3] << 7) | pl[4];
                        if (addr < fxBytes) fx[pl[2]].p[(size_t) addr] = pl[5];
                    }
                    break;
                case ra::cmd::EFXD:
                    if (pl.size() >= 3 + (size_t) fxBytes && pl[2] < numFx)
                        std::copy (pl.begin() + 3, pl.begin() + 3 + fxBytes, fx[pl[2]].p.begin());
                    break;
                default: break;
            }
            return;
        }
        if (size < 2)
            return;
        switch (status)
        {
            case 0x90: if (size >= 3) { if (d[2] > 0) noteOn (channel, d[1], d[2]); else noteOff (channel, d[1]); } break;
            case 0x80: if (size >= 3) noteOff (channel, d[1]); break;
            case 0xB0:
                if (size >= 3)
                {
                    cc[(size_t) (channel - 1)][d[1] & 0x7F] = (float) d[2] / 127.0f;
                    if (d[1] == 123 || d[1] == 120)
                        allNotesOff();
                }
                break;
            case 0xD0: pressure[(size_t) (channel - 1)] = (float) d[1] / 127.0f; break;
            default: break;
        }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            if (v.active)
            {
                v.held = false;
                const auto* p = snd[(size_t) v.sound].data();
                envRelease (v.e1, p + env1);
                envRelease (v.e2, p + env2);
            }
    }
};

//==============================================================================
Synth::Synth() : impl (std::make_unique<Impl>())   { prepare (44100.0); }
Synth::~Synth() = default;

void Synth::prepare (double sampleRate)
{
    impl->sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
    for (auto& f : impl->fx)
        f.prepare ((float) impl->sr);
    for (auto& v : impl->voices)
        v.active = false;
}

void Synth::setCalibration (const Calibration& c)          { impl->cal = c; }
const Calibration& Synth::getCalibration() const            { return impl->cal; }

void Synth::setSound (int sound, const std::uint8_t* data)
{
    if (sound >= 0 && sound < numSounds && data != nullptr)
        std::copy (data, data + soundBytes, impl->snd[(size_t) sound].begin());
}

void Synth::setEffect (int e, const std::uint8_t* data)
{
    if (e >= 0 && e < numFx && data != nullptr)
    {
        const bool typeChanged = impl->fx[(size_t) e].p[0] != data[0];
        std::copy (data, data + fxBytes, impl->fx[(size_t) e].p.begin());
        if (typeChanged)
            impl->fx[(size_t) e].reset();
    }
}

void Synth::setGlobalChannel (int ch)    { impl->globalChannel = std::clamp (ch, 0, 16); }
void Synth::setBaseNote (int note)       { impl->baseNote = std::clamp (note, 0, 127 - numSounds); }
void Synth::setTempo (double bpm)        { impl->tempo = std::clamp (bpm, 20.0, 400.0); }
void Synth::handleMidi (const std::uint8_t* d, int size)   { impl->handleMidi (d, size); }
void Synth::allNotesOff()                { impl->allNotesOff(); }
void Synth::render (float* l, float* r, int n)             { impl->render (l, r, n); }

int Synth::activeVoices() const
{
    int n = 0;
    for (auto& v : impl->voices)
        n += v.active ? 1 : 0;
    return n;
}

float Synth::soundLevel (int sound) const
{
    return sound >= 0 && sound < numSounds ? impl->levels[(size_t) sound] : 0.0f;
}
}
