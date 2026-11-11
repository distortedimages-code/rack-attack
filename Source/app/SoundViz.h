// Pixel pictures of a sound, one per section of the sound page: the oscillator's
// wave, the mix levels and the crack, the filter's curve, the stereo place, the
// envelope's shape, the LFO's wave, the routing and the keys played as notes.
// Drawn from the values (0..127, or the index of a choice), which the caller can
// move gradually so the picture stretches towards a new value. Some parts move by
// themselves (a wave scrolling, a dot running along the envelope), from `timeMs`.

#pragma once

#include <JuceHeader.h>

namespace viz
{
    using Value = std::function<float (int address)>;

    // section: the index in ui::soundSections()
    void draw (juce::Graphics&, juce::Rectangle<int> area, int section, const Value& value,
               juce::Colour ink, double timeMs);
}
