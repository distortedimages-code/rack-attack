// Pixel pictures of a sound, one per section of the sound page: the oscillator's
// wave, the mix levels and the crack, the filter's curve, the stereo place, the
// envelope's shape, the LFO's wave, the routing and the keys played as notes.
// Drawn from the values (0..127, or the index of a choice), which the caller can
// move gradually so the picture stretches towards a new value. Some parts move by
// themselves: the envelope's dot runs at each note, the LFO's dot goes at the LFO's real speed (from the
// simulator's measured times), from `timeMs` and `Live`.

#pragma once

#include <JuceHeader.h>

namespace viz
{
    using Value = std::function<float (int address)>;

    // What makes the moving parts follow what is heard: the sound's last note and the real times.
    struct Live
    {
        int sound = 0;
        double lastHitMs = -1.0;              // the sound's last note, on the timeMs clock (-1: none yet)
        double tempo = 120.0;                 // synced LFOs
    };

    // section: the index in ui::soundSections(); timeMs: juce::Time::getMillisecondCounterHiRes()
    void draw (juce::Graphics&, juce::Rectangle<int> area, int section, const Value& value,
               juce::Colour ink, double timeMs, const Live& live = {});
}
