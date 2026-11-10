# Proposition — contrôleur RackAttack

## L'idée

Un plugin AU / VST3 + appli autonome, sur la même base qu'OXIde (JUCE, interface en français avec l'anglais au choix, site GitHub Pages, mises à jour intégrées), qui :

1. **édite et range tout le RackAttack** : 24 sons, 4 effets, programme, réglages globaux, 50 programmes, avec rappel automatique et sauvegardes ;
2. **lui donne le séquenceur qu'il n'a jamais eu** : 24 pistes à la Elektron, vélocité libre, retrigs, conditions, et surtout des **p-locks** (une valeur de paramètre différente à chaque pas), envoyés en SysEx juste avant chaque note ;
3. **ajoute une couche « synthé de percussions »** par-dessus les réglages bruts : modèles de sons (grosse caisse, clap, charleston…) avec 6 potards de caractère, accordage en notes, générateur et morphing.

Aucun outil existant ne fait de séquenceur ni de p-locks pour cette machine (voir [RECHERCHE.md](RECHERCHE.md#outils-existants)).

## Ce qui rend le projet faisable

- Le protocole SysEx est en grande partie connu ([SYSEX.md](SYSEX.md)) : adresses de presque tous les paramètres de son, d'effet, de programme et globaux, et le checksum vérifié sur de vrais messages.
- Un changement de paramètre ne fait que 12 octets : on peut en envoyer environ 260 par seconde sur un câble MIDI.
- Avec *Ctl Send = SysEx*, la façade envoie chaque potard tourné : le plugin peut suivre la machine en direct, et on retrouve les adresses manquantes simplement en tournant les potards.

## Les contraintes qui décident de la conception

**1. Tout passe par un port MIDI direct.** VST3 ne sait pas sortir de SysEx vers le DAW. Comme dans OXIde, le plugin parle donc directement au port MIDI du RackAttack (ta MioXL, par exemple). Les notes du séquenceur passent par le même port, pour que les p-locks arrivent toujours avant leur note. Les messages sont horodatés dans le bloc audio et partent avec un peu d'avance ; un réglage de décalage (en ms) recale le tout sur l'audio qui revient dans le DAW.

**2. La bande passante MIDI est le budget du séquenceur.** Pour des doubles croches :

| Tempo | Durée d'un pas | Changements de paramètre max par pas (câble vide) |
|---|---|---|
| 120 BPM | 125 ms | 32 |
| 140 BPM | 107 ms | 27 |
| 170 BPM | 88 ms | 22 |

En gardant de la marge pour les notes, on vise une quinzaine de p-locks par pas au plus. C'est largement assez pour du jeu réel (2–3 paramètres sur 4–5 pistes à la fois), et le plugin le montre avec un indicateur de charge.

**3. Un effet global coûte moins cher qu'un réglage sur les 24 sons.** Un filtre sur tout le kit = 24 messages ; un paramètre d'effet = 1 message. Les gestes « tout le kit » de performance passent donc de préférence par les 4 effets.

**4. Le séquenceur interne (Pattern Play) est limité** (3 vélocités, un pattern par son) et son format de données n'est pas documenté. Le séquenceur vit donc dans le plugin ; l'export vers le Pattern Play pour jouer sans ordinateur viendra une fois le format décodé sur la machine.

## Les pages

| Page | Contenu |
|---|---|
| **Kit** | Les 24 sons en grille (comme le schéma du manuel), avec label, modèle, sortie, groupe XOR, mute. Clic = écouter le son. Glisser = copier ou échanger deux sons. Réglages du programme (nom, tempo, triggers A/B/C/S). |
| **Son** | Tous les réglages du son choisi, rangés selon les 7 sections de la façade (Oscillateurs, Mix/Crack, Filtre, Pattern/Amp, Enveloppes, LFO, Tuned/Out/FX), enveloppes dessinées, pitch affiché en notes. |
| **Séquenceur** | Les 24 pistes, pas, p-locks, patterns et chaînes (voir plus bas). |
| **Modèles** | Les fonctions de synthé de percussions : modèle du son, potards de caractère, générateur, morphing. |
| **Effets** | Les 4 effets, avec seulement les réglages qui ont un sens pour le type choisi. |
| **Bibliothèque** | Les 50 programmes, tes kits et tes sons, les sauvegardes automatiques, import / export `.syx`. |
| **Réglages** | Ports MIDI, Sysex ID, canal, décalage, journal MIDI. |

Comme pour OXIde, la façade du RackAttack peut servir de modèle visuel, pour qu'on ne soit pas perdu en passant de l'un à l'autre.

## Le séquenceur

**Pistes** : une par son du kit (24). Par piste : longueur (1–64 pas, puis plus), division (1/32 à 1 mesure, triolets), swing, mute / solo, rotation, générateur euclidien. Longueurs différentes par piste = polymétrie.

**Pas** : trig, vélocité 1–127 (pas seulement 3 niveaux), décalage fin (micro-timing), longueur de note, retrig / roulement (2 à 8 coups, vélocité qui monte ou descend), probabilité, conditions (1:2, 3:4, Fill, pas Fill, première fois…), et les p-locks.

**P-locks** :
- N'importe quel paramètre du son peut être verrouillé sur un pas, y compris un potard de caractère (qui verrouille alors les réglages bruts qu'il pilote).
- Le plugin garde en mémoire l'état exact de la machine et n'envoie que ce qui change. Après un pas verrouillé, la valeur de base revient juste avant le trig suivant de la piste, seulement si besoin.
- File d'envoi avec priorité (le prochain trig d'abord), limite de débit, et rien pendant un transfert de banque.
- **Sound lock** (changer tout le son sur un pas) : un dump de son complet fait 118 octets, environ 38 ms. Possible ponctuellement si la machine l'applique assez vite (test T13).

**Pistes mélodiques** : un son avec Tuned Channel se joue en notes. Une piste peut donc porter une ligne de basse ou d'acid avec hauteur par pas, sur la gamme choisie.

**Patterns** : banque de patterns par projet, chaîne / mode morceau, Fill tenu, copier-coller de pistes et de pas. Tout est enregistré avec le projet du DAW.

**Synchro** : dans un DAW, le séquenceur suit le transport et les mesures. En appli autonome, il a son propre tempo et peut envoyer l'horloge MIDI. Option pour envoyer l'horloge au RackAttack (LFO synchronisés, Pattern Play interne).

**Enregistrement** : jouer les sons depuis les pads de la page Kit ou depuis un clavier via le DAW, et enregistrer dans le pattern.

## Fonctions de synthé de percussions

**Modèles et potards de caractère.** Les réglages bruts du RackAttack sont ceux d'un synthé ; on ajoute une couche « boîte à rythmes », un peu comme les machines du Machinedrum. Chaque modèle règle le son d'une façon connue et expose 6 potards de caractère :

| Modèle | Recette de départ | Potards |
|---|---|---|
| Grosse caisse | Osc 1 Sine, phase Peak+ pour le clic, Env 2 rapide sur le pitch (balayage), Env 1 sur l'ampli, un peu de bruit court sur Osc 2 | Tune, Decay, Sweep, Sweep time, Click, Drive |
| Caisse claire | Osc 1 Triangle/Sine 180–220 Hz avec petit balayage, Osc 2 Noise avec sa propre décroissance (Osc2 Mix Mod par Env 2), filtre passe-haut ou passe-bande | Tune, Decay, Snappy, Snap decay, Tone, Drive |
| Clap | Noise en passe-bande, générateur Crack (Speed ≈ 80–120 Hz, Length 3–4) pour les impacts | Spread, Hits, Tone, Decay, Reso, Drive |
| Charleston | Échantillons HH Closed / HH Open, ou deux carrés en FM pour un son métallique, passe-haut, groupe XOR pour couper l'ouverte | Tune, Decay, Tone, Metal, Noise, Choke |
| Tom / conga | Sine + balayage de pitch, accordé sur la gamme | Tune, Decay, Sweep, Tone, Skin (bruit), Drive |
| Cloche (808) | Deux carrés à environ 540 et 800 Hz (rapport fixe), passe-bande | Tune, Decay, Tone, Ratio, Drive, Pan |
| Rim / clave | Triangle aigu très court | Tune, Decay, Tone, Click, Drive, Pan |
| Cymbale | Échantillon Crash + filtre | Tune, Decay, Tone, Shimmer (LFO), Drive, Pan |
| FM perc / zap / bruit | Osc 1 FM, longs balayages, LFO | selon le modèle |

Les recettes exactes seront réglées à l'oreille sur la machine. Les réglages bruts restent accessibles sur la page Son.

**Accordage.** Pitch affiché en notes ; Scale et Root comme dans MIFM1 et OXIde ; « accorder le kit » (toms et grosse caisse sur des notes de la gamme), « accorder les toms sur un accord ».

**Vélocité et accents.** Préréglages de ce que fait la vélocité (plus fort, plus brillant, plus aigu, plus court) via les sources Ctrl = Velocity.

**Groupes de coupure (XOR).** Vue claire des 3 groupes dans le kit, avec les cas classiques (charleston, basse mono).

**Générateur** (repris d'OXIde) : *Varier*, *Nouveau* (à partir des modèles), *Croiser* deux sons, sections verrouillées, règles qui garantissent que le résultat s'entend, historique.

**Morphing** entre deux sons ou deux kits, avec débit limité comme dans OXIde (le kit entier fait beaucoup de paramètres : on morphe d'abord les potards de caractère).

**Random / Scale** en haut de fenêtre, comme dans OXIde et MIFM1.

## Feuille de route

| Phase | Contenu | Pourquoi d'abord |
|---|---|---|
| **0. Labo** | Petite appli autonome : choix des ports, « tout lire » (globaux, programme, 24 sons, 4 effets, patterns), journal hexadécimal, enregistrement de tout ce qui arrive (y compris les potards de la façade), test de débit. Tu la lances sur la machine et tu m'envoies le fichier de journal. | Je ne peux pas toucher la machine : c'est le moyen le plus rapide de combler les « ❓ » de SYSEX.md et de décoder les patterns. |
| **1. Cœur** | Bibliothèque C++ sans JUCE (comme `oscar_core`) : codec SysEx, tables de paramètres, checksum, lecture des dumps, avec tests sur les dumps réels capturés en phase 0. | Tout le reste repose dessus. |
| **2. Éditeur + bibliothèque** | Pages Kit, Son, Effets, Bibliothèque, Réglages ; rappel automatique, suivi de la façade, sauvegardes, `.syx`. | Utile tout de suite, et valide le protocole. |
| **3. Séquenceur v1** | 24 pistes, vélocité, longueur et division par piste, swing, synchro DAW, mutes, patterns et chaînes. | Le cœur du projet. |
| **4. Séquenceur v2** | P-locks, retrigs, conditions, micro-timing, pistes mélodiques, sound lock. | Dépend des mesures de débit (T7, T8, T13). |
| **5. Percussions** | Modèles et potards de caractère, accordage, générateur, morphing. | |
| **6. Export Pattern Play** | Écrire un pattern simplifié dans la machine pour jouer sans ordinateur. | Dépend du décodage PATD (T6). |
| **7. Publication** | Site GitHub Pages, releases, mises à jour (repris d'OXIde). | |

## Tests sur la machine

À faire avec l'appli Labo (phase 0). Réglages de départ : Sysex ID = 0, Ctl Send = SysEx, Ctl Receive = On, Rx PrgChange = On.

| Test | Quoi | Ce qu'on apprend |
|---|---|---|
| T1 | Noter la version d'OS (menu Global) | 1.04 ou 1.05 : la doc couvre la 1.04 |
| T2 | Demander les globaux (GLBR) | Format exact de la réponse, longueur (199 ?) |
| T3 | Demander programme, 24 sons, 4 effets | Longueurs 40 / 109 / 18, checksums, octets d'emplacement |
| T4 | Tourner chaque potard de chaque section, valeur mini puis maxi | Toutes les adresses manquantes, encodage du pitch et des valeurs bipolaires |
| T5 | Pareil pour programme, effets, globaux | Idem |
| T6 | Programmer des patterns connus (1 pas, 2 pas, 3 vélocités, longueurs 16 / 17 / 128), demander leur dump | Format des pas (PATD) |
| T7 | Changer un paramètre pendant qu'une note sonne | Le changement s'entend-il tout de suite, avec ou sans artefact ? |
| T8 | Envoyer 50, 100, 200, 260 changements par seconde pendant un rythme | Débit réel supporté, notes perdues ou retards |
| T9 | En mode Plug-in, envoyer les CC 12 à 59 sur le canal 1 | Si la machine répond aux CC du VST Attack, les p-locks coûteraient 3 octets au lieu de 12 |
| T10 | Mesurer le délai note → son (boucle audio) | Valeur du décalage |
| T11 | Lire les 50 programmes | Durée d'une sauvegarde complète, emplacements des programmes en mémoire |
| T12 | Program Change et commandes MODR | Changement de kit, initialisation, stockage |
| T13 | Envoyer un dump de son complet pendant que ça joue | Faisabilité du sound lock |

## Questions ouvertes

1. Le RackAttack est-il chez toi, et avec quelle version d'OS ?
2. Même base qu'OXIde (JUCE, AU / VST3 / autonome, FR / EN, site, mises à jour) ?
3. Le séquenceur dans le plugin, synchronisé au DAW, ça te va ? L'export vers le Pattern Play pour jouer sans ordinateur, c'est important pour toi ?
4. Un nom pour le plugin ?
