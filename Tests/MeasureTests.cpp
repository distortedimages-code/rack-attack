// The measuring procedure, run on the simulator with a known calibration that is
// not the default one: the Bench must find it again. Also the signal helpers, the
// "nothing comes in" case, and the measurement file.

#include <JuceHeader.h>

#include "../Source/app/Measure.h"
#include "../Source/app/Text.h"

#include <cstdio>

using ra::synth::Calibration;

namespace
{
int failures = 0;

void expect (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void near (double got, double want, double relative, const juce::String& what)
{
    const bool ok = std::abs (got - want) <= std::abs (want) * relative;
    expect (ok, what + " : " + juce::String (got, 4) + " (attendu " + juce::String (want, 4) + ")");
}

void within (double got, double want, double absolute, const juce::String& what)
{
    expect (std::abs (got - want) <= absolute, what + " : " + juce::String (got, 4) + " (attendu " + juce::String (want, 4) + ")");
}

Calibration truth()
{
    auto c = Calibration::defaults();
    for (int v = 0; v < 128; ++v)
    {
        const float t = (float) v / 127.0f;
        c.pitchHz[(size_t) v] = 0.012f * std::pow (21000.0f / 0.012f, t);
        c.cutoffHz[(size_t) v] = 30.0f * std::pow (18000.0f / 30.0f, t);
        c.attackMs[(size_t) v] = v == 0 ? 0.0f : 0.35f * std::exp2 (t * 14.0f);
        c.decayMs[(size_t) v] = 1.3f * std::exp2 (t * 14.0f);
        c.releaseMs[(size_t) v] = 0.8f * std::exp2 (t * 14.0f);
        c.lfoHz[(size_t) v] = 0.018f * std::exp2 (t * 11.0f);
        c.crackHz[(size_t) v] = 1.1f * std::pow (4500.0f / 1.1f, t);
        c.volumeGain[(size_t) v] = std::pow (t, 1.5f);
        c.velocityGain[(size_t) v] = std::pow (t, 1.7f);
        c.sustainLevel[(size_t) v] = std::pow (t, 1.3f);
    }
    c.velocityToAmp = true;
    c.selfOscillation = 110.0f;
    for (int v = 0; v < 128; ++v)
        c.resonanceK[(size_t) v] = 2.0f * (1.0f - (float) v / c.selfOscillation);
    c.pitchModSemitones = 36.0f;
    c.cutoffModSemitones = 72.0f;
    c.detuneSemitones = 1.5f;
    c.tunedRootNote = 64;
    return c;
}

// A sound with a bit of everything, rendered with a calibration: amplitude per 10 ms
std::vector<double> render (const Calibration& cal)
{
    ra::synth::Synth s;
    s.prepare (48000.0);
    s.setCalibration (cal);
    s.setGlobalChannel (1);
    s.setBaseNote (36);
    std::uint8_t d[ra::synth::soundBytes];
    std::fill (std::begin (d), std::end (d), 64);
    auto set = [&d] (int a, int v) { d[a] = (std::uint8_t) v; };
    set (0x01, 3); set (0x02, 96); set (0x03, 0); set (0x04, 0); set (0x07, 0); set (0x08, 0); set (0x09, 0);
    set (0x0C, 0); set (0x0D, 0); set (0x1A, 90); set (0x1B, 0); set (0x1C, 0); set (0x1D, 0);
    set (0x20, 0); set (0x21, 0); set (0x26, 1); set (0x27, 80); set (0x28, 60); set (0x29, 0);
    set (0x2D, 0); set (0x2E, 0); set (0x31, 0); set (0x32, 0); set (0x35, 110); set (0x38, 0); set (0x39, 0);
    set (0x3D, 0); set (0x3E, 0); set (0x3F, 40); set (0x40, 76); set (0x41, 30); set (0x42, 70); set (0x43, 40);
    set (0x45, 0); set (0x46, 60); set (0x47, 0); set (0x48, 0); set (0x49, 0);
    set (0x4C, 0); set (0x4D, 0); set (0x4E, 0); set (0x50, 0); set (0x51, 0); set (0x53, 0);
    set (0x55, 1); set (0x56, 0); set (0x57, 0); set (0x58, 0); set (0x5A, 0); set (0x5B, 127); set (0x5E, 0);
    s.setSound (0, d);

    std::vector<double> env;
    std::vector<float> L (480), R (480);
    const std::uint8_t on[] = { 0x90, 36, 100 }, off[] = { 0x80, 36, 0 };
    s.handleMidi (on, 3);
    for (int block = 0; block < 300; ++block)
    {
        if (block == 60)
            s.handleMidi (off, 3);
        s.render (L.data(), R.data(), 480);
        double sum = 0.0;
        for (auto v : L) sum += (double) v * v;
        env.push_back (std::sqrt (sum / 480.0));
    }
    return env;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    //==========================================================================
    {
        std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) std::sin (2.0 * 3.14159265358979 * 1234.5 * (double) i / 48000.0);
        near (measure::frequencyOf (x, 1000, 20000, 48000.0), 1234.5, 0.0005, "fréquence d'un sinus"_fr);
        near (measure::peakFrequency (x, 1000, 20000, 48000.0, 20.0, 20000.0), 1234.5, 0.002, "pic du spectre");
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) std::sin (2.0 * 3.14159265358979 * 17500.0 * (double) i / 48000.0);
        near (measure::frequencyOf (x, 0, 20000, 48000.0), 17500.0, 0.001, "fréquence d'un sinus aigu"_fr);
    }

    //==========================================================================
    {
        measure::SynthRig deaf (Calibration::defaults(), 1, 36);
        deaf.gain = 0.0f;
        measure::Bench bench (deaf, 0x7F, 1, 36, 5);
        const auto r = bench.run();
        expect (! r.ok && r.message.contains ("n'entend pas"), "sans signal : " + r.message);
    }

    //==========================================================================
    const auto want = truth();
    measure::SynthRig rig (want, 1, 36);
    measure::Bench bench (rig, 0x7F, 1, 36, 5);
    int steps = 0, redone = 0;
    juce::String last;
    bool bumped = false;
    bench.onProgress = [&] (int done, int, const juce::String& what)
    {
        if (done <= steps)
            ++redone;
        steps = done;
        if (! bumped && what.contains ("volumes"))
        {
            rig.touch = true;            // someone touches the unit during this part
            bumped = true;
        }
        if (what != last)
        {
            std::printf ("     ... %s\n", what.toRawUTF8());
            last = what;
        }
    };
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto r = bench.run();
    const double seconds = (double) rig.mark() / rig.sampleRate();
    std::printf ("     %d étapes, %.0f s de mesure simulée, calculées en %.1f s\n", steps,
                 seconds, (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
    for (auto& w : r.warnings)
        std::printf ("     remarque : %s\n", w.toRawUTF8());
    expect (r.ok, "la mesure va au bout : " + r.message);
    expect (redone == 1, "la partie où la machine a été touchée est refaite"_fr);
    expect (std::abs (steps - measure::Bench::expectedSteps) < 60, "nombre d'étapes prévu : "_fr + juce::String (steps));
    expect (seconds < 600.0, "moins de dix minutes : " + juce::String (seconds, 0) + " s");

    const auto& c = r.calibration;
    for (int v : { 20, 64, 90, 110, 127 })
        near (c.pitchHz[(size_t) v], want.pitchHz[(size_t) v], 0.01, "hauteur " + juce::String (v));
    for (int v : { 40, 64, 90, 110 })
        near (c.cutoffHz[(size_t) v], want.cutoffHz[(size_t) v], 0.04, "coupure " + juce::String (v));
    for (int v : { 64, 96, 120 })
        near (c.attackMs[(size_t) v], want.attackMs[(size_t) v], 0.12, "attaque " + juce::String (v));
    for (int v : { 48, 80, 112 })
        near (c.decayMs[(size_t) v], want.decayMs[(size_t) v], 0.12, "chute " + juce::String (v));
    for (int v : { 48, 80, 112 })
        near (c.releaseMs[(size_t) v], want.releaseMs[(size_t) v], 0.12, "relâchement "_fr + juce::String (v));
    for (int v : { 127, 100, 80 })
        near (c.lfoHz[(size_t) v], want.lfoHz[(size_t) v], 0.04, "LFO " + juce::String (v));
    for (int v : { 60, 90, 120 })
        near (c.crackHz[(size_t) v], want.crackHz[(size_t) v], 0.04, "crack " + juce::String (v));
    within (c.volumeGain[64], want.volumeGain[64], 0.03, "volume 64");
    within (c.sustainLevel[64], want.sustainLevel[64], 0.03, "sustain 64");
    expect (c.velocityToAmp, "la vélocité joue sur le volume"_fr);
    within (c.velocityGain[64], want.velocityGain[64], 0.04, "vélocité 64"_fr);
    expect (c.env1ToAmp, "l'enveloppe 1 règle le volume"_fr);
    within (c.selfOscillation, want.selfOscillation, 3.0, "le filtre siffle à partir de"_fr);
    near (c.resonanceK[48], want.resonanceK[48], 0.15, "amortissement 48");
    near (c.resonanceK[96], want.resonanceK[96], 0.3, "amortissement 96");
    within (c.pitchModSemitones, want.pitchModSemitones, 1.0, "profondeur de la modulation de hauteur");
    within (c.cutoffModSemitones, want.cutoffModSemitones, 3.0, "profondeur de la modulation de coupure");
    within (c.detuneSemitones, want.detuneSemitones, 0.05, "désaccord"_fr);
    expect (c.tunedRootNote == want.tunedRootNote, "note du son accordé : "_fr + juce::String (c.tunedRootNote));
    expect (c.fallCurves.size() == 9, "neuf formes de chute");
    for (int i = 0; i < 3; ++i)
        expect (c.samples[i].data.size() > 1000, "cymbale " + juce::String (i + 1) + " enregistrée"_fr);

    if (std::getenv ("RA_PROFILE") != nullptr)
    {
        std::printf ("decay76 %.1f / %.1f  sustain30 %.4f / %.4f  attack40 %.2f / %.2f release70 %.1f / %.1f\n",
                     c.decayMs[76], want.decayMs[76], c.sustainLevel[30], want.sustainLevel[30], c.attackMs[40], want.attackMs[40], c.releaseMs[70], want.releaseMs[70]);
        for (int i = 0; i < 9; ++i)
        {
            std::printf ("curve %d:", i);
            for (int k : { 1, 3, 6, 12, 24, 40, 56 }) std::printf (" %.3f", c.fallCurves[(size_t) i][(size_t) k]);
            std::printf ("\n");
        }
    }
    // The simulator with what was measured sounds like the one it was measured on
    {
        const auto a = render (want), b = render (c);
        double worst = 0.0, top = 0.0;
        size_t where = 0;
        for (auto v : a)
            top = std::max (top, v);
        for (size_t k = 2; k < a.size(); ++k)
            if (a[k] > top * 0.03 && std::abs (juce::Decibels::gainToDecibels (b[k] / a[k])) > worst)
            {
                worst = std::abs (juce::Decibels::gainToDecibels (b[k] / a[k]));
                where = k;
            }
        if (std::getenv ("RA_PROFILE") != nullptr)
            for (size_t k = 0; k < a.size(); k += 3)
                std::printf ("%4d ms  %.4f  %.4f\n", (int) k * 10, a[k], b[k]);
        std::printf ("     pire écart à %d ms : %.4f contre %.4f\n", (int) where * 10, b[where], a[where]);
        expect (worst < 1.5, "même son, à "_fr + juce::String (worst, 2) + " dB près au pire"_fr);
    }

    // The file
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ra-measure-test");
        folder.deleteRecursively();
        const auto file = measure::save (r, folder);
        expect (file.existsAsFile() && file.hasFileExtension ("ramesure"), "fichier écrit : "_fr + file.getFileName());
        Calibration back;
        juce::String error;
        expect (measure::load (file, back, error), "fichier relu " + error);
        near (back.cutoffHz[64], c.cutoffHz[64], 1e-5, "coupure relue");
        expect (back.fallCurves.size() == 9 && back.samples[2].data.size() == c.samples[2].data.size(), "courbes et cymbales relues");
        expect (std::abs (back.samples[0].data[100] - c.samples[0].data[100]) < 1e-6f, "échantillons intacts"_fr);
        folder.deleteRecursively();
    }

    std::printf (failures == 0 ? "MeasureTests: all passed\n" : "MeasureTests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
