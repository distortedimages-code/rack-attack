// One Labo session on disk: Documents/RackAttack Labo/session-<date>/
//   journal.txt     everything sent and received, with times and descriptions
//   recus/*.syx     every RackAttack dump received, one file each
//   tout-recu.syx   the same dumps, end to end (re-sendable later)
//   carte.txt       the parameter addresses seen (front panel knobs)
// Message thread only.

#pragma once

#include <JuceHeader.h>

#include "../core/RaSysex.h"

class Session
{
public:
    Session();

    juce::File getFolder() const   { return folder; }

    void line (const juce::String& text);                                   // timestamped journal line
    void saveDump (const ra::Bytes& sysex, const juce::String& name);       // recus/NNNN-name.syx
    void saveText (const juce::String& fileName, const juce::String& text);

    // Zips the session folder next to it and returns the zip (or an empty File on failure).
    juce::File makeZip();

    static juce::String timeStamp();

private:
    juce::File folder, dumps;
    std::unique_ptr<juce::FileOutputStream> journal;
    int dumpCount = 0;
};
