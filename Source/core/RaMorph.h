// Morph: two whole kits, A and B, and one fader between them. Every setting of
// the 24 sounds and of the 4 effects goes from its value in A to its value in B; a list (a shape, a source, a
// filter type...) switches half way. The kit's own settings (name, MIDI, trigger velocities) and the sequence
// don't morph, nor does a sound's MIDI routing (its tuned channel and key range): A and B always share it.
//
// Editing works at any position: at the ends only that kit changes; in between both move, more
// the nearer one, so that the kit heard lands on the value asked for. A sound or a kit loaded into A or B only
// changes that end.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "RaKit.h"

namespace ra
{
class Morph
{
public:
    enum class Side { a, b };

    // A and B both become this kit's sounds and effects (nothing morphs until they differ)
    void reset (const Kit& kit);
    void load (Side, const Kit& kit);                       // its sounds and effects
    void loadSound (Side, int sound, const Bytes& data);    // kSoundDataSize bytes
    void loadEffect (Side, int effect, const Bytes& data);  // kEffectDataSize bytes
    void copyBlendTo (Side);                                // the kit heard becomes that end
    void swap();

    double position() const          { return pos; }
    void setPosition (double p);
    bool differs() const;                                   // A and B are not the same

    // the value heard at the current position
    int sound (int s, int address) const;
    int effect (int e, int address) const;
    int end (Side side, int s, int address) const           { return side == Side::a ? a.s[(size_t) s][(size_t) address] : b.s[(size_t) s][(size_t) address]; }
    int effectEnd (Side side, int e, int address) const     { return side == Side::a ? a.e[(size_t) e][(size_t) address] : b.e[(size_t) e][(size_t) address]; }

    // an edit at the current position: A and B move so that the blend lands on `value` (as near as they can)
    void editSound (int s, int address, int value);
    void editEffect (int e, int address, int value);
    // an edit of one end only (that end armed, as the Octatrack's scenes): heard as far as the fader goes there
    void setSoundEnd (Side, int s, int address, int value);
    void setEffectEnd (Side, int e, int address, int value);

    // the blend written into a kit (its sounds and effects; the rest of it unchanged)
    void blendInto (Kit& kit) const;

    // a list switches half way rather than morphing (shapes, sources, types, routings...)
    static bool isList (bool effect, int address);
    // a sound's MIDI routing (tuned channel, low and high key): the same in A and B, whatever is loaded
    static bool isKept (int soundAddress)   { return soundAddress == 0x58 || soundAddress == 0x5A || soundAddress == 0x5B; }

    std::vector<std::uint8_t> save() const;
    bool restore (const std::vector<std::uint8_t>& data);

private:
    struct Ends
    {
        std::array<std::array<std::uint8_t, kSoundDataSize>, Kit::numSounds> s {};
        std::array<std::array<std::uint8_t, kEffectDataSize>, Kit::numEffects> e {};
    };
    Ends a, b;
    double pos = 0.0;

    int blend (int va, int vb, bool list) const;
    void edit (std::uint8_t& va, std::uint8_t& vb, bool list, int value) const;
    static void fill (Ends&, const Kit&);
    void keepRouting (Side loaded, int sound);       // the other end's routing back into the end just loaded
};
}
