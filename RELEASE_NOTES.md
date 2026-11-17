**RackAttack Editor 0.5.14** : les écrans d'enveloppe et de LFO suivent ce qu'on entend, un LFO libre ne tire plus une hauteur au hasard à chaque note, et le kick envoyé sur un autre effet s'entend tout de suite.

- **Les écrans suivent les notes** : sur Env 1 et Env 2, le point rouge part à chaque note du son (séquenceur, clavier, Écouter) et suit les temps mesurés sur la machine. Sur LFO 1 et LFO 2, un cycle avec le point à l'endroit où en est le LFO, et sa vitesse (« 1,4 s le cycle », « 3,2 Hz »).
- **LFO libre (Sync Off) dans le simulateur** : il tourne en continu, le même pour toutes les notes du son. À Speed 0, la hauteur dérive doucement au lieu de changer au hasard à chaque coup. En Key, il repart du haut à chaque note.
- **LFO 2 plus clair** : « Speed Mod » règle de combien la source change la vitesse du LFO 2, « Speed Ctrl » est cette source (Velocity, Modwheel, Pressure, Breath, Foot, Keytrack).
- **Un son envoyé sur un autre effet s'entend tout de suite** : la machine ne le faisait passer dans l'effet qu'après avoir changé la sortie de l'effet et y être revenu. Le plug-in le fait maintenant tout seul.
- **Lire les noms** : chaque programme est aussi demandé par son numéro (lecture seule). Si la machine ne prend pas les Program Change mais répond ainsi, les noms viennent de là.
- Tout ce qu'apportait la 0.5.13 reste : solo et mute en glissant, stop qui arrête la machine, Mode MIDI des kits, canal d'« accorde mon son » au choix.

Pour garder un kit sur la machine, toujours **Shift + Store**.
