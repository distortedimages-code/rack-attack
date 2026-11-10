# Waldorf RackAttack — fiche machine

Ce qu'il faut savoir de la machine pour lui écrire un contrôleur. Le détail du protocole est dans [SYSEX.md](SYSEX.md).

## En bref

- Synthé de percussions en rack 2U (2002), version matérielle du plugin VST *Attack* (2001). Électronique de microQ, même philosophie de façade : une matrice de 7 sections, 6 potards sous l'écran 2×20 caractères, un encodeur.
- Synthèse « virtual analog » orientée percussions : 2 oscillateurs, un générateur de *crack* (pour les claps), un filtre multimode, 2 enveloppes, 2 LFO, 4 effets.
- 24 à 25 voix de polyphonie. 50 programmes (kits) de 24 sons, soit 1200 sons.
- Sorties : 3 paires stéréo (Out 1+2, 3+4, 5+6), chaque paire utilisable en deux sorties mono. Une entrée stéréo (Ext In) qu'on peut traiter comme un oscillateur. Le casque recopie Out 1+2.
- Séquenceur interne « Pattern Play » : un pattern par son, jusqu'à 128 pas, 3 niveaux de vélocité. Assez limité, d'où l'intérêt d'un séquenceur dans le plugin.
- Édition à distance entièrement en SysEx (pas de liste de CC pour les paramètres de son). Le plugin Attack d'époque pouvait envoyer ses sons au RackAttack par SysEx.

## Architecture d'un programme

Un programme = 24 sons (numérotés 01 à 24, sur la façade en grille de 4 par ligne) + 4 effets + des réglages de programme (nom de 16 caractères, mode MIDI, tempo, signature, les triggers A/B/C/S).

Chaque son : Osc 1 + Osc 2 → Mix (avec Crack et entrée externe) → Ring mod → Filtre → Ampli → sortie sèche (Dry Out) + envoi vers l'un des 4 FX → sorties 1 à 6.

## Le son, section par section

**Oscillateurs** (×2)
- Formes : Triangle, Sine, Square, Saw, S&H, Noise, et 3 échantillons PCM : HH Closed, HH Open, Crash. Plus l'entrée externe (Ext L, Ext R, Ext Mono).
- Pitch de 0,0079 Hz à 18,7 kHz (C-11 à D10), Detune, phase de départ (Peak+, Peak−, Zero, Free : important pour le claquement d'une grosse caisse).
- Chaque pitch a une modulation (source Env1, Env2, LFO1 ou LFO2) et un contrôle (Velocity, Modwheel, Pressure, Breath, Foot, Keytrack), chacun avec sa quantité bipolaire.
- Osc 1 a en plus de la FM (Osc 2 module Osc 1), avec sa propre modulation et son contrôle.

**Mix / Crack**
- Niveaux Osc1, Osc2, Ringmod (Osc1 × Osc2), Crack. Le niveau d'Osc 2 peut être modulé.
- **Crack** : un générateur qui redéclenche l'enveloppe en rafale. Crack Speed (1 Hz–5 kHz) et Length (1 à 49 répétitions, ou infini). C'est ce qui fait les claps « multi-impacts ».

**Filtre**
- Types : Bypass, Lowpass (12 dB/oct), Highpass, Bandpass, Notch, EQ Shelving, Bell EQ, S&H (réduction de fréquence d'échantillonnage, pas un vrai filtre).
- Cutoff, Resonance, Drive, Keytrack (−200 % à +196 %, référence E3), deux modulations et deux contrôles de cutoff.

**Pattern / Amp**
- Pattern Mode, Select, Volume, Pan, avec modulation et contrôle de Volume et de Pan.

**Enveloppes** (×2) : ADSR de 0 à 127, plus une *Shape* (exponentielle → linéaire → « In » → « Co ») qui agit sur Decay et Release.

**LFO** (×2) : Speed 0–127, formes Sine, Tri, Square, Saw, Random, S&H, Sync (Off, Key, 8 mesures à 1/64, pointé, triolet). LFO 1 a un mode One Shot (devient une enveloppe de plus), LFO 2 a une modulation de vitesse.

**Tuned / Out / FX**
- Tuned Channel (Off, 1–16, Global), Transpose ±48, Low Key / High Key : pour jouer un son mélodiquement et en polyphonie.
- Dry Out (mono 1–6 ou stéreo 1/2, 3/4, 5/6), FX Select (FX1–4), FX Send Mix.

**Réglages divers du son** : un label parmi 128 noms prédéfinis, et un groupe XOR (Off, 1–3) : dans un même groupe, une nouvelle note coupe les autres sons (charleston ouverte/fermée, basse monophonique).

## Effets

4 effets par programme. Types : Chorus, Flanger, Phaser, Overdrive, Lo-Fi sur les quatre ; Delay (synchronisable, auto-pan), ModDelay et Reverb seulement sur FX1 et FX2. Chaque effet a sa sortie (mono ou stéréo).

## Modes MIDI (réglage du programme)

| Mode | Comportement |
|---|---|
| **Native** | Les 24 sons sur le canal global, une note par son, de C1 à B2 (décalable avec *Map Base Key*). Un son qui a un Tuned Channel se joue en plus mélodiquement sur ce canal. |
| **Plug-in** | Compatible avec le VST Attack : canaux 1 à 12 ; les sons 13 à 24 se jouent aussi mélodiquement de C3 à G9 sur ces canaux. Canaux 13 à 16 inutilisés. |
| **GlobalChn** | Tout sur le canal global, pas de jeu polyphonique. |

Reçus : notes (vélocité d'attaque et de relâchement), aftertouch mono (source *Pressure*), pitch bend 14 bits, Modwheel, Breath, Foot (sources de contrôle), Channel Volume (CC 7), Program Change 1–50, horloge MIDI et Start/Stop/Continue, Reset All Controllers.

D'après polynominal, l'OS 1.05 aurait ajouté quelques contrôles (panoramique, volume, quantité d'effet). Le manuel et la doc SysEx couvrent l'OS 1.04 : il faudra regarder la version de ta machine (menu Global).

## Pattern Play (séquenceur interne)

- Un pattern par son. Quand le pattern d'un son est actif, la note qui déclenche ce son lance le pattern au lieu d'un coup unique.
- **Pattern Mode** : Off, On (joue tant que la note est tenue), Latch (démarre et boucle jusqu'à Panic), Toggle (la même note l'arrête), One Shot (une seule fois).
- **Group** : Off, Trigger A, B, C ou A+B+C. Un trigger peut lancer jusqu'à 24 patterns synchronisés d'une seule note.
- **Length** 1–128 pas ; **Step Clk** et **Note Length** de 1/64T à 8 mesures ; **Swing** Off ou 58,3–75 % ; **Start When** (immédiat, mesure suivante… jusqu'au 1/16) ; **Start Where** (pas 1 ou position courante).
- Chaque pas : vide, ou Vel. 1 / Vel. 2 / Vel. 3 (trois vélocités réglables, 64, 96 et 127 par défaut).
- Tempo du programme : Global, Extern (horloge MIDI) ou 0,2–320 BPM. Signature 4/4, 3/4 ou 5/4.
- Arrêt : Panic (appui court sur Power), Pattern Mode sur Off, ou All Notes Off.

## Réglages globaux utiles au plugin

| Réglage | Valeur conseillée pour le plugin |
|---|---|
| Sysex ID | 0 (le panel Ctrlr l'exige aussi) |
| Global Chn | un canal fixe (pas Omni) |
| Ctl Send | **SysEx** : la façade envoie ses changements, le plugin suit |
| Ctl Receive | On |
| Rx PrgChange | On |
| Tx Clock | Off (c'est le plugin ou le DAW qui mène) |
| Glb. Pattern Mode | selon l'usage : Disable si le plugin séquence tout |

## Outils existants

| Outil | Ce qu'il fait | Ce qui manque |
|---|---|---|
| Panel Ctrlr « RackAttack panel 1.4 » (zeoka, ~2013) | Éditeur « tout-en-un » : sons, programme, globaux | Pas les patterns, pas de séquenceur. Ctrlr vieillissant. |
| ra-editor (Python/Tkinter, GitHub) | Lit et édite le programme courant | Abandonné, Python 2, pas de patterns |
| JRackAttack (Java, SourceForge, 2008) | Éditeur à distance | Code inaccessible d'ici, projet mort |
| Preset Electra One (déc. 2024) | Contrôleur matériel | Contrôle seulement, pas de séquenceur |
| Patch Base (Coffeeshopped) | — | Seulement « envisagé », pas fait |

Aucun ne fait de séquenceur ni de p-locks : un plugin moderne avec séquenceur serait une première.

## Sources

- Manuel utilisateur du RackAttack (Waldorf, OS 1.04) — https://www.polynominal.com/sample-packs/waldorf-rack-attack/waldorf-rack-attack-manual.pdf
- Fiche polynominal (caractéristiques, Pattern Play, OS 1.05) — https://polynominal.com/sample-packs/waldorf-rack-attack
- Fiche Gearspace (filiation microQ) — https://gearspace.com/gear/waldorf/rack-attack
- Panel Ctrlr — https://ctrlr.org/rackattack-panel-1-1/
- ra-editor — https://github.com/adsr/ra-editor
- JRackAttack — https://jrackattack.sourceforge.io
- Forum Electra One — https://forum.electra.one/t/sysex-messages-for-waldorf-rackattack/3469
- Doc SysEx d'Achim Gratz — http://synth.stromeko.net/docs/Waldorf.pdf
