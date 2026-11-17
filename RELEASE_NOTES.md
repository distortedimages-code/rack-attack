**RackAttack Editor 0.5.10** : la mesure guidée ne bloque plus sur un faux changement de volume.

- **Plus de pause « Le son a changé de volume »** quand personne n'a touché la machine. À la fin d'une partie, si la note de référence n'a plus le même niveau, la mesure attend le silence, renvoie tout le son de test à la machine (elle a pu rater un message) et réécoute ; si l'écart reste, la partie est refaite une fois toute seule, puis la mesure continue avec une remarque.
- **Chaque partie commence dans le silence** : un long relâchement laissé par la partie d'avant ne fausse plus la note de référence.
- **Une mesure arrêtée est gardée** dans Documents › RackAttack › Mesures (`RackAttack-mesure-arretee-….ramesure`) pour voir ce qui a coincé. Elle ne se charge pas comme réglage du simulateur.
- Tout ce qu'apportait la 0.5.9 reste : le simulateur réglé sur une vraie machine, l'enveloppe 2 qui règle le volume, et la mesure qui va plus loin (enveloppes, ampli, LFO, modulation du filtre).

Pour garder un kit sur la machine, toujours **Shift + Store**.
