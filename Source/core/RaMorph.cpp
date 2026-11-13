#include "RaMorph.h"

#include <algorithm>
#include <cmath>

#include "RaSysex.h"

namespace ra
{
namespace
{
    std::uint8_t clamp7 (double v)   { return (std::uint8_t) std::clamp ((int) std::lround (v), 0, 127); }

    // the lists, known once
    struct Lists
    {
        std::array<bool, kSoundDataSize> sound {};
        std::array<bool, kEffectDataSize> effect {};
        Lists()
        {
            // a setting with choices, or a byte the notes don't name: it switches, never takes a value in between
            for (int a = 0; a < kSoundDataSize; ++a)
                sound[(size_t) a] = parameterName (Object::sound, a) == nullptr || ! valueNames (Object::sound, a).empty();
            for (int a = 0; a < kEffectDataSize; ++a)
                effect[(size_t) a] = parameterName (Object::effect, a) == nullptr || ! valueNames (Object::effect, a).empty();
        }
    };
    const Lists& lists()   { static const Lists l; return l; }
}

bool Morph::isList (bool effect, int address)
{
    if (effect)
        return address < 0 || address >= kEffectDataSize || lists().effect[(size_t) address];
    return address < 0 || address >= kSoundDataSize || lists().sound[(size_t) address];
}

void Morph::fill (Ends& ends, const Kit& kit)
{
    for (int s = 0; s < Kit::numSounds; ++s)
        for (int i = 0; i < kSoundDataSize; ++i)
            ends.s[(size_t) s][(size_t) i] = clamp7 (kit.soundValue (s, i));
    for (int e = 0; e < Kit::numEffects; ++e)
        for (int i = 0; i < kEffectDataSize; ++i)
            ends.e[(size_t) e][(size_t) i] = clamp7 (kit.effectValue (e, i));
}

void Morph::reset (const Kit& kit)              { fill (a, kit); b = a; }
void Morph::load (Side side, const Kit& kit)
{
    fill (side == Side::a ? a : b, kit);
    for (int s = 0; s < Kit::numSounds; ++s)
        keepRouting (side, s);
}

void Morph::keepRouting (Side loaded, int s)
{
    auto& to = (loaded == Side::a ? a : b).s[(size_t) s];
    const auto& from = (loaded == Side::a ? b : a).s[(size_t) s];
    for (int i : { 0x58, 0x5A, 0x5B })
        if (i < kSoundDataSize)
            to[(size_t) i] = from[(size_t) i];
}
void Morph::swap()                               { std::swap (a, b); }
void Morph::setPosition (double p)               { pos = std::clamp (p, 0.0, 1.0); }

void Morph::loadSound (Side side, int s, const Bytes& data)
{
    if (s < 0 || s >= Kit::numSounds || (int) data.size() != kSoundDataSize)
        return;
    auto& to = (side == Side::a ? a : b).s[(size_t) s];
    for (int i = 0; i < kSoundDataSize; ++i)
        to[(size_t) i] = (std::uint8_t) (data[(size_t) i] & 0x7F);
    keepRouting (side, s);
}

void Morph::loadEffect (Side side, int e, const Bytes& data)
{
    if (e < 0 || e >= Kit::numEffects || (int) data.size() != kEffectDataSize)
        return;
    auto& to = (side == Side::a ? a : b).e[(size_t) e];
    for (int i = 0; i < kEffectDataSize; ++i)
        to[(size_t) i] = (std::uint8_t) (data[(size_t) i] & 0x7F);
}

void Morph::copyBlendTo (Side side)
{
    Ends heard;
    for (int s = 0; s < Kit::numSounds; ++s)
        for (int i = 0; i < kSoundDataSize; ++i)
            heard.s[(size_t) s][(size_t) i] = (std::uint8_t) sound (s, i);
    for (int e = 0; e < Kit::numEffects; ++e)
        for (int i = 0; i < kEffectDataSize; ++i)
            heard.e[(size_t) e][(size_t) i] = (std::uint8_t) effect (e, i);
    (side == Side::a ? a : b) = heard;
}

bool Morph::differs() const
{
    return a.s != b.s || a.e != b.e;
}

int Morph::blend (int va, int vb, bool list) const
{
    if (va == vb)
        return va;
    if (list)
        return pos < 0.5 ? va : vb;
    return std::clamp ((int) std::lround (va + (vb - va) * pos), 0, 127);
}

int Morph::sound (int s, int i) const
{
    if (s < 0 || s >= Kit::numSounds || i < 0 || i >= kSoundDataSize)
        return 0;
    return blend (a.s[(size_t) s][(size_t) i], b.s[(size_t) s][(size_t) i], isList (false, i));
}

int Morph::effect (int e, int i) const
{
    if (e < 0 || e >= Kit::numEffects || i < 0 || i >= kEffectDataSize)
        return 0;
    return blend (a.e[(size_t) e][(size_t) i], b.e[(size_t) e][(size_t) i], isList (true, i));
}

void Morph::edit (std::uint8_t& va, std::uint8_t& vb, bool list, int value) const
{
    value = std::clamp (value, 0, 127);
    if (value == blend (va, vb, list))
        return;                          // already heard (an echo, the same value again): nothing moves
    if (pos <= 0.0)  { va = (std::uint8_t) value; return; }
    if (pos >= 1.0)  { vb = (std::uint8_t) value; return; }
    if (list)
    {
        // the end being heard takes the choice
        (pos < 0.5 ? va : vb) = (std::uint8_t) value;
        return;
    }
    // both move, each in proportion to its weight, so that the blend moves by the change asked for:
    // dA = k (1 - p), dB = k p, with k = delta / ((1 - p)^2 + p^2)
    const double heard = va + (vb - va) * pos, delta = value - heard;
    const double k = delta / ((1.0 - pos) * (1.0 - pos) + pos * pos);
    va = clamp7 (va + k * (1.0 - pos));
    vb = clamp7 (vb + k * pos);
}

void Morph::editSound (int s, int i, int value)
{
    if (s < 0 || s >= Kit::numSounds || i < 0 || i >= kSoundDataSize)
        return;
    if (isKept (i))
        a.s[(size_t) s][(size_t) i] = b.s[(size_t) s][(size_t) i] = clamp7 (value);
    else
        edit (a.s[(size_t) s][(size_t) i], b.s[(size_t) s][(size_t) i], isList (false, i), value);
}

void Morph::editEffect (int e, int i, int value)
{
    if (e >= 0 && e < Kit::numEffects && i >= 0 && i < kEffectDataSize)
        edit (a.e[(size_t) e][(size_t) i], b.e[(size_t) e][(size_t) i], isList (true, i), value);
}

void Morph::setSoundEnd (Side side, int s, int i, int value)
{
    if (s < 0 || s >= Kit::numSounds || i < 0 || i >= kSoundDataSize)
        return;
    if (isKept (i))
        a.s[(size_t) s][(size_t) i] = b.s[(size_t) s][(size_t) i] = clamp7 (value);
    else
        (side == Side::a ? a : b).s[(size_t) s][(size_t) i] = clamp7 (value);
}

void Morph::setEffectEnd (Side side, int e, int i, int value)
{
    if (e >= 0 && e < Kit::numEffects && i >= 0 && i < kEffectDataSize)
        (side == Side::a ? a : b).e[(size_t) e][(size_t) i] = clamp7 (value);
}

void Morph::blendInto (Kit& kit) const
{
    for (int s = 0; s < Kit::numSounds; ++s)
        for (int i = 0; i < kSoundDataSize; ++i)
            if (kit.soundValue (s, i) != sound (s, i))
                kit.setSoundValue (s, i, sound (s, i));
    for (int e = 0; e < Kit::numEffects; ++e)
        for (int i = 0; i < kEffectDataSize; ++i)
            if (kit.effectValue (e, i) != effect (e, i))
                kit.setEffectValue (e, i, effect (e, i));
}

// "RAMORPH1", the position (0..1000), A, B
std::vector<std::uint8_t> Morph::save() const
{
    std::vector<std::uint8_t> out { 'R', 'A', 'M', 'O', 'R', 'P', 'H', '1' };
    const int p = (int) std::lround (pos * 1000.0);
    out.push_back ((std::uint8_t) (p >> 7));
    out.push_back ((std::uint8_t) (p & 0x7F));
    for (auto* ends : { &a, &b })
    {
        for (auto& s : ends->s) out.insert (out.end(), s.begin(), s.end());
        for (auto& e : ends->e) out.insert (out.end(), e.begin(), e.end());
    }
    return out;
}

bool Morph::restore (const std::vector<std::uint8_t>& data)
{
    const size_t perEnd = (size_t) Kit::numSounds * kSoundDataSize + (size_t) Kit::numEffects * kEffectDataSize;
    if (data.size() != 10 + 2 * perEnd || ! std::equal (data.begin(), data.begin() + 8, "RAMORPH1"))
        return false;
    setPosition ((data[8] * 128 + data[9]) / 1000.0);
    size_t at = 10;
    for (auto* ends : { &a, &b })
    {
        for (auto& s : ends->s) { std::copy_n (data.begin() + (long) at, s.size(), s.begin()); at += s.size(); }
        for (auto& e : ends->e) { std::copy_n (data.begin() + (long) at, e.size(), e.begin()); at += e.size(); }
    }
    return true;
}
}
