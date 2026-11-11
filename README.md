# rack-attack

**RackAttack Editor** : éditeur et bibliothèque pour le **Waldorf RackAttack** (2002). Plug-in AU / VST3 et appli pour Mac ; séquenceur à venir.

**Page de téléchargement** : https://distortedimages-code.github.io/rack-attack/

## Ce que fait l'appli (version 0.3)

- **Branchement guidé** en 3 écrans : les câbles et ports MIDI, deux réglages sur la machine, puis l'appli trouve le RackAttack toute seule (y compris son Sysex ID) et lit le kit en cours.
- **Kit** : les 24 sons en pads (écouter, volume, pan, sortie), le programme (nom, mode MIDI, triggers A/B/C), changement de programme.
- **Son** : tous les réglages connus d'un son, rangés comme la façade (oscillateurs, mix/crack, filtre, ampli, enveloppes, LFO, sortie). Les changements partent tout de suite vers la machine, et l'appli suit les potards du RackAttack (Ctl. send = SysEx). Copier / coller un son, enregistrer et charger des sons.
- **Effets** : les 4 effets, avec seulement les réglages du type choisi.
- **Bibliothèque** : lire les noms des 50 programmes, tout sauvegarder sur l'ordinateur (un fichier `.syx` par programme), enregistrer le kit en cours, renvoyer un kit ou un son à la machine. Chaque envoi est vérifié en relisant la machine.
- **Outils** : les tests du protocole (lectures brutes, carte des adresses vues, patterns, rafales, CC, envoi libre) et les réglages MIDI.

L'appli n'écrit jamais dans la mémoire du RackAttack : elle modifie le programme en cours. Pour garder un changement : **Shift + Store** sur la machine. Pour l'annuler : **Shift + Recall**.

Les valeurs s'affichent brutes (0–127) tant que leur échelle exacte n'a pas été mesurée sur la machine. Pendant que tu t'en sers, l'appli note ce qui manque encore au protocole dans son journal.

## Installer

Télécharge `RackAttack-Editor-mac.dmg` (lien sur la page, toujours la dernière version), ouvre-le et glisse chaque élément sur le dossier à côté de lui. Premier lancement de l'appli : clic droit → *Ouvrir*.

Ensuite, les mises à jour se font depuis le plug-in : un bouton **Mise à jour** apparaît en haut quand une version sort (fichier vérifié, mot de passe demandé par macOS).

## Publier une version

1. Monter `VERSION` dans `CMakeLists.txt` (`project(RackAttack VERSION x.y.z …)`) et écrire `RELEASE_NOTES.md`.
2. Fusionner dans `main`.
3. Sur GitHub : *Actions* → **release** → *Run workflow* (sur `main`). La version est construite, testée, et une **version brouillon** est créée avec le `.dmg`, le `.zip` et son contrôle.
4. *Releases* → relire le brouillon → **Publish release**. Les plug-ins installés proposent alors la mise à jour.

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

- `Source/core/` — protocole, modèle de kit et séquenceur (`RaSeq` : patterns, euclidien, horloge en noires, verrous), sans JUCE.
- `Source/app/` — l'interface et le lien avec la machine (JUCE 8). `Sequencer` envoie les notes et les verrous à l'heure (planifiés 40 ms à l'avance, en suivant le transport du logiciel) ; `SeqPage` est la page principale (voix, son, séquenceur) ; `SoundViz` dessine le son de chaque section en pixels ; `Pixel` le look rétro.
- `Source/plugin/` — le plug-in et les mises à jour.
- `Resources/fonts/` — Silkscreen et VT323, sous licence SIL Open Font License (textes de licence à côté).
- `docs/index.html` — la page de téléchargement (GitHub Pages, dossier `docs` de `main`).
- `Tests/` — `CoreTests` (dont deux messages réels acceptés par la machine), `SeqTests` (rythmes euclidiens, temps des notes, verrous, conditions, sauvegarde), `EngineTests` (l'appli face à un faux RackAttack : ID à trouver, 50 programmes, changements de programme, envois vérifiés, séquenceur avec horloge propre puis suivant un logiciel qui boucle) et `UpdaterTests` (versions, réponse de GitHub, contrôle, remplacement des plug-ins avec ou sans mot de passe).

Voir l'appli sans machine : `RA_DEMO=1` (un faux RackAttack et un petit pattern qui joue).

Compiler : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 8 && ctest --test-dir build`.
