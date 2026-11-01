# Contributing to Revforge

Issues and pull requests are welcome, whether they fix a bug or simply say "this doesn't sound like a real engine, and here is why".

## Reporting a sound problem

Sound is judged by ear, so a description is real data. Please include:

- what you hear, in your own words ("a zipper", "a vacuum cleaner" have both led to real fixes);
- the engine and listening position, using the site's **Copy link** button, or the `revforge-render` command;
- if you can, a recording of the real thing to compare with. Please link to it rather than attaching it: no audio goes into this repository.

## Setup

See the [README](README.md#try-it) for the build. You need CMake, Ninja, a C++20 compiler and Emscripten (which provides Node).

## Before opening a pull request

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
scripts/build_wasm.sh
node tools/check_wasm.mjs build-wasm/revforge.wasm build/revforge-render
cd web && npm run check && npm run build && npx playwright test
```

`ctest` does not rebuild, and Playwright tests `web/dist`: build first, or you will test the previous version.

## Rules that matter

[`CLAUDE.md`](CLAUDE.md) is the detailed reference: architecture, invariants, and the lists of what must stay in sync, since nothing generates those links and a mistake can pass the compiler. The essentials:

- **Changing the sound is a decision.** The `golden_*` tests compare a hash of the render. If your change is meant to alter the sound, update the hashes with the new values printed by `revforge-render --out - <arguments>` and say so in the pull request. If it isn't, a failing fingerprint is a regression.
- **Measurements do not validate a sound.** Say what you listened to, and if you haven't listened, say that too.
- **Counter-test your tests.** A sound test must fail when you remove what it protects. Check it and mention how.
- **No samples.** The shipped sound stays entirely simulated. Recorded or generated audio may be used as a reference outside the repository, never shipped. Check its license if you publish numbers measured from it.
- **Real time.** `EngineSound::render` must not allocate or lock, and its result must not depend on the block size: state evolves per sample.
- **Determinism.** Native and WebAssembly must match bit for bit. In sound code, go through `revforge::km`. Adding a call to `random()` shifts all the sound that follows, so a new source gets its own generator.
- **WebAssembly.** No exceptions, no RTTI, 4 MB of fixed memory, no imports: the worklet instantiates the module with `{}`.
- **Positional data.** The arrays in `web/src/lib/config.ts` and the share link format are positional. Reordering them changes the engine requested, or breaks links people already shared.
- **Site text** goes in both language dictionaries, English and French.

## Language

Code comments, test names and commit messages are in French, and existing code should stay consistent. Don't let that stop you: if your French isn't up to it, write in English and a maintainer will adapt it. Issues and pull requests can be in either language.

## License

By contributing, you agree that your contribution is released under the [MIT License](LICENSE).
