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

**Ce qui n'est pas modélisé :** le détail des bouffées dans la turbine (seule leur énergie moyenne compte), la géométrie variable (un diesel y gagnerait beaucoup : le sien réagit comme un turbo fixe), le biturbo séquentiel, l'anti-lag. Le gros turbo pompe légèrement à très bas régime sous pleine charge : c'est un vrai défaut de turbo surdimensionné, mais le niveau n'a pas été comparé à une mesure. Sur un V, le modèle sonore et le gaz voient un turbo équivalent, même si les vues en dessinent deux.

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

Le préréglage `retro_coupe` est un 6 cylindres en ligne de 2,5 L, 84 mm d'alésage, 6400 tr/min, à arbre unique. Rien n'y est réglé pour coller à un chiffre publié, et le modèle donne moins de puissance que la référence : 149 ch et 209 Nm, pour 171 ch et 222 Nm (Auto-Data, lu).

| | Coupé, préréglage | Compacte, même moteur, réglages de la compacte | Compacte, même moteur, même masse et mêmes jantes |
|---|---:|---:|---:|
| Cx, surface | 0,40, 1,85 m² | non saisis | non saisis |
| CdA | 0,74 m² | 0,70 m² | 0,70 m² |
| Masse, jantes, transmission | 1150 kg, 14", propulsion | 1350 kg, 18", traction | 1150 kg, 14", propulsion |
| Rapports | 5 | 6 | 6 |
| Vitesse maximale | 213 km/h | 218,5 km/h | 217 km/h |
| 0 à 100 km/h | 8,45 s | 9,53 s | 8,45 s |
| WLTC | 9,47 L/100 km | 9,49 L/100 km | 9,35 L/100 km |
| 120 km/h | 9,34 L/100 km | 8,95 L/100 km | 9,01 L/100 km |
| Habitacle, ralenti, plein gaz | 46,2, 76,0 dB(A) | 42,0, 70,8 dB(A) | avec la même isolation : 46,7, 76,0 dB(A) |
| Dehors, à 5 m, plein gaz | 80,4 dB(A) | 80,6 dB(A) | 80,6 dB(A) |

Mesuré par `measurePerformance`, `--fuel 1` et `soundSheet`. Les niveaux restent des estimations.

Face aux chiffres publiés : 8,3 s et 217 km/h en manuelle (E30 325i, vus cités, Cx donné à 0,38), 10 s et 206 km/h en automatique à 4 rapports (Auto-Data, lu, Cx donné à 0,35) ; 8 s et 210 km/h pour la Capri 2.8i (vus cités). Consommation : 12,7 L/100 km en ville et 6,8 sur route pour cette automatique, dans une norme d'époque qui n'est pas le WLTC.

**Ce que la caisse change peu :** le CdA de la compacte, à 0,70 m², est déjà haut pour une voiture récente, et une petite surface frontale rattrape un mauvais Cx. L'écart de traînée n'est donc que de 6 %. Cela vaut 4 km/h en pointe, et 3,7 % à 120 km/h ; en WLTC, à basse vitesse, l'écart se confond avec celui des rapports. Dans l'habitacle, presque tout l'écart vient de l'isolation : à isolation égale, les deux caisses sont à moins de 1 dB.

**Ce qui manque :** la résistance au roulement (0,0095) et l'adhérence sont celles de pneus récents, pour toutes les voitures. Le moteur n'a ni carburateur ni allumage d'époque : c'est le même modèle de couple, et sa consommation garde l'écart du tableau « Face aux chiffres publiés ». `camshaft: sohc` n'a d'effet que si ce réglage est modélisé.

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
