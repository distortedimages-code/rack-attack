// The main page, in the retro pixel look: everything about one track at once.
//
// At the top, the clock (play, tempo, swing, how busy the MIDI cable is) and the
// 24 voices, which choose the track and flash when they play (white from the
// sequencer, red from a keyboard). In the middle, the sound of that track, a
// section at a time, with pixel knobs. At the bottom, its sequencer: a row of 16
// steps, 8 pages for 128 steps, as steps or as notes (a melody on the sound's
// Tuned Channel).
//
// Shift + click on steps selects them: the knobs then lock values on those steps
// (yellow) instead of changing the sound.

#pragma once

#include <JuceHeader.h>

#include "Engine.h"
#include "ParamText.h"
#include "Pixel.h"
#include "Sequencer.h"
#include "SoundSections.h"
#include "SoundViz.h"

class SeqPage : public juce::Component,
                private juce::Timer
{
public:
    SeqPage (Engine&, Sequencer&);
    ~SeqPage() override;

    void selectTrack (int track);
    int getSelectedTrack() const   { return track; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Zone
    {
        none, play, tempo, swing, voice,
        section, listen, param, tune,
        mode, length, division, mute, options, miniMap, step, trig, clearSelection
    };
    struct Hit { Zone zone = Zone::none; int index = -1; };

    // what the voices show, copied out of the pattern so the lock is held briefly
    struct Voice
    {
        ra::seq::Mode mode = ra::seq::Mode::steps;
        ra::seq::Division division = ra::seq::Division::sixteenth;
        bool mute = false, locks = false;
        int length = 16;
        juce::String name;
        std::vector<bool> trigs;
    };

    void timerCallback() override;
    void layout();
    Hit zoneAt (juce::Point<int>) const;

    void paintHeader (juce::Graphics&, double now, double tempo, int swing);
    void paintVoices (juce::Graphics&, const std::array<Voice, ra::seq::numTracks>&, double now, double ppq);
    void paintSound (juce::Graphics&, const ra::seq::Track&);
    void paintSequencer (juce::Graphics&, const ra::seq::Track&, double ppq);
    void paintSteps (juce::Graphics&, const ra::seq::Track&, int playing);
    void paintMiniMap (juce::Graphics&, const ra::seq::Track&, int playing);
    void paintTrigRow (juce::Graphics&, const ra::seq::Track&);

    // the sound's parameters of the section shown
    const std::vector<ui::ParamDef>& params() const   { return sectionParams[(size_t) section]; }

    // Every setting on screen: the section's, then the quick access row (the same few
    // settings for every sound, some swapped for what this sound needs).
    struct Quick { ui::ParamDef def; juce::String why; };
    std::vector<Quick> quickFor (int sound) const;
    void updateSlots();                    // slots = the section's settings + the quick row
    std::vector<ui::ParamDef> slots;
    std::vector<juce::String> slotWhy;     // why a quick setting is there (empty: always there)
    int quickFrom = 0;
    juce::Rectangle<int> quickRow, quickHeader, tuneBox;
    void toggleTuned();
    int paramMax (int address) const;
    int shownValue (const ra::seq::Track&, int slot) const;   // the lock of the selected steps, else the sound's
    void setParam (int slot, int value);
    void clearParamLocks (int slot);
    void showChoices (int slot);

    // the selected steps' settings
    int trigValue (const ra::seq::Track&, int field) const;
    void setTrigValue (int field, int value);
    juce::String trigText (const ra::seq::Track&, int field) const;

    void toggleSelection (int step);
    bool isSelected (int step) const;
    void showVoiceMenu (int voice);
    void showOptions();
    void keepForUndo (const juce::String& what);      // the pattern as it is, for "Annuler"
    std::unique_ptr<ra::seq::Pattern> undo;
    juce::String undoWhat;
    void showStepMenu (int step);
    void rename (int voice);
    static int playingStep (ra::seq::Division, int length, double ppq);

    Engine& engine;
    Sequencer& seq;

    int track = 0, page = 0, section = 3;
    std::vector<int> selection;            // steps of the track selected for locks
    int lastNote = 60;
    ra::seq::Track clipboard;
    bool hasClipboard = false;
    std::vector<std::vector<ui::ParamDef>> sectionParams;

    // geometry
    juce::Rectangle<int> header, mascotArea, playBox, tempoBox, swingBox, loadBox, kitBox;
    std::array<juce::Rectangle<int>, ra::seq::numTracks> voices;
    juce::Rectangle<int> soundPanel, soundTitle, listenBox, vizBox;
    std::array<float, 128> shownValues {};      // what the picture shows, moving towards the values
    bool shownReady = false;
    std::vector<juce::Rectangle<int>> sectionTabs, paramBoxes, quickBoxes;
    juce::Rectangle<int> seqPanel, nameBox, lengthBox, divisionBox, muteBox, optionsBox, hintRow, miniMap, trigRow, clearBox;
    std::array<juce::Rectangle<int>, 2> modeTabs;
    std::array<juce::Rectangle<int>, 16> stepBoxes;
    std::array<juce::Rectangle<int>, 7> trigBoxes;
    int laidOutSection = -1;

    // dragging
    struct Drag
    {
        Hit hit;
        int startY = 0, startValue = 0;
        bool paintOn = false, moved = false, noteDrag = false;
        int lastStep = -1;
    } drag;
    Hit hover;

    std::unique_ptr<juce::TextEditor> nameEditor;

    //==========================================================================
    // Little animations, all in steps (no smooth fades: pixels and dithers)
    void paintOverlays (juce::Graphics&, double now);
    void showPopup (int slot, const ra::seq::Track&);
    void addBurst (juce::Point<int> at, juce::Colour);
    void showToast (const juce::String&, juce::Colour = px::white);
    void startWipe()                          { wipeStart = juce::Time::getMillisecondCounterHiRes(); }
    void paintSectionTitle (juce::Graphics&, juce::Rectangle<int> screen, const juce::String&, double t);
    double titleStart = -1e9;                 // the section's name flies over the screen

    struct Popup
    {
        juce::String name, value;
        float position = 0.0f, marker = -1.0f;
        bool choice = false, centred = false;
        juce::Colour colour;
        juce::Rectangle<int> anchor;
        double shownMs = -1e9;
    } popup;
    struct Burst { juce::Point<int> at; double startMs; juce::Colour colour; };
    std::vector<Burst> bursts;
    struct Toast { juce::String text; juce::Colour colour; double startMs = -1e9; } toast;
    double wipeStart = -1e9;
    double pointerFromX = -1.0, pointerStart = -1e9;
    double stopWaveStart = -1e9, excitedUntil = 0.0;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqPage)
};
