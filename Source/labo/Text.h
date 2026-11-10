// French texts: JUCE reads a plain "..." as ASCII, so every accented literal
// goes through this suffix to be read as UTF-8: "Écouter"_fr.

#pragma once

#include <JuceHeader.h>

inline juce::String operator""_fr (const char* text, std::size_t size)
{
    return juce::String::fromUTF8 (text, (int) size);
}
