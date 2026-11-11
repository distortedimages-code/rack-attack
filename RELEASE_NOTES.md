**RackAttack Editor 0.5** : le RackAttack en logiciel, et une mesure guidée pour qu'il sonne comme une vraie machine.

- **Mode simulation** : pas de RackAttack sous la main ? Le plug-in en joue un en logiciel, par sa sortie audio. Même architecture que la machine (2 oscillateurs avec FM, ring, crack, filtre multimode avec drive, 2 enveloppes, 2 LFO, 4 effets : chorus, flanger, phaser, overdrive, delay, mod delay, reverb, lo-fi) et mêmes messages : le séquenceur, l'éditeur et les verrous marchent pareil. Bouton dans le branchement guidé, ou *Outils → Mode simulation*. Un kit de démo est prêt à jouer.
- **Mesurer mon RackAttack** (*Outils*) : avec la machine branchée en MIDI et ses sorties sur ta carte son, le plug-in lui fait jouer des sons de test pendant environ 5 minutes et en tire un fichier de mesures : hauteurs, volumes, enveloppes et leurs formes, filtre, résonance, LFO, crack, et un enregistrement des cymbales.
  - Avant de commencer : un vu-mètre et un son test pour être sûr que le son arrive bien, puis deux cases à cocher (enceintes coupées, mains loin de la machine).
  - Pendant la mesure : « NE TOUCHE PAS À LA MACHINE », le fantôme qui mesure des objets sur un tapis roulant, et quelques conseils pour patienter. Si quelqu'un touche la machine, tout se met en pause et la partie en cours recommence.
  - Le son 24 du kit sert de cobaye ; il est remis comme avant à la fin, rien n'est enregistré dans la machine.
  - Le fichier `.ramesure` (dans *Documents/RackAttack/Mesures*) s'envoie à qui veut ; *Outils → Charger des mesures…* fait alors sonner son simulateur comme cette machine. Il contient des enregistrements des sons de la machine : garde-le pour toi.
- **Essai sans machine** : l'assistant de mesure peut tourner sur le simulateur, en accéléré, pour voir comment ça se passe.

Pour garder un réglage de son, toujours **Shift + Store** sur la machine.
