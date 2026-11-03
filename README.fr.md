# Revforge

[English](README.md)

**Construis un moteur, monte-le dans une voiture, puis écoute-le d'où tu veux : une simulation physique en temps réel d'une voiture et de son son, avec un cœur C++20 sans dépendance, joué dans le navigateur grâce à WebAssembly.**

**[Essayer dans le navigateur](https://wasabules.github.io/revforge/?lang=fr)** (son activé, au casque si possible).

Il n'y a aucun échantillon sonore. Tout ce qu'on entend est calculé, échantillon par échantillon, par le modèle qui fait avancer la voiture : le couple vient du moteur que tu as construit, le régime de l'embrayage et de la boîte que tu as choisis, et le son de ce que fait le gaz en sortant.

![La page de Revforge : compte-tours, voiture en coupe, vues du moteur, de l'embrayage, de la boîte et du gaz](docs/images/revforge.png)

## Ce qu'on peut faire

- **Construire le moteur.** Cylindres (de 2 à 12), architecture, cylindrée, alésage et course (le curseur ajuste la cylindrée), régime maximal, essence ou diesel, un turbo avec sa taille et sa pression, et un filtre à air (papier d'origine, panneau sport, cône à l'air libre, cornets). Puissance, couple, 0 à 100 km/h et vitesse de pointe découlent de ces réglages : rien n'est saisi à la main. Le temps de réponse du turbo vient d'un vrai bilan de puissance sur son arbre, et se sent dans la voiture.
- **Choisir l'échappement.** Confort, origine, sans résonateur, sport, sans silencieux ou ligne directe, avec une ou deux sorties, et un pont en H ou en X sur un moteur en V. Chaque ligne est calculée depuis sa géométrie.
- **Choisir la boîte, indépendamment du moteur.** Synchros, crabots, séquentielle, double embrayage ou convertisseur ; nombre de rapports, rapports issus de vrais nombres de dents, couple final, denture hélicoïdale ou droite, volant simple ou bi-masse. Une boîte mal adaptée a de vraies conséquences : on peut caler, manquer de vitesse ou la casser.
- **Conduire.** Démarreur, calage, embrayage qu'on fait patiner, coup de gaz au rétrogradage, grattement si on passe sans débrayer, marche arrière, rupteur, coupure d'injection, détonations, décharge du turbo. Les automatiques ont des modes Éco, Normal, Sport, Piste et palettes, avec des points de passage issus des courbes du moteur.
- **Partir en balade.** Neuf trajets, de la ville au col de montagne, du tour de circuit au départ arrêté ou au remorquage : la pente, les virages et la remorque se sentent sur la voiture.
- **Lire les jauges.** Un large tableau de bord : compte-tours, pression d'admission, température et contre-pression des gaz, puissance et couple sur leurs courbes, consommation estimée d'essence et d'huile. Chaque aiguille lit une valeur simulée. Les réglages sont dans des panneaux latéraux : on entend chaque changement en le faisant.
- **Se placer où l'on veut.** Glisse-toi autour de la voiture sur un plan vu de dessus, ou saute devant, derrière, sur le côté, l'oreille dans le pot, dans l'habitacle ou sous le capot. Les sources sont à leur vraie place, en vraie stéréo, avec un niveau estimé en dB(A).
- **S'asseoir à bord.** L'habitacle dépend de l'isolation (de la voiture de course dépouillée à la berline de luxe), de l'emplacement du moteur (avant, central ou arrière) et des vitres ouvertes ou fermées.
- **Voir à l'intérieur, en 2D ou en 3D.** Un vilebrequin aux bons manetons pour chaque architecture, pistons, soupapes, arbres à cames, chaîne de distribution, bougies d'allumage ou de préchauffage. Un embrayage avec sa vraie butée, qui prend de la vitesse au contact des doigts du diaphragme. Une boîte reconstruite selon tes réglages, avec les vrais nombres de dents, toujours en prise.
- **Une page qui chauffe.** La couleur d'accent est une vraie température de la simulation : métal froid au repos, incandescent à pleine charge.
- **Voir le gaz et les données.** Un gaz calculé par un vrai solveur de fluide, de la boîte à air au panache de sortie, en passant par le turbo et l'échangeur, et des graphiques à la demande : courbes dans le temps, spectrogramme, réponse de la ligne.
- **Ralentir, ou pas.** Les animations tournent à 1/12, à 1/4 ou en temps réel. En temps réel, comme sur un vrai moteur, l'œil ne suit plus les pièces au-delà de 2 000 tr/min environ.

Le site existe en anglais et en français.

## Essayer

Il faut [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), qui fournit aussi Node, ainsi que CMake et Ninja.

```bash
scripts/build_wasm.sh    # compile le moteur vers web/public/revforge.wasm
cd web
npm ci
npm run dev              # ou : npm run build && npm run preview
```

| Commande | Clavier | Manette |
|---|---|---|
| Accélérateur | Espace ou ↑ (Maj : à mi-course) | gâchette droite, progressive |
| Frein | B ou ↓ | gâchette gauche, progressive |
| Embrayage (manuelle) | C | bouton A |
| Rapports (manuelle ou palettes) | ← et → ; ← depuis le point mort pour la marche arrière | boutons d'épaule |
| Redémarrer après un calage | D | |

## Écouter sans navigateur

La même simulation rend un WAV en ligne de commande :

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/revforge-render --list
./build/revforge-render --preset v8_cross_plane --scenario blips --out v8.wav
./build/revforge-render --preset v8_cross_plane --scenario start --out demarrage.wav

# Un V8 en ligne directe, en stéréo, derrière et à gauche, avec son niveau estimé
./build/revforge-render --preset v8_cross_plane --exhaust straight --routing dual \
    --at -4,1.5,1.7 --stereo 1 --db 1 --scenario launch --out derriere.wav

# Ton propre moteur, depuis l'habitacle d'une voiture dépouillée
./build/revforge-render --cylinders 5 --cc 2500 --bore 82 --redline 7000 --aspiration turbo --boost 1.5 \
    --exhaust sport --pops strong --body compact --mass 1400 --rim 18 --drive all \
    --gearbox dog --teeth spur --listener cabin --insulation stripped --scenario launch --out cinq.wav
```

Autres options utiles : `--turbo small|medium|large`, `--turbo-pipe MM`, `--filter stock|panel|cone|stacks`, `--shift-mode`, `--drive-scenario`, et `--fuel 1` avec `--out none` pour la consommation. `--help` donne la liste complète.

## Comment le son est fabriqué

Le schéma de la chaîne est dans le [README anglais](README.md#how-the-sound-is-made).

- **Le modèle de conduite** intègre couple, inertie, embrayage, boîte et pneus à pas fixe de 1 ms, et pilote le son avec le régime et la charge *réels* du moteur.
- **La ligne d'échappement est calculée depuis sa géométrie** par matrices de transfert acoustiques (tubes, catalyseur, résonateur, silencieux, sortie libre), puis appliquée par convolution partitionnée. Tout le gaz la traverse : les harmoniques défilent devant un timbre fixe, au lieu d'entraîner les résonances avec elles.
- **Chaque source est en pascals à 1 m**, puis voyage jusqu'à l'auditeur : 1/r et retard (donc Doppler), directivité de la sortie selon Levine et Schwinger, absorption de l'air selon ISO 9613-1, réflexion au sol, masquage par la caisse selon Maekawa, modèle de tête sphérique selon Brown et Duda. Aucun gain par position.
- **Le bruit de boîte vient de ses causes :** erreur de transmission à la fréquence d'engrènement, qui suit les arbres et non le moteur, et claquement au ralenti calculé d'après les roues folles dans leur jeu, qui s'arrête quand on débraye.
- **Un vrai sonomètre** (pondération A de l'IEC 61672, constante « fast ») donne le chiffre en dB(A). Le niveau est ensuite comprimé pour tes haut-parleurs, qui ne peuvent pas jouer de 40 à 130 dB.

La physique, la calibration et toutes les cibles sont dans [docs/modele.md](docs/modele.md) ([in English](docs/model.md)).

## Quelle confiance lui accorder

C'est un simulateur réglé à l'oreille et à la mesure, pas un instrument de mesure.

- **Les dB(A) sont des estimations**, comparées à des valeurs d'essai publiées. Aucun micro ne les a contrôlés.
- **De bonnes mesures n'ont jamais suffi à faire un bon son.** Les premiers démarreurs avaient le bon rythme et le bon spectre moyen, et ne ressemblaient en rien à un démarreur. Tout changement de son passe donc par l'écoute, et `tools/perceptual_compare.py` est un guide, pas un verdict.
- **Les enregistrements de référence ne sont pas dans ce dépôt.** De vrais enregistrements (par exemple de [La Sonothèque](https://lasonotheque.org/)) et quelques sons générés n'ont servi qu'à comparer. Rien du son livré n'en provient.

Limites connues :

- Distinguer l'avant de l'arrière au casque reste difficile : le modèle stéréo n'a pas de HRTF.
- Le ralenti du 4 cylindres sportif sort de la plage des vrais moteurs au comparateur, et reste trop haché.
- À plein gaz, le limiteur resserre l'écart entre les lignes : environ 6 dB joués pour environ 22 dB estimés entre l'origine et la ligne directe.
- Les vues 2D et 3D simplifient une partie de la mécanique (automatique schématique, double embrayage approché, distribution à double arbre partout) et n'ont pas été vérifiées sur beaucoup de cartes graphiques. Sans carte graphique, la 3D passe en rendu plus grossier et tourne lentement ; le bouton 2D reste fluide.
- La consommation estimée lit environ 15 à 25 % au-dessus des chiffres officiels, et l'huile n'est qu'un ordre de grandeur, à un facteur trois près.
- À cylindrée égale, la course ne change le couple qu'au-delà de 24 m/s de vitesse de piston, et ne change jamais le frottement, l'inertie ni la consommation.
- La voiture ne recule jamais (démarrage en côte avec maintien supposé), toutes les voitures ont la même adhérence, et les trajets sont réglés en durée, pas en distance.
- En automatique, un diesel peut parfois caler à très faible vitesse ; le site propose alors de le relancer.
- Le claquement de boîte n'a aucun vrai enregistrement isolé pour comparaison, et plusieurs paramètres de boîte (module, angle d'hélice, jeu, vitesse d'usure) sont supposés.
- La longueur du tube du turbo change son retard, son gaz et ses vues, mais le sifflement vient toujours du compartiment moteur, même sur un turbo déporté à l'arrière, et les réflexions d'onde dans ce tube ne sont pas dans le son.
- La vue du gaz suit le modèle de conduite sans agir sur lui : ses pressions ne recoupent donc pas exactement le couple affiché.
- Certaines valeurs réglementaires (ONU R51.03 et UE 540/2014) restent à contrôler sur les textes officiels.
- La carrosserie des années 80 garde la résistance au roulement et l'adhérence de pneus récents, et sa surface frontale et sa faible isolation sont supposées.

## Organisation

| Dossier | Contenu |
|---|---|
| `core/` | La simulation : son, acoustique, ligne d'échappement, démarreur, boîte, puissance et modèle de conduite |
| `presets/presets.json` | Les moteurs proposés, source unique pour le C++ et le site |
| `cli/` | Rendu WAV en ligne de commande |
| `wasm/` | Interface C pour WebAssembly |
| `web/` | Le site : AudioWorklet, TypeScript, Svelte 5, vues canvas et three.js, solveur du gaz |
| `tests/`, `web/tests/` | Tests C++ (doctest) et tests du navigateur (Playwright) |
| `tools/` | Analyse, comparaison perceptive, parité natif et WebAssembly |
| `docs/` | [Le modèle et sa calibration](docs/modele.md) |

`CLAUDE.md` est la carte la plus complète du code : architecture, invariants, et ce qui doit rester synchronisé.

## Règles d'ingénierie

- **Temps réel :** `EngineSound::render` n'alloue jamais et ne prend aucun verrou ; un test le vérifie.
- **La taille des blocs ne change rien :** tout état évolue par échantillon.
- **Au bit près partout :** natif et WebAssembly produisent le même son, vérifié par `tools/check_wasm.mjs`.
- **Changer le son est un choix :** CTest compare des empreintes du son, donc tout changement se voit et doit être voulu.
- **Les tests sont contre-éprouvés :** un test de son doit échouer quand on retire ce qu'il protège. Cela a déjà révélé plusieurs tests aveugles.
- **Limites du WebAssembly :** ni exceptions ni RTTI, 4 Mo de mémoire fixe, aucun import.

## Tests

```bash
ctest --test-dir build                                                     # moteur et empreintes du son
source ~/emsdk/emsdk_env.sh                                                # fournit node
node tools/check_wasm.mjs build-wasm/revforge.wasm build/revforge-render   # WebAssembly = natif, au bit près
cd web && npx playwright install chromium firefox                          # une première fois
npm run check && npm run build && npx playwright test                      # site, dans Chromium et Firefox
```

Lis [CONTRIBUTING.md](CONTRIBUTING.md) avant de modifier quoi que ce soit.

## Remerciements

L'approche doit beaucoup à [engine-sim](https://github.com/ange-yaghi/engine-sim) d'AngeTheGreat et au [Sound Design Toolkit](https://github.com/SkAT-VG/SDT). Les travaux publiés derrière le modèle sont cités dans [docs/modele.md](docs/modele.md). Les vrais démarrages mesurés viennent de [La Sonothèque](https://lasonotheque.org/).

## Licence

[MIT](LICENSE). `tests/vendor/doctest.h` : [doctest](https://github.com/doctest/doctest), MIT. Les vues 3D du site utilisent [three.js](https://threejs.org/), MIT, chargé seulement quand une vue 3D s'affiche ; les polices sont Big Shoulders Display et Inter Tight, sous licence SIL Open Font. Le cœur C++ ne dépend de rien.
