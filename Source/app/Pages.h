// The four main pages: Kit, Son (the front panel of one sound), Effets, Bibliothèque.

#pragma once

#include <JuceHeader.h>

#include "Engine.h"
#include "Library.h"
#include "MorphControl.h"
#include "Widgets.h"

//==============================================================================
class SoundPage : public juce::Component,
                  private Engine::Listener
{
public:
    SoundPage (Engine&, Library&);
    ~SoundPage() override;

    void selectSound (int sound);
    int getSelectedSound() const   { return sound; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Param
    {
        int address;
        ui::Cell* cell = nullptr;
        int source = -1;                                     // the address of its modulation source
    };

    struct Content : public juce::Component
    {
        juce::OwnedArray<ui::Card> cards;
        std::vector<std::vector<std::vector<ui::Cell*>>> cells;   // per card, per row
        void layout (int width);                              // the four columns of the front panel
    };

    void kitChanged (const ra::Kit::Change&) override;
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;
    void refresh();
    void showLoadMenu();

    Engine& engine;
    Library& library;
    int sound = 0;

    juce::OwnedArray<juce::TextButton> soundButtons;
    juce::Label title;
    juce::TextButton play { juce::String::fromUTF8 ("\xe2\x96\xb6  ") + "Écouter"_fr },
                     save { "Enregistrer ce son" }, load { "Charger un son" },
                     copy { "Copier" }, paste { "Coller" }, saveOk { "OK" }, saveCancel { "Annuler" };
    juce::TextEditor saveName;
    ui::StatusLine status;
    ra::Bytes clipboard;

    Content content;
    juce::OwnedArray<juce::Component> controls;
    std::vector<Param> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundPage)
};

//==============================================================================
class KitPage : public juce::Component,
                private Engine::Listener
{
public:
    explicit KitPage (Engine&);
    ~KitPage() override;

    std::function<void (int)> onEditSound;   // open the Son page on this sound

    void resized() override;

private:
    struct Pad : public ui::Card
    {
        Pad (KitPage& owner, int index);
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        void paintOverChildren (juce::Graphics&) override;

        KitPage& owner;
        int index;
        juce::Label number, shapes;
        juce::TextButton play, edit;
        ui::Cell volume { "Volume" }, pan { "Pan" };
        std::unique_ptr<ui::Choice> out;
        bool selected = false;
    };

    void kitChanged (const ra::Kit::Change&) override;
    void programNamesChanged() override   { fillPrograms(); }
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;
    void refresh();
    void refreshPad (int i);
    void fillPrograms();
    void selectPad (int i);

    Engine& engine;
    ui::Card programCard { "Programme" };
    juce::ComboBox programBox;
    juce::TextButton reread { "Relire" };
    juce::Label nameLabel, hint;
    juce::TextEditor name;
    std::unique_ptr<ui::Choice> midiMode;
    std::array<std::unique_ptr<ui::Choice>, 3> triggerSound;
    std::array<std::unique_ptr<ui::Cell>, 3> triggerVelocity;
    juce::OwnedArray<Pad> pads;
    int selectedPad = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitPage)
};

//==============================================================================
class EffectsPage : public juce::Component,
                    private Engine::Listener
{
public:
    explicit EffectsPage (Engine&);
    ~EffectsPage() override;
    void resized() override;

private:
    struct Column : public ui::Card
    {
        Column (EffectsPage& owner, int index);
        void resized() override;
        void refresh();

        EffectsPage& owner;
        int index;
        std::unique_ptr<ui::Choice> type, out;
        juce::OwnedArray<ui::Cell> knobs;   // the 11 parameters, shown by type
    };

    void kitChanged (const ra::Kit::Change&) override;

    Engine& engine;
    juce::OwnedArray<Column> columns;
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPage)
};

//==============================================================================
class LibraryPage : public juce::Component,
                    private Engine::Listener
{
public:
    LibraryPage (Engine&, Library&, MorphControl&, std::function<int()> selectedSound);
    ~LibraryPage() override;

    void resized() override;
    void visibilityChanged() override   { if (isVisible()) refreshFiles(); }

private:
    struct ProgramList : public juce::ListBoxModel
    {
        explicit ProgramList (LibraryPage& o) : owner (o) {}
        int getNumRows() override   { return 50; }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        LibraryPage& owner;
    };

    struct FileList : public juce::ListBoxModel
    {
        explicit FileList (LibraryPage& o) : owner (o) {}
        int getNumRows() override   { return owner.items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override   { owner.sendSelected (row); }
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override        { if (e.mods.isPopupMenu()) owner.showFileMenu (row); }
        LibraryPage& owner;
    };

    void programNamesChanged() override   { programs.updateContent(); programs.repaint(); }
    void taskFinished (const juce::String& task, bool ok, const juce::String& message) override;
    void progressChanged (int done, int total, const juce::String& what) override;

    void refreshFiles();
    void askFirst (const juce::String& question, std::function<void()> then);
    void sendSelected (int row);
    void showFileMenu (int row);                     // right click: send, or into A or B of the morph
    void loadIntoMorph (int row, MorphControl::Side);
    bool morphsShown() const   { return showMorphs.getToggleState(); }
    void importWaldorf();                            // Waldorf's .mid / .syx / .zip into Usine
    void patternsOf (int row, bool toMachine);      // a file's internal patterns: into the sequence, or the unit
    std::unique_ptr<juce::FileChooser> chooser;

public:
    // the internal patterns of a kit into the sequencer (the window does it, and shows the sequencer)
    std::function<void (const ra::Kit&)> onPatternsToSequencer;

private:

    Engine& engine;
    Library& library;
    MorphControl& morph;
    std::function<int()> selectedSound;

    ui::Card machineCard { "Sur le RackAttack" }, computerCard { "Sur l'ordinateur"_fr };
    juce::TextButton readNames { "Lire les noms" }, backup { "Tout sauvegarder" };
    juce::Label machineHint;
    ProgramList programModel { *this };
    juce::ListBox programs { "programmes", &programModel };

    juce::Label confirmText;
    juce::TextButton confirmYes { "Continuer" }, confirmNo { "Annuler" };
    std::function<void()> confirmed;

    juce::TextButton showKits { "Kits" }, showSounds { "Sons" }, showMorphs { "Morphs" };
    FileList fileModel { *this };
    juce::ListBox files { "fichiers", &fileModel };
    juce::Array<Library::Item> items;
    juce::Label emptyHint;
    juce::TextButton importButton { "Importer (Waldorf)…"_fr };
    juce::TextButton saveKit { "Enregistrer le kit en cours" }, sendButton { "Envoyer au RackAttack" },
                     openFolder { "Ouvrir le dossier" }, saveOk { "OK" }, saveCancel { "Annuler" };
    juce::TextEditor saveName;
    ui::StatusLine status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPage)
};
