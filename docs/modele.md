# Le modèle et sa calibration

## Pourquoi simuler le trajet du gaz

Un vrai moteur n'a rien de périodique : chaque explosion diffère de la précédente et
l'échappement est turbulent. Deux modèles plus simples ont été essayés et écartés à
l'oreille :

- des impulsions dans un tube résonant : « un pet dans une canette » ;
- une somme de raies pures, calées sur la rotation : « un moustique », trop propre.

Un troisième défaut est apparu à l'écoute d'un gros V8 : « une fermeture éclair qu'on monte ».
Sur le spectrogramme, c'était un éventail de raies parallèles, aussi fortes à 3 kHz qu'à 300 Hz,
et un timbre qui montait avec le régime. Les vrais moteurs mesurés montrent autre chose : quelques
raies graves, l'énergie groupée dans des bosses fixes sous 600 Hz, et des raies qui s'effacent peu à peu vers les aigus. La chaîne décrite ici découle de ce constat, précisé plus bas.

## Ce qui fait avancer la voiture

Rien n'est saisi à la main : tout part de la cylindrée, du régime maximal et de l'alimentation.

1. **Couple.** Une pression moyenne au frein (de 10,5 à 14,5 bar en atmosphérique essence, selon
   le régime maximal), une courbe de respiration selon le régime, une perte quand la vitesse de
   piston dépasse 24 m/s, et l'effet de l'échappement : une ligne libre gagne en haut et perd en bas.
2. **Turbo.** Sa pression se règle, au-dessus de la pression atmosphérique : 0,5 à 1,5 bar sur la
   route, jusqu'à 2 bar sur une sportive de série, 3 bar et plus en course. Chaque bar ajoute 90 % d'un
   remplissage. Le calculateur tient ensuite le couple constant : le plateau des moteurs turbo. Le modèle
   ignore la casse et le cliquetis : rien n'interdit 3,5 bar sur un moteur de série. Le temps que met
   cette pression à venir est calculé : voir « Le turbo ».
3. **Moteur.** Inertie selon la cylindrée, frottements, pompage contre un papillon fermé (le frein
   moteur), collecteur qui met du temps à se remplir, régulation de ralenti, démarreur.
   **Rupteur :** la combustion s'arrête au régime maximal et ne reprend que 4 % plus bas, après
   30 ms au moins. Le papillon reste ouvert, donc le moteur ne redescend que sur ses frottements :
   il rebondit de 7 à 15 fois par seconde selon le moteur.
4. **Transmission.** Un embrayage qui patine ou colle selon le couple transmis, un arbre primaire et ses inerties,
   des arbres de roue qui se tordent, et une boîte qui est une pièce à part : voir « La boîte ».
5. **Voiture.** Masse, résistance de l'air et au roulement, transfert de charge, et des pneus dont
   l'adhérence limite le couple : une traction trop puissante patine.

Le 0 à 100 affiché est mesuré en conduisant ce modèle, pied au plancher. Ces chiffres restent ceux
d'un modèle : les tests comparent la puissance maximale à de vrais moteurs, à 30 % près.

## Le turbo

`TurboCharger` (`core/src/turbo.cpp`) avance à chaque pas de 1 ms de `DrivingModel`. Il ne contient aucune constante de temps de « retard » : le retard est ce qu'il faut à l'arbre pour accumuler son énergie.

```
J·ω·dω/dt = P_turbine − P_compresseur − P_paliers
```

- **Dimensionnement.** Le moteur choisit son turbo : le débit au point nominal (88 % du régime maximal, à la pression demandée) et la vitesse de bout de pale que demande cette pression donnent le diamètre de la roue. Le réglage de taille multiplie ce diamètre par 0,85 ou 1,2 : la section de la turbine suit en D², l'inertie en D⁵. Plus de pression demande une plus grosse roue.
- **Turbine.** Sa pression d'entrée suit la loi de l'ellipse de Stodola, résolue directement : `p3² = p4² + ṁ²·T3/K²`, où K compte aussi la soupape de décharge si elle est ouverte. La puissance est la détente de ce gaz, avec un rendement selon la vitesse réduite U/c (sommet à 0,65). `T3` vient du carburant brûlé, avec une lenteur pour les parois du collecteur (0,35 s à la montée, supposé). Le gaz sort à débit moyen, mais les bouffées ajoutent à la détente un écart de pression qui décroît avec le régime (0,25 bar à l'arrêt, supposé) : sans lui, l'arbre ne tournerait pas au ralenti, et le bas régime manquerait d'énergie.
- **Compresseur.** La hauteur dépend du débit réduit, à la manière de Moore et Greitzer : sommet à la limite de pompage, pente positive à gauche, chute vers le blocage à droite. La colonne d'air du conduit a son inertie, et le volume de 2,5 à 14 L entre le compresseur et le papillon se remplit. L'air sort chaud (`T2`, selon le rendement), passe l'échangeur (efficacité de 0,86 à bas débit, 0,72 au nominal, avec sa masse thermique) et y perd 0,12 bar au point nominal.
- **Soupapes.** La wastegate, ressort et membrane, ouvre autour de la pression demandée, avec une anticipation du calculateur, et ouvre aussi si les roues dépassent la vitesse qu'elles supportent. Une soupape de décharge ouvre sous le vide du collecteur s'il y a de la pression à vider, et reste ouverte tant que ça passe. Sans elle (réglage flutter), l'air revient par le compresseur : le débit s'inverse, la pression retombe, le compresseur reprend, et ça recommence. Le pompage n'est pas écrit : c'est la pente positive de la ligne de vitesse, chargée par ce volume et cette colonne d'air.
- **La courbe de la fiche** est l'équilibre de ce même bilan (`turboSteady`, deux dichotomies) : ce qu'on obtient en tenant le régime. Un test vérifie que la simulation y arrive. `measurePerformance`, lui, conduit le vrai modèle : il compte le retard.
- **La contre-pression** coûte du couple : ce qui dépasse ce qu'un turbo bien apparié coûterait au même débit. Les couples de référence viennent de vrais moteurs, qui la comptent déjà : le turbo moyen perd très peu, un petit s'étouffe en haut, un gros soulage.

### Face aux chiffres publiés

| | Modèle | Référence |
|---|---|---|
| Inertie de l'arbre, 2,0 L | 2,8·10⁻⁵ kg·m² (taille moyenne) | 1,844·10⁻⁵ (rotor) + 8,786·10⁻⁶ (roue) kg·m² : Chung et Lee, 2016, sur un 2,0 L essence (lu) |
| Montée à 2000 tr/min, 2,0 L | 2,6 s à régime tenu, 2,3 s en quatrième depuis 1800 tr/min (le régime monte) | 2785 ms sur un échelon de charge à 2000 tr/min : Energies 2013, 6(3), 1746 (vu cité, l'article n'a pas pu être lu) |
| Inertie et retard | ×2 sur l'inertie, retard × ≈1,6 (à 3000 tr/min) | -50 % d'inertie, -25 % de retard : Guarda et coll., 2020 (vu cité) ; TiAl, jusqu'à -13 % sur la montée : Park et coll., 2022 (vu cité) |
| Vitesse de l'arbre | 150 000 à 175 000 tr/min (2,0 L moyen), jusqu'à 290 000 (roues de 34 mm) | 185 000 tr/min au plus sur une GTX2860R Gen II : Garrett (vu sur le catalogue). Dans la plage de 100 000 à 250 000 de la plupart des turbos de voiture |
| Pompage sans soupape | 7 à 9 Hz sur le 2,0 L de 6,5 L de volume | 5,1 et 9,6 Hz en pompage léger sur deux compresseurs : thèse d'Imperial College, 2020 (vu cité). La fréquence dépend de la tuyauterie |
| Sortie de compresseur à 2,3 bar | 140 °C | Ordre de grandeur connu (non référencé) |
| Perte de l'échangeur | 0,12 bar au nominal | Ordre de grandeur connu (non référencé), réglé ici : ce n'est pas un résultat |

**Ce qui est supposé :** le diamètre de la roue du moteur mesuré par Chung et Lee (ils ne le donnent pas ; on prend celui que ce calcul donne à 2,0 L et 1,3 bar) ; la part du gaz qui passe la turbine au nominal (moitié, 70 % sur un diesel) ; la forme de la ligne de vitesse ; les rendements maximaux (0,74 et 0,68) ; la lenteur des parois du collecteur ; le frottement des paliers (800 W pour une roue de 49 mm au nominal) ; la vitesse limite des roues (520 m/s).

**Ce qui n'est pas modélisé :** le détail des bouffées dans la turbine (seule leur énergie moyenne compte), la géométrie variable (un diesel y gagnerait beaucoup : le sien réagit comme un turbo fixe), le biturbo séquentiel. Le gros turbo pompe légèrement à très bas régime sous pleine charge : c'est un vrai défaut de turbo surdimensionné, mais le niveau n'a pas été comparé à une mesure. Sur un V, le modèle sonore et le gaz voient un turbo équivalent, même si les vues en dessinent deux.

### Le retard, moteur par moteur

Temps pour atteindre 90 % de la pression établie, à régime tenu, depuis une conduite tranquille :

| Moteur | Taille | 2000 tr/min | 2500 | 3000 | 4000 | 0-100 km/h | Vmax | Couple max | Puissance |
|---|---|---|---|---|---|---|---|---|---|
| 2.0 de 1,3 bar | petit | 1,0 s | 0,7 s | 0,6 s | 0,4 s | 6,4 s | 254 | 350 Nm | 182 kW |
| 2.0 de 1,3 bar | moyen | 2,6 s | 1,2 s | 0,9 s | 0,6 s | 6,9 s | 260 | 357 Nm | 194 kW |
| 2.0 de 1,3 bar | gros | pas de pression | 0,3 de la pression | 2,0 s | 1,1 s | 7,3 s | 262 | 362 Nm | 201 kW |

`hot_hatch_turbo` montait auparavant à 90 % en 2,0 s à 2500 tr/min et en 1,6 s à 3800 tr/min, avec une pression déjà présente à mi-charge ; il monte maintenant en 1,2 s et 0,6 s en partant de presque rien. Le 0-100 passe de 6,84 à 6,89 s, le maximum de 198 à 194 kW.

### Le chemin de l'air, à l'écran

Dans le gaz (`fluidGas.ts`), un moteur atmosphérique garde son simple conduit. Sur un turbo, l'admission est une vraie chaîne de tronçons calculés : boîte à air (grande section, le filtre y freine), conduit d'entrée, compresseur, tube chaud, échangeur (grand volume, qui freine et refroidit, de 6 L environ avec les tubes sur un 2 L, comme en C++), tube froid, décharge, papillon, plénum. Le gaz ne calcule pas son turbo : il reçoit du C++ le rapport de pression, la sortie du compresseur et l'ouverture des soupapes, et sa turbine a la même loi de taille. Sa pression avant la turbine reste à 15 % de celle du C++ au point nominal. Au ralenti des animations, le gaz a douze fois moins de temps physique : le volume met alors douze fois plus de temps à l'écran à se remplir.

Les vues (`turboChain.ts` pour le moteur, 2D et 3D ; `ExhaustView` et `CarStage` pour le reste) dessinent la même chaîne, seulement sur un turbo : deux roues sur un arbre, leurs carters, la boîte à air et son débitmètre, les tubes teintés par la température, l'échangeur à l'avant, la décharge et la wastegate qui bougent selon la télémétrie. Sur un V ou un boxer, un turbo par rangée, comme le collecteur, et un seul échangeur. L'arbre tourne à l'écran proportionnellement à sa vraie vitesse et selon `motion.slow`, avec un ralenti fixe de 60 fois en plus : à 150 000 tr/min, même douze fois ralenti, il fait 200 tours par seconde. Trop vite pour l'œil, ses aubes laissent place à un disque flou.

### Le son du turbo

Le sifflement est à la vitesse de l'arbre : son ordre 2 domine, avec un peu d'ordre 1 (le balourd). La hauteur et le niveau suivent donc le vrai arbre, sa montée après un coup de gaz, sa lente descente sans soupape. Sous `setTurbo`, la décharge et le flutter n'ont plus d'enveloppe : leur niveau suit la force du jet, débit fois racine de la pression (la soupape reste du bruit sur une large bande, autour de 1,8 à 3,8 kHz), ou le retour par la roue du compresseur. Leurs tirages viennent d'un générateur à part : ils ne décalent aucun autre son. Sans `setTurbo`, un `EngineSound` piloté de l'extérieur garde l'ancien comportement, qui se déduit de la pression.

L'ordre dominant, le niveau du flutter et le partage entre ordres restent des choix : aucun enregistrement de référence n'a été comparé, et seule l'écoute tranchera.

## Le tube du turbo

`turboPipeMm` est la longueur du tube entre les cylindres et la turbine, de 150 à 3000 mm (`core/src/turbo_pipe.cpp`). 400 mm est la référence de la calibration : à cette valeur, rien de ce qui suit ne s'exécute, et aucune empreinte ne bouge. Seul l'écart à cette référence existe, et aucune constante de retard n'y est écrite : le retard reste ce qu'il faut à l'arbre pour accumuler son énergie, avec moins d'énergie.

- **La paroi.** Le gaz passe sur la surface ajoutée, `π·d·ΔL`, à une paroi qui a sa masse. Échange intérieur de Dittus-Boelter (`Nu = 0,023·Re^0,8·Pr^0,4`, majoré de 1,5 parce que le flux pulse), selon le débit ; dehors, 50 W/(m²·K) pour la convection sous la voiture et le rayonnement ; acier de 1,5 mm, soit environ 40 s pour chauffer. À froid ou à faible débit, le gaz y laisse beaucoup de chaleur, donc d'enthalpie à la turbine. C'est la cause dominante.
- **Le volume.** La section du tube sur la longueur ajoutée se remplit : la pression avant la turbine rejoint celle de l'ellipse avec `τ = V·(dp/dṁ)/(γ·R·T)`, et ce qui sort par la turbine n'est pas encore ce qui entre. Quelques dizaines de millisecondes : 37 ms pour 63 % de la montée avec 3 m.
- **Le parcours.** La masse de gaz du tube, renouvelée par le débit : 125 ms à 20 g/s, 12 ms à 200 g/s. Le tube est mélangé, pas un piston : c'est son temps moyen, pas un retard pur.
- **Les bouffées.** Le volume du tube filtre au premier ordre la cadence d'allumage, avec ce même `τ`. L'écart de pression qu'apportent les bouffées est multiplié par le rapport de ce filtre à celui d'un tube de 400 mm : au-dessus de 1 sur un tube court, très en dessous sur un long. Cela compte surtout à bas régime.
- **Le frottement.** `f·(ΔL/d)·½ρv²` s'ajoute à ce contre quoi les cylindres poussent, pas à la pression de la turbine : quelques Nm en haut.
- **Le retour d'air.** Déporté, le compresseur part avec la turbine : `chargeVolumeM3`, la colonne d'air du conduit et sa perte s'allongent de ΔL à la section du conduit. Le pompage ralentit de 8,7 à 3,3 Hz à 3 m : c'est lui que suit le flutter à l'oreille.
- **Plus court que 400 mm.** On ne peut pas retirer un volume qui n'est pas compté : seules restent la chaleur que le tube retiré aurait prise (le gaz est plus chaud, en inversant la loi de la paroi), les bouffées mieux gardées, et moins de chaleur à prendre avant la turbine (un collecteur de 3 kg reste, supposé). Le retour d'air ne change pas : l'échangeur reste à l'avant.

### Ce que cela donne

`hot_hatch_turbo`, 2,0 L à 1,3 bar. `--scenario turbo` : temps jusqu'à 50 et 90 % de la pression demandée. « Froid » : tel que le scénario le joue, le tube n'a roulé que quelques secondes. « Chaud » : après deux minutes de plus à mi-charge.

| Tube | Scénario, froid | Scénario, chaud | Gaz à 90 %, froid | 2500 tr/min tenus, 90 % | 3500 tr/min, 90 % | Gaz établi, 2500 / 5500 tr/min | Couple à 2500 / 5000 | Puissance | 0-100 km/h | WLTC | 90 km/h |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 150 mm | 1,39 / 1,91 s | 1,43 / 1,96 s | 770 °C | 1,06 s | 0,66 s | 794 / 888 °C | 351,1 / 349,0 Nm | 194,6 kW | 6,75 s | 8,89 L/100 | 7,26 L/100 |
| 400 mm | 1,76 / 2,33 s | 1,70 / 2,28 s | 754 °C | 1,21 s | 0,71 s | 782 / 882 °C | 350,2 / 348,4 Nm | 194,4 kW | 6,86 s | 8,90 L/100 | 7,26 L/100 |
| 1000 mm | 2,55 / 3,34 s | 2,39 / 3,16 s | 647 °C | 1,57 s | 0,79 s | 756 / 866 °C | 349,5 / 347,7 Nm | 194,0 kW | 7,14 s | 8,92 L/100 | 7,26 L/100 |
| 2000 mm | 4,19 / 5,31 s | 3,74 / 4,81 s | 519 °C | 2,54 s | 0,99 s | 714 / 841 °C | 348,9 / 346,7 Nm | 193,4 kW | 7,61 s | 8,93 L/100 | 7,20 L/100 |
| 3000 mm | 6,06 / 7,47 s | 5,32 / 6,66 s | 431 °C | 7,97 s | 1,26 s | 675 / 817 °C | 348,4 / 345,8 Nm | 192,7 kW | 8,06 s | 8,95 L/100 | 7,20 L/100 |

Le temps en scénario s'allonge plus que le retard seul : en quatrième, tant que la pression n'arrive pas, le régime monte lentement, et le gaz reste rare. À 2500 tr/min, un tube de 3 m approche la limite d'énergie : les 90 % arrivent en rampant, avec la paroi qui chauffe. La consommation bouge à peine. À 90 km/h, elle baisse de 0,8 % d'un coup entre 1 et 2 m : la cause n'a pas été isolée, ce n'est pas un gain à retenir.

Ce que pèse chaque cause, à 3 m, en la retirant seule (`--no-skip -tc="le tube : poids*"`) :

| Retiré | 3500 tr/min tenus | Scénario, chaud |
|---|---|---|
| Rien | 1,26 s | 6,66 s |
| Échange avec la paroi | 0,87 s | 2,99 s |
| Masse de la paroi (÷ 100 : déjà chaude) | 1,13 s | 4,20 s |
| Filtre des bouffées | 1,19 s | 5,84 s |
| Volume | 1,14 s | 6,50 s |
| Retour d'air froid | 1,14 s | 6,46 s |
| Temps de parcours | 1,24 s | 6,65 s |
| Frottement | 1,26 s | 6,70 s |

### Face aux montages réels

| | Modèle | Référence |
|---|---|---|
| Pleine pression sur un V8 | 500 tr/min plus tard avec 2,6 m en plus (4,8 L, 0,48 bar, gros turbo) | Environ 500 tr/min plus tard avec 3,66 m en plus, 4,8 L, deux GT35 de 61 mm, 0,48 bar, au banc : R. Holdener, EngineLabs (article lu, essai sans publication scientifique ni barre d'erreur) |
| Puissance maximale | -0,4 % sur ce V8 ; -0,9 % à 3 m sur le 2,0 L | 613 → 609 ch : même essai |
| Retour d'air seul | 0,2 s sur 6,7 s en scénario, 0,1 s à 3500 tr/min | 2,44 m de plus au refoulement : presque rien à 0,48 bar, un retard visible à 0,9 bar : même essai |
| Gaz moins chaud à la turbine | -107 °C établi à 2500 tr/min, -65 °C à 5500 ; -296 °C dans le trajet « chaud », paroi encore tiède | « 260 °C de moins » : Turbodynamics, estimation d'un atelier, sans condition de mesure (lu) |
| Volume avant la turbine et réponse | Le retard croît, surtout par les bouffées | SAE 920361 et 2004-01-0926 : un volume mort avant la turbine dégrade le transitoire (résumés lus, sur des diesels) |
| Collecteur intégré | -18 % sur le temps jusqu'à 90 %, +16 °C | Le sens est connu ; aucun chiffre comparable trouvé |

**Ce qui est supposé :** le diamètre du tube (50 mm sur 2 L, celui du gaz à l'écran), la paroi de 1,5 mm, le coefficient extérieur, la majoration de 1,5 de l'échange (Caton et Heywood, 1981, *Int. J. Heat Mass Transfer*, le mesurent bien plus près des soupapes : de mémoire, non relu), le frottement de 0,02, le collecteur de 3 kg, et le filtre du premier ordre pour les bouffées. Aucun n'a été réglé sur le retard ou sur l'essai ci-dessus : l'accord à 500 tr/min près vient d'un seul essai, sur un moteur approché, et ne prouve pas le reste. Le gros de l'effet repose sur la paroi : c'est aussi la partie la moins mesurée. Le turbo n'est pas redimensionné : un vrai montage déporté prendrait souvent une turbine plus petite.

**Ce qui n'est pas modélisé :** les ondes dans le tube pour le bilan de l'arbre (seule leur énergie moyenne compte, comme sur le reste de la turbine), la turbine et la ligne qui raccourcirait d'autant derrière elle, l'huile et son retour sur un turbo déporté, l'air froid sous la voiture qui dispenserait d'échangeur, la chaleur sous le capot autour d'un turbo collé à la culasse.

### À l'écran

Dans le gaz, un tube déporté a ses propres cellules et un volume de plus avant la turbine : les bouffées et la chaleur s'y perdent par le même calcul que le reste, et la partie `hot` de `TURBO_INTAKE` s'allonge de la même longueur qu'en C++. Plus court, ce qui manque manque au volume de la jonction. Le gaz reste un calcul d'affichage, sans retour vers le son.

`turboChain.ts` place le turbo en vraie grandeur : il recule le long du moteur jusqu'à ce que la boîte à air ait passé le volant, puis descend sous le niveau de l'échangeur, puis tout le reste part vers l'arrière ; un test vérifie que les pièces ne traversent ni le volant ni le gabarit du bloc. La 3D garde cette échelle, et son cadre suit : à 3 m, le moteur y est petit, et se regarde en zoomant. La coupe de côté, elle, replie le tube au-delà de 2,5 alésages, avec un trait de coupure et sa longueur écrite, pour que les cylindres restent lisibles. Sur la voiture (`CarStage`), le turbo part vers l'arrière de la vraie longueur, sans sortir de la caisse, et bien déporté le filtre le suit.

### Le son

Il change par ce qui le pilotait déjà, sans une ligne de plus dans `EngineSound` : le sifflement suit l'arbre, sa hauteur et son niveau montent plus tard ; la pression retarde `boost`, donc l'étouffement de la turbine ; le flutter suit le pompage, plus lent. Mesuré sur `--scenario turbo`, de 400 à 3000 mm : les bandes de 4 et 8 kHz perdent 2 dB, le reste 1 dB au plus (`tools/analyze_sound.py`), et le scénario est décalé dans le temps. Rien n'a été ajouté pour la pulsation : le filtre des bouffées vaut en dessous de quelques dizaines de hertz, où le tube est plus court que l'onde ; au-dessus, c'est un guide d'onde qui transmet presque tout, et ses réflexions contre la turbine demanderaient un modèle de cette réflexion et un enregistrement que nous n'avons pas. La place du sifflement ne change pas non plus : il reste dans la source de l'admission, même turbo déporté à l'arrière. L'écoute reste à faire.

## L'anti-lag

Pied levé, un turbo ordinaire perd son gaz : l'injection se coupe, la soupape vide la pression, l'arbre tombe. L'anti-lag (`core/src/anti_lag.cpp`, réglage `--antilag`) continue d'envoyer de l'énergie à la turbine. Aucun système n'a de constante de temps ni de pression visée : chacun change ce qui arrive à la turbine, et le bilan de puissance de `TurboCharger` fait le reste.

Il travaille si le pied est levé (moins de 3 %), si le moteur dépasse 27 % du régime maximal, hors rupteur et coupure de passage. Le calculateur tient la soupape de décharge fermée, ce que font les vraies préparations : sinon la pression s'en irait par là. L'air qui bute contre un papillon presque fermé repasse par la roue : le pompage déjà calculé, ici vers 17 Hz, devient le « chatter » qu'on entend, soupape montée ou non.

- **Retard d'allumage.** Papillon à 30 %, étincelle 55° après le meilleur couple, soit 35° après le point mort haut. Le couple suit une parabole, `1 − (Δ/68°)²`, 0,35 ici : la forme des tables « torque ratio » des calculateurs. Le travail que le piston ne reçoit pas part dans le gaz, moins 20 % dans les parois : `T3` monte vers 1090 °C. Lambda 0,9 : ce surplus ne brûle pas, faute d'oxygène, et il compte.
- **Injection d'air.** Une vanne, de 17 mm sur un 2 L, relie le tube froid au collecteur. Son débit est une loi d'orifice entre la pression de charge et celle avant la turbine, résolue par dichotomie à chaque pas (explicite, elle battrait) ; un clapet l'arrête si le collecteur pousse plus fort. Le calculateur ajoute l'essence qui lui correspond, au plus jusqu'à λ 0,65 dans le cylindre, et 70 % de ce surplus brûle dans le collecteur, s'il y fait assez chaud. La chaleur y arrive sans passer par le cycle. Sur le 2,0 L à 4400 tr/min, compresseur, flamme et turbine s'entretiennent comme une turbine à gaz : la pression tient à 1,3 bar, soupape de turbine ouverte, sans qu'aucune valeur ne l'ait demandé, jusqu'à ce que le régime tombe vers 3500 tr/min : elle retombe alors à 0,3 de la pression. Papillon à 25 %, 40° de retard : moins ouvert (15 %), il manque d'air pour porter le surplus, et le système reste bien plus faible.
- **Coupure de cylindres.** Deux explosions sur cinq, dans l'ordre d'allumage (F C C F C) : le motif tourne ainsi d'un cylindre à l'autre sur 4 ou 6 cylindres. Papillon à 35 %. Le mélange des cylindres coupés traverse sans brûler, et 45 % s'enflamme au collecteur si le gaz y dépasse 800 K (entièrement à 1100 K) : à froid il ne s'allume pas, et le collecteur refroidit encore, ce qu'on voit vers 1900 tr/min (260 °C), où le système ne sert plus à rien.

Rien ne régule le couple qui reste. Pied levé, le moteur freine beaucoup moins, et sur le 5 cylindres l'injection d'air donne même 4 Nm de plus que ses pertes en quatrième. Au point mort, tous les turbos à essence essayés retrouvent le ralenti (testé).

**Le diesel n'est pas concerné, par choix.** Ni papillon à entrouvrir, ni étincelle à retarder, ni mélange préparé à faire passer imbrûlé. Une post-injection dans un gaz riche en oxygène existe pour régénérer un filtre, à 600 °C : on n'a trouvé aucun chiffre pour en faire un anti-lag. Le site cache le choix, et tout reste identique au bit près (testé).

### Les quatre choix, chiffres mesurés

`hot_hatch_turbo` (2,0 L, 1,3 bar) sur `--scenario turbo --seconds 32`, relevé avec `--trace`, sauf la reprise après 3 s de lever. La moyenne est prise de 0,3 à 3 s après le dernier lever.

| | Sans | Retard | Air | Coupure |
|---|---|---|---|---|
| Arbre, 1 s après le lever (tr/min) | 54 000 | 109 000 | 149 000 | 110 000 |
| Arbre, 3 s après | 22 000 | 97 000 | 149 000 | 99 000 |
| Pression à 3 s (part de 1,3 bar) | 0,01 | 0,29 | 0,99 | 0,34 |
| 90 % de la pression, après 0,35 s de lever | 0,40 s | 0,21 s | 0,21 s | 0,15 s |
| 90 % de la pression, coup de gaz depuis 1800 tr/min | 2,20 s | 1,62 s | 1,92 s | 2,00 s |
| 90 % de la pression, après 3 s de lever (test, en quatrième) | 0,97 s | 0,53 s | 0,51 s | 0,54 s |
| `T3` au plus haut | 789 °C | 1090 °C | 1094 °C | 937 °C |
| Couple au vilebrequin pied levé | −80 Nm | −36 Nm | −4 Nm | −24 Nm |
| Couple retiré à cette charge d'air | 0 | 54 Nm | 34 Nm | 59 Nm |
| Air envoyé au collecteur | 0 | 0 | 27 g/s | 0 |
| Carburant pied levé | 0 | 2,08 g/s | 3,66 g/s | 2,30 g/s |
| Carburant sur les 32 s | 0,136 L | 0,163 L | 0,178 L | 0,166 L |
| WLTC, L/100 km et g CO₂/km | 8,90 et 210 | 9,48 et 224 | 9,39 et 222 | 9,54 et 225 |
| 0-100 km/h | 6,86 s | 6,64 s | 6,66 s | 6,68 s |

Les écarts ont leur cause. Le retard chauffe sans dépendre de la température du collecteur, mais à faible débit. L'injection d'air est la seule à augmenter le débit dans la turbine, d'où sa pression tenue, et la plus chère en essence. La coupure brûle moins bien ce qu'elle envoie, mais garde sa réserve : à 0,35 s de lever, elle reprend le plus vite, sur un collecteur encore chaud. Elle s'éteint plus bas dans les tours, et l'injection d'air s'y refroidit à l'air frais : vers 1800 tr/min, seul le retard gagne franchement.

### La consommation

Le couple au frein passé à `ConsumptionMeter` suit l'air admis, pas le couple rendu : un cylindre retardé ou coupé paie donc déjà tout son carburant. S'y ajoutent `extraFuelGps`, le surplus du retard et de l'injection d'air, et `extraAirGps`, l'air de la vanne. Le CO₂ suit le carburant et le lambda les deux. Rien de cela ne revient vers la physique.

### Le son

Tout passe par `setTurbo` (`TurboDrive::antiLag`), avec un générateur à part. Sans anti-lag, rien de ce code ne s'exécute : aucune empreinte ne bouge.

- L'injection ne se coupe pas : ni `overrun`, ni la chute de pression d'un lever de pied. La pression à l'ouverture de la soupape est multipliée par ce que le retard laisse de chaleur en plus (`evoRatio`).
- La coupure agit sur les forces d'explosion : un cylindre coupé s'ouvre sur 28 % de la pression et ne frappe pas le bloc. Face au retard, la raie d'allumage perd 8 dB (140 Hz), et les sous-harmoniques du motif (21 à 35 Hz) en gagnent 6 à 18 : le son est haché par la cause, pas par une enveloppe.
- Les détonations sortent du même chemin que les `pops`. Une chance par ouverture de soupape, selon que le collecteur peut enflammer ce qui arrive : on en compte 8 par seconde sur la coupure, 4 sur le retard et 1 sur l'injection d'air. Leur amplitude suit la racine de l'énergie par ouverture, rapportée à une détonation ordinaire de 90 J : environ 360 J pour un cylindre coupé.
- La flamme est un bruit grave, autour de 380 Hz, à la racine de la chaleur libérée, qui traverse la ligne comme le reste. La bande 8-16 kHz ne monte pas au-delà de celle du même turbo à plein gaz (−69 dB contre −64 dB).
- Le jet de sortie suit le débit réel, à la température calculée.

**Écoute restant à faire.** Rien n'a pu être écouté ni comparé à un enregistrement : les chances, les parts de chaleur et le niveau de la flamme sont supposés, et seule l'écoute tranchera.

### Ce qui est supposé, et ce qui manque

Sources, vues par une recherche et non lues en entier : [Antilag system, Wikipédia](https://en.wikipedia.org/wiki/Antilag_system), [Car Throttle](https://www.carthrottle.com/news/anti-lag-blogpost) et [TorqueCars](https://www.torquecars.com/tuning/anti-lag-systems.php) pour les principes et les ordres de grandeur (allumage de 30 à 45° après le point mort haut, plus de 1000 °C), des sources d'amateurs dont aucun chiffre n'a été confirmé par une mesure publiée. **Supposés :** la parabole du couple (la forme est connue, son zéro à 68° est un ordre de grandeur), les ouvertures de papillon, les 20 % de parois, les parts qui brûlent au collecteur (45 et 70 %), les 800 à 1100 K d'allumage, la section de la vanne, le seuil de 27 %.

**Pas modélisé :**
- **L'usure et la casse.** Rien ne les calcule, donc rien ne casse : un vrai système use vite le collecteur, la turbine et les soupapes, et les vrais calculateurs limitent la durée et la température. Ici, ce temps est sans limite, et `T3` peut rester au-dessus de 1000 °C. C'est écrit dans l'aide du site.
- **Le couple explosion par explosion.** `DrivingModel` garde un couple moyen ; seuls le son et l'acyclisme transmis à la boîte voient le motif, et leurs compteurs ne sont pas en phase.
- **Les vues de la voiture et du moteur.** Seule la vue du gaz dessine la vanne d'air et fait partir l'étincelle tard (`fluidGas.ts`) ; `CarStage` et `EngineView` ne la montrent pas.

## Le compresseur mécanique

`Supercharger` (`core/src/supercharger.cpp`) avance à chaque pas de 1 ms de `DrivingModel`. Son arbre est lié au vilebrequin par une courroie : sa vitesse est un rapport fixe du régime, sans turbine ni inertie à lancer. Le seul temps de réponse est celui du volume entre le compresseur et le papillon (1 L + 0,8 fois la cylindrée) : 80 % de la pression en 0,28 s à 2500 tr/min sur un 2,0 L, contre 1,2 s pour le turbo moyen au même régime.

```
dp/dt = (R·T/V)·(ṁ_compresseur − ṁ_moteur − ṁ_dérivation)
```

- **Volumétriques (Roots, à vis).** `ṁ = ρ·(V_r·N − A_fuite·√(2Δp/ρ))` : le volume engendré, moins des fuites qui suivent l'écart de pression et pas la vitesse. D'où une pression qui tient dès le bas, et un manque tout en bas seulement. Le rapport de poulie vient de la vitesse maximale de l'arbre (16 000 tr/min pour un Roots, 20 000 pour une vis, ordres de grandeur des constructeurs) ; le volume engendré, de la pression demandée à 40 % du régime maximal. Le calcul donne 0,95 L par tour sur un 2,0 L à 1 bar, soit l'ordre d'un Eaton M45 à M62. La section de fuite (`kRootsLeak`, `kScrewLeak`) est supposée : elle donne 85 % de rendement volumétrique à 8000 tr/min et 0,5 bar sur 1,5 L.
- **Le travail.** Un Roots ne comprime pas en lui : la poche s'ouvre sur la sortie, et tout le volume engendré paie `Δp`. Une vis referme d'abord son volume (rapport 1,3, supposé), puis finit à l'ouverture : `w = γ/(γ−1)·p₁·(v^(γ−1)−1) + (p₂ − p₁·v^γ)/v`. Avec `v = 1`, c'est le Roots. Ce même calcul explique deux choses connues : le Roots chauffe plus (tout ce travail finit dans l'air qui sort, 144 °C à 2720 tr/min et 1 bar, contre 104 °C pour la vis), et la vis coûte un peu plus soupape ouverte, puisqu'elle comprime quand même.
- **Centrifuge.** Une roue, comme celle d'un turbo, mais entraînée : hauteur `ψ·U²`, donc une pression qui croît avec le carré de la vitesse. Le point nominal est à 88 % du régime maximal. Avec un débit réduit de 1,5, le calcul donne 101 mm et 67 000 tr/min sur un 6,2 L à 0,8 bar, 60 mm et 120 000 tr/min sur un 2,0 L à 1 bar : l'ordre de ProCharger et de Rotrex. Vitesse de bout de pale plafonnée à 520 m/s : sur un petit moteur, 3,5 bar ne sont plus atteints. Rendement de 72 %, pas de pompage modélisé : la soupape de dérivation est censée l'éviter. Passé le point nominal, elle jette à pleine pression ce que la roue continue de monter, d'où une cassure dans la courbe de couple : un vrai montage sans décharge laisserait plutôt monter la pression, mais ici `boostBar` est tenue.
- **La soupape de dérivation.** Le vide du collecteur l'ouvre (grande ouverte à 70 % de remplissage, fermée à 95 %, en 40 ms) : pédale relâchée, le débit revient à l'entrée, sous quelques kPa. Plein gaz, elle tient aussi la pression demandée, en rendant le surplus : c'est `spillShare`. Ce qu'on rend a quand même été comprimé : à 6500 tr/min, un Roots de 1 bar rend 33 % de son débit et prend 29 kW au vilebrequin.
- **Le couple.** `fullLoadTorqueNm` est ce que donnent les pistons : le couple atmosphérique (compression abaissée de 5 %, comme un turbo), multiplié par `p/T` de l'air après l'échangeur (efficacité de 0,86 à bas débit, 0,72 au nominal, 0,05 bar de perte). L'air plus chaud d'un Roots pèse moins : à même pression, il donne moins de couple brut qu'une vis. L'entraînement se retire à part, comme le pompage, et s'ajoute au frein moteur : c'est de la puissance que l'échappement ne donne pas. Ce sont aussi ses `driveNm` qui font que, sur un Roots, monter la pression au-delà de 2 bar coûte plus qu'il ne rapporte : 3,5 bar donnent moins de couple net que 1 bar. Ce n'est pas un défaut du calcul : c'est pour cela qu'on ne suralimente pas ainsi à forte pression. La détonation n'est pas modélisée, et aucun moteur à 3,5 bar n'y survivrait à cet air-là.
- **Ce qui compare à `Aspiration::Turbo`, et a été décidé un par un.** Comme un turbo : le pic de couple un peu plus tôt (`peakAt`, cames plus sages), la compression de 9,5 du démarreur, le rendement indiqué et l'enrichissement à pleine charge de la consommation, la formule d'Anderton d'un diesel suralimenté, le débit d'échappement sous pression. Sans turbine, contrairement à lui : `popForce` n'est pas amorti, les bouffées ne sont pas lissées (`active.turbo` dans `gasPathSample`), rien n'est pris à l'échappement, pas d'huile en plus pour des paliers d'arbre (les rotors ont leur propre huile, hors de ce chiffre). Le réglage de décharge n'a pas de sens : pas de soupape à l'atmosphère, et le réglage disparaît du volet.
- **La consommation** lit le couple : `burnt_.frictionNm` compte l'entraînement, car les pistons le paient. Soupape ouverte, il reste petit : voir le tableau.

| 2,0 L, 4 cylindres, 6800 tr/min, 1 bar | Couple max | Puissance | 0-100 km/h | Vmax | 90 km/h | WLTC |
|---|---|---|---|---|---|---|
| Atmosphérique | 174 Nm à 4100 | 97 kW | 10,2 s | 209 km/h | 6,8 L/100 km | 8,9 L/100 km |
| Turbo moyen | 311 Nm à 3750 | 170 kW | 7,8 s | 254 km/h | 6,4 L/100 km | 8,9 L/100 km |
| Roots | 260 Nm à 3750 | 138 kW | 8,5 s | 237 km/h | 6,6 L/100 km | 9,2 L/100 km |
| À vis | 280 Nm à 3750 | 150 kW | 8,2 s | 244 km/h | 6,6 L/100 km | 9,3 L/100 km |
| Centrifuge | 235 Nm à 5950 | 146 kW | 9,8 s | 242 km/h | 6,4 L/100 km | 9,2 L/100 km |

Les consommations restent les estimations de `ConsumptionMeter` (voir « La consommation »). Une boîte différente, plus longue quand il y a plus de puissance, y joue autant que le compresseur.

### Le son du compresseur

Le son vient de `SuperchargerSound` (`core/src/supercharger_sound.cpp`), avec son propre générateur : aucun tirage du moteur n'est décalé, et sans compresseur rien de cela ne s'exécute.

- **Volumétriques.** Quand une poche s'ouvre sur la sortie, l'air comprimé y rentre d'un coup : c'est une pulsation de débit, à (poches par tour) × (tours par seconde). Trois lobes sur deux rotors font six poches par tour d'un Roots (1412 Hz à 6000 tr/min sur le 2,0 L). Trois lobes sur le rotor mâle font trois poches par tour d'une vis. La phase de l'arbre suit celle du vilebrequin, acyclisme compris. Le niveau est un monopôle `ρ/(4π)·2πf·Q`, avec `Q = V_poche·part·f`, où `part` est ce que la poche a à rattraper : `Δp/p₂` sur un Roots, l'écart à sa pression interne sur une vis, plus un reste (0,2, supposé). D'où un niveau en `f²·Δp` : presque rien soupape ouverte ou au ralenti, et un sifflement qui monte vite plein gaz. La transmission par le carter et le conduit (36 dB, 31 pour une vis) est supposée, sans mesure.
- **Le timbre.** Quatre harmoniques, un souffle étroit autour de la raie, une légère gigue de phase, des raies voisines d'ordre rotor et la variation d'une explosion à l'autre : ce sont elles qui évitent la sirène. Les modes du carter sont fixes (1,7 kHz) : les raies défilent devant.
- **Centrifuge.** Un ordre 2 de l'arbre, dans un souffle étroit, comme le turbo, et le passage des six aubes, qui s'éteint entre 4 et 12 kHz : sans cela, sur un 2,0 L, la raie restait à 12 kHz devant tout. Niveau en `U³` selon le débit, 0,35 Pa à 1 m au nominal (supposé).
- **Sa place.** Le carter d'un volumétrique rayonne sous le capot (`Source::Bay`). La roue d'un centrifuge et le souffle d'entrée (0,5 Pa au débit nominal, en carré du débit, sous 3 kHz) sortent par le conduit d'admission (`Source::Intake`). Le sifflement n'a pas de niveau fixe, et ne passe donc pas devant au ralenti : à 800 tr/min, soupape ouverte, ni la bande 500-2000 Hz ni la bande 8-16 kHz ne bougent de plus de 1,5 dB (test contre-éprouvé).

Mesuré sur `--scenario launch`, de l'avant, à plein gaz : la raie se lit sur le spectrogramme de 500 à 1500 Hz sans changer le bilan des bandes (500-2000 Hz : −31,7 dB contre −31,3 dB sans compresseur ; 8-16 kHz : −55,7 dB contre −54,2 dB). De derrière, elle reste masquée par l'échappement, comme sur une vraie voiture. **Aucune écoute n'a été faite** : le niveau et le timbre restent à juger.

## Ce qui est simulé dans le son

Une première version, qui détruisait toute raie au-dessus de 850 Hz pour éviter la fermeture éclair, était
allée trop loin : sur de vrais moteurs, les raies restent nettes jusqu'à 2 ou 3 kHz, et un test verrouillait
ce défaut. Inspirée du code d'engine-sim et du Sound Design Toolkit, la chaîne actuelle fait passer tout le gaz par la ligne.

1. **Cylindres.** Chacun garde sa pression, différente à chaque cycle et liée au remplissage réel. La soupape
   s'ouvre vite, mais la came décolle en douceur, sur 15° de vilebrequin : un front de 3 ms au ralenti, de 0,4 ms à
   6000 tr/min. Sans cela, chaque explosion faisait un clic dans les médiums au ralenti, là où de vrais moteurs ont un
   tapis continu. Le cylindre se vide en 2 ms environ à pleine charge, bien plus lentement à faible charge, car il
   s'ouvre alors sur peu de pression. Les combustions varient davantage à bas régime.
2. **Vilebrequin.** Il accélère à chaque explosion et ralentit à chaque compression, surtout au ralenti.
3. **Collecteurs.** Une ligne à retard par cylindre, de longueur propre à l'architecture, dont les parois retiennent
   les aigus. Un V8 cross-plane allume à intervalles égaux, mais pas en alternance : deux bouffées de suite dans une
   rangée s'y gênent, d'où le « glouglou ».
4. **Turbine.** Elle prend son énergie aux bouffées, qui en ressortent plus douces et plus faibles.
5. **Turbulence.** Une gigue d'échantillon à échantillon (22 µs) défait les raies progressivement vers les aigus, sans
   coupure, et un bruit proportionnel au débit qui varie s'y ajoute. D'après des mesures sur un V6, il croît très vite
   avec le régime à pleine charge, et reste à peu près stable à charge partielle.
6. **La ligne.** Une réponse impulsionnelle de 4096 points, calculée par matrices de transfert depuis la géométrie :
   tubes, catalyseur, résonateur, silencieux garni, sortie à l'air libre. Elle suit la cylindrée et la longueur de la
   voiture, et tout le gaz la traverse, raies comme souffle ; seuls le bruit de bouche et le grondement la contournent. La pression rayonnée suit la dérivée du débit. Le calcul est
   contrôlé par la formule analytique d'une chambre d'expansion. Convolution partitionnée. Au-dessus de 700 Hz, les
   allers-retours se brouillent peu à peu, et seul le trajet direct reste net : le débit et la température font bouger
   les résonances d'un instant à l'autre. Sans cela, des tronçons de 35 cm forment un peigne de bosses fixes tous les
   680 Hz, entendu comme « un son de canette », et que de vrais moteurs n'ont pas. Voir « L'échappement ».
7. **Le reste.** Admission, grondement sans rythme, souffle mécanique, claquement diesel (large, autour de 1 kHz, sur
   12 ms), vent, pneus (qui perdent leurs aigus au-dessus de 2,2 kHz : sinon, ils couvraient tout, comme une pluie, dès que la voiture roulait), crissement. Le bloc suit les explosions plus qu'un fond continu, et perd ses aigus au-dessus de
   4,5 kHz : plus continu et plus large, il sonnait comme un bruit blanc sous le capot, de 4 à 8 dB au-dessus de vrais
   compartiments. Une
   soupape ne s'entend qu'en retombant sur son siège, et faiblement hors du capot. Plus fortes et sur chaque mouvement
   de soupape, ces salves à niveau fixe passaient devant l'échappement à bas régime, et sonnaient comme « un truc
   métallique qui claque contre le sol ». Le souffle mécanique et l'admission perdent leurs aigus au-dessus de 3 kHz à
   faible charge : à pleine bande et à niveau presque fixe, ils restaient derrière le moteur comme un bruit blanc,
   de 10 à 18 dB au-dessus de vrais ralentis entre 8 et 16 kHz. Ils le gardent sous le capot.
8. **Écoute et niveau.** Toutes ces sources sont en pascals, à 1 m d'elles, et placées sur la voiture. Voir « D'où l'on
   écoute », « L'habitacle » et « Du niveau réel à la sortie ». Un limiteur agit sur le gain, sans déformer le signal.

Limite assumée : la réponse est calculée quand on change de moteur, et ne suit pas un curseur de cylindrée tant qu'on
ne change pas de pièces. Elle ne suit pas non plus la température du gaz.

L'injection ne se coupe que pied levé (papillon sous 3 %, ou charge sous 15 % quand sa position est inconnue), au-dessus de 1,35 fois le ralenti : une croisière à faible charge brûle encore son
carburant. Avec un seuil plus haut, elle perdait 72 % de sa pression, et le son sautait de 8 dB à régime fixe.

Si on ne lui donne pas le remplissage, le son le déduit de la montée du régime : il peut ainsi
fonctionner avec le seul régime, par exemple lu sur un bus CAN.

## L'échappement

Avant, la réponse de la ligne était normalisée, puis un niveau automatique égalisait les moteurs : une ligne directe
sortait 2,4 dB(A) *moins fort* qu'une ligne d'origine, alors que la géométrie seule donnait déjà de 14 à 51 dB de perte
d'insertion. La physique existait, et la normalisation la jetait. La réponse reste maintenant absolue.

**Six lignes, depuis leur géométrie** (`ExhaustLine`, recopiée pour le dessin dans `EXHAUST_LINES`) : origine (catalyseur,
résonateur de Helmholtz accordé sur le bourdonnement, silencieux), sport (silencieux libéré, peu de laine), directe, sans
silencieux, sans résonateur, confort (plusieurs volumes). Les matrices de transfert font tout : aucun gain n'est saisi par
ligne. La bouche suit Levine et Schwinger, à travers l'approximation de Silva et al. (2009) pour |R| et la correction de
bout. L'impédance de source est de 2 Zc : ni rigide (résonances trop marquées) ni anéchoïque.

| 4 cyl. sport, à l'arrêt, 0,5 m | Écart à l'origine | Cible (registres, Japon) |
|---|---:|---:|
| Confort | −10,9 dB | sans cible |
| Sans résonateur | +2,0 dB | +1,5 (0 à +3), faible |
| Sport | +8,9 dB | +7 (+3 à +11) |
| Sans silencieux | +11,9 dB | +11 (+9 à +14) |
| Directe | +21,5 dB | +15 (+10 à +25), faible |

**Une ou deux sorties.** Sur un moteur en V ou à plat d'au moins 4 cylindres, si l'on ne choisit pas une seule sortie, deux lignes sont réellement simulées, chaque rangée
dans la sienne, à moitié de cylindrée et de section. Le pont en H mêle les deux rangées avant les sorties, le pont en X encore plus.
Une ligne qui passe tous les tests de perte par transmission peut sonner comme une canette : le test du peigne porte
sur toutes les lignes.

**Ce que les détonations et le jet doivent à la ligne.** Une détonation sort par la ligne : autant retenue que ses médiums.
Le débit moyen donne la vitesse du gaz en sortie, donc le souffle de bouche (57 dB par décade de régime, Selamet) et, tout près,
le vent du jet : ½ρU² sur l'axe, en U ∝ 6D/z, sur un cône de 5,4° à mi-vitesse, où l'oreille reçoit vent et bouffées.

Un test ne suffit pas à juger ces lignes : à la même charge et au même endroit, aucun enregistrement comparant plusieurs échappements sur une même voiture, au même micro, n'a été trouvé. Les écarts viennent de mesures constructeur japonaises et de registres, les timbres sont à
écouter.

## D'où l'on écoute

Six sources sont placées sur la voiture (`Source`, `carAcousticsOf`) : sorties A et B, admission, compartiment moteur, plancher,
route. Le démarreur est dans le compartiment, le turbo à l'admission, la boîte sous le plancher, au milieu (à l'arrière d'un
moteur arrière). Aucun gain n'est saisi par position : tout découle de ce plan et de `SoundField`, dans cet ordre :

1. **Directivité** de la bouche, selon l'angle et ka (Levine et Schwinger, par un pôle et un zéro par angle), plus l'effet de l'écoulement
   (1 + M cos θ, Masovic). Au-delà de ka ≈ 5, le jet diffuse : le gain sur l'axe plafonne.
2. **La caisse** : le plus court trajet qui la contourne, par le toit ou les côtés, et l'atténuation de Maekawa, adoucie en 10·log(1 + 20 N).
3. **L'air** : ISO 9613-1, à 20 °C et 50 %. Rien de près, 6 dB à 8 kHz à 50 m.
4. **Le retard et 1/r.** Une ligne à retard par source et par oreille : l'effet Doppler et l'arrivée tardive viennent seuls. La pression
   d'un monopôle suit 1/r à toute distance, sans terme de champ proche (Rienstra et Hirschberg, §6.2). Ce qui borne le niveau de près, c'est
   l'étendue de la source : 0,35 fois le rayon pour une sortie, à partir de son centre acoustique, 0,61 rayon en aval de la bouche.
5. **Le sol** : l'image de la source, réfléchie sur un enrobé (Delany et Bazley). Sous 250 Hz, elle reste en phase ; plus haut, une part
   seulement (25 %) reste cohérente, car de vrais enregistrements ne creusent pas de peigne au-delà de 3 dB.
6. **Le lieu** : un peu de réverbération, pour ne pas écouter dans le vide.
7. **La tête** : stéréo par Brown et Duda. Trajet autour d'une sphère de 8,75 cm jusqu'à chaque oreille (donc l'écart de temps), et étagère
   d'ombre α(θ) (donc l'écart de niveau). Pas de HRTF : coût d'une convolution par source et par oreille, données mesurées à embarquer,
   pour un gain sur l'élévation et le devant-derrière, de 3 à 10 dB sur l'ombre derrière la tête. Sans elle, la localisation avant-arrière
   est faible : le casque ne dit pas si la voiture est devant ou derrière.

La tête **glisse** vers sa place (`setTarget`, 6 m/s pour une position toute prête), sans clic. `render` reste mono, au centre de la tête : c'est le
chemin des tests et des empreintes ; `renderStereo` donne les deux oreilles, et le site s'en sert.

**Le capot.** Fermé, le compartiment rayonne par son ouverture avant et par le bas (les aigus restent dedans, −10 dB au-dessus de 3 kHz).
En approchant du bloc (`bayOpen`), la source glisse vers lui et s'ouvre.

Écarts mesurés, ralenti et plein gaz (dB par octave, voir `tests/acoustics_test.cpp` pour ce qui est verrouillé). Les références viennent de vraies
prises, mêmes voitures, mêmes micros : BBC, La Sonothèque, Mercedes 190E, Saturn ION, Volvo S60. Seuls des chiffres en sont tirés : aucune de ces prises n'est dans ce dépôt.

| | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cabine − dehors, ralenti, réel (BBC, n=14) | 0 | −3 | −4 | −8 | −11 | ≤−14 | ≤−10 |
| Citadine, simulée | +5 | −5 | −2 | −10 | −14 | −16 | −17 |
| V8, simulé | +3 | −3 | −1 | −8 | −14 | −15 | −15 |

Hors plage, connu : sous le capot, de 1 à 8 kHz, encore 4 à 6 dB au-dessus du gabarit, et les graves y manquent (A−Z de −0,2 pour −3,5) ; 4 cylindres
en sport, ralenti : 6 dB de trop à 125 Hz, 6 dB de moins à 500 Hz. Le comparateur perceptif (`tools/perceptual_compare.py`) le dit aussi.

## L'habitacle

Trois groupes de sources (échappement, moteur, route et plancher) traversent chacun un filtre à phase minimale de 1024 points, calculé par cepstre à l'application
d'une voiture, puis convolué. Ce filtre vient d'un bilan d'énergie sur dix parois (plancher, toit, deux portes, deux vitres, cloison, paroi arrière, pare-brise, vitre arrière), chacune
avec sa loi de masse, sa fréquence de coïncidence pour le verre et sa part qui fuit (`cabinTransfer`). De près, chaque source voit plus fortement la paroi
qu'elle regarde. Les supports moteur font entrer les bas ordres directement, sans passer par l'air (`structureGain`). La cabine y ajoute ses modes et sa
courte réverbération.

| Insonorisation | Tôle | Vitres | Cloison | Fuites | Citadine, ralenti | Citadine, plein gaz |
|---|---:|---:|---:|---:|---:|---:|
| Course | 4 kg/m² | 4,8 | 6,5 | 0,3 % | 52,1 | 85,0 |
| Sportive | 5,5 | 8 | 9 | 0,08 % | 38,8 | 74,4 |
| Normale | 7 | 10 | 12 | 0,025 % | 33,9 | 69,8 |
| Confort | 10 | 13 | 17 | 0,008 % | 30,5 | 66,3 |
| Luxe | 15 | 20 | 26 | 0,002 % | 26,5 | 62,1 |

Les vitres ouvertes transforment la vitre du conducteur en trou (30 % de fuite) et font monter le bruit de route, par les passages de roues : +8,7 dB(A) sur une balade, réel +5 en route.

**L'emplacement du moteur** (`placement`) change la place du bloc et de l'admission, celle du siège et de la cloison, la longueur de la ligne (environ 0,4 fois en central, 0,25 fois
en arrière), et la part de la masse sur l'avant : ces effets sont liés. Dans l'habitacle, le bloc et l'admission gagnent de 3 à 5 dB derrière les sièges. **À plein gaz, sur une ligne
libre, la sortie reste devant et masque cet écart :** à niveau total, moteur central et moteur avant se valent. Avec une ligne confort : avant 67,9, central 71,6, arrière 73,4.

Absent : la ventilation. De vrais ralentis de citadine et de berline sont de 3 à 6 dB plus forts que le modèle, et un ventilateur au ralenti peut ajouter 15 dB(A) sur une Volvo 240.

## Du niveau réel à la sortie

Le sonomètre (`LevelMeter`) lit la pression avant le volume : filtre A de la norme IEC 61672 (classe 1 aux taux courants, le pôle aigu corrigé de la déformation de la transformée
bilinéaire), constante de temps « fast » de 125 ms. C'est le chiffre affiché : **une estimation**, qu'aucun micro n'a contrôlée.

Une oreille à 118 dB(A) et un haut-parleur à 70 dB ne peuvent rien avoir en commun : `toneGain` comprime le niveau réel, par morceaux, vers un niveau de sortie. Il monte de moitié
environ : 40 dB(A) vers −42 dBFS, 80 vers −22,5, 100 vers −15, 120 vers −13. Attaque en 1 s, retombée en 2 s, 0,04 s quand la tête se déplace : un coup de gaz garde son contraste, le
gain ne le rattrape pas. Le volume reste à part. Un plafond d'efficace à 0,25, puis le limiteur, gardent la forme d'onde : en montée, son gain moyen est de 1,000 sur tous les moteurs
sauf le V10 à ligne directe (0,97, 117 dB(A) derrière). Avant, il agissait en continu à pleine charge (0,35 à 0,40).

**Lois** : la charge agit surtout sur l'échappement, comme sur de vraies prises (Saturn : +15 à +22 dB au-dessus de 500 Hz au micro d'échappement, rien au micro moteur). Le bloc ne perd que 5 dB à vide, 2 dB sur un diesel.

| Loi | Cible | Mesuré |
|---|---:|---:|
| Bloc, dB(A) à 1 m, pleine charge | Anderton, 50 log N + 60 log B − 203 (essence ; les diesels ont leurs formules) | ±2 dB dès 3750 tr/min, jusqu'à −5 à 2000 |
| Échappement à vide | 10 dB par doublement du régime | 10,0 |
| Pleine charge − à vide | +12 (+10 à +15) | +13,9 |
| Ralenti − essai à l'arrêt | −25 (±6) | −20,8 |
| Route à 7,5 m, 130 km/h | CNOSSOS : 72 + 34 lg (v/70) | 83,9 pour 81,1 |

`exhaustLaw`, une pente de 3,6 dB par octave, est le seul réglage de cette colonne, avec un facteur 0,5 sur le filtre des diesels : la synthèse donne déjà presque tout, il lui manquait cette pente.

| Préréglage | Essai à l'arrêt (R51.03, 0,5 m) | Cible | Habitacle, ralenti | Cible | Habitacle, plein gaz | Cible |
|---|---:|---:|---:|---:|---:|---:|
| Citadine 1.4 | 76 | 78 ±4 | 34 | 40-46 | 70 | 72-76 |
| 4 cyl. sport | 89,5 | 86 ±4 | 43 | 45-56 | 79,5 | 82-98 |
| GTI turbo | 88,5 | 88 ±6 | 42,5 | 41-52 | 82 | 78-85 |
| Diesel | 73 | 74 ±4 | 41,5 | 43-50 | 72,5 | 68-75 |
| 6 en ligne | 93 | 87-93 ±3 | 48 | 45-56 | 82 | 82-98 |
| V8 | 93 | 93 ±4 | 48,5 | 45-56 | 84 | 82-98 |
| Flat 6 | 93 | 96 ±3 | 49 | 45-56 | 86,5 | 82-98 |
| V12 | 102 | 108 ±4 | 48 | 52-72 | 88,5 | 88-98 |
| V10, ligne directe, course | 108 | 112 ±6 | 68 | 56-61 | 107,5 | 98-108 |

Ces cibles viennent de la base néerlandaise RDW (cartes grises, valeurs U.1 et U.3) et d'*auto motor und sport*. Elles disent qu'un niveau est plausible, pas qu'un son est juste.

**Ces chiffres ne disent pas si ces positions et ces lignes sonnent juste : l'oreille tranche.**

## Le démarrage

Cinq essais ont été écartés à l'oreille : un sifflement et du souffle, « un aspirateur » ; des sinusoïdes
pures, « un vaisseau extraterrestre » ; des chocs aléatoires sur des résonances molles, « des cailloux dans
une boîte en plastique » ; un frottement sur des résonances très fines, « un ballon en latex » ; une note
forte qui glisse à volume constant, « un DJ qui fait drifter un CD ». Tous avaient un rythme et un spectre
moyen justes : ces mesures ne suffisent pas.

Le modèle est maintenant calé sur neuf vrais démarrages : une Mazda MX-5, une Shelby GT500 et sept prises
rapprochées de [La Sonothèque](https://lasonotheque.org/search?q=demarrage) (dont Peugeot 106 et 107, Citroën
Saxo, Ford Courrier, Kia Carnival et Iveco Daily). Des sons générés par ElevenLabs ont servi de références
complémentaires, pour le V8 notamment : ils guident à l'oreille, sans valoir une mesure. Rien de tout cela
n'est dans ce dépôt, et le son du site reste entièrement simulé. `tools/analyze_starter.py` refait une partie de ces mesures (raie d'engrènement, rythme, contraste) sur vos propres enregistrements.

Ce que ces enregistrements ont appris :

- **C'est surtout du bruit façonné, pas des raies.** Au-dessus de 800 Hz, les raies des neuf références ne se
  distinguent pas d'un témoin à phases aléatoires. Blanchi, le résidu est gaussien : ni trains d'impulsions ni
  peigne, et un seul pic, vers l'engrènement, sous 800 Hz.
- **Une structure fine fixe.** Des bosses de 40 à 100 Hz de large et de 2 à 5 dB, qui ne bougent pas pendant
  le lancement (corrélation de 0,5 à 0,97 entre deux moitiés de prise, sur sept références sur neuf). Une partie
  vient peut-être de la pièce où l'on a enregistré.
- **L'attaque.** Un précurseur de -22 à -35 dB, 8 à 20 ms de montée, un sommet de 10 à 15 dB au-dessus du
  lancement (15 à 25 dB dans l'aigu), retombé en 30 à 60 ms, avec plusieurs pics. Sur un diesel, le sommet
  n'est qu'à 4 à 7 dB.
- **Deux sources distinctes.** Entre deux compressions d'un diesel, les médiums tombent de 12 à 19 dB, et les
  aigus de 3 à 7 dB seulement : le contact des dents suit le couple, les balais suivent le courant.

Deux hypothèses de la littérature n'ont pas été retenues, faute de les retrouver dans ces prises : la roue libre
à deux inerties, et une résonance plus amortie sous couple.

Ce qui est simulé (`StarterSound`) :

- **Le démarreur dépend du moteur.** À réducteur (petit moteur rapide, train planétaire, denture inclinée) ou à
  entraînement direct (gros moteur lent, denture droite, plus rugueux) ; puissance, couronne et pignon suivent
  la cylindrée.
- **Le vilebrequin.** Moteur à courant continu contre compressions calculées d'après la géométrie, avec des gaz
  qui perdent de la chaleur et des cylindres inégaux : le régime et son balancement sortent de cet équilibre, et
  le modèle transmet au son le couple qui passe vraiment par les dents. Quand ce couple s'annule, les dents se
  séparent, puis reprennent contact.
- **Le bruit de contact et les balais.** Le premier suit le couple, avec un coude à 2 kHz ; les seconds suivent
  le courant. Une raie d'engrènement faible et intermittente sort à peine du bruit.
- **La structure.** Tout passe par une réponse dense, propre à chaque moteur et convoluée : elle donne les bosses
  fixes et l'impression de carter.
- **La séquence.** Plusieurs chocs : relais, plongeur, pignon, reprises de contact. Puis le moteur prend, le pignon
  sort, et le démarreur retombe discrètement.
- **Le moteur prend.** Le calculateur enrichit : il part même très bas, monte à 1,45 fois son ralenti, qui
  redescend en une seconde environ.

Le calage est automatique (CMA-ES borné sur neuf démarrages, vérifié sur cinq autres), retouché à la main sur
l'attaque, le V8 et le diesel. Limites connues : il manque des graves sous 400 Hz au V8, sans doute l'air pompé
dans l'échappement, et les arcs mobiles d'une des prises vers 2,6 kHz ne sont pas reproduits. Le V8 repose sur
très peu de prises, et le diesel de 2 L sur des prises de 3 L. Le son étant surtout du bruit, son risque principal
reste « l'aspirateur », que seule l'oreille peut écarter.

## La course

La course n'est pas un réglage stocké : `V = π/4 × alésage² × course × cylindres` la déduit de la cylindrée et de l'alésage, qui restent ce que le lien de partage garde. Le curseur est une autre façon d'écrire la cylindrée, comme sur un vrai moteur : un vilebrequin plus long à alésage constant, un « stroker ». Quand il bouge, l'alésage reste et la cylindrée s'ajuste, arrondie à l'entier ; quand la cylindrée, l'alésage ou le nombre de cylindres bougent, il suit. `web/src/lib/stroke.ts` fait le calcul, `--stroke` de la CLI aussi (`displacementForStroke`).

**Bornes.** De 40 mm (un V8 de Formule 1 de 2,4 L, 98 mm d'alésage, soit 39,8 mm) à 130 mm (Bugatti Royale, 125 × 130), resserrées pour que la cylindrée reste entre 600 et 7000 cm³. Le pas de 0,5 mm garde l'aller-retour exact : sur le plus petit cylindre, l'arrondi de la cylindrée à l'entier déplace la course de moins de 0,09 mm, moins que le demi-pas. Revenir au cran de départ rend la cylindrée de départ, celle d'un préréglage comprise. Hors de ces bornes, un préréglage, un lien ou les deux autres curseurs restent permis : le texte dit la vraie course, le curseur reste en butée, et rien n'est réécrit tant qu'on n'y touche pas. Les vues dessinent à l'échelle tout ce que le curseur atteint, de 0,36 à 2,2 alésages.

**Ce que la course change vraiment.**

- *Le couple*, seulement par la vitesse moyenne du piston : au-delà de 24 m/s, `breathing` perd `0,012 × excès²`, sans descendre sous 0,55. C'est un bon indicateur : le débit par les soupapes, dont la section suit l'alésage, se lit sur la vitesse du piston (indice de Mach de Taylor). Un V8 de Formule 1 de 39,8 mm à 18 000 tr/min fait 23,9 m/s. Sous cette limite, à cylindrée égale, la courbe ne bouge pas.
- *Le son*, par deux chemins qui bougent ensemble à cylindrée constante, séparés en retirant chacun à son tour. La course règle la durée de la bouffée (`blowdownBase`, `√course`, à volume rejeté égal) : derrière la voiture, plein gaz, le niveau perd 1,9 dB de 69 à 110 mm, et ce chemin retiré il ne bouge plus (89,8 à 89,9 dB(A)) ; au bout du tube, la bande 8-16 kHz gagne 1,9 dB. L'alésage règle le bruit du bloc (loi d'Anderton, `60 log10 alésage`) : dans le compartiment, 108,0 dB(A) à 96 mm d'alésage, 102,2 à 76, et 105,2 partout s'il est retiré. Le débit moyen et la ligne, eux, suivent la cylindrée. Plus d'aigus pour une bouffée plus longue n'est pas le sens attendu : il découle du modèle, sans avoir été confronté à de vrais moteurs.
- *Le dessin et le gaz* : la manivelle, la bielle (1,6 course), la hauteur du bloc, le volume de chaque cylindre et la section du piston.
- *La cylindrée*, si l'alésage reste : tout ce qui en découle.

**À 2 L et 4 cylindres, 6800 tr/min** (carrosserie compacte, ligne sport) :

| | Super-carré, 96 × 69,1 | Carré, 86 × 86,1 | Longue course, 76 × 110,2 |
|---|---|---|---|
| Couple max | 173,6 Nm à 4100 tr/min | identique | identique |
| Puissance max | 132 ch à 6250 tr/min | identique | identique |
| Vitesse du piston au rupteur | 15,7 m/s | 19,5 m/s | 25,0 m/s |
| WLTC | 8,85 L/100 km | identique | identique |
| 90 km/h | 6,70 L/100 km | identique | identique |
| LAeq, plein gaz | 90,9 dB(A) | 90,0 dB(A) | 89,0 dB(A) |
| Bande 8-16 kHz, plein gaz | 17,1 dB | 17,9 dB | 19,0 dB |
| LAeq, compartiment moteur | 108,0 dB(A) | 105,2 dB(A) | 102,2 dB(A) |

À 8500 tr/min, la longue course atteint 31,2 m/s : 182 ch à 6750 tr/min au lieu de 191 ch à 7800.

**Ce que la course ne change pas, et pourquoi.** Trois effets réels manquent, et les ajouter déplacerait toutes les empreintes, puisque tout moteur a une course : le frottement (`frictionTorqueNm` suit le régime, pas la vitesse du piston), l'inertie (`engineInertia` suit la cylindrée, pas le rayon de manivelle ni la masse du piston) et le régime du pic de couple (une part fixe du régime maximal). La consommation à cylindrée égale ne dépend donc pas de la course. Ajouter le seul frottement à la consommation ne serait pas plus juste : une longue course frotte plus, mais perd moins de chaleur par ses parois et brûle plus vite (Filipi et Assanis, *International Journal of Engine Research* 1(2), 2000, lu seulement à travers des résumés de seconde main). Le modèle n'a ni l'un ni l'autre, et n'en montrer qu'un donnerait le mauvais sens : ce bilan reste à faire. Le plancher de 0,55 reste un choix de modèle : au-delà de 30 m/s, aucun moteur réel ne donne de référence.

## La boîte

Une pièce qu'on choisit et qu'on règle (`GearboxConfig`), indépendante du moteur : on peut monter la même sur deux
moteurs, y compris une boîte mal adaptée. Une valeur nulle laisse le constructeur décider : le pont et le couple
admissible s'adaptent alors à la voiture et au moteur, ce que font les préréglages. Un pont et un couple choisis, et plus
rien ne dépend du moteur.

**Des dents, pas des rapports.** Chaque paire a des nombres de dents entiers, premiers entre eux si possible, à entraxe
à peu près constant. Rapports, vitesses, fréquences d'engrènement en découlent. L'entraxe suit le couple admissible
(70 mm pour 300 Nm, au cube), le module fixe le nombre de dents. Un module trop petit pour le couple demandé abaisse
ce que la boîte admet : la dent du pignon de première casse la première (Lewis). Un pont de traction est une paire
hélicoïdale ; en propulsion ou en intégrale, la boîte est longitudinale (une paire en prise constante, une prise directe
si un rapport tombe à 1) et le pont un couple conique.

**Boîte mal adaptée :** trop courte, le rupteur arrive avant la vitesse de pointe ; trop longue, ça se traîne et ça cale
en manuelle. Au-delà de ce qu'elle admet, elle s'use selon la règle de Miner, en puissance sixième du couple : 30 s juste au-dessus de sa limite, environ une demi-seconde à deux fois sa limite. Puis une dent cède.

**Les types.** Synchros : le cône met les arbres à la même vitesse, T = µFR/sin α (Socin et Walters), en un temps J·Δω/T.
C'est donc plus long sur un grand écart. Crabots : pas de cône, les crabots tombent tant que l'écart le permet, et le choc
vaut ½J·Δω². Séquentielle : barillet, moins de 100 ms, allumage coupé à la montée, pied au plancher ; l'embrayage reste
fermé et patine sous le choc. Double embrayage : le rapport suivant est déjà engagé, le couple passe d'un disque à l'autre
sans trou. Automatique : convertisseur de couple, qui multiplie par deux au décollage, puis se ponte. Électrique : un
réducteur à deux étages.

**Un passage a des étapes :** débrayage, sortie du rapport (impossible sous charge, les dents sont plaquées), course,
synchronisation, engagement, rembrayage. Assisté, le levier actionne l'embrayage. Au pied, passer sans débrayer gratte à
la descente, car le cône ne freine pas tout un moteur ; à la montée, il suffit d'attendre que le régime tombe, comme un
double débrayage. Un coup de gaz au point mort, moteur embrayé, remonte le primaire : c'est lui, le double débrayage.

**Plus de minuterie à la place de cette pièce.** L'arbre primaire est une vraie inertie : disque, arbre, pignons. Au
point mort, il tourne avec le moteur si l'embrayage est fermé, ce qui est le cas pédale relevée. Les arbres de roue se
tordent (6000 Nm/rad) : le mode propre va de 2 à 3 Hz en première à une dizaine de hertz en haut de boîte, et donne les
à-coups quand on remet les gaz. Le rendement vient des étages (hélicoïdal 98,5 %, droit 99 %, conique 95,5 %) et d'une
traînée selon la vitesse.

### Son bruit

Rien n'est joué : tout est une force sur les roulements, qui traverse une seule réponse, fixe, celle du carter. Plus de
99 % de ce bruit passe par la structure (Rust, Brandl et Thien, 1992), et les raies défilent devant ses résonances. Le
son ne lit jamais le régime du moteur pour faire siffler les dents : seulement les vitesses des arbres et le couple. Il a
son propre générateur, et sans `setGearbox` il reste muet et ne coûte rien.

- **Le sifflement** est l'erreur de transmission des paires chargées, dent par dent. La profondeur de chaque harmonique
  vient des rapports de conduite : 2 Sinc(k εα) Sinc(k εβ) (Velex, 2012), plus un reste dû aux défauts, car un recouvrement
  entier n'efface rien sur de vraies dents. Avec le couple, elle passe par un minimum là où le profil est corrigé, comme
  sur le banc de la NASA (TM-105331), et elle dépend du flanc : le flanc de retenue est moins soigné. Hors charge, elle
  s'éteint. Sur une boîte de route, les raies restent discrètes ; en denture droite, on les entend nettement : 16 dB
  d'écart en moyenne en régime tenu, de 7 à 22 dB selon la charge. La NASA mesure 7 à 17 dB, 22 au mieux (TM-106511).
- **Ce n'est pas une sinusoïde.** Mesuré sur de vraies marches arrière (Peugeot 106, Saxo, Golf), en denture droite même sur
  une voiture de route : une raie bat de 2,6 à 4,5 dB d'une fenêtre de 46 ms à l'autre, et disparaît parfois une seconde
  entière ; autant d'énergie dans une jupe de ±100 Hz que dans son pic ; les harmoniques baissent d'environ 3 dB chacun.
  D'où un faux-rond par tour et un défaut par dent, qui reviennent tous les pignon × roue dents, une jupe et un
  balancement sur chaque harmonique, une vitesse jamais tenue à mieux que quelques centimètres par seconde.
- **Le claquement** n'est pas déclenché : les roues folles sont simulées dans leur jeu, secouées par l'acyclisme du primaire.
  Il naît au-dessus du seuil de Rust, J·α > traînée, et se tait dès qu'on débraye, car plus rien ne secoue le primaire. Un
  V12 tourne trop rond pour cela, un diesel à volant simple claque, un double volant l'efface, un convertisseur aussi.
  Les chocs tombent en phase avec les explosions.
- **Les passages :** le cône qui frotte, le baladeur (deux chocs, la double bosse), le choc d'engagement, la reprise de jeu quand
  le couple change de sens, bien plus forte sur de larges crabots. Quand le baladeur force, une dent frappe à chaque passage,
  irrégulièrement, à la fréquence de l'écart de vitesses, et le levier rebondit vers 120 Hz : mesuré sur un vrai
  grattement, comme les bosses du carter vers 700 Hz, 2,1, 4 et 6 kHz.
- **La marche arrière** est en denture droite et sans correction dans le simulateur, comme sur beaucoup de voitures : ses défauts dominent même à
  faible charge, ce qui fait d'elle le bruit de boîte le plus fort d'une voiture de route.
- **Le reste :** l'huile et les roulements, l'embrayage qui patine, la butée pédale enfoncée.

Limites assumées. Le module et l'angle d'hélice réels des boîtes de série sont introuvables : ils sont supposés. Aucun
enregistrement isolé de claquement au ralenti n'a été trouvé : sa forme vient de la littérature (une bouffée à chaque
explosion, au-dessus de 1 kHz), et d'un ralenti de voiture de rallye dont les aigus battent à la cadence d'allumage. Le
son ne suit pas la température de l'huile, ni l'usure, et les boîtes automatiques n'ont pas de train épicycloïdal : leurs
raies sont celles d'une boîte à arbres parallèles. **Les mesures disent que la structure est celle d'un vrai bruit de boîte, pas que
cela sonne juste : l'oreille tranche.**

## Les modes de la boîte automatique

Cinq modes (`ShiftMode`, `core/src/shift_logic.cpp`). Aucun régime n'est écrit par voiture : tous les points viennent de `ShiftMap`, calculé à chaque changement de moteur ou de voiture à partir de la courbe de couple, du couple et de la puissance maximaux, et de l'écart entre les rapports. **Normal reste la logique d'origine, au bit près** sur route plate : les empreintes `golden_lap_*` n'ont pas bougé à cause de lui.

| | Point de montée, pied léger | À fond | Rétrograder | Autre |
|---|---|---|---|---|
| **Éco** | juste assez pour retomber au-dessus de son plancher | entre le couple max et la puissance max (35 %) | kick-down à 96 % de pédale ; au frein, sous 30 % du régime max | monte aussi au lever de pied ; pontage dès 84 % de glissement |
| **Normal** | `max(2,6 × ralenti, 0,38 × max)` | 96,5 % du max | kick-down à 85 % sous 42 % du max | d'origine |
| **Sport** | au-dessus du plancher (55 % du régime de couple max), multiplié par l'écart de rapports | le croisement des forces aux roues, entre 97,9 et 98,5 % du max | kick-down à 65 % ; au frein dès que le moteur reste sous 75 % ; coup de gaz | ne monte pas en virage ; garde 4 s la mémoire du pied |
| **Piste** | 92 % au plus de son point à fond | 99,5 % du max, pratiquement au rupteur | kick-down à 45 % ; au frein sous 88 % | ne monte pas en virage ; garde 10 s la mémoire du pied ; passages plus courts ; pontage à pleine charge |
| **Palettes** | jamais de lui-même | au rupteur (99,8 %), pour protéger le moteur | sous son plancher, pour ne pas caler | le conducteur décide ; ce qui ferait sur-régimer est refusé par `requestShift` ; le pilote automatique ne s'en sert pas et conduit en normal |

À même pédale, Éco < Normal < Sport ≤ Piste à toute demande et sur tous les rapports (`upshiftPoint`, vérifié sur tous les préréglages). La **demande** est la position de la pédale ; en sport et en piste s'y ajoutent sa vitesse (une pédale qui s'enfonce vite compte d'avance) et sa mémoire (lever le pied un instant ne fait pas repasser en douceur). Le freinage et le virage la remontent : le rapport reste pour la sortie.

**Le croisement** est le régime où le rapport suivant tire autant aux roues que celui-ci, calculé sur la courbe. Il sert au sport tant qu'il reste sous le rupteur, mais sur ces courbes il ne tombe sous le régime maximal que sur les hauts rapports du V10 : presque partout, tirer jusqu'au bout est ce qui pousse le plus. Le sport passe donc au-delà de la puissance maximale, pas à elle. Et à fond le mode normal va déjà à 96,5 % : c'est à mi-pédale que les modes diffèrent le plus, pas au plancher, où Normal, Sport et Piste sont à une centaine de tours les uns des autres. Mesuré, régime au départ du passage de 1re en 2e, depuis l'arrêt :

| Moteur | Pédale | Éco | Normal | Sport | Piste |
|---|---|---|---|---|---|
| Citadine 1.4 | 60 % | 2890 | 3664 | 5393 | 6059 |
| | 100 % | 4401 | 5985 | 6109 | 6171 |
| 6 cylindres (8000 tr/min) | 60 % | 3495 | 4728 | 6846 | 7832 |
| | 100 % | 5817 | 7725 | 7883 | 7964 |
| Diesel 4 cylindres (4500 tr/min) | 60 % | 2451 | 2987 | 3898 | 4401 |
| | 100 % | 3165 | 4345 | 4435 | 4480 |

**Pour ne pas hésiter :** on ne monte que si le moteur retombe au-dessus du plancher (2 % de marge), et si le rapport suivant tire, 10 % sous le régime d'arrivée, une part de la résistance de la route choisie pour ne pas retomber sous le seuil de kick-down (1,25 à 2,6 fois). On ne rétrograde pour accélérer que si le moteur reste à moins de 88 % du point de remontée du rapport inférieur et si les roues y gagnent, en tenant compte de ce que les pneus peuvent transmettre : un rapport de moins sur des roues qui patinent n'apporte rien. Juste après un passage, on ne revient en arrière que si la voiture n'avance vraiment plus. Tout cela est réservé aux modes autres que Normal, et à Normal dès que la route n'est plus plate (pente, remorque) : sur le plat, sa logique d'origine ne connaît pas ces gardes, et peut encore hésiter un instant 1re-2e à très petite pédale.

**La durée des passages** reste celle de la boîte, avec une exception assumée : le sport (0,85) et la piste (0,7) raccourcissent l'embrayage piloté et le relais, et coupent davantage le couple pendant la montée d'une boîte à double embrayage ou à convertisseur. C'est ce que font les vrais calculateurs : plus de pression hydraulique, embrayage plus franc, au prix d'un passage plus dur. La séquentielle n'y gagne rien : son barillet a sa propre durée. Une boîte manuelle n'est jamais touchée.

**Le pontage du convertisseur** dépend du mode : à partir de 84 % de vitesse turbine/pompe en éco (90 % ailleurs), et jusqu'à pleine charge en sport et en piste, à partir de la première en piste.

**Conséquences :** le 0 à 100 est toujours aussi bon en sport qu'en éco (vérifié sur tous les préréglages et toutes les boîtes) ; sur un trajet de ville, l'éco consomme moins que le sport (`consumption()`).

## Les trajets du pilote automatique

`step` suit un trajet (`DriveScenario`, `core/src/driver_scenarios.cpp`) : une liste de données, sans logique. Une étape donne son début, sa vitesse visée, la pédale au plus, la pente, le rayon du virage et de quoi la nommer. Le trajet a aussi une façon de conduire (vitesses de pédale, gain, marge et force de freinage, anticipation, accélération latérale acceptée) et peut tirer une remorque. `City`, le premier, est la boucle d'origine, valeurs inchangées : ses empreintes n'ont pas bougé.

| Trajet | Ce qu'on y entend |
|---|---|
| `city` | ville, feu, voie rapide, dépassement : la boucle d'origine |
| `stop_go` | bouchon : première et seconde, très peu de gaz, jamais plus de 40 km/h |
| `country` | lignes droites à 90, virages de 45 à 150 m, petites côtes |
| `motorway` | bretelle à fond, croisière à 130, dépassement avec kick-down, faux plat, sortie |
| `mountain` | 8 à 10 % à la montée, épingles de 18 m, puis la descente |
| `drag` | 16 s à pleine charge depuis l'arrêt, gros freinage, retour au pas |
| `track` | pleine charge, gros freinages, virages à 0,95 g ; ne repasse pas par l'arrêt à chaque tour |
| `hill_start` | à l'arrêt sur 12 puis 16 %, on repart |
| `towing` | 1500 kg de plus à tirer, côte à 5 %, descente |

**Ce que la voiture sent :**

- **La pente** ajoute `m g sin θ` à la résistance, et charge l'arrière comme une accélération : une propulsion y patine moins qu'une traction. Elle change progressivement, de 2 % par seconde.
- **Le virage** limite la vitesse à `√(a R)`, avec l'accélération latérale `a` du conducteur, et coûte 5 % de la force latérale (dérive des pneus). Il fait aussi tenir leur rapport aux modes sportifs.
- **La remorque** ajoute sa masse à accélérer et à freiner (elle a ses propres freins), ses pneus et son air.

**Le conducteur** garde le calcul d'origine (pédale pour tenir la vitesse, plus l'écart à la consigne), et y ajoute sur les nouveaux trajets l'anticipation : la vitesse à ne pas dépasser maintenant pour arriver à celle de l'étape suivante, à la décélération voulue. C'est ce qui le fait freiner avant le virage. `underpowered()` dit quand, pied au plancher, la voiture n'atteint pas la consigne ; le site l'affiche.

**Changer de trajet en roulant** garde la vitesse : on prend le nouveau trajet à sa première étape qui roule, et la pente arrive progressivement.

**Ce que le modèle ne sait pas faire :** la voiture ne recule pas (la vitesse reste positive), donc un démarrage en côte suppose une aide au démarrage. Il n'y a qu'une seule adhérence : ni largeur de pneu, ni appui, ni transfert de charge latéral. Le virage n'est donc pas plus rapide sur une voiture plus large. La trajectoire et la position sur la route n'existent pas : une étape dure un temps, pas une distance, et une voiture faible n'arrive simplement pas à sa vitesse avant la fin de l'étape. Les modes reposent sur des courbes à l'équilibre : le retard du turbo n'entre pas dans le calcul des points.

## Le calage

En boîte manuelle, rien ne protège le moteur. Sous le ralenti, la combustion devient instable et le couple
s'effondre. Trop bas en prise, chaque explosion donne un à-coup : ça broute. Sous 68 % du ralenti, si
l'embrayage relie le moteur aux roues, il cale : plus de combustion, et il faut relancer le démarreur.
Enfoncer l'embrayage l'évite. Seule exception, pour pouvoir partir au clavier : en première, l'embrayage
reste assisté tant qu'il n'a pas collé. L'embrayage piloté d'une boîte automatique s'ouvre à temps.

## Le rupteur

Un test le protège : sans la coupure, le son ne varie que de 4 à 6 dB et ressemble à une note tenue.

- **Coupure franche :** la pression tombe de 95 % à l'explosion suivante, sans lissage, et ce qui suit
  le débit tombe avec elle : pompage, souffle, admission, grondement. Le son bégaie avec 15 à 20 dB de contraste.
- **Jamais deux rebonds identiques :** le calculateur reprend à une explosion près.
- **Pas de décharge du turbo :** le papillon reste ouvert, il n'y a pas de vide pour ouvrir la soupape. Le test le vérifie sur le modèle.
- **Détonations :** du carburant arrive encore pendant la coupure, et peut s'enflammer à la reprise.

## Le lever de pied

- **Coupure d'injection :** la pression d'échappement chute, le moteur n'est plus qu'une pompe.
- **Détonations :** du carburant imbrûlé s'enflamme dans le tube, surtout juste après le
  lever de pied, jamais au ralenti, sous charge ni sur un diesel. C'est une onde qui fait
  résonner le tube selon sa longueur et la cylindrée. Force, brillance, durée et fréquence
  dépendent de l'échappement. Un premier essai en claquements de bruit blanc sonnait comme
  « un caillou sur le fond de caisse ».
- **Turbo :** la soupape de décharge s'ouvre sous le vide du collecteur et vide le conduit ; sans elle, l'air revient par le compresseur, par à-coups (flutter). L'arbre garde son élan : son sifflement redescend lentement, plus vite si la soupape le laisse encore pomper de l'air. Voir « Le turbo ».

## La consommation

Essence, huile ou énergie sont une **lecture** de ce que fait le moteur : elles s'intègrent à chaque pas de 1 ms de
`DrivingModel` (`core/src/consumption.cpp`) et rien n'en revient vers la physique. La masse ne baisse pas avec le carburant,
le son ne change pas : les empreintes sont les mêmes avec et sans le compteur, vérifié en le retirant sur une copie.

### La méthode : un bilan d'énergie

```
puissance du carburant = (couple au frein + frottements + pompage derrière le papillon) × ω × enrichissement / rendement indiqué
```

- **Couple au frein et frottements :** ceux du modèle, à chaque pas.
- **Pompage :** il vient d'un écart de pression, donc jamais plus d'une atmosphère derrière un papillon fermé, soit 1 bar de
  pression moyenne au plus. Il dépend de la pression du collecteur, elle-même du travail demandé : une droite, résolue
  directement. Au-dessus de la pleine charge atmosphérique, plus de papillon. Un diesel n'en a pas.
- **Rendement indiqué :** 0,405 en essence atmosphérique, 0,375 en turbo (moins de compression), 0,46 en diesel. Il perd jusqu'à
  8 % sous 2000 tr/min (le gaz cède sa chaleur aux parois) et jusqu'à 22 % à faible remplissage (10 % en diesel).
- **Enrichissement :** à partir de 85 % de la pleine charge, jusqu'à λ = 0,88 en atmosphérique et 0,82 en turbo, d'autant plus que la
  puissance est élevée. Le diesel reste pauvre.
- **Carburant :** essence 0,745 kg/L, 42,9 MJ/kg, 3,17 g de CO₂ par gramme ; diesel 0,832 kg/L, 43,1 MJ/kg, 3,15 g/g. Valeurs courantes,
  citées de mémoire et non relues dans le rapport JEC que je cherchais ; seul le diesel d'une table de l'EPA irlandaise a été vu
  (43,31 GJ/t, 73,3 t de CO₂/TJ). Contrôle : Ford annonce 268 g/km pour 11,8 L/100 km, soit 2,27 kg/L, contre 2,36 kg/L ici.

Cela fait émerger sans réglage : zéro au rupteur, contact coupé, calé, ou pied levé en prise (le couple est aussi négatif que
celui d'un moteur entraîné), un ralenti non nul, une pleine charge plus chère que le meilleur point, une faible charge bien plus
chère encore, un gros moteur qui dépense plus à la même vitesse, un rapport court qui dépense plus, et un embrayage ou des pneus
qui patinent et brûlent pour rien. **Le moteur froid n'est pas simulé :** aucune température ne le justifie dans le modèle.

### Pourquoi pas le débit d'air, et ce qu'il confirme

La seconde méthode part de la cylindrée, du régime et du remplissage. Elle sert de vérification (`lambdaAverage`, affiché par
`--fuel 1`). Le couple du modèle étant proportionnel au remplissage, elle revient à diviser par un rendement la combustion
`remplissage × pleine charge`.

Elle n'a pas été retenue, car ce remplissage porte un **pompage de 3,2 bar** (`pumpingTorqueNm`) : plus que ce qu'un écart
de pression peut donner, parce qu'il règle aussi le frein moteur. À faible charge, le modèle ouvre donc plus le papillon qu'un vrai moteur.
Si les deux méthodes étaient d'accord, λ vaudrait 1 en essence. On lit :

| | Ralenti | 90 km/h | 120 km/h | WLTC |
|---|---|---|---|---|
| Citadine 1.4 | 0,51 | 1,16 | 1,12 | 1,13 |
| 2.0 atmosphérique | 0,43 | 1,01 | 0,96 | 1,01 |
| 2.0 turbo | 0,45 | 1,05 | 1,03 | 1,03 |
| V8 5.0 | 0,48 | 1,22 | 1,15 | 1,13 |
| Diesel, utilitaire | 8,0 | 3,0 | 2,9 | 3,4 |

En roulant, les deux méthodes s'accordent à 25 % près. Le débit d'air donnerait plus de carburant, et s'éloignerait des chiffres
publiés : 7,9 L/100 km en WLTC sur la citadine, au lieu de 7,0. Au ralenti, la comparaison ne vaut rien : le remplissage du modèle
n'y équilibre que les frottements, sans pompage.

### L'huile

Elle passe aux segments et aux guides de soupape, et s'évapore sur la chemise. Le modèle est un **ordre de grandeur, à un facteur trois
près** : `(0,6 × litres × krpm + 0,1 × kW)` g/h, multiplié par 1,3 en turbo et 1,2 en diesel, tant que le moteur tourne, même sans combustion.
Ces deux facteurs sont supposés : la source dit « plus », sans chiffre. Ni l'âge, ni le niveau, ni la pression, ni la température d'huile ne sont simulés.

Les sources ne s'accordent pas entre elles. La brochure de Kolbenschmidt/MS Motorservice (lue) dit à la fois : 0,5 à 1 g/kWh au banc
pour une bonne étanchéité, moins de 0,05 % du carburant sur un moteur moderne (40 mL/1000 km à 8 L/100 km), et une limite de 0,5 % (0,4 L/1000 km).
Or 0,05 % du carburant fait environ 0,13 g/kWh. La fourchette de 0,1 à 0,5 g/kWh de la demande initiale n'a pas été retrouvée dans une source
lue ; GM admet 1 quart pour 2000 miles (vu cité). Le modèle se place entre les deux : voir le tableau.

### L'électrique

Puissance à la batterie : celle de la machine divisée par un rendement supposé de 0,90, et multipliée par lui en récupération. Ni carburant, ni huile, ni CO₂.
Ni pertes de charge, ni chauffage, ni auxiliaires.

### Face aux chiffres publiés

Rien n'est réglé préréglage par préréglage : seuls trois rendements et les deux coefficients d'huile sont des choix globaux.
Mesuré avec `revforge-render --out none --preset X --scenario idle|cruise|wltc [--kmh N] --fuel 1`, à chaud, sans arrêt automatique, sur route plate,
avec la boîte automatique du modèle. Le WLTC est la vraie trace de la classe 3b. Valeurs mesurées avec le nouveau modèle du turbo, encore en cours de réglage : les préréglages turbo peuvent bouger de 1 à 2 %.

| Mesure | Préréglage | Cible | Source | Modèle | Écart |
|---|---|---|---|---|---|
| Meilleur g/kWh, essence | `city_14` | 225 à 250 | Wikipédia : 1NZ-FXE, Saturn (citant Muta et al.) | 245 | dans la plage |
| Meilleur g/kWh, turbo | `hot_hatch_turbo` | 245 | Wikipédia : Ford EcoBoost | 240 | −2 % |
| Meilleur g/kWh, diesel | `diesel_four` | 197 à 205 | Wikipédia : 1.9 TDI, N47 | 196 | juste sous la plage |
| À pleine puissance, essence | `city_14` | pas de source lue | | 304 | |
| Ralenti, 2,0 L essence | `four_cyl_sport` | 0,61 L/h | DOE, Fact #861 (vu cité) | 0,77 | +26 % |
| Ralenti, V8 | `v8_cross_plane`, 5,0 L | 1,48 L/h pour un 4,6 L | DOE (vu cité) | 1,83 | +24 %, +14 % ramené à la cylindrée |
| Ralenti, 2,0 L diesel | `diesel_four` | 0,64 L/h | DOE (vu cité) | 0,46 | −28 % |
| 77 km/h, 1.4 TSI | réglage proche, 1270 kg | 4,5 L/100 | km77, Golf VI 1.4 TSI DSG (lu) | 5,1 | +12 % |
| 98 km/h, 1.4 TSI | même réglage | 5,6 L/100 | km77 (lu) | 6,2 | +10 % |
| 120 km/h, citadine | `city_14` | 6,0 à 6,4 | km77, i10 1.2 de 84 ch (vu cité) | 7,35 | +15 à +23 % |
| 120 km/h, GTI | `hot_hatch_turbo` | 7,1 | km77, Golf GTI 220 ch, route en pente (lu) | 8,31 | +17 % |
| 120 km/h, diesel | réglage proche, 1400 kg | 5,9 | km77, Golf GTD, même route (lu) | 7,3 | +24 % |
| WLTC, citadine | `city_14` | 5,3 à 5,5 | Sandero 1.0, i10 1.2 (annonces, vu cité) | 6,76 | +23 à +28 % |
| WLTC, GTI | `hot_hatch_turbo` | 7,1 à 7,3 | Volkswagen (vu cité) | 8,90 | +22 à +25 % |
| WLTC, V8 | `v8_cross_plane` | 11,8 à 12,1 | Ford, fiche Mustang EU 2022 (vu cité) | 13,9 | +15 à +18 % |
| WLTC, diesel | `diesel_four` | 6,9 à 8,3 | T6.1 2.0 TDI, auto-data (vu cité) | 7,58 | dans la plage, le conducteur a relancé le moteur une fois |
| WLTC, électrique | `electric`, 1800 kg | 13,0 à 14,4 kWh | Model 3, Tesla et FIA (vu cité, à la prise) | 17,5 kWh, à la batterie | +22 à +35 % |
| Huile, part du carburant, WLTC | `city_14` | moins de 0,05 %, limite 0,5 % | MS Motorservice (lu) | 0,10 % | deux fois plus qu'« usuel » |
| Huile, mL/1000 km, WLTC | `city_14` | 40 à 400 | MS Motorservice (lu) | 62 | dans la plage |
| Huile, g/kWh, autoroute | `city_14` | 0,13 à 1 | MS Motorservice (lu, et déduit) | 0,23 | dans la plage |

« Lu » : la page elle-même. « Vu cité » : un extrait de recherche ou une reprise, sans le document d'origine. La vitesse de 90 km/h et celle de 130 km/h n'ont pas de source lue :
le modèle donne 5,8 et 8,3 L/100 km sur la citadine, 7,5 et 9,3 sur la GTI, 10,9 et 11,9 sur le V8, 6,4 et 10,3 sur l'utilitaire diesel.

### Ce qui manque

**Le modèle lit trop haut sur route :** de 15 à 25 % environ en régime établi et en WLTC, sauf l'utilitaire diesel. Les lignes à 77 km/h, 98 km/h et du diesel sur mesure datent d'avant la correction de la résistance au roulement, qui les baisse un peu ; elles n'ont pas été remesurées.
Les chiffres officiels incluent un départ à froid (qui coûte) et souvent l'arrêt automatique (qui rapporte), absents ici : ils jouent en sens contraires, et je les suppose faibles, sans l'avoir mesuré. Sur la citadine en WLTC, l'écart de 29 % se décompose en :

- un peu d'énergie à fournir, malgré une résistance au roulement ramenée à 0,0095 : le préréglage pèse 1100 kg et perd 7 % dans la boîte, plus sa traînée ;
- environ 8 % de rendement : 361 g/kWh sur le cycle, pour environ 335 estimés.

La boîte automatique du modèle joue peu : un conducteur qui suit des rapports plus longs, à la façon du règlement, ne gagne que 3 à 8 %.

**L'électrique lit encore trop haut :** CdA de 0,70 pour 0,47 sur une Model 3, et un chiffre à la batterie comparé à des chiffres officiels à la prise, qui comptent les pertes de charge et flattent donc cette comparaison. Les corrections du roulement et de la traînée du moteur l'ont fait passer de 22,2 à 17,5 kWh/100 km.

**Le ralenti diesel lit bas, les ralentis essence lisent haut,** et les trois tournent à des régimes de 850 tr/min, plus hauts que d'autres moteurs réels. Aucun alternateur ni accessoire n'est simulé.

**En boîte automatique, le moteur peut encore s'éteindre** sur `diesel_four`, une fois pendant le WLTC, à faible vitesse en première, embrayage ouvert : sa régulation de ralenti ne le rattrape pas. Il se déclare maintenant calé, et le site propose de le relancer, mais la cause reste.

**Les réglages de turbo et de boîte comptent** : ces chiffres suivent le couple et la pression réels du modèle, et bougeront si ces parties changent.

## La carrosserie des années 80

`Body::Retro` est un coupé trois volumes à arêtes franches. Elle n'ajoute aucun calcul : `BodySpec` donne ses chiffres et tout en découle, traînée, vitesse de pointe, pont, place des sources, longueur de ligne, tête du conducteur. Les autres carrosseries ne bougent pas.

| Grandeur | Retenu | D'où ça vient |
|---|---:|---|
| Cx | 0,40 | Liste Wikipédia (lue) : Capri III 0,374, 280ZX 0,38, Mark VII 0,38, Cougar 1983 0,40, XJS 0,40, Volvo 740 de 1982 0,41 ; mais Sierra 0,34 et E30 M3 0,33 : tout le monde n'en était pas là. Choisi dans la plage demandée, en haut d'une plage réelle plus large |
| Surface frontale | 1,85 m² | **Supposée.** Aucune valeur publiée lue. La règle 0,84 × largeur × hauteur (vue citée, sans la page d'origine) donnerait 1,88 m². Contrôle : le CdA de 0,808 m² de la Volvo 740 Turbo, lu, avec Cx 0,41, implique 1,97 m² pour une voiture plus grosse |
| CdA | 0,74 m² | Le seul chiffre que lit la physique : Cx et surface n'y sont pas séparés |
| Dimensions | 4,25 × 1,65 × 1,36 m, empattement 2,50 m | E30 325i coupé (Auto-Data, lu) : 4,325 × 1,645 × 1,38 m, 2,57 m. Capri 2.8i (vu cité) : 4,35 × 1,70 × 1,30 m, 2,565 m. Plus courte qu'elles : **choix** d'un empattement court |
| Masse | 1100 kg au choix de la carrosserie, 1150 kg au préréglage | E30 325i : 1145 kg (Auto-Data, lu), 1180 kg (vu cité). Capri 2.8i : 1170 kg (vu cité) |
| Jantes | 14 pouces | 5,5J × 14 en 195/60 R14 sur la 325i (lu). Le rayon reste la formule commune : 273 mm, pour environ 295 mm réels |
| Boîte | 5 rapports | Fréquent à l'époque, sans source lue pour le fixer : la Capri a existé en 4 et 5 rapports (vu cité) |
| Centre de gravité, part sur l'avant, temps de passage | 0,52 m, 53 %, 0,36 s | **Supposés**, entre le coupé et la citadine |
| Ligne | 0,98 fois celle d'une compacte | Suit la longueur, comme les autres carrosseries |
| Isolation | Sportive au choix de la carrosserie | **Supposée :** peu de garnissage, vitres fines. Aucune mesure d'habitacle d'époque lue. Le site la règle en choisissant la carrosserie et la retire en la quittant, sauf si on l'a changée à la main. En ligne de commande, `--body retro` ne la change pas : donner `--insulation light` |

Le préréglage `retro_coupe` est un 6 cylindres en ligne de 2,5 L, 84 mm d'alésage, 6400 tr/min, à arbre unique. Rien n'y est réglé pour coller à un chiffre publié, et le modèle donne moins de puissance que la référence : 137 ch et 208 Nm, pour 171 ch et 222 Nm (Auto-Data, lu). Ces chiffres comptent l'arbre unique : voir « Le simple arbre ».

| | Coupé, préréglage | Compacte, même moteur, réglages de la compacte | Compacte, même moteur, même masse et mêmes jantes |
|---|---:|---:|---:|
| Cx, surface | 0,40, 1,85 m² | non saisis | non saisis |
| CdA | 0,74 m² | 0,70 m² | 0,70 m² |
| Masse, jantes, transmission | 1150 kg, 14", propulsion | 1350 kg, 18", traction | 1150 kg, 14", propulsion |
| Rapports | 5 | 6 | 6 |
| Vitesse maximale | 207 km/h | 212 km/h | 210,5 km/h |
| 0 à 100 km/h | 8,75 s | 9,71 s | 8,58 s |
| WLTC | 9,65 L/100 km | 9,67 L/100 km | 9,56 L/100 km |
| 120 km/h | 9,51 L/100 km | 9,13 L/100 km | 9,21 L/100 km |
| Habitacle, ralenti, plein gaz | 43,9, 73,3 dB(A) | 39,6, 66,9 dB(A) | avec la même isolation : 44,4, 72,9 dB(A) |
| Dehors, à 5 m, plein gaz | 77,4 dB(A) | 77,7 dB(A) | 77,7 dB(A) |

Mesuré par `measurePerformance`, `--fuel 1` et `soundSheet`. Les niveaux restent des estimations.

Face aux chiffres publiés : 8,3 s et 217 km/h en manuelle (E30 325i, vus cités, Cx donné à 0,38), 10 s et 206 km/h en automatique à 4 rapports (Auto-Data, lu, Cx donné à 0,35) ; 8 s et 210 km/h pour la Capri 2.8i (vus cités). Consommation : 12,7 L/100 km en ville et 6,8 sur route pour cette automatique, dans une norme d'époque qui n'est pas le WLTC.

**Ce que la caisse change peu :** le CdA de la compacte, à 0,70 m², est déjà haut pour une voiture récente, et une petite surface frontale rattrape un mauvais Cx. L'écart de traînée n'est donc que de 6 %. Cela vaut 3,5 km/h en pointe, et 3,3 % à 120 km/h ; en WLTC, à basse vitesse, l'écart se confond avec celui des rapports. Dans l'habitacle, presque tout l'écart vient de l'isolation : à isolation égale, les deux caisses sont à moins de 1 dB.

**Ce qui manque :** la résistance au roulement (0,0095) et l'adhérence sont celles de pneus récents, pour toutes les voitures. Le moteur n'a ni carburateur ni allumage d'époque : c'est le même modèle de couple, et sa consommation garde l'écart du tableau « Face aux chiffres publiés ».

## Le filtre à air

Quatre filtres (`AirFilter`, `core/src/air_filter.cpp`) : papier d'origine dans sa boîte, panneau sport dans la même boîte, cône à l'air libre, cornets. Le papier est la référence : les couples sont calibrés sur lui, et avec lui aucun de ces calculs ne s'exécute. Tout le reste est écrit par rapport à lui. Le réglage n'agit pas sur un moteur électrique, qui n'a pas de filtre.

**La perte de charge** suit `ΔP = k·ṁ²`. Au point de dessin, 0,11 kg/s, soit 2 L à 6000 tr/min :

| | Perte | D'où vient le chiffre |
|---|---|---|
| Papier et boîte | 3 kPa | Réglé sur le meilleur filtre monté dans une boîte d'origine : 2,56 kPa à 6 m³/min (fastcar.co.uk, banc d'un magazine, lu en résumé). Déjà la valeur de `kRatedFilterPa` du turbo |
| Panneau | 2,55 kPa | Même boîte, élément plus libre. Une boîte d'origine sans aucun élément perd déjà 1,25 kPa (5 po d'eau : Bob Is The Oil Guy, essai d'amateur, lu en résumé) : retirer du papier ne retire pas la boîte |
| Cône | 1,21 kPa | Le plus fermé des cônes du même banc fastcar.co.uk, à 6 m³/min |
| Cornets | 0,2 kPa | Supposé : une bouche arrondie et presque pas de grille |

`k` dépend du moteur. Une boîte grandit comme une boîte, sa surface en V^(2/3) : un 5 L n'a pas une boîte 2,5 fois plus grande qu'un 2 L, et il y perd plus. Le constructeur ne dépasse quand même pas 5 kPa sur un filtre neuf, car la limite usuelle d'un filtre chargé est de 20 à 25 po d'eau, soit 5 à 6 kPa (ordre de grandeur lu dans un brevet de filtration). Sur un turbo, `k` est celui du turbo existant, 3 kPa au débit nominal, multiplié par le rapport de ce tableau.

**La température.** Sans boîte, le filtre respire l'air du compartiment moteur. Sa hausse suit un bilan : ce qui est rejeté suit la puissance, donc le régime, et ce qui ventile suit la vitesse de la voiture, avec une part de ventilateur (`kFanShare`, 35 %, supposé). À pleine charge, en rapportant le régime `r` au point nominal, `ΔT = ΔT₀ · r / (0,35 + 0,65·r)`. `ΔT₀` vaut 12 K sur un moteur avant, 16 K à l'arrière, 20 K au centre, où l'échappement est enfermé avec le moteur : **ces valeurs sont supposées**. On n'a pas trouvé d'essai contrôlé capot fermé ; les récits vont de 10 à 30 K, et un banc capot ouvert ne voit presque rien, ce qui explique que le cône « gagne » au banc et perde sur la route. La température ne dépend ni de la charge ni de la vitesse réelle : c'est le chiffre à pleine charge, que voit aussi la fiche, sans inertie thermique. L'accumulation de chaleur à l'arrêt n'est pas simulée, ni l'encrassement.

**Le couple.** Sur un atmosphérique, la masse d'air admise suit la pression et la densité derrière le filtre, et n'agit que sur le travail des gaz : `C = (C_réf + C_frott)·r_remplissage − C_frott`, plus le pompage en moins (`ΔP·V/4π`). Sur un turbo, la soupape de décharge tient la pression : la perte n'enlève pas de couple au plateau, elle change l'arbre. L'air entre au compresseur à `inletPa` et à sa température, ce qui change le débit réduit, la hauteur et la sortie du compresseur (`TurboSpec::inletFilter`). Seule la température de la charge touche le couple, atténuée par l'échangeur (`turboChargeDensityShare`). Le turbo reste dimensionné sur le papier d'origine, et la boîte de vitesses aussi : changer de filtre ne change pas les rapports (`gearingOf`).

**Les cornets.** Leur longueur se choisit : accordée à 72 % du régime maximal, elle vaut 28 cm sur un 2 L à 6800 tr/min, soit un quart d'onde de `f = c/(4(L + 0,61a))`, 294 Hz. L'onde qui revient dans le tube change ce que trouve la soupape à sa fermeture (accord par inertie : Heywood, *Internal Combustion Engine Fundamentals*, § 7.6, cité de mémoire et non relu). Le modèle en tire `−A·cos(2π·f·25/N)`, amorti avec le nombre d'allers-retours (`A` = 3 %, `Q` = 6, **supposés**). Le pic de couple se déplace donc vers le régime accordé, avec un creux au-dessus. Les cornets d'un turbo sont une seule bouche sur l'entrée du compresseur : ni accord, ni plénum retiré.

**Le son.** C'est l'effet le plus marqué. `IntakePath` (`core/src/intake_path.cpp`) calcule par matrices de transfert les deux trajets, de la soupape à l'air libre : conduit, boîte, élément, prise d'air, bouche pour le papier ; conduit court, élément et cône pour le cône ; un tube par cylindre pour les cornets. Comme le niveau du papier est calibré, seul leur **rapport** passe, à phase minimale, sur tout ce qui sort de l'admission (bruit de bouche, raies, sifflement et décharge du turbo). Il n'y a ni nouveau bruit, ni nouveau tirage. La boîte fuit par ses parois (loi de masse, 2,5 kg/m²) : c'est ce qui borne le gain à la retirer, et aucun niveau n'est saisi par filtre. Au-dessus de 3 kHz, le rapport retombe : la boîte cachait là une partie de la source que le modèle n'a jamais décrite, et sans cela les cornets faisaient un bruit blanc tout près du bloc (8-16 kHz à 3 dB sous 0,5-2 kHz au ralenti, 21 dB une fois corrigé, contre 14 dB avec le papier). Ces 3 kHz sont un choix, voisin de `kNoiseTopHz`.

Gain calculé pour un 2 L : le panneau reste à 2 dB au plus. Le cône et les cornets perdent 1 à 7 dB sous 100 Hz, où la boîte résonne sur sa prise d'air, puis gagnent 10 à 25 dB entre 200 Hz et 2 kHz. Sans boîte, l'admission s'ouvre dans le compartiment : elle sort avec lui (`Source::Bay`, capot fermé), et non plus par une prise derrière la calandre. Ces niveaux ne sont comparés à aucun enregistrement : **seule l'écoute tranchera**.

**Les chiffres.** Calculés par ce modèle, voiture compacte de 1300 kg (coupé de 1650 kg pour le V8). Le niveau est un LAeq estimé pendant un départ à pleine charge de 8 s, jamais une mesure.

| Moteur | Filtre | Couple max (Nm) | Puissance max (kW) | Air à la puissance max (°C) | Perte à la puissance max (kPa) | 0-100 km/h (s) | WLTC (L/100 km) | Habitacle, départ (dB(A)) | Devant, départ (dB(A)) |
|---|---|---|---|---|---|---|---|---|---|
| 2,0 L atmo | Papier | 174 | 96,8 | 20 | 2,1 | 9,99 | 8,80 | 72,4 | 82,1 |
|  | Panneau | 174 | 97,3 | 20 | 1,8 | 9,97 | 8,80 | 72,5 | 82,4 |
|  | Cône | 168 | 93,3 | 32 | 0,8 | 10,21 | 8,77 | 75,9 | 84,0 |
|  | Cornets | 173 | 93,6 | 32 | 0,1 | 10,16 | 8,77 | 77,6 | 85,0 |
| V8 5,0 L | Papier | 473 | 275,1 | 20 | 4,7 | 6,28 | 15,81 | 77,6 | 84,8 |
|  | Panneau | 475 | 277,9 | 20 | 4,0 | 6,26 | 15,81 | 77,6 | 85,4 |
|  | Cône | 463 | 270,8 | 32 | 1,8 | 6,37 | 15,76 | 80,7 | 86,2 |
|  | Cornets | 482 | 273,4 | 32 | 0,3 | 6,37 | 15,76 | 83,8 | 88,5 |
| 2,0 L turbo 1,3 bar | Papier | 354 | 193,5 | 20 | 2,9 | 8,09 | 8,69 | 70,2 | 83,7 |
|  | Panneau | 354 | 193,6 | 20 | 2,5 | 8,09 | 8,69 | 70,1 | 84,1 |
|  | Cône | 350 | 190,0 | 32 | 1,1 | 8,06 | 8,69 | 74,2 | 84,2 |
|  | Cornets | 350 | 190,2 | 32 | 0,2 | 8,06 | 8,68 | 76,5 | 85,5 |

Sur le turbo, à 6000 tr/min : arbre à 176 700 tr/min avec le papier, 176 500 avec le panneau, 179 650 avec le cône. Le temps pour atteindre 90 % de la pression à 2500 tr/min passe de 1,31 s à 1,35 s avec le cône : l'air chaud coûte plus que la perte ne rapporte. Le 0-100 de ce turbo gagne pourtant 0,03 s avec moins de couple : cette traction de 350 Nm patine, et avec quatre roues motrices, le même cône perd 0,09 s (6,32 à 6,41 s). La consommation WLTC bouge à peine, et c'est attendu : à charge partielle, c'est le papillon qui étrangle, pas le filtre.

À régime tenu, l'écart dépend beaucoup du régime dans l'habitacle, où les modes de la caisse peuvent tout couvrir : 0,4 dB à 5000 tr/min, jusqu'à 9 dB à 6500 tr/min pour le cône, sur le 2 L à pleine charge. À 30 % de charge, celui du cône reste sous 3 dB ; derrière la voiture, l'échappement reste maître, à moins de 1 dB. Au ralenti, le niveau ne bouge pas de plus de 2 dB dans l'habitacle, et de 3 à 11 dB la tête au-dessus du moteur : la place à bas régime tient, l'échappement discret ne se fait pas couvrir par un fond fixe.

**Le gaz à l'écran** (`AIR_FILTER_GAS`, `fluidGas.ts`) : le frottement suit le même rapport, la section d'entrée se réduit sans boîte, et l'air entre plus chaud. Le volume entre le compresseur et le papillon ne change pas. **Les vues** dessinent la boîte, le cadre d'un panneau, le cône, ou un cornet par cylindre sans plénum ni papillon central. La jauge de dépression affiche l'air admis, seulement sur un cône ou des cornets : c'est `intakeAirK`, ce que voit le couple.

**Ce qui n'est pas modélisé :** la filtration et l'usure qu'elle épargne (les cornets n'arrêtent rien), l'encrassement, le cliquetis que favorise l'air chaud, l'effet d'un boîtier de cornets alimenté en air frais, et le calculateur qui compenserait la densité sur un turbo.

## Le simple arbre

Le modèle d'origine est un double arbre : `Camshaft::Dohc` ne passe par aucun calcul de cette section, et aucune empreinte n'a bougé. `Camshaft::Sohc` lui compare un arbre en tête par rangée, deux soupapes par cylindre et des culbuteurs (`core/src/valvetrain.cpp`, recopié pour le dessin et le gaz dans `web/src/lib/valves.ts`).

**La section, depuis ce qui tient dans l'alésage.** Deux soupapes côte à côte : d = (alésage − 2 × 2,5 mm de bord − 5 mm de pont) / 1,83, l'échappement faisant 83 % de l'admission. Quatre soupapes, chacune dans son quart, les admissions écartées de 100° et les échappements de 80°, avec un pont de 4 mm. Ces marges sont supposées, mais elles retrouvent de vrais moteurs à 6 % près : 41,5 mm à 86 d'alésage (C20NE, 41,8, de mémoire), 36,1 et 32,2 mm à 93,4 (S38, 37 et 32, lu). La levée suit l'alésage (0,12 fois). La section efficace moyenne met en série la jupe de la levée moyenne et la gorge du siège, avec Cd = 0,7 ; ramenée à la tête, elle donne 0,35, dans le coefficient de 0,3 à 0,4 de Taylor. À 86 mm : 473 mm² à l'admission contre 676, 363 à l'échappement contre 566. Deux grandes soupapes laissent 30 % de section de moins que quatre petites, et leur rapport bouge à peine avec l'alésage : c'est **la vitesse du piston, donc la course, qui décide** quand cela devient gênant.

**Le remplissage décroche selon la vitesse du gaz.** L'indice de Mach de Taylor (SAE 520259, vu cité) Z = Ap·Sp / (Aeff·a) vaut 0,53 sur un 8 soupapes de 2 L à 5200 tr/min, et 0,43 sur un 16 soupapes à 6000 tr/min : de vrais moteurs sont dimensionnés sous 0,55 environ à leur régime de puissance. `tools/valve_filling.py` intègre le remplissage quasi stationnaire d'un cylindre, sans résonance : fill(Z) = (1 + (Z/0,8)⁴)^(−1/4), à 1,8 % près de Z = 0,2 à 2. Sa limite étranglée, 0,76/Z, recoupe le 0,75/Z des cours de moteurs (vu cité). `breathing` multiplie sa forme par le rapport des deux remplissages, ce qui ne touche rien du double arbre.

**L'échappement coûte en haut.** Le même script vide un cylindre ouvert sur 5 bar et 1250 K : trop tôt, on perd de la détente ; trop tard, le piston pousse. Le travail perdu passe par un creux, puis monte vite : 0,3 bar à Z = 0,35, 1,2 bar à Z = 0,5 (`kExhaustLossTable`). Rapporté à 12 bar de pression moyenne indiquée (supposé), il entre dans `breathing`.

**Le couple vient plus tôt.** L'accord d'admission suit la racine de section sur longueur et volume (Engelman, de mémoire) : le pic descend de √(section) = 0,84. En bas, le simple arbre est donc devant, ce que disent aussi de vrais couples de moteurs. Ce décalage suppose des conduits proportionnés aux soupapes.

**Frottement et inertie, modestes.** Le ressort total reste le même, deux soupapes deux fois plus lourdes ayant un ressort deux fois plus fort, mais le culbuteur charge la came de son rapport et ajoute son axe : 50 % de plus sur le contact came-poussoir, environ 0,03 bar à 90 mm de course (Patton, Nitschke et Heywood, 1989, de mémoire : leur article n'a pas pu être relu). Le palier d'un arbre en moins pèse moins de 0,01 bar (Petroff), négligé. Un arbre et sa roue de moins par rangée, à demi-vitesse : −0,1 % d'inertie.

| Moteur | Arbre | Couple maximal | Puissance maximale | 0 à 100 km/h | WLTC |
|---|---|---|---|---|---|
| 2 L, 86 × 86, 6500 tr/min, compacte de 1200 kg | double | 168,5 Nm à 3900 | 122 ch à 5950 | 9,95 s | 8,40 L/100 |
| | simple | 166,9 Nm à 3050 | 108 ch à 5700 | 10,38 s | 8,48 L/100 |
| `hot_hatch_na` | double | 194,1 Nm à 4500 | 160 ch à 6750 | 8,72 s | 9,07 L/100 |
| | simple | 188,8 Nm à 3400 | 131 ch à 6100 | 9,42 s | 9,07 L/100 |
| `inline_six`, haut régime (8000) | double | 335,3 Nm à 4950 | 303 ch à 7400 | 6,48 s | 13,08 L/100 |
| | simple | 325,0 Nm à 3650 | 243 ch à 6600 | 6,47 s | 13,24 L/100 |
| `v8_cross_plane` | double | 428,9 Nm à 3700 | 291 ch à 5550 | 6,95 s | 13,90 L/100 |
| | simple | 424,0 Nm à 3000 | 264 ch à 5400 | 6,78 s | 14,13 L/100 |

Le 0 à 100 km/h ne suit pas toujours la puissance : `gearingOf` raccourcit le pont quand la vitesse maximale baisse, et le V8 gagne ainsi 0,17 s. Une vraie voiture déclinée garderait souvent son pont.

Face à de vrais moteurs déclinés, à cylindrée égale :

| | 8 ou 12 soupapes | 16 ou 24 soupapes | Écart |
|---|---|---|---|
| Opel 2,0 L, 86 × 86 (lu, régimes sur un forum) | 115 ch à 5200, 170 Nm à 2600 | 150 ch à 6000, 196 Nm à 4500 | −23 %, −13 %, pic ×0,58 |
| Mercedes 2,3 L (lu, sources en désaccord sur le 8 soupapes) | 136 ch à 5200, 200 Nm à 3500 | 185 ch à 6200, 235 Nm à 4500 | −26 %, −15 %, pic ×0,78 |
| BMW 3,4 L et 3,5 L (M30 lu, M88 de mémoire) | 218 ch à 5800, 310 Nm à 4200 | 286 ch à 6500, 340 Nm à 4500 | −24 %, −9 %, pic ×0,93 |

Le modèle donne de −9 à −20 % de puissance, de −1 à −3 % de couple, et un pic à ×0,74 à ×0,81. **Le sens et le haut régime sont justes ; l'écart sur un 2 L à régime modeste et sur le couple maximal reste plus faible qu'en réel.** Ces couples réels ne diffèrent pas que par leurs soupapes : le 16 soupapes a aussi des cames plus pointues, plus de compression et un rupteur plus haut, que ce réglage ne touche pas. Aucune constante n'a été ajustée pour coller à ce tableau.

**Le son.** La bouffée se vide par une section plus petite : sa durée croît comme la section (τ = V / (Cd·A·a)), soit ×1,56, et son débit de crête baisse d'autant, à gaz égal. Le débit que pousse le piston, lui, ne change pas. S'y ajoute le remplissage perdu vers le rupteur. Mesuré à régime tenu, `hot_hatch_na`, dehors : −2,7 dB(A) à 900 tr/min, −3,1 à 4000, −3,3 à 6000 ; `v8_cross_plane` : −1,7, −2,3, −2,7. Rien de mesuré ne dit qu'un vrai 8 soupapes est 3 dB plus discret : **c'est la conséquence du calcul, et l'écoute tranchera.** Les raies gardent leur structure : peigne de 13,8 à 13,4 dB, sur un tour.

La distribution change de voix, et chaque part est un choix marqué. Sous une tête plus grande, le siège résonne plus bas (raideur en d, masse en d³) : 2,85 kHz au lieu de 3,6. Chaque fermeture ne frappe qu'une tête ; la masse totale étant presque égale, l'énergie du choc reste la même. Le souffle mécanique perd 0,7 dB, la distribution y comptant pour 30 % (supposé) avec deux fois moins de cames. Surtout, le culbuteur rattrape son jeu à chaque ouverture : un tic autour de 2,4 kHz (supposé), à son propre générateur, qui vit dans le plancher du ralenti et suit la vitesse de la came, bien moins vite que le bloc. Sa force (`kRockerShare`) n'est pas dérivée : elle est choisie pour se détacher sous le capot au ralenti, sans ressortir de la voiture. Relief de 1,8 à 3,2 kHz, en fenêtres de 1 ms : sous le capot de 10,0 à 12,6 dB à 800 tr/min, de 9,2 à 13,2 à 1500, de 6,1 à 6,2 à 6000 ; dehors à 800 tr/min, de 9,6 à 8,4. **Le bruit mécanique ne passe donc pas devant l'échappement**, mais l'échappement étant plus discret, le rapport 8-16 kHz sur 500-2000 Hz monte de 2,4 dB au ralenti dehors (−21,1 à −18,7) : à surveiller à l'écoute. Un rattrapage hydraulique supprimerait ce tic ; le modèle suppose un jeu mécanique.

**Ce qui n'est pas modélisé.** La chambre elle-même : bougie décentrée, combustion un peu plus lente, moins d'avance. La consommation n'en sait donc rien, et ne bouge que par le frottement, la démultiplication et la charge : de 0 à +1,7 % au WLTC. Les gaz résiduels, plus nombreux quand le cylindre se vide mal à très haut régime, ne réduisent pas le remplissage suivant. Le bruit de l'air dans la soupape reste celui du double arbre. Le calage reste le même, et la chaîne ne fait pas de bruit à elle, sur aucun des deux.

**La vue.** `engineLayout` déduit tout de la queue de soupape : le culbuteur est perpendiculaire à la queue à mi-levée, son patin perpendiculaire au rayon de came à mi-levée, et l'angle est résolu exactement à chaque image (`rockerAngle`), car il atteint une vingtaine de degrés. Le bossage vaut la levée divisée par le rapport du culbuteur (1,4). L'arbre au milieu de la culasse prend la place de la bougie : elle passe à côté, penchée de 30° vers l'échappement. La chaîne ne fait le tour que du vilebrequin et d'une roue. Le gaz à l'écran reçoit les mêmes rapports de section, sans allocation ni changement de `EVO`, `EVC`, `IVO`, `IVC`.

## Le carburateur

`FuelSystem` : `Injection` (la référence, inchangée au bit près), `Carburetor` (un col et une pipe communs) ou `MultiCarb` (un corps par cylindre). `core/src/carburetor.cpp`. Sans effet sur un diesel ni sur un moteur électrique.

**Le venturi.** Le col se dimensionne à la puissance maximale : `d = k √(V × N)`, en mm, litres et milliers de tr/min. Un corps par cylindre suit la règle de Weber (k = 20, sur le volume d'un cylindre). Un col partagé vaut k = 10 sur toute la cylindrée, soit 32 mm sur le 2 L à 5500 tr/min d'une BMW 2002 (Solex 40 PDSI). La perte vient ensuite du débit : `Δp = (K₁ + K₂ (d/D)⁴) × ½ρv² × F × (1 + M²/4)`, où `F = <v³>/<v>³` est calculé en additionnant les bouffées qui passent par ce col (environ 10 pour un cylindre seul, 1,1 pour quatre). K₁ = 0,9 est supposé, entre ce que donnent les débits annoncés par Holley à 1,5 inHg sur un quatre corps et à 3 inHg sur un double corps. Le résultat, 7 kPa à la puissance maximale sur un 2 L à carburateur unique et 3 kPa avec un corps par cylindre, tombe dans cet ordre de grandeur. Cette perte retire de la charge sans retirer les frottements, et s'ajoute au pompage : `breathing` en tient compte au frein. Rien n'est ajouté à bas régime : la pipe longue aide à tenir le débit, mais je n'ai pas de chiffre qui justifie un gain sur l'injection de référence, qui a elle aussi ses conduits accordés.

**Le mélange.** Le gicleur vise le cylindre le plus pauvre, donc la moyenne est d'autant plus riche que la distribution est inégale : ± 7 % sur une pipe commune, ± 3 % avec un corps par cylindre (de ± 5 à 10 % chez Heywood, 3 % supposés). À charge partielle, λ = 1 − écart ; à pleine charge, λ = 0,88 × (1 − écart), 0,82 sur un turbo ; au circuit de ralenti, λ = 0,9. La soupape d'enrichissement suit la dépression et ignore la puissance, contrairement au calculateur. Pied levé, le circuit de ralenti débite encore, à son gicleur fixe : autant qu'au ralenti, à tout régime. Au rupteur, l'étincelle est coupée mais le carburateur continue. Rien n'en revient vers le couple en régime établi : la consommation lit. La pompe de reprise, elle, se compte en plus.

**Le film.** C'est le modèle x-τ d'Aquino (1981), intégré à chaque pas de `DrivingModel`, en temps physique. La colonne de carburant retarde le débit de `2L/v`, avec v tiré de la dépression au col par Bernoulli (L = 5 cm supposé) : c'est la lenteur à bas régime. Une part `x = (1 − e^(−L/0,5)) × 40/(40 + v) × (0,4 + 0,6 × remplissage)` se dépose, selon le chemin parcouru, et moins à forte vitesse d'air, où les gouttes sont fines (Nukiyama et Tanasawa). Elle repart en `τ = 0,35 s × (50/v)^0,75 × remplissage`. Ces valeurs tombent dans les plages publiées à chaud (x de 0,2 à 0,6, τ de 0,1 à 1 s ; Hires et Overington, 1981) sans y être calées point par point. Ce qui reste en suspension attend dans le collecteur, et n'arrive aux cylindres qu'avec l'air qui en sort. La pompe donne 0,4 cm³ par litre sur une course, en 0,25 s, surtout sur les parois, et seulement si le pied va franchement vite : une course lente repart au réservoir. Le couple est multiplié par `f(richesse réelle)/f(richesse établie)` : plat vers 1,15, en baisse côté pauvre, avec des ratés sous λ = 1,6 (d'après les courbes de Heywood, chapitre 5, lues à la main).

**Au son.** Pour le souffle, une turbulence confinée : `W = K ρ A v⁶ / c³` au col, soit un rendement acoustique de `2K M³` (K = 10⁻³ supposé, d'après Nelson et Morfey, 1981). La pression suit donc v³ : presque rien à bas régime, ce qui respecte « tout ce qui ne vient pas de l'échappement garde sa place à bas régime ». Sa bande suit Strouhal, 0,5 v/d, et ses aigus suivent la charge comme le bruit d'admission. Sur un corps par cylindre, chaque col suit sa propre bouffée, et les puissances s'ajoutent. La boîte à air garde 12 dB de ce souffle sur un carburateur unique, 3 dB avec de petits filtres plats (supposés). Toujours avec un corps par cylindre, le conduit droit résonne en quart d'onde, et l'aspiration perd sa boîte : 6 dB de plus, en bas de ce que retire une boîte à air (de 10 à 20 dB, supposé). Sur une pipe commune, les cylindres diffèrent selon `f` : ± 3,5 % une explosion sur deux, découlant de l'écart, et rien sur un corps par cylindre. Pied levé, le mélange du ralenti brûle encore environ un cycle sur deux (supposé) : la pression à l'ouverture tombe moins, et des détonations restent possibles même sans en demander, d'autant plus avec un échappement libre. Le creux de reprise s'entend, car `load()` le transmet au son. Rendus mesurés sur fichiers à 1500 tr/min pleine charge : 8-16 kHz face à 500-2000 Hz ne bouge pas de plus de 0,1 dB derrière la voiture.

**Suralimentation.** Montage soufflé, comme la Renault 5 Alpine Turbo : le carburateur est après le compresseur, dans une boîte sous pression. Le débit volumique ne change pas, et ρ suit la pression : la perte reste la même part de la pression, et le même facteur s'applique à `breathing`. Le film suit la masse volumique au col. Non simulé : la pression d'essence à suivre, l'échauffement dans le compresseur, un montage aspiré (un Roots surmonté de ses carburateurs, qui verrait la perte dans son rapport de pression et un mélange humide). `Aspiration::Supercharger` passe par le même `breathing`, donc par la même hypothèse.

**Chiffres.** `revforge-render --out none --carb … --scenario tipin --rpm 1500`, `--scenario wltc --fuel 1`. Compacte 1200 kg, 2 L atmosphérique, et coupé 1600 kg, V8 5 L. Creux : baisse maximale du couple due au mélange, dans la première seconde. Admission : LAeq devant la voiture, à plein gaz, un seul chiffre indicatif, jamais une mesure.

| | Couple | Puissance | 0-100 | WLTC | CO₂ | Couple 1ʳᵉ s à 1500 tr/min | Creux à 1500 / 4000 | LAeq devant |
|---|---|---|---|---|---|---|---|---|
| 4 cyl., injection | 173,6 Nm | 96,8 kW | 9,51 s | 8,64 L | 204 g | 132,0 Nm | 0 / 0 % | 82,2 dB(A) |
| 4 cyl., carburateur | 167,3 Nm | 89,3 kW | 9,91 s | 10,02 L | 237 g | 122,2 Nm | 23,5 / 11,7 % | 82,3 dB(A) |
| 4 cyl., multiples | 170,6 Nm | 93,5 kW | 9,77 s | 9,63 L | 227 g | 122,2 Nm | 14,7 / 12,8 % | 84,8 dB(A) |
| V8, injection | 401,3 Nm | 197,5 kW | 7,23 s | 13,76 L | 325 g | 288,4 Nm | 0 / 0 % | 85,2 dB(A) |
| V8, carburateur | 387,0 Nm | 182,5 kW | 7,34 s | 15,99 L | 378 g | 275,4 Nm | 12,9 / 0,8 % | 85,7 dB(A) |
| V8, multiples | 394,4 Nm | 190,7 kW | 7,42 s | 15,38 L | 363 g | 276,2 Nm | 8,2 / 1,0 % | 89,8 dB(A) |

Écart de puissance attendu : les BMW 2002 à un carburateur, à deux et à injection annoncent environ 100, 120 et 130 ch, avec une compression et des cames qui diffèrent aussi. Ici, l'écart ne vient que de l'alimentation, et reste donc plus faible. Entre un carburateur et une injection à boucle fermée, la consommation différait historiquement de 10 à 20 % selon les époques et les réglages : un ordre de grandeur, pas une cible.

**Simplifications et limites.** Tout est à chaud : ni starter ni démarrage à froid, puisque rien ne calcule la température. Pas de double corps progressif, de correcteur d'altitude, de niveau de cuve, de givrage, ni d'effet des virages sur la cuve. Le refroidissement de l'air par l'évaporation et le chauffage de la pipe sont supposés se compenser, sans les calculer. Le pauvre et le riche passagers ne changent pas le rendement de la combustion ailleurs que dans `f`. L'air d'un corps par cylindre ne pulse pas dans le film : seule sa dépression le fait. Le gaz à l'écran n'a qu'un conduit : plusieurs corps y sont un seul col, qui garde le rapport de chacun à sa pipe. Il n'y perd que ses changements de section, sans le coefficient de perte du C++ : 2,4 kPa dans le plénum à 6000 tr/min sur un 2 L à carburateur unique, 0,3 kPa avec un corps par cylindre, pour 7 et 3 kPa à la puissance maximale au couple. Les sources sont citées de mémoire : Heywood (*Internal Combustion Engine Fundamentals*, 1988, chapitres 5 et 7), Aquino (SAE 810494), Hires et Overington (SAE 810495), Nelson et Morfey (*J. Sound Vib.*, 1981), Nukiyama et Tanasawa (1939), le manuel de préparation des Weber de Passini pour la règle de 20 √(V N), les fiches Holley. À recouper avant de les citer ailleurs. L'écoute reste à faire.

**Au contact des autres réglages.** Le filtre à air (`withAirFilter`) et le venturi sont en série : leurs pressions s'ajoutent au premier ordre, car leurs facteurs se multiplient et restent petits. Le souffle du venturi rejoint `Source::Intake` directement : il ne passe pas par la réponse du filtre, et n'en suit donc pas l'emplacement. Avec un filtre « cornets » et un corps par cylindre, la boîte serait retirée deux fois : 6 dB ici, et ce que retire déjà le filtre. Un filtre qui n'a pas de sens sur un corps par cylindre n'est pas interdit.

## Le moteur rotatif

`Layout::Rotary`, un moteur Wankel. `cylinders` y compte les rotors, `displacementCc` la cylindrée habituelle : une chambre par rotor, 654 cm³ sur le moteur de référence. `isRotary(config)` existe en C++ (`engine_config.hpp`) et sur le site (`rotary.ts`, réexporté par `config.ts`) : c'est lui que lisent les réglages qui n'ont pas de sens ici.

**Géométrie.** Rayon générateur R, excentricité e, largeur b, avec V = 3√3·R·e·b par chambre. R 105, e 15, b 80 mm donnent 654,7 cm³. `rotaryGeometry` les déduit de la cylindrée aux mêmes proportions : ni alésage, ni course. Le carter est la trace d'un sommet, H(u) = R·(sin u, cos u) − e·(sin 3u, cos 3u). Une couronne de 30 dents, de primitif 3e, roule sur un pignon fixe de 20 dents, de primitif 2e : le rotor tourne au tiers de l'arbre. `rotatif.spec.ts` vérifie sur le dessin que le volume mesuré entre le flanc et le carter donne la cylindrée, et suit V₀ + V/2·(1 − cos(2A/3)).

**Cycle : la seule chose qui change les comptes.** Chaque chambre fait ses quatre temps en trois tours d'arbre, et chaque rotor a trois chambres : une explosion par rotor et par tour d'arbre, là où un cylindre en donne une pour deux tours. Une seule grandeur le porte, `sweptCcPerTwoTurns` : le volume balayé en deux tours vaut deux fois la cylindrée. Couple (`bmepToNm`, soit p·V/2π), débit d'air, turbo (`displacementM3`), pompage, papillon et consommation en découlent sans autre coefficient. Un birotor de 1,3 L avale donc comme un quatre temps de 2,6 L.

**Rendement : la cause, pas un chiffre visé.** `rotaryEfficiencyShare` multiplie trois effets. Le taux de compression, 9,4 (8,5 en turbo), par le cycle idéal. La chambre allongée : `rotaryWallRatio` compare sa surface au point mort haut, une longue lame, à celle d'un cylindre carré de même volume, et trouve environ 1,8. À 12,5 % du rendement par unité d'écart, ordre de grandeur tiré des 10 à 15 % de l'énergie du carburant qui passent aux parois (Heywood), cela retire 10 %. Les fuites aux sommets, 5 %, sont supposées. Il en sort 0,83, qui s'applique à la pression moyenne et au rendement de `ConsumptionModel`. Les frottements, ramenés au volume balayé, valent 75 % de ceux d'un moteur à pistons, sans distribution ni bielles : c'est supposé.

**Huile.** Un rotatif en brûle volontairement, dosée pour graisser ses segments : environ 8 g/h par litre et par 1000 tr/min, soit de l'ordre de 25 mL/h à 2000 tr/min. Le chiffre vient d'un souvenir de la pompe de dosage de 13B, **non retrouvé sur une fiche constructeur** ; il reste, comme le reste de l'huile, à un facteur trois près. Il n'y a ni usure ni segments d'apex simulés : ils n'existent que par ces fuites et cette huile.

**Démarrage.** Trois chambres par rotor sur un cycle de trois tours, à 2/3 du bras de levier (`rotaryCompressionLoadNm`). Le gaz fuit par les sommets à bas régime : on prend n = 0,96, qui donne 8,6 bar au point mort haut, proche de la valeur d'atelier du 13B-MSP ([830 kPa à 250 tr/min, table Mazda](https://www.foxed.ca/rx7manual/2003mazdarx8/esicont/en/srvc/html/BHE010002000T04.html)), contre 13 à 14 sur un moteur à pistons. La table donne ce chiffre à un taux de 10,0 : on l'applique à 9,4, écart négligé. Le démarreur et le volant se dimensionnent sur le volume balayé : le premier comprime autant par tour qu'un 2,6 L, le second encaisse les mêmes explosions.

**Son.** `firing` reste un cycle de deux tours : deux explosions par rotor, à 360°/rotors d'arbre. `cylinders` et `firingCount` du profil valent donc deux fois les rotors, et `boreM`, `strokeM` donnent un cylindre carré du volume d'une chambre : tout ce qui suit le débit, ligne comprise, tombe juste. Ce qui vient ensuite est propre au rotatif :

- Des lumières (`kPortEvo`…) : ouverture à 195° d'arbre après le point mort haut, fermeture à 588°, admission de 572° à 850°, sur le cycle de 1080°. **Ce calage est celui d'un 13B à lumière périphérique, de mémoire, et n'a pas été recoupé sur un manuel constructeur** ; un Renesis, à échappement latéral, en diffère sensiblement.
- Une seule largeur de lumière rend ce calage possible : le sommet d'avant l'ouvre, le sommet d'arrière la referme 120° de rotor plus loin, donc (588 − 195 − 360)/3 = 11° de rotor, 33° d'arbre. Elle s'ouvre sur 4° d'arbre, l'épaisseur du segment (supposée), au lieu de 15° d'une came.
- La bouffée (`blowdownS`) : volume à l'ouverture sur section de lumière, 14 cm² sur la référence (supposé, à 30 % près), rapportés aux 2 ms du cylindre de référence : 1,6 ms. Le gaz s'ouvre sur 15 % de pression de plus, par une détente plus courte.
- Le rotor chasse le même volume sur 393° au lieu de 240° : un débit plus continu.
- Aucun bruit de distribution : aucune soupape ne retombe. Bloc à −5 dB (40 bar de pression maximale au lieu de 70, **ordre de grandeur non recoupé**), plancher au ralenti à −3 dB, moins d'à-coups (supposés).
- **Une mémoire par chambre, et non par explosion.** Les gaz résiduels passent d'un cycle au suivant dans la même chambre, donc tous les trois tours, là où un cylindre les retrouve tous les deux tours. C'est de là que viennent les tiers d'ordre, et que disparaissent les demi-ordres, sans gain ajouté pour les obtenir. Un écart fixe entre les trois faces d'un rotor a été essayé, puis retiré : il ne changeait rien de mesurable, et le chiffre aurait été inventé.

Rangs mesurés à 4000 tr/min, ligne d'origine, régime tenu (`tools/analyze_orders.py`, en dB sous le rang le plus fort) : les deux moteurs ont les mêmes rangs 2 et 4, ceux de la cadence, mais pas les mêmes sous-rangs.

| Rang | 1 | 5/3 | 2 | 5/2 | 3 | 4 | 6 | moyenne demis | moyenne tiers |
|---|---|---|---|---|---|---|---|---|---|
| Birotor 1,3 L | −25,7 | −29,8 | 0 | −38,5 | −30,1 | −1,3 | −24,9 | −44,0 | −39,4 |
| 4 cylindres 2,15 L | −21,2 | −38,7 | −2,9 | −25,7 | −28,2 | 0 | −22,5 | −34,8 | −41,2 |

Un bicylindre à 360°, mesuré de même, a son plus fort rang à 1. `tests/rotary_test.cpp` protège les tiers et les demis.

**Face à un 4 cylindres de même puissance** (même coupé de 1300 kg, 9000 tr/min, ligne d'origine) :

| | Birotor 1,3 L | 4 cylindres 2,15 L |
|---|---|---|
| Couple | 227 Nm à 5400 tr/min | 225 Nm à 5400 tr/min |
| Puissance | 167 kW à 8250 tr/min | 166 kW à 8150 tr/min |
| 0-100 km/h | 7,1 s | 7,2 s |
| WLTC | 13,35 L/100 km | 10,94 L/100 km |
| Meilleur point de la carte | 287 g/kWh | 241 g/kWh |
| Huile, WLTC | 707 mL/1000 km | 103 mL/1000 km |

L'égalité de couple et de puissance vient du choix de la cylindrée du 4 cylindres, pour la comparaison. Le birotor lui-même n'a pas été réglé : ses 167 kW et 227 Nm sont à −2 % et +8 % des 170 kW et 211 Nm du 13B-MSP de même cylindrée (chiffres de mémoire), écart laissé tel quel. Sur une ligne sport, il passe à 181 kW et 242 Nm. La ligne d'origine du préréglage est aussi celle qui tient le contrôle de « grain » de `engine_sound_test.cpp` : sur la ligne sport, très forte dans les graves, ce rapport tombe à −41 dB, sous le seuil de −40, dans la plage de −47 à −3 dB des voitures mesurées.

**Ce qui reste simplifié.** Pas d'usure, pas de segments, pas de « ring-a-ding » au ralenti, pas de lumières variables : une admission à un seul calage. La température d'échappement du turbo suit la même loi qu'un moteur à pistons, alors qu'un rotatif rejette plus chaud. Dans `EngineView`, ce qui se branche sur ses lumières (turbo, compresseur, carburateurs, filtre) reste un schéma, sans roues animées : la chaîne complète reste sur la vue du gaz et sur la voiture. Le gaz à l'écran (`GasNetwork`) calcule vraiment trois chambres par rotor, sur 1080°, avec un tube par rotor : leur volume s'y écrit comme un piston à bielle infinie, en degrés de cycle.

## Quand les réglages se rencontrent

Chaque réglage a sa section. Ici, seulement ce qui n'existe que réuni, avec ses mesures. Les tests sont dans `tests/jonctions_test.cpp` et `web/tests/jonctions.spec.ts`.

**Filtre et compresseur.** L'air passe par le filtre avant le compresseur : `airInletBehind` donne sa pression et sa température d'entrée, `filterDrop` suit le filtre, et le compresseur reste celui du papier d'origine. La densité perdue sort du calcul même du compresseur, par la température de sortie de l'échangeur (`coolK`), qui entre déjà dans `superchargerAirShare`. `turboChargeDensityShare` n'est donc pas appliqué en plus : il compterait la même chaleur deux fois. Il sert de recoupement, à 1 % près dans le test. Le gaz à l'écran lisait déjà `AIR_FILTER_GAS` à travers `CHARGER_INTAKE`.

| V8 de 6,2 L, Roots, 0,7 bar | Papier | Panneau | Cône | Cornets |
|---|---:|---:|---:|---:|
| Perte à 88 % du régime maximal | 2,89 kPa | 2,46 kPa | 1,14 kPa | 0,19 kPa |
| Air à l'entrée | 20 °C | 20 °C | 32 °C | 32 °C |
| Après l'échangeur | 40,1 °C | 39,9 °C | 43,3 °C | 42,9 °C |
| Entraînement | 76,2 Nm | 75,8 Nm | 74,5 Nm | 73,7 Nm |
| Puissance | 393,6 kW | 394,1 kW | 390,1 kW | 391,2 kW |

Sur un compresseur, un cône perd donc 1 % : il respire mieux, mais de l'air plus chaud, qu'on recomprime. Les cornets n'accordent rien ici : l'onde de `stackRamGain` n'atteint pas la soupape à travers le compresseur.

**Carburateur et filtre.** Les facteurs de `breathing` se multiplient : ce sont deux pertes en série. Dans le son, un seul retrait de la boîte à air : celui des corps par cylindre (`kCarbOpenInlet`) s'efface devant la réponse calculée d'un cône ou de cornets, qui le contient déjà. Cornets et corps multiples à 3000 tr/min, devant la voiture, entre 100 et 800 Hz : +4,3 dB face à l'injection avant cette correction, +1,0 dB après. Sur le papier d'origine, rien ne change au bit près.

Le souffle du col dépend du montage, et c'est la position du col qui décide. Un col unique est derrière le filtre : son souffle remonte le même chemin que l'aspiration, donc par la réponse du filtre, et sort là où il sort. Chaque corps d'un carburateur par cylindre a sa bouche : son souffle n'a que l'air devant lui, et il n'y a pas de réponse à traverser, seulement l'endroit où ces bouches s'ouvrent. Le papier d'origine reste le chemin calibré.

**Carburateur et suralimentation.** `Supercharger` et `TurboCharger` reçoivent `breathing`, perte du col comprise : le couple la compte une fois. Part du couple gardée sur le V8 à 5000 tr/min, col unique : 0,926 en atmosphérique, 0,926 en turbo, 0,920 en compresseur, dont l'entraînement ne baisse pas avec elle. Comptée deux fois, elle serait à 0,857. En bas, le turbo la cache sous son plateau de calculateur. Le compresseur reçoit la densité au col et le mélange de pleine charge du turbo, qu'il n'avait pas.

**Carburateur et anti-lag.** Un carburateur ne dose pas sur commande : le retard et l'injection d'air ne peuvent plus demander de surplus de carburant, le seul surplus est le mélange qu'il débite déjà (`carbRichness`), que `fuelGps` a déjà compté. Sur 2 L à 1,3 bar, pied levé, surplus de 0,20 g/s en injection et 0,13 g/s sur carburateur au retard, 1,05 et 0,12 g/s à l'injection d'air : celle-ci n'a presque plus rien à brûler au collecteur, c'est la cause et non un malus. La coupure n'en demande pas. L'anti-lag exige `!cut_` et l'absence de coupure n'y change rien : le circuit de ralenti reste un plancher, qui ne s'ajoute pas. Dans le son, la pression d'ouverture du mélange qui brûle encore s'écrit avant `evoRatio`. L'écart ne fait que 0,3 dB : aucun test ne peut l'y garder.

**Anti-lag et tube.** La chaleur de l'anti-lag entre dans `stepHeat` comme une autre. Ce qui arrive imbrûlé s'enflamme au collecteur, et non à la turbine : `collectorK`. Sur 2 L à 1,3 bar, 3 s pied levé en quatrième :

| Tube | Sans | Retard | Air | Coupure |
|---|---:|---:|---:|---:|
| 400 mm, arbre à 3 s | 21 700 tr/min | 93 700 | 104 200 | 95 800 |
| 400 mm, reprise | 0,97 s | 0,53 s | 0,51 s | 0,54 s |
| 3000 mm, arbre à 3 s | 21 900 tr/min | 57 700 | 76 400 | 59 800 |
| 3000 mm, reprise | 2,18 s | 1,41 s | 1,26 s | 1,50 s |
| T3 au plus haut, 400 puis 3000 mm | 813, 435 °C | 1072, 455 °C | 1030, 485 °C | 927, 442 °C |

Toujours utile, moins sur un tube long : la paroi prend sa part. L'injection d'air à 400 mm se met à pomper au bout d'une seconde dans cette reprise en quatrième : l'arbre tombe de 149 000 à 104 000 tr/min. C'était déjà le cas avant de réunir les branches.

Au passage, le tube court inversait la loi de paroi jusqu'à 2051 °C sans débit, pied levé : l'inversion s'arrête au débit d'un ralenti soutenu, 20 g/s sur 2 L.

**Rotatif.** `valvetrain()` et `effectiveBoreMm()` disent ce qu'il n'a pas : arbre à cames et alésage y sont sans effet, au bit près dans le son. `strokeMm()` y donne le cylindre carré de sa chambre, et `--stroke` est ignoré avec un avertissement. Compresseur, carburateur et tube passent par `sweptCcPerTwoTurns` : le compresseur d'un birotor de 1,3 L déplace autant que celui d'un piston de 2,6 L, à 10 % près. Un corps de carburateur voit deux bouffées par cycle. Toute suralimentation prend le taux de compression du turbo.

Le motif de coupure F C C F C garde un sens sur deux rotors, et il reste tel quel. Cinq est impair : il passe d'un rotor à l'autre, comme d'un cylindre à l'autre sur un 4 ou un 6, et chaque rotor allume 8 événements sur 20. À 3600 tr/min, les raies du motif gagnent 11 dB sur un moteur rond. Personne n'a écouté, et aucune préparation réelle n'a été consultée : c'est une déduction, pas une mesure.

**Vues.** Le filtre choisi se voit sur un compresseur comme sur un turbo. Un carburateur soufflé n'a pas de second filtre. Le rotatif montre en schéma ce qui se branche sur ses lumières, sans roues animées.

## Calibration

Le réglage est comparé à dix enregistrements de voitures de Wikimedia Commons (série « engine and
exhaust sound », CC BY 3.0 : Shelby Cobra, Mustang GT, Shelby GT500, Lexus RC F, Honda S2000, Mazda
MX-5, Porsche 911 GT3 RS, Ferrari F12, Audi S1, Alfa Romeo GTV6). Ils ont servi à la mesure seule et
ne sont pas dans ce dépôt. `tools/analyze_sound.py` reproduit ces mesures et trace les spectrogrammes.

Moyennes sur les fenêtres les plus fortes, en dB par octave. La simulation est mesurée sur six
préréglages, avec le scénario `lap` :

| | 63 Hz | 125 | 250 | 500 | 1 k | 2 k | 4 k | 8 k |
|---|---|---|---|---|---|---|---|---|
| Réel | −11 | −9 | −11 | −14 | −19 | −24 | −30 | −42 |
| Version précédente | −13 | −5 | −10 | −14 | −18 | −22 | −27 | −32 |
| Simulation, avant la refonte de l'acoustique | −10 | −4 | −9 | −15 | −23 | −28 | −31 | −37 |

Ces moyennes bougent peu d'une version à l'autre, alors que le son a beaucoup changé : l'ancienne
version avait déjà le bon spectre moyen, et sonnait pourtant comme une fermeture éclair. Le défaut
est temporel, et se voit sur un spectrogramme de montée en régime. Un test le protège (« les raies s'effacent progressivement vers les aigus, sans disparaître »).

Ces mesures disent si le son est plausible, pas s'il plaît : seule l'écoute tranche. Sur ce tableau, seul le 8 kHz reste au-dessus du réel : un son plus sombre avait été jugé étouffé. Limites
connues : les graves restent plus forts qu'en réel autour de 125 Hz, et les rafales sont moins
marquées que sur les enregistrements les plus bruts, comme la Cobra.

## Reproductibilité

Le rendu est identique au bit près entre le natif et WebAssembly : calculs en `double` arrondis en
`float`, ordre des tirages aléatoires figé, pas de contraction en virgule flottante, physique à pas
fixe. La parité avec la version Kotlin d'origine (CANCitroen) a été abandonnée lors de cette
refonte. `tools/compare_wav.py` compare deux rendus.

## Les architectures : VR, W, V14 et V16

Un moteur se décrit par son ordre d'allumage et par la place de chaque numéro : sa culasse, sa rangée dans la culasse, son rang le long du vilebrequin. Tout le reste s'en déduit (`core/src/architecture.cpp`, recopié dans `web/src/lib/architecture.ts`) : l'instant de chaque allumage, la ligne où il souffle, l'angle de chaque maneton. Les moteurs d'avant (en ligne, V, V8 croisé, boxer) y ont leur ligne, recopie de `FIRING_ORDERS` et de l'ancien `placementOf`, mais leur son ne passe pas par là : il reste au bit près. Seuls les VR, les W et les moteurs de plus de douze cylindres l'utilisent (`ownArchitecture`).

### Le modèle

- **Allumages.** À intervalles égaux, le rang s allume à 720·s/N degrés. Sur des manetons communs à un angle de V qui ne divise pas le cycle (le V14), la gauche allume tous les 720/(N/2) degrés et le cylindre de droite du même maneton l'angle du V plus tard : les intervalles en découlent, inégaux.
- **Désaxage.** Sur un VR et un W, l'axe de chaque rangée passe à e = 12,5 mm du vilebrequin, vers l'extérieur de sa culasse (VW, SSP 248). Le point mort haut est atteint quand maneton, axe de piston et vilebrequin sont alignés, soit à asin(e/(l+r)) de l'axe de la rangée : la course du piston vaut r·cos a + √(l² − (r·sin a − e)²). Le maneton d'un cylindre est donc à φ = β + asin(e/(l+r)) − θ, θ l'angle de son allumage.
- **Manetons.** Sur un VR6, les paires communes d'un six en ligne (1/6, 2/5, 3/4) s'écartent de 15° + 2·asin(12,5/(164 + 45,15)) = 21,85° ; le SSP 248 dessine 21,833°. Sur un W, les deux cylindres d'une même manivelle s'écartent de l'angle des culasses moins leur écart d'allumage : +12° sur le W12 (72 − 60), −18° sur le W8 (72 − 90), les « splitpins » du SSP 248. Le W16 et le V16 tombent sur des manetons communs (90 − 90, 45 − 45). Le dessin prend une bielle de 1,6 course : 22,6° sur le VR6 dessiné.
- **Son.** `applyArchitecture` (`engine_config.cpp`) donne à chaque allumage son instant (inégal sur le V14), la culasse où il souffle (`bank`, donc la ligne A ou B quand il y en a deux) et la longueur de son conduit : 0,62 m sur une culasse seule, 0,55 et 0,72 sur deux, comme en ligne et en V. La rangée loin de l'échappement traverse en plus la culasse de l'écart des deux rangées au plan de joint, 2·(H·sin(α/2) + e·cos(α/2)) avec H = 2,1·course + 0,36·alésage : 57 mm de plus sur le VR6. Aucun gain n'est réglé pour l'irrégularité : elle vient de l'ordre et du partage entre les lignes. Un VR n'a qu'une culasse, donc une seule ligne, quelle que soit la sortie choisie.
- **Démarreur.** `compressionLoadNm` place les compressions aux vrais angles d'allumage.
- **Vues et gaz.** Le dessin tire placement, angles et manetons de l'architecture ; un VR a une manivelle par cylindre en quinconce (sept paliers pour six), un W deux bielles par manivelle. Le gaz relie chaque conduit au collecteur de sa culasse, et les bouffées partent à leur angle. Sur le V10, le V12 et le V8 à plat, qui soufflaient jusqu'ici par la parité du numéro (les impairs d'un V12 allumaient six fois de suite dans le même collecteur), chaque cylindre souffle maintenant dans la culasse où il est dessiné.

### Les moteurs, et leurs sources

| Moteur | Angles | Ordre | Intervalles | Par culasse | Source |
|---|---|---|---|---|---|
| VR5 | rangées 15°, e 12,5 mm | 1-2-4-5-3 | 144° | une culasse | Wikipedia (VR5), Motor1 |
| VR6 | rangées 15°, e 12,5 mm | 1-5-3-6-2-4 | 120° | une culasse | Wikipedia (VR6), SSP 248 |
| W8 | culasses 72°, rangées 15° | 1-5-4-8-6-3-7-2 | 90° | 180, 270, 180, 90° | SSP 248 |
| W12 | culasses 72°, rangées 15° | 1-12-5-8-3-10-6-7-2-11-4-9 | 60° | 120° | SSP 248 |
| W16 | culasses 90°, rangées 15° | 1-14-9-4-7-12-15-6-13-8-3-16-11-2-5-10 | 45° | 135, 45, 135, 90, 45, 135, 45, 90° | Bugatti (« ordre asymétrique », 45°) ; l'ordre, d'après un communiqué de 2006 cité par un wiki |
| V14 | 45° | 1-8-2-9-4-11-6-13-7-14-5-12-3-10 | 45 et 57,86° | 102,86° | calcul, ci-dessous |
| V16 | 45° | 1-9-6-14-2-10-5-13-8-16-3-11-7-15-4-12 | 45° | 90° | Cadillac 452 : deux huit-en-ligne à 45° sur un vilebrequin |

Ce qui est supposé :
- les numéros alternent d'une rangée à l'autre le long du vilebrequin (zigzag), et ceux d'un W vont de la culasse gauche à la droite, comme le SSP 248 le fait pour le W12 ; aucune source ne le dit pour le VR6 ni pour le W16. Avec cette numérotation, les deux cylindres de chaque manivelle du W16 allument à 90° l'un de l'autre, l'angle des culasses : des manetons communs, ce qui la rend plausible ;
- le V16 : l'ordre de la Cadillac n'a pas été trouvé. Chaque rangée y prend celui d'un huit-en-ligne (1-6-2-5-8-3-7-4), la droite 45° après la gauche. La Cizeta V16T, deux V8 à plat de 90° dans un bloc, ferait souffler chaque culasse par paires ; elle n'est pas modélisée ;
- le V14 : aucune voiture de série n'en a eu. Le seul V14 trouvé est un diesel marin ou de centrale (MAN 14V32/44CR, carter en V de 45°, source de seconde main), dont l'ordre n'est pas publié. Sept manetons sont pris en étoile, à 720/7 degrés d'allumage, et l'ordre de la rangée gauche est celui qui annule le mieux les couples de basculement Σ xᵢ·exp(j·k·φᵢ) pour k = 1 et 2 (xᵢ la place du maneton, φᵢ son angle), parmi les 720 ordres possibles : 1-2-4-6-7-5-3, M₁ = 0,27 et M₂ = 1,01 (en r·m·pas), contre 4,48 et 3,59 pour 1-2-3-4-5-6-7.

### Ce que l'on mesure

Ordres de l'arbre en régime tenu (`tools/analyze_orders.py`, dB sous le plus fort, même cylindrée, deux lignes sauf le VR6) :

| | Rang dominant | Demi-ordres |
|---|---|---|
| VR6, 4000 tr/min | 3 (0) | 5/2 à −25, 7/2 à −27, 11/2 à −28 |
| Six en ligne, mêmes cotes | 3 (0) | 5/2 à −33, 7/2 à −38, 11/2 à −37 |
| W16, 3000 tr/min | 5/2 (0), 8 à −3,6 | moyenne −13,6 |
| V16 | 4 (0), 8 à −4,8 | moyenne −25,5 |
| W8 | 4 (0) | moyenne −24,9 |
| V8 à plat | 4 (0) | moyenne −32,8 |
| W12 | 3 (−1,8) | moyenne −26,3 |
| V12 | 3 (−2,0) | moyenne −32,2 |
| V14 à 45° | 7 (0) | moyenne −28,4 |

- Le VR6 tient le rang 3 d'un six, mais ses rangées allument trois par trois (1, 5, 3 puis 6, 2, 4) et la rangée lointaine a son conduit plus long : ses demi-ordres montent de 8 à 11 dB face au six en ligne.
- Le W16 et le W8 font entendre chaque culasse à son rythme irrégulier : des demi-ordres 8 à 12 dB plus forts qu'un V de même nombre. Bugatti décrit un moteur « équilibré à faible charge, qui devient un fauve qui gronde » ; aucune analyse publiée en ordres de l'arbre n'a été trouvée pour le confirmer.
- Le W12 sonne comme un V12 : même rythme par culasse.
- Sur le V14, un test (`architecture_test.cpp`) isole les instants : forces et conduits égaux, le rang 3,5 est 9,7 dB plus fort qu'avec des intervalles égaux.

Les préréglages, face aux chiffres publiés ; rien n'y est réglé pour coller :

| | Couple simulé | Publié | Puissance simulée | Publiée | 0-100 simulé | Publié |
|---|---|---|---|---|---|---|
| VR6 2.8 (Golf III) | 233 Nm à 3050 | 235 Nm à 4200 | 110 kW à 5650 | 128 kW à 5800 | 9,1 s | 7,8 s |
| W12 biturbo (Continental GT 2003) | 778 Nm | 650 Nm de 1600 à 6100 | 406 kW à 5950 | 412 kW à 6100 | 5,5 s | 4,8 s |
| W16 (Veyron 16.4) | 1306 Nm | 1250 Nm | 671 kW à 5900 | 736 kW à 6000 | 3,8 s | 2,5 s |
| V16 (Cadillac 452, 1930) | 578 Nm | 367 à 432 Nm | 179 kW à 3700 | 123 à 138 kW | 10,1 s | — |

- Le VR6 respire moins bien que le vrai, avec ses deux soupapes par cylindre (`camshaft: sohc`, le 12 soupapes).
- Le W12 et le W16 n'ont qu'un turbo dans le simulateur, en attendant le biturbo : `turboLayout: twin_parallel` y est déjà écrit, sans effet.
- La Cadillac comprimait à 5,35 (Old Cars Weekly), sous la plage du socle (6 à 24) : son couple est celui d'un moteur moderne de même cylindrée.
- Sources des préréglages : auto-motor-und-sport et auto-data pour la Golf VR6 (rapports 3,30, 1,94, 1,31, 1,03, 0,84, pont 3,68, 1285 kg) ; supercars.net (ZF 6HP26 de 4,17 à 0,69, pont 3,52, 2385 kg) et australiancar.reviews (0,7 bar) pour la Continental GT ; Bugatti, auto-motor-und-sport (rapports de la Super Sport) et conceptcarz (environ 18 psi) pour la Veyron ; son pont publié (3,64) donnerait 306 km/h au rupteur en septième, loin des 407 km/h : il est laissé au calcul de la boîte ; Wikipedia et Old Cars Weekly pour la Cadillac (3 × 4 pouces, pont 4,39, de 2400 à 3000 kg).
- Les régimes maximaux du VR6 (6500), du W12 (6500), du W16 (6400) et de la Cadillac (4000, la puissance maximale tombant à 3400) sont supposés. À 3800, les modes de passage automatiques de `shift_modes_test.cpp` ne tenaient plus leur hystérésis sur cette boîte à trois rapports.

### Seize cylindres

`kMaxCylinders` passe de 12 à 16 ; les rotors restent bornés à six (`kMaxRotors`). Le WASM atteint 1 255 492 octets au lieu de 1 249 204, soit 6 ko de plus sur 4 Mo fixes. Le rendu de seize cylindres n'alloue pas (test) et calcule 4,1 s de son en 0,4 à 0,7 s en natif. Le gaz à l'écran prend 3,9 ms par image sur le W16, 3,7 sur le V16, 3,4 sur un V12, mesurés sous la charge d'autres compilations ; sur une machine saturée, 6,8, 8,3 et 4,3 ms : au-delà du budget, la qualité du gaz baisse d'elle-même. La cylindrée du site monte à 8 L : le W16 fait 7993 cm³.

### Limites

- La culasse d'un VR ou d'un W est dessinée large, sur ses deux rangées, mais les soupapes suivent l'axe de chaque cylindre et les arbres à cames celui de la culasse : sans les linguets qui les relient, le dessin n'est qu'approché. Le fond plat de la culasse et les pistons à tête inclinée ne sont pas dessinés.
- Le frottement ne dépend pas de l'architecture : les sept paliers d'un VR6 coûtent autant que les quatre d'un V6.
- Une ligne par culasse : les quatre turbos d'une Veyron, deux par culasse, chacun sur quatre cylindres, ne sont pas modélisés.
- L'écoute reste à faire, en particulier du W16 et du VR6 face à de vrais enregistrements.

## L'air ambiant

Deux réglages, la température de l'air (`ambientC`, −30 à 50 °C) et l'altitude (`altitudeM`, 0 à 4500 m), donnent l'air du
jour : `ambientAirOf` (`core/include/revforge/atmosphere.hpp`), recopié pour le gaz à l'écran par `web/src/lib/atmosphere.ts`.

### Le calcul

- Pression de l'atmosphère type (ISA, OACI), dans la troposphère : p = 101 325·(1 − 2,25577·10⁻⁵·h)^5,25588. 89 875 Pa à
  1000 m, 79 495 à 2000 m, 70 109 à 3000 m, 57 728 à 4500 m, à moins de 1 Pa de la table publiée. La température n'y suit
  pas le gradient de 6,5 K/km : on règle celle du jour à part.
- Température dans le kelvin du modèle : T = 293 + (θ − 20). Tous les calculs du moteur supposaient 293 K à 20 °C ; l'écart
  de 0,15 K avec 293,15 change la masse volumique de 0,05 %.
- Gaz parfait, ρ = p/(R·T) avec R = 287 ; vitesse du son c = 343·√(T/293), soit 312 m/s à −30 °C et 360 à 50 °C (331,3 et
  343,2 publiés à 0 et 20 °C).
- Absorption de l'ISO 9613-1 (`airAbsorptionDbPerM`, déjà là) à la température et à la pression du jour, et à une humidité
  relative de référence de **50 %**, celle des mesures de passage : l'humidité n'est pas un réglage. Le test la confronte
  aussi au tableau 2 de l'ISO 9613-2 à 15 °C (20, 50 et 80 %) et à 30 °C.

**Dans l'air de référence, 20 °C et 0 m, rien ne change, au bit près.** Chaque usage reçoit sa constante d'avant, pas un
rapport qui arrondirait autrement : p vaut exactement 101 325 (1^5,25588 = 1 dans `portable::pow`), T exactement 293, et
δ = p/p₀, θ = T/T₀, σ = δ/θ et √θ valent exactement 1. Les constantes qui différaient restent les leurs : 1,2 kg/m³ pour la
traînée, 1,204 pour le son, 343 et 340 m/s. En dehors de la référence, chacune devient constante × σ (ou × √θ, × δ) : le calcul
physique, qui redonne la constante à 20 °C et 0 m. Les quatre empreintes `altitude_*` et `ambient_*` protègent le reste.

### Ce qui se dimensionne une fois pour toutes

Le turbo, le compresseur, la boîte et son pont, la capacité de l'embrayage, le convertisseur et la carte de passage sont des
pièces et un étalonnage : ils sont calculés sur le même montage ramené à l'air de référence (`withReferenceAir`). Monter à
3000 m ne change ni le diamètre de la roue, ni l'inertie de l'arbre, ni les rapports.

### Le remplissage

Le couple indiqué suit l'air piégé ; les frottements restent (C_frein = (C + f)·s − f). L'air piégé est réchauffé par les
parois du conduit et de la chambre avant la fermeture de l'admission :

  T_piégé = T_air + ε·(T_paroi − T_air), s = (p / p_réf)·T_piégé(T_réf)/T_piégé(T_air)

avec les parois à l'eau du moteur chaud, 90 °C (`kWarmWallK`, à lire chez le sujet « thermique »). Un air plus chaud reçoit
moins des parois : c'est ce qui fait suivre au remplissage T^−0,5 plutôt que T^−1, comme le mesurent les essais (Taylor) et
comme le corrige SAE J1349. ε = 0,45 est choisi pour que l'exposant local vaille 0,5 à 20 °C ; l'air y gagne 31 K, l'ordre de
l'échauffement de la charge par les parois et les gaz résiduels (supposé). Un filtre sous le capot ajoute son échauffement à
l'air de n'importe quel jour.

- **Turbo** (`turboAmbientChargeShare`) : la soupape à ressort tient une pression **relative**, puisque l'autre face de sa
  membrane voit l'atmosphère. En altitude, la charge vaut donc l'atmosphère plus cette pression ; le rapport de pression
  monte, l'air sort du compresseur plus chaud, et l'échangeur le rend à l'air du jour.
- **Compresseur** (`superchargerAmbientChargeShare`) : il déplace un volume ; en altitude, la même vitesse donne moins de
  masse, donc moins de pression relative, et l'air piégé se calcule de même.
- **Carburateur** : ses gicleurs, choisis dans l'air de référence, débitent en √(ρ_carburant·Δp) et l'air en √(ρ_air·Δp) :
  le mélange s'enrichit en 1/√σ, ×1,20 à 3000 m. Le couple perd en plus ce que coûte ce mélange trop riche
  (`torqueOfRichness`), et la consommation le paie. Sa perte de charge se compte en part de l'atmosphère du jour.
- **Diesel** : traité comme le reste, couple en proportion de l'air (limite de fumée). Une pompe mécanique sans correcteur
  altimétrique fumerait plutôt que de perdre ce couple : non modélisé.

Face à SAE J1349 (2004, allumage commandé : CF = 1,18·(99/p)·√((T+273)/298) − 0,18), couple maximal rapporté à 20 °C et 0 m :

| | 1000 m | 2000 m | 3000 m | −30 °C | 0 °C | 35 °C | 50 °C |
|---|---|---|---|---|---|---|---|
| SAE J1349 | 0,869 | 0,754 | 0,654 | 1,119 | 1,043 | 0,971 | 0,944 |
| 2.0 atmosphérique (`hot_hatch_na`) | 0,868 | 0,749 | 0,642 | 1,108 | 1,041 | 0,971 | 0,943 |
| sa puissance | 0,854 | 0,724 | 0,607 | 1,120 | 1,045 | 0,968 | 0,938 |
| 2.0 turbo (`hot_hatch_turbo`) | 0,943 | 0,891 | 0,843 | 1,106 | 1,040 | 0,972 | 0,945 |
| V8 à compresseur (`v8_supercharged`) | 0,903 | 0,793 | 0,692 | 1,110 | 1,042 | 0,970 | 0,942 |

L'atmosphérique colle à J1349 à 1000 et 2000 m ; à 3000 m, hors du domaine de la norme (environ 90 à 105 kPa), les frottements
pèsent davantage que ses 15 % fixes, surtout en haut du compte-tours, d'où une puissance qui baisse plus que le couple. Le
turbo perd deux fois moins : la pression relative revient.

### Le turbo en altitude

À 4000 tr/min, pleine charge, 2.0 turbo à 1,3 bar : rapport de pression 2,33 au niveau de la mer, 2,49 à 1000 m, 2,69 à
2000 m, 2,91 à 3000 m ; l'arbre passe de 160 700 à 168 500, 177 100 puis 186 400 tr/min. À 5500 tr/min et 3000 m, la roue est
à 99,8 % de sa vitesse limite (`tipLimit`) ; à 4500 m, le calculateur ouvre la soupape pour la protéger, et la pression
demandée n'est plus tenue (82 %). C'est aussi ce qui fait reculer de 4150 à 2800 tr/min la puissance maximale du diesel à
3000 m.

Le retard, régime tenu, de 20 % à 100 % de remplissage, jusqu'à 90 % de la pression demandée : 1,21 s au niveau de la mer,
1,41 s à 1000 m, 1,66 s à 2000 m, 1,96 s à 3000 m (à 2500 tr/min) ; 0,59, 0,67, 0,77 et 0,89 s à 4000 tr/min. En quatrième
depuis 1800 tr/min (`--scenario turbo`) : 2,21, 2,62, 3,12 et 3,72 s. L'arbre doit monter plus haut, et le moteur lui envoie
moins de gaz.

Une régulation électronique qui viserait la même pression **absolue** qu'au niveau de la mer (sujet « pression », qui lit
`TurboSpec::ambientPa`) demanderait plus encore : rapport 3,38 à 3000 m et 4000 tr/min, la roue à sa limite dès 2000 m en haut
du compte-tours et dès 3000 m à 4000 tr/min, et un retard de 2,17 s au lieu de 1,96 à 2500 tr/min. Ces chiffres sont calculés
en remplaçant `targetBar` par la pression absolue moins l'atmosphère du jour ; rien de cela n'est branché.

### La route et la consommation

La traînée suit σ, celle d'une remorque aussi : à 130 km/h, 715 N au niveau de la mer, 597 N à 2000 m, 547 N à 3000 m. Le
pompage derrière un papillon fermé et le frein moteur suivent δ : le collecteur se vide sous l'atmosphère du jour. Le
démarreur lance plus facilement : chaque compression part d'une atmosphère plus basse (creux de 175 à 195 tr/min sur le
diesel à 3000 m).

| | 0-100 km/h | V max | WLTC | 90 km/h |
|---|---|---|---|---|
| 2.0 atmosphérique, 0 / 1000 / 2000 / 3000 m | 8,72 / 9,62 / 10,35 / 11,87 s | 224,5 / 220,5 / 216,5 / 212,0 km/h | | |
| 2.0 turbo, 0 / 1000 / 2000 / 3000 m | 6,86 / 7,38 / 7,96 / 8,65 s | 260,5 / 264,5 / 268,5 / 260,0 km/h | | |
| Citadine 1.4, 0 m / 2000 m | | | 6,76 / 6,13 L/100 km | 5,57 / 4,84 L/100 km |
| Citadine 1.4, −10 °C / 40 °C | | | 6,98 / 6,63 L/100 km | 5,84 / 5,42 L/100 km |
| Électrique à 130 km/h, 0 m / 2000 m / −10 °C / 40 °C | | 208 km/h partout (régime du moteur) | | 26,0 / 22,1 / 28,1 / 24,9 kWh/100 km |

Le turbo va plus vite en haut à 2000 m qu'au niveau de la mer : il perd 13 % de puissance, l'air 22 % de traînée. Par grand
froid, l'atmosphérique gagne 12 % de puissance mais perd en vitesse de pointe : la traînée a gagné 21 %. Le départ à froid du
moteur lui-même (sujet « thermique ») n'est pas compté ici : à −10 °C, seuls l'air et la traînée changent.

### Le son

- **Propagation** : la vitesse du son fait les retards jusqu'à l'oreille, la bouche du tube (ka), la diffraction par la
  caisse, l'effet du jet, les modes de l'habitacle et la réponse de l'admission. À 30 m, le son arrive 1,098 fois plus tard
  à −30 °C.
- **Absorption** : celle de l'ISO 9613-1 du jour. À 8 kHz : 10,5 dB/100 m à 20 °C, 14,8 à 0 °C, 1,2 à −30 °C (l'air très froid
  porte peu d'eau), 7,4 à 50 °C.
- **Rayonnement** : un débit rayonne en ρ·Q̇/4πr, une paroi en ρc·v. À même température, les deux valent σ ; d'une
  température du réglage à l'autre, ils ne diffèrent que de √θ, 0,4 dB au plus. Toutes les sources à 1 m sont donc
  multipliées par σ, une fois, avant le champ et l'habitacle ; les supports moteur, qui ne passent pas par l'air, non. À
  3000 m, le niveau estimé baisse de 3,2 dB (66,4 → 63,2 dB(A) au régime d'essai) ; `toneGain` en rend une partie à la sortie.
- **Accords** : les cornets, coupés dans l'air de référence, résonnent plus haut dans l'air chaud ; leur gain de remplissage
  se décale avec. L'indice de Mach des soupapes d'un simple arbre se compte avec la vitesse du son du jour.
- **Ce qui ne change pas** : la ligne d'échappement (`ExhaustLine`) garde son gaz chaud (500 m/s, 0,6 kg/m³) : sa
  température vient de la combustion, pas de l'air du jour.

L'écoute reste à faire : un rendu à 3000 m doit sonner comme le même moteur, un peu plus doux et plus lent à prendre ses tours,
pas comme un autre moteur.

### Le gaz à l'écran

`GasNetwork` s'ouvre sur l'air du jour (`pAtm`, `tAmb`) : la ligne débouche à cette pression, l'admission avale cet air, la
soupape du turbo tient sa pression au-dessus de lui, et le panache monte selon l'écart à sa température. Les jauges de
suralimentation (`rf_turbo_*`) se lisent au-dessus de l'atmosphère du jour, comme un vrai manomètre. Le volet de la voiture
affiche la pression, la masse volumique et la vitesse du son qui en découlent.

### Limites et valeurs supposées

- ε = 0,45 (réchauffement par les parois) est calé sur l'exposant de J1349 à 20 °C, pas mesuré sur ce moteur.
- L'humidité reste à 50 % : elle ne change ni la masse volumique (air sec) ni le remplissage, seulement l'absorption.
- La perte d'un filtre suit ṁ² sans la masse volumique : en altitude, elle baisse comme σ² au lieu de σ.
- Le carburateur soufflé par un turbo prend σ de l'air du jour, sans la pression de charge.
- Les niveaux supposés du sifflement d'un compresseur et des bruits aérodynamiques suivent σ comme le reste, sans mesure.
- Ni le refroidissement du moteur, ni la détonation, ni le calculateur ne lisent encore l'air du jour : sujets « thermique »,
  « combustion », « calculateur », « pression ». Ils trouvent ici `ambientAirOf`, `trappedAirK` et `TurboSpec::ambientPa`.
