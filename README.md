# rack-attack

**RackAttack Editor** : éditeur et bibliothèque pour le **Waldorf RackAttack** (2002). Appli Mac ; plugin AU / VST3 et séquenceur 24 pistes à venir.

## Ce que fait l'appli (version 0.2)

- **Branchement guidé** en 3 écrans : les câbles et ports MIDI, deux réglages sur la machine, puis l'appli trouve le RackAttack toute seule (y compris son Sysex ID) et lit le kit en cours.
- **Kit** : les 24 sons en pads (écouter, volume, pan, sortie), le programme (nom, mode MIDI, triggers A/B/C), changement de programme.
- **Son** : tous les réglages connus d'un son, rangés comme la façade (oscillateurs, mix/crack, filtre, ampli, enveloppes, LFO, sortie). Les changements partent tout de suite vers la machine, et l'appli suit les potards du RackAttack (Ctl. send = SysEx). Copier / coller un son, enregistrer et charger des sons.
- **Effets** : les 4 effets, avec seulement les réglages du type choisi.
- **Bibliothèque** : lire les noms des 50 programmes, tout sauvegarder sur l'ordinateur (un fichier `.syx` par programme), enregistrer le kit en cours, renvoyer un kit ou un son à la machine. Chaque envoi est vérifié en relisant la machine.
- **Outils** : les tests du protocole (lectures brutes, carte des adresses vues, patterns, rafales, CC, envoi libre) et les réglages MIDI.

L'appli n'écrit jamais dans la mémoire du RackAttack : elle modifie le programme en cours. Pour garder un changement : **Shift + Store** sur la machine. Pour l'annuler : **Shift + Recall**.

Les valeurs s'affichent brutes (0–127) tant que leur échelle exacte n'a pas été mesurée sur la machine. Pendant que tu t'en sers, l'appli note ce qui manque encore au protocole dans son journal.

## Installer

1. Onglet *Actions* du dépôt → dernière exécution réussie de **app** → *Artifacts* → `RackAttack-Editor-mac`.
2. Dézipper (deux fois), glisser *RackAttack Editor* dans *Applications*.
3. Premier lancement : clic droit → *Ouvrir*. Si macOS refuse : *Réglages Système* → *Confidentialité et sécurité* → *Ouvrir quand même*, ou dans le Terminal :
   `xattr -dr com.apple.quarantine "/Applications/RackAttack Editor.app"`

## Fichiers

Tout est dans `Documents/RackAttack/` :
- `Kits/`, `Sons/` : ce que tu enregistres ;
- `Sauvegardes/<date>/` : les 50 programmes, plus `noms.txt` ;
- `Journal/session-<date>/` : tout ce qui passe en MIDI (pour moi). Bouton *Journal* → *Préparer le zip* pour me l'envoyer.

## Documentation

- [docs/PROPOSITION.md](docs/PROPOSITION.md) — le projet : pages, séquenceur, fonctions percussions, feuille de route.
- [docs/RECHERCHE.md](docs/RECHERCHE.md) — la machine : architecture, son, effets, modes MIDI, Pattern Play, outils existants.
- [docs/SYSEX.md](docs/SYSEX.md) — le protocole SysEx et ce qui reste à vérifier.

## Code

- `Source/core/` — protocole et modèle de kit, sans JUCE (servira au plugin).
- `Source/app/` — l'appli (JUCE 8).
- `Tests/` — `CoreTests` (dont deux messages réels acceptés par la machine) et `EngineTests` (l'appli face à un faux RackAttack : ID à trouver, 50 programmes, changements de programme, envois vérifiés).

Compiler : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 8 && ctest --test-dir build`.
