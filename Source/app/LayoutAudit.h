// The layout check: walks a window and lists what doesn't fit — text cut or squeezed
// in its label, button, tick box or list, a control that pokes out of its parent, two
// controls on top of each other — plus the text the pages paint themselves, which
// px::drawText reports while the window is painted once off screen.
// Run with RA_AUDIT=1 (or RA_AUDIT=<file> to also write the list there).

#pragma once

#include <JuceHeader.h>

#include "Pixel.h"
#include "Text.h"

namespace audit
{
    inline juce::String nameOf (juce::Component& c)
    {
        if (auto* b = dynamic_cast<juce::Button*> (&c))
            if (b->getButtonText().isNotEmpty())
                return "bouton « "_fr + b->getButtonText() + " »"_fr;
        if (auto* l = dynamic_cast<juce::Label*> (&c))
            return "texte « "_fr + l->getText().substring (0, 40).replace ("\n", " ") + " »"_fr;
        if (c.getName().isNotEmpty())
            return c.getName();
        juce::String type = typeid (c).name();
        return type.fromLastOccurrenceOf (" ", false, false).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_:");
    }

    inline float width (const juce::Font& f, const juce::String& t)
    {
        float w = 0.0f;
        for (auto& line : juce::StringArray::fromLines (t))
            w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (f, line));
        return w;
    }

    // need: the text's width; room: the width it has; lines: how many lines it may take
    inline void fit (juce::StringArray& out, juce::Component& c, float need, float room, int lines = 1)
    {
        if (room <= 0.0f || need <= room * (float) lines + 0.5f)
            return;
        const auto pct = juce::roundToInt (need / juce::jmax (1.0f, room * (float) lines) * 100.0f);
        out.add (nameOf (c) + (pct <= 125 ? " : texte serré ("_fr : " : texte coupé ("_fr) + juce::String (pct) + " % de la place)");
    }

    inline void walk (juce::Component& c, juce::StringArray& out)
    {
        if (! c.isVisible() || c.getWidth() <= 0 || c.getHeight() <= 0)
            return;

        auto& lf = c.getLookAndFeel();
        if (auto* l = dynamic_cast<juce::Label*> (&c))
        {
            if (! l->isBeingEdited() && l->getText().isNotEmpty())
            {
                const auto f = lf.getLabelFont (*l);
                const auto area = l->getBorderSize().subtractedFrom (l->getLocalBounds());
                const int lines = juce::jmax (1, (int) ((float) area.getHeight() / f.getHeight()));
                fit (out, c, width (f, l->getText()), (float) area.getWidth(), lines);
            }
        }
        else if (auto* t = dynamic_cast<juce::TextButton*> (&c))
        {
            const auto f = lf.getTextButtonFont (*t, t->getHeight() - 3);
            fit (out, c, width (f, t->getButtonText()), (float) t->getWidth() - 12.0f);
        }
        else if (auto* tb = dynamic_cast<juce::ToggleButton*> (&c))
        {
            const auto f = px::text (juce::jmin (24.0f, (float) tb->getHeight() * 0.85f));
            const int lines = tb->getHeight() >= (int) (f.getHeight() * 2.0f) ? 2 : 1;
            fit (out, c, width (f, tb->getButtonText()), (float) (tb->getWidth() - tb->getHeight() - 4), lines);
        }
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
        {
            const auto f = lf.getComboBoxFont (*cb);
            fit (out, c, width (f, cb->getText()), (float) cb->getWidth() - (float) cb->getHeight() - 6.0f);
        }

        // children: inside, and not on top of each other
        const bool scrolls = dynamic_cast<juce::Viewport*> (&c) != nullptr;
        juce::Array<juce::Component*> kids;
        for (auto* k : c.getChildren())
            if (k->isVisible() && k->getWidth() > 0 && k->getHeight() > 0)
                kids.add (k);
        for (auto* k : kids)
        {
            const bool inViewport = dynamic_cast<juce::Viewport*> (c.getParentComponent()) != nullptr;
            if (! scrolls && ! inViewport && ! c.getLocalBounds().contains (k->getBounds()))
                out.add (nameOf (*k) + " : dépasse de "_fr + nameOf (c) + " (" + k->getBounds().toString() + " dans "
                         + c.getLocalBounds().toString() + ")");
        }
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j)
            {
                const auto a = kids[i]->getBounds(), b = kids[j]->getBounds();
                const auto both = a.getIntersection (b);
                // an overlay over the whole parent (an assistant) is meant to cover the rest
                const bool overlay = a == c.getLocalBounds() || b == c.getLocalBounds()
                                  || (bool) kids[i]->getProperties()["overlay"] || (bool) kids[j]->getProperties()["overlay"];
                if (! overlay && both.getWidth() > 2 && both.getHeight() > 2)
                    out.add (nameOf (*kids[i]) + " et " + nameOf (*kids[j]) + " : l'un sur l'autre");
            }
        for (auto* k : kids)
            walk (*k, out);
    }

    // Lists the problems of what is shown now, the window painted once to catch the
    // painted text cut by px::drawText.
    inline juce::StringArray run (juce::Component& root)
    {
        juce::StringArray out;
        // only what can be seen: under an assistant, the pages don't count
        juce::Component* top = &root;
        for (auto* k : root.getChildren())
            if (k->isVisible() && k->getBounds() == root.getLocalBounds() && k->isOpaque() == false && dynamic_cast<juce::Viewport*> (k) == nullptr
                && k->getNumChildComponents() > 0 && root.getIndexOfChildComponent (k) == root.getNumChildComponents() - 1)
                top = k;
        walk (*top, out);

        juce::StringArray painted;
        px::cutTextHook = [&painted] (const juce::String& text, int need, int room)
        {
            painted.addIfNotAlreadyThere ("dessin « "_fr + text.substring (0, 40) + " » : texte coupé ("_fr
                                          + juce::String (juce::roundToInt ((float) need / (float) juce::jmax (1, room) * 100.0f)) + " % de la place)");
        };
        root.createComponentSnapshot (root.getLocalBounds(), true, 1.0f);
        px::cutTextHook = nullptr;
        out.addArray (painted);
        return out;
    }
}
