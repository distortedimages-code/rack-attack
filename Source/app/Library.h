// Kits and sounds on the computer: Documents/RackAttack/
//   Kits/<name>.syx                 a whole kit (program, 24 sounds, 4 FX)
//   Sons/<name>.syx                 one sound
//   Morphs/<name>.ramorph           kits A and B of the morph and the fader's position
//   Usine/<set>/<name>.syx           Waldorf's own files, imported (factory sets, bonus kits, patterns): kits with
//                                    their internal patterns; Usine/Patterns/ holds the files of patterns alone
//   Sauvegardes/<date>/P01 - ….syx  the 50 programs of the unit, one file each

#pragma once

#include <JuceHeader.h>

#include "../core/RaKit.h"

class Library
{
public:
    explicit Library (juce::File rootFolder = {});

    struct Item
    {
        juce::File file;
        juce::String title, group;   // group: "Mes kits", "Mes sons" or the backup date
        bool isSound = false;
        bool isMorph = false;
        bool isPatterns = false;     // internal patterns alone (no kit)
    };

    // Waldorf's files (.mid as Waldorf ships them, .syx, or .zip of them, zips inside included) into Usine/.
    // The operating system updates and anything else are skipped. Importing twice replaces the files.
    struct Imported { int kits = 0, patternSets = 0, skipped = 0; juce::StringArray sets; };
    Imported importWaldorf (const juce::File& file) const;

    juce::Array<Item> kits() const;      // my kits, then the backups (newest first)
    juce::Array<Item> sounds() const;
    juce::Array<Item> morphs() const;

    juce::File saveKit (const ra::Kit& kit, const juce::String& name) const;
    juce::File saveSound (const ra::Bytes& soundData, const juce::String& name) const;
    juce::File saveMorph (const std::vector<std::uint8_t>& preset, const juce::String& name) const;
    juce::File newBackupFolder() const;

    static std::optional<ra::Kit> loadKit (const juce::File& file);
    static std::optional<ra::Bytes> loadSound (const juce::File& file);
    static std::optional<std::vector<std::uint8_t>> loadMorph (const juce::File& file);

    juce::File getRoot() const   { return root; }

private:
    void importData (const juce::String& set, const juce::String& stem, const ra::Bytes& data, Imported&) const;
    void importZip (juce::ZipFile&, const juce::String& set, Imported&, int depth) const;

    juce::File root, kitsFolder, soundsFolder, backupsFolder, morphsFolder, usineFolder;
};
