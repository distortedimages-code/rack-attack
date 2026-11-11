// Look and small controls shared by the pages.

#pragma once

#include <JuceHeader.h>

#include "../core/RaSysex.h"
#include "Pixel.h"
#include "Text.h"

namespace ui
{
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
