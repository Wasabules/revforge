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

Le 0 à 100 affiché est mesuré en conduisant ce modèle : c'est le meilleur départ d'un essayeur, qui dose
lui-même la pédale quand les roues patinent (« Le pneu, l'antipatinage et le départ »). Ces chiffres restent ceux
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

**Pour ne pas hésiter :** on ne monte que si le moteur retombe au-dessus du plancher (2 % de marge), et si le rapport suivant tire, 10 % sous le régime d'arrivée, une part de la résistance de la route choisie pour ne pas retomber sous le seuil de kick-down (1,25 à 2,6 fois). On ne rétrograde pour accélérer que si le moteur reste à moins de 88 % du point de remontée du rapport inférieur et si les roues y gagnent, en tenant compte de ce que les pneus peuvent transmettre : un rapport de moins sur des roues qui patinent n'apporte rien. Juste après un passage, on ne revient en arrière que si la voiture n'avance vraiment plus. Et si la route change sous le rapport (une côte, une remorque) au point qu'il ne tienne plus la réserve qu'on lui demandait pour y monter, que le pied demande plus que cette réserve n'en laisse (au-delà de 1/réserve) et que la voiture n'avance plus (moins de 0,3 m/s²), on reprend le rapport inférieur, s'il tire plus et ne remonterait pas aussitôt : sous le seuil de kick-down, rien d'autre ne le faisait. Une voiture faible restait ainsi en 4e à 1500 tr/min dans une côte de 3 %, à 0,25 km/h par seconde, quand la 3e tirait 1,8 fois plus aux roues ; les huit empreintes de trajets (six cylindres en ligne, du mode éco au mode piste) n'ont pas bougé. Tout cela est réservé aux modes autres que Normal, et à Normal dès que la route n'est plus plate (pente, remorque) : sur le plat, sa logique d'origine ne connaît pas ces gardes, et peut encore hésiter un instant 1re-2e à très petite pédale.

**La durée des passages** reste celle de la boîte, avec une exception assumée : le sport (0,85) et la piste (0,7) raccourcissent l'embrayage piloté et le relais, et coupent davantage le couple pendant la montée d'une boîte à double embrayage ou à convertisseur. C'est ce que font les vrais calculateurs : plus de pression hydraulique, embrayage plus franc, au prix d'un passage plus dur. La séquentielle n'y gagne rien : son barillet a sa propre durée. Une boîte manuelle n'est jamais touchée.

**Le pontage du convertisseur** dépend du mode : à partir de 84 % de vitesse turbine/pompe en éco (90 % ailleurs), et jusqu'à pleine charge en sport et en piste, à partir de la première en piste.

**Conséquences :** à adhérence tenue (antipatinage sport), le 0 à 100 est toujours aussi bon en sport qu'en éco (vérifié sur tous les préréglages et toutes les boîtes). Sans antipatinage, le pneu à sommet donne raison à l'éco sur huit cas sur cent quinze, les propulsions et les intégrales les plus puissantes, qui patinent au-delà du sommet : monter plus tôt retire du couple aux roues, et l'éco gagne jusqu'à 0,7 s (`v12`, boîte à synchros) ; sur un trajet de ville, l'éco consomme moins que le sport (`consumption()`).

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

**Le conducteur** garde le calcul d'origine (pédale pour tenir la vitesse, plus l'écart à la consigne), et y ajoute sur les nouveaux trajets l'anticipation : la vitesse à ne pas dépasser maintenant pour arriver à celle de l'étape suivante, à la décélération voulue. C'est ce qui le fait freiner avant le virage. `underpowered()` dit quand, pied au plancher, la voiture n'atteint pas la consigne ; le site l'affiche. Il le dit aussi quand le conducteur, pressé (une étape où il appuie au moins à 0,7), est à son plafond de pédale et qu'à ce train la voiture n'atteindra pas la vitesse de l'étape avant qu'elle finisse : elle accélère encore, mais n'en a pas la force. Ce signal ne change rien à la physique.

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
qui patinent et brûlent pour rien. **Le moteur froid est dans « La thermique » :** les chiffres ci-dessous sont à chaud.

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
Les chiffres officiels incluent un départ à froid (qui coûte) et souvent l'arrêt automatique (qui rapporte), absents ici : ils jouent en sens contraires. Le départ à froid, mesuré depuis avec `--start cold`, coûte ici de 1 à 3 % sur le cycle (voir « La thermique ») ; l'arrêt automatique n'est pas mesuré. Sur la citadine en WLTC, l'écart de 29 % se décompose en :

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

**Simplifications et limites.** Le starter et la came de ralenti accéléré sont dans « La thermique » ; le film, lui, reste celui d'un moteur chaud. Pas de double corps progressif, de correcteur d'altitude, de niveau de cuve, de givrage, ni d'effet des virages sur la cuve. Le refroidissement de l'air par l'évaporation et le chauffage de la pipe sont supposés se compenser, sans les calculer. Le pauvre et le riche passagers ne changent pas le rendement de la combustion ailleurs que dans `f`. L'air d'un corps par cylindre ne pulse pas dans le film : seule sa dépression le fait. Le gaz à l'écran n'a qu'un conduit : plusieurs corps y sont un seul col, qui garde le rapport de chacun à sa pipe. Il n'y perd que ses changements de section, sans le coefficient de perte du C++ : 2,4 kPa dans le plénum à 6000 tr/min sur un 2 L à carburateur unique, 0,3 kPa avec un corps par cylindre, pour 7 et 3 kPa à la puissance maximale au couple. Les sources sont citées de mémoire : Heywood (*Internal Combustion Engine Fundamentals*, 1988, chapitres 5 et 7), Aquino (SAE 810494), Hires et Overington (SAE 810495), Nelson et Morfey (*J. Sound Vib.*, 1981), Nukiyama et Tanasawa (1939), le manuel de préparation des Weber de Passini pour la règle de 20 √(V N), les fiches Holley. À recouper avant de les citer ailleurs. L'écoute reste à faire.

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
| W12 biturbo (Continental GT 2003) | 778 Nm | 650 Nm de 1600 à 6100 | 406 kW à 5950 | 412 kW à 6100 | 5,4 s | 4,8 s |
| W16 (Veyron 16.4) | 1306 Nm | 1250 Nm | 671 kW à 5850 | 736 kW à 6000 | 3,6 s | 2,5 s |
| V16 (Cadillac 452, 1930) | 456 Nm à 1950 | 367 à 432 Nm | 138 kW à 3600 | 123 à 138 kW | 12,4 s | — |

- Le VR6 respire moins bien que le vrai, avec ses deux soupapes par cylindre (`camshaft: sohc`, le 12 soupapes).
- Le W12 et le W16 ont deux turbos en parallèle, un par culasse (biturbo) ; la Veyron en a quatre, deux par culasse, que le modèle réunit en deux. Le couple et la puissance sont des équilibres, les mêmes qu'avec un turbo ; le 0 à 100 km/h, lui, gagne 0,2 s (3,8 s avec un seul turbo sur le W16).
- La Cadillac comprimait à 5,35 (Old Cars Weekly). La plage du taux descend donc à 4:1 (intégration), et le préréglage prend ce taux : à 10,5, celui du socle, elle faisait 578 Nm et 179 kW, ceux d'un moteur moderne de même cylindrée. Le couple reste 6 % au-dessus du haut de la fourchette publiée.
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

## La thermique

Les températures du moteur viennent d'un réseau de quatre capacités (`ThermalNetwork`, `core/src/thermal.cpp`), qu'intègre `DrivingModel` toutes les 10 ms : le métal (culasse, chemises, haut du bloc), l'eau côté moteur, l'eau du radiateur, et l'huile avec ce qu'elle mouille (vilebrequin, bielles, carter, bas du bloc).

```
C_m dT_m/dt = Q_parois + (1 − a)·P_f − G_me·(T_m − T_e) − G_mh·(T_m − T_h) − G_ma·(T_m − T_air)
C_e dT_e/dt = G_me·(T_m − T_e) − ṁc·(T_e − T_r)
C_r dT_r/dt = ṁc·(T_e − T_r) − Q_radiateur
C_h dT_h/dt = a·P_f + G_mh·(T_m − T_h) − G_ha·(T_h − T_air)
```

- **Chaleur aux parois.** Q_parois = s·P_carburant, avec s = 0,28·(φ/0,25)^−0,25·(T_g − T_m)/(T_g − 100 °C), où φ est le débit de carburant rapporté à celui de la puissance maximale. La loi d'échelle est celle de Taylor et Toong (Nu ∝ Re^0,75, donc Q ∝ ṁ^0,75) ; le niveau est calé sur le bilan d'énergie de Heywood (*Internal Combustion Engine Fundamentals*, 17 à 26 % à l'eau à pleine charge sur un moteur à essence, de mémoire). s vaut 0,20 à pleine charge et jusqu'à 0,5 au ralenti ; une paroi froide reçoit un peu plus (gaz à 1100 °C en moyenne, supposé). L'énergie du carburant vient de la même fonction pure que le compteur (`ConsumptionModel::fuelGps`), pas du compteur ; l'enrichissement à froid, qui ne brûle pas, ne chauffe rien.
- **Frottements.** P_f = couple de frottement × ω : a = 45 % à l'huile (paliers, distribution, pompe, environ la part de ces termes chez Patton et al.), le reste aux chemises.
- **Calorstat à cire.** Il commence à s'ouvrir à 87 °C (l'une des trois valeurs courantes : 82, 87, 92 °C) et il lui faut 8 K pour s'ouvrir en grand, comme celui du Land Rover Freelander 2 (83 à 91 °C, manuel d'atelier, vu cité). Le jeu de la cire le referme 2 K plus bas : c'est l'hystérésis (supposée). L'orifice de purge laisse passer 0,5 % du débit (supposé). La pompe débite 0,25 L/s par litre de cylindrée et par 1000 tr/min (supposé : environ 2 L/s à 6000 tr/min sur un 1,4 L).
- **Radiateur.** ε-NTU, courants croisés, les deux fluides non brassés : ε = 1 − exp(NTU^0,22/C_r·(exp(−C_r·NTU^0,78) − 1)). L'air traverse le faisceau à 30 % de la vitesse de la voiture, plus 3 m/s au ventilateur et 0,3 m/s de tirage naturel (supposés) ; UA ∝ ṁ_air^0,6 (ailettes à persiennes). Il est dimensionné comme un motoriste le ferait pour son cas le plus dur, ici supposé : 60 % de l'énergie maximale du carburant à 50 km/h, 40 °C dehors, eau à 110 °C en entrée, ε = 0,6. Le ventilateur suit un thermocontact au bas du radiateur : 95 °C en marche, 84 °C à l'arrêt (VW T3, contact simple, catalogue Mecatechnic).
- **Masses.** Supposées, par litre de cylindrée : 30 kg de métal à 600 J/(kg·K) côté eau, 15 kg à 500 J/(kg·K) côté huile ; 1,2 L + 1 L/L d'eau dans le moteur, 1 L + 0,6 L/L dans le radiateur (eau glycolée, 3,64 kJ/(L·K)) ; 2,5 L + 0,8 L/L d'huile. L'eau ne dépasse pas 129 °C : elle bout sous le bouchon, et le réseau ne suit plus.

### L'huile et les frottements

**Viscosité, loi de Vogel.** μ = a·exp(b/(T + c)), a = 3,477·10⁻⁵ Pa·s, b = 1345,6 K, c = 144,9 °C, ajustée exactement sur trois points de la fiche d'une 5W-30 synthétique (Motorcraft, « Product typicals », lue) : 60 mm²/s à 40 °C, 10,5 mm²/s à 100 °C, 4250 mPa·s à −30 °C (viscosité au démarreur), densité 0,853 et dilatation de 6,5·10⁻⁴/K. À 20 °C, elle est 14 fois plus épaisse qu'à 100 °C ; une revue italienne citée parle de « jusqu'à 15 fois ».

**Frottements hydrodynamiques.** Ceux du modèle de Patton, Nitschke et Heywood (SAE 890836) : paliers 3,03·10⁻⁴·N·D³·L·n_b/(B²·S·n_c) et jupes 294·v̄_p/B, en kPa, mm, tr/min et m/s, avec des paliers de 0,67 × 0,25 alésage (ligne) et 0,56 × 0,22 alésage (bielles). Ces coefficients sont ceux que reproduisent les travaux qui en partent, pas relus dans l'article. Vérification : Petroff (couple = π·μ·ω·D³·L/(4c), jeu radial de 1 ‰ du rayon, huile à 100 °C) donne 5,7 kPa pour les paliers de la citadine à 2000 tr/min, la formule 5,9 kPa. Ils sont proportionnels au régime.

**Mise à l'échelle.** Comme Sandoval et Heywood (SAE 2003-01-0725), les termes hydrodynamiques suivent la viscosité : ici (μ/μ₁₀₀)^0,5. L'exposant est cité de mémoire ; leur résumé, lu, dit que l'huile froide donne « environ deux fois » les frottements du moteur chaud. Ici : 1,57 fois à 20 °C et 2000 tr/min, 2,15 fois à 0 °C (`frictionFactor`). Au-dessus de 100 °C, rien ne change : le modèle de frottement d'origine est calé à chaud, et une huile plus fluide glisse vers le régime mixte (Stribeck), qui compenserait.

### Ce que change un départ à froid

Après un départ à froid (`--start cold`, le réglage `startTemp`), tout part de l'air ambiant, et trois écarts à la référence chaude agissent, tous nuls une fois chaud :

- **Frottements.** L'huile froide ajoute ses frottements au couple, au lancement et moteur calé compris : le démarreur tourne moins vite, le moteur cale plus facilement. La combustion, elle, reste celle du moteur chaud.
- **Ralenti visé.** Le calculateur de la Saab 9-5 (Trionic, manuel d'atelier en ligne, lu) vise 1200 tr/min à −30 °C d'eau et 825 tr/min une fois chaud : +45 % à −30 °C, en ligne droite jusqu'à 70 °C (fin supposée), donc +23 % à 20 °C. Un carburateur fait la même chose par sa came de ralenti accéléré.
- **Enrichissement.** Le carburant qui reste liquide sur les parois froides et repart imbrûlé. Supposé : λ ≈ 0,87 à 20 °C d'eau avec un calculateur, 0,77 avec un starter, qui n'a pas de sonde, en ligne droite jusqu'à 70 °C. Il se paie à la consommation, sans couple ni chaleur. Rien sur un diesel, qui n'a ni papillon ni starter.

**Parti chaud, rien n'en revient.** Le réseau tourne quand même, pour les jauges, mais ni le couple, ni la conduite, ni le son ne le lisent, quelle que soit la température qu'il calcule : les 29 empreintes sont les mêmes au bit près, et un test fait diverger les températures (20 °C et −30 °C dehors) sans que rien d'autre ne bouge. La fiche (0 à 100 km/h, vitesse de pointe) se mesure moteur chaud, même après un départ à froid.

**Le son.** Rien n'a été ajouté au son : il suit le régime et la charge réels. Le ralenti part plus haut et redescend à mesure que l'eau chauffe, en une dizaine de minutes au ralenti par 20 °C, en quatre minutes et demie sur le WLTC. Par −10 °C, le ralenti du V8 respire davantage : son niveau varie de 2,4 dB (écart type sur 50 ms) contre 1,3 dB à chaud, la régulation ouvrant plus pour tenir des frottements presque doubles. L'écoute reste à faire.

**Sur l'électrique, rien de tout cela** : pas de réseau, pas de jauge, et les fonctions renvoient l'air ambiant ou zéro.

### Face aux chiffres publiés

Mesuré avec `revforge-render --out none --preset X --scenario wltc|idle --start cold|warm --fuel 1` (et `--seconds 589` pour la première phase), par 20 °C.

| Préréglage | Phase basse, chaud → froid (L/100 km) | WLTC complet, chaud → froid | Eau à 70 °C | Calorstat ouvert | Huile à 80 °C | Ralenti 15 min, chaud → froid (L/h) | Eau à 70 °C au ralenti |
|---|---|---|---|---|---|---|---|
| `city_14` | 8,69 → 9,66 (+11 %) | 6,76 → 6,89 (+3 g/km) | 264 s | 373 s | 371 s | 0,56 → 0,67 (+20 %) | 632 s |
| `hot_hatch_turbo` | 12,80 → 14,09 (+10 %) | 8,90 → 9,08 (+4 g/km) | 219 s | 293 s | 317 s | 0,84 → 1,00 (+19 %) | 582 s |
| `v8_cross_plane` | 23,98 → 26,83 (+12 %) | 13,90 → 14,28 (+9 g/km) | 277 s | 385 s | 401 s | 1,87 → 2,21 (+18 %) | 605 s |
| `diesel_four` | 8,56 → 9,15 (+7 %) | 7,58 → 7,66 (+2 g/km) | 289 s | 427 s | 402 s | 0,46 → 0,56 (+20 %) | 725 s |
| `carb_twin_cam` | 13,51 → 15,22 (+13 %) | 8,94 → 9,17 (+5 g/km) | 232 s | 337 s | 341 s | 0,85 → 1,05 (+23 %) | 573 s |

Sur `diesel_four`, le conducteur relance le moteur une fois pendant le WLTC, à chaud comme à froid (voir « La consommation »). Par −20 °C, la citadine au ralenti met 17 minutes à atteindre 70 °C d'eau, et dépense 48 % de plus que chaude.

| Mesure | Modèle | Publié | Source |
|---|---|---|---|
| Ralenti visé, eau à −30 °C | 1,45 fois le ralenti chaud | 1200 tr/min pour 825 | Saab 9-5, Trionic (lu) : le modèle est calé dessus |
| Ralenti à 20 °C, `city_14` | 985 tr/min pour 815 à chaud (1,21), 1105 juste après le démarrage | 1100 pour 950 à 20 °C (1,16) dans un exemple de réglage ; « de 1200 à 1800 » au démarrage | HP Academy, engineerfix.com (vus cités) |
| Durée du ralenti accéléré, au point mort par 20 °C | 9,5 à 12 min, le temps que l'eau atteigne 70 °C | « une à deux minutes » pour le pic ; chauffe du catalyseur pendant jusqu'à 5000 allumages, environ 2 min | engineerfix.com (vu cité), Saab WIS (lu) |
| Eau à 70 °C, WLTC froid | 3,7 à 4,8 min | la période froide des essais en conduite réelle (RDE) finit à 70 °C d'eau, et dure 5 min au plus | règlement (UE) 2017/1151 (vu cité) |
| Huile après l'eau | environ 6 K derrière l'eau pendant la mise en température du WLTC, puis au-dessus | « environ 10 °C de retard » sur un diesel de poids lourd | *Applied Sciences* 2020 (MDPI, vu cité) |
| Surcoût, première phase | +7 % (diesel) à +13 % (carburateurs) sur 589 s | +20 % sur la phase froide de la FTP (505 s) face à la phase chaude, par 24 °C ; +40 à +80 % par −6,7 °C | Taylor et Stewart, SAE 2001-01-0221 (résumé lu) |
| Surcoût, WLTC complet | +2 à +9 g/km de CO₂ (+1 à +3 %) | +6,1 g/km en moyenne sur 20 voitures | Tsokolis et al., *Applied Energy* 2016, JRC102610 (résumé lu) |
| Frottements à froid | ×1,57 à 20 °C, ×2,15 à 0 °C, à 2000 tr/min | « environ deux fois » | Sandoval et Heywood, SAE 2003-01-0725 (résumé vu cité) |

« Lu » : la page ou le document lui-même. « Vu cité » : un extrait de recherche ou une reprise. Le ralenti accéléré du modèle dure plus que le pic que décrivent les conducteurs, qui est la chauffe du catalyseur : le modèle ne la simule pas, et garde le ralenti du calculateur selon l'eau, qui redescend avec elle.

### Ce qui manque

- **Le surcoût d'un départ à froid lit environ moitié moins que les chiffres publiés.** Le modèle ne compte que le moteur : ni l'huile de boîte et de pont, ni les pneus froids, ni la chauffe du catalyseur (allumage retardé vers 10° après le point mort haut chez Saab, pendant jusqu'à 5000 allumages), ni la combustion moins bonne sur des parois froides (de l'ordre de 1 à 2 %), ni le chauffage. Rien n'a été réglé pour rattraper cet écart.
- **Supposés** : la fin du ralenti accéléré et de l'enrichissement à 70 °C, l'enrichissement lui-même, les masses, les conductances, le cas de dimensionnement du radiateur, les vitesses d'air, la part des frottements qui va à l'huile, la température moyenne des gaz.
- **Pas d'échangeur eau-huile** : l'huile monte à 115-120 °C sur la fin du WLTC, et vers 140 °C en tirant une remorque par 45 °C.
- **La surchauffe** : avec un ventilateur qui marche, le ralenti par 45 °C ne surchauffe pas (l'eau tient vers 97 °C) ; ni le condenseur de climatisation, ni l'air chaud qui revient sous le capot ne sont simulés. Ventilateur en panne, l'eau dépasse 115 °C (test). Au-delà de 129 °C, l'eau bout : ni perte de liquide, ni protection du calculateur.
- **Parti chaud, le réseau ne fait que les jauges** : un moteur chaud qui tourne une heure au ralenti par −30 °C refroidit à l'écran sans que ses frottements changent.
- Le rotatif n'a que ses paliers dans les frottements hydrodynamiques (pas de jupes), au même quart de moins que ses frottements (supposé). Le gaz à l'écran et la chaleur de la page (`Motion.heat`) ne lisent pas ce réseau, et la vapeur d'un échappement froid n'est pas dessinée.
- L'air du radiateur est à 101 325 Pa : l'altitude n'y est pas encore.

### Ce que lisent et exposent les autres sujets

Lu : la température ambiante (`EngineConfig::ambientC`, 20 °C par défaut). Exposé, en fonctions pures : `oilViscosityPaS`, `hydrodynamicNmPerRpm`, `coldFrictionNm`, `frictionFactor`, `coldIdleRise`, `coldEnrichment`, `wallHeatShare` ; et dans `DrivingModel::thermal()` : `coolantC`, `oilC`, `metalC`, avec `coldIdleRpm`, `coldEnrichment`, `coldFrictionNm` de ce qui agit. Pour qu'un moteur parti chaud reste au bit près, un effet branché sur ces températures passe, comme ici, par un écart à la référence chaude (`kWarmCoolantC`, `kWarmOilC`) qui n'agit qu'après un départ à froid (`thermalCoupled()`).

## La combustion

Trois réglages du volet du moteur, section « Combustion » : le **taux de compression** (0 = celui d'origine), le **carburant** (SP95,
SP98, essence de course 102, E85, méthanol), et ce qui en découle, le **cliquetis**. Le code est dans `core/src/combustion.cpp`
(fonctions pures et `KnockMap`), `DrivingModel::stepKnock` (le capteur) et `core/src/knock_sound.cpp` (le son). Au défaut, rien de tout
cela ne s'exécute : `combustionStock` renvoie vrai, `fullLoadTorqueNm` garde son chemin d'origine, aucune carte de cliquetis n'est
construite, le son ne tire rien de plus, et les 29 empreintes restent au bit près.

### Le taux de compression

Le moteur d'aujourd'hui suppose 10,5:1 en atmosphérique, 9,5:1 suralimenté, 18:1 en diesel, 9,4 ou 8,5:1 sur un rotatif
(`stockCompressionRatio`, les valeurs du démarreur). Le réglage est borné à ce qui existe (`compressionRatioOf`) : un diesel ne descend
pas sous 14:1, le plus bas d'un diesel de série (Mazda Skyactiv-D), et un rotatif ne dépasse pas 11,9:1, le plus haut d'un rotatif de
série (Mazda 8C, 2023), faute de creux dans le rotor (supposé) ; l'essence va de 4 à 24, pour les moteurs d'avant-guerre (Ford A,
4,2:1 ; Cadillac V16, 5,35:1). Un deux-temps part de son taux géométrique, 7,5:1.

Le rendement indiqué suit le cycle air-carburant, puis ce que perdent les parois :

```
η(r) = η_ac(r) · (1 − k · (2 (r − 1) · B/S + 4))
η_ac = 1 − r^(1−γ)                                   Otto, γ = 1,3 (Heywood, ch. 5)
η_ac = 1 − r^(1−γ) · (β^γ − 1) / (γ (β − 1))         Diesel, rapport d'injection β = 2 (supposé)
```

Le second facteur est la surface de la chambre au point mort haut, rapportée à l'alésage : elle croît avec le taux, et la chaleur
perdue avec elle. k = 0,00533 met l'optimum à 17:1 sur un moteur carré, comme l'ont mesuré Caris et Nelson (1959, cité par Heywood).
De 10,5 à 12,5:1, le rendement gagne 2,5 % ; un diesel à 18:1 est déjà près de son optimum, et 16:1 ne lui coûte que 0,75 %, ce que
montrent les diesels récents descendus à 15,5 ou 16:1. Sur un rotatif, le même calcul passe par sa chambre allongée
(`rotaryCycleEfficiency`, voir « Le moteur rotatif »).

Ce rapport `η(r)/η(r₀)` multiplie le travail des gaz, pas les frottements, dans `fullLoadTorqueNm` ; la consommation multiplie son
rendement par le même rapport, si bien que le carburant par kilogramme d'air ne change pas. Le taux change aussi le démarreur
(`compressionLoadNm` lit le taux réel) et la détente à l'ouverture de l'échappement : la pression d'Otto `p₄/p₁ = 1 + q/(c_v·T₁·r^(γ−1))`
baisse quand le taux monte, et la force des bouffées du son suit (`releasePressureShare`). La boîte reste celle du moteur d'origine,
comme pour le filtre à air : son pont ne bouge pas.

### Les carburants

| | PCI (MJ/kg) | Pouvoir comburivore | kg/L | RON / MON | Vaporisation (kJ/kg) | Énergie par kg d'air |
|---|---|---|---|---|---|---|
| SP95 (référence) | 42,9 | 14,7 | 0,745 | 95 / 85 | 350 | 2,92 MJ |
| SP98 | idem | idem | idem | 98 / 88 | idem | idem |
| Essence de course 102 | idem | idem | idem | 102 / 90 | idem | idem |
| E85 | 29,3 | 9,75 | 0,782 | 107 / 89 | 831 | 3,01 MJ (+3,0 %) |
| Méthanol | 19,9 | 6,47 | 0,792 | 109 / 89 | 1168 | 3,08 MJ (+5,4 %) |
| Gazole | 43,1 | 14,5 | 0,832 | — | 250 | — |

L'essence garde exactement les chiffres de la consommation d'origine. Le 98 et le 102 n'en diffèrent que par l'octane (EN 228 pour le
98 ; valeurs courantes, supposées, pour le 102). L'E85 n'est pas saisi : il est calculé à partir de 85 % d'éthanol en volume et de 15 %
d'hydrocarbures (Heywood, annexe D ; NIST pour les chaleurs de vaporisation à 25 °C), sauf ses indices d'octane, mesurés et non
mélangeables (valeurs courantes, à quelques points près). Les masses molaires et les températures d'ébullition donnent la tension de
vapeur par Clausius-Clapeyron.

La vaporisation refroidit la charge : `evaporationCoolingK` prend la chaleur latente de la part du carburant qui se vaporise aux dépens
de l'air, jusqu'à saturation de sa vapeur à la température atteinte. En injection indirecte et au carburateur, cette part vaut 0,15
(supposé : l'essentiel se vaporise sur la soupape et le conduit chauds) : 3,6 K pour l'essence, 12,7 K pour l'E85. Tout vaporisé dans
l'air à 320 K, l'E85 saturerait à 40 K et le méthanol à 49 K, bien avant les 84 et 182 K que sa chaleur latente permettrait. La charge
plus froide est plus dense, mais sa vapeur prend la place d'un peu d'air : `fuelTorqueShare` fait le produit de l'énergie par kilogramme
d'air et de cette densité, face à l'essence de référence, dans `fullLoadTorqueNm`. La consommation en litres suit la densité et le PCI de
chaque carburant.

Rien de cela ne s'applique au diesel : ni indice d'octane (il s'allume par compression, d'où l'absence du choix dans le volet), ni
cliquetis d'étincelle, ni refroidissement par vaporisation (le gazole est injecté vers le point mort haut, après la fermeture de
l'admission). Son taux, lui, compte : rendement, démarreur, et claquement. Ce dernier suit la part du carburant qui brûle d'un coup,
donc le délai d'auto-inflammation de Hardenberg et Hase (1979), calculé au point mort haut d'une compression polytropique (n = 1,35)
depuis 330 K et 1 atm : à 15:1, le délai s'allonge de 26 %, à 22:1 il raccourcit de 11 % (`dieselKnockScale`). La montée de pression, donc
le claquement (`kDieselKnock`) et le bruit de combustion du bloc (loi d'Anderton), suivent en proportion : +2 dB à 15:1, −1 dB à 22:1.

### Le cliquetis

**La chambre.** `endGasCycle` déroule une combustion à une zone, degré par degré, de la fermeture de l'admission (50° après le point
mort bas, le calage d'origine du son) à l'ouverture de l'échappement : bielle de 3,2 fois le rayon de manivelle, γ = 1,3, combustion
de Wiebe (a = 5, m = 2) sur 45° après un noyau de flamme de 0,35 ms, et 80 % de la chaleur du carburant au gaz (le reste aux parois et
aux imbrûlés). La charge arrive du collecteur, réchauffée par les parois (20 % de l'écart à l'eau plus 40 K, moins quand elle est
dense : h ∝ (ρv)^0,8), refroidie par la vaporisation, et mêlée à 4 % de gaz résiduels à 1000 K. Sa masse est celle que donne le
remplissage du modèle de couple (0,92 × la respiration), à la température qu'elle a vraiment. Ce cycle donne, à 3000 tr/min et pleine
charge, 60 bar au plus et 12 bar de pression moyenne indiquée sur un atmosphérique de 10,5:1 : l'ordre de grandeur des vrais.

**Le bout de chambre.** Les gaz frais sont comprimés par le piston puis par la combustion, à γ = 1,30 (pertes aux parois comprises) :
`T_u = T_FA · (p/p_FA)^((γ−1)/γ)`. Leur délai d'auto-inflammation est celui de Douaud et Eyzat (1978) :

```
τ = 17,68 · (ON/100)^3,402 · p^−1,7 · exp(3800/T)     ms, p en atm, T en K, ON l'indice d'octane recherche
I = ∫ dt / τ                                          Livengood et Wu, jusqu'à 90 % brûlé
```

Le cycle moyen cliquette si l'intégrale atteint 1 avant que la flamme n'ait brûlé 90 % du mélange.

**La carte.** Le calcul est trop lent pour chaque pas de 1 ms : `KnockMap` le fait une fois par réglage, sur 10 régimes et
15 avances, et le garde (`knockMapOf`, quatre moteurs à la fois, sans allocation). La pression n'y est pas tabulée : à température
égale, toute la trace de pression est proportionnelle à celle de la fermeture, et l'intégrale suit sa puissance 1,7. À une pression de
charge `k` fois celle de la pleine charge, la limite est l'avance où l'intégrale vaut `k^−1,7`. Le meilleur couple (MBT) brûle la
moitié du mélange 8° après le point mort haut (Heywood).

**Un cycle n'est pas l'autre.** La phase de combustion varie d'un cycle à l'autre (supposé : 1,5° d'écart type, une cloche bornée à
trois écarts types, somme de trois tirages uniformes). La **limite de cliquetis** est l'avance où un cycle sur cinquante cliquette,
deux écarts types sous celle du cycle moyen.

**La carte d'origine** est réglée comme le fait un calibrateur : le meilleur couple, sauf là où il cliquetterait, où elle reste 2° sous
la limite du moteur d'origine, au SP95, dans l'air de référence. Même son cycle le plus rapide reste alors sous la limite du cycle moyen :
**aucun préréglage ne cliquette à ses réglages**, et le couple calibré des préréglages est celui de cette carte. Elle reste la même
quand on change le taux ou le carburant : c'est la carte du calculateur, pas celle d'un moteur re-réglé.

| Préréglage | Taux | Marge la plus faible | à | Retard le plus grand face au MBT | à | MBT atteint dès |
|---|---|---|---|---|---|---|
| `city_14` | 10,5 | 2,0° | 1000 tr/min | 9,4° | 1000 tr/min | 2250 tr/min |
| `four_cyl_sport` | 10,5 | 2,0° | 1000 | 10,1° | 1000 | 2250 |
| `three_cyl_turbo` | 9,5 | 2,0° | 1000 | 22,1° | 2000 | 6000 |
| `hot_hatch_turbo` | 9,5 | 2,0° | 1000 | 22,3° | 2250 | 6500 |
| `hot_hatch_na` | 10,5 | 2,0° | 1000 | 10,0° | 1000 | 2250 |
| `five_cyl_turbo` | 9,5 | 2,0° | 1000 | 24,3° | 2250 | jamais |
| `inline_six` | 10,5 | 2,0° | 1000 | 10,0° | 1000 | 2250 |
| `v6_gt` | 10,5 | 2,0° | 1000 | 10,6° | 1000 | 2500 |
| `flat_six` | 10,5 | 2,0° | 1000 | 9,8° | 1000 | 2250 |
| `boxer_four_turbo` | 9,5 | 2,0° | 1000 | 20,7° | 2250 | 6500 |
| `v8_cross_plane` | 10,5 | 2,0° | 1000 | 11,1° | 1000 | 2500 |
| `v10_open` | 10,5 | 2,0° | 1000 | 8,0° | 1000 | 2000 |
| `v12` | 10,5 | 2,0° | 1000 | 8,7° | 1000 | 2000 |
| `retro_coupe` | 10,5 | 2,0° | 1000 | 10,2° | 1000 | 2500 |
| `v8_supercharged` | 9,5 | 2,0° | 1000 | 17,6° | 1000 | 5000 |
| `carb_twin_cam` | 10,5 | 2,0° | 1000 | 10,6° | 1000 | 2500 |
| `twin_rotor_13` | 9,4 | 2,0° | 1000 | 20,6° (d'arbre) | 1000 | 2500 |
| `diesel_four`, `electric` | | sans étincelle | | | | |

Pleine charge stabilisée, de 1000 tr/min au régime maximal, par `revforge-render --out none --preset X --scenario steady --seconds 1 --knock 1`,
qui écrit la carte régime par régime ; le test « aucun préréglage ne cliquette à ses réglages » le vérifie de 50 en 50 tr/min, depuis le
ralenti. Les atmosphériques sont limités sous 2000 à 2500 tr/min, ce que font les vrais à 10,5:1 au SP95 ; les turbos de 9,5:1 à 1,2 à
1,5 bar le sont jusqu'en haut, avec une demi-combustion vers 29° après le point mort haut à 2000 tr/min sur la GTI, l'ordre de grandeur
des moteurs suralimentés à 20 bar de pression moyenne (de mémoire : 25 à 35°, sans source relue).

Ce que le modèle donne, à 1500 tr/min pleine charge sur la citadine : −4,2° par point de compression, +0,69° par point d'octane,
−0,85° pour 10 °C d'air en plus. Soit environ 6 points d'octane par point de compression, quand la règle des motoristes en dit 3 à 5 (de
mémoire, sans source relue) : le modèle est sans doute un peu trop sensible au taux.

**Le capteur.** Chaque cliquetis détecté retire 1,5° à son cylindre, l'avance revient de 1° par seconde quand il se tait, jamais plus de
20° retirés (supposés, ordres de grandeur des calculateurs de série). En moyenne : `dρ/dt = 1,5° × part des cycles qui cliquettent ×
combustions par seconde et par cylindre − 1°/s`. Il s'équilibre vers 2 % de cycles qui cliquettent, à la limite. **Un carburateur n'a pas
de capteur** : il cliquette. Le travail des gaz suit l'avance appliquée, rapportée à celle de la carte d'origine, par le même cycle
(`KnockMap::torqueShare`), sur le couple produit à chaque pas ; la fiche prend l'état stabilisé. Le carburant, lui, suit l'air : retarder
gaspille, ne dépense pas moins.

**Ce que cela donne**, avec `revforge-render --out none --preset X [réglage] --scenario wltc --fuel 1` :

| Réglage | Couple max | Puissance max | 0-100 km/h | WLTC | Meilleur point |
|---|---|---|---|---|---|
| `city_14` | 111,4 Nm | 56,6 kW | 13,34 s | 6,76 L/100 | 245 g/kWh |
| à 12,5:1 | 114,6 (+2,9 %) | 58,5 (+3,4 %) | 12,96 s | 6,61 (−2,2 %) | 238 |
| à l'E85 | 119,0 (+6,8 %) | 60,9 (+7,6 %) | 12,47 s | 9,48 (+40 %) | 356 |
| au méthanol | 128,1 (+15 %) | 66,0 (+17 %) | 11,68 s | 13,88 (+105 %) | 522 |
| `hot_hatch_turbo` | 357,0 Nm | 194,4 kW | 6,86 s | 8,90 L/100 | 240 g/kWh |
| à l'essence 102 | identique | identique | identique | identique | identique |
| à l'E85 | 380,1 (+6,5 %) | 208,1 (+7,0 %) | 6,50 s | 12,49 (+40 %) | 350 |
| à 10,5:1 | 355,2 (−0,5 %) | 193,9 (−0,3 %) | 6,85 s | 8,76 (−1,6 %) | 235 |
| `carb_twin_cam` | 171,5 Nm | 89,5 kW | 8,65 s | 8,94 L/100 | 253 g/kWh |
| à 12:1 (cliquette, sans capteur) | 175,6 (+2,4 %) | 91,8 (+2,6 %) | 8,51 s | 8,78 (−1,8 %) | 247 |
| `diesel_four` | 315,1 Nm | 113,5 kW | 12,94 s | 7,58 L/100 | 196 g/kWh |
| à 16:1 | 312,5 (−0,8 %) | 112,5 | 13,02 s | 7,63 (+0,7 %) | 198 |
| à 22:1 | 314,6 | 113,3 | 12,95 s | 7,59 | 196 |
| `twin_rotor_13` | 226,7 Nm | 167,3 kW | 7,13 s | 13,35 L/100 | 287 g/kWh |
| à 11:1 | 237,9 (+4,9 %) | 176,7 (+5,6 %) | 6,92 s | 12,88 (−3,5 %) | 274 |

Un taux plus haut paie en bas, où le capteur retire l'avance (jusqu'à 7,4° sur la citadine à 13:1, plein gaz en quatrième depuis
1800 tr/min), et gagne en haut. Un meilleur octane ne change rien tant que la carte reste celle d'origine : il donne de la marge, que le
décalage d'avance du calculateur pourra prendre. L'E85 et le méthanol gagnent par leur énergie par kilogramme d'air et leur charge plus
froide, et coûtent 40 % et 105 % de litres en plus, le rapport de leurs énergies par litre. Ces gains à avance égale sont un peu au-dessus
de ce qu'on lit d'ordinaire pour l'E85 (3 à 5 %, sans source relue) : la part vaporisée dans l'air est supposée.

### Le son du cliquetis

Quand les gaz frais s'enflamment d'eux-mêmes, la pression saute et la chambre résonne sur ses modes acoustiques (Draper, 1938) :

```
f_mn = α_mn · c / (π · alésage)       α = 1,841 (1er circonférentiel), 3,054 (2e), 3,832 (1er radial)
c = √(γ R T)                          gaz brûlé : γ = 1,25, R = 289,7 J/(kg·K), T du cycle à 90 % brûlé (≈ 2580 K)
```

Soit 6,59 kHz pour 86 mm d'alésage, 6,75 kHz pour 84 mm, 7,55 kHz pour 75 mm. Face aux mesures publiées :

- la règle des préparateurs, 1800/(π × alésage en mm) kHz, donne 6,66 kHz pour 86 mm, à 1 % du modèle (forums HP Academy, vu cité) ;
- Mittal (SAE 2018-01-1666) trouve le premier mode circonférentiel vers 6 kHz sur un moteur actuel, Eng (SAE 2002-01-2859) entre 5 et
  6 kHz (résumés lus) ;
- un moteur au gaz de 126 mm d'alésage (Physics, 2021, extrait vu cité) montre des bandes à 4,3-5,4, 6,4-7,7, 8,9-9,6, 10,3-11,4 et
  12,2-13,1 kHz : le modèle place ses cinq premiers modes à 4,50, 7,46, 9,36, 10,26 et 12,99 kHz, dans chacune (un test le vérifie).

Sur un rotatif, la chambre est une lame entre deux sommets : son premier mode est en longueur, `c/(2L)`, L la corde (√3 fois le rayon
générateur), soit 2,7 kHz sur le birotor de 1,3 L ; puis en largeur, et le deuxième en longueur.

Chaque combustion tire, à son propre générateur (`KnockSound`), sa phase dans la dispersion d'un cycle à l'autre ; si elle passe la
limite, la chambre sonne à 15° après le point mort haut. La crête de l'oscillation (MAPO) vaut 0,3 bar à la limite et croît d'un
facteur e tous les 3° au-delà, 10 bar au plus (supposé ; c'est l'allure que montrent les essais). Elle s'éteint en 1 ms (supposé), sur
trois modes de poids 0,6, 0,25 et 0,15, et sa fréquence descend pendant qu'elle sonne : le gaz se détend, `f ∝ V^−(γ−1)/2`. Le bloc la
rayonne au compartiment moteur (`Source::Bay`), avec 100 dB d'atténuation entre la chambre et 1 m (ordre de grandeur des courbes
d'atténuation de structure de Priede, supposé) : 1 bar d'oscillation donne 1 Pa à 1 m, 94 dB. Elle ne passe pas par la caisse, qui ne
rayonne pas ces aigus.

Sous le capot, les carburateurs à 12:1 en quatrième, plein gaz depuis 1900 tr/min, font 249 cliquetis en 16 s, deux tiers des
combustions vers 2000 tr/min ; la bande de 5,5 à 8 kHz monte de 10 dB face au moteur d'origine, puis retombe vers 3400 tr/min, où il
cesse. Le spectrogramme en fenêtres de 3 ms montre des traits brefs à 6,7, 11 et 14 kHz, qui penchent vers le bas. **L'écoute reste à
faire** :

```
./build/revforge-render --out cliquetis.wav --preset carb_twin_cam --compression 12 --scenario turbo --seconds 20 --listener bay
./build/revforge-render --out origine.wav --preset carb_twin_cam --scenario turbo --seconds 20 --listener bay
./build/revforge-render --out capteur.wav --preset city_14 --compression 13 --scenario turbo --seconds 20 --listener cabin
./build/revforge-render --out diesel15.wav --preset diesel_four --compression 15 --scenario idle --seconds 10 --listener bay
```

### Ce qui est lu, et ce qui est exposé

Lu, à sa valeur de référence tant que les autres sujets ne sont pas branchés (`KnockMap::Inputs`) : l'air ambiant (293,15 K,
101 325 Pa), l'eau (363,15 K), la fermeture de l'admission (50° après le point mort bas) et les gaz résiduels (4 %), le refroidissement
de charge par l'injection directe ou l'eau (0), le décalage de la carte d'avance (0). La pression et la température de charge d'un moteur
suralimenté sont celles du modèle actuel à l'équilibre (`TurboSteady::coolerOutK`, ajouté, et `chargePa` ; `SuperchargerSteady`). La
richesse à pleine charge est celle de la consommation (0,88 atmosphérique, 0,82 suralimenté).

Exposé : `fuelProperties`, `energyPerAirJPerKg`, `evaporationCoolingK` (avec saturation), `compressionRatioOf`, `indicatedEfficiency`,
`compressionEfficiencyShare`, `fuelTorqueShare`, `douaudEyzatDelayS`, `endGasCycle`, `mbtAdvanceDeg`, `knockingShare`, `draperHz`,
`dieselIgnitionDelayDeg` ; `KnockMap::at` donne, pour un régime et une pression de charge, le meilleur couple, la **carte d'origine**
(`stockDeg`, à prendre comme avance demandée de référence) et la limite de cliquetis ; `DrivingModel` donne l'avance appliquée, la limite,
l'avance retirée et la part des cycles qui cliquettent (`rf_spark_advance`, `rf_knock_limit`, `rf_knock_retard`, `rf_knock_share`). La
carte n'est construite que si la combustion n'est plus celle d'origine (`knockMapOf`) : une fois branchés, l'air, l'eau, la came ou
l'avance demandée devront aussi la déclencher.

### Ce qui est supposé, et ce qui manque

- Le cycle à une zone, ses constantes (chaleur au gaz, noyau de flamme, durée de Wiebe, réchauffement par les parois, gaz résiduels) et
  la dispersion d'un cycle à l'autre sont supposés, réglés pour donner des pressions et des avances de l'ordre des vraies. Douaud et
  Eyzat ont ajusté leur loi sur des carburants de référence (isooctane et heptane) : l'indice d'octane recherche y tient lieu d'indice,
  sans la sensibilité (RON − MON) qui avantage les alcools sur les moteurs suralimentés, et la loi est extrapolée au-delà de 100.
- Le cliquetis ne coûte au couple que l'avance retirée : ni la chaleur supplémentaire aux parois, ni l'usure des pistons, ni la casse.
- L'avance que retire le capteur ne réchauffe pas l'échappement et ne lance pas le turbo, comme le ferait un vrai : c'est le calculateur
  qui donnera la température d'échappement selon l'avance.
- L'E85 et le méthanol ne brûlent pas plus vite que l'essence dans le modèle, et leur rendement propre n'est pas meilleur.
- La part du carburant vaporisée dans l'air (0,15), l'atténuation de la structure (100 dB), la crête et la durée de l'oscillation sont
  supposées : l'écoute tranchera.
- Le gaz à l'écran suit le taux de compression (son volume mort), pas le carburant ni le cliquetis ; les vues 2D et 3D ne changent pas.
- Un diesel ne refuse pas de démarrer à froid sous un taux trop bas : le réglage s'arrête à 14:1.

## Le calculateur

Le calculateur (`core/src/ecu.cpp`, `ecu.hpp`) a deux parties. Sa **carte**, l'avance et la richesse, est faite de fonctions pures que lisent le couple, la consommation, la turbine et le son. Ce qu'il **fait à chaque pas** de `DrivingModel`, le rupteur, le launch control, le passage à fond et l'antipatinage, sort par `EcuAction`, et va au son par `EcuDrive`, avec son propre générateur. Au défaut, rien de tout cela ne s'exécute : les empreintes et `check_wasm` sont inchangés, et un test vérifie qu'un tour entier sans réglage ne voit jamais le calculateur agir.

### La carte d'avance

Le couple indiqué suit une parabole autour du meilleur couple (MBT) : `T/T_MBT = 1 − (Δ/68°)²`, Δ l'écart au MBT en degrés de vilebrequin, à peu près symétrique de part et d'autre (Heywood, *Internal Combustion Engine Fundamentals*, chap. 9 : une courbe plate au sommet, quelques pour cent à ±10°). C'est la parabole de l'anti-lag : 2,2 % à 10°, 8,7 % à 20°. **Supposé :** la carte d'origine est au MBT. Le cliquetis (sujet « combustion ») l'en éloignera : `knockRetardDeg` est déjà un paramètre de `demandedAdvanceDeg`, `mapRetardDeg` et `ecuFullLoadShare`, à 0 tant qu'il n'est pas branché. Le décalage `--advance` de ±10° ne peut donc ici que perdre du couple ; l'avance ne gagne qu'en sortant d'un cliquetis. Intégration : sur un quatre-temps à étincelle, la carte d'origine et la part de couple du décalage viennent maintenant de la carte du cliquetis (voir « Quand les réglages se rencontrent (2) ») ; la parabole reste celle du carburant, de la chaleur du gaz, d'un diesel et d'un deux-temps.

L'avance demandée, en degrés avant le point mort haut (`demandedAdvanceDeg`), part de `mbtAdvanceDeg` : l'étincelle qui place la moitié de la combustion vers 8° après le point mort haut. De l'étincelle à cette moitié, 34° à 3000 tr/min plein gaz, × (N/3000)^0,25 (la turbulence suit le piston : la durée en degrés croît peu), × (1 + 0,6 (1 − remplissage)) à faible charge. Il en sort 26° à 3000 tr/min plein gaz, 32° à 6000, 31° à mi-charge à 1500 ; des cartes publiées d'atmosphériques donnent 25 à 35° à pleine charge. Ordre de grandeur, supposé : rien d'autre ne le lit encore que la fiche.

Le travail que le piston ne reçoit pas part dans le gaz, moins 20 % dans les parois : `ΔQ_gaz = η_i (1 − T/T_MBT) × 0,8` de l'énergie du carburant, η_i = 0,405 (0,375 suralimenté), le rendement de `ConsumptionModel`. Du côté avancé, ce travail part dans les parois, pas dans le gaz. **Limite :** près du MBT, la hausse de température d'échappement est sans doute sous-estimée, puisque la baisse des pertes aux parois avec le retard n'est pas comptée (Heywood, chap. 12) : +17 K pour 10° de retard à pleine charge, quand on cite souvent quelques dizaines.

### La richesse

`--lambda` règle la richesse à pleine charge ; 0 garde celle d'origine, 0,88 (0,82 suralimenté), celle de `ConsumptionModel`. Le couple par kilogramme d'air suit la courbe que lit déjà le carburateur (`torqueOfRichness`, d'après Heywood, chap. 5) : un sommet plat vers λ 0,87, puis −6,3 % à λ 1, −12,7 % à λ 1,1 et −3,4 % à λ 0,70 face à λ 0,88. La richesse ne s'écarte de la boucle fermée (λ 1) qu'au-dessus de 85 % de remplissage, la rampe de la consommation : le couple à charge partielle ne change pas. Un carburateur dose par ses gicleurs, un diesel brûle toujours en excès d'air : le réglage ne leur fait rien, et le site le cache.

L'échauffement du gaz suit la température de flamme adiabatique d'un hydrocarbure dans l'air à 298 K (tables usuelles, ordre de grandeur) : un sommet vers φ = 1,05, −2 % à λ 0,88, −6 % à 0,82, −14 % à 0,70. **Limite :** sur un vrai moteur, enrichir refroidit davantage l'échappement, en partie parce que cela permet d'avancer sans cliquetis : cette part reviendra au cliquetis.

### Ce que la carte change

Le couple de pleine charge (`fullLoadTorqueNm`, donc les courbes, la fiche et la conduite) est multiplié hors frottements : `(T + T_f) × part − T_f`. La consommation suit l'air : à air égal, une étincelle décalée tire moins de chaque gramme, donc le rendement de `ConsumptionModel` est multiplié par la même part, et la richesse règle ce qu'il faut de carburant par gramme d'air. La turbine reçoit le gaz plus chaud ou plus froid (`TurboSpec::ecu`, le turbo restant celui d'origine), et le son ouvre la soupape sur autant de pression en plus (`evoRatio`, le même bilan que l'anti-lag).

Mesuré sur `hot_hatch_na` (2,0 L, 7300 tr/min) ; consommation en WLTC, à chaud :

| | Origine | Avance −10° | −5° | +5° | λ 1,0 | λ 0,80 | λ 0,75 | λ 1,1 |
|---|---|---|---|---|---|---|---|---|
| Couple maximal (Nm) | 194,1 | 189,2 | 192,9 | 192,9 | 179,7 | 194,0 | 191,3 | 165,5 |
| Puissance maximale (kW) | 117,8 | 114,5 | 117,0 | 117,0 | 108,2 | 117,7 | 115,9 | 98,6 |
| g/kWh à la puissance maximale | 304 | 313 | 306 | 306 | 291 | 334 | 362 | 290 |
| Gaz à la sortie des cylindres, puissance maximale | 863 °C | +17 K | +4 K | 0 | +20 K | −41 K | −69 K | −18 K |
| WLTC (L/100 km) | 9,07 | 9,30 | 9,13 | 9,13 | 9,05 | 9,07 | 9,07 | 9,03 |
| 0-100 km/h (s) | 8,72 | 8,82 | 8,75 | 8,75 | 9,14 | 8,72 | 8,80 | 9,60 |

Sur `hot_hatch_turbo` : λ 1 retire 6,9 % de couple (0-100 en 7,15 s au lieu de 6,86), 10° de retard 2,3 % et 1,9 % de carburant en WLTC (9,07 L/100 km au lieu de 8,90).

λ 0,80 ne change presque rien au couple, mais coûte 10 % de carburant de plus à la puissance maximale ; λ 1 en économise 4 %, pour 8 % de puissance en moins. Le WLTC touche rarement la pleine charge : la richesse n'y change presque rien, l'avance si.

### Le rupteur

- **D'origine (`stock`), le comportement d'avant ce calculateur, inchangé.** Au régime maximal, la combustion est coupée, injection et étincelle (un carburateur, lui, continue de débiter), pendant 30 ms au moins, et ne reprend que 2,6 à 5,4 % plus bas : 4 % × (0,65 + 0,7 u), u tiré d'un générateur à congruence à chaque reprise, pour que deux rebonds ne soient jamais identiques. Le papillon reste ouvert : peu de pompage, pas de décharge du turbo. Au son, la pression des bouffées tombe de 95 %, et tout ce qui suit le débit avec elle (voir « Le rupteur », plus haut).
- **Progressif (`soft`).** Sur les 3 % sous le régime maximal, l'étincelle recule jusqu'à 15° (5 % de couple) ; au-delà, une part croissante des cylindres est coupée, tous à +2 % : une coupure d'injection, explosion par explosion, dans l'ordre d'allumage (au son, un accumulateur qui en saute la part voulue). Sur un 2,0 L au point mort, pied au plancher, il tient à +1,4 % sans une seule coupure franche, avec 40 % des explosions.
- **Coupure d'allumage (`ignition_cut`).** Le même rebond que l'origine, mais seule l'étincelle est coupée : le carburant est toujours injecté. Le mélange du cylindre coupé arrive au collecteur, où 45 % en brûle si le gaz y dépasse 800 K, entièrement à 1100 K : les chiffres de la coupure de cylindres de l'anti-lag. Sur un turbo, cette chaleur va à la turbine ; sur un atmosphérique, c'est le gaz à la sortie des cylindres, suivi avec la même inertie des parois que celui de la turbine (0,35 s pour chauffer, 0,9 s pour refroidir), qui dit s'il s'enflamme. Au son, chaque ouverture d'un cylindre coupé a 45 % × allumage de chances de détoner, à l'amplitude √(E/90 J) d'une détonation ordinaire, E l'énergie de ce qui brûle : environ 760 J par cylindre sur un 2,0 L à 7200 tr/min, deux fois celle d'un cylindre coupé par l'anti-lag. Trois coups de gaz au rupteur sur un 2,0 L : 23 détonations contre aucune, et 47 mL d'essence contre 25 en 14 s. **Pas fait :** le gaz à l'écran montre cette coupure comme celle d'origine, des bouffées qui n'ont pas brûlé, sans la flamme du collecteur.

Un diesel n'a pas d'étincelle : sa coupure d'allumage redevient la coupure d'injection d'origine, et le progressif n'y coupe que l'injection. Le régime du rupteur passe par `limiterRpmFor` : le régime maximal, ou 2 % sous l'affolement des soupapes s'il arrive avant (sujet « culbuteurs », 0 tant qu'il n'est pas branché).

### Le launch control

Il s'arme pied au plancher (90 %), voiture arrêtée, moteur désaccouplé : embrayage enfoncé, ou boîte pilotée qui garde l'embrayage ouvert tant que le pied reste sur le frein. Il tient le régime choisi par la coupure d'allumage (même rebond), avec 25° de retard (supposé : l'ordre de grandeur des calculateurs de préparation). Le retard et le mélange qui brûle au collecteur chauffent la turbine : à l'arrêt, la pression monte. Il lâche dès que la voiture roule (1 m/s) ou que l'embrayage a rattrapé le moteur : l'embrayage se ferme alors depuis ce régime, sur tout le couple. Le 0-100 de la fiche s'en sert : trois secondes de frein et de pied au plancher, chronomètre au lâcher du frein. `--scenario launch_control` fait le même départ et écrit ses chiffres.

| 0-100 km/h de la fiche, chronomètre au lâcher du frein | Sans | Launch control |
|---|---|---|
| `hot_hatch_turbo` (2,0 L turbo, quatre roues motrices), à 4500 tr/min | 6,86 s (pneu à sommet : 6,82) | 6,13 s (6,16) |
| V8 de 7,0 L atmosphérique, propulsion, 1700 kg, à 3500 tr/min | 6,18 s (pneu à sommet : 8,55 ; 6,25 avec l'antipatinage sport) | 6,03 s (8,40 ; 6,08) |

À l'arrêt, `hot_hatch_turbo` à 4500 tr/min : 4406 tr/min et 1,03 de la pression demandée au lâcher du frein, le gaz avant la turbine jusqu'à 1129 °C (899 sans launch control), 27 détonations en trois secondes.

**Limite :** sans launch control, la boîte pilotée laisse déjà l'embrayage patiner contre les freins (à 2350 tr/min sur `hot_hatch_turbo`), ce qui charge aussi le turbo : ce départ en force, irréaliste pour un embrayage sec, donne même 5,94 s depuis le lâcher, contre 6,10 avec le launch control. C'est le comportement d'avant, gardé. De même, frein serré, les roues motrices patinent (le frein agit sur la caisse) : l'antipatinage y coupe le moteur.

### Le passage à fond

À la montée, pied au-delà de 80 %, sur une boîte à synchros ou à crabots que pilote le robot : le pied n'est plus levé. Le calculateur coupe l'étincelle, le carburant passant toujours, tant que le moteur dépasse le régime du rapport suivant : il l'y tient pendant le passage. Papillon ouvert, pas de vide : la soupape de décharge reste fermée, et le mélange qui brûle au collecteur garde la turbine lancée. Sur une séquentielle, la coupure d'origine laisse aussi passer le carburant. Une double embrayage ou un convertisseur passent déjà sous charge : rien n'y change. Sur `hot_hatch_turbo`, plein gaz : la pression au plus bas pendant les passages reste à 0,99 à 1,02 de la pression demandée au lieu de tomber à 0,16 à 0,23 ; 150 km/h en 13,3 s au lieu de 13,8 ; 0-100 en 6,54 s au lieu de 6,86 ; une à quatre détonations par passage.

### L'antipatinage

Une régulation proportionnelle et intégrale du glissement des roues motrices que calcule déjà `DrivingModel` (m/s), vers la part du couple à retirer : `u = kp e + ∫ ki e dt`, ses gains placés sur la transmission du rapport engagé (« Le pneu, l'antipatinage et le départ », plus bas ; avant les finitions, 0,1 et 1,0 réglés à l'essai). L'intégrale ne grossit pas dans le sens où la commande sature (anti-emballement) : sans cela, une fois les roues reprises, elle tenait le moteur coupé jusqu'à 550 tr/min. Le retard d'abord, jusqu'à 25° (13,5 % de couple), puis des cylindres coupés, à l'injection. Sur une voiture électrique, le couple du moteur directement. Il n'agit que roues tenues par le moteur, ou au départ, quand le robot ne serre l'embrayage que jusqu'au couple annoncé : un embrayage qui patine ne transmet que sa capacité, et couper l'allumage ne ferait sinon que caler le moteur.

**La cible, et ce que le pneu en fait.** Depuis les finitions, le pneu a un sommet d'adhérence (Pacejka, vers 11 % de glissement) : la cible est un glissement κ, le sommet en sport, 90 % du sommet en complet, et les gains se placent sur la transmission du rapport engagé. Voir « Le pneu, l'antipatinage et le départ », plus bas, qui remplace ce qui suit. Le glissement moyen, une fois l'embrayage serré, tient à ces cibles à 25 % près (testé).

| 0-100 km/h de la fiche | Sans | Sport | Complet |
|---|---|---|---|
| V8 de 7,0 L, propulsion, ancien pneu sans sommet | 6,18 s | 6,20 s | 6,86 s |
| Le même, pneu à sommet | 8,55 s | 6,25 s | 6,48 s |
| `hot_hatch_turbo`, quatre roues motrices | 6,82 s | 6,82 s | 6,82 s |

Le pneu à sommet de l'essai de l'intégration (`sin(1,4 atan(0,92 s))`, s en m/s) donnait 6,38, 5,86 et 6,10 s. Le 2,0 L turbo à quatre roues motrices ne glisse jamais jusqu'à ses cibles dans un départ ordinaire.

### Le son, et ce qui reste à écouter

Tout passe par `setEcu` : la pression à l'ouverture des cylindres allumés (retard, richesse), les explosions sautées de la coupure partielle, et les détonations d'un cylindre dont seule l'étincelle est coupée, par le chemin des `pops`. Mesuré en coups de gaz (2,0 L) : au rupteur, la bande 30-120 Hz gagne 2,6 dB avec la coupure d'allumage ; le progressif efface les rayures du rebond sur le spectrogramme. Au launch control, la coupure en rafales s'y voit comme une suite de bandes verticales, à l'arrêt. **Rien n'a été écouté :** la chance de détoner, l'énergie qui brûle et la part de 90 J sont reprises de l'anti-lag, et seule l'écoute dira si ces détonations sonnent comme une vraie coupure d'allumage. Commandes : voir le compte rendu, ou `revforge-render --out x.wav --cylinders 4 --cc 2000 --bore 86 --redline 7200 --exhaust sport --limiter ignition_cut --scenario blips`, `--preset hot_hatch_turbo --launch 4500 --scenario launch_control`, `--preset hot_hatch_turbo --flat-shift 1 --scenario launch --seconds 16`.

### Ce qu'il lit, et ce qu'il expose

| Grandeur | Fonction | Valeur tant que rien n'est branché |
|---|---|---|
| Lue : avance retirée par le cliquetis (combustion) | paramètre `knockRetardDeg` | 0 |
| Lue : régime d'affolement des soupapes (culbuteurs) | `limiterRpmFor(redline, valveFloatRpm)` | 0 : le régime maximal |
| Lue : température d'eau (thermique) | `warmupRichness(coolantC)`, `demandedLambda(…, coolantC)` | 90 °C : aucun enrichissement |
| Lue : air ambiant (atmosphère) | gaz à la sortie des cylindres, `kPortIntakeK` | 293 K |
| Exposée : avance demandée (° avant PMH) | `demandedAdvanceDeg(e, rpm, load, knock)`, `mbtAdvanceDeg` | |
| Exposée : richesse demandée | `demandedLambda(e, load, coolantC)`, `fullLoadLambdaOf` | |
| Exposée : couple et chaleur selon l'avance et λ | `sparkTorqueShare`, `sparkExhaustGain`, `lambdaTorqueShare`, `lambdaHeatShare`, `ecuFullLoadShare` | |
| Exposée : ce qu'il fait au pas | `DrivingModel::ecuAction()`, `ecuAdvanceDeg()`, `ecuLambda()` | |

Télémétrie : `rf_ecu_advance`, `rf_ecu_lambda`, `rf_ecu_fired`, `rf_ecu_limiter`, `rf_ecu_launch`, `rf_ecu_flat`, `rf_ecu_traction` (voyants et tableau des grandeurs) ; fiche : `rf_ecu_full_share`, `rf_ecu_spark_deg`, `rf_ecu_exhaust_c`. `--trace` en écrit les colonnes.

## L'injection

`FuelSystem::Direct` (injection directe d'essence), `DieselInjection` (pompe mécanique, la référence, ou rampe commune), `egr` et `particulateFilter`. `core/src/injection.cpp`, `core/src/particulate_filter.cpp`, `core/src/injector_sound.cpp`. Au défaut (injection indirecte, pompe mécanique, sans recirculation ni filtre), rien de ce qui suit ne s'exécute : les 29 empreintes d'origine ne bougent pas. Tout est écrit en fonctions pures qui reçoivent la température d'eau, le carburant et l'air de charge en paramètres, à leur valeur de référence tant que leurs agents ne sont pas branchés.

**Refroidissement de la charge.** Vaporiser le carburant prend sa chaleur latente : `ΔT = (m_f/m_a)·L_v / (c_p,air + (m_f/m_a)·c_p,vap)`, avec `m_f/m_a = 1/(AFR·λ)`. Pour une essence RON 95 (L_v = 305 kJ/kg, Heywood, annexe D), 18,5 K à λ = 1, 20,8 K à λ = 0,88. La question est de savoir à qui cette chaleur est prise. Une injection indirecte vise la soupape fermée : l'essentiel vient de la soupape et du conduit (Anderson et al., SAE 962018), 20 % de l'air (supposé). Une injection directe pulvérise dans l'air du cylindre : 70 % (supposé, environ 30 % du jet atteint le piston et la chemise, Zhao, Lai et Harrington, 1999). La loi en d² dit si les gouttes ont le temps : diamètre de Sauter de 13 µm à 200 bar (en p^−0,25), K = 5·10⁻⁸ m²/s, 170° entre le début d'injection et la fermeture de l'admission. À tout régime d'une voiture, tout est vaporisé avant la fermeture. D'où, à pleine charge, 14,5 K de moins à la fermeture de l'admission (4,2 K en indirecte), et 29 K au point mort haut d'une compression effective de 9 (`chargeCooling`, pour le cliquetis). Le remplissage gagne `T/(T − ΔT) − 1` sur une charge à 323 K, soit 3,3 % : Anderson et al. mesurent 2 à 3 % face à une injection indirecte de série, Wyszynski et al. (SAE 2002-01-0839) 9 % sur un monocylindre. `fullLoadTorqueNm` l'applique au travail des gaz, pas aux frottements.

**La pompe haute pression.** Une pompe à piston, entraînée par une came de l'arbre : `C = Q_carburant·p_rampe/(η·ω)`, η = 0,8 (supposé). La rampe va de 40 bar au ralenti à 200 bar à pleine charge (une génération Bosch HDEV5). 0,38 Nm à 4000 tr/min sur un 2 L : la combustion la paie comme un frottement, et la courbe de la fiche la retire.

**Le tic.** Chaque injecteur frappe deux fois par injection : l'induit sur sa butée à l'ouverture (60 % de la force, supposé), l'aiguille sur son siège à la fermeture, poussée par son ressort et la rampe, d'une amplitude en √(p/200 bar). L'injection commence 60° après le point mort haut d'admission ; sa durée est `m/(ρ·Cd·A·√(2Δp/ρ))`, avec un injecteur de 15 cm³/s à 100 bar pour un cylindre de 500 cm³ : 0,4 ms au ralenti, 2,5 ms à pleine charge au régime maximal. La pompe claque à chaque course de sa came (autant de bossages que de cylindres jusqu'à quatre, trois sur un six, quatre au-delà), calée pour finir 30° avant l'injection. Des chocs de 0,1 ms (bruit, jamais deux identiques) sur deux modes de la culasse, 6,3 et 9,8 kHz, et un demi-sinus de 0,3 ms sur deux modes de la rampe, 1,6 et 3,4 kHz : sans ce contact plus mou, la pompe débordait sur la bande des injecteurs. Les bandes viennent de SAE 2011-01-0930 : des contre-mesures sur un moteur à injection directe y ont retiré 3 dB au-dessus de 5 kHz (injecteurs suspendus) et de 5,8 à 7,5 dB(A) vers 1,6 kHz (pompe). Les niveaux sont calés là-dessus, face au plancher du bloc au ralenti, sous le capot : +5,8 à +7,0 dB dans le tiers d'octave de 1,6 kHz, +2,5 à +3,6 dB au-dessus de 5 kHz, sur trois préréglages. C'est une source du compartiment moteur (`stem(Source::Bay)`), à son propre générateur, coupée avec l'injection au lever de pied et au rupteur. Elle garde sa place : derrière la voiture, moins de 0,1 dB nulle part ; à 3000 tr/min sous le capot, moins de 1,4 dB ; le rapport 8-16 kHz sur 500-2000 Hz bouge de −0,9 à +1,5 dB au ralenti sous le capot. LAeq sous le capot au ralenti, `hot_hatch_na` : 64,2 dB(A) en indirecte, 67,1 en directe.

**Le film d'une injection indirecte.** Le modèle x-τ d'Aquino (SAE 810494), partagé avec le carburateur (`WallFilm`). La part déposée `X = 0,55·(1 − E(T_éq))` : 55 % du jet touche la soupape et le conduit (supposé), moins ce qui distille d'un coup sur la paroi. `E` est la courbe de distillation d'une essence EN 228 (E70 33 %, E100 56 %, E150 88 %, au milieu des classes d'été), lue à la température que la paroi aurait à la pression atmosphérique pour la même évaporation : `1/T_éq = 1/T_paroi + ln(p/p₀)/B`, B = L_v·M/R = 3668 K (Clausius-Clapeyron, M = 0,1 kg/mol). La paroi est à l'eau plus 15 K (supposé). Le film repart en s'évaporant, `τ_év = 0,25 s·exp(B(1/T − 1/T_réf))·p/p₀`, ou entraîné par l'air, `τ_air = 2 s·(50/v)^0,75` : `1/τ = 1/τ_év + 1/τ_air`. À chaud et à pleine charge, X = 0,22 et τ = 0,23 s ; à 20 °C, X = 0,53 et τ = 1,3 s ; sous vide (0,3 bar), X tombe à 0,05. Le calculateur compense par le modèle inverse, `injecté = (voulu − film_estimé/τ̂)/(1 − X̂)`, avec X̂ et τ̂ de sa calibration à 90 °C. À 90 °C il tombe juste à l'accélération ; au lever de pied, il coupe tout mais ne peut pas retirer ce qui s'évapore de la paroi. À froid, le mélange s'appauvrit à l'accélération (moins de 0,85 de la richesse voulue) et s'enrichit au lever de pied (plus de 1,1) ; le couple suit `torqueOfRichness`, celle du carburateur. `DrivingModel` lit la température d'eau par `setCoolantC`, 90 °C tant que l'agent thermique ne la donne pas : à 90 °C, le film ne s'exécute pas, et le modèle reste au bit près. Le carburateur reçoit la même correction à froid (`coldFilmShare`), rien à 90 °C. L'injection directe n'a pas de film.

**Le diesel.** `mechanical` est le diesel d'origine, au bit près. La combustion se calcule point par point (`dieselBurn`). Délai d'allumage de Hardenberg et Hase (1979, Heywood éq. 10.37) : `τ(°V) = (0,36 + 0,22·S̄p)·exp[E_A(1/(R̃T) − 1/17190) + (21,2/(p − 12,4))^0,63]`, `E_A = 618 840/(IC + 25)`, au point mort haut d'une compression polytropique (n = 1,35, ε = 18), gazole EN 590 (indice de cétane 51). La pompe mécanique refoule par sa came, le même volume par degré à tout régime ; sa pression en découle, de 150 à 850 bar (supposé). Ce qui est injecté pendant le délai brûle d'un coup, en triangle de 0,8 ms (Heywood : de 5 à 10°). La rampe commune débite par un orifice, de 250 à 1600 bar selon la charge et le régime ; une injection pilote de 1,5 mg (pour 500 cm³, de 1 à 2 mg publiés) brûle 12° avant, et la principale arrive dans sa flamme : il ne reste que le délai physique d'un jet qui se brise, `t = 28,65·ρ_l·d/√(ρ_a·Δp)` (Hiroyasu). Le reste brûle en diffusion (Wiebe, m = 1), sur la durée de l'injection plus un mélange de 30°·√(800 bar/p), en degrés parce que le tourbillon suit le régime (Heywood : de 40 à 60° à tout régime). La pente de pression est `(γ − 1)·(dQ/dθ)/V` à chaque pic (pilote, prémélange, diffusion), là où il tombe autour du centre de gravité, réglé à 8° après le point mort haut. Au ralenti du 2 L de série : 5,2 bar/° à la pompe, 2,5 bar/° en rampe commune (de 5 à 10 et de 2 à 5 bar/° publiés), soit 6,4 dB de moins. Le claquement (`knockEnv`) et la loi d'Anderton du bloc diesel prennent cet écart, tiré de 81 points de charge et de régime à chaque changement de moteur. LAeq au ralenti : 78,0 → 73,3 dB(A) sous le capot (−4,7), 45,3 → 42,5 derrière (−2,8) ; PSA annonce 3 dB pour la pilote, Denso 6,5 dB au ralenti pour sa rampe à 1800 bar. À pleine charge, la diffusion plus rapide donne de 2 à 4 dB de plus que la pompe : un résultat du modèle, non vérifié. Le rendement suit le degré d'isochore, `η(θc) + ½·η''(θc)·σ²`, σ² l'étalement de la combustion. Le couple à pleine charge suit la limite de fumée, `λ_min = 1 + c/√p`, avec λ = 1,43 à 600 bar : l'excès d'air de la pleine charge d'origine (8,5 bar de PME plus 2 bar de frottements à 46 % de rendement indiqué).

| `diesel_four`, 2 L turbo | Couple | Puissance | 0-100 | WLTC | Meilleure CSE | Ralenti, capot |
|---|---|---|---|---|---|---|
| Pompe mécanique | 315,1 Nm | 113,5 kW | 12,94 s | 7,58 L | 196 g/kWh | 78,0 dB(A) |
| Rampe commune | 364,6 Nm | 124,1 kW | 12,08 s | 7,54 L | 189 g/kWh | 73,3 dB(A) |
| Recirculation | 315,1 Nm | 113,5 kW | 12,94 s | 7,55 L | 196 g/kWh | |
| Filtre à particules | 313,4 Nm | 112,4 kW | 13,04 s | 7,62 L | 196 g/kWh | |

La rampe commune donne +29 % de couple à 1000 tr/min, +22 % à 1500, +9 % à 4000 : Denso annonce +35 % de couple et +24 % de puissance face à son système précédent, PSA passe de 196 Nm (1.9 TD) à 250 Nm (2.0 HDi 110). À charge partielle, la pilote brûle tôt : de 1,5 à 3,6 % de plus en consommation spécifique, à calage égal. Le modèle ne crédite pas le calage que le calculateur optimise, la principale source du gain réel.

**La recirculation.** Une part des gaz d'échappement dans la charge, selon la charge et le régime : jusqu'à 15 % sur un essence (EGR externe refroidi), entre 30 et 55 % de charge, rien au ralenti ni à pleine charge ; jusqu'à 30 % sur un diesel à faible charge, rien au-delà de 75 % ; rien au-dessus de 85 % du régime maximal (supposés, dans les plages publiées). Sur un essence, la vitesse de flamme laminaire suit Metghalchi et Keck (1982), `S_L ∝ 1 − 2,06·x^0,77` (0,52 à 15 %), la combustion s'allonge en `S_L^−1/3` (×1,24), et le bruit de combustion baisse d'autant (−1,9 dB). La flamme diluée est plus froide de `x·ΔT_ad` : les pertes aux parois baissent selon Woschni (`h ∝ T^−0,53`, 22 % de l'énergie aux parois à charge partielle), et pour le même air frais le collecteur est plus plein de `1/(1 − x)` : moins de pompage. Consommation spécifique à 2000 tr/min et 2 bar : −2,7 % (de 2 à 5 % publiés) ; WLTC de la citadine : 6,76 → 6,56 L (−3 %). Sur un diesel, seul l'oxygène en moins (`1 − x/λ`) refroidit la flamme et ralentit la diffusion : la consommation ne bouge presque pas (−0,3 %), le bruit baisse de 0,4 à 0,7 dB. Un rotatif n'en a pas : il garde ses gaz brûlés d'une chambre à l'autre.

**Le filtre à particules.** Sur un diesel ou une injection directe d'essence. Un monolithe de 1,25 fois la cylindrée, 15 cm de long, 0,8 m² de parois par litre. La contre-pression suit Darcy dans la paroi (0,3 mm, k = 10⁻¹²) et le gâteau de suie (100 kg/m³, k = 3·10⁻¹⁴, Konstandopoulos et al.), plus l'entrée des canaux : 3,7 kPa propre, 16 kPa à la suie moyenne, 26 kPa plein, à pleine charge à 4000 tr/min (de 2 à 5 et de 15 à 30 kPa publiés). Le piston la paie, `Δp·V/4π` : −1,7 Nm et −1,1 kW au sommet, la combustion ne la voit pas. La suie suit un indice d'émission qui monte vers la limite de fumée (de 0,4 à 6 g/kg) ; le calculateur d'un diesel demande la régénération à 7 g/L et l'arrête à 1 g/L. La post-injection brûle dans le catalyseur d'oxydation et porte le gaz à 620 °C : `ṁ_carburant = ṁ_gaz·c_p·ΔT/(PCI·0,95)` ; la suie brûle selon Arrhenius (150 kJ/mol). À 2000 tr/min et 35 % de charge, une régénération dure 4 min 43 et coûte 190 g de gazole ; ce point remplit le filtre en une dizaine d'heures : de l'ordre de 0,5 % de surcoût moyen (de 1 à 3 % publiés). Ce carburant passe par `extraFuelGps`, comme celui de l'anti-lag : la consommation le lit, rien n'en revient. Un filtre d'essence se régénère seul, en coupure d'injection sur un filtre chaud. Le filtre démarre à mi-chemin entre deux régénérations, sa moyenne : une régénération ne se voit pas dans un trajet ordinaire. Dans la ligne, il prend place après le catalyseur : ses canaux perdent par viscosité et conduction (Kirchhoff, 0,0675 Np/m/√Hz à 0,55 mm de rayon), et ses parois sont une résistance en série, 0,20 ρc/S à la suie moyenne. −2,9 dB de 100 à 500 Hz, −4,9 de 500 à 2000, −5,7 de 2 à 8 kHz. Le diesel d'origine a déjà 6 dB de moins à l'échappement (`kDieselFilter`), attribués à « une turbine et un filtre » : avec ce réglage, une partie peut être comptée deux fois. À trancher par l'écoute et l'intégration ; le défaut ne change pas.

**Ce que je lis, ce que j'expose.** Je lis la température d'eau (`setCoolantC`, `PortFilmInput::coolantC`, `CarburetorInput::coolantC`, 90 °C par défaut), le carburant (`InjectedFuel`, `DieselFuel`, RON 95 et EN 590 par défaut), l'air d'admission (`intakeAirK`) et la pression de charge (`DieselPoint::intakeBar`). J'expose `chargeCooling` (refroidissement à la fermeture de l'admission et au point mort haut, pour le cliquetis), `vaporizationDropK`, `directFillGain`, `dieselBurn` (délai, prémélange, pente de pression, rendement), `smokeLimitLambda`, `egrRate`, `egrEfficiencyShare`, `exhaustStateOf` (débit, température et richesse des gaz) et `ParticulateFilter` (suie, contre-pression, régénération).

**Simplifications et limites.** Pas d'injection directe stratifiée ni de double injection ; pas de film à froid sur le piston d'une injection directe ; pas de NOx, de suie à l'écran, de niveau d'huile diluée par la post-injection. La rampe commune n'a ni multiples injections pilotes, ni régulation de la pression de rampe ; le tic de ses injecteurs n'est pas simulé, son claquement le couvre. La pompe mécanique n'est pas distinguée entre pompe en ligne, distributrice ou injecteur-pompe. Le filtre ne change pas la pression à la sortie de la turbine. Le gaz et les vues à l'écran ne montrent ni injecteur, ni filtre, ni recirculation. Les sources sont citées de mémoire : Heywood (1988, chapitres 7, 9 et 10), Zhao, Lai et Harrington (*Prog. Energy Combust. Sci.*, 1999), Anderson et al. (SAE 962018), Wyszynski et al. (SAE 2002-01-0839), Aquino (SAE 810494), Hardenberg et Hase (SAE 790493), Hiroyasu et Arai (1990), Metghalchi et Keck (*Combust. Flame*, 1982), Konstandopoulos et al. (SAE 2000-01-1016), SAE 2011-01-0930 ; les chiffres de PSA et de Denso viennent de leur communication. À recouper avant de les citer ailleurs. L'écoute reste à faire.

## La came

Le profil de came, le calage variable et ce qu'ils font au remplissage, au ralenti et au son : `core/src/cam.cpp`, recopié pour le dessin et le gaz à l'écran dans `web/src/lib/cam.ts`. La came d'origine ne passe par aucun de ces calculs (`camIsStock`) : les 29 empreintes n'ont pas bougé. Sur un rotatif et un deux-temps, il n'y a pas de came, et ses réglages sont ignorés.

**La loi de levée.** L(θ) = Lmax · g(x), avec x = (θ − axe)/demi-durée siège à siège et g(x) = (1 − x²)²(1 − x⁴) = 1 − 2x² + 2x⁶ − x⁸. C'est le polynôme 3-4-5-6 d'une came à simple arrêt, (1 − x²)³ (Norton, *Cam Design and Manufacturing Handbook*, de mémoire), rempli par (1 + x²) : levée, vitesse et accélération sont nulles au siège, donc continues avec le cercle de base. Ce remplissage donne une durée à 1,27 mm sur durée à 0,15 mm de 0,84 à 0,86, comme les catalogues américains (0,83 à 0,84, de mémoire), et une décélération au nez de 64 % de l'accélération d'ouverture maximale. g″(0) = −4, d'où la décélération au nez `camNoseDecelerationMs2` = 4·L·ω²/demi-durée², en carré du régime : 313 g à 7000 tr/min sur la came d'origine d'un 2 L. Un jeu de 0,25 mm (supposé) reste à rattraper avant que la soupape décolle.

**Le profil.** Les réglages se lisent comme une fiche européenne, à 1 mm de levée : durée, levée à la soupape, angle entre cames. L'axe d'admission est à l'angle entre cames après le point mort haut (came calée « droite »), l'échappement autant avant. La came d'origine, 240° à 1 mm, 110° entre cames, levée de 0,12 fois l'alésage (celle de `valvetrain.cpp`), ouvre l'échappement à 130°, le ferme à 370°, ouvre l'admission à 350° et la ferme à 590° : exactement les `kEvo`… du son, qui gardent leur valeur. Siège à siège, elle tient 308°. Une levée trop faible pour une durée trop longue ne peut pas tenir : le bossage, siège à siège, est borné à 420° pour ne jamais couvrir le point mort haut d'allumage, et la durée à 1 mm se lit à ce qui en reste (260° au plus à 5 mm de levée). Sous les 0,25 mm de jeu, la soupape ne décolle pas.

**Le remplissage, calculé.** Un cylindre à zéro dimension (masse, énergie, part de gaz brûlés), sur trois cycles, au demi-degré sous 3000 tr/min et au degré au-dessus :

- les soupapes en orifice compressible, section efficace Cd·(1/A_jupe² + 1/A_gorge²)^(−1/2), Cd = 0,7, jupe π·d·L, gorge 0,636·d², les diamètres de `valveTrainOf` ;
- l'admission : un conduit coulé de 0,15 fois la cylindrée unitaire, puis une colonne d'air de la section des gorges, qui a de l'inertie, ρ·L·du/dt = p_plénum − p_conduit − ½ρK·u|u| − ζρc·u, K = 1 et ζ = 0,15 (supposés) ; ce qui reflue contre la soupape y reste en bouchon et revient le premier ;
- l'échappement : 0,3 fois la cylindrée unitaire, puis un tube primaire de 0,62 m, ζ = 0,25, vers une contre-pression de 8 kPa au régime nominal, en carré du régime (supposé, ligne d'origine) ;
- la combustion, d'un coup au point mort haut : 0,75 × 2,9 MJ par kg d'air enfermé, soit 5 bar à l'ouverture de l'échappement, comme `tools/valve_filling.py`.

Le conduit est accordé pour la came d'origine : sa longueur sort de la résonance de Helmholtz avec le cylindre à mi-course, f = c/2π·√(A/(L·V)), placée à 2,5 fois la fréquence de rotation au pic de couple du modèle de base (Engelman, de mémoire : de 2 à 3). Rien n'y est ajusté à la came. On lit le travail du cycle, ∮p·dV : la pression moyenne indiquée nette, où comptent l'air enfermé, la détente perdue par une ouverture précoce et le pompage. `breathing` en reçoit le rapport à la came d'origine, frottements tenus à part, comme pour le carburateur : ((pme + pmf)·r − pmf)/pme. Quatorze régimes, de 600 tr/min à 1,25 fois le régime maximal, chacun partant de l'état laissé par le précédent ; un moteur coûte de 10 à 80 ms en natif, de 40 à 130 ms en WebAssembly (mesuré sur une machine chargée), et quatre sont gardés en mémoire. Le turbo, le compresseur et la boîte restent ceux de la came d'origine, comme pour le filtre.

**La fermeture de l'admission place le meilleur remplissage.** Fermée tard, la soupape laisse refluer ce qui est entré quand le piston remonte lentement, et garde ce que la colonne pousse encore quand il remonte vite. **La compression effective** se lit au même point : ε_eff = 1 + (ε − 1)·V(IVC)/V_d, bielle de 3,2 rayons de manivelle : 9,25 sur la came d'origine à 10,5, 8,3 sur une came de course (fermeture à 66° après le point mort bas). **Le croisement** laisse repasser le gaz brûlé, au ralenti surtout.

| 2 L, 86 × 86, 6800 tr/min, compacte de 1200 kg | Couple maximal | Puissance | 0 à 100 km/h | WLTC (`hot_hatch_na`) | Ralenti : collecteur, résiduels, COV |
|---|---|---|---|---|---|
| Origine, 240°/10,3 mm/110° | 173,4 Nm à 4100 | 96,8 kW à 6250 | 9,53 s | 9,07 L | 38,9 kPa, 15 %, 3,0 % |
| Route rapide, 265°/11/110° | 162,8 Nm à 4850 | 95,5 kW à 6550 | 10,10 s | 8,96 L | 44,5 kPa, 21 %, 5,2 % |
| Course, 280°/12/106° | 150,8 Nm à 4850 | 91,7 kW à 6800 | 10,60 s | 8,85 L | 49,4 kPa, 31 %, 20 % |
| Origine, rupteur à 8000 | 193,1 Nm à 4800 | 126,7 kW à 7350 | 8,14 s | | |
| Course, rupteur à 8000 | 172,1 Nm à 5650 | 129,3 kW à 8000 | 9,05 s | | |
| Déphaseur | 183,0 Nm à 3650 | 97,2 kW à 6300 | 9,37 s | 9,12 L | comme l'origine |
| Deux profils, bascule à 5200 | 184,0 Nm à 3600 | 96,8 kW à 6800 | 9,52 s | 9,14 L | 34,7 kPa, 14 %, 2,7 % |
| Déphaseur et deux profils | 190,1 Nm à 3600 | 97,3 kW à 6800 | 9,47 s | 9,17 L | 34,7 kPa, 14 %, 2,7 % |

Une came de course ne donne rien sans régime : à 6800 tr/min, elle perd 13 % de couple et 5 % de puissance ; avec un rupteur à 8000, elle gagne 2 % de puissance et déplace le pic de couple de 850 tr/min. C'est le sens connu (une came « route rapide » déplace le pic de 500 à 1000 tr/min et coûte du bas, d'après les catalogues de préparateurs, de mémoire), sans y avoir été calé. **Aucune courbe publiée d'un même moteur, came d'origine contre came de course, n'a pu être recoupée** : les chiffres ci-dessus sont ceux du calcul.

**Gaz résiduels et ralenti.** La fraction de gaz résiduels vient de la corrélation de Fox, Cheng et Heywood (SAE 931025, de mémoire) : x = 1,266·(OF/N)·(p_i/p_e)^−0,87·√|p_e − p_i| + 0,632·(φ/ε)·(p_i/p_e)^−0,74, pressions en kPa, avec le facteur de croisement OF = (1,45/B)(107 + 7,8Δθ + Δθ²)(L·ΣD/B²), en mm et en degrés à 1 mm, ΣD la somme des diamètres des soupapes d'admission (lecture supposée pour quatre soupapes). La pression au ralenti se résout : le cycle calculé, à la moitié de sa chaleur (l'avance est retardée pour garder une réserve de couple, supposé), doit fournir les frottements du moteur et 0,3 bar d'accessoires. La came d'origine tient 39 kPa, soit 18 inHg de dépression (de 17 à 21 en atelier, de mémoire) ; la came de course 49 kPa, 15 inHg, quand les ateliers citent de 8 à 12 inHg pour les cames les plus pointues : **le sens est juste, l'écart plus faible qu'en réel**.

**La dispersion des combustions.** Les gaz brûlés ralentissent la flamme laminaire : S_L = S_L0·(1 − 2,06·x^0,77) (Metghalchi et Keck, 1982). La dispersion de la pression moyenne indiquée suit son inverse : COV = 1,04 %·(S_L0/S_L)^1,62. Les deux constantes sont calées sur deux ordres de grandeur, 3 % à un ralenti d'origine et 10 % (la limite de conduite) vers 27 % de gaz brûlés (Heywood, chapitre 9, de mémoire) ; au-delà, des ratés. Un diesel s'enflamme de lui-même : rien de cela ne s'y applique.

**Le son.** La came change le calage des bouffées (les `evo`… du son, ceux d'origine tant que la came l'est), leur durée (τ = V/(Cd·A·a), avec la section moyenne des 60 premiers degrés), et la pression à l'ouverture, lue dans le cycle calculé régime par régime. Le ralenti galope : à chaque explosion, la pression est tirée en loi normale (somme de trois tirages uniformes) avec l'écart que la came ajoute à la dispersion d'origine, √(COV² − COV₀²), les gaz résiduels pris au remplissage du moment ; sous 5 % de la pression attendue, c'est un raté. Une explosion plus faible lance moins le vilebrequin, de l'ordre de son à-coup au ralenti, et le régulateur l'efface en 0,25 s : le régime erre. Tout cela tire de son propre générateur, et ne décale aucun tirage du reste. Rendu à l'arrêt, `hot_hatch_na` au ralenti : crêtes des bouffées dispersées de 26 % sur la came d'origine (la dispersion propre du son, qui n'est pas une mesure), de 28 % sur 265°/11 mm, de 34 % sur 280°/12/106°, et l'intervalle entre bouffées passe de ± 0,8 ms à ± 0,9 ms. Sur 300°/13/104°, une came que personne ne laisserait tourner à 775 tr/min, la dispersion calculée dépasse 50 % : une explosion sur deux ou trois manque, le régime erre de ± 5 ms par intervalle.

**Le calage variable.** Le déphaseur d'admission a ± 20° de vilebrequin autour du calage de base (40° en tout, de l'ordre d'un VVT-i, de mémoire). Son meilleur calage à pleine charge sort du même cycle : trois calages, puis le sommet de la parabole qui passe par eux. Il avance l'admission en bas (de + 16 à + 20° sous 4000 tr/min) et la retarde en haut (− 9° à 7000, − 18° à 8000). Pied levé et au ralenti, il reste en butée retard, le moins de croisement possible ; il tourne à 150°/s (supposé). Une came à deux profils encadre le profil réglé : 25° et 15 % de levée de moins pour le doux, autant de plus pour le pointu (supposé, de l'ordre d'un VTEC). La bascule a lieu là où leurs courbes de couple se croisent, calculé, avec ± 2 % d'hystérésis, et seulement au-dessus de 60 % de remplissage (supposé). Le couple de la fiche est le meilleur des deux ; dans la bande d'hystérésis, les deux diffèrent de moins de 1 %, et le modèle de conduite ne le compte pas. À la bascule, chaque cylindre verrouille son culbuteur à sa prochaine fermeture d'admission, revenu sur le cercle de base : un tic à 4,5 kHz, de la moitié de l'énergie du tic du siège (une goupille de 5 g à 1 m/s contre une soupape de 40 g à 0,5 m/s, supposés), à son propre générateur.

**Ce que la came expose aux autres sujets**, en fonctions pures de `cam.hpp` : `intakeClosingAbdcDeg`, `effectiveCompressionRatio`, `valveOverlapDeg`, `overlapFactor` et `residualGasFraction` (qui reçoivent les pressions en paramètres), `combustionCov`, `camDurationAtLiftDeg`, `camProfileOf` (levée et durée effectives), `camNoseDecelerationMs2`. Elle lit le régime d'affolement des soupapes en paramètre de `camSwitchRpm` : un second profil qui ne servirait qu'au-delà ne bascule pas. Elle lit aussi `compressionRatio`.

**Ce qui n'est pas modélisé.** Le tube primaire n'est pas accordé à la came : le balayage par l'onde d'échappement reste celui d'un tube amorti. Le cycle n'a qu'un γ et pas de transfert de chaleur. La consommation ne voit que le couple : une came de course y gagne 2,4 %, parce que son couple maximal plus bas fait travailler le moteur plus près de sa pleine charge, alors qu'en réel ses gaz résiduels la feraient plutôt perdre. Le gaz à l'écran et le dessin suivent le profil de base, sans le déphaseur ni la bascule en direct, et le gaz ne suit pas la levée. L'affolement n'a ici aucune conséquence : il appartient aux culbuteurs. **Rien n'a été écouté** : à faire, `revforge-render --out ralenti.wav --preset hot_hatch_na --scenario idle --seconds 8 --cam-duration 300 --cam-lift 13 --lsa 104`, la même chose sans les options de came, et `--vvt two_step --scenario launch --listener bay` pour la bascule.

## Les culbuteurs et l'affolement

`Camshaft::Ohv` ne se fait plus passer pour un simple arbre : un seul arbre dans le bloc pour tout le moteur, des poussoirs hydrauliques, des tiges, des culbuteurs de rapport 1,6 et deux soupapes en ligne sous une culasse en coin. L'affolement des soupapes, lui, vaut pour toutes les distributions. Tout est dans `core/src/valve_float.cpp` (fonctions pures) et `core/src/valvetrain_sound.cpp` (les deux bruits, chacun à son générateur). `revforge-render --valves 1` imprime la distribution d'un réglage.

**Les pièces, ramenées à la soupape.** Ce que voit le ressort est la masse équivalente m = m_soupape + m_coupelle + m_ressort/3 + m_godet + I_culbuteur/L² + (m_poussoir + m_tige)/R², R étant le rapport du culbuteur : ce qui est côté came bouge R fois moins vite. Une soupape pèse comme une tête pleine de 6,5 mm d'épaisseur équivalente (44 g à 33 mm, 103 g à 50,8 mm, l'ordre de vraies soupapes, de mémoire) ; coupelle 18 %, ressort 90 % (un tiers suit), godet 70 %, culbuteur de simple arbre 80 %, culbuteur embouti 60 %, poussoir 85 % de la soupape (supposés d'après des pièces courantes). Un bras pivotant entre ses bouts donne I/L² = m (R² − R + 1)/(3R²). La tige fait 2,1 fois la course (183 mm sur un Small Block, 188 sur un LS), un tube de 0,155 fois la tête (6 mm au moins) alésé à 60 %. Les raideurs s'ajoutent en série, vues de la soupape : queue E·A/L, came et godet 30 kN/mm, bras de culbuteur 12 kN/mm, colonne d'huile du poussoir 40 kN/mm et tige E·A/L, ces deux dernières multipliées par R² (ordres de grandeur supposés). D'où f_n = √(k/m)/2π :

| 2 L, 86 × 86 | Masse ramenée | Fréquence propre | Ressort posé, ouvert |
|---|---:|---:|---:|
| Double arbre | 94 g | 2,2 kHz | 231 N, 579 N |
| Simple arbre | 117 g | 1,15 kHz | 287 N, 717 N |
| Culbuteurs | 147 g | 800 Hz | 361 N, 903 N |
| LS1, 99 × 92, 6000 tr/min | 203 g | 716 Hz | 489 N, 1222 N (110 et 275 lb) |
| Small Block 350, 101,6 × 88,4, 5500 tr/min | 215 g | 708 Hz | 445 N, 1113 N (100 et 250 lb) |

Les ressorts publiés (de mémoire) : 80 et 200 lb sur un Small Block de série, 76 à 85 et 220 à 240 lb sur un LS1, 105 et 280 lb sur un LS6. Le modèle est 20 à 25 % au-dessus du Small Block, entre les deux LS. Les fréquences propres publiées sont de 2 à 3 kHz pour une attaque directe et de 500 à 900 Hz pour des culbuteurs.

**La décélération au nez.** La loi de levée accélère en demi-sinus sur chaque flanc, puis décélère en demi-sinus sur les 60 % du milieu de l'ouverture (supposé : une came de route décélère sur la moitié aux deux tiers de son ouverture, moins fort qu'elle n'accélère). La levée vaut L = v_max·T·s avec s = (1 − f)/4 + f/π, f = 0,6, et la décélération au nez a = π·v_max/(f·T), soit a = π·L·ω²/(f·s·D²) ≈ 18·L·ω²/D², D étant la durée en radians de vilebrequin. Elle suit le carré du régime. La came d'origine lève 0,12 fois l'alésage (`valvetrain.cpp`) sur 240° (le son, `kEvo` à `kEvc`) : 613 g au nez à 7200 tr/min sur 86 mm d'alésage, l'ordre des cames de route.

**Le ressort d'origine.** Le constructeur le dimensionne en cinématique pour la came d'origine : à pleine levée, F = 1,25·m·a(zone rouge), et la force posée vaut 2,5 fois moins (80 et 200 lb, 105 et 280 lb). La souplesse de la chaîne n'y est pas : c'est elle qui sépare les distributions. Une zone rouge plus haute, c'est un ressort plus fort : le préréglage décrit le moteur tel que son constructeur l'a fait tourner.

**Le régime d'affolement.** La soupape quitte la came quand l'inertie, amplifiée par la chaîne, dépasse le ressort au nez. Au premier ordre, la décélération vue de la soupape vaut a/(1 − (f_d/f_n)²), f_d = 1/(2·f·T) étant la fréquence du demi-sinus de décélération. D'où m·α·N²/(1 − β·N²) = F, soit N² = F/(m·α + β·F), avec a = α·N² et (f_d/f_n)² = β·N². Avec le ressort d'origine, N_f = N_r·√(1,25/(1 + 1,25·β·N_r²)) : la marge du constructeur, moins ce que la chaîne lui prend. `valveFloatRpm(const EngineConfig&)` la donne ; le rupteur et la came la liront.

| Préréglage, ressorts d'origine | Zone rouge | Affolement | Marge |
|---|---:|---:|---:|
| `city_14` | 6200 | 6921 | +11,6 % |
| `four_cyl_sport` | 7200 | 8027 | +11,5 % |
| `three_cyl_turbo` | 6000 | 6698 | +11,6 % |
| `hot_hatch_turbo` | 6500 | 7252 | +11,6 % |
| `hot_hatch_na` | 7300 | 8140 | +11,5 % |
| `five_cyl_turbo` | 7000 | 7807 | +11,5 % |
| `inline_six` | 8000 | 8913 | +11,4 % |
| `v6_gt` | 6500 | 7251 | +11,5 % |
| `flat_six` | 7800 | 8682 | +11,3 % |
| `boxer_four_turbo` | 6800 | 7578 | +11,4 % |
| `v8_cross_plane` | 6000 | 6692 | +11,5 % |
| `v10_open` | 8500 | 9467 | +11,4 % |
| `v12` | 8900 | 9901 | +11,2 % |
| `diesel_four` | 4500 | 5026 | +11,7 % |
| `retro_coupe` (simple arbre) | 6400 | 7099 | +10,9 % |
| `v8_supercharged` | 6400 | 7133 | +11,5 % |
| `carb_twin_cam` | 6400 | 7140 | +11,6 % |
| `twin_rotor_13`, `electric` | — | sans soupapes | — |

Tous restent de 11 à 12 % au-dessus de leur zone rouge, où le rupteur coupe : rien ne change au défaut, et les 29 empreintes sont restées. C'est l'ordre des affolements connus, de 5 à 15 % au-dessus du rupteur ; le LS1 en sort à 6584 tr/min pour 6000, ses ressorts d'origine étant donnés bons jusqu'à 6300 à 6500 environ. Sur des culbuteurs, la chaîne prend 1 à 2 points de plus de la marge (de +7,9 à +10,9 % sur ces mêmes moteurs) : ils peuvent tourner haut, mais au prix de ressorts bien plus forts, qui frottent.

Une came plus pointue avec ses ressorts d'origine abaisse l'affolement : sur `hot_hatch_na`, 12,5 mm de levée sur 200° le font tomber à 6490 tr/min, sous la zone rouge ; des ressorts raides le remontent à 7394. La came réglable lit ces nombres par `camLobeOf` (`camLiftMm`, `camDurationDeg`, 0 d'origine) ; le ressort, lui, reste celui de la came d'origine.

**Des ressorts raides.** 30 % de force en plus, posés comme ouverts (de 24 à 50 % sur les ressorts de préparation courants) : l'affolement monte d'environ √1,3, de 13 à 14 %. Le frottement came-poussoir suit la charge, donc 30 % de plus sur lui : +0,03 bar à 6000 tr/min sur un 2 L de 86 mm de course, de 1 à 4 % du frottement total, plus au ralenti où la came pèse le plus. Comme le reste du frottement de distribution, il est pris au couple au frein dans `breathing` (rapporté à 12 bar de pression moyenne indiquée, comme `sohcBreathingRatio`) et ajouté à `frictionTorqueNm`, que lisent la conduite et la consommation.

**Au-delà de l'affolement.** La force demandée dépasse le ressort d'un rapport ρ = (N/N_f)²·(1 − β·N_f²)/(1 − β·N²). La soupape ne décélère plus qu'à 1/ρ de ce que veut la came : elle reste en l'air ρ fois plus longtemps, et l'admission ferme en retard de Δθ = f·D·(ρ − 1), borné par le flanc de fermeture ((1 − f)·D/2, 48° d'origine). Le piston remonte pendant ce temps et repousse la charge : le cylindre en garde V(fermeture + Δθ)/V(fermeture), bielle de 1,6 course et taux d'origine. La soupape retombe à v_flanc·(1 − 1/ρ), v_flanc = L·ω/(s·D), au lieu de la vitesse de la rampe de fermeture (0,013 mm par degré, supposé), et rebondit : chaque rebond garde e = 0,5 de la vitesse (supposé) et dure 2·e·m·v/F₀. Le rebond est chaotique : chaque cycle garde sa charge à ± la perte moyenne près. Sur `hot_hatch_na`, à 10 % au-delà : 30° de retard, 79 % de la charge gardée, une arrivée à 1,3 m/s pour 0,7 m/s sur la rampe. La courbe de couple plonge (`valveFloatShare`, exactement 1 en dessous), et le son suit.

**Le frottement et l'inertie des culbuteurs.** Le frottement came-poussoir d'un double arbre suit Patton, Nitschke et Heywood (C = 133, voir « Le simple arbre ») ; le simple arbre en a 50 %. Le ressort étant dimensionné sur la masse ramenée, et la came le voyant multiplié par le rapport, les culbuteurs en ont (m·R)_culbuteurs/(m·R)_simple arbre fois plus que le simple arbre : 2,2 fois le double arbre sur le 2 L, 0,144 bar à la zone rouge contre 0,067 et 0,100. C'est de 3 à 10 % du frottement total, le plus au ralenti. Un seul arbre dans le bloc pour tout le moteur, au lieu de deux par rangée : de −0,1 à −0,3 % d'inertie. Les poussoirs et les tiges vont et viennent : leur énergie, ramenée au vilebrequin, pèse moins de 10⁻⁵ kg·m², négligée.

**Le remplissage des culbuteurs.** Deux soupapes en ligne, le long du vilebrequin, sont bornées par le même bord et le même pont que deux soupapes en travers : la même section que le simple arbre, donc le même décrochage en haut, le même accord plus bas, la même bouffée allongée. Ce qui distingue vraiment une culasse à culbuteurs n'est pas modélisé : le pincement des conduits par les tiges, la paroi qui masque des soupapes en ligne, la culasse hémisphérique d'un Hemi.

| Moteur | Distribution | Couple maximal | Puissance maximale | 0 à 100 km/h | WLTC | Affolement |
|---|---|---|---|---|---|---|
| 2 L, 86 × 86, 6500 tr/min, compacte de 1200 kg | double | 168,5 Nm à 3900 | 89,9 kW à 5950 | 9,95 s | 8,40 L | 7251 |
| | simple | 166,9 Nm à 3050 | 79,7 kW à 5700 | 10,38 s | 8,48 L | 7205 |
| | culbuteurs | 166,2 Nm à 3050 | 79,5 kW à 5700 | 10,40 s | 8,58 L | 7140 |
| | culbuteurs, ressorts raides | 165,6 Nm à 3100 | 79,2 kW à 5700 | 10,44 s | 8,69 L | 8099 |
| `hot_hatch_na` | double | 194,1 Nm à 4500 | 117,8 kW à 6750 | 8,72 s | 9,07 L | 8140 |
| | culbuteurs | 188,2 Nm à 3400 | 95,8 kW à 6100 | 9,44 s | 9,16 L | 7985 |
| | culbuteurs, ressorts raides | 187,5 Nm à 3400 | 95,5 kW à 6100 | 9,46 s | 9,25 L | 9047 |
| `v8_cross_plane` | double | 428,9 Nm à 3700 | 214,0 kW à 5550 | 6,95 s | 13,90 L | 6692 |
| | simple | 424,0 Nm à 3000 | 194,5 kW à 5400 | 6,78 s | 14,13 L | 6646 |
| | culbuteurs | 422,3 Nm à 3000 | 193,8 kW à 5400 | 6,78 s | 14,35 L | 6592 |
| | culbuteurs, ressorts raides | 420,7 Nm à 3000 | 193,1 kW à 5400 | 6,78 s | 14,56 L | 7478 |

Face au simple arbre, les culbuteurs ne perdent que leur frottement : −0,4 % de couple, +1 à +1,6 % au WLTC. Face au double arbre, c'est surtout la section de deux soupapes qui coûte. Un vrai couple : la Corvette de 1992, LT1 à culbuteurs (101,6 × 88,4, rupteur vers 5700), 224 kW à 5000 et 461 Nm à 3600 ; la ZR-1, LT5 à double arbre (99 × 93, 7000), 280 kW à 5800 puis 302 kW (chiffres de mémoire). Le modèle donne 208 kW à 5200 et 472 Nm à 2850 au premier, 315 kW à 6450 et 542 Nm à 4350 au second : un écart de 1,52 au lieu de 1,25 à 1,35. Le sens est juste, l'écart trop grand : la zone rouge plus haute du LT5 pousse aussi sa pression moyenne (`naturalBmep`), et le LT1 avait ses propres conduits et sa propre came. Rien n'a été ajusté pour coller à ces chiffres. Deux autres moteurs à tiges, réglés comme eux (de mémoire) : le Ford Windsor 302 HO (101,6 × 76,2), 168 kW à 4200 et 407 Nm à 3200, que le modèle donne à 173 kW à 5300 et 386 Nm à 2850 (zone rouge de 5800 supposée), soupapes affolées à 6376 tr/min ; le BMC série A de 1275 cm³ de la Mini Cooper S (70,6 × 81,3), 57 kW à 5800 et 107 Nm à 3000, que le modèle donne à 51 kW à 5700 et 106 Nm à 3100 (zone rouge de 6500), 97 g en mouvement à chaque soupape, une chaîne à 964 Hz, soupapes affolées à 7179 tr/min.

**Le son des poussoirs.** À chaque ouverture, le corps du poussoir, poussé par la came sur sa rampe, rattrape la course de fuite du plongeur et frappe tout ce qui est au-dessus : la tige, le culbuteur et la soupape, vus du poussoir multipliés par R². L'impulsion vaut μ·v, μ étant la masse réduite des deux côtés et v la vitesse de la rampe divisée par le rapport. Rapportée à celle du culbuteur d'un simple arbre qui rattrape son jeu sur la queue (`kRockerShare`, le seul niveau supposé de cette chaîne), elle donne 1,79 fois plus sur le V8 : +5 dB. Le choc fait sonner la tige en flexion, à son deuxième mode d'une poutre appuyée, 4·(π/2L²)·√(EI/ρA) : 2,1 kHz sur le V8, 1,9 à 2,9 kHz sur les moteurs publiés. Il rayonne par le couvre-culasse, comme le culbuteur, et à son propre générateur. Mesuré face au simple arbre au ralenti (800 tr/min) : relief de la bande du choc en fenêtres de 1 ms +2,6 dB sous le capot, −0,04 dB dehors, et la bande 8-16 kHz face à 500-2000 Hz ne bouge pas dehors (−66,91 dB contre −66,91). Le choc ne passe donc pas devant l'échappement. Un rattrapage mécanique (poussoirs à jeu) frapperait plus fort ; le modèle suppose des poussoirs hydrauliques, dont le film d'huile n'est pas modélisé autrement.

**Le son de l'affolement.** Moins de charge, ce sont des bouffées plus faibles ; un rebond chaotique, des cycles inégaux ; une soupape qui retombe vite, un siège qui claque. Le claquement suit la vitesse d'arrivée (impulsion m·v), rapportée au tic d'origine du siège, calé au ralenti où la soupape se pose à la vitesse de la rampe à ce régime, et à son propre générateur ; il finit ses rebonds quand le régime retombe. Mesuré sur `hot_hatch_na`, face au même moteur dont les soupapes ne s'affoleraient jamais : à 15 % au-delà (9361 tr/min), −2,4 dB(A) dehors et un écart d'énergie d'un cycle à l'autre de 0,33 au lieu de 0,18 ; à 25 % au-delà, +1,7 dB sous le capot entre 6 et 12 kHz. Sous l'affolement, le son est identique au bit près (test). En conduite, le rupteur arrête le moteur avant l'affolement d'origine : il ne s'entend qu'avec une came plus pointue, ou un rétrogradage qui fait passer le moteur au-delà.

**Ce que lisent et exposent les autres sujets.** Exposés : `valveFloatRpm(const EngineConfig&)` (0 sans soupapes), et pour un réglage donné `valvetrainParts`, `valveSpringOf`, `noseDecelerationMs2`, `valvetrainProfileOf`, `floatOverload`, `floatTrappedShare`, `valveFloatShare`. Lus comme paramètres : `camLiftMm` et `camDurationDeg` (`camLobeOf`, 0 d'origine), dont la fermeture de l'admission se déduit à cheval sur l'axe de came (50° après le point mort bas d'origine) ; `compressionRatio` (0 : la règle du démarreur). Un rotatif et un deux-temps n'ont pas de soupapes (`hasValves`) : rien de tout cela ne s'y applique.

**Ce qui est supposé, et ce qui manque.** Une chaîne à un degré de liberté, amplifiée au premier ordre : ni rebond de la tige, ni pompage du poussoir hydraulique quand la soupape s'affole (il la tiendrait ouverte), ni flexion de l'arbre. La marge de 1,25, la loi de levée, les parts des pièces et les raideurs sont supposées, chiffrées depuis quelques pièces connues de mémoire. La soupape d'échappement, plus légère, n'est pas calculée à part : celle d'admission s'affole la première. Les gaz résiduels d'un échappement qui ferme tard ne sont pas comptés. La vue dessine toujours une culasse en coin, soupapes en ligne.

**À l'écran.** `engineLayout` déduit l'arbre dans le bloc de l'architecture : au fond du V, au-dessus des contrepoids, ou sur le flanc d'admission d'un moteur en ligne (`blockCamPosition`). Les culbuteurs reprennent le levier du simple arbre, retourné : la queue penche vers l'échappement, la tige monte côté admission, et l'axe du poussoir vise le patin à mi-levée. Le poussoir monte de la levée divisée par le rapport (`lifterTop`), le patin suit l'angle exact du culbuteur (`rockerPad`), et la tige va de l'un à l'autre : sa longueur varie de moins de 3 % de l'alésage sur un tour (`culbuteurs.spec.ts`). Le gaz à l'écran garde la section de deux soupapes (`twoValves`, `valves.ts`). Le volet de la distribution affiche le régime d'affolement, la masse ramenée et les ressorts que calcule le C++ (`rf_valve_sheet`).

**Écoute à faire.** Le tic des poussoirs : `revforge-render --out ralenti_culbuteurs.wav --preset v8_cross_plane --cam ohv --scenario steady --rpm 800 --listener bay`, face à `--cam sohc` et `--cam dohc`, puis dehors (`--listener outside`). L'affolement : `revforge-render --out affolement.wav --preset hot_hatch_na --cam-lift 12.5 --cam-duration 200 --scenario lap`, face à `--springs stiff`, et `--scenario steady --rpm 9000 --listener bay` face à 7500.

## La régulation de pression

Trois façons de tenir la pression d'un turbo (`boostControl`, `--boost-control`), dans `core/src/boost_control.cpp`. `TurboCharger` ne reçoit qu'une entrée de plus, `TurboInput::gateDemand` : négative, c'est le ressort, et rien ne change au bit près. Sur un compresseur mécanique, le réglage n'a pas de sens : il n'a pas de wastegate, sa pression suit le vilebrequin. Il y est ignoré au bit près, et le volet ne le propose que sur un turbo.

### Le ressort (`spring`), le modèle d'origine

Une membrane, poussée par la pression de charge au-dessus de l'air ambiant, ouvre la wastegate contre un ressort. Dans `TurboCharger::step` :

  g* = clamp((p + τa·dp/dt − p₀ − (1 − 0,2·b)·Δp) / (b·Δp), 0, 1),  puis  dg/dt = (g* − g)/τg

avec Δp la pression du préréglage, b = 0,12 la bande du ressort (sa raideur rapportée à la membrane), τa = 0,12 s une anticipation, τg = 0,05 s l'inertie de la tige. La précharge ouvre à 97,6 % de Δp, la wastegate est grande ouverte 12 % plus haut. `springGateDemand` refait ce calcul en fonction pure, vérifiée au bit près face au pas du turbo.

**Dérive en haut (« creep »).** À l'équilibre, p − p₀ = Δp·(0,976 + 0,12·g) : plus le moteur tourne, plus il y a de gaz à jeter, plus la wastegate doit s'ouvrir, et plus la membrane doit voir de pression. Plein gaz à régime tenu, écart à Δp :

| | 2500 tr/min | 3500 | 4500 | 5500 | 6500 |
|---|---:|---:|---:|---:|---:|
| `hot_hatch_turbo` (1,3 bar) | −0,9 % | +0,7 % | +1,5 % | +2,0 % | +2,1 % |
| `three_cyl_turbo` (1,2 bar) | +0,7 % | +2,2 % | +3,0 % | +3,3 % | +3,4 % (6000) |
| `five_cyl_turbo` (1,5 bar) | −1,3 % | +0,4 % | +1,3 % | +1,8 % | +2,1 % (7000) |
| `diesel_four` (1,8 bar) | −1,4 % | −0,7 % | −0,5 % (4500) | | |

L'autre dérive, celle d'une wastegate trop petite qui sature, n'existe pas sur les préréglages : elle ne s'ouvre jamais à plus de 0,49 (`wastegateK` vaut deux fois la turbine).

**Dépassements.** Il n'y en a presque pas : moins de 0,1 % sur un coup de gaz à régime tenu, grâce à l'anticipation de 0,12 s, qu'une membrane purement pneumatique n'a pas. Chiffré sans elle (copie de `turbo.cpp` hors du dépôt) : 2 à 19 % de dépassement, puis une oscillation qui ne s'éteint pas en 8 s, l'arbre et la soupape se renvoyant la balle. Retirer l'anticipation au défaut pour obtenir ce dépassement changerait toutes les empreintes turbo, et laisserait la pression osciller : le ressort reste tel quel.

### L'électronique (`electronic`)

C'est la chaîne d'un calculateur : capteur de pression absolue, tâche de régulation, électrovanne découpée, membrane. Comme sur les wastegates à dépression (BMW N54 et N55, de mémoire), la membrane est ouverte au repos par son ressort et fermée par le vide que règle l'électrovanne : une panne ouvre la wastegate.

- **La cible.** Une pression *absolue* : p_c = p_réf + Δp, p_réf = 101 325 Pa, l'atmosphère de la calibration. Elle baisse avec le régime là où le compresseur dépasserait la vitesse que ses roues supportent : `turboSteady` donne, à chaque régime, le rapport de pression où la limite de 520 m/s mord, et la cible y est ramenée (r_max·p_ambiant). Sur les préréglages, au niveau de la mer, cette limite ne mord pas : la cible est plate.
- **L'électrovanne.** 25 Hz, 5 ms pour s'ouvrir, 3 ms pour se fermer (ordres de grandeur supposés). Le rapport cyclique est pris au début de chaque période. Rapport effectif : 0 si l'impulsion est plus courte que l'ouverture (rapport < 12,5 %), 1 si la pause est plus courte que la fermeture (> 92,5 %), sinon D + (t_f − t_o)·f. Le vide de la membrane suit en 40 ms (son tuyau, supposé), l'ouverture vaut 1 − vide, puis la même inertie de 50 ms que le ressort.
- **Le PID.** Tâche de 10 ms, forme idéale : u = u_ff + Kc·e + I + D, borné à [0, 1]. La dérivée porte sur la mesure, pas sur l'écart (pas de coup quand la cible saute), filtrée à τD/10. La précommande u_ff est le rapport cyclique qui tient l'ouverture d'équilibre de `turboSteady` à ce régime, interpolée entre la cible et la mi-pression, en rapport de pression.
- **L'anti-emballement.** Intégration conditionnelle (Åström et Hägglund, *Advanced PID Control*, 2006) : l'intégrale s'arrête quand l'électrovanne est en butée et que l'écart l'y pousse, et quand la pression rejoint d'elle-même la cible avant τI (|dp/dt|·τI > |e|). Bien sous la cible, quand la précommande et l'action proportionnelle saturent seules, la boucle est ouverte : l'intégrale repart de zéro, comme un calculateur qui sort de régulation. Sans cela, après des montées en mi-pédale, la reprise en quatrième gardait l'intégrale d'avant : crête de +27 % au lieu de +8 % sur `three_cyl_turbo`.

**Le réglage des gains : un banc, puis SIMC.** À la configuration, un `TurboCharger` est tenu plein gaz à 40, 55, 70 et 88 % du régime maximal, wastegate fixée à son ouverture d'équilibre, puis on la ferme d'un point. La réponse de la pression donne un modèle du premier ordre à retard, δp = K·e^(−θs)/(τs + 1)·δu, par la méthode des deux points de Smith (28,3 % et 63,2 % : τ = 1,5·(t₆₃ − t₂₈), θ = t₆₃ − τ) ; la valeur finale est prolongée par le Δ² d'Aitken. Le retard effectif ajoute une demi-période de tâche et de découpage. La règle SIMC de Skogestad (*Journal of Process Control* 13, 2003) avec τc = θ donne Kc = τ/(2Kθ), τI = min(τ, 8θ), et τD = 40 ms compense le tuyau de la membrane. Les gains sont interpolés en régime, comme les tables d'un calculateur. Mesuré par le test, sur la boucle linéarisée complète (retards, tuyau, dérivée filtrée) :

| | K (bar par ouverture) | τ | θ | Kc (par bar) | τI | Marge de phase | Marge de gain |
|---|---:|---:|---:|---:|---:|---:|---:|
| `hot_hatch_turbo`, 2600 tr/min | 20,2 | 1,08 s | 0,076 s | 0,28 | 0,84 s | 57° | 3,0 |
| `hot_hatch_turbo`, 5720 tr/min | 7,6 | 0,65 s | 0,098 s | 0,37 | 0,69 s | 61° | > 100 |
| `five_cyl_turbo`, 2800 tr/min | 31,1 | 1,27 s | 0,071 s | 0,22 | 0,81 s | 55° | 3,0 |
| `diesel_four`, 3150 tr/min | 14,6 | 0,68 s | 0,10 s | 0,20 | 0,72 s | 61° | > 100 |

Le gain du banc retrouve, à 25 % près, celui que donnent deux équilibres voisins de `turboSteady`.

**Face au ressort**, coup de gaz plein pied après 8 s à mi-charge, régime tenu (`tests/boost_control_test.cpp`) :

| | Erreur établie | 90 % de la pression | Dépassement |
|---|---|---|---|
| Ressort, `hot_hatch_turbo` 3500 / 5980 tr/min | +0,8 % / +2,1 % | 0,68 s / 0,51 s | < 0,1 % |
| Électronique, idem | −0,03 % / −0,007 % | 0,62 s / 0,45 s | 3,9 % / 1,9 % |
| Ressort, `three_cyl_turbo` 3300 / 5520 tr/min | +2,0 % / +3,3 % | 0,48 s / 0,43 s | < 0,1 % |
| Électronique, idem | −0,01 % / −0,002 % | 0,40 s / 0,34 s | 2,7 % / 5,3 % |

Plus de dérive, une montée de 6 à 21 % plus courte (l'électrovanne tient la wastegate fermée jusqu'au bout, là où le ressort commence à s'ouvrir à 97,6 %), mais un dépassement : 1 à 6 % au-dessus de 3000 tr/min, 9 % à 2500 tr/min. En voiture, en quatrième depuis 1800 tr/min (`--scenario turbo`), le régime montant : +5,9 % en crête sur `hot_hatch_turbo`, +8,4 % sur `three_cyl_turbo`, face à +1,8 et +2,5 % pour le ressort, qui sont sa dérive. Sans anti-emballement : +137 % à 2500 tr/min, +102 % à 3500, et encore 1 à 3 % trop haut au bout de 12 s.

**Les zones mortes.** Quand la wastegate n'a presque rien à jeter, en bas du régime où la cible devient atteignable, l'ouverture voulue tombe entre 0 et 12,5 %, que l'électrovanne ne sait pas tenir : elle alterne entre les deux bouts, et la pression oscille de ±2 % vers 2,7 Hz (`five_cyl_turbo` et `diesel_four` vers 2500 tr/min). C'est la limite d'une électrovanne découpée ; une fréquence plus basse ou une électrovanne plus rapide la réduit.

**L'altitude.** La membrane du ressort respire l'air ambiant : elle tient Δp au-dessus de lui, donc moins de pression absolue, moins d'air, en altitude. Le calculateur vise une pression absolue : à 2000 m (79 495 Pa en atmosphère standard), il demande 0,218 bar de plus au-dessus de l'air ambiant, donc un rapport de pression plus grand, un arbre plus rapide, plus de retard, jusqu'à la limite des roues, qui est un rapport de pression et fait baisser la cible absolue. La pression ambiante est un paramètre (`BoostControlInput::ambientPa`, `boostTargetPa`), à 101 325 Pa tant que l'atmosphère n'est pas branchée ; `turboSteady` et le turbo, eux, gardent encore leur atmosphère fixe.

### Selon le rapport (`by_gear`)

La même régulation, avec une cible bornée sur chaque rapport par le couple qu'il peut passer, tiré de la voiture et non d'une table :

- la force que passent les pneus moteurs, report de charge compris (l'accélération qu'elle donne charge l'arrière) : F = μ·m·g·f/(1 + μ·h/L) en traction, μ·m·g·(1 − f)/(1 − μ·h/L) en propulsion, μ·m·g en intégrale, avec μ = 1,05 (celui de `DrivingModel`), f la part du poids sur l'avant, h la hauteur du centre de gravité, L l'empattement ;
- le couple au vilebrequin qui la donne, C = F·r/(i·η), borné par le couple admissible de la boîte ;
- la pression qui donne ce couple, en inversant `fullLoadTorqueNm` par dichotomie : le modèle de remplissage, à l'envers, comme un calculateur à structure de couple.

Le rapport engagé se lit dans `DrivingModel` ; au point mort, la première. Sur `three_cyl_turbo` (traction, 1150 kg), la première passe 137 Nm, face à 192 Nm au moteur : la cible y tombe de 2,21 à 1,46-1,71 bar absolus selon le régime, la deuxième (260 Nm) n'est pas bornée. Au départ arrêté, le glissement maximal en première passe de 5,6 à 1,9 m/s. Avec l'ancien pneu, sans sommet, le 0-100 km/h passait pourtant de 10,03 à 10,22 s : plus de glissement y donnait toujours un peu plus de force. Avec le pneu à sommet des finitions, pied au plancher, il passe de 10,48 s (ressort) à 10,19 s : plafonner le couple tient le pneu près de son sommet. La fiche, elle, mesure un essayeur qui dose déjà la pédale : 10,04 s au ressort, 10,19 s selon le rapport. Sur les intégrales (`hot_hatch_turbo`, 388 Nm en première pour 357 Nm), rien ne change.

**Ce qui ne change pas.** La fiche (`torqueCurveNm`) reste celle du ressort : au niveau de la mer, l'électronique tient exactement l'équilibre de `turboSteady`, et la boîte reste dessinée pour ce moteur. La consommation au WLTC ne bouge pas : 8,90 L/100 km dans les deux cas sur `hot_hatch_turbo`, 6,34 et 6,32 sur `three_cyl_turbo` selon le rapport.

**Télémétrie.** `rf_boost_target` et `rf_boost_charge` (bar absolus), `rf_boost_duty` (−1 sur un ressort). Le graphique de la pression du turbo montre la cible à côté de la pression réelle, et la jauge du collecteur le rapport cyclique de l'électrovanne. `--trace` écrit `target_abs_bar`, `charge_abs_bar` et `duty`.

**Ce qui est supposé, et ce qui manque.** Les temps de l'électrovanne et du tuyau sont des ordres de grandeur, pas des mesures. Le découpage est moyenné : l'ondulation à 25 Hz de la membrane n'est pas calculée, ni le claquement de l'électrovanne. Pas de cible à charge partielle : le papillon garde son rôle, et la pression monte avant lui comme avec le ressort. Pas de marge au pompage dans la cible. Un diesel garde ici une wastegate ; la géométrie variable est un autre réglage. Écoute à faire : `--scenario turbo` et `--scenario launch`, avec et sans `--boost-control electronic`.

## Deux turbos, twincharger et géométrie variable

`turboLayout` (`single`, `twin_parallel`, `twin_sequential`, `twincharger`) et `variableGeometry` choisissent le montage. Le turbo simple à soupape reste `TurboCharger`, au bit près : `TwinTurbo` (`core/src/twin_turbo.cpp`) ne s'exécute que pour les autres montages, et pour un turbo simple à aubes (`usesTwinTurbo`). Il reprend les lois de `turbo.cpp` (compresseur de Moore et Greitzer, turbine de Stodola, paliers, ventilation, échangeur, soupape de décharge) par un renvoi, `turbo_laws.hpp`, et celles du compresseur mécanique par `supercharger_laws.hpp` : rien n'est recopié.

### Chaque arbre, un volume commun

Chaque arbre intègre son propre bilan de puissance, comme le turbo simple :

    J_i · ω_i · dω_i/dt = P_turbine,i − P_compresseur,i − P_paliers,i

Chaque compresseur souffle par son conduit, avec l'inertie de sa colonne d'air (`ductInertance`), dans un même volume avant le papillon :

    dp_charge/dt = R·T / V_charge · (Σ ṁ_compresseur,i − ṁ_moteur − ṁ_décharge)

C'est ce volume commun qui couple les deux arbres : le plus lent débite moins, et peut pomper. Le filtre et l'échangeur sont communs, leurs pertes suivent le débit total ; la soupape de décharge aussi.

### En parallèle

Chaque turbo est celui d'une moitié de moteur (`turboSpecFor` sur la moitié de la cylindrée) : la moitié du débit nominal. À même pression et même vitesse de bout de pale, le débit suit D², donc la roue est √2 plus petite, et l'arbre tourne √2 plus vite.

**L'inertie en D⁵.** À géométrie semblable, la masse va comme D³ et le carré du rayon de giration comme D² : J ∝ D⁵, la loi qu'utilisent déjà les tailles de `turbo.cpp`. Chaque arbre a donc 2^(−5/2) = 0,177 de l'inertie du turbo simple. Le temps de lancement va comme J·ω²/P, soit D^(n−4) pour J ∝ Dⁿ : avec n = 5, deux petits turbos se lancent en 0,71 fois le temps d'un grand ; avec n = 3, ils seraient 1,41 fois plus lents, ce que contredit toute l'expérience des biturbos. Il faut n > 4 ; un petit rotor réel est un peu plus lourd que la similitude ne le dit (arbre, écrou, paliers), donc entre 4 et 5. On garde 5.

Deux chemins d'air, chacun avec son compresseur et son tube chaud : le volume avant le papillon est celui de deux moitiés, 3 L + 2,5·V au lieu de 1,5 L + 2,5·V. C'est 1,5 L de plus à remplir, qui reprend une part du gain.

**Deux vitesses d'arbre.** Chaque turbine reçoit sa rangée ; un moteur en ligne est coupé en avant et arrière. Le gaz perd de sa chaleur le long du collecteur, en exp(−k·L) de son écart à l'air, avec k = 0,27 /m (de l'ordre de 15 % sur 0,6 m entre la soupape et la turbine, supposé). Une rangée aux collecteurs plus longs envoie donc un gaz un peu moins chaud à sa turbine : sur un V6, 110 495 et 110 090 tr/min une seconde après le coup de gaz à 2500 tr/min. Ces 0,4 % suffisent à faire battre les deux sifflements.

Six en ligne de 3 L, 1 bar :

| | turbo simple | deux en parallèle |
|---|---|---|
| roue | 60,7 mm | 42,9 mm chacune |
| inertie | 8,39·10⁻⁵ kg·m² | 1,48·10⁻⁵ kg·m² chacune |
| 90 % de la pression, `--scenario turbo` | 2,55 s | 2,07 s (−19 %) |
| 90 % de la pression à 2500 tr/min tenus | 1,62 s | 1,19 s (−27 %) |
| pression établie, couple | — | à 3 % près |

Sur un petit moteur, deux demi-turbos butent sur la plus petite roue fabriquée (`kSmallestWheel`, 34 mm) : sur le diesel de 2 L, ils perdent l'avantage de l'inertie et gardent le volume des deux chemins, et la pression arrive plus tard (3,9 s contre 2,6 s). C'est bien pourquoi les petits diesels ont choisi le séquentiel ou la géométrie variable.

### En séquence

Deux turbos identiques, comme sur le Toyota 2JZ-GTE, la Porsche 959 (deux KKK) et le Mazda 13B-REW (deux Hitachi). Trois étapes :

- **0, le primaire seul.** Les deux collecteurs sont reliés ; la vanne d'échappement du secondaire, en aval de sa turbine, est fermée : tout le gaz passe par le primaire, petit turbo sur un gros moteur, qui se lance très vite.
- **1, le pré-lancement.** Une petite vanne laisse passer un peu de gaz vers la turbine du secondaire (16 % de la section, le rapport d'une vanne de 20 mm à une de 50 mm, supposé), sa soupape fermée. Son compresseur souffle dans un petit volume (0,3 L pour 2 L, supposé), rendu à son entrée par une vanne de retour.
- **2, les deux.** La vanne d'échappement s'ouvre en 0,12 s (actionneur à dépression, supposé) : le gaz se partage entre les deux turbines selon leur section, par la loi de l'ellipse à pression commune. La vanne de retour se ferme, et le clapet d'admission s'ouvre de lui-même quand le secondaire pousse plus fort que la charge.

La bascule suit le débit d'air du moteur, en part du débit nominal d'un turbo : pré-lancement à 1,25, ouverture à 1,42, retour au primaire sous 1,10, quand le primaire approche de son blocage. Le seuil est calé sur le 2JZ-GTE (vanne d'échappement du second turbo vers 3800 tr/min, d'après les relevés publiés de l'Aristo), puis vérifié sans retouche sur les deux autres :

| | publié | modèle, plein gaz en deuxième |
|---|---|---|
| 2JZ-GTE (2997 cm³, 0,8 bar) | vers 3800 tr/min | 3670 tr/min, pré-lancement à 3230 |
| Porsche 959 (2849 cm³, 1 bar) | vers 4000 tr/min | 4030 tr/min |
| Mazda 13B-REW (2 × 654 cm³, 0,7 bar) | 4000 à 4500 tr/min | 4340 tr/min |

Le creux de pression n'est pas écrit : il découle de la bascule. Le primaire perd la moitié de son gaz d'un coup, alors que le secondaire n'est qu'aux trois quarts de sa vitesse : sur le 2JZ, la pression passe de 1,02 à 0,91 de la pression demandée. À 2500 tr/min tenus, 90 % de la pression arrivent en 0,51 s, contre 1,19 s en parallèle.

### Le twincharger

Comme sur le 1.4 TSI de VW (1390 cm³, 125 kW, 240 Nm dès 1750 tr/min ; programme d'autoformation 359) : un Roots Eaton en amont du turbo, entraîné par la courroie à travers un embrayage électromagnétique, et un volet qui le court-circuite. Air → filtre → compresseur ou volet → volume entre les étages → turbo → échangeur → papillon. Ce volume, relié à l'entrée par le volet grand ouvert, est avancé implicitement : explicite, le pas de 1 ms oscillait.

- **L'embrayage.** VW le tient fermé sous charge jusqu'à 2400 tr/min, seulement au besoin jusqu'à 3500 tr/min, et ouvert au-delà. Ici, `turboAloneRpm` est le régime où le turbo seul tient la pression demandée à l'équilibre ; en dessous, l'embrayage se ferme dès qu'on la demande ; au-dessus, seulement s'il manque plus de 10 % de pression ; il s'ouvre quand le volet n'a plus rien retenu depuis 0,5 s, et toujours au-delà de `clutchOffRpm`, dans le même rapport que chez VW (3500/2400). Hystérésis de 10 %.
- **Le compresseur** est dimensionné pour tenir seul la pression demandée à 40 % du régime de coupure, comme `superchargerSpecFor`, et son rapport amène ses rotors à 16 000 tr/min (un Roots Eaton) au régime de coupure. L'inertie des rotors va comme V^(5/3) (4·10⁻⁴ kg·m² pour 0,5 L par tour, supposé) ; l'embrayage transmet 1,5 fois leur couple au point de dimensionnement (supposé). Pendant qu'il glisse, le vilebrequin paie ce couple multiplié par le rapport.
- **Le volet** rend à l'entrée ce qui dépasse la pression demandée ; tant que le compresseur est embrayé, la soupape du turbo reste fermée (sauf 10 % de trop) pour le lancer.

Sur le 1.4 : le turbo tient seul dès 2000 tr/min, coupure à 2920 tr/min, rapport 5,5, 0,51 L par tour (VW : coupure vers 3500 tr/min selon Autozine, 3000 selon Top Gear ; le turbo du modèle, dimensionné par `turboSpecFor`, est un peu plus prompt que celui de VW). À 1600 tr/min tenus, 90 % de la pression en 0,41 s ; le turbo seul ne les atteint jamais. L'équilibre de la fiche multiplie les rapports des deux étages, en gardant au turbo celui qu'il aurait seul : une approximation, l'air plus dense lui donnant en vrai un peu plus de gaz.

### La géométrie variable

Des aubes devant la turbine changent sa section : K_eff = K·v, sans soupape de décharge. La plus grande ouverture est celle qui tient la pression demandée au régime maximal, plus 15 % (supposé) ; la course va de 1 à 4 en section (ordre de grandeur des turbines à aubes récentes, supposé). Le rendement est meilleur vers 60 % de la course :

    η(v) = η₀ · (1 − 0,4 · (x − 0,6)²),   x = (v − v_min) / (v_max − v_min)

soit 14 % de moins aubes fermées, 6 % grandes ouvertes (supposé, sur la forme des cartes publiées). Les aubes suivent la même loi que la soupape du turbo simple, avec son anticipation, et un actionneur de 0,08 s.

| | aubes fermées, en part de la turbine fixe | 90 % de la pression |
|---|---|---|
| diesel de 2 L, 2000 tr/min | 0,53 | 1,21 s contre 2,10 s (−42 %) |
| six en ligne essence, 2500 tr/min | 0,95 | 2,21 s contre 1,62 s |

Sur le diesel, la pression d'équilibre à 1500 tr/min passe de 0,36 à 0,80. Sur l'essence, les gaz plus chauds et le débit plus étendu demandent une ouverture maximale bien plus grande : fermées, les aubes ne sont pas plus petites que la petite turbine fixe à soupape, et moins efficaces. Elles ne gagnent qu'en haut, par une contre-pression plus faible. C'est ce que dit leur rareté sur l'essence : la Porsche 911 Turbo (997, 2006) a été la première essence de série à turbines à aubes (BorgWarner), avec des matériaux tenant des gaz de l'ordre de 1000 °C. La géométrie variable ne se voit pas dans le gaz à l'écran, dont la turbine reste fixe.

### Le son

Le second arbre a son sifflement, à 2 et 1 fois sa vitesse, avec ses filtres et son propre générateur (`setTurboB`) : le premier n'en est pas décalé, et rien ne s'exécute sans lui. Deux petits compresseurs font ensemble le bruit d'un grand : chaque sifflement est multiplié par √(ṁ_turbo / ṁ_turbo simple) (`TurboDrive::levelShare`, exactement 1 pour un turbo simple). Le compresseur d'un twincharger passe par `SuperchargerSound`, à la vitesse réelle de ses rotors, muet débrayé. **L'écoute reste à faire** : `revforge-render --out biturbo.wav --cylinders 6 --layout v --cc 3000 --bore 86 --redline 6800 --aspiration turbo --boost 1 --exhaust sport --turbo-layout twin_parallel --scenario turbo`, puis `twin_sequential`, `--preset hot_hatch_turbo --turbo-layout twincharger --scenario launch`, `--preset diesel_four --vgt 1 --scenario turbo`.

### Le gaz à l'écran

Avec deux turbos, `GasNetwork` a deux collecteurs (un moteur en ligne coupé en avant et arrière), une turbine au bout de chacun, des tubes qui se rejoignent avant la ligne, et deux chemins d'air de la boîte à air au plénum, chacun avec son compresseur et son papillon. Leur volume total est celui du C++. Le rapport du second compresseur suit le carré du rapport des vitesses d'arbre (même roue, hauteur en U²). En séquentiel, un tube relie les deux collecteurs, la turbine du secondaire est réglée par sa vanne, et son chemin d'air reste clos tant que son clapet l'est. Le turbo simple garde son chemin de calcul.

### Ce qui n'est pas modélisé

- L'anti-lag et le tube déporté, seulement sur un turbo simple : le volet les cache sur les autres montages.
- L'énergie des bouffées est la même par turbine (`kPulsePa`) : l'avantage des collecteurs séparés aux bouffées mieux espacées n'est pas compté.
- Un turbo simple sur un V reste dessiné un par rangée dans les vues (choix antérieur).

### Ce qui vient d'ailleurs

- La pression demandée et l'ouverture de la soupape sont des paramètres (`TwinTurboInput::targetBar`, `wastegate`) : négatifs, ce sont ceux d'aujourd'hui (le ressort). La régulation les donnera.
- L'air ambiant passe par `turbo_laws::constants()` (`atmosphere`, `ambientK`), les mêmes que `turbo.cpp`.
- Les rangées et la longueur des collecteurs viennent de `EngineConfig::toProfile` (`bank`, `headerM`).
- `DrivingModel::turboState()` et `turboStateB()` exposent chaque arbre, avec la pression et la température de charge communes ; `twinTurboSteady` l'équilibre.

## L'air de charge

`core/src/charge_air.cpp` : l'échangeur (air-air, air-eau, aucun), l'injection d'eau et le protoxyde d'azote, en fonctions pures. Ce qui évolue en roulant (la boucle d'eau, la bouteille) est l'état `ChargeAir`, qu'avance `DrivingModel` à chaque pas de 1 ms, par `revforge::portable`. Au défaut (air-air, sans eau, sans protoxyde), `chargeAirActive` est faux et rien de cela ne s'exécute : les préréglages et les 29 empreintes d'origine ne bougent pas d'un bit. Sur un diesel, l'eau et le protoxyde restent sans effet (le réglage n'apparaît que sur un moteur à étincelle) ; sur un atmosphérique, l'échangeur n'existe pas.

**Le principe, partout le même.** Le couple de base (`fullLoadTorqueNm`) suppose l'échangeur d'origine. Ce qui change la charge change la masse d'air admise à pression égale : `C = ((C₀ + f)·T_réf/T·part_air − f)`, où `f` est le frottement, qui ne suit pas la densité. Sur la fiche, `torqueCurveNm` l'applique à l'équilibre, à 100 km/h ; en conduite, `DrivingModel` multiplie le couple indiqué par le même rapport, lu dans l'état du moment. La boîte et l'embrayage restent ceux du moteur d'origine (`chargeAirStock`), comme pour le filtre : un échangeur ne redessine pas une boîte, et un embrayage ne se renforce pas pour du protoxyde.

### L'échangeur

- **Air-air d'origine.** Inchangé : `ε = 0,86 − 0,14·ṁ/ṁ_nom`, une masse de 1,5 s, l'air extérieur de l'autre côté, quelle que soit la vitesse.
- **Air-air par ε-NTU, avec l'air de route** (`airToAirCoolerFor`, `airToAirOutletK`) : prêt à brancher, il ne remplace pas l'origine, car il ne la reproduit pas au bit près. `UA = 1/(1/hA_charge + 1/hA_route)`, `hA_charge ∝ ṁ^0,8` (Dittus-Boelter), `hA_route ∝ ṁ_route^0,5` (ailettes à persiennes, régime laminaire), `ṁ_route = ρ·A·(0,3·v + 1 m/s)` (une calandre ne laisse passer qu'une part de la vitesse ; le ventilateur, à l'arrêt ; deux valeurs supposées). Courants croisés, deux fluides non brassés : `ε = 1 − exp[(NTU^0,22/Cr)·(exp(−Cr·NTU^0,78) − 1)]` (Incropera). Face de 0,85 m² par kg/s (600 × 300 mm pour un 2 L), deux côtés de même conductance, calée pour rendre l'origine au nominal à 100 km/h. Ce qu'il changerait au couple à la puissance maximale, à pression égale :

| Préréglage | Origine | 0 km/h | 50 km/h | 100 km/h | 150 km/h |
|---|---|---|---|---|---|
| three_cyl_turbo | 52,4 °C | 92,0 °C, −10,8 % | 60,6 °C, −2,4 % | 52,2 °C, +0,1 % | 48,0 °C, +1,4 % |
| hot_hatch_turbo | 53,6 °C | 94,5 °C, −11,1 % | 62,0 °C, −2,5 % | 53,4 °C, +0,1 % | 49,0 °C, +1,4 % |
| five_cyl_turbo | 56,8 °C | 101,7 °C, −12,0 % | 66,1 °C, −2,7 % | 56,6 °C, +0,1 % | 51,8 °C, +1,6 % |
| boxer_four_turbo | 51,9 °C | 90,7 °C, −10,7 % | 59,9 °C, −2,4 % | 51,7 °C, 0,0 % | 47,5 °C, +1,4 % |
| diesel_four | 61,3 °C | 111,6 °C, −13,1 % | 71,6 °C, −3,0 % | 61,0 °C, +0,1 % | 55,6 °C, +1,7 % |
| v8_supercharged | 40,1 °C | 64,8 °C, −7,3 % | 45,3 °C, −1,6 % | 40,1 °C, 0,0 % | 37,4 °C, +0,9 % |

  L'écart ne compte vraiment qu'à l'arrêt, plein gaz : sur un banc sans soufflerie, ou au départ d'un feu. Le brancher changerait le 0-100 km/h des préréglages turbo, donc leurs empreintes : c'est un choix à faire à part.
- **Air-eau.** Une brique refroidie par une boucle d'eau glycolée, sa pompe électrique et son radiateur basse température à la calandre :

  ```
  C_boucle·dT_eau/dt = ε_b·ṁ·cp·(T2 − T_eau) − K_rad(v)·(T_eau − T_air) + G·(T_compartiment(v) − T_eau)
  T3 = T2 − ε_b·(T2 − T_eau)
  ```

  La brique : ε-NTU, côté eau quatre fois le côté air (supposé), 85 % au nominal (supposé). La pompe : 2 kg de liquide par kg d'air au nominal (24 L/min pour un 2 L) ; à 1 kg/kg, le liquide s'échauffait de plus de 20 K dans la brique, et aucun radiateur de cette taille ne pouvait rendre le point d'origine. Le radiateur : 0,7 m² par kg/s, ε-NTU face à l'air de route, sa conductance calée pour que l'équilibre au nominal, à 100 km/h, rende l'air-air d'origine : c'est le même échangeur, autrement refroidi. La boucle : 3 L de liquide et 3,5 kg d'aluminium pour 0,21 kg/s, soit 68 kJ/K par kg/s. Le compartiment : à l'arrêt, l'air y est à mi-chemin entre l'air extérieur et l'eau du moteur (55 °C par 20 °C, moteur chaud), deux fois moins en roulant à 36 km/h ; il rend 70 W/K par kg/s à la boucle. Toutes ces tailles sont supposées, dans l'ordre des montages de série.

  `--scenario soak` (hot_hatch_turbo, une minute à l'arrêt, puis trois départs arrêtés de 8 s, 20 s d'arrêt entre eux) :

  | | Arrêt (60 s) | Fin du départ 1 | 2 | 3 | Eau au départ 1/2/3 |
  |---|---|---|---|---|---|
  | Air-air | 19,9 °C | 48,6 °C | 48,6 °C | 48,7 °C | — |
  | Air-eau | 21,3 °C | 43,9 °C | 45,6 °C | 46,2 °C | 21,3/24,9/26,2 °C |

  La masse de la boucle fait le premier départ plus frais que l'air-air ; elle garde ensuite ce qu'elle a pris, et le radiateur, sans vitesse, ne le rend qu'en 90 s environ. À l'arrêt seul, la boucle ne prend que 1,4 K en une minute (3 K à l'équilibre) : ce qu'on lit d'un capteur dans un collecteur chaud, à très faible débit, vient surtout du collecteur, qui n'est pas modélisé, pour aucun échangeur. Ordre de grandeur publié : un échangeur air-air à l'avant tient la charge à environ 15 K au-dessus de l'air à pleine puissance (Behr, brevet US 7 779 791 B2), 20 à 30 K selon d'autres brevets du même fabricant ; ici, 28,6 K à la fin d'un départ et 33,6 K à l'équilibre à la puissance maximale.
- **Aucun.** La sortie du compresseur va droit au papillon : `T3 = T2`. Le chemin de l'air (`ChargePath`, recopié par `fluidGas.ts`) : tube chaud 0,45 m, faisceau 0,45 m de quatre fois la section, tube froid 0,5 m pour l'air-air d'un turbo ; brique de 0,25 m et tube froid de 0,2 m pour l'air-eau ; un seul tube de 0,8 m sans échangeur. Le volume suit : 6,5, 3,9 et 1,9 L sur un 2 L. La perte suit `ρv²` : Darcy (`f = 0,02`) et 0,3 par coude ; le faisceau porte environ 80 % des 0,12 bar d'origine, une brique la moitié (supposé). Le retard ne gagne presque rien : 90 % de la pression à 3000 tr/min en 0,85, 0,80 et 0,76 s, car l'inertie de l'arbre domine. Le turbo et le compresseur restent ceux de l'échangeur d'origine.

| | Couple max | Puissance | 0-100 km/h | Charge à la puissance max. |
|---|---|---|---|---|
| hot_hatch_turbo, air-air | 357 Nm à 2850 | 194,4 kW | 6,86 s | 53,6 °C |
| air-eau | 363 Nm à 2850 | 195,0 kW | 6,60 s | 52,6 °C |
| aucun | 274 Nm à 4350 | 150,4 kW | 7,79 s | 131,7 °C |
| injection d'eau | 378 Nm à 2850 | 209,5 kW | 6,59 s | 27,8 °C |
| protoxyde 75 kW | 598 Nm à 3000 | 271,5 kW | 5,57 s | 18,4 °C |
| v8_supercharged, d'origine | 767 Nm à 3650 | 393,6 kW | 6,99 s | 40,1 °C |
| air-eau | 772 Nm à 3550 | 393,7 kW | 6,96 s | 40,1 °C |
| aucun | 651 Nm à 3850 | 338,7 kW | 7,16 s | 87,4 °C |
| hot_hatch_na | 194 Nm à 4500 | 117,8 kW | 8,72 s | 19,9 °C |
| injection d'eau | 203 Nm à 4550 | 123,7 kW | 8,42 s | 6,4 °C |
| protoxyde 75 kW | 417 Nm à 3000 | 192,3 kW | 7,18 s | −27,5 °C |

Sans échangeur, le moteur perd 23 % de puissance par la seule densité : la détonation, qu'un calculateur éviterait en retirant de l'avance, n'est pas dans ce calcul (voir ce que lit le cliquetis, plus bas).

### L'injection d'eau

De l'eau, à 30 % de la masse d'essence (supposé ; selon la presse, la M4 GTS de BMW demande de refaire ses 5 L d'eau à chaque plein sur circuit, où l'injection ne marche qu'à pleine charge), pulvérisée dans le collecteur à partir de 80 % de remplissage, toute au-delà de 95 %. Elle s'évapore jusqu'à saturer l'air, ou jusqu'à la dernière goutte :

```
cp_air·(T_entrée − T) = x_eau·c_eau·(T − T_réservoir) + x_vap·h_fg(T),   x_vap = min(x_eau, 0,622·p_s(T)/(p − p_s(T)))
ln(p_s/p₀) = (a/R_v)·(1/T₀ − 1/T) − (b/R_v)·ln(T/T₀),   h_fg = a − b·T
```

Clausius-Clapeyron intégrée avec une chaleur latente linéaire (Kirchhoff) : `a = 3,1615 MJ/kg`, `b = 2386 J/(kg·K)`, depuis l'ébullition à 1 atm ; à 1 % près des tables à 20 et 60 °C. La température se trouve par dichotomie. L'air admis est `(p − p_vap)/p·T_entrée/T` de l'air sec : plus froid, mais la vapeur prend sa place. De l'air sec à 20 °C descend à 6,4 °C, sa température de saturation adiabatique (6 à 7 °C sur un diagramme psychrométrique) : +4,5 % de couple sur hot_hatch_na. Sur un turbo, l'air chaud de l'échangeur porte bien plus d'eau : 53,6 → 27,8 °C, +7 % d'air. Ce qui reste liquide est exposé (`liquidWaterPerAir`) : il s'évaporera pendant la compression, ce qui regarde le cliquetis. L'air extérieur est supposé sec ; il n'y a pas de réservoir qui se vide.

### Le protoxyde d'azote

- **Le protoxyde saturé** : corrélations ESDU 91022 (pression, masses volumiques du liquide et de la vapeur, enthalpies), de 183 à 309 K. 50,5 bar, 786 et 158 kg/m³, 169 kJ/kg à 20 °C ; 31,3 bar à 0 °C (NIST : 3,12 MPa).
- **La combustion.** `2 N₂O → 2 N₂ + O₂` : 36,4 % d'oxygène en masse, contre 23,1 % dans l'air, et 82,05 kJ/mol libérés, soit 1,86 MJ/kg, dans la flamme. L'oxygène brûle `O₂/(14,7·0,2314) = 0,107` kg d'essence par kg de protoxyde ; `P_ind = η·(m_essence·PCI + ṁ_N₂O·Δh)`, `η = 0,39` (entre les 0,375 et 0,405 de `consumption.cpp`), soit 2,52 MJ indiqués par kg. Un kit de 75 kW débite 29,8 g/s, 0,30 kg en 10 s pour 100 ch : la règle des préparateurs dit 0,8 lb, 0,36 kg.
- **Les gicleurs.** Celui d'essence est à pression régulée : 1 kg pour 6 de protoxyde au point de calcul (recommandation courante, plus riche que les 9,4 de la stœchiométrie), et il ne suit pas la bouteille ; le mélange s'enrichit à mesure qu'elle se vide. Tout son débit se paie à la consommation, brûlé ou non. Celui de protoxyde passe du liquide (`ṁ = Cd·A·√(2ρ_l·Δp)`), dimensionné bouteille pleine à l'air ambiant et collecteur à pleine charge ; de la vapeur seule, un col sonique (`ṁ = Cd·A·0,667·√(ρp)`, γ = 1,3).
- **La bouteille.** 10 lb (4,536 kg), au taux de remplissage réglementaire de 0,68 kg par litre (6,67 L), 6,8 kg d'aluminium (supposé). Le liquide parti laisse un volume que du liquide doit vaporiser pour le remplir : `ṁ_vap = ṁ·ρ_v/(ρ_l − ρ_v)`, et `C·dT/dt = −ṁ_vap·h_fg + hA·(T_air − T)` (liquide à 2,5 kJ/(kg·K), paroi à 2 W/K, supposés). Pour 75 kW : −0,8 bar en 10 s ; plus de liquide après 133 s, la bouteille à 8,8 °C ; 5 s plus tard, la vapeur seule donne 4,5 g/s, 85 % de moins. Les forums parlent de 80 à 120 s à 100 ch pour cette bouteille, Holley de 7 à 10 quarts de mile pour un kit de 125 ch. Mais la bouteille est ici un seul bloc à l'équilibre : la pression qui baisse nettement dès le troisième passage, que rapportent les préparateurs, vient d'un liquide qui se stratifie, et n'y est pas. Elle se remplit à chaque changement de moteur, ou par le bouton de la jauge.
- **L'activation.** Contacteur de plein gaz (pédale à 95 %), fenêtre de régime de max(3000, 0,4·régime maximal) à 97 % du régime maximal (les fabricants déconseillent d'ouvrir sous 3000 tr/min), combustion en marche. Un débit constant donne une puissance constante, donc un couple en `1/ω` : 417 Nm à 3000 tr/min sur un 2 L qui en fait 194. L'embrayage reste celui d'origine, à 1,6 fois le couple d'origine : il peut patiner.
- **La charge.** Le liquide se vaporise à la température de la bouteille, puis la vapeur et l'essence prennent celle du mélange : `ṁ_air·cp·(T3 − T) = ṁ_N₂O·[h_fg(T_b) + c_v·(T − T_b)] + ṁ_ess·[L + c_ess·(T − T_b)]`, avec la chaleur de vaporisation de l'essence lue en paramètre. À pression égale, le cylindre se partage entre les moles : le protoxyde et l'essence prennent la place de l'air, et le froid lui en rend. Sur hot_hatch_na à 6750 tr/min, −27,5 °C, et l'air admis reste à 100 % : les deux effets s'annulent, et le kit donne sa puissance (+74,5 kW). Les préparateurs annoncent 60 à 75 °F (33 à 42 K) de moins à l'admission ; ici 47 K, pour un kit de 75 kW sur un 2 L, gros pour ce moteur. L'écart au gaz parfait de la vapeur (de l'ordre de 60 kJ/kg) est ignoré : le refroidissement est plutôt sous-estimé.
- **Le son.** `EngineSound::setCharge` ajoute à la pression d'ouverture de l'échappement ce que la charge ajoute au couple, avec le rapport de `boostFill` (0,4 pour 0,9 de couple). Sur `--scenario launch`, hot_hatch_na : 89,6 → 93,2 dB(A) LAeq, la voiture allant aussi plus vite. **Aucune écoute n'a été faite.**

### Ce qui se lit, ce qui s'expose

Lu en paramètres, aux valeurs de référence tant que les sujets concernés ne sont pas branchés : `ChargeConditions` (`ambientK`, `ambientPa` de l'atmosphère, `engineCoolantK` de la thermique) et `ChargeFuel` (`latentJPerKg`, `stoichiometricAfr`, `lowerHeatingJPerKg` de la combustion, `fullLoadLambda` du calculateur). Exposé pour le cliquetis : `ManifoldCharge` (pression et température au collecteur, eau injectée et encore liquide, protoxyde, oxygène du comburant, part d'air), par `DrivingModel::manifoldCharge()` en roulant et `steadyManifoldCharge(p, rpm)` sur la fiche ; et les fonctions pures `mixCharge`, `waterSpray`, `waterSaturationPa`, `nitrousSaturation`, `chargeCoolerSteadyK`. Sur le site : `rf_charge_c`, `rf_charge_water_c`, `rf_water_gps`, `rf_nitrous_bar`, `rf_nitrous_kg`, `rf_nitrous_flow`, `rf_nitrous_refill`, `rf_nitrous_activation_rpm` ; la jauge de la bouteille et la charge sous celle du collecteur. Tests : `tests/charge_air_test.cpp`, `web/tests/air-de-charge.spec.ts` ; empreintes `golden_charge_*`.

## Le deux-temps

`EngineCycle::TwoStroke` : une explosion par tour et par cylindre, des lumières dans la chemise au lieu des soupapes, et un carter étanche qui aspire puis refoule le mélange frais. `core/src/two_stroke.cpp`, recopié pour le dessin et le gaz dans `web/src/lib/twoStroke.ts`. Le quatre-temps ne passe par rien de cette section : les 29 empreintes d'avant n'ont pas bougé. `isTwoStroke(config)` existe en C++ et sur le site ; un rotatif garde son propre cycle.

**Le cycle : la seule grandeur qui change les comptes.** Comme le rotatif, le deux-temps passe par `sweptCcPerTwoTurns` (deux fois la cylindrée) et `firingsPerTwoTurns` (deux fois les cylindres) : couple (`bmepToNm`, p·V/2π), débit d'air, carburateur, consommation et ondulation du couple en découlent. Dans le son, `firing` reste un cycle de deux tours, avec deux explosions par cylindre à 360° l'une de l'autre : l'explosion i et i + N sont celles du même cylindre.

**Les lumières, par leur hauteur.** La course du piston suit la bielle et la manivelle, x/S = ½(1 − cos θ) + λ(1 − √(1 − sin²θ/4λ²)), avec une bielle de deux courses (λ = l/S = 2, supposé). Une lumière de hauteur h s'ouvre quand x/S = 1 − h, et se ferme symétriquement après le point mort bas ; l'admission dans le carter, découverte par la jupe, s'ouvre quand il reste `inletTravel` de course avant le point mort haut. Les hauteurs croissent avec le régime maximal, qui demande la même section de passage en moins de temps (la « surface-temps » de Blair) : de 0,28 à 0,42 de la course à l'échappement, de 0,17 à 0,24 au transfert, entre 4500 et 9500 tr/min (bornes supposées, ordre de grandeur d'un moteur de tourisme et d'un moteur de course). Sur le préréglage (5000 tr/min) : échappement de 107° à 253°, soit 146° ; transfert de 124° à 236°, soit 112° ; admission 68,5° de part et d'autre du point mort haut. Les largeurs : 0,62 de l'alésage à l'échappement (la limite d'une fenêtre que le segment franchit sans s'y accrocher), 0,95 pour les transferts réunis, 0,6 à l'admission (supposées). Le taux réel, compté depuis la fermeture de l'échappement, vaut 1 + (1 − h)(ε − 1) : 5,6 pour un taux géométrique de 7,5 (Saab 96 : 7,3 ; Trabant 601 : 7,8).

**Le cycle intégré.** `twoStrokeCycle` intègre un cylindre à pas fixe de 40 µs sur quelques tours, sans constante de remplissage : un carter à zéro dimension (taux de 1,35, supposé), sa colonne d'admission avec son inertie (30 cm, la lumière, la pipe et le carburateur), les transferts et la lumière d'échappement en écoulement compressible (coefficients de débit 0,65 et 0,7, supposés), le cylindre en masse, énergie et part d'air frais, l'explosion au point mort haut (la chaleur de l'air frais enfermé, 3,0 MJ/kg dont 66 % restent au gaz, comme à l'écran). À la lumière, la ligne : chaque tronçon de section constante est un retard aller et un retard retour, et à chaque changement de section l'onde se partage, r = (A₁ − A₂)/(A₁ + A₂) (Kelly et Lochbaum) ; la bouche la renvoie changée de signe (−0,95). La pression à la lumière, p = p₀ + 2p⁻ + (c/A)·ṁ, et le débit, qui dépend d'elle, se résolvent ensemble à chaque pas (fausse position). La ligne est celle du son (`lineStretches`), avec des pertes de couche limite et de frottement turbulent (0,05 Np/m) au lieu de celles, plus larges, de la réponse sonore. Le moteur garde une table de 16 régimes, du dixième du régime maximal à 1,2 fois, calculée une fois (de 13 à 30 ms) et gardée en mémoire pour quatre moteurs.

**Le balayage.** Benson et Brandham (1969, vu cité) : une part x du cylindre chassée en déplacement parfait, le reste en mélange parfait, et une part y du débit qui file droit à l'échappement. En volume et à une seule température, SE = (1 − y)Λ tant que (1 − y)Λ ≤ x, puis SE = 1 − (1 − x)·exp(x − (1 − y)Λ). Le cylindre intégré n'ayant qu'une température, la phase de déplacement se compte sur le gaz brûlé chassé (une part x de celui du cylindre à l'ouverture du transfert), et non sur l'air entré, froid et dense. x = 0,5, comme le recommande une étude SAE de 1991 (911249, vu cité) ; y = 0,12, supposé. Ce qui sort en dernier est le plus près de la lumière : quand l'onde de retour repousse du gaz dans le cylindre, c'est d'abord celui-là, avec l'air frais qu'il contient (mémoire degré par degré).

| Saab 96, 3 cyl. 841 cm³ | 1600 tr/min | 3067 | 3800 | 4533 | 5267 |
|---|---:|---:|---:|---:|---:|
| Rapport de débit, ligne d'origine | 0,47 | 0,84 | 0,74 | 0,58 | 0,49 |
| Piégeage | 0,67 | 0,53 | 0,56 | 0,60 | 0,66 |
| Pureté enfermée | 0,75 | 0,78 | 0,75 | 0,71 | 0,68 |
| Remplissage (air frais / ρV) | 0,33 | 0,44 | 0,42 | 0,35 | 0,35 |
| Rapport de débit, chambre de détente | 0,54 | 0,94 | 0,91 | 0,82 | 0,73 |
| Remplissage, chambre de détente | 0,38 | 0,48 | 0,46 | 0,45 | 0,40 |

Ces rendements tombent dans les plages publiées pour un balayage en boucle (rapport de débit de 0,5 à 0,9, piégeage de 0,5 à 0,8 : Heywood, chapitre 6 ; Blair), le piégeage en bas de la fourchette.

**Du remplissage au couple.** Pression moyenne indiquée = η × remplissage × ρ × PCI / (A/F)ₛ, avec ρ = 1,2 kg/m³, 42,9 MJ/kg et 14,7 (le mélange riche ne brûle pas plus que l'air ne le permet). η vaut le rendement indiqué du quatre-temps atmosphérique (0,405, celui de la consommation) multiplié par le rapport des cycles d'Otto à γ = 1,3 : 5,6 face à 10,5, soit 0,80. Les frottements d'un tour sont ceux d'un quatre-temps de même cylindrée moins la distribution, un quart (supposé ; 20 à 30 % chez Heywood) ; le frein moteur, celui du seul carter qui pompe contre le papillon, la moitié (supposé). Le volant se dimensionne sur la cylindrée, pas sur le volume balayé : deux fois plus d'explosions, deux fois moins fortes. Sur un carburateur, la perte au col retire de la densité à l'air qui entre dans le carter, et son pompage.

**À charge partielle et au ralenti : les ratés.** Le papillon ne laisse passer qu'une part `fill` du débit de pleine charge ; le balayage suit le rapport des deux modèles de Benson et Brandham, ce qui garde le calcul de pleine charge à pleine charge. À faible débit, la charge enfermée est surtout du gaz brûlé : sous une pureté de 0,30, avec une dispersion triangulaire de ± 0,12 d'un cycle à l'autre (supposées), l'étincelle n'allume plus. Après un raté, le cylindre garde son mélange et le balayage suivant le complète, π' = 1 − (1 − SE)(1 − π) : le tour suivant part. C'est le « quatre-temps » des deux-temps à faible charge, et au ralenti une explosion tous les deux ou trois tours, le « ring-ding ». `twoStrokeFiring` en donne la moyenne par la chaîne de Markov des ratés successifs ; `DrivingModel` multiplie le couple par ce que l'air frais donne vraiment (`twoStrokePartLoad`), et la régulation de ralenti ouvre d'autant plus.

| Pureté de balayage | 0,75 (pleine charge) | 0,40 | 0,27 | 0,15 (ralenti) |
|---|---:|---:|---:|---:|
| Tours qui allument | 100 % | 100 % | 58 % | 36 % |

**La consommation.** L'essence arrive avec l'air (carburateur, injection indirecte) : ce qui file droit à l'échappement et ce qui repart après un raté sont perdus, carburant = brûlé / (piégeage × part qui brûle). Injectée dans le cylindre (`Direct`), elle arrive après la fermeture de la lumière : seuls les ratés coûtent. L'huile est mélangée à l'essence à 1 pour 50 en volume (Trabant 601), et part avec elle : sans mélange qui passe, pas d'huile. Le CO₂ compte tout le carburant, y compris l'imbrûlé, qui serait en réalité des hydrocarbures : il est surestimé.

**Le son.** Les lumières remplacent les soupapes : l'aire découverte suit la course du piston, avec un chanfrein de 3° au bord (supposé), et la bouffée dure le volume à l'ouverture sur la section de la lumière (1,0 ms sur le préréglage). Le balayage pousse vers l'échappement, sur la durée du transfert, le volume chaud de ce qui entre (1,3 fois l'air frais, supposé), au lieu du piston qui chasse sa cylindrée sur 240°. La pression à l'ouverture vient du cycle intégré, rapportée à celle d'un quatre-temps à l'ouverture de sa soupape : 0,72 (3,4 bar contre 4,8). Aucune soupape ne retombe ; pas de culbuteur ; le souffle mécanique perd la distribution (−1,5 dB). Bloc à −2 dB (pression maximale de 35 à 45 bar au lieu de 70, −5 dB, mais deux fois plus d'explosions, +3 dB, supposé), plancher du ralenti à −3 dB comme sur le rotatif. Les ratés tirent de leur propre générateur : la suite du moteur ne bouge pas. Chaque cylindre garde la pureté de sa charge ; un raté ouvre la lumière sur 28 % de la pression, sans coup dans le bloc.

Mesuré à régime tenu, préréglage, dehors : 65 % de ratés à 830 tr/min et 0,15 de charge, 44 % à 0,3, aucun à 0,5. Sur le spectrogramme du ralenti, en fenêtres de 40 ms, la cadence (41,5 Hz) s'accompagne de raies entre les siennes, absentes à 0,6 de charge ; la ligne d'origine de la carrosserie des années 80 a une bosse vers 83 Hz, qui domine le ralenti des deux cycles. À 3000 tr/min pleine charge, les raies tombent à 150, 300 et 450 Hz, une octave au-dessus du même moteur en quatre temps (75 Hz). LAeq dehors à 3000 tr/min pleine charge : 68,6 dB(A), 65,8 en quatre temps, 69,7 avec la chambre de détente. **L'écoute reste à faire.**

**La chambre de détente (`Exhaust::Expansion`).** Sa géométrie, par cylindre : un collecteur 1,1 fois le diamètre équivalent de la lumière, un diffuseur jusqu'à 2,75 fois lui (Blair : de 2,5 à 3,25), un ventre, un cône de retour jusqu'à une pointe de 0,6 fois le collecteur (Jennings : de 0,58 à 0,62) et longue de 12 fois son diamètre, puis un petit silencieux garni de dix fois sa section et une sortie (supposés : il faut un silencieux à une voiture). L'accord vient de la vitesse du son du gaz chaud, c = √(γRT) = 501 m/s à 650 K (supposé ; Jennings compte 1700 ft/s, 518 m/s) : la bouffée culmine à l'ouverture du transfert, et le milieu du cône de retour doit la renvoyer quand la lumière d'échappement est encore à moitié ouverte, 2L = c × trajet / 6N, au régime de puissance nominale (0,88 fois le maximum). La règle de Jennings, qui renvoie l'onde à la fermeture même, donnait dans le cycle intégré un sommet 25 % sous le régime visé : la lumière presque fermée ne laisse rien repousser. Les parts de longueur (collecteur 0,27, diffuseur 0,38, ventre 0,17, cône 0,36, ce dernier compté pour moitié) sont supposées. Le diffuseur renvoie une dépression pendant le balayage, qui fait entrer plus d'air dans le carter ; le cône de retour, une surpression qui repousse le mélange frais avant la fermeture. Sur le préréglage : 0,27 m de collecteur, 0,39 de diffuseur, 0,17 de ventre, 0,37 de cône, 38, 104 et 23 mm de diamètres.

La réponse sonore se calcule depuis cette géométrie par les mêmes matrices de transfert que les autres lignes (`ExhaustLine::computeExpansion`), les cônes par huit et six marches ; N chambres en parallèle ne font qu'un tube de N fois leur section, mêmes longueurs, donc même accord. Sur un quatre-temps (la CLI seule le permet), la chambre s'accorde sur la soupape, de 20° après l'ouverture à 30° avant la fermeture, et respire comme une ligne sport : les soupapes ferment le cylindre avant que l'onde n'y repousse rien. Les détonations y sortent autant qu'une ligne sport (`POP_JOULES`, ligne du turbo).

| Préréglage (Saab 96 en 3 cyl. 841 cm³, 813 kg) | Couple | Puissance | 0 à 100 km/h | 90 km/h | WLTC | Huile, 90 km/h |
|---|---|---|---|---|---|---|
| Ligne d'origine | 56,5 Nm à 3050 | 20,2 kW à 3800 | 33,3 s | 7,73 L/100 | 11,83 L/100 (22,9 km sur 23,3) | 1,5 L/1000 km |
| Chambre de détente | 60,9 Nm à 3050 | 26,1 kW à 4900 | 26,9 s | 8,00 L/100 | 12,35 L/100 | 1,6 L/1000 km |
| Injection dans le cylindre | 59,2 Nm à 3050 | 21,5 kW à 3800 | 30,2 s | 4,67 L/100 | 6,21 L/100 | 0,9 L/1000 km |
| Même moteur en quatre temps | 62,9 Nm à 2600 | 24,5 kW à 4550 | 26,1 s | 4,93 L/100 | 6,24 L/100 | 34 mL/1000 km |
| Saab 96, 1960-1966 (musée Saab, lu) | 79 Nm à 3000 | 38 ch (28 kW) à 4250 | 0 à 97 km/h en 25,6 s | 32,3 mpg en essai | | |

Le modèle lit 28 % sous la Saab en couple et en puissance, et sous le même moteur en quatre temps, quand de vrais deux-temps de l'époque faisaient mieux par litre. Le piégeage calculé au couple maximal (0,53) est en bas de la plage d'un balayage en boucle ; le taux réel de 5,6, le court-circuit de 0,12 et les largeurs de lumières sont supposés ; rien n'a été réglé pour coller à la Saab. Le meilleur point de la carte (492 g/kWh, à faible régime, 267 en quatre temps) et la consommation à 90 km/h (7,7 L/100, contre 32,3 mpg, soit 7,3 L/100 en gallons américains ou 8,7 en gallons impériaux, la source ne le dit pas) disent la perte par court-circuit : injectée dans le cylindre, la même essence fait 40 % de chemin en plus, de l'ordre de ce qu'annonçaient les deux-temps à injection directe (Orbital, de mémoire). Au WLTC, le préréglage ne suit pas la fin du cycle (22,9 km sur 23,3) : 20 kW ne tiennent pas 131 km/h. Ralenti : 1,0 L/h.

**Les cas à part.**
- **Le deux-temps diesel** (Detroit Diesel, à balayage en équicourant par soufflante Roots et soupapes d'échappement) n'est pas simulé : son balayage ne passe pas par un carter, ni ses lumières par le seul piston. `coherentTwoStroke` (C++, CLI et WASM) et `coherentConfig` (site) ramènent un deux-temps diesel à l'essence, comme un rotatif.
- **Le deux-temps suralimenté** : sous un turbo seul, rien ne balaierait le cylindre au démarrage, la turbine n'ayant encore aucune énergie (les diesels de locomotive EMD entraînent leur turbo par engrenages pour cela). Un compresseur qui pousse le carter changerait tout le calcul du balayage, sans source pour le caler. Les deux reviennent donc à l'atmosphérique, avec un texte dans le volet.
- **Au plus six cylindres** : chacun allume deux fois en deux tours, et le son compte au plus 12 explosions par cycle. Un vilebrequin à plat croisé n'a pas de sens ici : il revient au V.
- La came, le calage variable, les culbuteurs et les ressorts n'ont pas de sens : le socle cache déjà la distribution, et `valvetrain()` rend le double arbre d'origine, qui ne passe par aucun calcul de soupape.

**À l'écran.** Le gaz (`GasNetwork`) calcule chaque cylindre et son carter, un volume à zéro dimension qui suit le piston, avec les trois lumières découvertes par lui et les mêmes coefficients que le cycle C++ : on y voit le carter monter au-dessus de l'atmosphère piston en bas, le mélange passer par les transferts, et du mélange frais filer jusqu'à la sortie. La chambre de détente y est accordée sur le moteur (`expansionSections`), ses cônes en quelques marches de 3 cellules au moins ; `EXHAUST_LINES.expansion` n'est que celle du préréglage, pour le dessin. La vue du gaz trace ses cônes en pente. La fumée bleue est le traceur des suies du diesel, teinté de bleu : d'autant plus épaisse que le gaz qui sort est frais (l'huile ne brûle qu'à moitié dans la charge enfermée, supposé). La voiture dessine la chambre à ses vraies dimensions sous le plancher.

**Ce qui reste simplifié.** Pas de clapets ni de disque rotatif à l'admission, seulement la jupe du piston. Pas de ralenti en roue libre (les Saab deux-temps en avaient une, pour que l'huile arrive pied levé). L'huile ne lubrifie rien dans le calcul : sans mélange, rien ne grippe. Les bouffées des autres cylindres ne passent pas dans la ligne du cycle intégré : chacun n'a que sa part de la ligne commune. L'onde est linéaire : la vitesse des grandes amplitudes n'y est pas. La pureté minimale et sa dispersion sont supposées, et l'écoute dira si le ring-ding en sort. Sources lues : fiche du musée Saab (saabmuseum.com, Saab 96 1960-1966), fiche de la Westsächsische Hochschule Zwickau (Trabant P 601 : 594 cm³, 72 × 73, 26 ch à 4240 tr/min, 7 L/100 km, 620 kg), Wikipédia anglaise (Trabant 601 : 54 Nm à 3000 tr/min, taux de 7,8, mélange 1:50). De mémoire ou vu cité : Benson et Brandham (1969), Blair (*Design and Simulation of Two-Stroke Engines*, 1996), Jennings (*Two-Stroke Tuner's Handbook*), Heywood (1988), l'étude SAE 911249.

## Quand les réglages se rencontrent (2)

Les douze sujets du moteur réunis : architectures, atmosphère, thermique, combustion, calculateur, injection, came, culbuteurs, régulation de pression, biturbo, air de charge, deux-temps. Ici, ce qui n'existe que réuni. Au défaut, rien ne change : les 29 empreintes d'origine et les préréglages restent au bit près (la Cadillac mise à part, voir plus bas). Huit empreintes de sujets changent, chacune expliquée dans `CMakeLists.txt`. Les tests sont dans `tests/jonctions_test.cpp` (huit cas de plus, chacun contre-éprouvé en retirant ce qu'il protège) et `web/tests/jonctions.spec.ts`.

### Ce qui est branché

**L'air du jour.** Le radiateur est dimensionné dans l'air de référence, et traversé par celui du jour (`ThermalSpec::airPressureRatio`). Une wastegate à ressort tient une pression relative, le calculateur une pression absolue, plafonnée par la vitesse des roues en rapport de pression (`boostTargetPa`, `BoostControlInput::ambientPa`) : à 3000 m, le ressort vise 2,0 bar absolus, l'électronique 2,31 jusqu'à 4100 tr/min, puis 2,03 à 5900, l'arbre à sa vitesse limite. L'échangeur air-air refroidit vers l'air du jour (`coolerOutlet`). La carte du cliquetis lit la pression d'un atmosphérique ; la température lui arrive déjà par l'air d'admission. Tout ce qui se dimensionne une fois (turbo, compresseur, boîte, embrayage, carte de passage, étalonnage de la pression) l'est dans l'air de référence (`withReferenceAir`).

**La carte du cliquetis** se construit dès que change ce qu'elle lit (`knockConditionsStock`), plus seulement la combustion : l'air, la came (la fermeture de l'admission), l'injection directe (ce que sa vaporisation refroidit de plus qu'une injection indirecte), l'avance du calculateur et l'air de charge (la charge au collecteur). Un jour à 45 °C la rapproche de sa limite ; la carte d'origine, réglée dans l'air de référence, ne bouge pas.

**L'avance du calculateur.** Sur un quatre-temps à étincelle, la part de couple que change le décalage d'avance vient de la carte du cliquetis seule (`ecuAdvanceInKnockMap`) : elle part de la carte d'origine réelle, en retard sur le meilleur couple là où le cliquetis la borne, là où le calculateur supposait le meilleur couple. Le cycle de cette carte rend pourtant son meilleur travail environ 9° plus tôt que son critère de meilleur couple (la moitié de la combustion à 8° après le point mort haut) : à 3000 comme à 6000 tr/min, −10° coûtent 6,5 %, −6° 3,4 %, et +6° gagnent 1,6 %, +10° 1,7 %, là où la parabole du calculateur perdait 2,2 et 0,8 % des deux côtés. La courbure est celle de la parabole ; c'est le sommet qui est décalé. À trancher : aligner le critère sur le sommet du cycle (pertes aux parois), ou garder la parabole pour le décalage. Carburant et chaleur du gaz gardent le retard du calculateur. Ce que le capteur retire, `ecuAdvanceDeg` le lit. Sur le 2.0 atmosphérique, +6° : 194,1 → 197,9 Nm, 117,8 → 120,3 kW ; sous 2250 tr/min la carte d'origine n'est qu'à 2° de sa limite à pleine charge, la marge tombe à −4,0° (−2,2° en SP98), et le capteur reprend de l'avance en reprise. La fiche (`rf_knock_margin_deg`, `--knock`) lit cette même carte.

**La thermique.** Après un départ à froid, l'eau du réseau fait le film de l'injection indirecte et l'enrichissement du calculateur (une seule loi, `coldEnrichment`) ; la paroi froide, à la température du métal, réchauffe moins l'air piégé (`trappedAirK`). Le ralenti accéléré garde l'autorité du démarrage (0,8 du remplissage, contre 0,45 moteur chaud) : limité comme moteur chaud, il ne tenait pas l'huile froide de la Cadillac peu comprimée. Parti chaud, rien de tout cela, au bit près.

**La vaporisation** n'est comptée qu'une fois : le carburant choisi en donne la chaleur latente (`fuelPropertiesOf`), l'alimentation la part prise à l'air (`mixtureAirShare` : 0,5 au carburateur, 0,2 en injection indirecte ; l'injection directe a son calcul de gouttes). La combustion, l'injection directe (`injectedFuelOf`), l'air de charge et la carte du cliquetis lisent les mêmes chiffres. Avant, la combustion prenait 0,15 pour toute alimentation, et l'injection 305 kJ/kg pour toute essence, éthanol compris.

**Le biturbo.** Une turbine par culasse : un W en compte deux (`architectureOf`). L'échangeur commun refroidit vers l'air du jour. La régulation électronique et selon le rapport ne donnent que leur cible à la loi des soupapes du biturbo. Leur PID et leur anticipation sont étalonnés sur le banc d'un turbo seul : branchés tels quels, en reprise en troisième depuis 2000 tr/min (V6 de 3 L, 1 bar), ils dépassaient la cible de 0,49 bar sur un séquentiel, dont l'étagement oscillait, et de 0,13 bar en parallèle, contre 0,24 et 0,02 bar pour la loi du biturbo. Un étalonnage sur le banc de deux turbos reste à faire. Selon le rapport, la cible du W12 baisse en première de 0,29 bar, et ses deux arbres la suivent.

**Le deux-temps.** Le taux réglé est son taux géométrique (7,5 d'origine) : il change le taux réel que laisse la lumière d'échappement, et la combustion ne le compte pas une seconde fois (`compressionEfficiencyShare` vaut 1). La carte du cliquetis comprime depuis la fermeture de la lumière. À 9:1, le couple gagne plus de 3 %. Au plus six cylindres, et ni VR, ni W, ni vilebrequin croisé : ils reviennent au V.

**La came et les culbuteurs.** L'affolement lit le profil de la came (levée et durée, celles de la bascule sur deux profils). Le rupteur reste réglé sur l'affolement de la came d'origine : une came pointue s'affole avant lui. Aucun préréglage n'est touché : leur came d'origine s'affole au moins 9 % au-delà de leur zone rouge. La bascule d'un calage à deux profils lit l'affolement.

**L'air de charge** lit le carburant choisi et la richesse du calculateur.

### Les doubles comptes

- **Vaporisation** : une seule fonction, ci-dessus. L'injection d'eau s'ajoute à part : c'est de l'eau.
- **Filtre à particules** : `kDieselFilter`, dans la ligne d'échappement, était la turbine d'un diesel et la masse de sa ligne, pas un filtre : il reste. Le filtre calculé (`ParticulateFilter`) ajoute sa résistance (`filterResistance`) : 61,2 → 61,0 dB(A) sur le diesel à 2500 tr/min.
- **Air piégé** : la paroi (atmosphère) réchauffe l'air qui sort de l'échangeur (air de charge), une fois : `trappedAirK` lit la charge au collecteur, déjà refroidie.
- **Rendement** : le taux (combustion), l'avance et la richesse (calculateur), le gain de remplissage de l'injection directe et la came se multiplient, chacun d'un effet distinct. L'avance ne passe plus par le calculateur (ci-dessus) ; sur un deux-temps, le taux n'est compté que par son cycle. Le pompage n'est compté qu'une fois, dans les frottements (`pumpingTorqueNm`), plus la contre-pression du filtre ; la came compare des pressions moyennes indiquées dans le même air, et la recirculation n'agit qu'à charge partielle.

### Ce qui n'est pas branché, et pourquoi

- L'air du cycle de la came reste à 20 °C et 101 325 Pa : elle donne un rapport entre deux cycles dans le même air, d'ordre deux.
- La carte du cliquetis et la chaleur du compartiment que lit l'air de charge restent celles d'un moteur chaud : elles se construisent une fois par réglage, et un moteur froid cliquette moins.
- Les arbres du biturbo comptent dans l'air de référence (`turbo_laws::constants().atmosphere`) : seuls l'échangeur et la cible le voient. Le PID sur deux turbos : ci-dessus.
- La fiche d'une régulation électronique en altitude reste un équilibre en pression relative : 301 Nm à 3000 m comme au ressort ; seule la conduite voit la cible absolue (0 à 100 km/h en 8,56 s contre 8,65).
- La consommation ne voit pas le couple de l'avance lu par la carte du cliquetis ; la chaleur du gaz garde la parabole du calculateur.
- La volatilité du carburant dans le film d'une injection indirecte reste celle de l'essence.

### Les rencontres, en chiffres

| Rencontre | Mesure |
|---|---|
| E85 et injection directe, 2.0 turbo à 11:1 | SP95 : 349 Nm, 192 kW, marge −6,4° ; E85 : 396 Nm, +2,9° ; SP95 directe : 382 Nm, −4,0° ; E85 directe : 455 Nm, 252 kW, +12,7°. La directe gagne 2,4° en SP95 et 9,8° en E85 : trois fois plus de chaleur latente, une fois et demie plus de carburant. |
| Départ à froid en altitude (citadine, WLTC) | Au niveau de la mer, 6,76 → 6,90 L/100 (+2,1 %) ; à 2500 m et −10 °C, 6,16 → 6,54 (+6,2 %), avec une relance après un calage. |
| Came pointue et ressorts d'origine (2.0, zone rouge 7300) | Affolement à 8140 tr/min d'origine ; 12,5 mm sur 200° : 6490 (−11 %), sous le rupteur ; ressorts durs : 7394 (+1,3 %) ; deux profils : 8752. |
| Biturbo séquentiel et électronique (V6 3 L, 1 bar, reprise depuis 2000) | 90 % de la pression en 1,76 s sur un turbo, 0,62 s en séquentiel, 0,43 s en twincharger, 1,41 s en parallèle ; le séquentiel dépasse de 0,24 bar à l'étagement. |
| W16 en biturbo parallèle | Même équilibre (1306 Nm) ; 0 à 100 km/h en 3,61 s contre 3,82 sur un turbo ; 90 % de la pression en 1,13 s contre 1,38 depuis 2500 tr/min. |
| Deux-temps en altitude | 56,5 Nm et 20,2 kW au niveau de la mer ; à 2000 m, 43,1 Nm (−24 %, quand l'air ne perd que 22 %) et 15,3 kW : 100 km/h n'est plus atteint ; 7,73 → 7,16 L/100 à 90 km/h. |
| Protoxyde et came de course (2.0, 75 kW) | 194 → 417 Nm, 118 → 192 kW ; came de 280° seule : 174 Nm, 121 kW, marge 29,7° (fermeture tardive) ; les deux : 379 Nm, 194 kW. |
| Launch control et twincharger (2.0 turbo) | 0 à 100 km/h : 6,86 s ; launch à 4500 : 6,13 ; twincharger : 6,35 ; les deux : 6,15. Le compresseur remplit déjà au départ : le launch ne lui apporte que 0,2 s. |
| Altitude et régulation (2.0 turbo, 3000 m) | 357 → 301 Nm ; 0 à 100 km/h en 8,65 s au ressort, 8,56 en électronique, qui dépasse sa cible de 0,18 bar en reprise. |

### Le reste

- **Cadillac** : son taux publié (5,35) sortait de la plage ; elle descend à 4:1, et le préréglage le prend. 456 Nm et 138 kW, contre 367 à 432 Nm et 123 à 138 kW publiés (578 Nm et 179 kW à 10,5). Partie à froid, elle tenait son ralenti par 5 °C, pas par 0 °C ; depuis les finitions, elle le tient par −30 °C (« Le démarrage à froid »).
- **`decideShift`**, changé par le deux-temps : les empreintes des trajets, du launch control, du passage à fond et de `by_gear` restent au bit près.
- **Biturbo et anti-lag** : le C++ ignore l'anti-lag et le tube sur deux turbos, un twincharger ou des aubes ; `coherentConfig` les ramène au défaut, et le volet le dit, avec la cible seule de la régulation.
- **À l'écran**, la ligne du cliquetis (`Dynamics`) s'affiche dès que la carte se construit, avec les mêmes conditions que `knockConditionsStock`.

L'écoute reste à faire : le cliquetis d'une avance de +6° (`--preset hot_hatch_na --advance 6 --scenario turbo`), l'E85 en injection directe (`--preset hot_hatch_turbo --compression 11 --grade e85 --fuel-system direct --scenario turbo`), la Cadillac à son taux (`--preset v16_cadillac --scenario lap`) et le départ à froid en altitude (`--preset city_14 --start cold --altitude 2500 --ambient -10 --scenario start`).

## Le pneu, l'antipatinage et le départ

Le pneu a maintenant un sommet d'adhérence, au-delà duquel il perd de la force : l'antipatinage, le launch control, la cible selon le rapport et la logique de passage en tirent leur sens. Les 0 à 100 km/h cités plus haut dans ce document ont été mesurés avec l'ancien pneu, sans sommet ; le tableau de cette section fait foi. Code : `core/src/tyre.cpp`, `DrivingModel::integrate` ; tests : `tests/tyre_test.cpp`, `tests/cold_start_test.cpp`.

### La loi du pneu

La « formule magique » de Pacejka, en glissement longitudinal pur :

```
F = D·sin(C·atan(φ)),   φ = B·κ − E·(B·κ − atan(B·κ)),   D = μ·N,   B·C·D = K
κ = (ωr − v) / max(v, 1 m/s)        (norme SAE J670)
```

N est la charge de l'essieu moteur, report de charge compris, comme avant. B, C et E viennent de trois caractéristiques publiées, pas d'un essai de pneu :

| Caractéristique | Valeur retenue | Source |
|---|---|---|
| Sommet μ = D/N | 1,05 (`kTyreGrip`, inchangé) | pDx1 ≈ 1,04 à 1,21 pour un 205/60R15 (Pacejka, *Tyre and Vehicle Dynamics*, de mémoire) |
| Raideur K/N | 21,5 | pKx1 ≈ 21,5 pour le même pneu (Pacejka, de mémoire) |
| Glissement franc, κ = 1 | 0,76 du sommet (C = 1,5) | Bosch, *Kraftfahrtechnisches Taschenbuch* : béton sec, pneu neuf, 50 km/h, μ 0,85 au sommet et 0,65 roue bloquée (de mémoire). D'autres sources donnent 0,65 (Burckhardt, asphalte sec) à 0,88 (Wong : 0,75 pour 0,85) ; le C = 1,65 de Bakker, Nyborg et Pacejka (SAE 870421) donnerait 0,63 |
| Sommet | κ* = 11,0 % (E = −0,45) | 8 à 12 % demandés ; 12 % environ pour le 205/60R15 de Pacejka |

D'où B = K/(C·μ) = 13,65 ; 90 % du sommet à κ = 6,5 %, 76 % à κ = 1, 71 % roue lancée dans le vide. Toutes les fonctions passent par `revforge::portable` (une arc tangente portable s'ajoute à `portable_math.hpp`, à 2·10⁻¹⁶ de celle de la bibliothèque) : le pneu revient à chaque pas.

**L'intégration.** À basse vitesse, la raideur du pneu se compte en dizaines de kN par m/s : sur un essieu de 8 kN à 1 m/s, dF/dv ≈ 180 kN·s/m, et la roue (1,6 kg·m² avec le pont) battrait d'un pas de 1 ms à l'autre (dt·r²·dF/dv/I ≈ 10). La roue est donc intégrée implicitement, la force linéarisée au pas (`TyreContact::perWheelMs`), et la caisse reçoit la même force, celle de la roue en fin de pas (`tyreForceN`). Au-delà du sommet, la pente est négative : rien n'y est linéarisé, et la roue s'emballe, comme une vraie. Sur un départ à fond du V8 de 7 L, le glissement change de sens 38 fois en 6000 pas (les à-coups de la transmission et le rupteur) ; sans le terme implicite, il bat à chaque pas (test).

**Supposé :** la vitesse de référence de 1 m/s sous laquelle le glissement se rapporte à elle (à l'arrêt, ωr − v n'a plus de vitesse à diviser) ; une adhérence sèche unique pour toutes les voitures, les deux roues d'un essieu réunies ; ni sensibilité à la charge (pDx2, pKx2), ni glissement combiné avec un virage, ni longueur de relaxation, ni température du pneu. Le frein agit toujours sur la caisse, à μ·N au plus, comme un ABS au sommet.

### Ce que les réglages en tirent

**L'antipatinage** vise un glissement κ : le sommet en sport (11,0 %), 90 % du sommet en complet (6,5 %), qui garde √(1 − 0,9²) = 44 % de l'adhérence latérale par le cercle de Kamm (supposé). L'écart se compte en m/s, `ωr − v − κ_cible·max(v, 1 m/s)` : c'est la vitesse que l'inertie de la roue intègre, et le même retrait de couple la corrige à toute allure. Ses gains ne sont plus réglés à l'essai : ils sont placés sur la transmission du rapport engagé. Tout le couple du moteur donne au glissement une accélération `a = r·T_max·g·η/J`, J l'inertie vue de la roue (roues, primaire et moteur ramenés par le rapport) ; la boucle a ses pôles à ω = 6 rad/s (1 Hz, sous la torsion des arbres de roue, 2 à 3 Hz en première) et ζ = 0,7 (supposés) : `kp = 2ζω/a`, `ki = ω²/a`. Au départ, il agit aussi pendant que l'embrayage patine : le robot ne le serre que jusqu'au couple que le moteur annonce, ce que le calculateur retire compris (ci-dessous). Avant, il n'agissait qu'embrayage serré, et sa cible se lisait en m/s sur un pneu sans sommet : il ne pouvait que retenir le patinage.

**Le robot du départ** (la branche du démarrage de `updateClutch`) serrait l'embrayage selon le seul régime, jusqu'à 1,6 fois le couple du moteur : le volant moteur, monté au-dessus du régime de départ, se vidait dans les roues. Le pneu sans sommet absorbait ce surplus ; avec un sommet, il fait patiner, et l'antipatinage, qui n'agit que sur l'allumage, n'y pouvait rien tant que l'embrayage patinait. Tant que le moteur entraîne, le robot ne serre plus au-delà du couple annoncé, `part du calculateur × remplissage × couple de pleine charge`, plus de quoi ramener le moteur à son régime de départ en 0,3 s (`J·(ω − ω_départ)/0,3 s`, supposé) : les roues reçoivent son couple, et son élan peu à peu, plus d'un coup. Une fois les disques à 150 tr/min l'un de l'autre (supposé), pied posé, il n'y a plus d'élan à vider : il serre, et l'embrayage colle. Pied levé, il reste plafonné : sans cela, la W16 qui ralentit en côte de 16 % était entraînée par ses roues jusqu'à caler. Sans cette dernière règle, il restait à moitié ouvert jusqu'à 25 km/h, et l'éco et le normal montaient la deuxième au même régime. Le couple annoncé comptant ce que retire le calculateur, l'antipatinage agit dès le départ : sans ce plafond, le 0 à 100 en sport perdait de 0,05 à 0,5 s (`v8_supercharged` : 6,27 → 6,74 s ; mesuré sans le retour au régime de départ), en complet jusqu'à 0,4 s ; sans antipatinage, 0,02 s au plus. Sans le retour au régime de départ, le moteur restait au régime où il s'était emballé, et l'embrayage patinait jusqu'à 25 km/h. Contre-épreuve : sans ce plafond, la W16 cale au départ en côte de 16 %, dans les quatre modes : le test « chaque trajet va au bout » échoue.

| 0-100 km/h pied au plancher | Pneu sans sommet : sans / sport / complet | Pneu à sommet : sans / sport / complet |
|---|---|---|
| V8 de 7 L, propulsion, 1700 kg | 6,18 / 6,20 / 6,86 s | 8,55 / 6,25 / 6,48 s |
| Le même, launch control à 3500 tr/min (sans ; sport) | 6,03 ; 6,05 s | 8,40 ; 6,08 s |
| `hot_hatch_turbo`, quatre roues motrices | 6,86 s partout | 6,82 s partout |

Sans antipatinage, pied au plancher, la propulsion patine bien au-delà du sommet en première et en deuxième (κ de 1 à 10) : elle perd 2,4 s. Avec, elle retrouve l'ancien chiffre à 0,05 s près : l'ancien pneu ne donnait sa pleine force qu'en patinant de plusieurs m/s. L'essayeur de la fiche (ci-dessous), sans antipatinage, n'en perd que 0,5 s : 6,72 s.

**Le launch control** ne change pas : il tient le régime, et lâche à 1 m/s. Sans antipatinage, il ne fait souvent que patiner plus fort (ci-dessous, colonne « sans, launch ») ; avec, il gagne jusqu'à 0,7 s, sauf sur `three_cyl_turbo`, `v10_open` et `v12`, où il coûte jusqu'à 0,17 s. `hot_hatch_turbo` à 4500 tr/min : 6,13 → 6,16 s ; avec le passage à fond : 6,54 → 6,51 s.

**La cible selon le rapport (`by_gear`)** plafonne le couple à ce que passent les pneus au sommet, report de charge compris (`gearTorqueCapNm`, inchangée). Elle fait maintenant gagner du temps pied au plancher : `three_cyl_turbo`, 0 à 100 km/h en 10,48 s au ressort, 10,47 en électronique, 10,19 selon le rapport (10,05, 10,04 et 10,26 avec l'ancien pneu). L'essayeur de la fiche, qui dose déjà la pédale, n'a plus rien à y gagner : 10,04, 10,14 et 10,19 s.

**La logique de passage** (`decideShift`) disait « les pneus sont à leur limite » au-delà de 0,8 m/s de glissement : c'est maintenant au-delà du sommet, κ > κ* (`ShiftView::slipRatio`). Tant que les roues patinent au-delà du sommet, le régime ne dit plus la vitesse de la voiture : elle ne monte un rapport que s'il retombe à 1,3 fois le ralenti au moins à la vitesse de la voiture elle-même (supposé), comme le lit une boîte aux roues non motrices ; la protection du moteur au régime maximal y renonce aussi, le rupteur le protégeant en attendant (sans cela, le V8 à compresseur en côte de 16 % montait la deuxième à 9 km/h, et calait). Sans cela, la propulsion de 3 L du départ en côte (12 %, pied à 55 %, mode éco) patinait, montait la deuxième à 4 km/h, et calait. Le mode normal sur route plate, qui n'y passe pas, reste la logique d'origine.

**La fiche** (`measurePerformance`) mesurait le 0 à 100 pied au plancher, sans antipatinage : avec un pneu à sommet, c'était chronométrer une propulsion qui fume ses pneus. Elle mesure maintenant le meilleur départ d'un essayeur, comme un essai de presse. Il pose d'abord le pied au plancher et laisse faire la boîte. Si les roues ont dépassé le sommet (κ > κ*), sans antipatinage ni launch control, il refait huit départs en passant lui-même les rapports, au régime où la boîte les passerait pied au plancher :
- il part pied à 40, 60, 80 ou 100 % de sa course, ce qui fixe le régime auquel le robot fait patiner l'embrayage : il choisit ainsi son régime de départ ;
- une fois l'embrayage serré, soit il passe au plancher, soit il dose la pédale sur le patinage qu'il sent. Ce dosage est la boucle de l'antipatinage, glissement en m/s vers le sommet, gains placés sur la même transmission (`DrivingModel::slipPlantGain`), mais à la bande passante d'un pilote humain : selon le modèle de McRuer, l'ensemble pilote et véhicule se comporte près de la coupure comme un intégrateur de pulsation ω_c, retardé de τ (McRuer et Jex, *A Review of Quasi-Linear Pilot Models*, 1967 : ω_c de 2 à 5 rad/s, τ de 0,1 à 0,3 s, de mémoire). Retenus : ω_c = 3 rad/s, ζ = 0,7, et ce qu'il sent date de 0,2 s (supposés).

Il garde le meilleur temps ; un départ déjà plus lent est abandonné en route, ce qui ne change rien au chiffre. Avec l'antipatinage ou le launch control réglés, la fiche les prend tels quels, pied au plancher ; une électrique, dont l'onduleur dose déjà le couple, aussi. Une voiture qui ne patine pas garde son chiffre au centième. En conduite (site, trajets, pilote automatique), rien ne change : les empreintes sont les mêmes. Sur le V8 de 7 L : 6,72 s, contre 8,55 pied au plancher et 6,25 avec l'antipatinage sport. Le départ choisi à 40 % gagne le plus souvent : à 30 %, trois préréglages gagneraient encore jusqu'à 0,2 s, les autres rien. La vitesse de pointe ne conduit pas (`topSpeedKmh`, à part, que lit `rf_top_speed` sans refaire le 0 à 100).

### 0-100 km/h de tous les préréglages

Mesuré par `measurePerformance` : `revforge-render --out none --preset X --scenario idle --seconds 1 --fuel 1 [--traction sport|full] [--launch RPM]`. « Fiche » est l'essayeur ci-dessus, sans antipatinage ; « Au plancher », pied au plancher sans antipatinage, la boîte laissée faire, ce que la fiche mesurait jusqu'ici ; « Ancien pneu », ce même départ avec le pneu sans sommet. Les colonnes suivantes sont la fiche avec ces réglages, donc pied au plancher. Le launch control est réglé à 60 % du régime maximal (de 2000 à 7000 tr/min, supposé). Les chiffres publiés sont ceux d'une voiture comparable (constructeur ou essais de presse, de mémoire) : un ordre de grandeur, jamais une cible ; aucun préréglage n'a été retouché.

| Préréglage | Ancien pneu | Fiche | Au plancher | Sport | Complet | Sans, launch | Sport, launch | Publié (voiture comparable) |
|---|---|---|---|---|---|---|---|---|
| `city_14` (57 kW, 1100 kg, traction) | 13,34 | 13,44 | 13,49 | 13,46 | 13,58 | 13,34 | 13,35 | 12 à 14 s (citadines 1.4 de 55 à 70 kW) |
| `four_cyl_sport` (115 kW, 1300 kg, traction) | 9,09 | 9,22 | 9,42 | 8,94 | 9,28 | 9,27 | 8,85 | 8 à 9 s (compactes 2.0 de 110 à 120 kW) |
| `three_cyl_turbo` (95 kW, 1150 kg, traction) | 10,05 | 10,04 | 10,48 | 9,66 | 9,82 | 10,79 | 9,83 | 9,4 s (Fiesta 1.0 EcoBoost 125 ch) |
| `hot_hatch_turbo` (194 kW, 1400 kg, 4 roues) | 6,86 | 6,82 | 6,82 | 6,82 | 6,82 | 6,16 | 6,40 | 5,7 à 6,1 s (Golf R VI) |
| `hot_hatch_na` (118 kW, 1250 kg, traction) | 8,72 | 9,06 | 9,23 | 8,67 | 8,97 | 9,09 | 8,59 | ≈ 8 s (Focus ST170) |
| `five_cyl_turbo` (297 kW, 1500 kg, 4 roues) | 5,29 | 5,25 | 5,25 | 5,25 | 5,25 | 6,73 | 4,89 | 4,1 s (RS3 8V, launch) |
| `inline_six` (223 kW, 1500 kg, propulsion) | 6,48 | 6,89 | 8,47 | 6,37 | 6,60 | 6,96 | 6,20 | 5,2 s (M3 E46, 252 kW) |
| `v6_gt` (146 kW, 1650 kg, propulsion) | 8,33 | 8,58 | 9,65 | 8,83 | 8,81 | 9,44 | 8,62 | ≈ 8 s |
| `flat_six` (250 kW, 1450 kg, propulsion) | 4,83 | 5,72 | 6,15 | 5,42 | 5,61 | 6,10 | 5,19 | 4,8 s (911 Carrera S 997) |
| `boxer_four_turbo` (253 kW, 1450 kg, 4 roues) | 5,45 | 5,41 | 5,41 | 5,41 | 5,41 | 5,60 | 5,09 | ≈ 5 s (Impreza WRX STI) |
| `v8_cross_plane` (214 kW, 1700 kg, propulsion) | 6,95 | 6,92 | 9,33 | 6,98 | 7,10 | 7,48 | 6,57 | ≈ 5,5 s (Mustang GT 2005) |
| `v10_open` (410 kW, 1500 kg, 4 roues) | 3,49 | 3,79 | 4,01 | 3,69 | 3,87 | 3,96 | 3,73 | 3,7 s (Gallardo LP560-4) |
| `v12` (544 kW, 1650 kg, 4 roues) | 3,32 | 3,73 | 4,00 | 3,52 | 3,79 | 3,94 | 3,63 | 2,9 s (Aventador LP700-4) |
| `diesel_four` (114 kW, 1950 kg, traction) | 12,94 | 12,70 | 12,70 | 12,68 | 12,73 | 13,87 | 12,01 | 11 à 12,5 s (Transporter 2.0 TDI) |
| `electric` (190 kW, 1800 kg, propulsion) | 7,74 | 7,74 | 7,74 | 7,74 | 7,74 | — | — | 6,1 s (Model 3 propulsion) |
| `retro_coupe` (101 kW, 1150 kg, propulsion) | 8,75 | 9,00 | 9,75 | 9,11 | 9,36 | 9,64 | 8,98 | 8,3 s (325i E30, 125 kW) |
| `v8_supercharged` (394 kW, 1850 kg, propulsion) | 6,99 | 6,75 | 8,72 | 6,28 | 6,72 | 8,48 | 6,25 | 4,0 à 4,3 s (Camaro ZL1, CTS-V) |
| `carb_twin_cam` (90 kW, 1040 kg, propulsion) | 8,65 | 8,82 | 9,35 | 9,06 | 9,13 | 9,14 | 8,85 | ≈ 9 s (Alfa Romeo 2000 GTV) |
| `twin_rotor_13` (167 kW, 1300 kg, propulsion) | 7,13 | 7,44 | 7,68 | 7,07 | 7,20 | 7,55 | 6,83 | 6,4 s (RX-8 231 ch) |
| `vr6_golf` (110 kW, 1285 kg, traction) | 9,06 | 9,45 | 9,56 | 9,09 | 9,43 | 9,50 | 9,03 | 7,8 s (Golf III VR6) |
| `w12_continental` (406 kW, 2385 kg, 4 roues) | 5,38 | 5,39 | 5,39 | 5,39 | 5,39 | 5,32 | 4,93 | 4,8 s (Continental GT 2003) |
| `w16_hypercar` (671 kW, 1888 kg, 4 roues) | 3,61 | 4,07 | 4,16 | 3,66 | 3,84 | 4,02 | 3,49 | 2,5 s (Veyron 16.4) |
| `v16_cadillac` (138 kW, 2400 kg, propulsion) | 12,37 | 12,36 | 12,88 | 12,48 | 12,60 | 15,27 | 12,41 | — |
| `two_stroke_triple` (20 kW, 813 kg, traction) | 33,26 | 33,21 | 33,21 | 33,21 | 33,21 | 33,00 | 33,00 | 25,6 s à 97 km/h (Saab 96, 28 kW) |

Ce qui se lit :
- **Pied au plancher, sans antipatinage**, les propulsions patinent au-delà du sommet et perdent de 0,5 s (`twin_rotor_13`) à 2,4 s (`v8_cross_plane`) ; les tractions, de 0,1 à 0,5 s ; les quatre roues motrices turbo, rien, et les plus puissantes (V10, V12, W16) de 0,5 à 0,7 s. Le launch control sans antipatinage patine plus fort encore sur les moteurs coupleux (`five_cyl_turbo`, `diesel_four`, `v16_cadillac`).
- **La fiche** reprend à l'essayeur l'essentiel de ce que le patinage coûtait : 2,4 s sur `v8_cross_plane` (9,33 → 6,92 s), 2 s sur `v8_supercharged`, 1,6 s sur `inline_six`, 1,1 s sur `v6_gt`, de 0,05 à 0,45 s sur les tractions à essence. Elle fait mieux que l'antipatinage sport sur six préréglages (`city_14`, `v6_gt`, `v8_cross_plane`, `retro_coupe`, `carb_twin_cam`, `v16_cadillac`, jusqu'à 0,25 s), moins bien sur ceux qui patinent encore (jusqu'à 0,5 s sur `inline_six` et `v8_supercharged`) : un pied qui sent avec 0,2 s de retard ne tient pas le sommet aussi près qu'un calculateur, mais il choisit son régime de départ, ce que le robot ne fait pas. Les quatre roues motrices turbo, le diesel, l'électrique et la deux-temps gardent leur chiffre, et `w12_continental` aussi : ses roues restent sous le sommet (κ = 0,106).
- **Avec l'antipatinage**, le sport n'est jamais plus lent que pied au plancher sans (jusqu'à 2,4 s de mieux), ni que le complet, à 0,02 s près (`v6_gt`) ; là où les roues ne patinent pas (les quatre roues motrices turbo, l'électrique), rien ne change. Le meilleur départ (sport et launch) retrouve l'ancien chiffre à 0,4 s près (`flat_six`), et le bat sur 15 préréglages sur 23.
- **Face aux voitures publiées**, le modèle reste lent sur les plus puissantes (V12, W16, V8 à compresseur : une seule adhérence, des pneus de série, pas de répartition du couple entre les essieux), et sur l'électrique, dont le moteur ne patine pas et que l'antipatinage ne change donc pas : son écart vient de sa courbe de couple, pas du pneu. Rien n'a été réglé pour s'en approcher.

### Le démarrage à froid

**Ce qui calait.** Après un départ à froid, le modèle posé au ralenti (construit moteur tournant : c'est ainsi que partent les trajets, les tests, et le site quand il change de mode) calait en moins d'une seconde : tous les moteurs à injection indirecte par −20 °C, la Cadillac dès 0 °C. Le démarreur, lui, tenait déjà le ralenti par −30 °C pour tous les préréglages. Ni les frottements de l'huile froide, ni l'avance, ni l'autorité du ralenti n'en étaient la cause : le collecteur du moteur posé partait vide, et le film de l'injection indirecte (x-τ d'Aquino, voir « L'injection ») se posait à l'équilibre de ce débit nul. La régulation de ralenti ouvrait, la paroi froide buvait la moitié de l'essence le temps de se mouiller (richesse 0,49 à 0,52, 10 à 17 % du couple), et les frottements l'emportaient. Au démarreur, au contraire, le film se pose quand le moteur prend (`catching_`), à son vrai débit.

**La correction.** Moteur froid, le modèle posé au ralenti part avec ce qu'un ralenti établi a dans son collecteur : l'air qui équilibre ses frottements, huile froide comprise, `frottements / couple indiqué` au ralenti, le même équilibre que la régulation de ralenti. Le film et le carburateur se posent alors sur un vrai débit. Parti chaud, le collecteur part vide, comme à l'origine, au bit près : un creux d'une centaine de tr/min au premier dixième, sans conséquence. Rien n'a été retouché à la physique du froid : frottements, enrichissement, film et ralenti accéléré sont ceux de la thermique et de l'injection.

| Posé au ralenti, puis 10 s au point mort et 5 s pied à 30 % | Avant | Après |
|---|---|---|
| Moteurs à injection indirecte, −20 °C | calent en 0,34 à 0,72 s | ralenti jamais sous 970 tr/min |
| Cadillac, 0 °C ; −30 °C | cale en 1,3 s ; cale en 0,4 s | 1009 tr/min ; 1121 tr/min au plus bas |
| Tous les préréglages, −30 °C, posés ou au démarreur | Cadillac et deux-temps calaient posés | aucun ne cale |

Le test (`cold_start_test.cpp`) démarre chaque préréglage par −20 °C, au démarreur et posé, tient 10 s de ralenti au-dessus de 95 % du ralenti chaud, puis part sans caler ; un second, par −30 °C, posé. Contre-épreuve : sans la correction, le premier échoue sur les 19 préréglages posés à injection indirecte, et le second sur 20 ; sans l'enrichissement du moteur qui prend (`catching_`), le premier échoue au démarreur sur le diesel, la Cadillac, le W12 et le W16. L'empreinte `blips_cold_carb` (carburateurs par −10 °C, posé) change, seule de son espèce.

### La reprise (`--scenario tipin`)

Ce trajet de mesure passait les rapports sur le régime du moteur, quand l'embrayage patinait encore au départ : la deuxième entrait vers 3 km/h, la troisième vers 2,5 km/h, et avec `--advance 6` le moteur calait (« Tip-in from 0 rpm »). Il lit maintenant le régime qu'imposerait le rapport suivant à la vitesse de la voiture, comme au compteur : ni le moteur, qui tourne plus vite que la voiture ne roule tant que l'embrayage patine, ni les roues, qui peuvent patiner. `hot_hatch_na` depuis 1500 tr/min : 145,1 Nm la première seconde avec `--advance 6`, 142,5 sans (143,7 avant, la deuxième broutant à 870 tr/min) ; le creux du carburateur unique reste à 23 % (81,4 Nm). Un test de `ctest` (`tipin_advance`) le tient.

### Les empreintes

Le pneu change toutes les empreintes où la voiture roule : chacune est expliquée dans `CMakeLists.txt`, avec l'écart mesuré (vitesse en fin de trajet, glissement maximal). Celles où elle reste à l'arrêt ne bougent pas : régime tenu, coups de gaz au point mort, démarrages, ralentis. `blips_cold_carb` change par la correction du départ à froid.

### Ce qui manque

- Une seule adhérence, sèche ; pas de pluie, de neige, ni de pneu de sport. Les deux roues d'un essieu, et les quatre d'une transmission intégrale, sont une seule roue : ni différentiel, ni répartition du couple.
- Les coefficients sont cités de mémoire ; le glissement franc publié va de 0,65 à 0,88 du sommet selon la source, et le 0 à 100 sans antipatinage des propulsions en dépend directement.
- Le pilote automatique ne lève pas le pied quand les roues patinent : sans antipatinage, il garde le pied au plancher. L'essayeur de la fiche ne dose que la pédale : l'embrayage reste au robot, les rapports passent au régime de pleine charge. Sa bande passante, son retard et ses quatre pédales de départ sont supposés ; la fiche étant le meilleur de neuf départs, elle garde un peu du hasard d'un départ, quelques centièmes à quelques dixièmes selon la pédale. Elle coûte aussi plus cher à calculer : dans le WASM (mesuré sous Node), 0,14 s pour `v8_cross_plane` et 0,22 s pour `city_14`, qui patinent, contre 0,05 à 0,07 s auparavant (deux départs, la vitesse de pointe refaisant le 0 à 100) ; une voiture qui ne patine pas coûte moitié moins qu'avant. Le site la recalcule pendant qu'on glisse un curseur.
- **Des tests ont changé de conditions, pas de seuils.** Trois vérifiaient en conduite une affirmation qui supposait l'adhérence illimitée : le sport n'est jamais plus lent que l'éco et l'éco monte avant le normal (`shift_modes_test`), le film d'un carburateur soufflé (`jonctions_test`). Sans antipatinage, le pneu à sommet les contredit pour une raison physique (la voiture patine) ; ils se mesurent maintenant antipatinage sport, à seuils inchangés. Trois autres, sur la fiche (la masse décide du 0 à 100, `powertrain_test` ; le protoxyde fait gagner 0,5 s, `charge_air_test` ; la caisse rétro légère bat la compacte, `retro_body_test`), étaient passés à l'antipatinage sport avec le pneu à sommet : l'essayeur les fait passer de nouveau tels qu'ils étaient écrits. Deux tests de la boîte ont aussi changé de mesure : l'oscillation de la chaîne se lit au sommet de son spectre, et non plus au compte des passages à zéro, que la dérive lente faussait une fois l'oscillation amortie (3,45 Hz en deuxième pour 3,56 attendus, 1,66 avec l'ancien compte) ; le rapport « sous charge » attend que l'embrayage soit serré. À vérifier par la relecture.
- L'écoute reste à faire : le crissement des pneus suit le glissement en m/s, désormais plus fort et plus long sur les propulsions sans antipatinage (`--preset v8_cross_plane --scenario launch`, puis `--traction sport`), et le départ à froid posé (`--preset city_14 --start cold --ambient -20 --scenario lap --seconds 30`).
