#include "SeqPage.h"

#include "../core/RaFactory.h"
#include "Text.h"

using namespace ra::seq;

namespace
{
    const char* trigNames[7] = { "Vélo", "Durée", "Micro", "Proba", "Cond.", "Retrig", "Note" };
    const char* modeNames[2] = { "Pas", "Notes" };

    juce::String fr (const char* s)   { return juce::String::fromUTF8 (s); }
    double nowMs()                    { return juce::Time::getMillisecondCounterHiRes(); }

    // progress 0..1 of an animation, in a few hard steps (pixel style)
    float stepped (double t, int steps)
    {
        return (float) std::floor (juce::jlimit (0.0, 1.0, t) * steps) / (float) steps;
    }

    // The A:B conditions in order: always, then 1:2, 2:2, 1:3...
    std::vector<std::pair<int, int>> conditions()
    {
        std::vector<std::pair<int, int>> c { { 0, 0 } };
        for (int b = 2; b <= 8; ++b)
            for (int a = 1; a <= b; ++a)
                c.emplace_back (a, b);
        return c;
    }

    juce::String trackName (const Track& t, int index)
    {
        if (! t.name.empty())
            return juce::String::fromUTF8 (t.name.c_str());
        return "SON " + juce::String (index + 1).paddedLeft ('0', 2);
    }

    juce::String noteName (int n)   { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }

    // the value as shown (ParamText.h)
    juce::String valueText (int address, int value)   { return ui::valueText (address, value); }

    // a little play / stop icon in pixels
    void playIcon (juce::Graphics& g, juce::Rectangle<int> r, bool playing, juce::Colour c)
    {
        g.setColour (c);
        const int s = 3;
        auto o = r.getCentre().translated (-8, -9);
        if (playing)
            g.fillRect (o.x, o.y + 2, 16, 16);
        else
            for (int i = 0; i < 6; ++i)
                g.fillRect (o.x + i * s, o.y + i * s / 2 + 1, s, 18 - i * s);
    }

    int textWidth (const juce::Font& f, const juce::String& t)
    {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, t));
    }
}

//==============================================================================
SeqPage::SeqPage (Engine& e, Sequencer& s) : engine (e), seq (s)
{
    // each section's parameters in one line, in the front panel's order
    for (auto& sec : ui::soundSections())
    {
        std::vector<ui::ParamDef> flat;
        for (auto& row : sec.rows)
            for (auto& d : row)
                flat.push_back (d);
        sectionParams.push_back (std::move (flat));
    }

    setWantsKeyboardFocus (true);
    setOpaque (true);
    startTimerHz (40);
}

SeqPage::~SeqPage()
{
    stopTimer();
}

void SeqPage::selectTrack (int t)
{
    t = juce::jlimit (0, numTracks - 1, t);
    if (t != track)
    {
        pointerFromX = voices[(size_t) track].getCentreX();
        pointerStart = nowMs();
        startWipe();
    }
    track = t;
    selection.clear();
    page = 0;
    stickyQuick = {};
    repaint();
}

void SeqPage::timerCallback()
{
    if (! isShowing())
        return;
    // Shift let go: out of the lock mode, as when a held trig is let go on an Elektron
    if (shiftHeld && ! juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
    {
        shiftHeld = false;
        selection.clear();
    }
    const bool playing = seq.playheadPpq() >= 0.0;
    if (wasPlaying && ! playing)
        stopWaveStart = nowMs();          // a wave runs over the steps when it stops
    if (! wasPlaying && playing)
        excitedUntil = nowMs() + 400.0;
    wasPlaying = playing;
    if (drag.hit.zone != Zone::param)
        updateSlots();
    repaint();
}

//==============================================================================
// The quick access row

std::vector<SeqPage::Quick> SeqPage::quickFor (int s) const
{
    const auto& kit = engine.getShownKit();
    auto v = [&] (int address) { return kit.hasSound (s) ? kit.soundValue (s, address) : 64; };
    auto q = [] (int address, const char* label, bool choice, const char* why = "") { return Quick { { address, label, choice }, fr (why) }; };

    std::vector<Quick> list { q (0x02, "Pitch 1", false), q (0x11, "Pitch 2", false), q (0x27, "Cutoff", false),
                              q (0x28, "Reso", false), q (0x3F, "Attack", false), q (0x40, "Decay", false),
                              q (0x42, "Release", false), q (0x35, "Volume", false) };
    if (! kit.hasSound (s))
        return list;

    // noise, hats, cymbal: the colour matters more than a second pitch or an attack
    const int shape = v (0x01);
    const bool noisy = shape >= 5 && shape <= 8;
    if (v (0x1B) == 0 && v (0x1C) == 0 && v (0x09) == 0)
        list[1] = q (0x01, "Forme 1", true, "SOLO");           // Osc 2 can't be heard: the shape of Osc 1, noise or not
    else if (noisy)
        list[1] = q (0x29, "Drive", false, "BRUIT");
    if (noisy)
        list[4] = q (0x26, "Filtre", true, "BRUIT");

    // the slot that shows what makes this sound particular
    const bool pitchDrops = v (0x05) != 64 && (v (0x07) == 0 || v (0x07) == 1);
    if (v (0x1D) > 0)
    {
        list[6] = q (0x24, "C.Speed", false, "CLAP");
        list[7] = q (0x25, "C.Len", false, "CLAP");
    }
    else if (pitchDrops)
    {
        list[6] = q (0x05, "P.Mod", false, "CHUTE");
        if (v (0x07) == 1)
            list[7] = q (0x46, "Decay 2", false, "CHUTE");
    }
    else if (v (0x09) > 0)
        list[6] = q (0x09, "FM", false, "FM");
    else if (v (0x58) != 0)
        list[6] = q (0x59, "Transp", false, "NOTES");
    return list;
}

void SeqPage::updateSlots()
{
    auto next = params();
    std::vector<juce::String> why (next.size());
    auto quick = quickFor (track);
    if (stickyQuick.track == track && stickyQuick.index >= 0 && stickyQuick.index < (int) quick.size()
        && quick[(size_t) stickyQuick.index].def.address != stickyQuick.def.address)
        quick[(size_t) stickyQuick.index] = { stickyQuick.def, {} };
    for (auto& qk : quick)
    {
        next.push_back (qk.def);
        why.push_back (qk.why);
    }
    bool same = next.size() == slots.size() && why == slotWhy;
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].address == slots[i].address;
    if (same)
        return;
    slots = std::move (next);
    slotWhy = std::move (why);
}

void SeqPage::toggleTuned()
{
    const auto& kit = engine.getKit();
    if (! kit.hasSound (track))
        return;
    if (kit.soundValue (track, 0x58) != 0)
    {
        engine.setSoundParameter (track, 0x58, 0);
        showToast (fr ("SON NON ACCORDÉ"));
        return;
    }
    const int channel = engine.freeTunedChannel (track);
    if (channel == 0)
    {
        showToast (fr ("PLUS DE CANAL LIBRE"), px::red);
        return;
    }
    engine.setSoundParameter (track, 0x58, channel);
    showToast (fr ("SON ACCORDÉ : CANAL ") + juce::String (channel));
    addBurst (tuneBox.getCentre(), px::white);
}

bool SeqPage::isSelected (int step) const
{
    return std::find (selection.begin(), selection.end(), step) != selection.end();
}

void SeqPage::toggleSelection (int step)
{
    auto it = std::find (selection.begin(), selection.end(), step);
    if (it != selection.end())
        selection.erase (it);
    else
        selection.push_back (step);
}

const juce::String SeqPage::initTrackText = "INIT PISTE";
const juce::String SeqPage::initAllText = juce::String::fromUTF8 ("INIT SÉQUENCE");

int SeqPage::playingStep (Division d, int len, double ppq)
{
    if (ppq < 0.0 || len <= 0)
        return -1;
    const auto n = (std::int64_t) std::floor (ppq * stepsPerBeat (d) + 1e-9);
    return (int) (((n % len) + len) % len);
}

//==============================================================================
// Layout

void SeqPage::resized()
{
    laidOutSection = -1;
    layout();
}

void SeqPage::layout()
{
    if (laidOutSection == section && ! header.isEmpty())
        return;
    laidOutSection = section;

    auto r = getLocalBounds().reduced (14, 10);

    // clock
    header = r.removeFromTop (42);
    {
        auto h = header;
        mascotArea = h.removeFromLeft (44);
        h.removeFromLeft (10);
        kitBox = h.removeFromLeft (juce::jmin (420, h.getWidth() / 3));
        loadBox = h.removeFromRight (150);
        h.removeFromRight (14);
        swingBox = h.removeFromRight (150);
        h.removeFromRight (10);
        tempoBox = h.removeFromRight (170);
        h.removeFromRight (10);
        playBox = h.removeFromRight (130);
    }
    r.removeFromTop (10);

    // the voices: two rows of twelve
    {
        const int cellH = 46, gap = 6;
        auto area = r.removeFromTop (cellH * 2 + gap);
        const float w = (float) (area.getWidth() + gap) / 12.0f;
        for (int i = 0; i < numTracks; ++i)
        {
            const int col = i % 12, row = i / 12;
            const int x0 = area.getX() + juce::roundToInt ((float) col * w);
            const int x1 = area.getX() + juce::roundToInt ((float) (col + 1) * w) - gap;
            voices[(size_t) i] = { x0, area.getY() + row * (cellH + gap), x1 - x0, cellH };
        }
    }
    r.removeFromTop (14);

    // the sequencer at the bottom, the sound above it
    const int fixed = 24 + 40 + 6 + 10 + 10 + 16 + 8 + 28 + 6 + 40;    // everything but the steps
    const int stepsH = juce::jlimit (48, 92, r.getHeight() - 12 - fixed - 300);
    seqPanel = r.removeFromBottom (fixed + stepsH);
    r.removeFromBottom (0);
    soundPanel = r;

    // sound: section tabs, listen, then the knobs of the section in one line
    {
        auto in = soundPanel.reduced (16, 12);
        soundTitle = in.removeFromTop (32);
        auto t = soundTitle;
        listenBox = t.removeFromRight (150);
        t.removeFromRight (16);
        const int n = (int) sectionParams.size();
        const int tw = juce::jmin (112, (t.getWidth() - (n - 1) * 4) / n);
        sectionTabs.clear();
        for (int i = 0; i < n; ++i)
        {
            sectionTabs.push_back (t.removeFromLeft (tw));
            t.removeFromLeft (4);
        }
        in.removeFromTop (14);

        // the quick access row: the essentials of this sound, and its tuning, in a band of its own
        quickRow = in.removeFromTop (72);
        in.removeFromTop (16);
        {
            auto q = quickRow.reduced (8, 8);
            quickHeader = q.removeFromLeft (76);
            q.removeFromLeft (14);
            tuneBox = q.removeFromRight (190);
            q.removeFromRight (14);
            const int count = 8, gap = 12;
            const float w = (float) (q.getWidth() - (count - 1) * gap) / (float) count;
            quickBoxes.clear();
            for (int i = 0; i < count; ++i)
            {
                const int x0 = q.getX() + juce::roundToInt ((float) i * (w + (float) gap));
                const int x1 = q.getX() + juce::roundToInt ((float) i * (w + (float) gap) + w);
                quickBoxes.push_back ({ x0, q.getY(), x1 - x0, q.getHeight() });
            }
        }

        // the picture on the left, the settings on the right in the front panel's rows
        vizBox = in.removeFromLeft (juce::jlimit (260, 560, in.getWidth() * 2 / 5));
        in.removeFromLeft (16);
        const auto& sec = ui::soundSections()[(size_t) section];
        const int rows = (int) sec.rows.size();
        int cols = 1;
        for (auto& row : sec.rows)
            cols = juce::jmax (cols, (int) row.size());
        const int gap = 8;
        const int cellW = juce::jmin (190, (in.getWidth() - (cols - 1) * gap) / cols);
        const int cellH = juce::jmin (70, (in.getHeight() - (rows - 1) * gap) / rows);
        const int y0 = in.getY() + (in.getHeight() - (rows * cellH + (rows - 1) * gap)) / 2;
        paramBoxes.clear();
        for (int rw = 0; rw < rows; ++rw)
            for (int c = 0; c < (int) sec.rows[(size_t) rw].size(); ++c)
                paramBoxes.push_back ({ in.getX() + c * (cellW + gap), y0 + rw * (cellH + gap), cellW, cellH });
        quickFrom = (int) paramBoxes.size();
        for (auto& b : quickBoxes)
            paramBoxes.push_back (b);
    }
    updateSlots();

    // sequencer
    {
        auto in = seqPanel.reduced (16, 12);
        auto t = in.removeFromTop (40);
        // INIT PISTE and INIT SÉQUENCE at the right end, in reach without opening OPTIONS
        const auto initFont = px::label (12.0f);
        initAllBox = t.removeFromRight (textWidth (initFont, initAllText) + 28).reduced (0, 3);
        t.removeFromRight (6);
        initTrackBox = t.removeFromRight (textWidth (initFont, initTrackText) + 28).reduced (0, 3);
        t.removeFromRight (16);
        const int tabW = 104, headerFixed = 2 * (tabW + 6) + 10 + 138 + 134 + 102 + 136;
        nameBox = t.removeFromLeft (juce::jlimit (160, 330, t.getWidth() - headerFixed - 10));
        t.removeFromLeft (10);
        for (auto& tab : modeTabs)
        {
            tab = t.removeFromLeft (tabW).reduced (0, 3);
            t.removeFromLeft (6);
        }
        t.removeFromLeft (10);
        lengthBox = t.removeFromLeft (132).reduced (0, 3);
        t.removeFromLeft (6);
        divisionBox = t.removeFromLeft (128).reduced (0, 3);
        t.removeFromLeft (6);
        muteBox = t.removeFromLeft (96).reduced (0, 3);
        t.removeFromLeft (6);
        optionsBox = t.removeFromLeft (136).reduced (0, 3);

        in.removeFromTop (6 + 10);     // the playhead runs in the gap above the steps
        auto row = in.removeFromTop (stepsH);
        const int gap = 6, groupGap = 10;
        const float w = (float) (row.getWidth() - 15 * gap - 3 * groupGap) / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            const int x = row.getX() + juce::roundToInt ((float) i * (w + (float) gap)) + (i / 4) * groupGap;
            stepBoxes[(size_t) i] = { x, row.getY(), juce::roundToInt (w), stepsH };
        }
        in.removeFromTop (10);
        miniMap = in.removeFromTop (16);
        in.removeFromTop (8);
        hintRow = in.removeFromTop (28);
        in.removeFromTop (6);
        trigRow = in.removeFromTop (40);
        const int tw = (trigRow.getWidth() - 6 * 8) / 7;
        auto tr = trigRow;
        for (auto& b : trigBoxes)
        {
            b = tr.removeFromLeft (tw);
            tr.removeFromLeft (8);
        }
        clearBox = hintRow.withLeft (hintRow.getRight() - 170);
    }
}

SeqPage::Hit SeqPage::zoneAt (juce::Point<int> p) const
{
    if (playBox.contains (p))     return { Zone::play, 0 };
    if (tempoBox.contains (p))    return { Zone::tempo, 0 };
    if (swingBox.contains (p))    return { Zone::swing, 0 };
    for (int i = 0; i < numTracks; ++i)
        if (voices[(size_t) i].contains (p)) return { Zone::voice, i };
    for (int i = 0; i < (int) sectionTabs.size(); ++i)
        if (sectionTabs[(size_t) i].contains (p)) return { Zone::section, i };
    if (listenBox.contains (p))   return { Zone::listen, 0 };
    if (tuneBox.contains (p))     return { Zone::tune, 0 };
    for (int i = 0; i < (int) paramBoxes.size(); ++i)
        if (paramBoxes[(size_t) i].contains (p)) return { Zone::param, i };
    for (int i = 0; i < 2; ++i)
        if (modeTabs[(size_t) i].contains (p)) return { Zone::mode, i };
    if (lengthBox.contains (p))   return { Zone::length, 0 };
    if (optionsBox.contains (p))  return { Zone::options, 0 };
    if (initTrackBox.contains (p)) return { Zone::initTrack, 0 };
    if (initAllBox.contains (p))  return { Zone::initAll, 0 };
    if (divisionBox.contains (p)) return { Zone::division, 0 };
    if (muteBox.contains (p))     return { Zone::mute, 0 };
    for (int i = 0; i < 16; ++i)
        if (stepBoxes[(size_t) i].contains (p)) return { Zone::step, i };
    if (miniMap.expanded (0, 4).contains (p)) return { Zone::miniMap, 0 };
    if (! selection.empty() && clearBox.contains (p)) return { Zone::clearSelection, 0 };
    for (int i = 0; i < 7; ++i)
        if (trigBoxes[(size_t) i].contains (p)) return { Zone::trig, i };
    return {};
}

//==============================================================================
// Painting

void SeqPage::paint (juce::Graphics& g)
{
    layout();
    g.fillAll (px::black);
    px::dither (g, getLocalBounds(), juce::Colour (0xff161616), 1);   // the faint dot grid

    const auto now = nowMs();
    const auto ppq = seq.playheadPpq();

    std::array<Voice, numTracks> v;
    Track open;
    double tempo = 120.0;
    int swing = 50;
    seq.read ([&] (const Pattern& p)
    {
        for (int i = 0; i < numTracks; ++i)
        {
            const auto& t = p.tracks[(size_t) i];
            auto& o = v[(size_t) i];
            o.mode = t.mode;
            o.division = t.division;
            o.mute = t.mute;
            o.length = t.activeLength();
            o.name = trackName (t, i);
            o.trigs = t.trigs();
            o.locks = t.countLocks() > 0;
        }
        open = p.tracks[(size_t) track];
        tempo = p.tempo;
        swing = p.swing;
    });

    paintHeader (g, now, tempo, swing);
    paintVoices (g, v, now, ppq);

    // one panel for the open track: its sound, then its sequencer
    auto panel = soundPanel.getUnion (seqPanel);
    g.setColour (px::panel);
    g.fillRect (panel);
    px::frameRect (g, panel, px::white, 3);
    {
        // the pointer towards the open voice, sliding when the track changes
        float x = (float) voices[(size_t) track].getCentreX();
        const double t = (now - pointerStart) / 160.0;
        if (t < 1.0 && pointerFromX >= 0.0)
            x = (float) (pointerFromX + (x - pointerFromX) * stepped (t, 5));
        juce::Path tri;
        const float y = (float) panel.getY();
        tri.addTriangle (x - 12.0f, y, x + 12.0f, y, x, y - 12.0f);
        g.setColour (px::white);
        g.fillPath (tri);
    }
    paintSound (g, open);
    px::dither (g, juce::Rectangle<int> (seqPanel.getX() + 16, seqPanel.getY() - 2, seqPanel.getWidth() - 32, 4), px::faint, 3);
    paintSequencer (g, open, ppq);
    paintOverlays (g, now);
}

void SeqPage::paintHeader (juce::Graphics& g, double now, double tempo, int swing)
{
    const auto ppq = seq.playheadPpq();
    const bool playing = ppq >= 0.0;
    const bool host = seq.isFollowingHost();
    const int load = seq.midiLoadPercent();

    // the mascot hops on the beat, jumps when excited, blinks now and then, sweats when the cable is full
    const bool up = playing && std::fmod (ppq, 1.0) < 0.5;
    const bool excited = now < excitedUntil;
    const bool blink = std::fmod (now, 4200.0) < 140.0;
    const int lift = excited ? (std::fmod (now, 160.0) < 80.0 ? 10 : 4) : (up ? 4 : 0);
    const auto m = juce::Point<int> (mascotArea.getX(), mascotArea.getCentreY() - 16 - lift);
    px::mascot (g, m, 4, blink && ! excited);
    if (load > 80)
    {
        g.setColour (px::red);
        const int dy = (int) (std::fmod (now, 600.0) / 100.0) * 2;
        g.fillRect (m.x + 42, m.y + 2 + dy, 4, 4);
        g.fillRect (m.x + 40, m.y + 6 + dy, 8, 4);
    }

    auto k = kitBox;
    auto kitName = juce::String (engine.getKit().name());
    if (kitName.isEmpty())
        kitName = fr ("non lu");
    px::drawText (g, "KIT", k.removeFromLeft (46), juce::Justification::centredLeft, px::text (30.0f), px::dim);
    px::drawText (g, upper (kitName), k, juce::Justification::centredLeft, px::text (30.0f), px::white);

    // play
    {
        auto b = playBox.reduced (0, 4);
        const bool down = drag.hit.zone == Zone::play;
        if (host)
        {
            g.setColour (px::red);
            g.fillRect (b);
            px::drawText (g, "DAW", b, juce::Justification::centred, px::label (19.0f, true), px::black);
        }
        else
        {
            px::button (g, b.withTrimmedBottom (3), playing, down, px::red);
            auto inner = b.withTrimmedBottom (3).translated (0, down ? 3 : 0);
            playIcon (g, inner.removeFromLeft (44), playing, playing ? px::black : px::white);
            px::drawText (g, playing ? "STOP" : "PLAY", inner, juce::Justification::centredLeft, px::label (19.0f, true),
                          playing ? px::black : px::white);
        }
    }

    auto field = [&] (juce::Rectangle<int> r, const juce::String& name, const juce::String& value, bool active, Zone z)
    {
        if (active && hover.zone == z)
            px::frameRect (g, r, px::frame, 2);
        auto t = r.reduced (8, 0);
        px::drawText (g, name, t.removeFromLeft (textWidth (px::text (30.0f), name) + 10),
                      juce::Justification::centredLeft, px::text (30.0f), px::dim);
        px::drawText (g, value, t, juce::Justification::centredLeft, px::text (34.0f), active ? px::white : px::grey);
    };
    field (tempoBox, "BPM", juce::String (host ? seq.getTempo() : tempo, 1), ! host, Zone::tempo);
    field (swingBox, "SWING", juce::String (swing) + "%", true, Zone::swing);

    // MIDI cable load
    {
        auto r = loadBox;
        auto top = r.removeFromTop (24);
        px::drawText (g, "MIDI", top.removeFromLeft (60), juce::Justification::centredLeft, px::label (12.5f), px::dim);
        px::drawText (g, juce::String (load) + "%", top, juce::Justification::centredRight, px::text (26.0f), load > 80 ? px::red : px::white);
        px::bar (g, r.removeFromTop (10), 12, juce::jlimit (0, 12, (load * 12 + 50) / 100), load > 80 ? px::red : px::white);
    }

    px::dither (g, header.withTop (header.getBottom() + 4).withHeight (4), px::faint, 3);
}

void SeqPage::paintVoices (juce::Graphics& g, const std::array<Voice, numTracks>& all, double now, double ppq)
{
    for (int i = 0; i < numTracks; ++i)
    {
        const auto& tr = all[(size_t) i];
        auto r = voices[(size_t) i];
        bool keyboard = false;
        const double hit = seq.lastHitMs (i, keyboard);
        const double age = hit < 0.0 ? 1e9 : now - hit;
        // the flash fades out in four dithers, like an old screen
        const int level = age < 60.0 ? 4 : age < 120.0 ? 3 : age < 180.0 ? 2 : age < 240.0 ? 1 : 0;
        const auto flash = keyboard ? px::red : px::white;

        g.setColour (px::cell);
        g.fillRect (r);
        px::dither (g, r, flash, level);
        const bool selected = i == track;
        px::frameRect (g, r, selected ? px::white : (hover.zone == Zone::voice && hover.index == i ? px::grey : px::frame), selected ? 3 : 2);

        const bool inverted = level >= 4;
        auto inner = r.reduced (8, 5);
        auto strip = inner.removeFromBottom (4);
        inner.removeFromBottom (3);
        const int jump = age < 60.0 ? 3 : 0;
        auto top = inner.removeFromTop (inner.getHeight() / 2 + 2).translated (0, -jump);
        auto nameRow = inner.translated (0, -jump);

        // on the dithered fade, the text sits on small black plates to stay readable
        auto plate = [&] (juce::Rectangle<int> area, const juce::String& t, const juce::Font& f)
        {
            if (level > 0 && level < 4)
            {
                const int w = juce::jmin (area.getWidth(), textWidth (f, t) + 6);
                g.setColour (px::black);
                g.fillRect (juce::Rectangle<int> (area.getX() - 3, area.getCentreY() - juce::roundToInt (f.getHeight() * 0.4f),
                                                  w, juce::roundToInt (f.getHeight() * 0.8f)));
            }
        };
        const auto numText = juce::String (i + 1).paddedLeft ('0', 2);
        plate (top, numText, px::text (28.0f));
        px::drawText (g, numText, top, juce::Justification::centredLeft, px::text (28.0f),
                      inverted ? px::black : (tr.mute ? px::faint : px::dim));
        top.removeFromRight (12);
        if (tr.mode == Mode::notes && ! tr.mute)
            px::drawText (g, "NOTE", top, juce::Justification::centredRight, px::label (9.0f), inverted ? px::black : px::grey);
        const auto name = upper (tr.name);
        plate (nameRow, name, px::label (10.0f));
        px::drawText (g, name, nameRow, juce::Justification::centredLeft, px::label (10.0f),
                      inverted ? px::black : (tr.mute ? px::dim : px::white));

        // a strip of the page being played
        const int len = tr.length;
        const int playing = playingStep (tr.division, tr.length, ppq);
        const int base = playing >= 0 ? (playing / stepsPerPage) * stepsPerPage : 0;
        const float w = (float) (strip.getWidth() + 1) / 16.0f;
        for (int s = 0; s < 16; ++s)
        {
            const int idx = base + s;
            auto b = juce::Rectangle<int> (strip.getX() + juce::roundToInt ((float) s * w), strip.getY(), juce::jmax (1, juce::roundToInt (w) - 1), strip.getHeight());
            juce::Colour c = juce::Colour (0xff222222);
            if (idx < len && tr.trigs[(size_t) idx])
                c = tr.mute ? px::faint : px::white;
            if (idx == playing && ! tr.mute)
                c = px::red;
            if (idx >= len)
                c = px::black;
            g.setColour (inverted && c != px::red ? px::black : c);
            g.fillRect (b);
        }

        if (tr.mute)
        {
            auto chip = r.withTrimmedLeft (r.getWidth() - 50).withHeight (16).translated (-5, 5);
            g.setColour (px::red);
            g.fillRect (chip);
            px::drawText (g, "MUTE", chip, juce::Justification::centred, px::label (9.0f), px::black);
        }
        else if (tr.locks)
        {
            g.setColour (px::yellow);
            g.fillRect (r.getRight() - 14, r.getY() + 6, 7, 7);
        }
    }
}

void SeqPage::paintSound (juce::Graphics& g, const Track& tr)
{
    const auto now = nowMs();

    // section tabs
    for (int i = 0; i < (int) sectionTabs.size(); ++i)
    {
        auto b = sectionTabs[(size_t) i];
        const bool on = i == section;
        if (on)
        {
            g.setColour (px::white);
            g.fillRect (b);
        }
        else
        {
            g.setColour (px::cell);
            g.fillRect (b);
            px::frameRect (g, b, hover.zone == Zone::section && hover.index == i ? px::grey : px::frame, 2);
        }
        px::drawText (g, upper (fr (ui::soundSections()[(size_t) i].tab)), b, juce::Justification::centred,
                      px::label (11.0f), on ? px::black : px::grey);
    }
    {
        const bool down = drag.hit.zone == Zone::listen;
        px::button (g, listenBox.withTrimmedBottom (3), true, down, px::white);
        auto inner = listenBox.withTrimmedBottom (3).translated (0, down ? 3 : 0).withTrimmedBottom (down ? 0 : 4);
        playIcon (g, inner.removeFromLeft (40), false, px::black);
        px::drawText (g, fr ("ÉCOUTER"), inner, juce::Justification::centredLeft, px::label (12.0f), px::black);
    }

    // the values shown move towards the real ones: the picture stretches
    const auto& kit = engine.getShownKit();
    const bool known = kit.hasSound (track);
    const bool locking = ! selection.empty();
    auto target = [&] (int address) -> float
    {
        float v = known ? (float) kit.soundValue (track, address) : 0.0f;
        if (locking)
            if (auto* l = tr.steps[(size_t) selection.front()].findLock (address))
                v = (float) l->value;
        return v;
    };
    bool anyLocked = false;
    for (auto& def : slots)
    {
        auto& v = shownValues[(size_t) (def.address & 0x7F)];
        const float to = target (def.address);
        if (! shownReady || def.choice || std::abs (to - v) < 0.6f)
            v = to;
        else
            v += (to - v) * 0.28f;
        if (locking)
            for (auto st : selection)
                anyLocked = anyLocked || tr.steps[(size_t) st].findLock (def.address) != nullptr;
    }
    shownReady = true;

    const double wt = (now - wipeStart) / 200.0;
    const int slide = wt < 1.0 ? juce::roundToInt ((1.0f - stepped (wt, 5)) * 40.0f) : 0;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (soundPanel.reduced (6));
    g.addTransform (juce::AffineTransform::translation ((float) slide, 0.0f));

    viz::draw (g, vizBox, section, [this] (int address) { return shownValues[(size_t) (address & 0x7F)]; },
               anyLocked ? px::yellow : px::white, now);

    // which page the screen shows, in a white tag at its top right
    {
        const auto name = upper (fr (ui::soundSections()[(size_t) section].tab));
        const auto f = px::label (10.0f, true);
        const int w = textWidth (f, name) + 16;
        auto tag = juce::Rectangle<int> (vizBox.getRight() - w - 6, vizBox.getY() + 6, w, 20);
        g.setColour (px::white);
        g.fillRect (tag);
        px::drawText (g, name, tag, juce::Justification::centred, f, px::black);
    }

    // the band of the quick access row (it stays while the section slides)
    {
        auto band = quickRow.translated (-slide, 0);
        g.setColour (px::black);
        g.fillRect (band);
        px::dither (g, band.reduced (2), px::frame, 1);
        px::frameRect (g, band, px::grey, 2);
        auto head = quickHeader.translated (-slide, 0);
        g.setColour (px::white);
        g.fillRect (head);
        auto lines = head.reduced (6, 8);
        px::drawText (g, fr ("ACCÈS"), lines.removeFromTop (lines.getHeight() / 2), juce::Justification::centredLeft,
                      px::label (9.0f, true), px::black);
        px::drawText (g, "RAPIDE", lines, juce::Justification::centredLeft, px::label (9.0f, true), px::black);
    }

    const auto& defs = slots;
    for (int i = 0; i < (int) defs.size() && i < (int) paramBoxes.size(); ++i)
    {
        const auto& def = defs[(size_t) i];
        auto b = paramBoxes[(size_t) i];
        const bool quick = i >= quickFrom;
        if (quick && slide != 0)
            b.translate (-slide, 0);              // the quick row stays still while the section slides
        const int base = known ? kit.soundValue (track, def.address) : 0;
        const int maxV = paramMax (def.address);

        int lockedCount = 0, lockedValue = -1;
        bool mixed = false;
        for (auto st : selection)
            if (auto* l = tr.steps[(size_t) st].findLock (def.address))
            {
                if (lockedCount > 0 && l->value != lockedValue)
                    mixed = true;
                lockedValue = l->value;
                ++lockedCount;
            }
        const bool locked = locking && lockedCount > 0;
        const bool hot = hover.zone == Zone::param && hover.index == i;
        const bool dragging = drag.hit.zone == Zone::param && drag.hit.index == i;

        if (! quick)
        {
            g.setColour (dragging ? juce::Colour (0xff1c1c1c) : px::cell);
            g.fillRect (b);
            px::frameRect (g, b, locked ? px::yellow : (hot || dragging ? px::white : px::frame), 2);
        }

        juce::String text;
        juce::Colour colour = px::white;
        int shown = base;
        if (locked)
        {
            shown = lockedValue;
            text = (mixed || lockedCount < (int) selection.size()) ? fr ("MIXTE") : valueText (def.address, lockedValue);
            colour = px::yellow;
        }
        else if (! known)
        {
            text = "?";
            colour = px::dim;
        }
        else
        {
            text = valueText (def.address, base);
            colour = locking ? px::dim : px::white;
        }

        if (quick)
        {
            // the quick row: black cells with the small ring knobs (not the bars of the section)
            g.setColour (dragging ? juce::Colour (0xff1c1c1c) : px::black);
            g.fillRect (b);
            px::frameRect (g, b, locked ? px::yellow : (hot || dragging ? px::white : px::faint), 2);
            auto in = b.reduced (8, 6);
            if (! def.choice)
            {
                const int k = juce::jmin (in.getHeight(), 34);
                auto knobArea = in.removeFromLeft (k).withSizeKeepingCentre (k, k);
                const float marker = locked && known ? (float) base / (float) juce::jmax (1, maxV) : -1.0f;
                px::knob (g, knobArea, (float) shown / (float) juce::jmax (1, maxV),
                          locked ? px::yellow : (locking ? px::faint : (hot || dragging ? px::white : juce::Colour (0xffe0e0e0))),
                          2, marker, {}, ui::isBipolar (def.address));
                in.removeFromLeft (8);
            }
            auto nameRow = in.removeFromTop (16);
            px::drawText (g, upper (fr (def.label)), nameRow, juce::Justification::centredLeft, px::text (19.0f),
                          locked ? px::yellow : (hot || dragging ? px::white : px::grey));
            if ((size_t) i < slotWhy.size() && slotWhy[(size_t) i].isNotEmpty())
            {
                // why it is offered for this sound: a small tag sitting on the cell's top edge
                const auto f = px::label (7.0f);
                const int tw = textWidth (f, slotWhy[(size_t) i]) + 8;
                const juce::Rectangle<int> tag (b.getRight() - tw - 6, b.getY() - 6, tw, 12);
                g.setColour (px::white);
                g.fillRect (tag);
                px::drawText (g, slotWhy[(size_t) i], tag, juce::Justification::centred, f, px::black);
            }
            px::drawText (g, text, in, juce::Justification::bottomRight, px::text (28.0f), colour);
            if (def.choice)
            {
                g.setColour (hot ? px::white : px::dim);
                g.fillRect (b.getX() + 10, b.getBottom() - 14, 8, 2);
                g.fillRect (b.getX() + 12, b.getBottom() - 12, 4, 2);
            }
            continue;
        }

        auto in = b.reduced (8, 5);
        const bool roomy = in.getHeight() >= 46;
        auto bottom = in.removeFromBottom (def.choice ? 0 : 5);
        auto nameRow = roomy ? in.removeFromTop (16) : in;
        if (quick && (size_t) i < slotWhy.size() && slotWhy[(size_t) i].isNotEmpty())
        {
            // why it is offered for this sound, in a small dim tag
            const auto f = px::label (7.0f);
            const int tw = textWidth (f, slotWhy[(size_t) i]) + 6;
            auto tag = nameRow.removeFromRight (tw).withSizeKeepingCentre (tw, 12);
            if (roomy)
                tag = tag.withY (nameRow.getY() + 2);
            g.setColour (px::frame);
            g.fillRect (tag);
            px::drawText (g, slotWhy[(size_t) i], tag, juce::Justification::centred, f, px::grey);
        }
        px::drawText (g, upper (fr (def.label)), nameRow, roomy ? juce::Justification::centredLeft : juce::Justification::topLeft,
                      px::label (9.5f), locked ? px::yellow : (hot || dragging ? px::white : px::grey));
        px::drawText (g, text, in, roomy ? juce::Justification::centredRight : juce::Justification::bottomRight, px::text (30.0f), colour);
        if (def.choice)
        {
            // a small pixel arrow: click for the list
            g.setColour (hot ? px::white : px::dim);
            const int ax = b.getRight() - 16, ay = b.getY() + 8;
            g.fillRect (ax, ay, 8, 2);
            g.fillRect (ax + 2, ay + 2, 4, 2);
        }
        else
        {
            const int filled = juce::roundToInt (16.0 * shown / juce::jmax (1, maxV));
            const int marker = locked && known ? juce::jlimit (0, 15, juce::roundToInt (16.0 * base / juce::jmax (1, maxV))) : -1;
            const auto barColour = locked ? px::yellow : (locking ? px::faint : px::white);
            if (ui::isBipolar (def.address))
                px::barCentred (g, bottom, 16, (float) shown / 127.0f, barColour, marker);
            else
                px::bar (g, bottom, 16, filled, barColour, marker);
        }
    }

    // "accorde mon son !": the sound plays notes on its own MIDI channel (Tuned Channel)
    {
        auto b = tuneBox.translated (-slide, 0);
        const int tuned = known ? kit.soundValue (track, 0x58) : 0;
        const bool on = tuned != 0;
        const bool hot = hover.zone == Zone::tune;
        g.setColour (px::cell);
        g.fillRect (b);
        px::frameRect (g, b, on ? px::white : (hot ? px::grey : px::frame), 2);
        auto in = b.reduced (10, 6);
        auto box = in.removeFromLeft (20).withSizeKeepingCentre (20, 20);
        g.setColour (px::black);
        g.fillRect (box);
        px::frameRect (g, box, hot || on ? px::white : px::grey, 2);
        if (on)
        {
            g.setColour (px::white);
            g.fillRect (box.reduced (6));
        }
        in.removeFromLeft (10);
        px::drawText (g, fr ("ACCORDE MON SON !"), in.removeFromTop (in.getHeight() / 2), juce::Justification::bottomLeft,
                      px::label (9.0f), on || hot ? px::white : px::grey);
        const juce::String tuning = ! known ? juce::String ("?")
                                 : tuned == 0 ? fr ("non (batterie)")
                                 : tuned == 17 ? fr ("canal global") : fr ("canal ") + juce::String (tuned);
        px::drawText (g, tuning, in, juce::Justification::centredLeft, px::text (22.0f), on ? px::white : px::dim);
    }

    // the section's name over the screen, as on a pinball display
    {
        static const double slow = juce::jmax (1.0, juce::SystemStats::getEnvironmentVariable ("RA_SLOWMO", "1").getDoubleValue());
        const double t = (now - titleStart) / 1000.0 / slow;     // RA_SLOWMO: slower, for screenshots
        if (t >= 0.0 && t < 0.52)
            paintSectionTitle (g, vizBox, upper (fr (ui::soundSections()[(size_t) section].tab)), t);
    }

    // the wipe: a dither that clears in steps (the quick row stays)
    if (wt < 1.0)
        px::dither (g, soundPanel.reduced (6).withTop (quickRow.getBottom() + 4).translated (-slide, 0), px::panel,
                    3 - (int) (stepped (wt, 3) * 3.0f));
}

// A pinball display title: a white band rushes in from the right with speed streaks,
// the name in black dots on it, a flash, then it breaks up in dithers. Half a second.
void SeqPage::paintSectionTitle (juce::Graphics& g, juce::Rectangle<int> screen, const juce::String& name, double t)
{
    juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (screen);

    const auto f = px::label (juce::jlimit (14.0f, 34.0f, (float) screen.getHeight() * 0.21f), true);
    const int bandH = juce::roundToInt (f.getHeight() * 1.45f);
    const int textW = textWidth (f, name);
    const int y = screen.getCentreY() - bandH / 2;

    // the dot matrix: a black grid over what is lit
    auto dots = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (px::black.withAlpha (0.55f));
        for (int x = r.getX(); x < r.getRight(); x += 4)
            g.fillRect (x, r.getY(), 1, r.getHeight());
        for (int yy = r.getY(); yy < r.getBottom(); yy += 4)
            g.fillRect (r.getX(), yy, r.getWidth(), 1);
    };

    if (t < 0.12)
    {
        // a plate rushes in from the right, speed streaks behind it
        const float in = stepped (t / 0.12, 6);
        const int plateW = textW + 48;
        const int home = screen.getCentreX() - plateW / 2;
        const auto plate = juce::Rectangle<int> (home + juce::roundToInt ((1.0f - in) * (float) (screen.getRight() - home)), y, plateW, bandH);
        g.setColour (px::white);
        const int rows[] = { 2, 7, 12, 17, 22 };
        for (int k = 0; k < 5; ++k)
        {
            const int len = juce::roundToInt ((1.0f - in) * (float) screen.getWidth() * (0.7f - 0.1f * (float) k)) + 12;
            g.fillRect (plate.getRight() + 4 + k * 5, plate.getY() + rows[k] * plate.getHeight() / 24, len, 2);
        }
        g.fillRect (plate);
        px::drawText (g, name, plate, juce::Justification::centred, f, px::black);
        dots (plate);
        return;
    }

    const auto band = juce::Rectangle<int> (screen.getX(), y, screen.getWidth(), bandH);
    if (t < 0.17)
    {
        // landed: a flash, the band across the screen
        g.setColour (px::black);
        g.fillRect (band);
        px::frameRect (g, band, px::white, 3);
        px::drawText (g, name, band, juce::Justification::centred, f, px::white);
        return;
    }
    if (t < 0.38)
    {
        g.setColour (px::white);
        g.fillRect (band);
        px::drawText (g, name, band, juce::Justification::centred, f, px::black);
        dots (band);
        return;
    }
    // breaks up in dithers
    const int level = 3 - (int) ((t - 0.38) / 0.14 * 3.0);
    if (level > 0)
        px::dither (g, band, px::white, level);
}

void SeqPage::paintSequencer (juce::Graphics& g, const Track& tr, double ppq)
{
    const int playing = playingStep (tr.division, tr.activeLength(), ppq);

    px::drawText (g, juce::String (track + 1).paddedLeft ('0', 2) + " " + upper (trackName (tr, track)), nameBox,
                  juce::Justification::centredLeft, px::text (44.0f), px::white);
    for (int i = 0; i < 2; ++i)
    {
        const bool on = (tr.mode == Mode::notes) == (i == 1);
        auto b = modeTabs[(size_t) i];
        px::button (g, b.withTrimmedBottom (2), on, false);
        px::drawText (g, modeNames[i], b.withTrimmedBottom (on ? 6 : 2), juce::Justification::centred, px::label (14.0f),
                      on ? px::black : px::grey);
    }
    auto smallField = [&] (juce::Rectangle<int> r, const juce::String& name, const juce::String& value, Zone z)
    {
        g.setColour (px::cell);
        g.fillRect (r);
        px::frameRect (g, r, hover.zone == z ? px::grey : px::frame, 2);
        auto t = r.reduced (8, 0);
        px::drawText (g, name, t.removeFromLeft (58), juce::Justification::centredLeft, px::label (11.0f), px::dim);
        px::drawText (g, value, t, juce::Justification::centredRight, px::text (32.0f), px::white);
    };
    smallField (lengthBox, "LONG.", juce::String (tr.activeLength()), Zone::length);
    smallField (divisionBox, "VIT.", divisionName (tr.division), Zone::division);
    {
        auto b = muteBox;
        g.setColour (tr.mute ? px::red : px::cell);
        g.fillRect (b);
        if (! tr.mute)
            px::frameRect (g, b, hover.zone == Zone::mute ? px::grey : px::frame, 2);
        px::drawText (g, "MUTE", b, juce::Justification::centred, px::label (13.0f), tr.mute ? px::black : px::grey);
    }
    {
        auto b = optionsBox;
        const bool hot = hover.zone == Zone::options;
        g.setColour (px::cell);
        g.fillRect (b);
        px::frameRect (g, b, hot ? px::white : px::frame, 2);
        auto in = b.reduced (12, 0);
        g.setColour (hot ? px::white : px::dim);
        const int ax = in.getRight() - 8, ay = in.getCentreY() - 2;
        g.fillRect (ax, ay, 8, 2);
        g.fillRect (ax + 2, ay + 2, 4, 2);
        in.removeFromRight (12);
        px::drawText (g, "OPTIONS", in, juce::Justification::centred, px::label (12.0f), hot ? px::white : px::grey);
    }
    for (auto [b, z, label] : { std::tuple { initTrackBox, Zone::initTrack, initTrackText },
                                std::tuple { initAllBox, Zone::initAll, initAllText } })
    {
        const bool hot = hover.zone == z;
        const bool pressed = drag.hit.zone == z;
        g.setColour (pressed ? px::red : px::cell);
        g.fillRect (b);
        if (! pressed)
            px::frameRect (g, b, hot ? px::red : px::frame, 2);
        px::drawText (g, label, b, juce::Justification::centred, px::label (12.0f), pressed ? px::black : hot ? px::white : px::grey);
    }

    paintSteps (g, tr, playing, ppq);
    paintMiniMap (g, tr, playing);

    // hint or selection
    auto h = hintRow;
    if (! selection.empty())
    {
        juce::StringArray nums;
        auto sorted = selection;
        std::sort (sorted.begin(), sorted.end());
        for (auto s : sorted)
            nums.add (juce::String (s + 1));
        auto chipText = fr ("VERROU · PAS ") + nums.joinIntoString (" ");
        auto chip = h.removeFromLeft (juce::jmin (h.getWidth() - 200, textWidth (px::label (14.0f), chipText) + 24));
        g.setColour (px::yellow);
        g.fillRect (chip);
        px::drawText (g, chipText, chip.reduced (10, 0), juce::Justification::centredLeft, px::label (14.0f), px::black);
        h.removeFromLeft (14);
        px::drawText (g, shiftHeld ? fr ("les potards verrouillent ces pas · lâche Maj pour finir") : fr ("les potards du son verrouillent ces pas"),
                      h.withTrimmedRight (180), juce::Justification::centredLeft,
                      px::text (26.0f), px::grey);
        g.setColour (px::cell);
        g.fillRect (clearBox);
        px::frameRect (g, clearBox, hover.zone == Zone::clearSelection ? px::white : px::yellow, 2);
        px::drawText (g, fr ("FINI (ÉCHAP)"), clearBox, juce::Justification::centred, px::label (11.0f), px::yellow);
    }
    else
    {
        auto hint = tr.mode == Mode::notes
            ? fr ("clic = poser · glisser haut/bas sur une note = la changer · Maj + clic = verrou")
            : fr ("clic = poser · glisser = peindre · tirer vers le haut = ratchet · Maj + clic = verrou · clic droit = menu");
        px::drawText (g, hint, h, juce::Justification::centredLeft, px::text (26.0f), px::dim);
    }

    paintTrigRow (g, tr);
}

void SeqPage::paintSteps (juce::Graphics& g, const Track& tr, int playing, double ppq)
{
    const auto now = nowMs();
    const int len = tr.activeLength();
    // where the playhead is inside its step, and how long a step lasts: the trail fades by the clock
    const double spb = stepsPerBeat (tr.division);
    const double frac = ppq >= 0.0 ? ppq * spb - std::floor (ppq * spb) : 0.0;
    const double stepMs = 60000.0 / juce::jmax (1.0, seq.getTempo()) / spb;
    const auto trigs = tr.trigs();
    const double waveT = (now - stopWaveStart) / 360.0;

    for (int i = 0; i < 16; ++i)
    {
        const int idx = page * stepsPerPage + i;
        auto r = stepBoxes[(size_t) i];
        if (idx >= len)
        {
            px::dither (g, r, juce::Colour (0xff1a1a1a), 1);
            continue;
        }
        const auto& s = tr.steps[(size_t) idx];
        const bool on = trigs[(size_t) idx];
        const bool selected = isSelected (idx);
        const bool soft = s.velocity < 64;

        if (selected)
            r.translate (0, 4);
        if (on)
        {
            if (soft)
            {
                g.setColour (px::black);
                g.fillRect (r);
                px::dither (g, r, px::white, 3);
            }
            else
            {
                g.setColour (px::white);
                g.fillRect (r);
                if (! selected)
                {
                    g.setColour (juce::Colour (0xffa8a8a8));
                    g.fillRect (r.withTop (r.getBottom() - 7));
                }
            }
        }
        else
        {
            g.setColour (px::cell);
            g.fillRect (r);
            if (i % 4 == 0)
                px::dither (g, r.reduced (2), px::faint, 1);
        }

        // the step being played: the densest red, under the red line; behind it a short trail that
        // fades in a few hundredths of a second; a wave runs when it stops
        if (playing >= 0 && ! tr.mute && ! on)
        {
            const int back = ((playing - idx) % len + len) % len;
            int level = 0;
            if (back == 0)
                level = 3;
            else
            {
                const double since = (frac + back - 1) * stepMs;     // ms since this step ended
                level = since < 45.0 ? 2 : since < 110.0 ? 1 : 0;
            }
            if (level > 0)
                px::dither (g, r.reduced (3), px::red, level);
        }
        if (waveT >= 0.0 && waveT < 1.0)
        {
            const float pos = (float) waveT * 20.0f - 2.0f;
            if (std::abs ((float) i - pos) < 1.0f)
                px::dither (g, r.reduced (2), on ? px::black : px::white, 2);
        }

        px::frameRect (g, r, selected ? px::yellow : (on ? px::white : (hover.zone == Zone::step && hover.index == i ? px::grey : px::frame)),
                       selected ? 4 : 2);

        // marks: locks (yellow), condition / probability (dithered band), retrigs (ticks)
        const bool dark = on && ! soft;
        if (! s.locks.empty())
        {
            g.setColour (px::yellow);
            g.fillRect (r.getRight() - 16, r.getY() + 6, 10, 10);
        }
        if (s.condB > 0 || s.probability < 100)
            px::dither (g, juce::Rectangle<int> (r.getX() + 6, r.getY() + 6, juce::jmax (4, r.getWidth() - 26), 6), dark ? px::black : px::grey, 3);
        if (s.retrig > 1)
            for (int k = 0; k < s.retrig; ++k)
            {
                g.setColour (dark ? px::black : px::grey);
                g.fillRect (r.getX() + 6 + k * 7, r.getY() + 16, 4, 8);
            }

        auto label = r.reduced (6, 4).withTrimmedBottom (dark && ! selected ? 6 : 0);
        juce::String text = juce::String (idx + 1);
        if (tr.mode == Mode::notes && on)
            text = noteName (s.note);
        const auto textColour = on ? (soft ? px::white : px::black) : juce::Colour (0xff4a4a4a);
        if (soft && on)
        {
            auto t = label.removeFromBottom (24).withWidth (textWidth (px::text (26.0f), text) + 6);
            g.setColour (px::black);
            g.fillRect (t);
            px::drawText (g, text, t.withTrimmedLeft (3), juce::Justification::centredLeft, px::text (26.0f), textColour);
        }
        else
            px::drawText (g, text, label, juce::Justification::bottomLeft, px::text (26.0f), textColour);

        if (idx == playing && ! tr.mute)
        {
            g.setColour (px::red);
            g.fillRect (stepBoxes[(size_t) i].getX() + 4, stepBoxes[(size_t) i].getY() - 10, stepBoxes[(size_t) i].getWidth() - 8, 5);
        }
    }
}

void SeqPage::paintMiniMap (juce::Graphics& g, const Track& tr, int playing)
{
    const int len = tr.activeLength();
    const auto trigs = tr.trigs();
    const int gap = 1, groupGap = 5;
    const float w = (float) (miniMap.getWidth() - 7 * groupGap) / (float) maxSteps;
    for (int i = 0; i < maxSteps; ++i)
    {
        const int x = miniMap.getX() + juce::roundToInt ((float) i * w) + (i / 16) * groupGap;
        juce::Rectangle<int> b (x, miniMap.getY(), juce::jmax (1, juce::roundToInt (w) - gap), miniMap.getHeight());
        if (i >= len)
        {
            g.setColour (juce::Colour (0xff111111));
            g.fillRect (b);
            continue;
        }
        const bool visible = i / stepsPerPage == page;
        juce::Colour c = trigs[(size_t) i] ? (visible ? px::white : px::grey) : (visible ? px::faint : juce::Colour (0xff1f1f1f));
        if (i == playing && ! tr.mute)
            c = px::red;
        g.setColour (c);
        g.fillRect (b);
        if (! tr.steps[(size_t) i].locks.empty() && trigs[(size_t) i])
        {
            g.setColour (px::yellow);
            g.fillRect (b.removeFromTop (4));
        }
    }
    // the page shown above, framed
    const int x0 = miniMap.getX() + juce::roundToInt ((float) (page * stepsPerPage) * w) + page * groupGap;
    const int x1 = miniMap.getX() + juce::roundToInt ((float) ((page + 1) * stepsPerPage) * w) + page * groupGap - gap;
    px::frameRect (g, juce::Rectangle<int> (x0, miniMap.getY(), x1 - x0, miniMap.getHeight()).expanded (3), px::white, 2);
}

void SeqPage::paintTrigRow (juce::Graphics& g, const Track& tr)
{
    const bool active = ! selection.empty();
    for (int i = 0; i < 7; ++i)
    {
        auto b = trigBoxes[(size_t) i];
        const bool usable = active && (i != 6 || tr.mode == Mode::notes);
        g.setColour (px::cell);
        g.fillRect (b);
        px::frameRect (g, b, usable && hover.zone == Zone::trig && hover.index == i ? px::grey : px::frame, 2);
        auto t = b.reduced (10, 0);
        px::drawText (g, upper (fr (trigNames[i])), t.removeFromLeft (t.getWidth() / 2), juce::Justification::centredLeft,
                      px::label (13.0f), usable ? px::grey : px::faint);
        px::drawText (g, usable ? trigText (tr, i) : juce::String ("-"), t, juce::Justification::centredRight, px::text (32.0f),
                      usable ? px::white : px::faint);
    }
}

//==============================================================================
// Animations

void SeqPage::showPopup (int slot, const Track& tr)
{
    if (slot < 0 || slot >= (int) slots.size())
        return;
    const auto& def = slots[(size_t) slot];
    const int v = shownValue (tr, slot);
    const int maxV = paramMax (def.address);
    const bool locked = ! selection.empty();
    popup.name = upper (fr (def.label));
    popup.value = valueText (def.address, v);
    popup.position = (float) v / (float) juce::jmax (1, maxV);
    popup.choice = def.choice;
    popup.centred = ui::isBipolar (def.address);
    popup.colour = locked ? px::yellow : px::white;
    popup.marker = locked && engine.getShownKit().hasSound (track)
                       ? (float) engine.getShownKit().soundValue (track, def.address) / (float) juce::jmax (1, maxV) : -1.0f;
    popup.anchor = paramBoxes[(size_t) slot];
    touchPopup();
}

void SeqPage::touchPopup()
{
    // already up (being turned, or let go a moment ago): it stays as it is, without growing in again
    const double now = nowMs();
    if (! popup.up)
        popup.openedMs = now;
    popup.up = true;
    popup.shownMs = now;
}

void SeqPage::addBurst (juce::Point<int> at, juce::Colour c)
{
    if (bursts.size() > 24)
        bursts.erase (bursts.begin());
    bursts.push_back ({ at, nowMs(), c });
}

void SeqPage::showToast (const juce::String& text, juce::Colour c)
{
    toast = { text, c, nowMs() };
}

void SeqPage::paintOverlays (juce::Graphics& g, double now)
{
    // pixels flying out of a step or a knob
    for (auto it = bursts.begin(); it != bursts.end();)
    {
        const double t = (now - it->startMs) / 420.0;
        if (t >= 1.0)
        {
            it = bursts.erase (it);
            continue;
        }
        const float d = stepped (t, 7) * 38.0f;
        g.setColour (it->colour);
        for (int k = 0; k < 8; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / 8.0f + 0.3f;
            const int s = t < 0.6 ? 6 : 4;
            g.fillRect (it->at.x + juce::roundToInt (d * std::cos (a)) - s / 2, it->at.y + juce::roundToInt (d * std::sin (a)) - s / 2, s, s);
        }
        ++it;
    }

    // a message sliding in at the top right of the sequencer
    {
        const double t = now - toast.startMs;
        if (t >= 0.0 && t < 1300.0 && toast.text.isNotEmpty())
        {
            const auto f = px::label (13.0f);
            const int w = textWidth (f, toast.text) + 30, hgt = 30;
            float in = t < 120.0 ? stepped (t / 120.0, 4) : (t > 1180.0 ? 1.0f - stepped ((t - 1180.0) / 120.0, 4) : 1.0f);
            auto box = juce::Rectangle<int> (seqPanel.getRight() - 16 - juce::roundToInt ((float) w * in), seqPanel.getY() + 14, w, hgt);
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (seqPanel.reduced (3));
            g.setColour (toast.colour);
            g.fillRect (box);
            px::drawText (g, toast.text, box.reduced (14, 0), juce::Justification::centredLeft, f, px::black);
        }
    }

    // the value of the knob being turned, big, as on Elektron screens
    {
        const double t = now - popup.shownMs;
        const bool held = drag.hit.zone == Zone::param;
        popup.up = popup.up && (held || (t >= 0.0 && t < 900.0));
        if (popup.up)
        {
            const int w = 360, h = 150;
            auto box = juce::Rectangle<int> (w, h).withCentre ({ popup.anchor.getCentreX(), popup.anchor.getY() - h / 2 - 8 });
            box = box.constrainedWithin (getLocalBounds().reduced (8));
            // opens in two steps
            const float open = stepped (juce::jmax (0.0, now - popup.openedMs) / 80.0, 2);
            auto shown = box.withSizeKeepingCentre (juce::roundToInt (w * juce::jmax (0.5f, open)), juce::roundToInt (h * juce::jmax (0.5f, open)));
            g.setColour (px::black);
            g.fillRect (shown);
            px::frameRect (g, shown, popup.colour, 3);
            if (open >= 1.0f)
            {
                auto in = shown.reduced (18, 12);
                px::drawText (g, popup.name, in.removeFromTop (26), juce::Justification::centredLeft, px::label (12.0f), px::grey);
                if (! popup.choice)
                {
                    auto barRow = in.removeFromBottom (16);
                    const int mk = popup.marker >= 0.0f ? juce::jlimit (0, 23, juce::roundToInt (popup.marker * 24.0f)) : -1;
                    if (popup.centred)
                        px::barCentred (g, barRow, 24, popup.position, popup.colour, mk);
                    else
                        px::bar (g, barRow, 24, juce::roundToInt (popup.position * 24.0f), popup.colour, mk);
                    in.removeFromBottom (6);
                }
                px::drawText (g, popup.value, in, juce::Justification::centredLeft,
                              px::text (juce::jmin (80.0f, 620.0f / (float) juce::jmax (4, popup.value.length()))), popup.colour);
            }
            // dissolves in dithers at the end
            if (! held && t > 600.0)
                px::dither (g, shown, px::black, 1 + (int) ((t - 600.0) / 100.0));
        }
    }
}

//==============================================================================
// Values

int SeqPage::paramMax (int address) const
{
    auto names = ra::valueNames (ra::Object::sound, address);
    return names.empty() ? 127 : (int) names.size() - 1;
}

int SeqPage::shownValue (const Track& tr, int slot) const
{
    const auto& def = slots[(size_t) slot];
    int v = engine.getShownKit().hasSound (track) ? engine.getShownKit().soundValue (track, def.address) : 0;
    if (! selection.empty())
        if (auto* l = tr.steps[(size_t) selection.front()].findLock (def.address))
            v = l->value;
    return v;
}

void SeqPage::setParam (int slot, int value)
{
    if (slot < 0 || slot >= (int) slots.size())
        return;
    const auto& def = slots[(size_t) slot];
    value = juce::jlimit (0, paramMax (def.address), value);
    if (slot >= (int) params().size())
        stickyQuick = { track, slot - (int) params().size(), def };
    if (selection.empty())
    {
        if (engine.getShownKit().hasSound (track) && engine.getShownKit().soundValue (track, def.address) != value)
            engine.setSoundParameter (track, def.address, value);
    }
    else
    {
        bool first = false;
        seq.edit ([&] (Pattern& p)
        {
            for (auto s : selection)
            {
                auto& step = p.tracks[(size_t) track].steps[(size_t) s];
                first = first || step.findLock (def.address) == nullptr;
                step.setLock (def.address, value);
            }
        });
        if (first)
        {
            excitedUntil = nowMs() + 500.0;
            addBurst (paramBoxes[(size_t) slot].getCentre(), px::yellow);
        }
    }
    Track tr;
    seq.read ([&] (const Pattern& p) { tr = p.tracks[(size_t) track]; });
    showPopup (slot, tr);
}

void SeqPage::clearParamLocks (int slot)
{
    if (slot < 0 || slot >= (int) slots.size())
        return;
    const auto address = slots[(size_t) slot].address;
    seq.edit ([&] (Pattern& p)
    {
        for (auto s : selection)
            p.tracks[(size_t) track].steps[(size_t) s].clearLock (address);
    });
    showToast (fr ("VERROU RETIRÉ"), px::yellow);
}

void SeqPage::showChoices (int slot)
{
    if (slot < 0 || slot >= (int) slots.size())
        return;
    const auto& def = slots[(size_t) slot];
    auto names = ra::valueNames (ra::Object::sound, def.address);
    Track tr;
    seq.read ([&] (const Pattern& p) { tr = p.tracks[(size_t) track]; });
    const int current = shownValue (tr, slot);
    juce::PopupMenu m;
    for (int i = 0; i < (int) names.size(); ++i)
        m.addItem (i + 1, juce::String::fromUTF8 (names[(size_t) i].c_str()), true, i == current);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (paramBoxes[(size_t) slot])),
                     [safe = juce::Component::SafePointer<SeqPage> (this), slot] (int r)
    {
        if (safe != nullptr && r > 0)
            safe->setParam (slot, r - 1);
    });
}

int SeqPage::trigValue (const Track& tr, int field) const
{
    if (selection.empty())
        return 0;
    const auto& s = tr.steps[(size_t) selection.front()];
    switch (field)
    {
        case 0: return s.velocity;
        case 1: return s.length;
        case 2: return s.micro;
        case 3: return s.probability;
        case 4:
        {
            auto c = conditions();
            for (size_t i = 0; i < c.size(); ++i)
                if (c[i].first == (s.condB == 0 ? 0 : s.condA) && c[i].second == s.condB)
                    return (int) i;
            return 0;
        }
        case 5: return s.retrig;
        case 6: return s.note;
        default: return 0;
    }
}

juce::String SeqPage::trigText (const Track& tr, int field) const
{
    const int v = trigValue (tr, field);
    switch (field)
    {
        case 1:
        {
            static const char* eighths[8] = { "", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8" };
            if (v < 8) return eighths[v];
            return juce::String (v / 8.0, v % 8 == 0 ? 0 : 2).trimCharactersAtEnd ("0");
        }
        case 2: return v > 0 ? "+" + juce::String (v) : juce::String (v);
        case 3: return juce::String (v) + "%";
        case 4:
        {
            auto c = conditions()[(size_t) v];
            return c.second == 0 ? juce::String ("1:1") : juce::String (c.first) + ":" + juce::String (c.second);
        }
        case 5: return v <= 1 ? juce::String ("NON") : "x" + juce::String (v);
        case 6: return noteName (v);
        default: return juce::String (v);
    }
}

void SeqPage::setTrigValue (int field, int value)
{
    if (selection.empty())
        return;
    seq.edit ([&] (Pattern& p)
    {
        for (auto idx : selection)
        {
            auto& s = p.tracks[(size_t) track].steps[(size_t) idx];
            switch (field)
            {
                case 0: s.velocity = (std::uint8_t) juce::jlimit (1, 127, value); break;
                case 1: s.length = (std::uint8_t) juce::jlimit (1, 64, value); break;
                case 2: s.micro = (std::int8_t) juce::jlimit (-11, 11, value); break;
                case 3: s.probability = (std::uint8_t) juce::jlimit (0, 100, value); break;
                case 4:
                {
                    auto c = conditions();
                    auto pick = c[(size_t) juce::jlimit (0, (int) c.size() - 1, value)];
                    s.condA = (std::uint8_t) pick.first;
                    s.condB = (std::uint8_t) pick.second;
                    break;
                }
                case 5: s.retrig = (std::uint8_t) juce::jlimit (1, 8, value); break;
                case 6: s.note = (std::uint8_t) juce::jlimit (0, 127, value); lastNote = s.note; break;
                default: break;
            }
        }
    });
}

//==============================================================================
// Mouse and keys

void SeqPage::mouseMove (const juce::MouseEvent& e)
{
    auto h = zoneAt (e.getPosition());
    if (h.zone != hover.zone || h.index != hover.index)
    {
        hover = h;
        const bool draggable = h.zone == Zone::tempo || h.zone == Zone::swing || h.zone == Zone::length
                            || h.zone == Zone::trig || h.zone == Zone::param;
        setMouseCursor (draggable ? juce::MouseCursor::UpDownResizeCursor
                                  : (h.zone == Zone::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor));
    }
}

void SeqPage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    drag = {};
    drag.hit = zoneAt (e.getPosition());
    drag.startY = e.getPosition().y;
    const bool right = e.mods.isPopupMenu();
    const bool shift = e.mods.isShiftDown();

    Track tr;
    double tempo = 120.0;
    int swing = 50;
    seq.read ([&] (const Pattern& p) { tr = p.tracks[(size_t) track]; tempo = p.tempo; swing = p.swing; });

    switch (drag.hit.zone)
    {
        case Zone::play:
            if (! seq.isFollowingHost())
                seq.togglePlay();
            break;
        case Zone::tempo:      drag.startValue = juce::roundToInt (tempo * 10.0); break;
        case Zone::swing:      drag.startValue = swing; break;
        case Zone::voice:
            if (right)
                showVoiceMenu (drag.hit.index);
            else if (e.mods.isAltDown())
                seq.edit ([&] (Pattern& p) { auto& m = p.tracks[(size_t) drag.hit.index].mute; m = ! m; });
            else
                selectTrack (drag.hit.index);
            break;
        case Zone::section:
            if (drag.hit.index != section)
            {
                section = drag.hit.index;
                startWipe();
                titleStart = nowMs();
            }
            break;
        case Zone::listen:     engine.playSound (track); break;
        case Zone::tune:       toggleTuned(); break;
        case Zone::param:
            if (right)
            {
                if (! selection.empty())
                    clearParamLocks (drag.hit.index);
                drag.hit = {};
                break;
            }
            drag.startValue = shownValue (tr, drag.hit.index);
            showPopup (drag.hit.index, tr);
            break;
        case Zone::mode:
        {
            const auto m = drag.hit.index == 1 ? Mode::notes : Mode::steps;
            if (m != tr.mode)
            {
                seq.edit ([&] (Pattern& p) { p.tracks[(size_t) track].mode = m; });
                showToast (m == Mode::notes ? fr ("MODE NOTES") : fr ("MODE PAS"));

                // notes only change the pitch of a tuned sound
                const auto& kit = engine.getKit();
                if (m == Mode::notes && kit.hasSound (track) && kit.soundValue (track, 0x58) == 0)
                    showToast (fr ("SON PAS ACCORDÉ : COCHE ACCORDE MON SON !"), px::yellow);
            }
            break;
        }
        case Zone::length:     drag.startValue = tr.activeLength(); break;
        case Zone::division:
            seq.edit ([&] (Pattern& p)
            {
                auto& d = p.tracks[(size_t) track].division;
                d = (Division) (((int) d + (right ? numDivisions - 1 : 1)) % numDivisions);
            });
            break;
        case Zone::mute:       seq.edit ([&] (Pattern& p) { auto& m = p.tracks[(size_t) track].mute; m = ! m; }); break;
        case Zone::options:    showOptions(); break;
        case Zone::initTrack:  initTracks (false); break;
        case Zone::initAll:    initTracks (true); break;
        case Zone::miniMap:
        {
            const int i = juce::jlimit (0, maxSteps - 1, (e.getPosition().x - miniMap.getX()) * maxSteps / juce::jmax (1, miniMap.getWidth()));
            if (i < tr.activeLength() && i / stepsPerPage != page)
            {
                page = i / stepsPerPage;
                showToast ("PAGE " + juce::String (page + 1) + "/" + juce::String ((tr.activeLength() + stepsPerPage - 1) / stepsPerPage));
            }
            break;
        }
        case Zone::step:
        {
            const int idx = page * stepsPerPage + drag.hit.index;
            if (idx >= tr.activeLength())
                break;
            if (right)
            {
                showStepMenu (idx);
                break;
            }
            if (shift)
            {
                toggleSelection (idx);
                shiftHeld = ! selection.empty();
                drag.hit = {};
                break;
            }
            const bool wasOn = tr.isOn (idx);
            drag.lastStep = idx;
            if (tr.mode == Mode::notes && wasOn)
            {
                // decided on mouse up: a click switches it off, a drag changes the note
                drag.noteDrag = true;
                drag.startValue = tr.steps[(size_t) idx].note;
                break;
            }
            drag.paintOn = ! wasOn;
            drag.startX = e.getPosition().x;
            drag.ratchetBase = juce::jmax (1, (int) tr.steps[(size_t) idx].retrig);
            if (wasOn)
            {
                // switched off on release, unless the drag makes ratchets or paints
                drag.pendingOff = true;
                break;
            }
            seq.edit ([&] (Pattern& p)
            {
                auto& t = p.tracks[(size_t) track];
                if (t.mode == Mode::euclid)
                    t.printEuclid();
                auto& s = t.steps[(size_t) idx];
                s.on = true;
                if (t.mode == Mode::notes)
                    s.note = (std::uint8_t) lastNote;
            });
            addBurst (stepBoxes[(size_t) drag.hit.index].getCentre(), px::white);
            break;
        }
        case Zone::trig:
            if (selection.empty() || (drag.hit.index == 6 && tr.mode != Mode::notes))
            {
                drag.hit = {};
                break;
            }
            drag.startValue = trigValue (tr, drag.hit.index);
            if (drag.hit.index == 4)
                setTrigValue (4, right ? 0 : (drag.startValue + 1) % (int) conditions().size());
            break;
        case Zone::clearSelection: selection.clear(); break;
        case Zone::none:
            if (soundPanel.getUnion (seqPanel).contains (e.getPosition()))
                selection.clear();
            break;
    }
    repaint();
}

void SeqPage::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = drag.startY - e.getPosition().y;
    if (e.getDistanceFromDragStart() > 3)
        drag.moved = true;

    switch (drag.hit.zone)
    {
        case Zone::tempo:
        {
            const bool fine = e.mods.isCommandDown() || e.mods.isCtrlDown();     // ⌘ or Ctrl: fine steps (Shift is for locks)
            const double t = juce::jlimit (30.0, 300.0, (drag.startValue + dy * (fine ? 1 : 5)) / 10.0);
            seq.edit ([&] (Pattern& p) { p.tempo = std::round (t * (fine ? 10.0 : 2.0)) / (fine ? 10.0 : 2.0); });
            break;
        }
        case Zone::swing:
        {
            const int v = juce::jlimit (50, 75, drag.startValue + dy / 4);
            seq.edit ([&] (Pattern& p) { p.swing = (std::uint8_t) v; });
            break;
        }
        case Zone::length:
        {
            const int v = juce::jlimit (1, maxSteps, drag.startValue + dy / 3);
            seq.edit ([&] (Pattern& p)
            {
                auto& t = p.tracks[(size_t) track];
                if (t.mode == Mode::euclid)
                    t.printEuclid();
                t.length = (std::uint8_t) v;
            });
            page = juce::jmin (page, (v - 1) / stepsPerPage);
            selection.erase (std::remove_if (selection.begin(), selection.end(), [v] (int s) { return s >= v; }), selection.end());
            break;
        }
        case Zone::param:
        {
            // up / down turns the knob from where it is: the whole range in about 200 pixels
            // (finer with Shift), a list one choice per 12 pixels
            const int maxV = paramMax (slots[(size_t) drag.hit.index].address);
            const double fine = e.mods.isCommandDown() || e.mods.isCtrlDown() ? 0.25 : 1.0;   // ⌘ or Ctrl: fine steps
            const double delta = maxV >= 24 ? dy * maxV / 200.0 * fine : dy / 12.0;
            if (drag.moved)
                setParam (drag.hit.index, drag.startValue + juce::roundToInt (delta));
            break;
        }
        case Zone::step:
        {
            if (drag.lastStep < 0)
                break;
            if (drag.noteDrag)
            {
                // a semitone per 6 pixels
                const int note = juce::jlimit (0, 127, drag.startValue + dy / 6);
                const int idx = drag.lastStep;
                seq.edit ([&] (Pattern& p) { p.tracks[(size_t) track].steps[(size_t) idx].note = (std::uint8_t) note; });
                lastNote = note;
                break;
            }
            // the first clear move decides: up or down on the step = ratchets (IMPAKT's gesture), across = painting
            const int dx = e.getPosition().x - drag.startX;
            auto h = zoneAt (e.getPosition());
            const int under = h.zone == Zone::step ? page * stepsPerPage + h.index : -1;
            if (! drag.ratchet && ! drag.painting)
            {
                if (std::abs (dy) >= 8 && std::abs (dy) > std::abs (dx))
                    drag.ratchet = true;
                else if (under >= 0 && under != drag.lastStep)
                    drag.painting = true;
                else
                    break;
            }
            if (drag.ratchet)
            {
                // a hit more every 14 pixels up, back down to where it was (and to 1)
                const int idx = drag.lastStep, r = juce::jlimit (1, 8, drag.ratchetBase + dy / 14);
                bool changed = false;
                seq.edit ([&] (Pattern& p)
                {
                    auto& s = p.tracks[(size_t) track].steps[(size_t) idx];
                    s.on = true;
                    changed = s.retrig != r;
                    s.retrig = (std::uint8_t) r;
                });
                drag.pendingOff = false;
                if (changed)
                    showToast (r > 1 ? "RETRIG x" + juce::String (r) : fr ("RETRIG : NON"), r > 1 ? px::white : px::grey);
                break;
            }
            if (drag.pendingOff)
            {
                // painting from a trig: it goes off first, like the ones crossed
                const int idx = drag.lastStep;
                seq.edit ([&] (Pattern& p)
                {
                    auto& t = p.tracks[(size_t) track];
                    if (t.mode == Mode::euclid)
                        t.printEuclid();
                    t.steps[(size_t) idx].on = false;
                });
                drag.pendingOff = false;
            }
            // paint the same state over the steps the mouse crosses
            if (h.zone == Zone::step)
            {
                const int idx = page * stepsPerPage + h.index;
                bool changed = false;
                seq.edit ([&] (Pattern& p)
                {
                    auto& t = p.tracks[(size_t) track];
                    if (idx >= t.activeLength())
                        return;
                    auto& s = t.steps[(size_t) idx];
                    if (s.on != drag.paintOn)
                    {
                        s.on = drag.paintOn;
                        changed = true;
                        if (s.on && t.mode == Mode::notes)
                            s.note = (std::uint8_t) lastNote;
                    }
                });
                if (changed && drag.paintOn)
                    addBurst (stepBoxes[(size_t) h.index].getCentre(), px::white);
            }
            break;
        }
        case Zone::trig:
        {
            if (drag.hit.index == 4)
                break;
            const int perUnit = drag.hit.index == 0 || drag.hit.index == 3 ? 2 : 6;
            setTrigValue (drag.hit.index, drag.startValue + dy / perUnit);
            break;
        }
        case Zone::none: case Zone::play: case Zone::voice: case Zone::section: case Zone::listen: case Zone::tune:
        case Zone::mode: case Zone::division: case Zone::mute: case Zone::options: case Zone::initTrack: case Zone::initAll:
        case Zone::miniMap: case Zone::clearSelection:
            break;
    }
    repaint();
}

void SeqPage::mouseUp (const juce::MouseEvent&)
{
    if (drag.hit.zone == Zone::step && drag.pendingOff && drag.lastStep >= 0)
    {
        const int idx = drag.lastStep;
        seq.edit ([&] (Pattern& p)
        {
            auto& t = p.tracks[(size_t) track];
            if (t.mode == Mode::euclid)
                t.printEuclid();
            t.steps[(size_t) idx].on = false;
        });
    }
    if (drag.hit.zone == Zone::step && drag.noteDrag && ! drag.moved && drag.lastStep >= 0)
    {
        const int idx = drag.lastStep;
        seq.edit ([&] (Pattern& p) { p.tracks[(size_t) track].steps[(size_t) idx].on = false; });
    }
    if (drag.hit.zone == Zone::param && ! drag.moved && drag.hit.index >= 0 && drag.hit.index < (int) slots.size()
        && slots[(size_t) drag.hit.index].choice)
        showChoices (drag.hit.index);
    if (drag.hit.zone == Zone::param)
        touchPopup();                  // stays a moment, then dissolves
    drag = {};
    repaint();
}

void SeqPage::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto h = zoneAt (e.getPosition());
    if (h.zone == Zone::voice)
        rename (h.index);
    else if (nameBox.contains (e.getPosition()))
        rename (track);
    else if (h.zone == Zone::param && ! selection.empty())
        clearParamLocks (h.index);
}

void SeqPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int dir = (w.deltaY + w.deltaX) > 0 ? 1 : ((w.deltaY + w.deltaX) < 0 ? -1 : 0);
    if (dir == 0)
        return;
    auto h = zoneAt (e.getPosition());
    Track tr;
    seq.read ([&] (const Pattern& p) { tr = p.tracks[(size_t) track]; });
    switch (h.zone)
    {
        case Zone::tempo:  seq.edit ([&] (Pattern& p) { p.tempo = juce::jlimit (30.0, 300.0, p.tempo + dir); }); break;
        case Zone::swing:  seq.edit ([&] (Pattern& p) { p.swing = (std::uint8_t) juce::jlimit (50, 75, p.swing + dir); }); break;
        case Zone::trig:
            if (! selection.empty())
                setTrigValue (h.index, trigValue (tr, h.index) + dir);
            break;
        case Zone::param:
            setParam (h.index, shownValue (tr, h.index) + dir);     // which keeps the value up
            break;
        case Zone::none: case Zone::play: case Zone::voice: case Zone::section: case Zone::listen: case Zone::mode: case Zone::tune:
        case Zone::length: case Zone::division: case Zone::mute: case Zone::options: case Zone::initTrack: case Zone::initAll:
        case Zone::miniMap: case Zone::step: case Zone::clearSelection:
            break;
    }
    repaint();
}

bool SeqPage::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        selection.clear();
        repaint();
        return true;
    }
    if (k == juce::KeyPress::spaceKey)
    {
        if (! seq.isFollowingHost())
            seq.togglePlay();
        return true;
    }
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey)
    {
        int len = 16;
        seq.read ([&] (const Pattern& p) { len = p.tracks[(size_t) track].activeLength(); });
        const int pages = (len + stepsPerPage - 1) / stepsPerPage;
        const int next = juce::jlimit (0, pages - 1, page + (k == juce::KeyPress::rightKey ? 1 : -1));
        if (next != page)
        {
            page = next;
            showToast ("PAGE " + juce::String (page + 1) + "/" + juce::String (pages));
        }
        repaint();
        return true;
    }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        selectTrack (track + (k == juce::KeyPress::downKey ? 1 : -1));
        return true;
    }
    return false;
}

//==============================================================================
// Menus and renaming

void SeqPage::showVoiceMenu (int voice)
{
    bool muted = false;
    seq.read ([&] (const Pattern& p) { muted = p.tracks[(size_t) voice].mute; });
    juce::PopupMenu m;
    m.addItem (1, muted ? "Remettre le son" : "Couper (mute)");
    m.addItem (2, "Renommer");
    m.addSeparator();
    m.addItem (3, "Copier la piste");
    m.addItem (4, "Coller la piste", hasClipboard);
    m.addItem (5, "Effacer la piste");
    m.addSeparator();
    m.addItem (6, fr ("Écouter le son"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SeqPage> (this), voice] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r == 0)
            return;
        auto& s = self->seq;
        switch (r)
        {
            case 1: s.edit ([&] (Pattern& p) { auto& mu = p.tracks[(size_t) voice].mute; mu = ! mu; }); break;
            case 2: self->rename (voice); break;
            case 3:
                s.read ([&] (const Pattern& p) { self->clipboard = p.tracks[(size_t) voice]; });
                self->hasClipboard = true;
                self->showToast (fr ("PISTE COPIÉE"));
                break;
            case 4:
                s.edit ([&] (Pattern& p) { auto name = p.tracks[(size_t) voice].name; p.tracks[(size_t) voice] = self->clipboard; p.tracks[(size_t) voice].name = name; });
                self->showToast (fr ("PISTE COLLÉE"));
                break;
            case 5:
                self->keepForUndo (fr ("effacer la piste"));
                s.edit ([&] (Pattern& p) { auto name = p.tracks[(size_t) voice].name; p.tracks[(size_t) voice] = Track(); p.tracks[(size_t) voice].name = name; });
                self->showToast (fr ("PISTE EFFACÉE"), px::red);
                break;
            case 6: self->engine.playSound (voice); break;
            default: break;
        }
        if (voice == self->track)
            self->selection.clear();
        self->repaint();
    });
}

void SeqPage::keepForUndo (const juce::String& what)
{
    undo = std::make_unique<Pattern>();
    seq.read ([&] (const Pattern& p) { *undo = p; });
    undoWhat = what;
}

int SeqPage::importInternal (const ra::Kit& kit, bool all)
{
    keepForUndo (fr ("patterns internes"));
    int filled = 0, approximate = 0;
    std::vector<int> swings;
    seq.edit ([&] (Pattern& p)
    {
        for (int i = 0; i < numTracks; ++i)
        {
            if (! kit.hasPattern (i))
                continue;
            auto ip = ra::InternalPattern::fromData (kit.patternData (i));
            if (! ip || (ip->isEmpty() && ! all))
                continue;
            bool approx = false;
            ra::internalToTrack (*ip, p.tracks[(size_t) i], &approx);
            approximate += approx ? 1 : 0;
            if (! ip->isEmpty())
            {
                ++filled;
                swings.push_back (ip->swing);
            }
        }
        // one swing for the whole pattern here: the most used one
        if (! swings.empty())
        {
            std::array<int, 6> count {};
            for (auto sw : swings) ++count[(size_t) juce::jlimit (0, 5, sw)];
            const auto most = (int) (std::max_element (count.begin(), count.end()) - count.begin());
            p.swing = (std::uint8_t) ra::swingPercent (most);
        }
    });
    selection.clear();
    startWipe();
    repaint();
    showToast (filled > 0 ? fr ("PATTERNS INTERNES : ") + juce::String (filled) + fr (" PISTES") : fr ("AUCUN PATTERN DANS CE KIT"),
               filled > 0 ? px::white : px::red);
    if (onStatus)
        onStatus (filled > 0 ? juce::String (filled) + fr (" pistes remplies avec les patterns internes du kit")
                               + (approximate > 0 ? fr (" (") + juce::String (approximate) + fr (" avec une valeur de pas approchée)") : juce::String())
                               + fr (". OPTIONS › Annuler pour revenir.")
                             : fr ("Ce kit n'a pas de pattern interne."), filled > 0);
    return filled;
}

void SeqPage::patternsRead (bool ok)
{
    if (! waitingForPatterns)
        return;
    waitingForPatterns = false;
    if (ok)
        importInternal (engine.getKit(), true);
}

void SeqPage::sendInternal (bool all)
{
    if (engine.isBusy())
    {
        showToast (fr ("LA MACHINE EST OCCUPÉE"), px::red);
        return;
    }
    const auto& kit = engine.getKit();
    std::vector<std::pair<int, ra::Bytes>> out;
    seq.read ([&] (const Pattern& p)
    {
        for (int i = 0; i < numTracks; ++i)
            if (all || i == track)
            {
                // the sound's Pattern Mode stays as it is (Son › Sortie › PAT.INT)
                const int mode = kit.hasSound (i) ? kit.soundValue (i, 0x6C) : 0;
                auto ip = ra::trackToInternal (p.tracks[(size_t) i], mode);
                ip.swing = ra::internalSwing (p.swing);
                if (kit.hasPattern (i))
                    if (auto old = ra::InternalPattern::fromData (kit.patternData (i)))
                    {
                        ip.group = old->group;           // the unit's own settings stay
                        ip.startWhen = old->startWhen;
                        ip.startWhere = old->startWhere;
                    }
                out.push_back ({ i, ip.toData() });
            }
    });
    engine.sendPatterns (out);
    showToast (all ? fr ("SÉQUENCE → PATTERNS INTERNES") : fr ("PISTE → PATTERN INTERNE"), px::yellow);
}

void SeqPage::initTracks (bool all)
{
    // the steps emptied, 16 steps at 1/16 in steps mode; the names (and the tempo) stay. OPTIONS > Annuler brings it back
    keepForUndo (all ? fr ("init séquence") : fr ("init piste"));
    const int t = track;
    seq.edit ([&] (Pattern& p)
    {
        for (int i = 0; i < numTracks; ++i)
            if (all || i == t)
                ra::seq::resetTrack (p.tracks[(size_t) i]);
    });
    selection.clear();
    showToast (all ? fr ("SÉQUENCE INITIALISÉE") : fr ("PISTE INITIALISÉE"), px::red);
    startWipe();
    addBurst ((all ? initAllBox : initTrackBox).getCentre(), px::red);
    repaint();
}

void SeqPage::showOptions()
{
    juce::String name;
    seq.read ([&] (const Pattern& p) { name = trackName (p.tracks[(size_t) track], track); });
    juce::PopupMenu m;
    m.addSectionHeader ("PISTE " + juce::String (track + 1).paddedLeft ('0', 2) + "  " + upper (name));
    m.addItem (1, fr ("Effacer les pas"));
    m.addItem (2, fr ("Effacer les verrous"));
    m.addItem (3, fr ("Init piste : vide, 16 pas, 1/16"));
    juce::PopupMenu fill;
    fill.addItem (10, fr ("Un pas sur 2"));
    fill.addItem (11, fr ("Un pas sur 4"));
    fill.addItem (12, fr ("Un pas sur 8"));
    m.addSubMenu ("Remplir", fill);
    m.addItem (4, fr ("Décaler d'un pas vers la gauche"));
    m.addItem (5, fr ("Décaler d'un pas vers la droite"));
    m.addItem (6, "Copier la piste");
    m.addItem (7, "Coller la piste", hasClipboard);
    m.addSectionHeader (fr ("SÉQUENCE"));
    m.addItem (20, fr ("Effacer tous les pas"));
    m.addItem (21, fr ("Effacer tous les verrous"));
    m.addItem (22, fr ("Init séquence : tout vide, 16 pas, 1/16 (le tempo reste)"));
    m.addSectionHeader (fr ("PATTERNS INTERNES DE LA MACHINE"));
    const int kitPatterns = engine.getKit().patternsRead();
    m.addItem (40, kitPatterns == 24 ? fr ("Patterns internes du kit → séquence") : fr ("Lire les patterns internes → séquence"));
    m.addItem (41, fr ("Cette piste → pattern interne du son"));
    m.addItem (42, fr ("Toute la séquence → patterns internes"));
    m.addSeparator();
    m.addItem (30, undo != nullptr ? fr ("Annuler : ") + undoWhat : fr ("Annuler"), undo != nullptr);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (optionsBox)),
                     [safe = juce::Component::SafePointer<SeqPage> (this)] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r == 0)
            return;
        const int t = self->track;
        auto onTrack = [self, t] (const juce::String& what, std::function<void (Track&)> fn)
        {
            self->keepForUndo (what);
            self->seq.edit ([&] (Pattern& p) { fn (p.tracks[(size_t) t]); });
        };
        auto onAll = [self] (const juce::String& what, std::function<void (Track&)> fn)
        {
            self->keepForUndo (what);
            self->seq.edit ([&] (Pattern& p) { for (auto& tr : p.tracks) fn (tr); });
        };
        switch (r)
        {
            case 1:  onTrack (fr ("effacer les pas"), [] (Track& tr) { ra::seq::clearSteps (tr); });
                     self->showToast (fr ("PAS EFFACÉS"), px::red); break;
            case 2:  onTrack (fr ("effacer les verrous"), [] (Track& tr) { ra::seq::clearLocks (tr); });
                     self->showToast (fr ("VERROUS EFFACÉS"), px::yellow); break;
            case 3:  self->initTracks (false); break;
            case 4:  onTrack (fr ("décaler"), [] (Track& tr) { ra::seq::shiftSteps (tr, -1); }); self->showToast (fr ("<< DÉCALÉ")); break;
            case 5:  onTrack (fr ("décaler"), [] (Track& tr) { ra::seq::shiftSteps (tr, 1); }); self->showToast (fr ("DÉCALÉ >>")); break;
            case 6:
                self->seq.read ([&] (const Pattern& p) { self->clipboard = p.tracks[(size_t) t]; });
                self->hasClipboard = true;
                self->showToast (fr ("PISTE COPIÉE"));
                break;
            case 7:
                onTrack (fr ("coller la piste"), [self] (Track& tr) { auto n = tr.name; tr = self->clipboard; tr.name = n; });
                self->showToast (fr ("PISTE COLLÉE"));
                break;
            case 10: case 11: case 12:
            {
                const int every = r == 10 ? 2 : r == 11 ? 4 : 8;
                onTrack (fr ("remplir"), [every] (Track& tr) { ra::seq::fillEvery (tr, every); });
                self->showToast (fr ("REMPLI : 1 SUR ") + juce::String (every));
                break;
            }
            case 20: onAll (fr ("effacer tous les pas"), [] (Track& tr) { ra::seq::clearSteps (tr); });
                     self->showToast (fr ("TOUT EFFACÉ"), px::red); break;
            case 21: onAll (fr ("effacer tous les verrous"), [] (Track& tr) { ra::seq::clearLocks (tr); });
                     self->showToast (fr ("TOUS LES VERROUS EFFACÉS"), px::yellow); break;
            case 22: self->initTracks (true); break;
            case 40:
                if (self->engine.getKit().patternsRead() == 24)
                    self->importInternal (self->engine.getKit(), true);
                else if (self->engine.isBusy())
                    self->showToast (fr ("LA MACHINE EST OCCUPÉE"), px::red);
                else
                {
                    self->waitingForPatterns = true;
                    self->engine.readPatterns();
                    self->showToast (fr ("LECTURE DES PATTERNS…"));
                }
                break;
            case 41: self->sendInternal (false); break;
            case 42: self->sendInternal (true); break;
            case 30:
                if (self->undo != nullptr)
                {
                    auto back = std::move (self->undo);
                    self->seq.edit ([&] (Pattern& p) { p = *back; });
                    self->showToast (fr ("ANNULÉ"));
                }
                break;
            default: break;
        }
        self->selection.clear();
        self->startWipe();
        self->repaint();
    });
}

void SeqPage::showStepMenu (int step)
{
    juce::PopupMenu m;
    m.addItem (1, isSelected (step) ? fr ("Retirer de la sélection") : fr ("Choisir pour verrouiller (Maj + clic)"));
    m.addItem (2, "Effacer ses verrous");
    m.addItem (3, fr ("Remettre le pas à zéro"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<SeqPage> (this), step] (int r)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || r == 0)
            return;
        const int t = self->track;
        if (r == 1)
            self->toggleSelection (step);
        else if (r == 2)
            self->seq.edit ([&] (Pattern& p) { p.tracks[(size_t) t].steps[(size_t) step].locks.clear(); });
        else if (r == 3)
            self->seq.edit ([&] (Pattern& p)
            {
                auto& s = p.tracks[(size_t) t].steps[(size_t) step];
                const bool on = s.on;
                s = Step();
                s.on = on;
            });
        self->repaint();
    });
}

void SeqPage::rename (int voice)
{
    juce::String current;
    seq.read ([&] (const Pattern& p) { current = juce::String::fromUTF8 (p.tracks[(size_t) voice].name.c_str()); });
    nameEditor = std::make_unique<juce::TextEditor>();
    auto* ed = nameEditor.get();
    ed->setFont (px::text (28.0f));
    ed->setText (current, false);
    ed->setTextToShowWhenEmpty ("SON " + juce::String (voice + 1).paddedLeft ('0', 2), px::dim);
    ed->setInputRestrictions (14);
    ed->setColour (juce::TextEditor::backgroundColourId, px::black);
    ed->setColour (juce::TextEditor::outlineColourId, px::white);
    ed->setBounds (voices[(size_t) voice].reduced (3));
    addAndMakeVisible (ed);
    ed->selectAll();
    ed->grabKeyboardFocus();
    auto finish = [this, voice] (bool keep)
    {
        if (nameEditor == nullptr)
            return;
        if (keep)
        {
            auto text = nameEditor->getText().trim().toStdString();
            if ((int) text.size() > maxNameBytes)
                text.resize ((size_t) maxNameBytes);
            seq.edit ([&] (Pattern& p) { p.tracks[(size_t) voice].name = text; });
        }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SeqPage> (this)]
        {
            if (safe != nullptr)
            {
                safe->nameEditor.reset();
                safe->grabKeyboardFocus();
            }
        });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onFocusLost = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };
}
