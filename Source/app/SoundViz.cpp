#include "SoundViz.h"
#include "Pixel.h"
#include "../core/RaSysex.h"

namespace viz
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    float frac (float x)   { return x - std::floor (x); }

    // a stable pseudo-random value in -1..1 for an integer
    float hash (int n)
    {
        const float s = std::sin ((float) n * 12.9898f + 78.233f) * 43758.5453f;
        return frac (s) * 2.0f - 1.0f;
    }

    // the oscillator / LFO waves, phase in cycles, -1..1
    enum class Wave { triangle, sine, square, saw, sampleHold, random };

    float wave (Wave w, float ph)
    {
        switch (w)
        {
            case Wave::triangle:   return 1.0f - 4.0f * std::abs (frac (ph + 0.25f) - 0.5f);
            case Wave::sine:       return std::sin (twoPi * ph);
            case Wave::square:     return frac (ph) < 0.5f ? 1.0f : -1.0f;
            case Wave::saw:        return 1.0f - 2.0f * frac (ph);
            case Wave::sampleHold: return hash ((int) std::floor (ph * 4.0f));
            case Wave::random:
            {
                const float x = ph * 3.0f;
                const int i = (int) std::floor (x);
                const float t = frac (x);
                const float s = t * t * (3.0f - 2.0f * t);
                return hash (i) + (hash (i + 1) - hash (i)) * s;
            }
        }
        return 0.0f;
    }

    // A grid of square cells over the area: the picture is drawn in cells, like an LCD.
    struct Canvas
    {
        juce::Graphics& g;
        int p, cols, rows, x0, y0;

        Canvas (juce::Graphics& gr, juce::Rectangle<int> r, int cell)
            : g (gr), p (cell), cols (r.getWidth() / cell), rows (r.getHeight() / cell),
              x0 (r.getX() + (r.getWidth() - (r.getWidth() / cell) * cell) / 2),
              y0 (r.getY() + (r.getHeight() - (r.getHeight() / cell) * cell) / 2) {}

        juce::Rectangle<int> cellRect (int cx, int cy) const   { return { x0 + cx * p, y0 + cy * p, p, p }; }

        void dot (int cx, int cy, juce::Colour c)
        {
            if (cx < 0 || cy < 0 || cx >= cols || cy >= rows)
                return;
            g.setColour (c);
            g.fillRect (x0 + cx * p, y0 + cy * p, p - (p > 3 ? 1 : 0), p - (p > 3 ? 1 : 0));
        }

        // a checker dither in cells: level 1 = one cell in four, 2 = one in two
        void ditherDot (int cx, int cy, juce::Colour c, int level)
        {
            if ((level >= 2 && (cx + cy) % 2 == 0) || (level == 1 && cx % 2 == 0 && cy % 2 == 0))
                dot (cx, cy, c);
        }

        void vspan (int cx, int a, int b, juce::Colour c)
        {
            if (a > b) std::swap (a, b);
            for (int y = a; y <= b; ++y)
                dot (cx, y, c);
        }

        void line (int ax, int ay, int bx, int by, juce::Colour c)
        {
            const int dx = std::abs (bx - ax), dy = -std::abs (by - ay);
            const int sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
            int err = dx + dy;
            for (int guard = 0; guard < 4096; ++guard)
            {
                dot (ax, ay, c);
                if (ax == bx && ay == by)
                    break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; ax += sx; }
                if (e2 <= dx) { err += dx; ay += sy; }
            }
        }

        // y = fn (t), t from 0 to 1 over columns [a, b), fn in 0..1 (1 = top of [top, bottom]);
        // consecutive cells are joined so the line has no holes. Optionally fills under it.
        void curve (int a, int b, int top, int bottom, const std::function<float (float)>& fn, juce::Colour c,
                    int fillLevel = 0, juce::Colour fill = juce::Colours::transparentBlack)
        {
            int prev = -1;
            for (int x = a; x < b; ++x)
            {
                const float t = b - a > 1 ? (float) (x - a) / (float) (b - a - 1) : 0.0f;
                const float v = juce::jlimit (0.0f, 1.0f, fn (t));
                const int y = bottom - juce::roundToInt (v * (float) (bottom - top));
                if (fillLevel > 0)
                    for (int yy = y + 1; yy <= bottom; ++yy)
                        ditherDot (x, yy, fill, fillLevel);
                if (prev >= 0 && std::abs (y - prev) > 1)
                    vspan (x, prev + (y > prev ? 1 : -1), y, c);
                dot (x, y, c);
                prev = y;
            }
        }

        juce::Rectangle<int> cells (int cx, int cy, int w, int h) const   { return { x0 + cx * p, y0 + cy * p, w * p, h * p }; }
    };

    void label (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour c,
                juce::Justification j = juce::Justification::topLeft)
    {
        px::drawText (g, t, r, j, px::label (9.0f), c);
    }

    juce::String choice (int address, float v)
    {
        auto names = ra::valueNames (ra::Object::sound, address);
        const int i = juce::roundToInt (v);
        return i >= 0 && i < (int) names.size() ? juce::String::fromUTF8 (names[(size_t) i].c_str()).toUpperCase() : juce::String (i);
    }

    //==========================================================================
    void oscillator (Canvas& cv, juce::Rectangle<int> area, int base, bool fm, const Value& v, juce::Colour ink, double now)
    {
        const int shape = juce::roundToInt (v (base));
        const float pitch = v (base + 1) / 127.0f;
        const float detune = v (base + 2) / 127.0f;
        const int start = juce::roundToInt (v (base + 3));
        const float fmAmount = fm ? v (0x09) / 127.0f : 0.0f;
        const int top = 3, bottom = cv.rows - 3, mid = (top + bottom) / 2;
        const float t = (float) now / 1000.0f;

        for (int x = 0; x < cv.cols; x += 2)
            cv.dot (x, mid, px::frame);

        if (shape <= 4)
        {
            const Wave w = shape == 0 ? Wave::triangle : shape == 1 ? Wave::sine : shape == 2 ? Wave::square
                         : shape == 3 ? Wave::saw : Wave::sampleHold;
            const float cycles = 1.0f + pitch * 5.0f;
            const float scroll = t * (0.15f + pitch * 0.6f);
            const float offset = start == 0 ? 0.0f : start == 1 ? 0.5f : start == 2 ? 0.25f : frac (t * 0.37f);
            auto at = [&] (float x, float extra)
            {
                const float ph = x * cycles + offset + scroll + extra + fmAmount * 0.25f * std::sin (twoPi * x * cycles * 3.0f);
                return 0.5f + 0.42f * wave (w, ph);
            };
            if (detune > 0.02f)
            {
                // the detuned copy, as a ghost
                Canvas ghost = cv;
                for (int x = 0; x < cv.cols; ++x)
                {
                    const float xx = (float) x / (float) juce::jmax (1, cv.cols - 1);
                    const int y = bottom - juce::roundToInt (at (xx * (1.0f + detune * 0.12f), detune * 0.3f) * (float) (bottom - top));
                    ghost.ditherDot (x, y, px::dim, 2);
                }
            }
            cv.curve (0, cv.cols, top, bottom, [&] (float x) { return at (x, 0.0f); }, ink);
        }
        else if (shape <= 8)
        {
            // noise and the sampled metals: a crackle that dies away (or not, for noise)
            const float decay = shape == 5 ? 1e9f : shape == 6 ? 0.12f : shape == 7 ? 0.4f : 0.85f;
            const int seed = (int) (now / 70.0);
            for (int x = 0; x < cv.cols; ++x)
            {
                const float xx = (float) x / (float) cv.cols;
                const float env = decay > 1e6f ? 1.0f : std::exp (-xx / decay);
                const float amp = env * (float) (bottom - top) * 0.5f;
                const int a = mid - juce::roundToInt (amp * std::abs (hash (seed * 131 + x * 7)));
                const int b = mid + juce::roundToInt (amp * std::abs (hash (seed * 173 + x * 11)));
                for (int y = a; y <= b; ++y)
                    if (std::abs (hash (seed + x * 31 + y * 17)) > 0.35f)
                        cv.dot (x, y, ink);
            }
        }
        else
        {
            // external input
            auto r = area.reduced (area.getWidth() / 5, area.getHeight() / 3);
            px::frameRect (cv.g, r, ink, 4);
            px::drawText (cv.g, juce::String::fromUTF8 ("ENTRÉE"), r, juce::Justification::centred, px::label (12.0f), ink);
        }
        label (cv.g, area.reduced (10, 8), choice (base, (float) shape), px::grey);
    }

    void mix (Canvas& cv, juce::Rectangle<int> area, const Value& v, juce::Colour ink, double now)
    {
        // four level meters
        const char* names[] = { "O1", "O2", "RM", "CR" };
        const int addresses[] = { 0x1A, 0x1B, 0x1C, 0x1D };
        const int top = 3, bottom = cv.rows - 5;
        const int blocks = (bottom - top + 1) / 2;
        for (int m = 0; m < 4; ++m)
        {
            const int x = 3 + m * 6;
            const int lit = juce::roundToInt (v (addresses[m]) / 127.0f * (float) blocks);
            for (int b = 0; b < blocks; ++b)
            {
                const int y = bottom - b * 2;
                const auto c = b < lit ? ink : px::frame;
                for (int k = 0; k < 4; ++k)
                    cv.dot (x + k, y, c);
            }
            auto lr = cv.cells (x, bottom + 2, 4, 3);
            label (cv.g, lr.expanded (6, 0), names[m], px::grey, juce::Justification::centredTop);
        }

        // the crack: claps one after the other, as many as its length, as fast as its speed
        const float amount = v (0x1D) / 127.0f, speed = v (0x24) / 127.0f, length = v (0x25) / 127.0f;
        const int count = 1 + juce::roundToInt (length * 7.0f);
        const int spacing = 2 + juce::roundToInt ((1.0f - speed) * 5.0f);
        const int startX = 3 + 4 * 6 + 3;
        const int mid = (top + bottom) / 2;
        const float phase = frac ((float) now / 1100.0f);
        const int shown = (int) (phase * (float) (count + 3));
        const int size = 1 + juce::roundToInt (amount * 2.0f);
        for (int k = 0; k < count; ++k)
        {
            const int cx = startX + size + k * (spacing + size);
            if (cx + size >= cv.cols)
                break;
            const auto c = amount < 0.02f ? px::frame : (k < shown ? ink : px::frame);
            cv.dot (cx, mid, c);
            for (int s = 1; s <= size; ++s)
            {
                cv.dot (cx - s, mid, c); cv.dot (cx + s, mid, c);
                cv.dot (cx, mid - s, c); cv.dot (cx, mid + s, c);
            }
        }
        label (cv.g, cv.cells (startX, top, 20, 3), "CRACK", px::grey);
        juce::ignoreUnused (area);
    }

    void filter (Canvas& cv, juce::Rectangle<int> area, const Value& v, juce::Colour ink, double now)
    {
        const int type = juce::roundToInt (v (0x26));
        const float c = juce::jlimit (0.01f, 0.99f, v (0x27) / 127.0f);
        const float r = v (0x28) / 127.0f, drive = v (0x29) / 127.0f;
        const int top = 4, bottom = cv.rows - 2;
        const int seed = (int) (now / 250.0);

        auto gain = [&] (float x) -> float
        {
            const float bump = r * 0.9f * std::exp (-std::pow ((x - c) / 0.035f, 2.0f));
            switch (type)
            {
                case 0:  return 0.72f;
                case 1:  return 1.0f / std::sqrt (1.0f + std::pow (x / c, 8.0f)) * 0.75f + bump * 0.5f;
                case 2:  return std::exp (-std::pow ((x - c) / (0.05f + (1.0f - r) * 0.12f), 2.0f)) * (0.6f + 0.4f * r);
                case 3:  return 1.0f / std::sqrt (1.0f + std::pow (c / juce::jmax (0.001f, x), 8.0f)) * 0.75f + bump * 0.5f;
                case 4:  return 0.75f * (1.0f - 0.95f * std::exp (-std::pow ((x - c) / (0.03f + (1.0f - r) * 0.06f), 2.0f)));
                case 5:  return 0.35f + 0.4f / (1.0f + std::exp (-(x - c) * 40.0f));
                case 6:  return 0.45f + 0.5f * std::exp (-std::pow ((x - c) / 0.07f, 2.0f)) * (0.3f + 0.7f * r);
                default: return 0.3f + 0.25f * (hash ((int) std::floor (x * 12.0f) + seed) + 1.0f);
            }
        };
        // drive squashes the top and thickens the fill
        const float ceiling = 1.0f - drive * 0.25f;
        cv.curve (0, cv.cols, top, bottom, [&] (float x) { return juce::jmin (gain (x), ceiling); }, ink,
                  1 + juce::roundToInt (drive * 1.0f), ink.withAlpha (0.55f));
        if (type != 0)
        {
            const int cx = juce::roundToInt (c * (float) (cv.cols - 1));
            for (int y = top; y <= bottom; y += 2)
                cv.dot (cx, y, px::red);
        }
        label (cv.g, area.reduced (10, 8), choice (0x26, (float) type), px::grey);
    }

    void amplifier (Canvas& cv, juce::Rectangle<int> area, const Value& v, juce::Colour ink, double now)
    {
        const float vol = v (0x35) / 127.0f, pan = v (0x3A) / 127.0f;
        const int mid = cv.rows / 2;
        for (int x = 2; x < cv.cols - 2; x += 2)
            cv.dot (x, mid, px::frame);
        label (cv.g, cv.cells (0, mid - 2, 3, 4), "L", px::grey, juce::Justification::centred);
        label (cv.g, cv.cells (cv.cols - 3, mid - 2, 3, 4), "R", px::grey, juce::Justification::centred);

        const int cx = 4 + juce::roundToInt (pan * (float) (cv.cols - 9));
        const int size = 1 + juce::roundToInt (vol * 3.0f);
        // waves leaving the sound, bigger when louder
        for (int w = 0; w < 3; ++w)
        {
            const float t = frac ((float) now / 1200.0f + (float) w / 3.0f);
            const float radius = (float) size + 2.0f + t * (4.0f + vol * 10.0f);
            const int steps = juce::jmax (12, juce::roundToInt (radius * 6.0f));
            for (int i = 0; i < steps; ++i)
            {
                const float a = twoPi * (float) i / (float) steps;
                const int x = cx + juce::roundToInt (radius * std::cos (a));
                const int y = mid + juce::roundToInt (radius * std::sin (a) * 0.8f);
                if (t < 0.5f || (x + y) % 2 == 0)
                    cv.dot (x, y, t < 0.75f ? px::dim : px::frame);
            }
        }
        for (int y = -size; y <= size; ++y)
            for (int x = -size; x <= size; ++x)
                cv.dot (cx + x, mid + y, ink);

        const int pattern = juce::roundToInt (v (0x6C));
        if (pattern != 0)
            label (cv.g, area.reduced (10, 8), "PATTERN " + choice (0x6C, (float) pattern), px::yellow);
    }

    void envelope (Canvas& cv, juce::Rectangle<int> area, int base, const Value& v, juce::Colour ink, double now)
    {
        const float a = 0.02f + v (base) / 127.0f * 0.3f;
        const float d = 0.02f + v (base + 1) / 127.0f * 0.3f;
        const float s = v (base + 2) / 127.0f;
        const float r = 0.02f + v (base + 3) / 127.0f * 0.3f;
        const float hold = 0.16f;
        const float k = std::pow (4.0f, (v (base + 4) / 127.0f - 0.5f) * 2.0f);   // curve of the slopes
        const float total = a + d + hold + r;
        const float span = 1.2f;                                                   // the widest envelope fills the screen
        const int top = 4, bottom = cv.rows - 4;

        auto level = [&] (float t) -> float
        {
            if (t < a)             return std::pow (t / a, 1.0f / k);
            t -= a;
            if (t < d)             return 1.0f - (1.0f - s) * std::pow (t / d, k);
            t -= d;
            if (t < hold)          return s;
            t -= hold;
            if (t < r)             return s * (1.0f - std::pow (t / r, k));
            return 0.0f;
        };
        cv.curve (1, cv.cols - 1, top, bottom, [&] (float x) { return level (x * span); }, ink, 1, ink);

        // where each part begins
        const char* names[] = { "A", "D", "S", "R" };
        const float starts[] = { 0.0f, a, a + d, a + d + hold };
        for (int i = 0; i < 4; ++i)
        {
            const int cx = 1 + juce::roundToInt (starts[i] / span * (float) (cv.cols - 3));
            for (int y = top; y <= bottom; y += 3)
                cv.dot (cx, y, px::frame);
            label (cv.g, cv.cells (cx + 1, bottom - 3, 4, 3), names[i], px::grey);
        }

        // a dot running along it
        const float t = frac ((float) now / 1800.0f) * (total + 0.2f);
        if (t <= total)
        {
            const int cx = 1 + juce::roundToInt (t / span * (float) (cv.cols - 3));
            const int cy = bottom - juce::roundToInt (level (t) * (float) (bottom - top));
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                    cv.dot (cx + x, cy + y, px::red);
        }
        juce::ignoreUnused (area);
    }

    void lfo (Canvas& cv, juce::Rectangle<int> area, int base, bool second, const Value& v, juce::Colour ink, double now)
    {
        const float speed = v (base) / 127.0f;
        const int shape = juce::roundToInt (v (base + 1));
        const int sync = juce::roundToInt (v (base + 2));
        const bool oneShot = ! second && juce::roundToInt (v (base + 3)) != 0;
        const float mod = second ? v (base + 3) / 127.0f : 0.0f;
        const Wave w = shape == 0 ? Wave::sine : shape == 1 ? Wave::triangle : shape == 2 ? Wave::square
                     : shape == 3 ? Wave::saw : shape == 4 ? Wave::random : Wave::sampleHold;
        const float cycles = 1.0f + speed * 7.0f;
        const float scroll = oneShot ? 0.0f : (float) now / 1000.0f * (0.1f + speed * 1.2f);
        const int top = 4, bottom = cv.rows - 3, mid = (top + bottom) / 2;
        for (int x = 0; x < cv.cols; x += 2)
            cv.dot (x, mid, px::frame);
        cv.curve (0, cv.cols, top, bottom, [&] (float x)
        {
            const float ph = x * cycles + scroll;
            if (oneShot && x * cycles > 1.0f)
                return 0.5f;
            const float depth = 1.0f - mod * 0.5f * (1.0f + std::sin (twoPi * (x * 0.7f + scroll * 0.3f)));
            return 0.5f + 0.42f * depth * wave (w, ph);
        }, ink);
        juce::String text = choice (base + 1, (float) shape);
        if (sync != 0)
            text << "  SYNC " << choice (base + 2, (float) sync);
        if (oneShot)
            text << "  ONE SHOT";
        label (cv.g, area.reduced (10, 8), text, px::grey);
    }

    void output (Canvas& cv, juce::Rectangle<int> area, const Value& v, juce::Colour ink)
    {
        auto& g = cv.g;
        const float send = v (0x57) / 127.0f;
        const int tuned = juce::roundToInt (v (0x58));
        const float transpose = v (0x59);
        const int low = juce::roundToInt (v (0x5A)), high = juce::roundToInt (v (0x5B));
        const int xorGroup = juce::roundToInt (v (0x5E));

        // routing: the sound to its output, and to an effect with the send level
        const int boxW = 9, rowA = 2, rowB = 8;
        auto box = [&] (int cx, int cy, const juce::String& t, juce::Colour c)
        {
            auto r = cv.cells (cx, cy, boxW + 6, 4);
            px::frameRect (g, r, c, 2);
            px::drawText (g, t, r, juce::Justification::centred, px::label (9.0f), c);
        };
        box (1, rowA + 2, "SON", ink);
        const int outX = cv.cols - (boxW + 6) - 1;
        box (outX, rowA, choice (0x55, v (0x55)), ink);
        box (outX, rowB, choice (0x56, v (0x56)), send > 0.01f ? ink : px::dim);
        cv.line (boxW + 7, rowA + 4, outX - 1, rowA + 2, ink);
        // the send: a dotted line, denser when louder
        const int step = 1 + juce::roundToInt ((1.0f - send) * 4.0f);
        for (int x = boxW + 7, i = 0; x < outX - 1; ++x, ++i)
            if (send > 0.01f && i % step == 0)
                cv.dot (x, rowA + 4 + (x - boxW - 7) * (rowB + 2 - rowA - 4) / juce::jmax (1, outX - boxW - 8), ink);
        if (xorGroup != 0)
            px::drawText (g, "XOR " + juce::String (xorGroup), cv.cells (1, rowB + 1, 16, 3), juce::Justification::centredLeft,
                          px::label (9.0f), px::yellow);

        // the keys it plays as notes: a keyboard of 128 keys
        const int keysTop = cv.rows - 9, keysBottom = cv.rows - 2;
        const bool on = tuned != 0;
        for (int n = 0; n < 128; ++n)
        {
            const int cx = juce::roundToInt ((float) n * (float) (cv.cols - 1) / 127.0f);
            const int pc = n % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const bool inRange = on && n >= juce::jmin (low, high) && n <= juce::jmax (low, high);
            const auto c = inRange ? ink : (black ? px::frame : px::faint);
            cv.vspan (cx, keysTop + (black ? 0 : 2), keysBottom, c);
        }
        juce::String t = on ? juce::String ("NOTES CANAL ") + choice (0x58, (float) tuned) : juce::String ("NOTES : OFF");
        if (on && std::abs (transpose - 64.0f) > 0.5f)
            t << "  TRANSP. " << (transpose >= 64.0f ? "+" : "") << juce::roundToInt (transpose - 64.0f);
        px::drawText (g, t, cv.cells (1, keysTop - 4, cv.cols - 2, 3), juce::Justification::centredLeft, px::label (9.0f),
                      on ? px::grey : px::dim);
        juce::ignoreUnused (area);
    }
}

void draw (juce::Graphics& g, juce::Rectangle<int> area, int section, const Value& v, juce::Colour ink, double now)
{
    // the screen: black, a faint grid of dots, a frame
    g.setColour (px::black);
    g.fillRect (area);
    const int cell = area.getHeight() >= 200 ? 5 : 4;
    Canvas cv (g, area.reduced (6), cell);
    for (int y = 0; y < cv.rows; y += 2)
        for (int x = 0; x < cv.cols; x += 2)
            cv.dot (x, y, juce::Colour (0xff161616));

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);
    switch (section)
    {
        case 0:  oscillator (cv, area, 0x01, true, v, ink, now); break;
        case 1:  oscillator (cv, area, 0x10, false, v, ink, now); break;
        case 2:  mix (cv, area, v, ink, now); break;
        case 3:  filter (cv, area, v, ink, now); break;
        case 4:  amplifier (cv, area, v, ink, now); break;
        case 5:  envelope (cv, area, 0x3F, v, ink, now); break;
        case 6:  envelope (cv, area, 0x45, v, ink, now); break;
        case 7:  lfo (cv, area, 0x4B, false, v, ink, now); break;
        case 8:  lfo (cv, area, 0x4F, true, v, ink, now); break;
        default: output (cv, area, v, ink); break;
    }
    px::frameRect (g, area, px::frame, 2);
}
}
