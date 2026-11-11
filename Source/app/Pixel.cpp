#include "Pixel.h"

#include <BinaryData.h>

#include <set>

namespace px
{
namespace
{
    juce::Typeface::Ptr silk()
    {
        static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::SilkscreenRegular_ttf, BinaryData::SilkscreenRegular_ttfSize);
        return t;
    }

    juce::Typeface::Ptr silkBold()
    {
        static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::SilkscreenBold_ttf, BinaryData::SilkscreenBold_ttfSize);
        return t;
    }

    juce::Typeface::Ptr vt()
    {
        static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
        return t;
    }

    // Tiles of 2-pixel dots: 1 = one dot per 8x8, 2 = one per 4x4, 3 = checkerboard.
    juce::Image makeTile (juce::Colour c, int level)
    {
        const int size = level == 1 ? 8 : 4;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        g.setColour (c);
        g.fillRect (0, 0, 2, 2);
        if (level == 3)
            g.fillRect (2, 2, 2, 2);
        return img;
    }

    const juce::Image& tile (juce::Colour c, int level)
    {
        static std::map<std::pair<juce::uint32, int>, juce::Image> cache;
        auto key = std::make_pair (c.getARGB(), level);
        auto it = cache.find (key);
        if (it == cache.end())
            it = cache.emplace (key, makeTile (c, level)).first;
        return it->second;
    }
}

juce::Font label (float height, bool bold)
{
    // Silkscreen's capitals are small for its height: scaled so sizes match VT323's
    return juce::Font (juce::FontOptions (bold ? silkBold() : silk()).withHeight (height * 1.6f));
}

juce::Font text (float height)
{
    return juce::Font (juce::FontOptions (vt()).withHeight (height));
}

void dither (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c, int level)
{
    if (level <= 0 || r.isEmpty())
        return;
    if (level >= 4)
    {
        g.setColour (c);
        g.fillRect (r);
        return;
    }
    juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.setTiledImageFill (tile (c, level), 0, 0, 1.0f);
    g.fillRect (r);
}

void frameRect (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c, int t)
{
    g.setColour (c);
    g.fillRect (r.removeFromTop (t));
    g.fillRect (r.removeFromBottom (t));
    g.fillRect (r.removeFromLeft (t));
    g.fillRect (r.removeFromRight (t));
}

void button (juce::Graphics& g, juce::Rectangle<int> r, bool on, bool pressed, juce::Colour accent)
{
    if (pressed)
        r.translate (0, 3);
    if (on)
    {
        g.setColour (accent);
        g.fillRect (r);
        if (! pressed)
        {
            g.setColour (accent.darker (0.45f));
            g.fillRect (r.removeFromBottom (4));
        }
    }
    else
    {
        g.setColour (cell);
        g.fillRect (r);
        frameRect (g, r, pressed ? grey : frame, 2);
    }
}

void bar (juce::Graphics& g, juce::Rectangle<int> r, int blocks, int filled, juce::Colour on, int marker, juce::Colour off)
{
    if (blocks <= 0)
        return;
    const int gap = r.getWidth() / blocks > 6 ? 3 : 2;
    const float w = (float) (r.getWidth() + gap) / (float) blocks;
    for (int b = 0; b < blocks; ++b)
    {
        const int x0 = r.getX() + juce::roundToInt ((float) b * w);
        const int x1 = r.getX() + juce::roundToInt ((float) (b + 1) * w) - gap;
        juce::Rectangle<int> block (x0, r.getY(), juce::jmax (1, x1 - x0), r.getHeight());
        if (b < filled)
        {
            g.setColour (on);
            g.fillRect (block);
        }
        else
        {
            g.setColour (off);
            g.fillRect (block);
            if (b == marker)
                dither (g, block, white, 3);
        }
    }
}

void barCentred (juce::Graphics& g, juce::Rectangle<int> r, int blocks, float position, juce::Colour on, int marker, juce::Colour off)
{
    if (blocks <= 0)
        return;
    const int gap = r.getWidth() / blocks > 6 ? 3 : 2;
    const float w = (float) (r.getWidth() + gap) / (float) blocks;
    const float mid = (float) blocks * 0.5f, at = juce::jlimit (0.0f, 1.0f, position) * (float) blocks;
    for (int b = 0; b < blocks; ++b)
    {
        const int x0 = r.getX() + juce::roundToInt ((float) b * w);
        const int x1 = r.getX() + juce::roundToInt ((float) (b + 1) * w) - gap;
        juce::Rectangle<int> block (x0, r.getY(), juce::jmax (1, x1 - x0), r.getHeight());
        const float c = (float) b + 0.5f;
        const bool lit = (c >= mid && c < at) || (c < mid && c >= at);
        g.setColour (lit ? on : (b == marker ? grey : off));
        g.fillRect (block);
    }
    // the middle
    const int mx = r.getX() + juce::roundToInt (mid * w) - gap / 2 - 1;
    g.setColour (grey);
    g.fillRect (mx, r.getY() - 2, 2, r.getHeight() + 4);
}

void knob (juce::Graphics& g, juce::Rectangle<int> area, float pos, juce::Colour on, int p, float marker, juce::Colour, bool centred)
{
    const int size = juce::jmin (area.getWidth(), area.getHeight());
    if (size < 12)
        return;
    pos = juce::jlimit (0.0f, 1.0f, pos);
    // one ring of p-sized squares, open at the bottom, lit up to the value
    const auto c = area.getCentre();
    const float r = (float) size * 0.5f - (float) p * 0.5f;
    const float a0 = -0.75f * juce::MathConstants<float>::pi, a1 = 0.75f * juce::MathConstants<float>::pi;

    std::set<std::pair<int, int>> done;
    auto put = [&] (float x, float y, juce::Colour col, bool replace = false)
    {
        const int gx = juce::roundToInt (x / (float) p), gy = juce::roundToInt (y / (float) p);
        if (! done.insert ({ gx, gy }).second && ! replace)
            return;
        g.setColour (col);
        g.fillRect (c.x + gx * p - p / 2, c.y + gy * p - p / 2, p, p);
    };

    const int steps = juce::jmax (16, juce::roundToInt (r * 1.5f * juce::MathConstants<float>::pi / (float) p * 1.3f));
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float a = a0 + (a1 - a0) * t;
        const bool lit = centred ? ((t >= 0.5f - 1e-4f && t <= pos + 1e-4f) || (t <= 0.5f + 1e-4f && t >= pos - 1e-4f))
                                 : t <= pos + 1e-4f;
        put (r * std::sin (a), -r * std::cos (a), lit ? on : faint);
    }
    if (centred)
        put (0.0f, -r - (float) p, grey, true);          // the middle, marked above the ring

    // where it stands: one square just inside the ring
    const float av = a0 + (a1 - a0) * pos;
    const float ri = r - 2.0f * (float) p;
    put (ri * std::sin (av), -ri * std::cos (av), on, true);

    // the sound's own value under a lock: a square just outside
    if (marker >= 0.0f)
    {
        const float a = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, marker);
        put ((r + (float) p) * std::sin (a), -(r + (float) p) * std::cos (a), white, true);
    }
}

void mascot (juce::Graphics& g, juce::Point<int> p, int s, bool blink)
{
    static const char* art[8] = {
        "...XXXX...",
        ".XXXXXXXX.",
        "XXXXXXXXXX",
        "XX.XXXX.XX",
        "XXXXXXXXXX",
        "XXXRRRRXXX",
        ".XXXXXXXX.",
        ".X.X..X.X.",
    };
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 10; ++x)
        {
            char ch = art[y][x];
            if (blink && y == 3 && (x == 2 || x == 7))
                ch = 'X';
            if (ch == '.')
                continue;
            g.setColour (ch == 'R' ? red : white);
            g.fillRect (p.x + x * s, p.y + y * s, s, s);
        }
}

std::function<void (const juce::String&, int, int)> cutTextHook;

void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j,
               const juce::Font& f, juce::Colour c)
{
    g.setFont (f);
    g.setColour (c);
    if (cutTextHook != nullptr && t.trim().isNotEmpty())
    {
        const float need = juce::GlyphArrangement::getStringWidth (f, t);
        if (need > (float) r.getWidth() + 0.5f)
            cutTextHook (t, juce::roundToInt (need), r.getWidth());
    }
    auto s = t;
    while (s.isNotEmpty() && juce::GlyphArrangement::getStringWidth (f, s) > (float) r.getWidth())
        s = s.dropLastCharacters (1);
    g.drawText (s, r, j, false);
}

//==============================================================================
Look::Look()
{
    setColourScheme (getDarkColourScheme());
    setDefaultSansSerifTypeface (vt());

    setColour (juce::ResizableWindow::backgroundColourId, black);
    setColour (juce::DocumentWindow::backgroundColourId, black);
    setColour (juce::TextButton::buttonColourId, cell);
    setColour (juce::TextButton::buttonOnColourId, white);
    setColour (juce::TextButton::textColourOffId, white);
    setColour (juce::TextButton::textColourOnId, black);
    setColour (juce::TextEditor::backgroundColourId, cell);
    setColour (juce::TextEditor::textColourId, white);
    setColour (juce::TextEditor::highlightColourId, yellow.withAlpha (0.5f));
    setColour (juce::TextEditor::outlineColourId, frame);
    setColour (juce::TextEditor::focusedOutlineColourId, white);
    setColour (juce::CaretComponent::caretColourId, yellow);
    setColour (juce::Label::textColourId, white);
    setColour (juce::Slider::thumbColourId, white);
    setColour (juce::Slider::rotarySliderFillColourId, white);
    setColour (juce::Slider::trackColourId, white);
    setColour (juce::ToggleButton::tickColourId, white);
    setColour (juce::ToggleButton::textColourId, white);
    setColour (juce::ComboBox::backgroundColourId, cell);
    setColour (juce::ComboBox::outlineColourId, frame);
    setColour (juce::ComboBox::textColourId, white);
    setColour (juce::ComboBox::arrowColourId, white);
    setColour (juce::PopupMenu::backgroundColourId, black);
    setColour (juce::PopupMenu::textColourId, white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, white);
    setColour (juce::PopupMenu::highlightedTextColourId, black);
    setColour (juce::ListBox::backgroundColourId, cell);
    setColour (juce::ListBox::textColourId, white);
    setColour (juce::ProgressBar::foregroundColourId, white);
    setColour (juce::ProgressBar::backgroundColourId, cell);
    setColour (juce::AlertWindow::backgroundColourId, black);
    setColour (juce::AlertWindow::textColourId, white);
    setColour (juce::AlertWindow::outlineColourId, white);
    setColour (juce::ScrollBar::thumbColourId, faint);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefacePtr() != nullptr)
        return f.getTypefacePtr();
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
        || f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return vt();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int h)          { return text (juce::jmin (26.0f, (float) h * 0.72f)); }
juce::Font Look::getLabelFont (juce::Label& l)                          { return l.getFont(); }
juce::Font Look::getComboBoxFont (juce::ComboBox& b)                    { return text (juce::jmin (24.0f, (float) b.getHeight() * 0.8f)); }
juce::Font Look::getPopupMenuFont()                                     { return text (22.0f); }
juce::Font Look::getAlertWindowMessageFont()                            { return text (22.0f); }
juce::Font Look::getAlertWindowTitleFont()                              { return label (13.0f, true); }

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds();
    const bool on = b.getToggleState() || (bool) b.getProperties()["primary"];
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    if (! b.isEnabled())
    {
        g.setColour (cell);
        g.fillRect (r);
        dither (g, r.reduced (2), frame, 1);
        frameRect (g, r, frame, 2);
        return;
    }
    button (g, r.withTrimmedBottom (3), on, down, accent);
    if (highlighted && ! on && ! down)
        frameRect (g, r.withTrimmedBottom (3), grey, 2);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds().withTrimmedBottom (3);
    if (down)
        r.translate (0, 3);
    const bool on = b.getToggleState() || (bool) b.getProperties()["primary"];
    auto c = on ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId);
    if (! b.isEnabled())
        c = dim;
    g.setFont (getTextButtonFont (b, r.getHeight()));
    g.setColour (c);
    g.drawFittedText (b.getButtonText(), r.reduced (6, 0).withTrimmedBottom (on && ! down ? 4 : 0), juce::Justification::centred, 1, 0.8f);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds();
    auto box = r.removeFromLeft (r.getHeight()).reduced (4);
    box = box.withSizeKeepingCentre (juce::jmin (box.getWidth(), 18), juce::jmin (box.getHeight(), 18));
    g.setColour (cell);
    g.fillRect (box);
    frameRect (g, box, highlighted ? white : grey, 2);
    if (b.getToggleState())
    {
        g.setColour (white);
        g.fillRect (box.reduced (5));
    }
    g.setColour (b.isEnabled() ? white : dim);
    g.setFont (text (juce::jmin (24.0f, (float) r.getHeight() * 0.85f)));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (4), juce::Justification::centredLeft, 2, 0.8f);
}

void Look::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    juce::Rectangle<int> r (0, 0, width, height);
    g.setColour (cell);
    g.fillRect (r);
    frameRect (g, r, box.hasKeyboardFocus (true) ? white : frame, 2);
    // a small pixel arrow
    const int s = 2, ax = width - 18, ay = height / 2 - 2;
    g.setColour (box.isEnabled() ? white : dim);
    g.fillRect (ax, ay, 5 * s, s);
    g.fillRect (ax + s, ay + s, 3 * s, s);
    g.fillRect (ax + 2 * s, ay + 2 * s, s, s);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (4, 1, box.getWidth() - 26, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                             juce::Slider& s)
{
    const int size = juce::jmin (width, height);
    knob (g, { x, y, width, height }, pos, s.findColour (juce::Slider::rotarySliderFillColourId), size >= 56 ? 3 : 2);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                             juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0, 0, style, s);
        return;
    }
    juce::Rectangle<int> r (x, y + height / 2 - 6, width, 12);
    const int blocks = juce::jlimit (8, 32, width / 12);
    const float frac = (pos - (float) x) / (float) juce::jmax (1, width);
    bar (g, r, blocks, juce::roundToInt (frac * (float) blocks), s.findColour (juce::Slider::trackColourId));
}

void Look::drawProgressBar (juce::Graphics& g, juce::ProgressBar& b, int width, int height, double progress, const juce::String& t)
{
    juce::Rectangle<int> r (0, 0, width, height);
    g.setColour (cell);
    g.fillRect (r);
    frameRect (g, r, frame, 2);
    if (progress >= 0.0 && progress <= 1.0)
        dither (g, r.reduced (3).withWidth (juce::roundToInt ((width - 6) * progress)), b.findColour (juce::ProgressBar::foregroundColourId), 3);
    if (t.isNotEmpty())
    {
        g.setColour (white);
        g.setFont (text (juce::jmin (22.0f, (float) height * 0.85f)));
        g.drawText (t, r.reduced (6, 0), juce::Justification::centred, false);
    }
}

void Look::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRect (0, 0, width, height);
}

void Look::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    if (! e.isEnabled())
        return;
    frameRect (g, { 0, 0, width, height }, e.hasKeyboardFocus (true) && ! e.isReadOnly() ? white : frame, 2);
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (black);
    frameRect (g, { 0, 0, width, height }, white, 2);
}

namespace
{
    // a small arrow of square pixels, pointing right (or up / down)
    void pixelArrow (juce::Graphics& g, juce::Point<int> tip, int dx, int dy, juce::Colour c)
    {
        g.setColour (c);
        for (int k = 0; k < 4; ++k)
        {
            if (dx != 0)
                g.fillRect (tip.x - dx * k * 2 - (dx > 0 ? 2 : 0), tip.y - k * 2, 2, 2 + k * 4);
            else
                g.fillRect (tip.x - k * 2, tip.y - dy * k * 2 - (dy > 0 ? 2 : 0), 2 + k * 4, 2);
        }
    }
}

void Look::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                              bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                              const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (faint);
        for (int x = area.getX() + 10; x < area.getRight() - 10; x += 6)
            g.fillRect (x, area.getCentreY() - 1, 2, 2);
        return;
    }

    auto r = area.reduced (2, 1);
    const bool lit = isHighlighted && isActive;
    if (lit)
    {
        g.setColour (white);
        g.fillRect (r);
    }
    auto ink = ! isActive ? faint : lit ? black : (textColour != nullptr ? *textColour : white);

    auto in = r.reduced (10, 0);
    auto mark = in.removeFromLeft (16);
    if (isTicked)           // a square, yellow like what's chosen on the unit's screen
    {
        g.setColour (lit ? black : yellow);
        g.fillRect (juce::Rectangle<int> (8, 8).withCentre (mark.getCentre()));
    }
    else if (icon != nullptr)
        icon->drawWithin (g, mark.toFloat().reduced (1.0f), juce::RectanglePlacement::centred, 1.0f);
    in.removeFromLeft (6);

    if (hasSubMenu)
    {
        pixelArrow (g, { in.getRight() - 2, in.getCentreY() }, 1, 0, lit ? black : grey);
        in.removeFromRight (16);
    }
    if (shortcutKeyText.isNotEmpty())
    {
        const auto f = text (20.0f);
        const int w = juce::GlyphArrangement::getStringWidthInt (f, shortcutKeyText) + 4;
        drawText (g, shortcutKeyText, in.removeFromRight (w), juce::Justification::centredRight, f, lit ? black : dim);
        in.removeFromRight (12);
    }
    drawText (g, itemText, in, juce::Justification::centredLeft, getPopupMenuFont(), ink);
}

void Look::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& title)
{
    // the cards' inverted title: a white band, black capitals
    auto band = area.reduced (2, 0).withTrimmedTop (area.getHeight() - 26).withTrimmedBottom (2);
    g.setColour (white);
    g.fillRect (band);
    drawText (g, title.toUpperCase(), band.reduced (12, 0), juce::Justification::centredLeft, label (11.0f), black);
}

void Look::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int standardMenuItemHeight,
                                      int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 12;
        return;
    }
    idealHeight = standardMenuItemHeight > 0 ? juce::jmax (standardMenuItemHeight, 30) : 30;
    // wide enough for the text, the tick, a sub-menu arrow, and the capitals of a section title
    idealWidth = juce::jmax (juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), itemText),
                             juce::GlyphArrangement::getStringWidthInt (label (11.0f), itemText.toUpperCase()) * 4 / 5) + 64;
}

void Look::drawPopupMenuUpDownArrow (juce::Graphics& g, int width, int height, bool isScrollUpArrow)
{
    g.fillAll (black);
    pixelArrow (g, { width / 2 - 1, isScrollUpArrow ? height / 2 - 4 : height / 2 + 4 }, 0, isScrollUpArrow ? -1 : 1, white);
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                          int thumbStart, int thumbSize, bool over, bool down)
{
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
    dither (g, thumb, over || down ? white : grey, 3);
}
}
