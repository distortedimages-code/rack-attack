// Look and small controls shared by the pages.

#pragma once

#include <JuceHeader.h>

#include "../core/RaSysex.h"
#include "Pixel.h"
#include "Text.h"

namespace ui
{
    // The window is laid out once at this size, on a 4-pixel grid, and zoomed as a whole
    // in steps of 25 %: every 4-pixel block then covers a whole number of screen pixels.
    constexpr int designWidth = 1360, designHeight = 848;
    constexpr float zoomSteps[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    // The retro pixel palette (see Pixel.h): black, white, red for "live" and
    // problems, yellow-orange for locks and warnings.
    const juce::Colour background (0xff000000);
    const juce::Colour panel (0xff0a0a0a);
    const juce::Colour panelLight (0xff1c1c1c);
    const juce::Colour accent (0xffffffff);
    const juce::Colour text (0xffffffff);
    const juce::Colour dim (0xff8a8a8a);
    const juce::Colour warning (0xffffb000);
    const juce::Colour bad (0xffff3b2f);

    // VT323 runs small: its sizes are scaled so the old sizes stay readable.
    inline juce::Font font (float size, bool bold = false)
    {
        return bold ? px::label (size * 0.62f, true) : px::text (size * 1.4f);
    }

    inline juce::Font mono (float size)
    {
        return px::text (size * 1.45f);
    }

    inline juce::String twoDigits (int n)   { return juce::String (n).paddedLeft ('0', 2); }

    // A rounded card with an optional title.
    struct Card : public juce::Component
    {
        juce::String title;
        explicit Card (juce::String t = {}) : title (std::move (t)) {}

        void paint (juce::Graphics& g) override
        {
            g.setColour (panel);
            g.fillRect (getLocalBounds());
            px::frameRect (g, getLocalBounds(), px::frame, 2);
            if (title.isNotEmpty())
                px::drawText (g, title.toUpperCase(), { 12, 6, getWidth() - 24, 20 }, juce::Justification::centredLeft,
                              px::label (10.0f), px::grey);
        }

        int titleHeight() const   { return title.isNotEmpty() ? 28 : 8; }
    };

    // A 0..127 knob with its name above and its value below.
    struct Knob : public juce::Component
    {
        explicit Knob (const juce::String& name)
        {
            label.setText (name, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (font (13.0f));
            label.setColour (juce::Label::textColourId, px::grey);
            label.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (label);

            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setRange (0.0, 127.0, 1.0);
            slider.setMouseDragSensitivity (180);
            slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            slider.setColour (juce::Slider::thumbColourId, text);
            slider.onValueChange = [this]
            {
                showValue();
                if (onChange) onChange ((int) slider.getValue());
            };
            addAndMakeVisible (slider);

            value.setJustificationType (juce::Justification::centred);
            value.setFont (font (15.0f));
            addAndMakeVisible (value);
            showValue();
        }

        void setValue (int v)
        {
            slider.setValue (v, juce::dontSendNotification);
            showValue();
        }

        void showValue()   { value.setText (juce::String ((int) slider.getValue()), juce::dontSendNotification); }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (18));
            value.setBounds (r.removeFromBottom (20));
            slider.setBounds (r.reduced (2));
        }

        std::function<void (int)> onChange;
        juce::Label label, value;
        juce::Slider slider;
    };

    // A list of choices with its name above.
    struct Choice : public juce::Component
    {
        Choice (const juce::String& name, const std::vector<std::string>& choices)
        {
            label.setText (name, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (font (12.0f));
            label.setColour (juce::Label::textColourId, dim);
            label.setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (label);

            for (size_t i = 0; i < choices.size(); ++i)
                box.addItem (juce::String::fromUTF8 (choices[i].c_str()), (int) i + 1);
            box.onChange = [this] { if (onChange && box.getSelectedId() > 0) onChange (box.getSelectedId() - 1); };
            addAndMakeVisible (box);
        }

        void setValue (int v)
        {
            if (v >= 0 && v < box.getNumItems())
                box.setSelectedId (v + 1, juce::dontSendNotification);
            else
                box.setText (juce::String (v), juce::dontSendNotification);   // a value outside the known list
        }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (16));
            box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 26));
        }

        std::function<void (int)> onChange;
        juce::Label label;
        juce::ComboBox box;
    };

    // A message line that can say "ok", "careful" or "problem".
    struct StatusLine : public juce::Label
    {
        StatusLine()
        {
            setFont (ui::font (15.0f));
            setJustificationType (juce::Justification::centredLeft);
            setMinimumHorizontalScale (0.8f);
        }

        void show (const juce::String& t, juce::Colour c = text)
        {
            setColour (juce::Label::textColourId, c);
            setText (t, juce::dontSendNotification);
        }
    };

    // A name shortened to fit a narrow box: common abbreviations first, then cut.
    inline juce::String fitName (const juce::String& name, const juce::Font& f, int room)
    {
        auto w = [&f] (const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); };
        if (w (name) <= (float) room)
            return name;
        static const char* const pairs[][2] = {
            { "Stereo ", "St " }, { "Mono ", "M " }, { "Velocity", "Velo" }, { "Modwheel", "MWheel" }, { "Pressure", "Press" },
            { "Keytrack", "KeyTr" }, { "Triangle", "Tri" }, { "HH Closed", "HH Cl" }, { "HH Open", "HH Op" },
            { "One Shot", "1Shot" }, { "Ext Mono", "Ext M" }, { "Random", "Rand" }, { "Global", "Glob" }, { "Bypass", "Byp" },
        };
        auto t = name;
        for (auto& p : pairs)
            t = t.replace (p[0], p[1]);
        while (t.isNotEmpty() && w (t) > (float) room)
            t = t.dropLastCharacters (1);
        return t;
    }

    // The Son page's cells, drawn in pixels like the main page. A name, then a knob and
    // its value (drag up or down, Shift for fine steps, double-click: the middle), or a
    // list (click), and for a modulation its source under it.
    struct Cell : public juce::Component
    {
        Cell (const juce::String& n, std::vector<std::string> list = {}) : name (n)
        {
            for (auto& c : list)
                choices.add (juce::String::fromUTF8 (c.c_str()));
        }

        void addSource (std::vector<std::string> names)
        {
            for (auto& c : names)
                sources.add (juce::String::fromUTF8 (c.c_str()));
            hasSource = true;
        }

        void setValue (int v)       { if (v != value) { value = v; repaint(); } }
        void setSource (int v)      { if (v != source) { source = v; repaint(); } }

        std::function<void (int)> onChange, onSource;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (2);
            const bool hot = isMouseOver (true) || dragging;
            const auto nameFont = px::text (15.0f);
            px::drawText (g, fitName (name, nameFont, r.getWidth()), r.removeFromTop (14), juce::Justification::centred,
                          nameFont, hot ? px::white : px::grey);
            if (hasSource)
            {
                sourceBox = r.removeFromBottom (20);
                drawBox (g, sourceBox, sources[source], true);
                r.removeFromBottom (2);
            }
            if (! choices.isEmpty())
            {
                listBox = r.withSizeKeepingCentre (r.getWidth(), 24);
                drawBox (g, listBox, choices[value].isNotEmpty() ? choices[value] : juce::String (value), false);
                return;
            }
            const auto valueFont = px::text (22.0f);
            auto valueRow = r.removeFromBottom (18);
            auto knobArea = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
            px::knob (g, knobArea, (float) value / 127.0f, hot ? px::white : juce::Colour (0xffd8d8d8), knobArea.getWidth() >= 44 ? 3 : 2);
            px::drawText (g, juce::String (value), valueRow, juce::Justification::centred, valueFont, px::white);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (hasSource && sourceBox.contains (e.getPosition()))
            {
                showMenu (sources, source, sourceBox, onSource);
                return;
            }
            if (! choices.isEmpty())
            {
                showMenu (choices, value, listBox, onChange);
                return;
            }
            dragging = true;
            startValue = value;
            startY = e.getPosition().y;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging)
                return;
            const float perPixel = e.mods.isShiftDown() ? 0.15f : 0.6f;
            const int v = juce::jlimit (0, 127, startValue + juce::roundToInt ((float) (startY - e.getPosition().y) * perPixel));
            if (v != value)
            {
                value = v;
                if (onChange) onChange (v);
                repaint();
            }
        }

        void mouseUp (const juce::MouseEvent&) override       { dragging = false; repaint(); }
        void mouseEnter (const juce::MouseEvent&) override    { repaint(); }
        void mouseExit (const juce::MouseEvent&) override     { repaint(); }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (choices.isEmpty() && ! (hasSource && sourceBox.contains (e.getPosition())))
            {
                value = 64;
                if (onChange) onChange (value);
                repaint();
            }
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (! choices.isEmpty())
                return;
            const int v = juce::jlimit (0, 127, value + (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0));
            if (v != value)
            {
                value = v;
                if (onChange) onChange (v);
                repaint();
            }
        }

        juce::String name;

    private:
        void drawBox (juce::Graphics& g, juce::Rectangle<int> b, const juce::String& shown, bool small)
        {
            g.setColour (px::cell);
            g.fillRect (b);
            px::frameRect (g, b, px::frame, 2);
            const auto f = px::text (small ? 16.0f : 18.0f);
            auto t = b.reduced (5, 0);
            g.setColour (px::dim);
            g.fillRect (t.getRight() - 6, t.getCentreY() - 1, 6, 2);
            g.fillRect (t.getRight() - 4, t.getCentreY() + 1, 2, 2);
            t.removeFromRight (8);
            px::drawText (g, fitName (shown, f, t.getWidth()), t, juce::Justification::centredLeft, f, px::white);
        }

        void showMenu (const juce::StringArray& items, int current, juce::Rectangle<int> b, std::function<void (int)> then)
        {
            juce::PopupMenu m;
            for (int i = 0; i < items.size(); ++i)
                m.addItem (i + 1, items[i], true, i == current);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (b)),
                             [safe = juce::Component::SafePointer<Cell> (this), then, isSource = &items == &sources] (int r)
            {
                if (safe == nullptr || r <= 0)
                    return;
                if (isSource) safe->source = r - 1;
                else safe->value = r - 1;
                if (then) then (r - 1);
                safe->repaint();
            });
        }

        juce::StringArray choices, sources;
        bool hasSource = false, dragging = false;
        int value = 0, source = 0, startValue = 0, startY = 0;
        juce::Rectangle<int> sourceBox, listBox;
    };

    inline void styleBig (juce::TextButton& b, bool primary = false)
    {
        // a primary button looks lit (white, black text)
        b.getProperties().set ("primary", primary);
        if (primary)
            b.setColour (juce::TextButton::textColourOffId, px::black);
        else
            b.removeColour (juce::TextButton::textColourOffId);
        b.repaint();
    }
}
