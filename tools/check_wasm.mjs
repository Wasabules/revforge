// Le WebAssembly doit produire le même son que le natif, au bit près.
// Usage : node tools/check_wasm.mjs build-wasm/revforge.wasm build/revforge-render
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const [wasmPath, renderPath] = process.argv.slice(2);
const bytes = readFileSync(wasmPath);
const module = new WebAssembly.Module(bytes);
const imports = WebAssembly.Module.imports(module);
if (imports.length) console.log('imports :', imports.map((i) => `${i.module}.${i.name}`).join(', '));

function load() {
  const stubs = {};
  for (const i of imports) (stubs[i.module] ??= {})[i.name] = () => 0;
  const { exports: rf } = new WebAssembly.Instance(module, stubs);
  rf._initialize?.();
  rf.rf_init(44100);
  return rf;
}

const LAYOUT = { inline: 0, v: 1, cross_plane: 2, boxer: 3 };
const EXHAUST = { stock: 0, sport: 1, straight: 2 };

function fnv(state, samples) {
  let hash = state;
  for (const s of samples) {
    // Même conversion que le rendu natif : `s * 32767f` en float, tronqué, borné.
    const scaled = Math.fround(s * Math.fround(32767));
    const v = Math.max(-32768, Math.min(32767, Math.trunc(scaled)));
    hash = Math.imul(hash ^ (v & 0xffff), 16777619) >>> 0;
  }
  return hash;
}

function render(rf, seconds, tick, beforeTick) {
  let hash = 2166136261;
  const total = 44100 * seconds;
  let step = 0;
  for (let done = 0; done < total; done += tick) {
    beforeTick?.(step++);
    const n = Math.min(tick, total - done);
    rf.rf_render(n);
    hash = fnv(hash, new Float32Array(rf.memory.buffer, rf.rf_buffer(), n));
  }
  return hash.toString(16).padStart(8, '0');
}

const native = (...args) => execFileSync(renderPath, ['--out', '-', ...args]).toString().trim();
let failed = 0;
function check(label, wasm, expected) {
  const ok = wasm === expected;
  console.log(`${ok ? 'identique ' : 'DIFFÉRENT '} ${label.padEnd(30)} ${wasm} / ${expected}`);
  if (!ok) failed++;
}

// Régime tenu, 4 cylindres d'origine.
{
  const rf = load();
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0);
  rf.rf_set_mode(4);
  rf.rf_set_manual_rpm(5000);
  check('régime tenu', render(rf, 4, 512), native('--cylinders', '4', '--cc', '1360', '--bore', '75', '--redline', '6200', '--scenario', 'steady', '--rpm', '5000'));
}
// Tour de 60 s, avec modèle de conduite, et moteur chargé.
{
  const rf = load();
  rf.rf_set_config(10, LAYOUT.v, 5204, 85, 8500, EXHAUST.straight, 0, 0, 0, 2, 0);
  rf.rf_set_mode(3);
  check('tour complet, V10', render(rf, 60, 2205), native('--preset', 'v10_open', '--scenario', 'lap'));
}
// Turbo et flutter au lever de pied.
{
  const rf = load();
  rf.rf_set_config(4, LAYOUT.boxer, 2500, 99, 6800, EXHAUST.sport, 1, 0, 0, 1, 1);
  rf.rf_set_mode(3);
  check('tour complet, boxer turbo', render(rf, 60, 2205), native('--preset', 'boxer_four_turbo', '--scenario', 'lap'));
}
// Électrique.
{
  const rf = load();
  rf.rf_set_electric();
  rf.rf_set_mode(3);
  check('tour complet, électrique', render(rf, 60, 2205), native('--preset', 'electric', '--scenario', 'lap'));
}
process.exit(failed ? 1 : 0);
