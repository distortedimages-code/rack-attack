// The retro pixel look: black, white, red (live), yellow-orange (locks), dithering,
// two pixel fonts (Silkscreen for labels, VT323 for values and text), square
// buttons with a bevel, and a small mascot. Also the LookAndFeel that gives the
// other pages the same look.

#pragma once

#include <JuceHeader.h>

namespace px
{
    const juce::Colour black (0xff000000);
    const juce::Colour panel (0xff0a0a0a);
    const juce::Colour cell (0xff121212);
    const juce::Colour frame (0xff2a2a2a);
    const juce::Colour faint (0xff3a3a3a);
    const juce::Colour dim (0xff6f6f6f);
    const juce::Colour grey (0xff9a9a9a);
    const juce::Colour white (0xffffffff);
    const juce::Colour red (0xffff3b2f);      // live: playhead, keyboard, play, mute
    const juce::Colour yellow (0xffffb000);   // p-locks

    juce::Font label (float height, bool bold = false);   // Silkscreen, capitals
    juce::Font text (float height);                       // VT323

    // Dithered fill: level 1 = sparse dots (6 %), 2 = 25 %, 3 = checkerboard, 4 = solid.
    // The pattern is anchored on the component, so neighbours line up.
    void dither (juce::Graphics&, juce::Rectangle<int>, juce::Colour, int level);

    // A square frame drawn inside the rectangle.
    void frameRect (juce::Graphics&, juce::Rectangle<int>, juce::Colour, int thickness = 2);

    // A button face: bevel at the bottom when up, pushed down when pressed.
    void button (juce::Graphics&, juce::Rectangle<int>, bool on, bool pressed, juce::Colour accent = white);

    // A pixel bar of `blocks` blocks, `filled` of them lit, an optional marker block.
    void bar (juce::Graphics&, juce::Rectangle<int>, int blocks, int filled, juce::Colour on,
              int marker = -1, juce::Colour off = juce::Colour (0xff1c1c1c));

    // The same for an amount that goes both ways: lit from the middle to the value
    // (position 0..1, 0.5 = the middle, shown by a brighter notch).
    void barCentred (juce::Graphics&, juce::Rectangle<int>, int blocks, float position, juce::Colour on,
                     int marker = -1, juce::Colour off = juce::Colour (0xff1c1c1c));

    // A round knob drawn in square pixels, as on Elektron screens: a lit arc up to the
    // value, the knob's circle and a pointer. `marker` (0..1, < 0 = none) shows a
    // second position on the arc (the sound's own value under a lock).
    void knob (juce::Graphics&, juce::Rectangle<int> area, float position, juce::Colour on,
               int pixel = 3, float marker = -1.0f, juce::Colour body = juce::Colour (0xff6f6f6f), bool centred = false);

    // The mascot, 10 x 8 pixels of `size`; `blink` closes its eyes.
    void mascot (juce::Graphics&, juce::Point<int> topLeft, int size, bool blink);

    // Text that is cut to fit, without the "..." of JUCE's drawFittedText.
    void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Justification,
                   const juce::Font&, juce::Colour);

    // The layout check listens here: text drawText had to cut (text, width needed, width it had).
    extern std::function<void (const juce::String&, int, int)> cutTextHook;

    //==========================================================================
    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowTitleFont() override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String&) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        // menus (right click, OPTIONS, combo boxes): black, a white frame, white bars for the item under
        // the mouse, section titles in an inverted band like the cards, pixel ticks and arrows
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                        int& idealWidth, int& idealHeight) override;
        void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height, bool isScrollUpArrow) override;
        int getPopupMenuBorderSize() override                    { return 4; }
        int getMenuWindowFlags() override                        { return 0; }     // flat: no soft shadow
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool down) override;
    };
}
