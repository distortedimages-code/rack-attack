// The sound's parameters, section by section as on the front panel (addresses:
// docs/SYSEX.md). Shared by the Son page and the sequencer page.

#pragma once

#include <vector>

namespace ui
{
struct ParamDef { int address; const char* label; bool choice; };
struct SectionDef { const char* title; const char* tab; std::vector<std::vector<ParamDef>> rows; };

// The sound, section by section as on the front panel (addresses: docs/SYSEX.md)
inline const std::vector<SectionDef>& soundSections()
{
    static const std::vector<SectionDef> sections {
        { "Oscillateur 1", "Osc 1", { { { 0x01, "Shape", true }, { 0x02, "Pitch", false }, { 0x03, "Detune", false }, { 0x04, "Start", true }, { 0x09, "FM", false } },
                             { { 0x05, "Pitch Mod", false }, { 0x07, "Mod Src", true }, { 0x06, "Pitch Ctrl", false }, { 0x08, "Ctrl Src", true } },
                             { { 0x0A, "FM Mod", false }, { 0x0C, "Mod Src", true }, { 0x0B, "FM Ctrl", false }, { 0x0D, "Ctrl Src", true } } } },
        { "Oscillateur 2", "Osc 2", { { { 0x10, "Shape", true }, { 0x11, "Pitch", false }, { 0x12, "Detune", false }, { 0x13, "Start", true } },
                             { { 0x14, "Pitch Mod", false }, { 0x16, "Mod Src", true }, { 0x15, "Pitch Ctrl", false }, { 0x17, "Ctrl Src", true } } } },
        { "Mix / Crack", "Mix",   { { { 0x1A, "Osc 1", false }, { 0x1B, "Osc 2", false }, { 0x1C, "Ringmod", false }, { 0x1D, "Crack", false },
                               { 0x24, "Crack Speed", false }, { 0x25, "Crack Length", false } },
                             { { 0x1E, "Osc 2 Mod", false }, { 0x20, "Mod Src", true }, { 0x1F, "Osc 2 Ctrl", false }, { 0x21, "Ctrl Src", true } } } },
        { "Filtre", "Filtre",        { { { 0x26, "Type", true }, { 0x27, "Cutoff", false }, { 0x28, "Resonance", false }, { 0x29, "Drive", false }, { 0x2A, "Keytrack", false } },
                             { { 0x2B, "Mod 1", false }, { 0x2D, "Mod Src", true }, { 0x2C, "Ctrl 1", false }, { 0x2E, "Ctrl Src", true } },
                             { { 0x2F, "Mod 2", false }, { 0x31, "Mod Src", true }, { 0x30, "Ctrl 2", false }, { 0x32, "Ctrl Src", true } } } },
        { "Ampli", "Ampli",         { { { 0x35, "Volume", false }, { 0x3A, "Pan", false } },
                             { { 0x36, "Vol Mod", false }, { 0x38, "Mod Src", true }, { 0x37, "Vol Ctrl", false }, { 0x39, "Ctrl Src", true } },
                             { { 0x3B, "Pan Mod", false }, { 0x3D, "Mod Src", true }, { 0x3C, "Pan Ctrl", false }, { 0x3E, "Ctrl Src", true } } } },
        { "Enveloppe 1", "Env 1",   { { { 0x3F, "Attack", false }, { 0x40, "Decay", false }, { 0x41, "Sustain", false }, { 0x42, "Release", false }, { 0x43, "Shape", false } } } },
        { "Enveloppe 2", "Env 2",   { { { 0x45, "Attack", false }, { 0x46, "Decay", false }, { 0x47, "Sustain", false }, { 0x48, "Release", false }, { 0x49, "Shape", false } } } },
        { "LFO 1", "LFO 1",         { { { 0x4B, "Speed", false }, { 0x4C, "Shape", true }, { 0x4D, "Sync", true }, { 0x4E, "One Shot", true } } } },
        { "LFO 2", "LFO 2",         { { { 0x4F, "Speed", false }, { 0x50, "Shape", true }, { 0x51, "Sync", true }, { 0x52, "Mod", false }, { 0x53, "Mod Src", false } } } },
        { "Sortie", "Sortie",        { { { 0x55, "Dry Out", true }, { 0x56, "FX", true }, { 0x57, "FX Send", false }, { 0x5E, "XOR", true }, { 0x6C, "Pattern int.", true } },
                             { { 0x58, "Tuned Chn", true }, { 0x59, "Transpose", false }, { 0x5A, "Low Key", false }, { 0x5B, "High Key", false } } } },
    };
    return sections;
}
}
