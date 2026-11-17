#include "UpdatePanel.h"
#include "Pixel.h"
#include "Text.h"
#include "Widgets.h"

namespace
{
    constexpr int cardWidth = 860, pad = 32, buttonHeight = 44;

    // text that wraps at the width given, from the top of its box: the height it took
    int wrapped (juce::Graphics* g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                 int x, int y, int width)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addJustifiedText (font, text, (float) x, (float) y + font.getAscent(), (float) width, juce::Justification::left);
        const auto box = glyphs.getBoundingBox (0, -1, true);
        if (g != nullptr)
        {
            g->setColour (colour);
            glyphs.draw (*g);
        }
        const int lines = juce::jmax (1, (int) std::round ((box.getBottom() - (float) y) / font.getHeight()));
        return (int) std::ceil (font.getHeight() * (float) lines);
    }
}

UpdatePanel::UpdatePanel (Updater& u) : updater (u)
{
    for (auto* b : { &install, &later, &skip, &readAll, &ok })
    {
        ui::styleBig (*b, b == &install || b == &ok);
        addChildComponent (b);
    }
    install.onClick = [this] { updater.install(); };
    later.onClick = [this] { close(); };
    ok.onClick = [this] { close(); };
    skip.onClick = [this] { updater.skipThisVersion(); close(); };
    readAll.onClick = [this]
    {
        juce::URL (mode == Mode::news || news.empty() ? Updater::pageOf (Updater::currentVersion())
                                                      : updater.getRelease().pageUrl.isNotEmpty() ? updater.getRelease().pageUrl
                                                                                                  : Updater::pageOf (latest))
            .launchInDefaultBrowser();
    };
    updater.changes.addChangeListener (this);
}

UpdatePanel::~UpdatePanel()
{
    updater.changes.removeChangeListener (this);
}

void UpdatePanel::showAvailable (const std::vector<Updater::News>& n, int versionsNewer)
{
    mode = Mode::available;
    news = n;
    latest = news.empty() ? juce::String() : news.front().version;
    more = juce::jmax (0, versionsNewer - (int) news.size());
   #if JUCE_MAC
    install.setButtonText ("Installer la "_fr + latest);
   #else
    install.setButtonText ("Télécharger la "_fr + latest);
   #endif
    refreshButtons();
    resized();
    setVisible (true);
    toFront (true);
}

void UpdatePanel::showNews (const Updater::News& n)
{
    mode = Mode::news;
    news = { n };
    latest = n.version;
    more = 0;
    refreshButtons();
    resized();
    setVisible (true);
    toFront (true);
}

void UpdatePanel::close()
{
    setVisible (false);
    if (onClose)
        onClose();
}

void UpdatePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! isVisible())
        return;
    refreshButtons();
    resized();
    repaint();
}

void UpdatePanel::refreshButtons()
{
    const auto state = updater.getState();
    const bool offer = mode == Mode::available;
    const bool busy = state == Updater::State::Installing;
    const bool done = state == Updater::State::Installed;
    install.setVisible (offer && ! done);
    install.setEnabled (! busy && state == Updater::State::Available);
    later.setVisible (offer && ! done);
    later.setEnabled (! busy);
    skip.setVisible (offer && ! done);
    skip.setEnabled (! busy && state == Updater::State::Available);
    readAll.setVisible (true);
    ok.setVisible (mode == Mode::news || done);
}

//==============================================================================
int UpdatePanel::drawContent (juce::Graphics* g, juce::Rectangle<int> area) const
{
    const int x = area.getX(), w = area.getWidth();
    int y = area.getY();
    const auto you = "TU AS LA "_fr + Updater::currentVersion();

    // the small line over the title, the version you have on its right
    if (g != nullptr)
    {
        px::drawText (*g, mode == Mode::news ? juce::String ("NOUVELLE VERSION") : "MISE À JOUR"_fr, { x, y, w / 2, 18 },
                      juce::Justification::centredLeft, px::label (12.0f, true), px::yellow);
        px::drawText (*g, you, { x + w / 2, y, w - w / 2, 18 }, juce::Justification::centredRight, px::label (12.0f, true), px::grey);
    }
    y += 26;

    const auto title = mode == Mode::news ? "Tu as maintenant la "_fr + latest : "La "_fr + latest + " est sortie";
    y += wrapped (g, title, px::text (46.0f), px::white, x, y, w) + 10;

    for (const auto& n : news)
    {
        // the version, in a white tag, then what it brings
        const auto tagFont = px::label (12.0f, true);
        const int tagW = juce::GlyphArrangement::getStringWidthInt (tagFont, n.version) + 16;
        if (g != nullptr)
        {
            g->setColour (px::white);
            g->fillRect (x, y + 6, tagW, 20);
            px::drawText (*g, n.version, { x, y + 6, tagW, 20 }, juce::Justification::centred, tagFont, px::black);
        }
        const int tx = x + tagW + 14, tw = w - tagW - 14;
        y += wrapped (g, n.headline.isNotEmpty() ? n.headline : "Pas de résumé pour cette version."_fr, px::text (30.0f), px::white, tx, y, tw) + 4;
        for (const auto& p : n.points)
        {
            if (g != nullptr)
            {
                g->setColour (px::yellow);
                g->fillRect (tx + 2, y + 10, 6, 6);
            }
            y += wrapped (g, p, px::text (26.0f), px::grey, tx + 18, y, tw - 18) + 2;
        }
        y += 14;
    }
    if (more > 0)
        y += wrapped (g, "+ " + juce::String (more) + (more > 1 ? " versions plus anciennes : « Tout lire » les montre."_fr
                                                                 : " version plus ancienne : « Tout lire » la montre."_fr),
                      px::text (24.0f), px::dim, x, y, w) + 8;

    // what the installing says
    const auto state = updater.getState();
    if (mode == Mode::available && (state == Updater::State::Installing || state == Updater::State::Installed || state == Updater::State::Failed))
    {
        const auto colour = state == Updater::State::Failed ? px::red : state == Updater::State::Installed ? px::white : px::yellow;
        y += wrapped (g, updater.getStatusText(), px::text (26.0f), colour, x, y, w) + 8;
    }
    else if (mode == Mode::available)
       #if JUCE_MAC
        y += wrapped (g, "Elle remplace le plug-in et l'appli sur ce Mac (macOS peut demander ton mot de passe). Quitte et rouvre ton logiciel ensuite."_fr,
                      px::text (22.0f), px::dim, x, y, w) + 8;
       #else
        y += wrapped (g, "La page de téléchargement s'ouvre dans ton navigateur."_fr, px::text (22.0f), px::dim, x, y, w) + 8;
       #endif
    return y - area.getY();
}

void UpdatePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.82f));
    g.setColour (px::panel);
    g.fillRect (card);
    px::frameRect (g, card, mode == Mode::news ? px::white : px::yellow, 3);
    drawContent (&g, content);
}

void UpdatePanel::resized()
{
    const int w = juce::jmin (cardWidth, getWidth() - 40);
    const int textH = drawContent (nullptr, { 0, 0, w - 2 * pad, 10 });
    const int h = juce::jmin (getHeight() - 40, pad + textH + 16 + buttonHeight + 24);
    card = getLocalBounds().withSizeKeepingCentre (w, h);
    content = card.reduced (pad, 0).withTrimmedTop (24).withTrimmedBottom (buttonHeight + 40);

    auto row = card.reduced (pad, 0).removeFromBottom (buttonHeight + 24).withTrimmedBottom (24);
    auto place = [&row] (juce::TextButton& b, int width, bool right)
    {
        if (! b.isVisible())
            return;
        b.setBounds (right ? row.removeFromRight (width) : row.removeFromLeft (width));
        if (right) row.removeFromRight (10); else row.removeFromLeft (10);
    };
    place (install, 250, false);
    place (ok, 120, false);
    place (later, 140, false);
    place (readAll, 140, true);
    place (skip, juce::jmax (0, juce::jmin (250, row.getWidth())), true);
}
