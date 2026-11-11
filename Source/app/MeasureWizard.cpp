#include "MeasureWizard.h"

#include "Pixel.h"

namespace
{
    //==========================================================================
    // Pixel art: '#' white, '+' grey, '-' dim, 'k' black, anything else is see-through.
    using Sprite = std::vector<const char*>;

    void drawSprite (juce::Graphics& g, const Sprite& s, int x, int y, int p)
    {
        for (size_t r = 0; r < s.size(); ++r)
            for (int c = 0; s[r][c] != 0; ++c)
            {
                juce::Colour col;
                switch (s[r][c])
                {
                    case '#': col = px::white; break;
                    case '+': col = px::grey; break;
                    case '-': col = px::dim; break;
                    case 'k': col = px::black; break;
                    default: continue;
                }
                g.setColour (col);
                g.fillRect (x + c * p, y + (int) r * p, p, p);
            }
    }

    const Sprite ghostBody {
        ".....######.....",
        "...##########...",
        "..############..",
        ".##############.",
        ".##############.",
        ".##############.",
        "################",
        "################",
        "################",
        "################",
        "################",
        "###############+",
        "##############++",
        "#############+++",
    };
    const Sprite skirtA { "##.####..####.##", "#...##....##...#" };
    const Sprite skirtB { "#.####..####.###", "...##....##...##" };

    const std::array<Sprite, 8> objects {{
        { "...+...+....", "....+...+...", "...+...+....", "............", "#########...", "#-------###.",
          "#-------#..#", "#-------#..#", "#-------###.", ".#-----#....", "..#####.....", "############" },     // cup
        { "..######....", "..#++++#....", "..######....", "..#----#....", "..#----#....", "..#----#....",
          "..#----#....", "..#----##...", ".#------##..", "#---------#.", "#--------##.", ".########..." },     // sock
        { "..........#.", ".........##.", ".........#+.", "........#++.", "........#++.", ".......#++#.",
          "#.....#++#..", "##...#++#...", ".##.#++##...", "..###+##....", "...####.....", "............" },     // banana
        { ".##......##.", "#++#....#++#", ".#.######.#.", "..#------#..", ".#---#----#.", ".#---#----#.",
          ".#---###--#.", ".#--------#.", "..#------#..", "...######...", "..#......#..", ".##......##." },     // alarm clock
        { ".....#......", "..#..#..#...", "...#.#.#....", "#...###...#.", ".#..#.#..#..", "..##..#.##..",
          "......#.....", "..########..", "...#++++#...", "...#++++#...", "...#++++#...", "....####...." },     // plant
        { "............", "....####....", "...#----#...", "...#-k--#++.", "...#----#+++", "....#--#....",
          ".##.#---#...", "#--#-----#..", "#---------#.", "#---------#.", ".#-------#..", "..#######..." },     // duck
        { "............", "############", "#----------#", "#-########-#", "#-#+#..#+#-#", "#-########-#",
          "#----------#", "#--######--#", "#-#......#-#", "############", "............", "............" },     // cassette
        { "............", "............", "############", "#----------#", "#-######-+-#", "#-#kkkk#---#",
          "#-######-+-#", "#----------#", "#-+-+-+-+--#", "############", ".#........#.", "............" },     // a midronome?
    }};

    const Sprite loupe { "..#####..", ".#-----#.", "#-------#", "#-------#", "#-------#", ".#-----#.", "..#####+.", ".......++", "........+" };
    const Sprite ruler { "##############", "#+#+#+#+#+#+##", "##############" };
    const Sprite tuningFork { "#...#", "#...#", "#...#", "#...#", ".###.", "..#..", "..#..", "..#.." };

    int kindOf (int i)                 { return i % 9 == 5 ? 7 : (i * 5 + 2) % 7; }

    juce::String readoutOf (int i)
    {
        static const char* const texts[8][3] = {
            { "25 CL", "1 CAFE", "440 HZ" },
            { "TAILLE 43", "1 SUR 2", "ODEUR 7/10" },
            { "1 BANANE", "120 BPM", "COURBE 31" },
            { "7 H 00", "60 BPM", "EN RETARD" },
            { "SOIF 80 %", "3 FEUILLES", "50 HZ" },
            { "COIN 1,2 KHZ", "100 % CANARD", "FLOTTE" },
            { "FACE A", "90 MIN", "-40 DB" },
            { "MIDRONOME ?!", "MIDRONOME ?!", "MIDRONOME ?!" },
        };
        return texts[kindOf (i)][(i / 3) % 3];
    }

    juce::String stampOf (int i)
    {
        if (kindOf (i) == 7)
            return "NON...";
        static const char* const stamps[] = { "OK", "VU", "OK !", "BON" };
        return stamps[i % 4];
    }

    const int spacingUnits = 36;
}

//==============================================================================
GhostShow::GhostShow()
{
    phrases.add ("Va te faire un café."_fr);
    phrases.add ("T'as pas envie d'une bière ?"_fr);
    phrases.add ("Il faut que tu ailles me chercher du poulet."_fr);
    phrases.add ("Est-ce que t'as vu dehors ?"_fr);
    phrases.add ("T'as pas vu ton midronome ?"_fr);
    phrases.add ("T'es sûr que le midronome n'est pas sous le carton ?"_fr);
    phrases.add ("Je crois que je vois un bout du midronome dépasser derrière toi."_fr);
    phrases.add ("Je mesure, je mesure... Toi, tu bouges pas."_fr);
    phrases.add ("C'est le moment d'arroser les plantes."_fr);
    phrases.add ("Si ça siffle, c'est normal. Enfin, je crois."_fr);
    phrases.add ("Et si tu rangeais les câbles ? Le midronome est peut-être dessous."_fr);
    phrases.add ("Une petite pause pipi ? Je garde la machine."_fr);
    phrases.add ("J'en suis à trois bananes et demie."_fr);
    phrases.add ("Même Aphex Twin trouve ça inquiétant."_fr);
    phrases.add ("Elle te manque pas trop ta SidStation ?"_fr);
    phrases.add ("Tu connais Yoann ? Il est génial !"_fr);
    phrases.add ("C'est Yoann qui m'a créé !"_fr);
    phrases.add ("Sommes-nous réellement libres de nos choix ?"_fr);
    phrases.add ("Que devient la conscience après la mort ?"_fr);
    phrases.add ("Le temps existe-t-il indépendamment de nous ?"_fr);
    phrases.add ("La réalité existe-t-elle sans observateur ?"_fr);
    phrases.add ("Pourquoi les neutrinos possèdent-ils une masse ?"_fr);
    phrases.add ("La turbulence possède-t-elle une description mathématique universelle ?"_fr);
    phrases.add ("Le temps émerge-t-il de l'intrication quantique ?"_fr);
    phrases.add ("Les constantes fondamentales peuvent-elles varier dans le temps ?"_fr);
    phrases.add ("Pourquoi l'entropie initiale de l'univers était-elle si faible ?"_fr);
    phrases.add ("La conscience nécessite-t-elle des phénomènes quantiques ?"_fr);
    phrases.add ("Existe-t-il une limite fondamentale à la divisibilité spatiale ?"_fr);
    phrases.add ("Les trous noirs conservent-ils toute l'information quantique ?"_fr);
    phrases.add ("Notre univers est-il une phase d'un système plus fondamental ?"_fr);
    phrases.add ("Ça sent pas le cramé ?"_fr);
    phrases.add ("Tu penses que je pourrais conclure ?"_fr);
    setOpaque (true);
    reset();
}

void GhostShow::reset()
{
    t = belt = inspectT = 0.0;
    inspecting = -1;
    measured.clear();
    running = finished = finishedOk = false;
    pausedWhy.clear();
    idleSays.clear();
    order.clear();
    phrase = 0;
    nextPhrase();
}

void GhostShow::nextPhrase()
{
    if (order.empty())
    {
        for (int i = 0; i < phrases.size(); ++i)
            order.push_back (i);
        for (size_t i = order.size(); i > 1; --i)
            std::swap (order[i - 1], order[(size_t) random.nextInt ((int) i)]);
    }
    phrase = order.back();
    order.pop_back();
    phraseT = 0.0;
}

void GhostShow::setRunning (bool on)
{
    running = on;
    if (on)
        startTimerHz (30);
    else if (! finished)
        stopTimer();
    repaint();
}

void GhostShow::setPaused (const juce::String& why)
{
    if (why != pausedWhy)
    {
        pausedWhy = why;
        phraseT = 0.0;
        if (why.isEmpty())
            nextPhrase();
    }
}

void GhostShow::setIdle (const juce::String& says)
{
    idleSays = says;
    phraseT = 0.0;
    startTimerHz (30);
}

void GhostShow::setFinished (bool ok)
{
    finished = true;
    finishedOk = ok;
    inspecting = -1;
    phraseT = 0.0;
    startTimerHz (30);
}

juce::Rectangle<int> GhostShow::objectBox (int index, int p, int) const
{
    const double startX = getWidth() + 4.0 * p;
    const int x = (int) std::lround (startX + index * spacingUnits * p - belt);
    const int floorY = getHeight() - 6 * p;
    const int beltY = floorY - 9 * p;
    return { x, beltY - 12 * p, 12 * p, 12 * p };
}

void GhostShow::timerCallback()
{
    const double dt = 1.0 / 30.0;
    t += dt;
    phraseT += dt;
    const int p = juce::jlimit (3, 6, getHeight() / 48);
    const int ghostX = (int) (getWidth() * 0.36);

    if (running && pausedWhy.isEmpty() && ! finished)
    {
        if (inspecting >= 0)
        {
            inspectT += dt;
            if (inspectT >= 2.6)
            {
                measured.insert (inspecting);
                inspecting = -1;
            }
        }
        else
        {
            belt += 30.0 * p * dt;
            const int first = std::max (0, (int) ((belt - getWidth() - 16.0 * p) / (spacingUnits * p)));
            for (int i = first; i < first + 40; ++i)
            {
                const auto box = objectBox (i, p, ghostX);
                if (box.getX() > getWidth())
                    break;
                if (measured.count (i) == 0 && box.getCentreX() <= ghostX)
                {
                    belt -= ghostX - box.getCentreX();
                    inspecting = i;
                    inspectT = 0.0;
                    break;
                }
            }
            while (! measured.empty() && objectBox (*measured.begin(), p, ghostX).getRight() < -20 * p)
                measured.erase (measured.begin());
        }
        if (phraseT > 7.0)
            nextPhrase();
    }
    repaint();
}

void GhostShow::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds();
    const int w = b.getWidth(), h = b.getHeight();
    const int p = juce::jlimit (3, 6, h / 48);
    const int floorY = h - 6 * p;
    const int beltY = floorY - 9 * p;
    const int ghostX = (int) (w * 0.36);

    g.fillAll (px::black);
    px::dither (g, { 0, 0, w, beltY }, px::frame, 1);
    px::dither (g, { 0, floorY, w, h - floorY }, px::faint, 2);
    g.setColour (px::grey);
    g.fillRect (0, floorY, w, std::max (1, p / 2));

    // the conveyor belt
    g.setColour (px::dim);
    for (int x : { 7 * p, w - 10 * p })
        g.fillRect (x, beltY + 3 * p, 2 * p, floorY - beltY - 3 * p);
    g.setColour (px::white);
    g.fillRect (2 * p, beltY, w - 4 * p, p);
    g.setColour (px::frame);
    g.fillRect (2 * p, beltY + p, w - 4 * p, 2 * p);
    const int dash = 6 * p;
    const int off = (int) std::fmod (belt, (double) dash);
    g.setColour (px::grey);
    for (int x = 2 * p - off; x < w - 4 * p; x += dash)
        if (x >= 2 * p)
            g.fillRect (x, beltY + p + p / 2, 2 * p, p);
    for (int x = 4 * p; x < w - 6 * p; x += 14 * p)
    {
        g.setColour (px::dim);
        g.fillRect (x, beltY + 3 * p, 3 * p, 3 * p);
        const int phase = (int) (belt / (2.0 * p)) % 4;
        g.setColour (px::white);
        g.fillRect (x + (phase == 1 || phase == 2 ? 2 : 0) * p, beltY + 3 * p + (phase >= 2 ? 2 : 0) * p, p, p);
    }

    // the objects
    const auto labelFont = px::label ((float) p * 2.6f, true);
    auto tag = [&] (const juce::String& text, juce::Point<int> centre, float scale, bool solid)
    {
        const auto f = px::label ((float) p * 2.6f * scale, true);
        const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, text)) + 3 * p;
        const int th = juce::roundToInt (f.getHeight()) + p;
        const juce::Rectangle<int> r (centre.x - tw / 2, centre.y - th / 2, tw, th);
        g.setColour (solid ? px::white : px::black);
        g.fillRect (r);
        px::frameRect (g, r, px::white, std::max (1, p / 2));
        px::drawText (g, text, r, juce::Justification::centred, f, solid ? px::black : px::white);
    };

    const int first = std::max (0, (int) ((belt - w - 16.0 * p) / (spacingUnits * p)));
    for (int i = first; i < first + 40; ++i)
    {
        const auto box = objectBox (i, p, ghostX);
        if (box.getX() > w)
            break;
        if (box.getRight() < 0)
            continue;
        drawSprite (g, objects[(size_t) kindOf (i)], box.getX(), box.getY(), p);
        const bool stamped = measured.count (i) > 0 || (inspecting == i && inspectT >= 1.5);
        if (stamped)
        {
            const double since = inspecting == i ? inspectT - 1.5 : 1.0;
            const float scale = since < 0.15 ? 1.8f - (float) (since / 0.15) * 0.8f : 1.0f;
            tag (stampOf (i), box.getCentre().translated (0, p), scale, true);
            if (since < 0.45)
            {
                g.setColour (px::white);
                const double r = since * 70.0 * p;
                for (int k = 0; k < 8; ++k)
                {
                    const double a = k * juce::MathConstants<double>::pi / 4.0;
                    g.fillRect (box.getCentreX() + (int) (std::cos (a) * r), box.getCentreY() + (int) (std::sin (a) * r), p, p);
                }
            }
        }
    }

    // the ghost
    const int gw = 16 * p;
    const bool looking = inspecting >= 0;
    double bob = std::sin (t * 2.4) * 1.5 * p;
    if (finished && finishedOk)
        bob = -std::abs (std::sin (t * 5.0)) * 7.0 * p;
    if (looking && inspectT < 2.2)
        bob += 2.0 * p * std::min (1.0, inspectT * 4.0);
    const int gx = ghostX - gw / 2;
    const int gy = beltY - 12 * p - 6 * p - 16 * p + (int) bob;
    drawSprite (g, ghostBody, gx, gy, p);
    drawSprite (g, ((int) (t * 4.0) % 2) == 0 ? skirtA : skirtB, gx, gy + 14 * p, p);

    g.setColour (px::black);
    const bool blink = std::fmod (t, 3.3) < 0.12;
    const int gazeX = looking ? 0 : 1, gazeY = looking ? 1 : 0;
    for (int eye : { 4, 10 })
    {
        if (finished && finishedOk)
        {
            g.fillRect (gx + (eye - 1) * p, gy + 6 * p, p, p);
            g.fillRect (gx + eye * p, gy + 5 * p, 2 * p, p);
            g.fillRect (gx + (eye + 2) * p, gy + 6 * p, p, p);
        }
        else if (blink)
            g.fillRect (gx + (eye + gazeX) * p, gy + (6 + gazeY) * p, 2 * p, p);
        else
            g.fillRect (gx + (eye + gazeX) * p, gy + (4 + gazeY) * p, 2 * p, 3 * p);
    }
    if (finished && finishedOk)
    {
        g.fillRect (gx + 6 * p, gy + 9 * p, p, p);
        g.fillRect (gx + 9 * p, gy + 9 * p, p, p);
        g.fillRect (gx + 7 * p, gy + 10 * p, 2 * p, p);
    }
    else if (finished)
        g.fillRect (gx + 6 * p, gy + 10 * p, 4 * p, p);
    else if (looking)
        g.fillRect (gx + 6 * p + gazeX * p, gy + 10 * p, 4 * p, p);
    else
        g.fillRect (gx + 7 * p + gazeX * p, gy + 9 * p, 2 * p, 2 * p);

    if (pausedWhy.isNotEmpty() && ! finished)
        px::drawText (g, "?", { gx + 4 * p, gy - 10 * p, 8 * p, 8 * p }, juce::Justification::centred, px::label ((float) p * 6.0f, true), px::grey);

    // the tool and the reading
    if (looking && pausedWhy.isEmpty())
    {
        const auto box = objectBox (inspecting, p, ghostX);
        const int tool = inspecting % 3;
        const Sprite& s = tool == 0 ? loupe : tool == 1 ? ruler : tuningFork;
        const int tw = (int) std::strlen (s[0]) * p, th = (int) s.size() * p;
        const juce::Point<int> hand (gx + gw - 2 * p, gy + 10 * p);
        const juce::Point<int> over (box.getCentreX() - tw / 2, tool == 1 ? box.getY() - th - p : box.getY() + 2 * p);
        double a = juce::jlimit (0.0, 1.0, inspectT / 0.35);
        if (inspectT > 2.1)
            a = juce::jlimit (0.0, 1.0, 1.0 - (inspectT - 2.1) / 0.3);
        const int tx = (int) (hand.x + (over.x - hand.x) * a), ty = (int) (hand.y + (over.y - hand.y) * a);
        const int wobble = tool == 0 && a >= 1.0 ? (int) (std::sin (inspectT * 12.0) * p) : 0;
        drawSprite (g, s, tx + wobble, ty, p);
        if (tool == 2 && a >= 1.0 && std::fmod (inspectT, 0.3) < 0.15)
        {
            g.setColour (px::grey);
            g.fillRect (tx - 2 * p, ty + p, p, 2 * p);
            g.fillRect (tx + tw + p, ty + p, p, 2 * p);
        }

        if (inspectT > 0.45)
        {
            const auto text = readoutOf (inspecting);
            const int chars = juce::jlimit (0, text.length(), (int) ((inspectT - 0.45) * 22.0));
            if (chars > 0)
                tag (text.substring (0, chars), { box.getCentreX(), box.getY() - 9 * p }, 1.0f, false);
        }
    }
    juce::ignoreUnused (labelFont);

    // what it says
    juce::String says;
    double typed = phraseT * 30.0;
    if (finished)
        says = finishedOk ? "Fini ! Tout est mesuré."_fr : "Oh non...";
    else if (pausedWhy.isNotEmpty())
        says = pausedWhy;
    else if (running)
        says = phrases[phrase];
    else
        says = idleSays;
    if (says.isNotEmpty())
    {
        const auto f = px::text ((float) p * 5.0f);
        const int bx = gx + gw + 4 * p;
        const int bw = juce::jmin (w - bx - 3 * p, 80 * p);
        if (bw > 30 * p)
        {
            const auto shown = says.substring (0, juce::jlimit (0, says.length(), (int) typed));
            juce::GlyphArrangement ga;
            ga.addJustifiedText (f, says, 0.0f, 0.0f, (float) (bw - 4 * p), juce::Justification::topLeft);
            const int lines = juce::jmax (1, (int) std::ceil (ga.getBoundingBox (0, -1, true).getHeight() / f.getHeight() - 0.01));
            const int bh = lines * (int) f.getHeight() + 3 * p;
            const int by = juce::jmax (p, gy - bh + 2 * p);
            const juce::Rectangle<int> bubble (bx, by, bw, bh);
            g.setColour (px::black);
            g.fillRect (bubble);
            px::frameRect (g, bubble, px::white, std::max (1, p / 2));
            g.setColour (px::white);
            for (int k = 0; k < 3; ++k)
                g.fillRect (bx - (k + 1) * p, by + bh - (3 - k) * p, p, p);
            g.setFont (f);
            g.setColour (px::white);
            g.drawFittedText (shown, bubble.reduced (2 * p, p), juce::Justification::topLeft, lines + 1, 1.0f);
        }
    }
}

//==============================================================================
MeasureWizard::MeasureWizard (Engine& e) : engine (e)
{
    stepLabel.setFont (ui::font (14.0f));
    stepLabel.setColour (juce::Label::textColourId, ui::accent);
    title.setFont (ui::font (28.0f, true));
    body.setFont (ui::font (18.0f));
    body.setJustificationType (juce::Justification::topLeft);
    details.setFont (ui::font (16.0f));
    details.setJustificationType (juce::Justification::topLeft);
    details.setColour (juce::Label::textColourId, ui::dim);
    status.setFont (ui::font (20.0f, true));
    for (auto* l : { &stepLabel, &title, &body })
        addAndMakeVisible (l);
    addChildComponent (details);
    addChildComponent (status);

    for (auto* b : { &later, &back, &next, &measureReal, &tryIt, &testTone, &go, &pauseButton, &stopButton, &showFile, &useHere, &again })
        addChildComponent (b);
    for (auto* t : { &speakersOff, &handsOff })
    {
        t->setColour (juce::ToggleButton::textColourId, ui::text);
        t->onClick = [this] { updateButtons(); };
        addChildComponent (t);
    }
    ui::styleBig (next, true);
    ui::styleBig (measureReal, true);
    ui::styleBig (tryIt, false);
    tryIt.setColour (juce::TextButton::textColourOffId, px::yellow);
    ui::styleBig (go, true);
    ui::styleBig (testTone, false);
    addChildComponent (ghost);

    later.onClick = [this] { close(); };
    back.onClick = [this]
    {
        if (screen == Screen::audio) { closeAudio(); show (Screen::intro); }
        else if (screen == Screen::ready) show (Screen::audio);
    };
    next.onClick = [this] { if (screen == Screen::audio) show (Screen::ready); };
    measureReal.onClick = [this] { trial = false; openAudio(); show (Screen::audio); };
    tryIt.onClick = [this] { trial = true; openAudio(); show (Screen::audio); };
    testTone.onClick = [this] { playTone(); };
    go.onClick = [this] { startMeasure(); };
    pauseButton.onClick = [this]
    {
        if (live != nullptr)
        {
            if (live->pausedFor.load() >= 0) live->resume();
            else live->pauseWanted = true;
        }
        if (sim != nullptr)
        {
            if (sim->pausedFor.load() >= 0) sim->resume();
            else sim->pauseWanted = true;
        }
    };
    stopButton.onClick = [this]
    {
        if (live != nullptr) live->stop = true;
        if (sim != nullptr) sim->stop = true;
    };
    showFile.onClick = [this] { if (resultFile.existsAsFile()) resultFile.revealToUser(); };
    useHere.onClick = [this]
    {
        juce::String error;
        if (useMeasures != nullptr && useMeasures (resultFile, error))
        {
            status.setText ("C'est fait : ton simulateur sonne maintenant comme cette machine."_fr, juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, px::white);
        }
        else
        {
            status.setText (error.isNotEmpty() ? error : "Ces mesures n'ont pas pu être chargées."_fr, juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, px::red);
        }
        status.setVisible (true);
    };
    again.onClick = [this] { openAudio(); show (Screen::audio); };

    engine.addListener (this);
}

MeasureWizard::~MeasureWizard()
{
    stopTimer();
    if (live != nullptr) live->stop = true;
    if (sim != nullptr) sim->stop = true;
    worker.stopThread (5000);
    engine.removeListener (this);
    closeAudio();
}

bool MeasureWizard::isBusy() const
{
    return worker.isThreadRunning();
}

measure::Rig* MeasureWizard::rig()
{
    if (trial)
        return sim.get();
    return live.get();
}

void MeasureWizard::start()
{
    resultFile = juce::File();
    show (Screen::intro);
    startTimerHz (30);
}

void MeasureWizard::demo (const juce::String& what)
{
    if (isBusy())
    {
        if (live != nullptr) live->stop = true;
        if (sim != nullptr) sim->stop = true;
        worker.stopThread (5000);
    }
    trial = what != "live";
    openAudio();
    if (what == "live")
        show (Screen::audio);
    else
    if (what == "audio")
        show (Screen::audio);
    else if (what == "ready")
    {
        signal = Signal::ok;
        show (Screen::ready);
    }
    else if (what == "done")
    {
        measure::Result r;
        r.ok = true;
        r.warnings.add ("Le son accordé (Tuned) n'a pas répondu comme prévu : note de référence non mesurée."_fr);
        finished (r, {});
    }
    else
        startMeasure();
}

void MeasureWizard::close()
{
    if (isBusy())
        return;
    closeAudio();
    stopTimer();
    ghost.setRunning (false);
    if (onClose)
        onClose();
}

//==============================================================================
void MeasureWizard::openAudio()
{
    if (isBusy())
        return;                 // the rigs are in use
    signal = Signal::none;
    toneMax = {};
    if (trial)
    {
        closeAudio();
        sim = std::make_unique<measure::SynthRig> (ra::synth::Calibration::defaults(), 1, 36, 48000.0, 211, true);
        sim->waitWhenPaused = true;
        return;
    }
    sim.reset();
    if (devices != nullptr)
        return;

    devices = std::make_unique<juce::AudioDeviceManager>();
    std::unique_ptr<juce::XmlElement> saved;
    if (settings != nullptr && settings() != nullptr)
        saved = settings()->getXmlValue ("measureAudio");
    const auto error = devices->initialise (2, 0, saved.get(), true);
    if (error.isNotEmpty())
        engine.userNote ("Mesure : carte son - " + error);
    devices->addAudioCallback (&recorder);
    selector = std::make_unique<juce::AudioDeviceSelectorComponent> (*devices, 1, 2, 0, 0, false, false, false, true);
    addAndMakeVisible (*selector);
    live = std::make_unique<measure::LiveRig> (engine, recorder);

    // macOS asks once whether the app may listen to the input
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [] (bool) {});
}

void MeasureWizard::closeAudio()
{
    if (isBusy())
        return;
    if (devices != nullptr)
    {
        if (settings != nullptr && settings() != nullptr)
            if (auto xml = devices->createStateXml())
                settings()->setValue ("measureAudio", xml.get());
        devices->removeAudioCallback (&recorder);
        selector.reset();
        devices.reset();
    }
    live.reset();
}

float MeasureWizard::level (int channel)
{
    if (trial)
        return sim != nullptr ? sim->level.load() : 0.0f;
    if (devices == nullptr || channel >= recorder.numInputs())
        return 0.0f;
    return recorder.takePeak (channel);
}

int MeasureWizard::freeChannel() const
{
    return engine.freeTunedChannel (23);
}

void MeasureWizard::withKit (std::function<void()> then)
{
    if (trial || engine.hasSoundDump (23))
    {
        then();
        return;
    }
    afterKit = std::move (then);
    status.setText ("Je lis le kit en cours d'abord..."_fr, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, ui::dim);
    status.setVisible (true);
    engine.readKit();
}

void MeasureWizard::taskFinished (const juce::String& task, bool, const juce::String&)
{
    if (task != "kit" || afterKit == nullptr)
        return;
    auto then = std::move (afterKit);
    afterKit = nullptr;
    if (engine.hasSoundDump (23))
        then();
    else
    {
        status.setText ("Le kit n'a pas pu être lu : vérifie la connexion MIDI (bouton en haut)."_fr, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, px::red);
        updateButtons();
    }
}

void MeasureWizard::restoreSound()
{
    if (! trial)
    {
        engine.resendSoundDump (23);
        engine.echoSound = -1;
        // if the unit echoed the test settings back, the kit shown has them: read it again
        juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<MeasureWizard> (this)]
        {
            if (safe != nullptr && ! safe->engine.isBusy())
                safe->engine.readKit();
        });
    }
}

//==============================================================================
void MeasureWizard::playTone()
{
    if (isBusy() || rig() == nullptr)
        return;
    withKit ([this]
    {
        if (stopSequencer)
            stopSequencer();
        if (! trial)
            engine.echoSound = 23;
        toneMax = {};
        signal = Signal::listening;
        toneRunning = true;
        if (sim != nullptr)
            sim->speed = 1.0;
        worker.job = [this]
        {
            measure::Bench bench (*rig(), engine.deviceId.load(), trial ? 1 : engine.channel.load(),
                                  trial ? 36 : engine.baseNote.load(), 0, 23);
            bench.tone();
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MeasureWizard> (this)]
            {
                if (safe != nullptr)
                    safe->toneDone();
            });
        };
        worker.startThread();
        updateButtons();
    });
}

void MeasureWizard::toneDone()
{
    worker.stopThread (1000);
    toneRunning = false;
    restoreSound();
    const float top = juce::jmax (toneMax[0], toneMax[1]);
    if (top > 0.97f)
        signal = Signal::tooLoud;
    else if (top > 0.01f)
    {
        signal = Signal::ok;
        juce::uint32 mask = 0;
        for (int c = 0; c < 2; ++c)
            if (toneMax[(size_t) c] >= top * 0.25f)
                mask |= 1u << c;
        recorder.setChannels (mask == 0 ? 1u : mask);
    }
    else
        signal = Signal::nothing;
    updateButtons();
    repaint();
}

void MeasureWizard::startMeasure()
{
    if (isBusy() || rig() == nullptr)
        return;
    withKit ([this]
    {
        if (stopSequencer)
            stopSequencer();
        if (! trial)
            engine.echoSound = 23;
        if (live != nullptr) { live->stop = false; live->pauseWanted = false; live->resume(); }
        if (sim != nullptr) { sim->stop = false; sim->pauseWanted = false; sim->resume(); sim->speed = 4.0; }
        stepsDone = 0;
        lastPaused = -2;
        show (Screen::running);

        const int device = engine.deviceId.load();
        const int channel = trial ? 1 : engine.channel.load();
        const int base = trial ? 36 : engine.baseNote.load();
        const int free = trial ? 5 : freeChannel();
        const bool isTrial = trial;
        worker.job = [this, device, channel, base, free, isTrial]
        {
            measure::Bench bench (*rig(), device, channel, base, free, 23);
            bench.onProgress = [this] (int done, int, const juce::String& what)
            {
                stepsDone = done;
                const juce::ScopedLock sl (whatLock);
                whatNow = what;
            };
            auto r = std::make_shared<measure::Result> (bench.run());
            juce::File file;
            if (r->ok && ! isTrial)
                file = measure::save (*r, measure::defaultFolder());
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MeasureWizard> (this), r, file]
            {
                if (safe != nullptr)
                    safe->finished (*r, file);
            });
        };
        worker.startThread();
    });
}

void MeasureWizard::finished (const measure::Result& r, const juce::File& file)
{
    worker.stopThread (1000);
    restoreSound();
    resultFile = file;
    resultOk = r.ok;
    resultStopped = ! r.ok && r.message.startsWith ("Mesure");
    show (Screen::done);

    if (r.ok)
    {
        title.setText (trial ? "Essai terminé !"_fr : "C'est fini !"_fr, juce::dontSendNotification);
        if (trial)
            body.setText ("Le fantôme a mesuré le simulateur. Avec une vraie machine, c'est la même chose, en quatre fois plus long."_fr,
                          juce::dontSendNotification);
        else if (file.existsAsFile())
            body.setText ("Les mesures sont dans le fichier « "_fr + file.getFileName() + " » ("_fr + file.getParentDirectory().getFullPathName()
                          + ").\n\nEnvoie-le à qui veut que son simulateur sonne comme ta machine (mail, Messages, AirDrop...). "_fr
                          "Il le charge dans Outils, « Charger des mesures... »."_fr, juce::dontSendNotification);
        else
            body.setText ("Tout est mesuré, mais le fichier n'a pas pu être écrit dans Documents."_fr, juce::dontSendNotification);
    }
    else
    {
        title.setText (resultStopped ? "Mesure arrêtée"_fr : "Ça n'a pas marché"_fr, juce::dontSendNotification);
        body.setText (r.message + "\n\nLe son 24 du kit est remis comme avant."_fr, juce::dontSendNotification);
    }
    details.setText (r.warnings.isEmpty() ? juce::String()
                                          : "À vérifier :\n  - "_fr + r.warnings.joinIntoString ("\n  - "), juce::dontSendNotification);
    details.setVisible (r.warnings.size() > 0);
    ghost.setFinished (r.ok);
    updateButtons();
    resized();
}

//==============================================================================
void MeasureWizard::show (Screen s)
{
    screen = s;
    const int number = s == Screen::intro ? 1 : s == Screen::audio ? 2 : s == Screen::ready ? 3 : s == Screen::running ? 4 : 5;
    stepLabel.setText ((trial ? "ESSAI SANS MACHINE   ·   "_fr : "MESURER LE RACKATTACK   ·   "_fr) + juce::String (number) + " / 5",
                       juce::dontSendNotification);
    status.setVisible (false);
    details.setVisible (false);

    const bool connected = engine.getConnection() == Engine::Connection::connected;
    const bool simulating = isSimulation != nullptr && isSimulation();
    if (s == Screen::intro)
    {
        title.setText ("Apprendre au simulateur à sonner comme ta machine"_fr, juce::dontSendNotification);
        juce::String text = "Le plugin fait jouer au RackAttack des sons de test, un réglage à la fois, et les écoute par ta carte son : "_fr
                            "hauteurs, volumes, enveloppes, filtre, LFO, crack, cymbales. Ça dure environ 5 minutes.\n\n"_fr
                            "Le son 24 du kit en cours sert de cobaye : il est remis comme avant à la fin, rien n'est enregistré dans la machine.\n\n"_fr;
        if (simulating)
            text << "Tu es en mode simulation : pour mesurer une vraie machine, branche-la d'abord (bouton en haut). En attendant, tu peux essayer la procédure sur le simulateur."_fr;
        else if (! connected)
            text << "Le RackAttack n'est pas connecté : branche-le d'abord (bouton en haut), ou essaie la procédure sans machine."_fr;
        else
            text << "Il te faut : les sorties Out 1 et Out 2 du RackAttack branchées sur deux entrées de ta carte son."_fr;
        body.setText (text, juce::dontSendNotification);
        measureReal.setEnabled (connected && ! simulating);
        ghost.reset();
        ghost.setIdle ("Salut ! Moi, je mesure tout. Même les bananes."_fr);
    }
    else if (s == Screen::audio)
    {
        title.setText (trial ? "Le son (pour de faux)" : "Le son", juce::dontSendNotification);
        body.setText (trial ? "Pas besoin de carte son pour l'essai : le simulateur joue directement dans le plugin. Clique sur « Jouer un son test » : les barres doivent bouger."_fr
                            : "Branche Out 1 et Out 2 du RackAttack (ou juste Out 1) sur les entrées de ta carte son, choisis-la ici, "_fr
                              "puis clique sur « Jouer un son test » : le RackAttack fait trois bips et les barres doivent bouger."_fr,
                      juce::dontSendNotification);
        if (selector != nullptr)
            selector->setVisible (! trial);
    }
    else if (s == Screen::ready)
    {
        title.setText ("Avant de commencer", juce::dontSendNotification);
        body.setText ("Le RackAttack va jouer des bips, des souffles et des sifflements, parfois forts : c'est normal. "_fr
                      "Le plugin écoute directement l'entrée de la carte son, alors coupe tes enceintes et ton casque.\n\n"_fr
                      "Ensuite, ne touche plus à la machine : ni potards, ni boutons, ni volume. Si tu y touches, la mesure se met en pause "_fr
                      "et reprend la partie en cours."_fr, juce::dontSendNotification);
        speakersOff.setToggleState (false, juce::dontSendNotification);
        handsOff.setToggleState (false, juce::dontSendNotification);
    }
    else if (s == Screen::running)
    {
        title.setText ({}, juce::dontSendNotification);
        body.setText ({}, juce::dontSendNotification);
        ghost.reset();
        ghost.setRunning (true);
    }
    if (s == Screen::audio || s == Screen::ready)
        ghost.setRunning (false);
    if (selector != nullptr && s != Screen::audio)
        selector->setVisible (false);

    updateButtons();
    resized();
    repaint();
}

void MeasureWizard::updateButtons()
{
    const bool busy = isBusy();
    later.setVisible (screen != Screen::running);
    later.setButtonText (screen == Screen::done ? "Fermer" : "Plus tard");
    later.setEnabled (! busy);
    back.setVisible (screen == Screen::audio || screen == Screen::ready);
    back.setEnabled (! busy);
    next.setVisible (screen == Screen::audio);
    next.setEnabled (signal == Signal::ok && ! busy);
    measureReal.setVisible (screen == Screen::intro);
    tryIt.setVisible (screen == Screen::intro);
    testTone.setVisible (screen == Screen::audio);
    testTone.setEnabled (! busy);
    speakersOff.setVisible (screen == Screen::ready);
    handsOff.setVisible (screen == Screen::ready);
    go.setVisible (screen == Screen::ready);
    go.setEnabled (speakersOff.getToggleState() && handsOff.getToggleState() && ! busy);
    ui::styleBig (go, go.isEnabled());
    pauseButton.setVisible (screen == Screen::running);
    stopButton.setVisible (screen == Screen::running);
    ghost.setVisible (screen == Screen::intro || screen == Screen::running || screen == Screen::done);
    showFile.setVisible (screen == Screen::done && resultFile.existsAsFile());
    useHere.setVisible (screen == Screen::done && resultFile.existsAsFile());
    again.setVisible (screen == Screen::done && ! resultOk);
}

//==============================================================================
void MeasureWizard::timerCallback()
{
    // the meters
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int c = 0; c < 2; ++c)
    {
        const float l = level (c);
        if (toneRunning)
            toneMax[(size_t) c] = juce::jmax (toneMax[(size_t) c], l);
        shown[(size_t) c] = juce::jmax (l, shown[(size_t) c] * 0.86f);
        if (l >= hold[(size_t) c] || now > holdUntil[(size_t) c])
        {
            hold[(size_t) c] = l;
            holdUntil[(size_t) c] = now + 1200.0;
        }
    }

    if (screen == Screen::running)
    {
        int paused = -1;
        if (live != nullptr) paused = live->pausedFor.load();
        if (sim != nullptr) paused = sim->pausedFor.load();
        if (paused != lastPaused)
        {
            lastPaused = paused;
            ghost.setPaused (paused == measure::Rig::asked ? "Pause ! Je t'attends."_fr
                             : paused == measure::Rig::touched ? "Hé ! T'as touché la machine ? On reprend cette partie quand tu veux."_fr
                             : paused == measure::Rig::levelChanged ? "Le son a changé de volume... Remets-le comme avant, puis reprends."_fr
                             : juce::String());
            pauseButton.setButtonText (paused >= 0 ? "Reprendre" : "Pause");
            ui::styleBig (pauseButton, paused >= 0);
        }
    }
    if (isShowing())
        repaint();
}

void MeasureWizard::drawMeters (juce::Graphics& g, juce::Rectangle<int> area, bool big)
{
    const int rows = trial ? 1 : 2;
    const int rowH = area.getHeight() / rows;
    const int blocks = big ? 40 : 24;
    for (int c = 0; c < rows; ++c)
    {
        auto row = area.removeFromTop (rowH).reduced (0, big ? 5 : 2);
        const auto labelArea = row.removeFromLeft (big ? 150 : 120);
        const bool used = trial || (recorder.getChannels() >> c) & 1u;
        px::drawText (g, trial ? juce::String ("SIMULATEUR") : "ENTRÉE "_fr + juce::String (c + 1), labelArea,
                      juce::Justification::centredLeft, px::label (big ? 12.0f : 10.0f, true), used ? px::white : px::dim);
        auto toBlocks = [blocks] (float l)
        {
            const float db = juce::Decibels::gainToDecibels (l, -60.0f);
            return juce::jlimit (0, blocks, (int) std::lround ((db + 60.0f) / 60.0f * (float) blocks));
        };
        const int lit = toBlocks (shown[(size_t) c]), peak = toBlocks (hold[(size_t) c]);
        const float bw = (float) row.getWidth() / (float) blocks;
        for (int k = 0; k < blocks; ++k)
        {
            const float db = -60.0f + 60.0f * (float) (k + 1) / (float) blocks;
            const auto colour = db > -3.0f ? px::red : db > -12.0f ? px::yellow : px::white;
            const juce::Rectangle<int> r ((int) ((float) row.getX() + (float) k * bw), row.getY(), juce::jmax (1, (int) bw - 2), row.getHeight());
            if (k < lit)
                g.setColour (colour);
            else if (k == peak - 1)
                g.setColour (colour.withAlpha (0.8f));
            else
                g.setColour (juce::Colour (0xff1c1c1c));
            g.fillRect (r);
        }
    }
}

void MeasureWizard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.88f));
    g.setColour (ui::panel);
    g.fillRect (card);
    px::frameRect (g, card, px::white, 3);

    if (screen == Screen::audio)
    {
        drawMeters (g, meterArea, true);
        juce::String text;
        juce::Colour colour = px::dim;
        switch (signal)
        {
            case Signal::none:      text = "APPUIE SUR « JOUER UN SON TEST »"_fr; break;
            case Signal::listening: text = "J'ÉCOUTE..."_fr; colour = px::white; break;
            case Signal::ok:        text = "SIGNAL OK"; colour = px::white; break;
            case Signal::tooLoud:   text = "TROP FORT : BAISSE LE GAIN D'ENTRÉE ET REJOUE LE SON"_fr; colour = px::red; break;
            case Signal::nothing:   text = "RIEN N'ARRIVE : VÉRIFIE LES CÂBLES ET L'ENTRÉE CHOISIE"_fr; colour = px::red; break;
        }
        auto light = meterArea.withY (meterArea.getBottom() + 12).withHeight (26);
        g.setColour (signal == Signal::ok ? px::white : signal == Signal::tooLoud || signal == Signal::nothing ? px::red : px::faint);
        g.fillRect (light.removeFromLeft (26));
        light.removeFromLeft (12);
        light.removeFromRight (testTone.getWidth() + 10);
        px::drawText (g, text, light, juce::Justification::centredLeft, px::label (14.0f, true), colour);
    }

    if (screen == Screen::running)
    {
        // the big warning, its frame blinking
        const bool on = std::fmod (juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.0) < 0.6;
        g.setColour (px::black);
        g.fillRect (bannerArea);
        if (on)
            px::frameRect (g, bannerArea, px::red, 4);
        else
        {
            px::dither (g, bannerArea, px::red, 2);
            g.setColour (px::black);
            g.fillRect (bannerArea.reduced (4));
        }
        auto inner = bannerArea.reduced (10, 6);
        px::drawText (g, "NE TOUCHE PAS À LA MACHINE"_fr, inner.removeFromTop (inner.getHeight() * 2 / 3),
                      juce::Justification::centred, px::label (30.0f, true), px::red);
        px::drawText (g, "et laisse tes enceintes coupées, ça dure environ 5 minutes"_fr, inner, juce::Justification::centred,
                      px::text (20.0f), px::grey);

        // progress
        const int done = stepsDone.load();
        const int total = measure::Bench::expectedSteps;
        auto row = progressArea;
        const int blocks = 50;
        px::bar (g, row.removeFromTop (18), blocks, juce::jlimit (0, blocks, done * blocks / total), px::white);
        row.removeFromTop (6);
        juce::String what;
        {
            const juce::ScopedLock sl (whatLock);
            what = whatNow;
        }
        const int pct = juce::jlimit (0, 99, done * 100 / total);
        px::drawText (g, juce::String (pct) + " %   ·   "_fr + what, row, juce::Justification::centredLeft, px::text (22.0f), px::white);

        drawMeters (g, smallMeterArea, false);
    }
}

void MeasureWizard::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (1000, getWidth() - 40), juce::jmin (720, getHeight() - 40));
    auto r = card.reduced (32, 22);
    stepLabel.setBounds (r.removeFromTop (24));

    auto buttons = r.removeFromBottom (44);
    r.removeFromBottom (12);
    later.setBounds (buttons.removeFromRight (140));
    buttons.removeFromRight (10);

    switch (screen)
    {
        case Screen::intro:
        {
            title.setBounds (r.removeFromTop (44));
            r.removeFromTop (6);
            body.setBounds (r.removeFromTop (240));
            r.removeFromTop (4);
            ghost.setBounds (r);
            measureReal.setBounds (buttons.removeFromLeft (300));
            buttons.removeFromLeft (12);
            tryIt.setBounds (buttons.removeFromLeft (364));
            break;
        }
        case Screen::audio:
        {
            title.setBounds (r.removeFromTop (44));
            r.removeFromTop (6);
            body.setBounds (r.removeFromTop (70));
            r.removeFromTop (6);
            const int meterH = trial ? 46 : 84;
            auto bottom = r.removeFromBottom (meterH + 44);
            meterArea = bottom.removeFromTop (meterH);
            auto statusRow = bottom.withTrimmedTop (10);
            testTone.setBounds (statusRow.removeFromRight (220).withHeight (32));
            if (selector != nullptr)
                selector->setBounds (r.reduced (0, 4));
            next.setBounds (buttons.removeFromRight (160));
            back.setBounds (buttons.removeFromLeft (120));
            status.setBounds (bottom);
            break;
        }
        case Screen::ready:
        {
            title.setBounds (r.removeFromTop (44));
            r.removeFromTop (6);
            body.setBounds (r.removeFromTop (150));
            r.removeFromTop (10);
            speakersOff.setBounds (r.removeFromTop (44));
            r.removeFromTop (6);
            handsOff.setBounds (r.removeFromTop (44));
            r.removeFromTop (16);
            status.setBounds (r.removeFromTop (30));
            go.setBounds (buttons.removeFromRight (260));
            back.setBounds (buttons.removeFromLeft (120));
            break;
        }
        case Screen::running:
        {
            title.setBounds ({});
            body.setBounds ({});
            bannerArea = r.removeFromTop (88);
            r.removeFromTop (12);
            auto bottom = r.removeFromBottom (110);
            ghost.setBounds (r);
            bottom.removeFromTop (12);
            progressArea = bottom.removeFromTop (52);
            bottom.removeFromTop (6);
            smallMeterArea = bottom.withWidth (juce::jmin (bottom.getWidth(), 420));
            stopButton.setBounds (buttons.removeFromRight (140));
            buttons.removeFromRight (10);
            pauseButton.setBounds (buttons.removeFromRight (160));
            break;
        }
        case Screen::done:
        {
            title.setBounds (r.removeFromTop (44));
            r.removeFromTop (6);
            ghost.setBounds (r.removeFromRight (juce::jmin (380, r.getWidth() / 2)).withTrimmedLeft (20).withHeight (260));
            body.setBounds (r.removeFromTop (150));
            r.removeFromTop (6);
            status.setBounds (r.removeFromTop (30));
            details.setBounds (r);
            showFile.setBounds (buttons.removeFromLeft (220));
            buttons.removeFromLeft (10);
            useHere.setBounds (buttons.removeFromLeft (380));
            again.setBounds (buttons.removeFromLeft (200));
            break;
        }
    }
}
