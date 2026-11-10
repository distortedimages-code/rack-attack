# Waldorf RackAttack — référence SysEx

Ce qu'on sait du protocole SysEx du RackAttack (OS 1.04), recoupé entre plusieurs sources (liste en bas). Tout ce qui n'est confirmé que par une seule source, ou qui manque, est marqué **à vérifier** : la liste des tests à faire sur la machine est dans [PROPOSITION.md](PROPOSITION.md#tests-sur-la-machine).

Légende : ✅ confirmé par au moins deux sources ou par de vrais messages · ⚠️ une seule source · ❓ inconnu

## 1. Forme générale

```
F0 3E 11 DEV CMD <données…> CHK F7
```

| Octet | Nom | Valeur | |
|---|---|---|---|
| 0 | SOX | `F0` | début de SysEx ✅ |
| 1 | IDW | `3E` | Waldorf ✅ |
| 2 | IDM | `11` | rackAttack ✅ |
| 3 | DEV | `00`–`7E` | Device ID = réglage global *Sysex ID* (0 par défaut). `7F` = broadcast ✅ |
| 4 | CMD | voir §2 | type de message ✅ |
| 5… | MSG | | emplacement puis données |
| avant-dernier | CHK | | checksum ✅ |
| dernier | EOX | `F7` | fin de SysEx ✅ |

### Checksum ✅

`CHK = (somme de tous les octets de CMD jusqu'au dernier octet de données) & 0x7F`

Vérifié sur deux messages réels envoyés à un RackAttack (Cutoff du son 1) :

```
F0 3E 11 00 | 20 20 00 00 27 01 | 68 | F7     valeur 1
F0 3E 11 00 | 20 20 00 00 27 7F | 66 | F7     valeur 127
```

`20+20+00+00+27+01 = 68` et `20+20+00+00+27+7F = E6 → & 7F = 66`. Le calcul tombe juste.

En réception, le RackAttack accepte aussi `7F` comme checksum « joker » ; lui n'envoie jamais `7F` (le checksum de ses dumps est toujours calculé). ⚠️ (auteur du panel Ctrlr et doc Gratz citée sur le forum Electra One)

Un SysEx de paramètre (SNDP) fait **12 octets** : environ 3,8 ms sur un câble MIDI, soit 260 messages par seconde au maximum.

## 2. Types de message ✅

Le quartet haut de CMD donne l'action, le quartet bas l'objet (convention Waldorf).

| Objet | Request `0x` | Dump `1x` | Paramètre `2x` | Inquiry `3x` |
|---|---|---|---|---|
| Son (Sound) | `00` SNDR | `10` SNDD | `20` SNDP | `30` SNDQ |
| Programme (kit) | `01` PRGR | `11` PRGD | `21` PRGP | `31` PRGQ |
| Pattern | `03` PATR | `13` PATD | `23` PATP | `33` PATQ |
| Global | `04` GLBR | `14` GLBD | `24` GLBP | `34` GLBQ |
| Effet (FX) | `05` EFXR | `15` EFXD | `25` EFXP | `35` EFXQ |
| Mode (commandes) | `07` MODR | `17` MODD | `27` MODP | `37` MODQ |

`71` est réservé (mise à jour d'OS). Sources : table `magic/sysex` du programme `file` (reprise de la doc Gratz) et code de ra-editor.

- **Request** : demande un dump. **Dump** : le bloc complet. **Paramètre** : change une seule valeur. **Inquiry** : demande la valeur d'un seul paramètre (pratique pour resynchroniser sans tout redemander).

## 3. Formats de message utilisés par ra-editor ⚠️

ra-editor les utilise réellement avec la machine, mais une seule source : à confirmer au premier branchement.

| Message | Octets après CMD | Réponse |
|---|---|---|
| SNDR | `BUFN SNDN` | SNDD : `10 BUFN SNDN` + 109 octets SDAT |
| SNDP | `BUFN SNDN PAH PAL VAL` | — |
| PRGR | `PRGN BUFN` (ra-editor envoie `00 20`) | PRGD : `11 ?? ??` + 40 octets KDAT |
| PRGP | `BUFN PAH PAL VAL` (ra-editor envoie `20`) | — |
| EFXR | `BUFN EFXN` | EFXD : `15 BUFN EFXN` + 18 octets FDAT |
| EFXP | `BUFN EFXN PAH PAL VAL` | — |
| GLBR | `00` | GLBD : `14 ??` + 199 octets GDAT |
| GLBP | `PAH PAL VAL` | — |
| MODR | `04 20 00` = « initialiser le programme courant » (commentaire du code : `MCMD MOV1 MOV2`) | |
| PATR / PATD / PATP | ❓ format inconnu | |

- `BUFN = 20h` : le tampon d'édition (le programme en cours). ✅ (ra-editor + messages Electra One)
- `SNDN` : numéro du son, `00`–`17` (sons 1 à 24). `EFXN` : `00`–`03` (FX 1 à 4).
- `PAH PAL` : adresse du paramètre sur deux octets (poids fort, poids faible). Toutes les adresses connues ont `PAH = 00`.
- ❓ Les emplacements des 50 programmes en mémoire (`BUFN`/`PRGN` = `00`–`31` ?) : à tester. Pour l'instant, ra-editor change de programme par Program Change puis lit le tampon d'édition.
- ❓ ra-editor ajoute parfois `F0 F7` vide après un message (« flush hack ») : probablement un contournement de son interface MIDI, à garder en tête si des messages se perdent.

## 4. SDAT — un son (109 octets, `00`–`6C`)

Adresse = `PAL` du SNDP (avec `PAH = 00`). Les valeurs vont de 0 à 127 sauf indication. Les noms suivent la façade.

Sources de modulation `MOD` : `0` Env1 · `1` Env2 · `2` LFO1 · `3` LFO2
Sources de contrôle `CTRL` : `0` Velocity · `1` Modwheel · `2` Pressure · `3` Breath · `4` Foot · `5` Keytrack
Formes d'oscillateur `SHAPE` : `0` Triangle · `1` Sine · `2` Square · `3` Saw · `4` S&H · `5` Noise · `6` HH Closed · `7` HH Open · `8` Crash · `9` Ext L · `10` Ext R · `11` Ext Mono

| Adr | Paramètre | Valeurs | |
|---|---|---|---|
| `00` | ❓ (label du son ?) | | ❓ |
| **Oscillateur 1** ||||
| `01` | Osc1 Shape | SHAPE | ⚠️ |
| `02` | Osc1 Pitch | encodage ❓ (façade : 0,0079 Hz–18,7 kHz / C-11–D10) | ⚠️ |
| `03` | Osc1 Detune | façade : 0–200 | ⚠️ |
| `04` | Osc1 Start | `0` Peak+ · `1` Peak− · `2` Zero · `3` Free | ⚠️ |
| `05` | Osc1 Pitch Mod | bipolaire −100…+100 % (centre ❓) | ⚠️ |
| `06` | Osc1 Pitch Ctrl | bipolaire | ⚠️ |
| `07` | Osc1 Pitch Mod Src | MOD | ⚠️ |
| `08` | Osc1 Pitch Ctrl Src | CTRL | ⚠️ |
| `09` | Osc1 FM | 0–100 % | ⚠️ |
| `0A` | Osc1 FM Mod | bipolaire | ⚠️ |
| `0B` | Osc1 FM Ctrl | 0–127 | ⚠️ |
| `0C` | Osc1 FM Mod Src | MOD | ⚠️ |
| `0D` | Osc1 FM Ctrl Src | CTRL | ⚠️ |
| `0E`–`0F` | ❓ | | |
| **Oscillateur 2** (pas de FM) ||||
| `10` | Osc2 Shape | SHAPE | ⚠️ |
| `11` | Osc2 Pitch | comme Osc1 | ⚠️ |
| `12` | Osc2 Detune | | ⚠️ |
| `13` | Osc2 Start | comme Osc1 | ⚠️ |
| `14` | Osc2 Pitch Mod | bipolaire | ⚠️ |
| `15` | Osc2 Pitch Ctrl | bipolaire | ⚠️ |
| `16` | Osc2 Pitch Mod Src | MOD | ⚠️ |
| `17` | Osc2 Pitch Ctrl Src | CTRL | ⚠️ |
| `18`–`19` | ❓ | | |
| **Mix / Crack** ||||
| `1A` | Osc1 Mix | 0–100 % | ⚠️ |
| `1B` | Osc2 Mix | 0–100 % | ⚠️ |
| `1C` | Ringmod | 0–100 % | ⚠️ |
| `1D` | Crack Mix | 0–100 % | ⚠️ |
| `1E` | Osc2 Mix Mod | bipolaire | ⚠️ |
| `1F` | Osc2 Mix Ctrl | bipolaire | ⚠️ |
| `20` | Osc2 Mix Mod Src | MOD | ⚠️ |
| `21` | Osc2 Mix Ctrl Src | CTRL | ⚠️ |
| `22`–`23` | ❓ | | |
| `24` | Crack Speed | façade : 1 Hz–5 kHz | ⚠️ |
| `25` | Crack Length | façade : 1–49, inf | ⚠️ |
| **Filtre** ||||
| `26` | Filter Type | ra-editor : `0` Bypass · `1` Low · `2` Band · `3` High · `4` Notch · `5` Shelf · `6` Bell · `7` S&H — **ordre à vérifier** (le manuel liste Low, High, Band) | ⚠️ |
| `27` | Cutoff | 0–127 | ✅ (messages Electra One) |
| `28` | Resonance | 0–127 | ⚠️ |
| `29` | Drive | 0–127 | ⚠️ |
| `2A` | Keytrack | façade : −200 %…+196 % (réf. E3) | ⚠️ |
| `2B` | Cutoff Mod 1 | bipolaire | ⚠️ |
| `2C` | Cutoff Ctrl 1 | bipolaire | ⚠️ |
| `2D` | Mod 1 Src | MOD | ⚠️ |
| `2E` | Ctrl 1 Src | CTRL | ⚠️ |
| `2F` | Cutoff Mod 2 | bipolaire | ⚠️ |
| `30` | Cutoff Ctrl 2 | bipolaire | ⚠️ |
| `31` | Mod 2 Src | MOD | ⚠️ |
| `32` | Ctrl 2 Src | CTRL | ⚠️ |
| `33`–`34` | ❓ | | |
| **Ampli** ||||
| `35` | Volume | | ⚠️ |
| `36` | Volume Mod | bipolaire | ⚠️ |
| `37` | Volume Ctrl | bipolaire | ⚠️ |
| `38` | Volume Mod Src | MOD | ⚠️ |
| `39` | Volume Ctrl Src | CTRL | ⚠️ |
| `3A` | Pan | centre ❓ | ⚠️ |
| `3B` | Pan Mod | bipolaire | ⚠️ |
| `3C` | Pan Ctrl | bipolaire | ⚠️ |
| `3D` | Pan Mod Src | MOD | ⚠️ |
| `3E` | Pan Ctrl Src | CTRL | ⚠️ |
| **Enveloppes** (ADSR) ||||
| `3F`–`42` | Env1 Attack, Decay, Sustain, Release | 0–127 (Release à 0 : pas de release) | ⚠️ |
| `43` | Env1 Shape | façade : Exp → Linear → In → Co (s'applique à Decay et Release) | ⚠️ |
| `44` | ❓ | | |
| `45`–`48` | Env2 Attack, Decay, Sustain, Release | | ⚠️ |
| `49` | Env2 Shape | | ⚠️ |
| `4A` | ❓ | | |
| **LFO** ||||
| `4B` | LFO1 Speed | ignoré si Sync est sur une valeur de note | ⚠️ |
| `4C` | LFO1 Shape | `0` Sine · `1` Triangle · `2` Square · `3` Saw · `4` Random · `5` S&H | ⚠️ |
| `4D` | LFO1 Sync | 24 valeurs : Off, Key, 8 bars, 4 bars, 2 bars, 1 bar, 1/2…1/64, 1/2.…1/64., 1/2t…1/64t | ⚠️ |
| `4E` | LFO1 OneShot | `0` Off · `1` On | ⚠️ |
| `4F` | LFO2 Speed | | ⚠️ |
| `50` | LFO2 Shape | comme LFO1 | ⚠️ |
| `51` | LFO2 Sync | comme LFO1 | ⚠️ |
| `52` | LFO2 Mod | bipolaire | ⚠️ |
| `53` | LFO2 Mod Src | ra-editor dit MOD, le manuel dit CTRL — **à vérifier** | ⚠️ |
| `54` | ❓ | | |
| **Tuned / Out / FX** ||||
| `55` | Dry Out | `0` Mono 1 · `1` Stereo 1/2 · `2` Mono 2 · `3` Mono 3 · `4` Stereo 3/4 · `5` Mono 4 · `6` Mono 5 · `7` Stereo 5/6 · `8` Mono 6 | ⚠️ |
| `56` | FX Select | `0`–`3` = FX1–FX4 | ⚠️ |
| `57` | FX Send Mix | 0–100 % | ⚠️ |
| `58` | Tuned Channel | `0` Off · `1`–`16` · `17` Global | ⚠️ |
| `59` | Transpose | −48…+48 (centre ❓) | ⚠️ |
| `5A` | Low Key | C-2…G8 | ⚠️ |
| `5B` | High Key | C-2…G8 | ⚠️ |
| `5C`–`5D` | ❓ | | |
| `5E` | XOR Group | `0` Off · `1`–`3` | ⚠️ |
| **Pattern** ||||
| `5F`–`6B` | ❓ 13 octets : très probablement les réglages du pattern du son (Group, Length 1–128, Step Clk, Note Length, Swing, Start When, Start Where, Vel. 1/2/3) et peut-être le label | | ❓ |
| `6C` | Pattern Mode | `0` Off · `1` On · `2` Latch · `3` Toggle · `4` One Shot | ⚠️ |

## 5. KDAT — un programme / kit (40 octets)

| Adr | Paramètre | Valeurs | |
|---|---|---|---|
| `00` | ❓ | | |
| `01`–`10` | Nom (16 caractères ASCII 32–127) | | ⚠️ |
| `11` | MIDI Mode | `0` Native · `1` Plug-in · `2` GlobalChn | ⚠️ |
| `12` | Tempo, poids fort | 0–24 (ra-editor l'appelle `Tempo_Lo`, mais la plage 0–24 en fait le poids fort) | ⚠️ |
| `13` | Tempo, poids faible | 0–127 | ⚠️ |
| `14` / `15` | Trigger A : son / vélocité | son `0`–`23` | ⚠️ |
| `16` / `17` | Trigger B : son / vélocité | | ⚠️ |
| `18` / `19` | Trigger C : son / vélocité | | ⚠️ |
| `1A`–`1D` | Trigger S : son ? / vélocité (Trg S Vel), puis Signature (`0` 4/4 · `1` 3/4 · `2` 5/4) | voir la note | ❓ |
| `1E`–`27` | ❓ | | |

**Contradiction dans ra-editor** : pour Trigger S (son, vélocité) et Signature, il lit les dumps aux adresses `1A`, `1B`, `1C`, mais envoie les changements de paramètre à `1B`, `1C`, `1D`. L'un des deux est faux : à trancher au test T5. Les 82 paramètres de son connus, eux, sont cohérents entre lecture et écriture.

**Tempo (hypothèse)** : `poids fort × 128 + poids faible` va de 0 à 3199, ce qui colle avec un tempo en dixièmes de BPM (0,2…320,0). Les valeurs Global et Extern du tempo de programme sont sans doute des valeurs spéciales. ❓

## 6. FDAT — un effet (18 octets)

| Adr | Paramètre | Valeurs |
|---|---|---|
| `00` | Type | `0` Bypass · `1` Chorus · `2` Flanger · `3` Phaser · `4` Overdrive · `5` Delay · `6` ModDelay · `7` Reverb · `8` Lo-Fi |
| `01` | Output | `0` Out1 · `1` Out1+2 · `2` Out2 · `3` Out3 · `4` Out3+4 · `5` Out4 · `6` Out5 · `7` Out5+6 · `8` Out6 |
| `02`–`0C` | Param 1–11 | sens selon le type (tableau ci-dessous) |
| `0D`–`11` | ❓ | |

Delay, ModDelay et Reverb ne sont disponibles que sur FX1 et FX2. ⚠️

| Type | P1 `02` | P2 `03` | P3 `04` | P4 `05` | P5 `06` | P6 `07` | P7 `08` | P8 `09` | P9 `0A` | P10 `0B` | P11 `0C` |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Chorus | Speed | Depth | | Delay | | | | | | | |
| Flanger | Speed | Depth | | | Feedback | | | | Polarity | | |
| Phaser | Speed | Depth | | | Feedback | Center | Spacing | | Polarity | | |
| Overdrive | | Drive | Post Gain | | | Cutoff | | | | | |
| Delay | | | | Length | Feedback | Cutoff | | | Polarity | Auto Pan | Sync |
| ModDelay | Speed | Depth | Sync | Length | Feedback | Lo Cut | Hi Cut | Spread | | | |
| Reverb | Size | Shape | Decay | Predelay | | Lo Cut | Hi Cut | | Diffusion | Damping | |
| Lo-Fi | | | | | S&H Rate | Drive | Gain | | | | |

## 7. GDAT — réglages globaux (199 octets)

| Adr | Paramètre | Valeurs | |
|---|---|---|---|
| `00` | Version | | ⚠️ |
| `02` | Popup Time | façade : 0,1–15,5 s | ⚠️ |
| `03` | Label Time | idem | ⚠️ |
| `04` | LCD Contrast | 0–127 | ⚠️ |
| `05` | Input Gain | façade : 1–4 | ⚠️ |
| `06` | Master Tune | | ⚠️ |
| `07` | Global Channel | `0` Omni · `1`–`16` | ⚠️ |
| `08` | Sysex ID | 0–126 | ⚠️ |
| `09` | Global Volume | | ⚠️ |
| `0C` | Programme actif | 0–49 | ⚠️ |
| `0D` | Map Base Key | façade : C-2…G8, C2 par défaut | ⚠️ |
| `0E` / `0F` | Tempo global, poids fort (0–24) / faible | | ⚠️ |
| `10` | Tx Clock | ordre On/Off ❓ | ⚠️ |
| `11` | Ctl Send | `0` Off · `1` SysEx | ⚠️ |
| `12` | Ctl Receive | `0` Off · `1` On · `2` MP-Bundle | ⚠️ |
| `13` | Tx PrgChange | ordre On/Off ❓ | ⚠️ |
| `14` | Rx PrgChange | ordre On/Off ❓ | ⚠️ |
| `16` | Screen saver | ra-editor : `0` On, `58` Off (étrange) | ⚠️ |
| `18` | Global Pattern Mode | ordre On/Off ❓ | ⚠️ |
| `01`, `0A`, `0B`, `15`, `17`, `19`–`C6` | ❓ | | |

## 8. Pattern (PATR / PATD / PATP) ❓

Le format des pas n'est documenté dans aucune source accessible. On sait par le manuel qu'un pattern a 1 à 128 pas, que chaque pas est vide ou porte l'une des 3 vélocités du pattern (Vel. 1/2/3), et qu'il y a un pattern par son (24 par programme).

À reconstituer sur la machine (test T6) : programmer des patterns connus (un seul pas, puis deux, puis trois vélocités différentes, longueurs 16 / 17 / 128), demander leur dump, comparer. Trois pistes probables : un octet par pas (`0`–`3`), 2 bits par pas regroupés dans des octets de 7 bits (128 pas → 37 octets), ou une liste de pas allumés.

## 9. Ce que le RackAttack envoie de lui-même

- Avec **Ctl Send = SysEx**, chaque potard tourné sur la façade part en message de paramètre (SNDP, etc.). Le plugin peut donc suivre la machine en direct, et c'est aussi le moyen le plus rapide de retrouver les adresses inconnues (test T4).
- Program Change si Tx PrgChange est actif, horloge MIDI si Tx Clock est actif.
- Le bouton Power envoie All Notes Off (appui court, ce qui arrête aussi les patterns) et All Sound Off (appui long).

## Sources

- Achim Gratz, *Waldorf MIDI Implementations* (brouillon de janvier 2008, chapitre rackAttack OS 1.04) — http://synth.stromeko.net/docs/Waldorf.pdf. Document de référence, injoignable depuis ici : à lire en priorité si tu peux l'ouvrir de chez toi.
- Table des types de message dans `magic/sysex` du programme `file` (Christos Zoulas et al.), reprise de la doc Gratz — https://github.com/file/file/blob/master/magic/Magdir/sysex
- ra-editor (adsr), éditeur Python/Tkinter — https://github.com/adsr/ra-editor (fichier `ra_editor/rack_attack.py`)
- Fil « Sysex messages for Waldorf RackAttack », forum Electra One (décembre 2024) — https://forum.electra.one/t/sysex-messages-for-waldorf-rackattack/3469
- Panel Ctrlr « RackAttack panel 1.4 » (zeoka) et ses commentaires — https://ctrlr.org/rackattack-panel-1-1/
- Manuel utilisateur du RackAttack (Waldorf) — https://www.polynominal.com/sample-packs/waldorf-rack-attack/waldorf-rack-attack-manual.pdf
