// French texts: JUCE reads a plain "..." as ASCII, so every accented literal
// goes through this suffix to be read as UTF-8: "Écouter"_fr.

#pragma once

#include <JuceHeader.h>

inline juce::String operator""_fr (const char* text, std::size_t size)
{
    return juce::String::fromUTF8 (text, (int) size);
}

// Capitals, French accents included (the C library leaves "é" alone on some systems).
inline juce::String upper (const juce::String& text)
{
    static const juce::String lower = juce::String::fromUTF8 ("\xc3\xa0\xc3\xa2\xc3\xa4\xc3\xa7\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab\xc3\xae\xc3\xaf\xc3\xb4\xc3\xb6\xc3\xb9\xc3\xbb\xc3\xbc\xc5\x93");
    static const juce::String capital = juce::String::fromUTF8 ("\xc3\x80\xc3\x82\xc3\x84\xc3\x87\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b\xc3\x8e\xc3\x8f\xc3\x94\xc3\x96\xc3\x99\xc3\x9b\xc3\x9c\xc5\x92");
    return text.replaceCharacters (lower, capital).toUpperCase();
}
