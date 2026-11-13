// The morph fader, in the bottom bar of every page: MORPH, A, the fader, B.
// Drag it (⌘ or Ctrl: finer), double-click: half way; it glides there (the red mark: what is heard). A click on
// A or B arms that end (lit yellow, as the Octatrack's scenes): what is turned then goes into it. Right click:
// the ends, the kit heard into A or B, A and B swapped, no more morph, the glide time. Kits, sounds and morphs
// go into A or B from the library (right click).
#pragma once

#include <JuceHeader.h>

#include "MorphControl.h"
#include "Pixel.h"
#include "Text.h"

class MorphFader : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    explicit MorphFader (MorphControl& m) : morph (m)
    {
        morph.addChangeListener (this);
        setTooltip ("Morph : du kit A au kit B. Glisse (⌘ ou Ctrl : plus fin), double-clic : au milieu. "
                    "Clic sur A ou B : ce que tu tournes va dans ce côté seulement (re-clic pour sortir). "
                    "Clic droit : aller à A ou B, glide… Pour charger un kit dans A ou B : clic droit dans la Bibliothèque."_fr);
    }

    ~MorphFader() override   { morph.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        layout();
        const bool on = morph.differs();
        const double p = morph.getPosition(), t = morph.getTarget();
        const auto armed = morph.getArmed();
        px::drawText (g, "MORPH", nameBox, juce::Justification::centredLeft, px::label (11.0f), armed ? px::yellow : (on ? px::white : px::grey));

        // an end: yellow when armed, white when heard, else a frame
        for (auto [box, label, side, atEnd] : { std::tuple { aBox, "A", MorphControl::Side::a, p <= 0.0 },
                                                std::tuple { bBox, "B", MorphControl::Side::b, p >= 1.0 } })
        {
            const bool lit = armed == side;
            g.setColour (lit ? px::yellow : (atEnd ? px::white : px::black));
            g.fillRect (box);
            if (! lit && ! atEnd)
                px::frameRect (g, box, on ? px::grey : px::frame, 2);
            px::drawText (g, label, box, juce::Justification::centred, px::label (12.0f),
                          lit || atEnd ? px::black : (on ? px::white : px::grey));
        }

        // the track: dots, the part from A filled, a mark in the middle, the knob
        const int cy = track.getCentreY();
        g.setColour (px::faint);
        for (int x = track.getX(); x < track.getRight(); x += 4)
            g.fillRect (x, cy - 1, 2, 2);
        g.fillRect (track.getCentreX() - 1, cy - 6, 2, 12);
        const int hx = track.getX() + juce::roundToInt (p * track.getWidth());     // heard
        const int kx = track.getX() + juce::roundToInt (t * track.getWidth());     // where it goes
        if (on)
        {
            g.setColour (px::white);
            g.fillRect (track.getX(), cy - 1, hx - track.getX(), 2);
        }
        auto knob = juce::Rectangle<int> (10, getHeight() - 4).withCentre ({ kx, getHeight() / 2 });
        g.setColour (dragging ? px::white : (on ? px::white : px::grey));
        g.fillRect (knob);
        if (dragging)
            px::frameRect (g, knob, px::black, 2);
        if (std::abs (hx - kx) > 1)
        {
            g.setColour (px::red);       // the glide: what is heard on its way
            g.fillRect (hx - 1, 2, 2, getHeight() - 4);
        }
        if (! on)
            px::drawText (g, "A = B", track.withTrimmedLeft (track.getWidth() / 2 + 8), juce::Justification::centredLeft, px::label (9.0f), px::dim);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        layout();
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        // A or B: armed, or no more
        for (auto [box, side] : { std::pair { aBox, MorphControl::Side::a }, std::pair { bBox, MorphControl::Side::b } })
            if (box.contains (e.getPosition()))
            {
                morph.arm (morph.getArmed() == side ? std::optional<MorphControl::Side>() : std::optional<MorphControl::Side> (side));
                return;
            }
        dragging = true;
        startX = e.getPosition().x;
        startPos = morph.getTarget();
        if (morph.onGestureStart) morph.onGestureStart();
        if (! (e.mods.isCommandDown() || e.mods.isCtrlDown()))
            morph.userMoved (positionAt (e.getPosition().x));    // a click on the track: there
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            morph.userMoved (startPos + (e.getPosition().x - startX) / (double) juce::jmax (1, track.getWidth()) * 0.25);
        else
            morph.userMoved (positionAt (e.getPosition().x));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && morph.onGestureEnd) morph.onGestureEnd();
        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        layout();
        if (track.expanded (6, 20).contains (e.getPosition()) && ! aBox.contains (e.getPosition()) && ! bBox.contains (e.getPosition()))
            gesture ([this] { morph.userMoved (0.5); });
    }

    // the times of the glide in the menu
    static constexpr std::array<int, 7> glideTimes { 0, 50, 150, 300, 600, 1000, 2000 };
    static juce::String glideText (int ms)
    {
        if (ms <= 0) return "Sans glide"_fr;
        if (ms < 1000) return "Glide " + juce::String (ms) + " ms";
        return "Glide " + juce::String (ms / 1000.0, 1).replace (".", ",") + " s";
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { repaint(); }

    void layout()
    {
        auto r = getLocalBounds();
        nameBox = r.removeFromLeft (64);
        aBox = r.removeFromLeft (r.getHeight());
        bBox = r.removeFromRight (r.getHeight());
        track = r.reduced (12, 0);
    }

    double positionAt (int x) const
    {
        return juce::jlimit (0.0, 1.0, (x - track.getX()) / (double) juce::jmax (1, track.getWidth()));
    }

    void gesture (std::function<void()> fn)
    {
        if (morph.onGestureStart) morph.onGestureStart();
        fn();
        if (morph.onGestureEnd) morph.onGestureEnd();
    }

    void showMenu()
    {
        juce::PopupMenu m, glide;
        const auto armed = morph.getArmed();
        m.addSectionHeader ("MORPH");
        m.addItem (5, "Aller à A"_fr);
        m.addItem (6, "Au milieu"_fr);
        m.addItem (7, "Aller à B"_fr);
        m.addSeparator();
        m.addItem (9, "Éditer A seulement"_fr, true, armed == MorphControl::Side::a);
        m.addItem (10, "Éditer B seulement"_fr, true, armed == MorphControl::Side::b);
        m.addSeparator();
        m.addItem (1, "Le kit entendu dans A"_fr);
        m.addItem (2, "Le kit entendu dans B"_fr);
        m.addItem (3, "Échanger A et B"_fr, morph.differs());
        m.addItem (4, "A = B : plus de morph"_fr, morph.differs());
        m.addSeparator();
        for (size_t i = 0; i < glideTimes.size(); ++i)
            glide.addItem (100 + (int) i, glideText (glideTimes[i]), true, morph.getGlideMs() == glideTimes[i]);
        m.addSubMenu (glideText (morph.getGlideMs()), glide);
        m.addSeparator();
        m.addItem (8, "Charger un kit dans A ou B, enregistrer le morph : Bibliothèque"_fr, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<MorphFader> (this)] (int r)
        {
            if (safe == nullptr) return;
            auto& mc = safe->morph;
            switch (r)
            {
                case 1: mc.copyHeardTo (MorphControl::Side::a); break;
                case 2: mc.copyHeardTo (MorphControl::Side::b); break;
                case 3: mc.swap(); break;
                case 4: mc.flatten(); break;
                case 5: safe->gesture ([&mc] { mc.userMoved (0.0); }); break;
                case 6: safe->gesture ([&mc] { mc.userMoved (0.5); }); break;
                case 7: safe->gesture ([&mc] { mc.userMoved (1.0); }); break;
                case 9: mc.arm (mc.getArmed() == MorphControl::Side::a ? std::optional<MorphControl::Side>() : MorphControl::Side::a); break;
                case 10: mc.arm (mc.getArmed() == MorphControl::Side::b ? std::optional<MorphControl::Side>() : MorphControl::Side::b); break;
                default:
                    if (r >= 100 && r < 100 + (int) glideTimes.size())
                        mc.setGlideMs (glideTimes[(size_t) (r - 100)]);
                    break;
            }
        });
    }

    MorphControl& morph;
    juce::Rectangle<int> nameBox, aBox, bBox, track;
    bool dragging = false;
    int startX = 0;
    double startPos = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphFader)
};
