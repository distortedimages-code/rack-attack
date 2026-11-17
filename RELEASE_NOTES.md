**RackAttack Editor 0.5.13** : solo et mute en glissant les sons, un stop qui arrête vraiment la machine, les kits en Mode MIDI Plug-in signalés (et passés en Native d'un clic), le canal d'« accorde mon son » au choix.

- **Solo et mute en glissant** : dans le séquenceur, tire un son vers le bas pour le couper (ou le remettre), vers le haut pour le mettre en solo (ou l'en sortir). Plusieurs solos possibles ; les autres sons se grisent. Aussi dans le menu du clic droit, et dans la version web.
- **Le stop arrête tout sur la machine** : il envoie maintenant All Notes Off sur le canal de la machine et ses canaux accordés. Un son dont le pattern interne est en Latch ou Toggle continuait à jouer après le stop.
- **Lire les noms** : quand la machine ne prend pas les Program Change, la lecture s'arrête au bout de 3 programmes au lieu de remplir la liste avec 50 fois le même nom, et dit quoi régler (Rx PrgChg sur On, le canal).
- **Kits en Mode MIDI Plug-in ou GlobalChn** : beaucoup de kits d'usine le sont, et la machine n'écoute alors plus les notes du séquenceur (Plug-in) ou plus les sons accordés (GlobalChn). Une étiquette rouge le dit en haut du séquenceur ; un clic dessus passe le kit en Native.
- **Accorde mon son : choix du canal** par clic droit sur la case (le canal de la machine et les canaux déjà pris sont signalés). Un clic simple prend toujours le plus haut canal libre.
- **FX Send dans le simulateur** : un dosage sec / effet comme sur la machine (le sec reste entier jusqu'au milieu, puis s'efface), et non plus un simple envoi.
- **La liste des programmes de la page Kit s'affiche** : elle était toute noire (mais on pouvait choisir dedans) dès qu'elle devait défiler, comme tout menu trop long pour l'écran.
- **Le filtre en Bypass grise ses réglages** (coupure, résonance, keytrack, modulations), sur la page Son, dans le séquenceur, l'accès rapide et la version web. Ils restent réglables, pour préparer le son. Drive reste actif : il sature avant le filtre.
- **Sortir du mode simulation** rouvre les ports MIDI à neuf et laisse un instant à la machine avant de la chercher, comme quand on rechoisissait les ports à la main.
- Tout ce qu'apportait la 0.5.12 reste : le simulateur a les enveloppes, les LFO et l'ampli d'un vrai RackAttack.

Pour garder un kit sur la machine, toujours **Shift + Store**.
