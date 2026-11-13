// The morph fader of the plug-in (ra::Morph): kits A and B, the position, and the kit heard
// kept on the unit (or the simulator) as the fader moves. The fader glides to where it is put (a time that can be
// set), which also keeps the unit from being flooded.
//
// Edits made anywhere (pages, the unit's own knobs) land at the position: on A, on B, or on both. With an end
// armed (A or B lit, as the Octatrack's scenes), they go into that end only, heard as far as the fader goes
// there; the pages then show that end. A kit or a sound that arrives whole (sent from the library, a program
// chosen on the unit) goes the same way: into the armed end, else at an end into that end, in between into both.
//
// Message thread, but for the host's automation and the project's state.
#pragma once

#include <JuceHeader.h>

#include "../core/RaMorph.h"
#include "Engine.h"

class MorphControl : public juce::ChangeBroadcaster,
                     private Engine::Listener,
                     private juce::Timer
{
public:
    using Side = ra::Morph::Side;

    explicit MorphControl (Engine&);
    ~MorphControl() override;

    double getPosition() const       { return morph.position(); }   // heard
    double getTarget() const         { return target; }             // where the fader is going
    bool differs() const             { return morph.differs(); }
    const ra::Morph& data() const    { return morph; }

    // the fader moved: it glides there (the screen, or the host's automation)
    void glideTo (double p);
    // the screen's fader: the host hears of it (its automation, its undo)
    std::function<void (double)> onUserPosition;
    std::function<void()> onGestureStart, onGestureEnd;
    void userMoved (double p)        { glideTo (p); if (onUserPosition) onUserPosition (target); }

    // the glide: the time the fader takes to get where it was put (0 = at once); remembered by the owner
    int getGlideMs() const           { return glideMs; }
    void setGlideMs (int ms);
    std::function<void (int)> onGlideChanged;

    // one end armed (A or B): edits go into it alone
    std::optional<Side> getArmed() const   { return armed; }
    void arm (std::optional<Side>);

    void loadKit (Side, const ra::Kit&);              // a kit of the library into A or B
    void loadSound (Side, int sound, const ra::Bytes&);
    void copyHeardTo (Side);                          // the kit heard becomes that end
    void swap();
    void flatten();                                   // A = B = the kit heard: no more morph

    // a morph preset (A, B and the position), as the library keeps it
    std::vector<std::uint8_t> preset() const   { return morph.save(); }
    bool loadPreset (const std::vector<std::uint8_t>&);

    // the host's automation (any thread): taken on the message thread
    void setFromHost (double p)      { hostPosition.store (p); }

    // the project's state (the plug-in's): the position, A and B; empty while A = B. Any thread. A project's
    // morph wins over the kit the unit has when the project opens: the unit gets the blend once its kit is read.
    juce::String toString();
    void fromString (const juce::String&);

private:
    void kitChanged (const ra::Kit::Change&) override;
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;
    void timerCallback() override;
    void setPosition (double p);                      // at once
    void apply();                                     // the unit's kit -> the blend
    void applySoon();                                 // now, or once the unit is done with what it does
    void sendHeard (bool effect, int index, int address);   // that one setting -> the blend
    bool takeEdit (bool effect, int index, int address, int value);
    void takeDumps();                                 // whole sounds or effects that arrived: into A, B or both
    const ra::Kit* shownKit();
    void changed();

    Engine& engine;
    ra::Morph morph;
    double target = 0.0, lastTick = 0.0;
    int glideMs = 150;
    std::optional<Side> armed;
    ra::Kit shown;                                    // the kit heard with the armed end's sounds and effects
    bool shownDirty = true;
    bool applying = false, reapply = false, dumped = false, restored = false;
    std::array<bool, ra::Kit::numSounds> dumpedSounds {};
    std::array<bool, ra::Kit::numEffects> dumpedEffects {};
    std::atomic<double> hostPosition { -1.0 };

    juce::SpinLock stateLock;
    juce::String state, incoming;
    bool stateDirty = true, hasIncoming = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphControl)
};
