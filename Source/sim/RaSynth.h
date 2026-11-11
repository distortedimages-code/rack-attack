// A software RackAttack: the same sound architecture, driven by the same data and
// MIDI messages as the real unit, so the plug-in can play without it.
//
// Per sound (24 per kit): two oscillators (Triangle, Sine, Square, Saw, S&H, Noise,
// HH Closed, HH Open, Crash; Osc 2 modulates Osc 1 in linear FM), a ring modulator,
// the crack (a sawtooth amplitude modulation repeated a number of times, for claps),
// a multimode filter with drive (Low 12 dB, Band, High, Notch, Shelf, Bell, S&H),
// two envelopes with a Shape for decay and release, two LFOs (key sync, tempo sync,
// one shot), volume and pan, each with a modulation source and a control source.
// Sounds go dry to an output and are sent to one of four effects (Chorus, Flanger,
// Phaser, Overdrive, Delay, ModDelay, Reverb, Lo-Fi). XOR groups cut each other.
//
// The manual gives the structure but few numbers: how a value 0..127 turns into Hz,
// milliseconds or gain lives in a Calibration. Its defaults come from the manual
// where it says something (pitch range, the filter oscillating at 440 Hz at cutoff 64,
// crack speed 1 Hz to 5 kHz, delay 1.4 ms to 1.48 s...) and from guesses elsewhere;
// measurements of a real unit replace them. The HH and Crash shapes are samples in
// the unit: without a recording of them, they are imitated.
//
// No JUCE here; everything happens on the thread that calls render().

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra::synth
{
constexpr int numSounds = 24;
constexpr int soundBytes = 109;
constexpr int fxBytes = 18;
constexpr int numFx = 4;
constexpr int maxVoices = 25;

struct Calibration
{
    // value 0..127 -> physical value
    std::array<float, 128> pitchHz {};        // oscillator pitch
    std::array<float, 128> cutoffHz {};       // filter cutoff
    std::array<float, 128> attackMs {};       // envelope times (attack: to the top; decay / release: to the end)
    std::array<float, 128> decayMs {};
    std::array<float, 128> releaseMs {};
    std::array<float, 128> lfoHz {};
    std::array<float, 128> crackHz {};
    std::array<float, 128> volumeGain {};     // linear
    std::array<float, 128> velocityGain {};   // how loud a velocity plays when it controls volume
    std::array<float, 128> sustainLevel {};   // envelope sustain value -> level (0..1)
    std::array<float, 128> resonanceK {};     // filter damping (2 = none, 0 = rings by itself, < 0 = rings louder)

    // The decay / release curves for Shape 0, 32, 64, 96, 127 (1 down to 0 over 64 points),
    // measured on a unit; empty: the manual's description (exp, linear, inverse, cosine).
    std::vector<std::array<float, 64>> fallCurves;

    float pitchModSemitones = 48.0f;          // a full Pitch Mod / Ctrl (100 %)
    float cutoffModSemitones = 96.0f;         // a full Cutoff Mod / Ctrl
    float fmMaxIndex = 8.0f;                  // FM 100 %: "phase overflows by a factor of 8"
    float selfOscillation = 114.0f;           // resonance from which the filter rings by itself
    int tunedRootNote = 60;                   // the note that plays a tuned sound at its own pitch
    bool env1ToAmp = true;                    // Envelope 1 shapes the level of every sound
    bool velocityToAmp = false;               // velocity changes the level even without a control set

    struct Sample
    {
        std::vector<float> data;              // mono
        double rate = 44100.0;
        int rootPitch = 64;                   // the Pitch value at which it was recorded
    };
    Sample samples[3];                        // HH Closed, HH Open, Crash (recorded from a unit)

    std::string source = "defaults";          // where the numbers come from

    static Calibration defaults();
};

class Synth
{
public:
    Synth();
    ~Synth();

    void prepare (double sampleRate);
    void setCalibration (const Calibration&);
    const Calibration& getCalibration() const;

    void setSound (int sound, const std::uint8_t* data);      // soundBytes
    void setEffect (int fx, const std::uint8_t* data);        // fxBytes
    void setGlobalChannel (int channel);                      // 0 = omni, 1..16
    void setBaseNote (int note);                              // note of sound 1
    void setTempo (double bpm);

    // A whole MIDI message: notes, controllers, aftertouch, pitch bend, and the
    // RackAttack's SysEx parameter changes and dumps for the edit buffer.
    void handleMidi (const std::uint8_t* data, int size);
    void allNotesOff();

    // Overwrites numSamples samples of each channel.
    void render (float* left, float* right, int numSamples);

    int activeVoices() const;
    float soundLevel (int sound) const;   // recent peak, for meters

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
