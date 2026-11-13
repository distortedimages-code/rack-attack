# rack-attack

**RackAttack Editor** : éditeur et bibliothèque pour le **Waldorf RackAttack** (2002). Plug-in AU / VST3 et appli pour Mac, avec un séquenceur et un simulateur du RackAttack.

**Page de téléchargement** : https://distortedimages-code.github.io/rack-attack/

## Ce que fait l'appli (version 0.5.7)

- **Branchement guidé** en 3 écrans : les câbles et ports MIDI, deux réglages sur la machine, puis l'appli trouve le RackAttack toute seule (y compris son Sysex ID) et lit le kit en cours.
- **Kit** : les 24 sons en pads (écouter, volume, pan, sortie), le programme (nom, mode MIDI, triggers A/B/C), changement de programme.
- **Son** : tous les réglages connus d'un son, rangés comme la façade (oscillateurs, mix/crack, filtre, ampli, enveloppes, LFO, sortie). Les changements partent tout de suite vers la machine, et l'appli suit les potards du RackAttack (Ctl. send = SysEx). Copier / coller un son, enregistrer et charger des sons.
- **Effets** : les 4 effets, avec seulement les réglages du type choisi.
- **Bibliothèque** : lire les noms des 50 programmes, tout sauvegarder sur l'ordinateur (un fichier `.syx` par programme), enregistrer le kit en cours, renvoyer un kit ou un son à la machine. Chaque envoi est vérifié en relisant la machine. **Importer (Waldorf)…** range les fichiers de Waldorf (sons d'usine, kits bonus, patterns : `.mid`, `.syx` ou le `.zip` tel quel) dans le groupe Usine, avec les patterns internes des sons.
- **Patterns internes** (le Pattern Play de la machine) : les patterns d'usine passent dans le séquenceur, et les pistes du séquenceur peuvent devenir les patterns internes des sons (OPTIONS du séquenceur).
- **Morph** : deux kits complets, un dans le cœur, un dans le crâne, et un fader en bas de toutes les pages pour passer de l'un à l'autre, avec un glide réglable. Clic sur le cœur ou le crâne pour l'armer (comme les scènes de l'Octatrack) : les réglages vont alors dans ce côté seulement. Bibliothèque : clic droit « Charger dans le cœur / le crâne », morphs enregistrés. Automatisable dans le DAW et gardé avec le projet.
- **Séquenceur** (page principale) : 24 pistes jusqu'à 128 pas, verrous de paramètres, conditions, suit le tempo du logiciel.
- **Simulateur** : sans machine, le plug-in joue un RackAttack en logiciel (même architecture : 2 oscillateurs, crack, filtre multimode, 2 enveloppes, 2 LFO, 4 effets), piloté par les mêmes messages. *Outils → Mode simulation*, ou le bouton du branchement guidé.
- **Comparer au simulateur** (*Outils*) : chaque son du kit en cours joué sur la machine puis dans le simulateur, avec ce qui diffère le plus (volume, hauteur, longueur, brillance) et les enregistrements gardés dans un fichier `.racompare` privé.
- **Mesure guidée** : *Outils → Mesurer mon RackAttack…* fait jouer à une vraie machine des sons de test (environ 5 minutes, par la carte son) et en tire un fichier `.ramesure` ; chargé avec *Charger des mesures…*, il fait sonner le simulateur comme cette machine. Le fichier contient des enregistrements des cymbales de la machine : il reste privé.
- **Outils** : les tests du protocole (lectures brutes, carte des adresses vues, patterns, rafales, CC, envoi libre) et les réglages MIDI.

L'appli n'écrit jamais dans la mémoire du RackAttack : elle modifie le programme en cours. Pour garder un changement : **Shift + Store** sur la machine. Pour l'annuler : **Shift + Recall**.

Les valeurs s'affichent dans leur unité quand elle est connue (pour cent, demi-tons, notes, gauche/droite), avec un zéro au milieu pour les réglages qui vont dans les deux sens ; les autres restent en 0–127 tant que leur échelle n'a pas été mesurée sur la machine. Pendant que tu t'en sers, l'appli note ce qui manque encore au protocole dans son journal.

## Installer

Télécharge `RackAttack-Editor-mac.dmg` (lien sur la page, toujours la dernière version) et ouvre-le. Deux façons, expliquées sur la page et dans le LISEZ-MOI du DMG :

- **l'installeur** (« Installer RackAttack Editor », AU, VST3 et appli au choix) ; comme il n'est pas signé chez Apple, macOS peut refuser de l'ouvrir : la commande Terminal du LISEZ-MOI le lance quand même ;
- **à la main** : glisser chaque élément sur le raccourci de son dossier, puis coller la commande Terminal qui débloque l'AU et le VST3 et fait valider l'AU.

Ensuite, les mises à jour se font depuis le plug-in : un bouton **Mise à jour** apparaît en haut quand une version sort (fichier vérifié, mot de passe demandé par macOS).

## Publier une version

1. Monter `VERSION` dans `CMakeLists.txt` (`project(RackAttack VERSION x.y.z …)`) et écrire `RELEASE_NOTES.md`.
2. Fusionner dans `main`. C'est tout, comme pour IMPAKT : quand `main` porte une version qui n'est pas encore sortie, le workflow **release** la construit, la teste sur un Mac et la publie avec le `.dmg`, le `.zip` et son contrôle (GitHub crée le tag). Les plug-ins installés proposent alors la mise à jour.

Pour regarder une version avant qu'elle sorte : *Actions* → **release** → *Run workflow* avec **Brouillon** coché, puis *Releases* → **Publish release**.

## Fichiers

Tout est dans `Documents/RackAttack/` :
- `Kits/`, `Sons/` : ce que tu enregistres ;
- `Sauvegardes/<date>/` : les 50 programmes, plus `noms.txt` ;
- `Mesures/` : les fichiers `.ramesure` de la mesure guidée ;
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
- `Source/web/RaWeb.cpp`, `site/app/` — la version web : le cœur et le simulateur du plug-in compilés en WebAssembly (`tools/web/build_wasm.sh`, avec `pip install ziglang`), l'interface refaite en HTML. Vérification : `node tools/web/test_wasm.mjs`.
- `site/` — le site (GitHub Pages, publié par `.github/workflows/site.yml` ; `site/build_site.py` y écrit la version et les commandes Terminal du DMG). `tools/dmg/` — le DMG et son installeur (`make_dmg.sh`).
- `Source/sim/` — le RackAttack en logiciel (`RaSynth`), sans JUCE ; ses échelles (Hz, ms, gains) sont dans un `Calibration`, réglé par défaut d'après le manuel et remplacé par des mesures. `Source/app/Measure` est le banc de mesure, `MeasureWizard` l'assistant.
- `Tests/` — `SynthTests` (le simulateur), `MeasureTests` (la mesure sur le simulateur avec un étalonnage connu, qu'elle doit retrouver), `CoreTests` (dont deux messages réels acceptés par la machine), `SeqTests` (rythmes euclidiens, temps des notes, verrous, conditions, sauvegarde), `EngineTests` (l'appli face à un faux RackAttack : ID à trouver, 50 programmes, changements de programme, envois vérifiés, séquenceur avec horloge propre puis suivant un logiciel qui boucle) et `UpdaterTests` (versions, réponse de GitHub, contrôle, remplacement des plug-ins avec ou sans mot de passe).

Voir l'appli sans machine : `RA_DEMO=1` (le simulateur et un petit pattern qui joue). Pour les captures : `RA_PAGE=n`, `RA_WIZARD=1`, `RA_MEASURE=1|audio|ready|run|done`. Contrôle de mise en page : `RA_AUDIT=1` (ou `RA_AUDIT=<fichier>`) parcourt toutes les pages et les assistants et liste les textes coupés ou serrés et les éléments qui débordent ou se chevauchent.

Compiler : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 8 && ctest --test-dir build`.
