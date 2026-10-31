# Le modèle et sa calibration

## Pourquoi simuler le trajet du gaz

Un vrai moteur n'a rien de périodique : chaque explosion diffère de la précédente et
l'échappement est turbulent. Deux modèles plus simples ont été essayés et écartés à
l'oreille :

- des impulsions dans un tube résonant : « un pet dans une canette » ;
- une somme de raies pures, calées sur la rotation : « un moustique », trop propre.

## Ce qui est simulé

1. **Cylindres.** Chacun garde sa pression, différente à chaque cycle. Elle se vide à
   l'ouverture de la soupape, en un temps fixé par la course. Le calage varie légèrement.
2. **Collecteurs.** Une ligne à retard par cylindre, de longueur propre à l'architecture :
   égale en ligne, par rangée en V, très inégale sur un boxer.
3. **Tube.** Une part de l'onde revient et se mêle à la suivante. Le tube rayonne les
   variations de débit, d'où l'énergie dans les médiums.
4. **Silencieux.** Des chambres dispersent l'onde, puis un filtre retire le haut du spectre.
5. **Le reste.** Turbulences proportionnelles au débit, admission, grondement sans rythme,
   souffle mécanique, claquements de soupapes, claquement diesel, écho de l'habitacle.
6. **Niveau.** Un réglage automatique très lent harmonise toutes les configurations, sans
   gommer une accélération. Un limiteur agit sur le gain, sans déformer le signal.

La charge se déduit de la montée du régime : le moteur fonctionne avec le seul régime.

## Le lever de pied

- **Coupure d'injection :** la pression d'échappement chute, le moteur n'est plus qu'une pompe.
- **Détonations :** du carburant imbrûlé s'enflamme dans le tube, surtout juste après le
  lever de pied, jamais au ralenti, sous charge ni sur un diesel. C'est une onde qui fait
  résonner le tube selon sa longueur et la cylindrée. Force, brillance, durée et fréquence
  dépendent de l'échappement. Un premier essai en claquements de bruit blanc sonnait comme
  « un caillou sur le fond de caisse ».
- **Turbo :** la pression monte avec retard, la turbine garde son élan et son sifflement
  redescend, la wastegate siffle à pleine pression, puis la pression se décharge (dump
  valve ou flutter).

## Calibration

Le réglage est calé sur six enregistrements de voitures de Wikimedia Commons (série
« engine and exhaust sound », CC BY 3.0 : Citroën DS3, Honda CR-X, Kia Ceed, Audi 100,
Abarth 500, BMW E46). Ils ont servi à la mesure seule et ne sont pas dans ce dépôt.

Moyennes sur les fenêtres les plus fortes, en dB par octave :

| | 63 Hz | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k | Tonal > 500 Hz | Platitude |
|---|---|---|---|---|---|---|---|---|---|---|
| Réel | −9 | −7 | −15 | −16 | −17 | −23 | −30 | −39 | 0,27 | 0,24 |
| Modèle à raies, écarté | −27 | −12 | −7 | −7 | −12 | −21 | −35 | −52 | 0,51 | 0,11 |
| Simulation actuelle | −9 | −4 | −10 | −15 | −19 | −23 | −29 | −33 | 0,33 | 0,33 |

Ces mesures disent si le spectre est juste, pas si le son plaît : seule l'écoute tranche.
Deux tests protègent ces cibles (« ne siffle pas comme un moustique », « garde son grain
dans les aigus »). Les aigus restent volontairement au-dessus du réel, un son plus sombre
ayant été jugé étouffé. Limite connue : les graves sont plus réguliers qu'en réel
(périodicité de 0,73 contre 0,45).

## Reproductibilité

Le rendu est identique au bit près entre le natif, WebAssembly et la version Kotlin d'origine
(CANCitroen) : calculs en `double` arrondis en `float`, ordre des tirages aléatoires figé,
pas de contraction en virgule flottante. `tools/compare_wav.py` compare deux rendus.
