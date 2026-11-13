#include "Pages.h"
#include "SoundSections.h"

namespace
{
    constexpr int cellW = 80, cellH = 104;   // the Kit and Effets pages
}

namespace
{

    using Def = ui::ParamDef;
    using SectionDef = ui::SectionDef;
    using ui::soundSections;

    // Names of the 11 effect parameters, per effect type (docs/SYSEX.md)
    const char* fxParamName (int type, int index)
    {
        static const char* names[9][11] = {
            { },
            { "Speed", "Depth", nullptr, "Delay" },
            { "Speed", "Depth", nullptr, nullptr, "Feedback", nullptr, nullptr, nullptr, "Polarity" },
            { "Speed", "Depth", nullptr, nullptr, "Feedback", "Center", "Spacing", nullptr, "Polarity" },
            { nullptr, "Drive", "Post Gain", nullptr, nullptr, "Cutoff" },
            { nullptr, nullptr, nullptr, "Length", "Feedback", "Cutoff", nullptr, nullptr, "Polarity", "Auto Pan", "Sync" },
            { "Speed", "Depth", "Sync", "Length", "Feedback", "Lo Cut", "Hi Cut", "Spread" },
            { "Size", "Shape", "Decay", "Predelay", nullptr, "Lo Cut", "Hi Cut", nullptr, "Diffusion", "Damping" },
            { nullptr, nullptr, nullptr, nullptr, "S&H Rate", "Drive", "Gain" },
        };
        return type >= 0 && type < 9 && index >= 0 && index < 11 ? names[type][index] : nullptr;
    }

    std::vector<std::string> soundNames()
    {
        std::vector<std::string> n;
        for (int i = 1; i <= 24; ++i)
            n.push_back ("Son " + ui::twoDigits (i).toStdString());
        return n;
    }

    juce::String shortShape (int value)
    {
        auto names = ra::valueNames (ra::Object::sound, 0x01);
        return value >= 0 && value < (int) names.size() ? juce::String (names[(size_t) value]) : juce::String (value);
    }
}

//==============================================================================
// SON

namespace
{
    // The Son page: the front panel in four columns, every setting at once. A modulation
    // amount carries its source under it.
    struct Spot { int address; const char* label; int source; };
    struct Box { const char* title; std::vector<std::vector<Spot>> rows; };

    const std::vector<std::vector<Box>>& soundColumns()
    {
        static const std::vector<std::vector<Box>> columns {
            { { "Oscillateur 1", { { { 0x01, "SHAPE", -1 }, { 0x02, "PITCH", -1 }, { 0x03, "DETUNE", -1 }, { 0x04, "START", -1 }, { 0x09, "FM", -1 } },
                                   { { 0x05, "P.MOD", 0x07 }, { 0x06, "P.CTRL", 0x08 }, { 0x0A, "FM MOD", 0x0C }, { 0x0B, "FM CTL", 0x0D } } } },
              { "Filtre",        { { { 0x26, "TYPE", -1 }, { 0x27, "CUTOFF", -1 }, { 0x28, "RESO", -1 }, { 0x29, "DRIVE", -1 }, { 0x2A, "KEYTRK", -1 } },
                                   { { 0x2B, "MOD 1", 0x2D }, { 0x2C, "CTL 1", 0x2E }, { 0x2F, "MOD 2", 0x31 }, { 0x30, "CTL 2", 0x32 } } } } },
            { { "Oscillateur 2", { { { 0x10, "SHAPE", -1 }, { 0x11, "PITCH", -1 }, { 0x12, "DETUNE", -1 }, { 0x13, "START", -1 } },
                                   { { 0x14, "P.MOD", 0x16 }, { 0x15, "P.CTRL", 0x17 } } } },
              { "Mix / Crack",   { { { 0x1A, "OSC 1", -1 }, { 0x1B, "OSC 2", -1 }, { 0x1C, "RING", -1 }, { 0x1D, "CRACK", -1 } },
                                   { { 0x24, "C.SPEED", -1 }, { 0x25, "C.LEN", -1 }, { 0x1E, "O2 MOD", 0x20 }, { 0x1F, "O2 CTL", 0x21 } } } } },
            { { "Ampli",         { { { 0x35, "VOLUME", -1 }, { 0x3A, "PAN", -1 } },
                                   { { 0x36, "V.MOD", 0x38 }, { 0x37, "V.CTRL", 0x39 }, { 0x3B, "PAN MOD", 0x3D }, { 0x3C, "PAN CTL", 0x3E } } } },
              { "Sortie",        { { { 0x55, "OUT", -1 }, { 0x56, "FX", -1 }, { 0x57, "SEND", -1 }, { 0x5E, "XOR", -1 } },
                                   { { 0x58, "TUNED", -1 }, { 0x59, "TRANSP", -1 }, { 0x5A, "LO KEY", -1 }, { 0x5B, "HI KEY", -1 } },
                                   { { 0x6C, "PAT.INT", -1 } } } } },
            { { "Enveloppe 1",   { { { 0x3F, "ATTACK", -1 }, { 0x40, "DECAY", -1 }, { 0x41, "SUSTAIN", -1 }, { 0x42, "RELEASE", -1 }, { 0x43, "SHAPE", -1 } } } },
              { "Enveloppe 2",   { { { 0x45, "ATTACK", -1 }, { 0x46, "DECAY", -1 }, { 0x47, "SUSTAIN", -1 }, { 0x48, "RELEASE", -1 }, { 0x49, "SHAPE", -1 } } } },
              { "LFO 1",         { { { 0x4B, "SPEED", -1 }, { 0x4C, "SHAPE", -1 }, { 0x4D, "SYNC", -1 }, { 0x4E, "1-SHOT", -1 } } } },
              { "LFO 2",         { { { 0x4F, "SPEED", -1 }, { 0x50, "SHAPE", -1 }, { 0x51, "SYNC", -1 }, { 0x52, "MOD", -1 }, { 0x53, "MOD SRC", -1 } } } } },
        };
        return columns;
    }

    bool isChoice (int address)
    {
        for (auto& section : soundSections())
            for (auto& row : section.rows)
                for (auto& d : row)
                    if (d.address == address)
                        return d.choice;
        return ! ra::valueNames (ra::Object::sound, address).empty();
    }

    constexpr int spotW = 68, knobRowH = 100, sourceRowH = 128, boxPad = 8;
}

void SoundPage::Content::layout (int width)
{
    const auto& columns = soundColumns();
    std::vector<int> widths;
    int total = 0;
    for (auto& column : columns)
    {
        int cols = 1;
        for (auto& box : column)
            for (auto& row : box.rows)
                cols = juce::jmax (cols, (int) row.size());
        widths.push_back (cols * spotW + 2 * boxPad);
        total += widths.back();
    }
    const int gap = juce::jmax (8, (width - total) / juce::jmax (1, (int) columns.size() - 1));

    int x = 0, card = 0;
    for (size_t c = 0; c < columns.size(); ++c)
    {
        int y = 0;
        for (auto& box : columns[c])
        {
            auto& rows = cells[(size_t) card];
            int h = cards[card]->titleHeight();
            for (size_t r = 0; r < box.rows.size(); ++r)
            {
                bool sources = false;
                for (auto& spot : box.rows[r])
                    sources = sources || spot.source >= 0;
                const int rowH = sources ? sourceRowH : knobRowH;
                for (size_t i = 0; i < rows[r].size(); ++i)
                    rows[r][i]->setBounds (boxPad + (int) i * spotW, h, spotW, rowH);
                h += rowH;
            }
            h += boxPad;
            cards[card]->setBounds (x, y, widths[c], h);
            y += h + 12;
            ++card;
        }
        x += widths[c] + gap;
    }
}

SoundPage::SoundPage (Engine& e, Library& l) : engine (e), library (l)
{
    for (int i = 0; i < 24; ++i)
    {
        auto* b = soundButtons.add (new juce::TextButton (ui::twoDigits (i + 1)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.3f));
        b->onClick = [this, i] { selectSound (i); };
        addAndMakeVisible (b);
    }

    title.setFont (ui::font (20.0f, true));
    addAndMakeVisible (title);

    for (auto* b : { &play, &save, &load, &copy, &paste, &saveOk, &saveCancel })
        addAndMakeVisible (b);
    ui::styleBig (play, true);
    play.onClick = [this] { engine.playSound (sound); };
    copy.onClick = [this]
    {
        clipboard = engine.getKit().soundData (sound);
        status.show ("Son "_fr + ui::twoDigits (sound + 1) + " copié. Choisis un autre son, puis « Coller »."_fr);
    };
    paste.onClick = [this]
    {
        if (clipboard.empty())
            status.show ("Rien à coller : copie d'abord un son."_fr, ui::warning);
        else
        {
            status.show ("Envoi…"_fr, ui::dim);
            engine.sendSound (clipboard, sound);
        }
    };
    load.onClick = [this] { showLoadMenu(); };

    saveName.setFont (ui::font (15.0f));
    addChildComponent (saveName);
    saveOk.setVisible (false);
    saveCancel.setVisible (false);
    auto showSave = [this] (bool show)
    {
        saveName.setVisible (show);
        saveOk.setVisible (show);
        saveCancel.setVisible (show);
        save.setVisible (! show);
        resized();
        if (show) saveName.grabKeyboardFocus();
    };
    save.onClick = [this, showSave]
    {
        saveName.setText (juce::String (engine.getKit().name()).trim() + " - son " + ui::twoDigits (sound + 1));
        showSave (true);
    };
    saveCancel.onClick = [showSave] { showSave (false); };
    saveOk.onClick = [this, showSave]
    {
        auto file = library.saveSound (engine.getKit().soundData (sound), saveName.getText());
        showSave (false);
        if (file.existsAsFile())
            status.show ("Enregistré dans Mes sons : "_fr + file.getFileNameWithoutExtension(), ui::accent);
        else
            status.show ("Le son n'a pas pu être enregistré."_fr, ui::bad);
    };
    saveName.onReturnKey = [this] { saveOk.triggerClick(); };

    addAndMakeVisible (status);

    // the controls, built from the front panel's columns
    const auto sources = [] (int address) { return ra::valueNames (ra::Object::sound, address); };
    for (auto& column : soundColumns())
        for (auto& box : column)
        {
            auto* card = content.cards.add (new ui::Card (juce::String::fromUTF8 (box.title), true));
            content.addAndMakeVisible (card);
            content.cells.emplace_back();
            for (auto& row : box.rows)
            {
                content.cells.back().emplace_back();
                for (auto& spot : row)
                {
                    auto* cell = new ui::Cell (spot.label, isChoice (spot.address) ? ra::valueNames (ra::Object::sound, spot.address)
                                                                                 : std::vector<std::string>());
                    cell->onChange = [this, a = spot.address] (int v) { engine.setSoundParameter (sound, a, v); };
                    cell->format = [a = spot.address] (int v) { return ui::valueText (a, v); };
                    cell->centred = ui::isBipolar (spot.address);
                    if (spot.source >= 0)
                    {
                        cell->addSource (sources (spot.source));
                        cell->onSource = [this, a = spot.source] (int v) { engine.setSoundParameter (sound, a, v); };
                    }
                    controls.add (cell);
                    card->addAndMakeVisible (cell);
                    content.cells.back().back().push_back (cell);
                    params.push_back ({ spot.address, cell, spot.source });
                }
            }
        }
    addAndMakeVisible (content);

    engine.addListener (this);
    selectSound (0);
}

SoundPage::~SoundPage()   { engine.removeListener (this); }

void SoundPage::selectSound (int s)
{
    sound = juce::jlimit (0, 23, s);
    soundButtons[sound]->setToggleState (true, juce::dontSendNotification);
    status.show ({});
    refresh();
}

void SoundPage::refresh()
{
    const auto& kit = engine.getShownKit();     // the kit heard, or the end of the morph armed
    title.setText ("Son "_fr + ui::twoDigits (sound + 1) + "   " + shortShape (kit.soundValue (sound, 0x01))
                   + " / " + shortShape (kit.soundValue (sound, 0x10)), juce::dontSendNotification);
    for (auto& p : params)
    {
        p.cell->setValue (kit.soundValue (sound, p.address));
        if (p.source >= 0)
            p.cell->setSource (kit.soundValue (sound, p.source));
    }
}

void SoundPage::kitChanged (const ra::Kit::Change& c)
{
    using W = ra::Kit::Change::What;
    if (c.what == W::whole || (c.what == W::sound && c.index == sound))
        refresh();
}

void SoundPage::taskFinished (const juce::String& task, bool, const juce::String&)
{
    if (task == "sendSound")
        status.show ({});   // the result is in the status bar
}

void SoundPage::showLoadMenu()
{
    auto sounds = library.sounds();
    if (sounds.isEmpty())
    {
        status.show ("Pas encore de son enregistré : utilise « Enregistrer ce son »."_fr, ui::warning);
        return;
    }
    juce::PopupMenu menu;
    for (int i = 0; i < sounds.size(); ++i)
        menu.addItem (i + 1, sounds[i].title);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&load), [this, sounds] (int r)
    {
        if (r <= 0) return;
        if (auto data = Library::loadSound (sounds[r - 1].file))
        {
            status.show ("Envoi de « "_fr + sounds[r - 1].title + " » dans le son "_fr + ui::twoDigits (sound + 1) + "…"_fr, ui::dim);
            engine.sendSound (*data, sound);
        }
        else
            status.show ("Ce fichier ne contient pas de son lisible."_fr, ui::bad);
    });
}

void SoundPage::paint (juce::Graphics& g)   { g.fillAll (ui::background); }

void SoundPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (64);
    auto grid = top.removeFromLeft (12 * 36);
    for (int i = 0; i < 24; ++i)
        soundButtons[i]->setBounds (grid.getX() + (i % 12) * 36, grid.getY() + (i / 12) * 32, 34, 30);
    top.removeFromLeft (16);
    title.setBounds (top.removeFromTop (30));
    auto buttons = top.removeFromTop (30);
    play.setBounds (buttons.removeFromLeft (120)); buttons.removeFromLeft (8);
    if (save.isVisible())
    {
        save.setBounds (buttons.removeFromLeft (170)); buttons.removeFromLeft (8);
    }
    else
    {
        saveName.setBounds (buttons.removeFromLeft (220)); buttons.removeFromLeft (4);
        saveOk.setBounds (buttons.removeFromLeft (44)); buttons.removeFromLeft (4);
        saveCancel.setBounds (buttons.removeFromLeft (80)); buttons.removeFromLeft (8);
    }
    load.setBounds (buttons.removeFromLeft (140)); buttons.removeFromLeft (8);
    copy.setBounds (buttons.removeFromLeft (80)); buttons.removeFromLeft (4);
    paste.setBounds (buttons.removeFromLeft (80));

    r.removeFromTop (4);
    status.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    content.setBounds (r);
    content.layout (r.getWidth());
}

//==============================================================================
// KIT

KitPage::Pad::Pad (KitPage& o, int i) : ui::Card(), owner (o), index (i)
{
    number.setText (ui::twoDigits (i + 1), juce::dontSendNotification);
    number.setFont (ui::font (20.0f, true));
    addAndMakeVisible (number);
    number.setInterceptsMouseClicks (false, false);
    shapes.setInterceptsMouseClicks (false, false);
    shapes.setFont (ui::font (12.0f));
    shapes.setColour (juce::Label::textColourId, ui::dim);
    shapes.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (shapes);

    play.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));
    ui::styleBig (play, true);
    play.onClick = [this] { owner.selectPad (index); owner.engine.playSound (index); };
    edit.setButtonText ("Éditer"_fr);
    edit.onClick = [this] { if (owner.onEditSound) owner.onEditSound (index); };
    addAndMakeVisible (play);
    addAndMakeVisible (edit);

    volume.onChange = [this] (int v) { owner.engine.setSoundParameter (index, 0x35, v); };
    pan.onChange = [this] (int v) { owner.engine.setSoundParameter (index, 0x3A, v); };
    pan.format = [] (int v) { return ui::valueText (0x3A, v); };
    pan.centred = true;
    addAndMakeVisible (volume);
    addAndMakeVisible (pan);

    out = std::make_unique<ui::Choice> ("Sortie", ra::valueNames (ra::Object::sound, 0x55));
    out->onChange = [this] (int v) { owner.engine.setSoundParameter (index, 0x55, v); };
    addAndMakeVisible (*out);
}

void KitPage::Pad::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (26);
    number.setBounds (top.removeFromLeft (40));
    play.setBounds (top.removeFromRight (34));
    top.removeFromRight (4);
    edit.setBounds (top.removeFromRight (58));
    shapes.setBounds (r.removeFromTop (16));
    auto knobs = r.removeFromTop (juce::jmax (60, r.getHeight() - 46));
    volume.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    pan.setBounds (knobs);
    out->setBounds (r.removeFromTop (44));
}

void KitPage::Pad::mouseUp (const juce::MouseEvent&)
{
    owner.selectPad (index);
    owner.engine.playSound (index);
}

void KitPage::Pad::paintOverChildren (juce::Graphics& g)
{
    if (selected)
    {
        px::frameRect (g, getLocalBounds(), px::white, 3);
    }
}

KitPage::KitPage (Engine& e) : engine (e)
{
    addAndMakeVisible (programCard);
    programCard.addAndMakeVisible (programBox);
    programBox.onChange = [this]
    {
        if (programBox.getSelectedId() > 0)
            engine.selectProgram (programBox.getSelectedId() - 1);
    };
    programCard.addAndMakeVisible (reread);
    reread.onClick = [this] { engine.readKit(); };

    nameLabel.setText ("Nom", juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, ui::dim);
    programCard.addAndMakeVisible (nameLabel);
    name.setFont (ui::font (16.0f));
    name.setInputRestrictions (16);
    name.onReturnKey = [this] { engine.setKitName (name.getText()); };
    name.onFocusLost = [this] { if (name.getText() != juce::String (engine.getKit().name())) engine.setKitName (name.getText()); };
    programCard.addAndMakeVisible (name);

    midiMode = std::make_unique<ui::Choice> ("Mode MIDI", ra::valueNames (ra::Object::program, 0x11));
    midiMode->onChange = [this] (int v) { engine.setProgramParameter (0x11, v); };
    programCard.addAndMakeVisible (*midiMode);

    const char* triggerNames[3] = { "Trigger A", "Trigger B", "Trigger C" };
    for (int t = 0; t < 3; ++t)
    {
        triggerSound[(size_t) t] = std::make_unique<ui::Choice> (triggerNames[t], soundNames());
        triggerSound[(size_t) t]->onChange = [this, t] (int v) { engine.setProgramParameter (0x14 + 2 * t, v); };
        programCard.addAndMakeVisible (*triggerSound[(size_t) t]);
        triggerVelocity[(size_t) t] = std::make_unique<ui::Cell> ("Vélocité"_fr);
        triggerVelocity[(size_t) t]->onChange = [this, t] (int v) { engine.setProgramParameter (0x15 + 2 * t, v); };
        programCard.addAndMakeVisible (*triggerVelocity[(size_t) t]);
    }

    hint.setText ("Les changements partent tout de suite vers le RackAttack, sans être sauvegardés.\n"_fr
                  "Pour les garder : Shift + Store sur la machine. Pour revenir en arrière : Shift + Recall."_fr,
                  juce::dontSendNotification);
    hint.setFont (ui::font (13.0f));
    hint.setColour (juce::Label::textColourId, ui::dim);
    hint.setJustificationType (juce::Justification::topLeft);
    programCard.addAndMakeVisible (hint);

    for (int i = 0; i < 24; ++i)
        addAndMakeVisible (pads.add (new Pad (*this, i)));

    fillPrograms();
    engine.addListener (this);
    refresh();
    selectPad (0);
}

KitPage::~KitPage()   { engine.removeListener (this); }

void KitPage::selectPad (int i)
{
    selectedPad = i;
    for (auto* p : pads)
    {
        p->selected = p->index == i;
        p->repaint();
    }
}

void KitPage::fillPrograms()
{
    programBox.clear (juce::dontSendNotification);
    const auto& names = engine.getProgramNames();
    for (int p = 0; p < 50; ++p)
        programBox.addItem ("P" + ui::twoDigits (p + 1) + (names[p].isNotEmpty() ? "   " + names[p] : juce::String()), p + 1);
    if (engine.getCurrentProgram() >= 0)
        programBox.setSelectedId (engine.getCurrentProgram() + 1, juce::dontSendNotification);
    else
        programBox.setText ("Programme ?", juce::dontSendNotification);
}

void KitPage::refreshPad (int i)
{
    const auto& kit = engine.getShownKit();
    auto* p = pads[i];
    p->shapes.setText (shortShape (kit.soundValue (i, 0x01)) + " / " + shortShape (kit.soundValue (i, 0x10)), juce::dontSendNotification);
    p->volume.setValue (kit.soundValue (i, 0x35));
    p->pan.setValue (kit.soundValue (i, 0x3A));
    p->out->setValue (kit.soundValue (i, 0x55));
}

void KitPage::refresh()
{
    const auto& kit = engine.getKit();
    if (! name.hasKeyboardFocus (true))
        name.setText (juce::String (kit.name()), juce::dontSendNotification);
    midiMode->setValue (kit.programValue (0x11));
    for (int t = 0; t < 3; ++t)
    {
        triggerSound[(size_t) t]->setValue (kit.programValue (0x14 + 2 * t));
        triggerVelocity[(size_t) t]->setValue (kit.programValue (0x15 + 2 * t));
    }
    for (int i = 0; i < 24; ++i)
        refreshPad (i);
}

void KitPage::kitChanged (const ra::Kit::Change& c)
{
    using W = ra::Kit::Change::What;
    if (c.what == W::sound)
        refreshPad (c.index);
    else if (c.what == W::program || c.what == W::whole)
        refresh();
}

void KitPage::taskFinished (const juce::String& task, bool, const juce::String&)
{
    if (task == "kit" || task == "connect" || task == "names" || task == "backup" || task == "sendKit")
    {
        fillPrograms();
        refresh();
    }
}

void KitPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto left = r.removeFromLeft (300);
    r.removeFromLeft (12);
    programCard.setBounds (left);

    auto c = programCard.getLocalBounds().reduced (12);
    c.removeFromTop (programCard.titleHeight() - 4);
    auto row = c.removeFromTop (30);
    reread.setBounds (row.removeFromRight (70));
    row.removeFromRight (6);
    programBox.setBounds (row);
    c.removeFromTop (12);
    nameLabel.setBounds (c.removeFromTop (18));
    name.setBounds (c.removeFromTop (30));
    c.removeFromTop (10);
    midiMode->setBounds (c.removeFromTop (48));
    c.removeFromTop (10);
    for (int t = 0; t < 3; ++t)
    {
        auto tr = c.removeFromTop (cellH);
        triggerSound[(size_t) t]->setBounds (tr.removeFromLeft (170).withSizeKeepingCentre (170, 48));
        triggerVelocity[(size_t) t]->setBounds (tr.removeFromLeft (cellW));
    }
    c.removeFromTop (10);
    hint.setBounds (c);

    const int cols = 6, rows = 4, gap = 8;
    int pw = (r.getWidth() - gap * (cols - 1)) / cols;
    int ph = (r.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < 24; ++i)
        pads[i]->setBounds (r.getX() + (i % cols) * (pw + gap), r.getY() + (i / cols) * (ph + gap), pw, ph);
}

//==============================================================================
// EFFETS

EffectsPage::Column::Column (EffectsPage& o, int i) : ui::Card ("FX" + juce::String (i + 1)), owner (o), index (i)
{
    type = std::make_unique<ui::Choice> ("Type", ra::valueNames (ra::Object::effect, 0x00));
    type->onChange = [this] (int v) { owner.engine.setEffectParameter (index, 0x00, v); refresh(); };
    addAndMakeVisible (*type);
    out = std::make_unique<ui::Choice> ("Sortie", ra::valueNames (ra::Object::effect, 0x01));
    out->onChange = [this] (int v) { owner.engine.setEffectParameter (index, 0x01, v); };
    addAndMakeVisible (*out);
    for (int k = 0; k < 11; ++k)
    {
        auto* knob = knobs.add (new ui::Cell ({}));
        knob->onChange = [this, k] (int v) { owner.engine.setEffectParameter (index, 0x02 + k, v); };
        addChildComponent (knob);
    }
}

void EffectsPage::Column::refresh()
{
    const auto& kit = owner.engine.getShownKit();
    int t = kit.effectValue (index, 0x00);
    type->setValue (t);
    out->setValue (kit.effectValue (index, 0x01));
    for (int k = 0; k < 11; ++k)
    {
        auto* n = fxParamName (t, k);
        knobs[k]->setVisible (n != nullptr);
        if (n != nullptr)
        {
            knobs[k]->setLabel (n);
            knobs[k]->setValue (kit.effectValue (index, 0x02 + k));
        }
    }
    resized();
}

void EffectsPage::Column::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (titleHeight() - 6);
    type->setBounds (r.removeFromTop (48));
    out->setBounds (r.removeFromTop (48));
    r.removeFromTop (8);
    int x = 0, y = 0;
    int cols = juce::jmax (1, r.getWidth() / cellW);
    for (auto* k : knobs)
        if (k->isVisible())
        {
            k->setBounds (r.getX() + x * cellW, r.getY() + y * cellH, cellW, cellH);
            if (++x >= cols) { x = 0; ++y; }
        }
}

EffectsPage::EffectsPage (Engine& e) : engine (e)
{
    for (int i = 0; i < 4; ++i)
        addAndMakeVisible (columns.add (new Column (*this, i)));
    hint.setText ("Delay, ModDelay et Reverb ne marchent que sur FX1 et FX2. Les sons choisissent leur effet dans Son > Sortie."_fr,
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, ui::dim);
    addAndMakeVisible (hint);
    engine.addListener (this);
    for (auto* c : columns) c->refresh();
}

EffectsPage::~EffectsPage()   { engine.removeListener (this); }

void EffectsPage::kitChanged (const ra::Kit::Change& c)
{
    using W = ra::Kit::Change::What;
    if (c.what == W::effect && c.index >= 0 && c.index < 4)
        columns[c.index]->refresh();
    else if (c.what == W::whole)
        for (auto* col : columns) col->refresh();
}

void EffectsPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    hint.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (8);
    int w = (r.getWidth() - 3 * 10) / 4;
    for (int i = 0; i < 4; ++i)
        columns[i]->setBounds (r.getX() + i * (w + 10), r.getY(), w, r.getHeight());
}

//==============================================================================
// BIBLIOTHÈQUE

void LibraryPage::ProgramList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll (ui::accent.withAlpha (0.25f));
    bool current = row == owner.engine.getCurrentProgram();
    g.setColour (current ? ui::accent : ui::text);
    g.setFont (ui::font (15.0f, current));
    auto name = owner.engine.getProgramNames()[row];
    g.drawText ("P" + ui::twoDigits (row + 1) + "    " + (name.isNotEmpty() ? name : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))),
                10, 0, w - 20, h, juce::Justification::centredLeft);
}

void LibraryPage::ProgramList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    owner.status.show ("Le RackAttack passe sur le programme P"_fr + ui::twoDigits (row + 1) + "…"_fr, ui::dim);
    owner.engine.selectProgram (row);
}

void LibraryPage::FileList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= owner.items.size())
        return;
    if (selected)
        g.fillAll (ui::accent.withAlpha (0.25f));
    auto& item = owner.items.getReference (row);
    g.setColour (ui::text);
    g.setFont (ui::font (15.0f));
    g.drawText (item.title, 10, 0, w - 200, h, juce::Justification::centredLeft);
    g.setColour (ui::dim);
    g.setFont (ui::font (12.0f));
    g.drawText (item.group, w - 190, 0, 180, h, juce::Justification::centredRight);
}

LibraryPage::LibraryPage (Engine& e, Library& l, MorphControl& m, std::function<int()> sel)
    : engine (e), library (l), morph (m), selectedSound (std::move (sel))
{
    addAndMakeVisible (machineCard);
    addAndMakeVisible (computerCard);

    for (auto* b : { &readNames, &backup })
        machineCard.addAndMakeVisible (b);
    ui::styleBig (backup, true);
    readNames.onClick = [this]
    {
        askFirst ("Pour lire les noms, le RackAttack passe par ses 50 programmes (environ 30 secondes)."_fr,
                  [this] { engine.readProgramNames(); });
    };
    backup.onClick = [this]
    {
        askFirst ("Le RackAttack va passer par ses 50 programmes pour tout copier sur l'ordinateur (2 à 3 minutes)."_fr,
                  [this]
        {
            auto folder = library.newBackupFolder();
            engine.backupAllPrograms (folder);
        });
    };
    machineHint.setText ("Double-clic sur un programme : le RackAttack passe dessus."_fr, juce::dontSendNotification);
    machineHint.setColour (juce::Label::textColourId, ui::dim);
    machineCard.addAndMakeVisible (machineHint);
    programs.setRowHeight (26);
    programs.setColour (juce::ListBox::backgroundColourId, ui::panelLight);
    machineCard.addAndMakeVisible (programs);

    confirmText.setFont (ui::font (14.0f));
    confirmText.setColour (juce::Label::textColourId, ui::warning);
    confirmText.setJustificationType (juce::Justification::topLeft);
    machineCard.addChildComponent (confirmText);
    machineCard.addChildComponent (confirmYes);
    machineCard.addChildComponent (confirmNo);
    ui::styleBig (confirmYes, true);
    confirmYes.onClick = [this]
    {
        for (auto* c : std::initializer_list<juce::Component*> { &confirmText, &confirmYes, &confirmNo }) c->setVisible (false);
        resized();
        if (confirmed) confirmed();
        confirmed = nullptr;
    };
    confirmNo.onClick = [this]
    {
        for (auto* c : std::initializer_list<juce::Component*> { &confirmText, &confirmYes, &confirmNo }) c->setVisible (false);
        resized();
        confirmed = nullptr;
    };

    for (auto* b : { &showKits, &showSounds, &showMorphs })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2001);
        b->setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.3f));
        b->onClick = [this] { refreshFiles(); };
        computerCard.addAndMakeVisible (b);
    }
    showKits.setToggleState (true, juce::dontSendNotification);

    files.setRowHeight (26);
    files.setColour (juce::ListBox::backgroundColourId, ui::panelLight);
    computerCard.addAndMakeVisible (files);
    emptyHint.setColour (juce::Label::textColourId, ui::dim);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setInterceptsMouseClicks (false, false);
    computerCard.addAndMakeVisible (emptyHint);

    for (auto* b : { &saveKit, &sendButton, &openFolder, &saveOk, &saveCancel })
        computerCard.addAndMakeVisible (b);
    ui::styleBig (sendButton, true);
    saveName.setFont (ui::font (15.0f));
    computerCard.addChildComponent (saveName);
    saveOk.setVisible (false);
    saveCancel.setVisible (false);

    auto showSave = [this] (bool show)
    {
        saveName.setVisible (show);
        saveOk.setVisible (show);
        saveCancel.setVisible (show);
        saveKit.setVisible (! show);
        resized();
        if (show) saveName.grabKeyboardFocus();
    };
    saveKit.onClick = [this, showSave]
    {
        if (morphsShown())
        {
            if (! morph.differs())
            {
                status.show ("A et B sont pareils : rien à morpher. Clic droit sur un kit : « Charger dans B »."_fr, ui::warning);
                return;
            }
            saveName.setText (juce::String (engine.getKit().name()).trim() + " morph");
            showSave (true);
            return;
        }
        if (! engine.getKit().isComplete())
        {
            status.show ("Le kit n'est pas encore lu en entier : branche le RackAttack ou « Relire » dans Kit."_fr, ui::warning);
            return;
        }
        saveName.setText (juce::String (engine.getKit().name()).trim());
        showSave (true);
    };
    saveCancel.onClick = [showSave] { showSave (false); };
    saveOk.onClick = [this, showSave]
    {
        if (morphsShown())
        {
            auto file = library.saveMorph (morph.preset(), saveName.getText());
            showSave (false);
            refreshFiles();
            if (file.existsAsFile())
                status.show ("Enregistré dans Mes morphs : "_fr + file.getFileNameWithoutExtension() + " (A, B et la position du fader)."_fr, ui::accent);
            else
                status.show ("Le morph n'a pas pu être enregistré."_fr, ui::bad);
            return;
        }
        auto file = library.saveKit (engine.getKit(), saveName.getText());
        showSave (false);
        showKits.setToggleState (true, juce::dontSendNotification);
        refreshFiles();
        if (file.existsAsFile())
            status.show ("Enregistré dans Mes kits : "_fr + file.getFileNameWithoutExtension(), ui::accent);
        else
            status.show ("Le kit n'a pas pu être enregistré."_fr, ui::bad);
    };
    saveName.onReturnKey = [this] { saveOk.triggerClick(); };

    sendButton.onClick = [this] { sendSelected (files.getSelectedRow()); };
    openFolder.onClick = [this] { library.getRoot().createDirectory(); library.getRoot().startAsProcess(); };

    addAndMakeVisible (status);
    engine.addListener (this);
    refreshFiles();
}

LibraryPage::~LibraryPage()   { engine.removeListener (this); }

void LibraryPage::askFirst (const juce::String& question, std::function<void()> then)
{
    if (engine.isBusy())
    {
        status.show ("Attends la fin de ce qui est en cours."_fr, ui::warning);
        return;
    }
    confirmText.setText (question + "\nSi tu as modifié le kit en cours sans le sauvegarder, fais d'abord Shift + Store sur la machine."_fr,
                         juce::dontSendNotification);
    confirmed = std::move (then);
    for (auto* c : std::initializer_list<juce::Component*> { &confirmText, &confirmYes, &confirmNo }) c->setVisible (true);
    resized();
}

void LibraryPage::refreshFiles()
{
    const bool kits = showKits.getToggleState(), morphs = morphsShown();
    items = morphs ? library.morphs() : (kits ? library.kits() : library.sounds());
    files.updateContent();
    files.repaint();
    emptyHint.setText (items.isEmpty() ? (morphs ? "Aucun morph enregistré. Clic droit sur des kits : « Charger dans A », « Charger dans B », puis « Enregistrer le morph »."_fr
                                          : kits ? "Aucun kit sur l'ordinateur pour l'instant. « Enregistrer le kit en cours » ou « Tout sauvegarder »."_fr
                                                 : "Aucun son enregistré pour l'instant. Page Son : « Enregistrer ce son »."_fr)
                                       : juce::String(), juce::dontSendNotification);
    sendButton.setButtonText (morphs ? juce::String ("Charger le morph")
                                     : kits ? juce::String ("Envoyer au RackAttack")
                                            : "Envoyer dans le son "_fr + ui::twoDigits (selectedSound() + 1));
    saveKit.setButtonText (morphs ? juce::String ("Enregistrer le morph") : juce::String ("Enregistrer le kit en cours"));
}

void LibraryPage::sendSelected (int row)
{
    if (row < 0 || row >= items.size())
    {
        status.show ("Choisis d'abord un kit ou un son dans la liste."_fr, ui::warning);
        return;
    }
    if (engine.isBusy())
    {
        status.show ("Attends la fin de ce qui est en cours."_fr, ui::warning);
        return;
    }
    auto& item = items.getReference (row);
    if (item.isMorph)
    {
        auto data = Library::loadMorph (item.file);
        if (data && morph.loadPreset (*data))
            status.show ("Morph « "_fr + item.title + " » chargé : A, B et la position du fader."_fr, ui::accent);
        else
            status.show ("Ce fichier ne contient pas de morph lisible."_fr, ui::bad);
        return;
    }
    if (item.isSound)
    {
        if (auto data = Library::loadSound (item.file))
        {
            status.show ("Envoi de « "_fr + item.title + " »…"_fr, ui::dim);
            engine.sendSound (*data, selectedSound());
        }
        else
            status.show ("Ce fichier ne contient pas de son lisible."_fr, ui::bad);
    }
    else
    {
        if (auto kit = Library::loadKit (item.file))
        {
            status.show ("Envoi du kit « "_fr + item.title + " »…"_fr, ui::dim);
            engine.sendKit (*kit);
        }
        else
            status.show ("Ce fichier ne contient pas de kit lisible."_fr, ui::bad);
    }
}

void LibraryPage::showFileMenu (int row)
{
    if (row < 0 || row >= items.size())
        return;
    files.selectRow (row);
    const auto& item = items.getReference (row);
    const auto slot = ui::twoDigits (selectedSound() + 1);
    juce::PopupMenu m;
    m.addSectionHeader (item.title.toUpperCase());
    if (item.isMorph)
        m.addItem (1, "Charger le morph (A, B et la position)"_fr);
    else if (item.isSound)
    {
        m.addItem (1, "Envoyer dans le son "_fr + slot);
        m.addSeparator();
        m.addItem (2, "Charger dans A (son "_fr + slot + ")");
        m.addItem (3, "Charger dans B (son "_fr + slot + ")");
    }
    else
    {
        m.addItem (1, "Envoyer au RackAttack"_fr);
        m.addSeparator();
        m.addItem (2, "Charger dans A"_fr);
        m.addItem (3, "Charger dans B"_fr);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                     [safe = juce::Component::SafePointer<LibraryPage> (this), row] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->sendSelected (row);
        if (r == 2) safe->loadIntoMorph (row, MorphControl::Side::a);
        if (r == 3) safe->loadIntoMorph (row, MorphControl::Side::b);
    });
}

void LibraryPage::loadIntoMorph (int row, MorphControl::Side side)
{
    if (row < 0 || row >= items.size())
        return;
    if (! engine.getKit().isComplete())
    {
        status.show ("Le kit du RackAttack n'est pas encore lu : connecte-le d'abord (ou lance la simulation)."_fr, ui::warning);
        return;
    }
    const auto& item = items.getReference (row);
    if (item.isMorph)
        return;
    const juce::String end = side == MorphControl::Side::a ? "A" : "B";
    if (item.isSound)
    {
        if (auto data = Library::loadSound (item.file))
        {
            const int slot = selectedSound();
            morph.loadSound (side, slot, *data);
            status.show ("« "_fr + item.title + " » est dans "_fr + end + " pour le son "_fr + ui::twoDigits (slot + 1)
                         + ". Le fader Morph, en bas, va de A à B."_fr, ui::accent);
        }
        else
            status.show ("Ce fichier ne contient pas de son lisible."_fr, ui::bad);
    }
    else
    {
        if (auto kit = Library::loadKit (item.file))
        {
            morph.loadKit (side, *kit);
            status.show ("Kit « "_fr + item.title + " » dans "_fr + end + ". Le fader Morph, en bas, va de A à B."_fr, ui::accent);
        }
        else
            status.show ("Ce fichier ne contient pas de kit lisible."_fr, ui::bad);
    }
}

void LibraryPage::taskFinished (const juce::String& task, bool ok, const juce::String& message)
{
    if (task == "connect")
    {
        programs.updateContent();
        programs.repaint();
    }
    juce::ignoreUnused (ok, message);
    if (task == "names" || task == "backup" || task == "sendKit" || task == "sendSound" || task == "kit")
    {
        status.show ({});
        programs.updateContent();
        programs.repaint();
        if (task == "backup")
            refreshFiles();
    }
}

void LibraryPage::progressChanged (int done, int total, const juce::String& what)
{
    juce::ignoreUnused (what);
    if (total == 50)
    {
        programs.updateContent();   // the names appear as they are read
        programs.repaint();
        juce::ignoreUnused (done);
    }
}

void LibraryPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    status.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (8);
    auto left = r.removeFromLeft (r.getWidth() / 2 - 6);
    r.removeFromLeft (12);
    machineCard.setBounds (left);
    computerCard.setBounds (r);

    auto m = machineCard.getLocalBounds().reduced (12);
    m.removeFromTop (machineCard.titleHeight() - 4);
    auto row = m.removeFromTop (34);
    readNames.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (8);
    backup.setBounds (row.removeFromLeft (180));
    if (confirmText.isVisible())
    {
        m.removeFromTop (8);
        confirmText.setBounds (m.removeFromTop (60));
        auto cr = m.removeFromTop (32);
        confirmYes.setBounds (cr.removeFromLeft (130));
        cr.removeFromLeft (8);
        confirmNo.setBounds (cr.removeFromLeft (100));
    }
    m.removeFromTop (8);
    machineHint.setBounds (m.removeFromBottom (22));
    programs.setBounds (m);

    auto c = computerCard.getLocalBounds().reduced (12);
    c.removeFromTop (computerCard.titleHeight() - 4);
    auto tabs = c.removeFromTop (32);
    showKits.setBounds (tabs.removeFromLeft (100));
    tabs.removeFromLeft (4);
    showSounds.setBounds (tabs.removeFromLeft (100));
    tabs.removeFromLeft (4);
    showMorphs.setBounds (tabs.removeFromLeft (100));
    c.removeFromTop (8);
    auto bottom = c.removeFromBottom (34);
    if (saveKit.isVisible())
    {
        saveKit.setBounds (bottom.removeFromLeft (264));
    }
    else
    {
        saveName.setBounds (bottom.removeFromLeft (200));
        bottom.removeFromLeft (4);
        saveOk.setBounds (bottom.removeFromLeft (44));
        bottom.removeFromLeft (4);
        saveCancel.setBounds (bottom.removeFromLeft (80));
    }
    openFolder.setBounds (bottom.removeFromRight (176));
    c.removeFromBottom (8);
    sendButton.setBounds (c.removeFromBottom (34).removeFromLeft (260));
    c.removeFromBottom (8);
    files.setBounds (c);
    emptyHint.setBounds (c.reduced (20));
    emptyHint.getProperties().set ("overlay", true);     // written over the empty list, on purpose
}
