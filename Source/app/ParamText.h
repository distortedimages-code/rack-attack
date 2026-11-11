// How a sound setting's value reads, the same everywhere (main page, Son page, popups):
// a list by its name, an amount that goes both ways from its middle (64 = 0) as a
// signed percentage, the pan as L/C/R, keys as notes, levels in %, with the scales of
// the front panel where the manual gives them (docs/SYSEX.md).

#pragma once

#include <JuceHeader.h>

#include "../core/RaSysex.h"

namespace ui
{
    // 64 is the middle (no effect); below goes one way, above the other
    inline bool isBipolar (int address)
    {
        switch (address)
        {
            case 0x05: case 0x06: case 0x0A: case 0x0B: case 0x14: case 0x15: case 0x1E: case 0x1F:
            case 0x2A: case 0x2B: case 0x2C: case 0x2F: case 0x30: case 0x36: case 0x37: case 0x3B: case 0x3C:
            case 0x52: case 0x3A: case 0x59:
                return true;
            default:
                return false;
        }
    }

    inline juce::String valueText (int address, int v)
    {
        const auto names = ra::valueNames (ra::Object::sound, address);
        if (! names.empty())
            return v >= 0 && v < (int) names.size() ? juce::String::fromUTF8 (names[(size_t) v].c_str()) : juce::String (v);

        auto withSign = [] (int x) { return x > 0 ? "+" + juce::String (x) : juce::String (x); };
        switch (address)
        {
            case 0x3A: return v == 64 ? juce::String ("C") : v < 64 ? "L" + juce::String (64 - v) : "R" + juce::String (v - 64);
            case 0x2A: return withSign (juce::roundToInt ((v - 64) * 3.125)) + "%";          // keytrack: -200 % .. +196 %
            case 0x59: return withSign (juce::jlimit (-48, 48, v - 64));                    // transpose, semitones
            case 0x5A: case 0x5B: return juce::MidiMessage::getMidiNoteName (v, true, true, 3);   // C-2 .. G8
            case 0x25: return v >= 50 ? juce::String ("INF") : juce::String (juce::jmax (1, v));     // crack length
            case 0x09: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x57:
                return juce::String (juce::roundToInt (v * 100.0 / 127.0)) + "%";
            default: break;
        }
        if (isBipolar (address))
            return withSign (juce::roundToInt ((v - 64) * 100.0 / 64.0)) + "%";           // -100 % .. +98 %
        return juce::String (v);
    }
}
