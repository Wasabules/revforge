# Revforge

Construis un moteur, puis écoute-le : une simulation acoustique en temps réel, en
C++, jouée dans le navigateur grâce à WebAssembly.

Cylindres, architecture, cylindrée, alésage, régime maximal, échappement, turbo,
carburant : chaque réglage agit sur la physique du son, pas sur un filtre posé
après coup. Au lever de pied, on entend la coupure d'injection, les détonations
dans le tube et la décharge du turbo.

## Lancer le site

Il faut [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) et
Node, fourni avec lui.

```bash
scripts/build_wasm.sh         # compile le moteur vers web/public/revforge.wasm
cd web
npm ci
npm run dev                   # ou : npm run build && npm run preview
```

Au clavier : barre d'espace pour l'accélérateur, B pour le frein.

## Écouter sans navigateur

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/revforge-render --list
./build/revforge-render --preset v8_cross_plane --scenario blips --out v8.wav
./build/revforge-render --cylinders 5 --cc 2500 --bore 82 --redline 7000 \
    --exhaust straight --aspiration turbo --pops strong --scenario lap --out cinq.wav
```

## Tests

```bash
ctest --test-dir build                                       # moteur et empreintes du son
node tools/check_wasm.mjs build-wasm/revforge.wasm build/revforge-render   # WebAssembly = natif, au bit près
cd web && npm run check && npm run build && npx playwright test            # site, dans Chromium et Firefox
```

Pour les tests du site : `npx playwright install chromium firefox` une première fois.

## Organisation

| Dossier | Contenu |
|---|---|
| `core/` | le moteur : simulation, configuration, modèle de conduite |
| `presets/presets.json` | les moteurs proposés, source unique pour le C++ et le site |
| `cli/` | rendu WAV en ligne de commande |
| `wasm/` | interface C pour WebAssembly |
| `web/` | le site : AudioWorklet, TypeScript, Svelte |
| `tests/`, `tools/` | tests du moteur, comparaison de rendus, contrôle du WebAssembly |
| `docs/` | [le modèle et sa calibration](docs/modele.md) |

## Principes

- **Aucune allocation ni verrou pendant le rendu** : un test le vérifie.
- **Le résultat ne dépend pas de la taille des blocs** : aucun saut entre deux blocs.
- **Même son partout, au bit près** : calculs déterministes, empreintes dans CTest.
  Changer le son doit être un choix, et se voit dans ces empreintes.

## Licence

[MIT](LICENSE). `tests/vendor/doctest.h` : [doctest](https://github.com/doctest/doctest), MIT.
