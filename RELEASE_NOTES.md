**RackAttack Editor 0.5.14** : double-clic pour remettre un réglage à zéro, « tout remettre à zéro », des écrans d'enveloppe et de LFO qui suivent les notes, et le son envoyé sur un autre effet s'entend tout de suite.

- **Double-clic sur un réglage** : il revient à sa valeur neutre (0 % pour les réglages qui vont dans les deux sens, ouvert pour le volume, la coupure, l'Osc 1 et la note la plus haute, 0 pour les autres). Dans le séquenceur, sur la page Son et dans la version web. Avec des pas sélectionnés, le double-clic efface toujours leurs verrous.
- **Tout remettre à zéro** (OPTIONS du séquenceur) : la machine vide le programme en cours (24 sons simples, sans effet), la séquence et les noms des pistes aussi, après confirmation. Ce qui est rangé dans la machine ne bouge pas (Shift + Recall), la séquence revient avec Annuler.
- **Les écrans suivent les notes** : sur Env 1 et Env 2, le point rouge part à chaque note du son (séquenceur, clavier, Écouter) et suit les temps mesurés sur la machine. Sur LFO 1 et LFO 2, un cycle et sa vitesse (« 1,4 s le cycle », « 3,2 Hz ») ; en Sync Key, le point suit le LFO depuis la dernière note. En Sync Off, chaque note prend le LFO n'importe où (la machine fait pareil) : pas de point.
- **LFO 2 plus clair** : « Speed Mod » règle de combien la source change la vitesse du LFO 2, « Speed Ctrl » est cette source (Velocity, Modwheel, Pressure, Breath, Foot, Keytrack).
- **Un son envoyé sur un autre effet s'entend tout de suite** : la machine ne le faisait passer dans l'effet qu'après avoir changé la sortie de l'effet et y être revenu. Le plug-in le fait maintenant tout seul.
- **Lire les noms** : chaque programme est aussi demandé par son numéro (lecture seule). Si la machine ne prend pas les Program Change mais répond ainsi, les noms viennent de là.
- Tout ce qu'apportait la 0.5.13 reste : solo et mute en glissant, stop qui arrête la machine, Mode MIDI des kits, canal d'« accorde mon son » au choix.

Pour garder un kit sur la machine, toujours **Shift + Store**.
