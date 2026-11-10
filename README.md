# rack-attack

Contrôleur pour le **Waldorf RackAttack** (2002) : éditeur / librarian complet, séquenceur 24 pistes avec p-locks, et fonctions de synthé de percussions. Plugin AU / VST3 et appli autonome.

**État** : phase 0. La recherche est faite ; l'appli de test **RackAttack Labo** sert à compléter ce qui manque sur la machine elle-même.

## Documentation

- [docs/PROPOSITION.md](docs/PROPOSITION.md) — ce qu'on veut faire : pages, séquenceur, fonctions percussions, feuille de route, tests à faire sur la machine.
- [docs/RECHERCHE.md](docs/RECHERCHE.md) — la machine : architecture, son, effets, modes MIDI, Pattern Play, outils existants.
- [docs/SYSEX.md](docs/SYSEX.md) — le protocole SysEx : messages, checksum, adresses de tous les paramètres connus, et ce qui reste à vérifier.

## RackAttack Labo

Petite appli Mac qui enregistre tout ce que le RackAttack répond : dumps, potards de la façade, réponses aux demandes de pattern, tenue de débit. Elle ne touche jamais à la mémoire de la machine (seul l'onglet *Envoi libre* peut tout envoyer). L'onglet **Guide** dit quoi faire, test par test.

**Installer**
1. Onglet *Actions* du dépôt → dernière exécution réussie de **labo** → *Artifacts* → `RackAttack-Labo-mac`.
2. Dézipper (deux fois), glisser *RackAttack Labo* dans *Applications*.
3. Premier lancement : clic droit → *Ouvrir*. Si macOS refuse (« endommagée » ou « développeur non identifié ») : *Réglages Système* → *Confidentialité et sécurité* → *Ouvrir quand même*, ou dans le Terminal :
   `xattr -dr com.apple.quarantine "/Applications/RackAttack Labo.app"`

Chaque lancement crée une session dans `Documents/RackAttack Labo/` : `journal.txt` (tout ce qui passe, avec l'heure), `recus/` (chaque dump en `.syx`), `tout-recu.syx`, `carte.txt` (adresses vues). Le bouton *Préparer le zip* rassemble la session en un fichier à m'envoyer.

**Compiler soi-même** (facultatif) : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 8 && ctest --test-dir build`. La première fois, JUCE 8.0.4 est téléchargé.

## Code

- `Source/core/` — le protocole SysEx, sans JUCE (construction, checksum, description lisible, tables de paramètres, carte des adresses). Servira au plugin.
- `Source/labo/` — l'appli Labo (JUCE).
- `Tests/` — `CoreTests` (dont deux messages réels acceptés par la machine) et `EngineTests` (l'appli complète face à un faux RackAttack).
