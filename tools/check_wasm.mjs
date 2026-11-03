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

const LAYOUT = { inline: 0, v: 1, cross_plane: 2, boxer: 3, rotary: 4, vr: 5, w: 6 };
const EXHAUST = { stock: 0, sport: 1, straight: 2, no_muffler: 3, no_resonator: 4, quiet: 5, expansion: 6 };
const CAMSHAFT = { dohc: 0, sohc: 1, ohv: 2 };
const FUEL_SYSTEM = { injection: 0, single: 1, multi: 2, direct: 3 };
const BODY = { city: 0, compact: 1, coupe: 2, supercar: 3, van: 4, retro: 5 };
const DRIVE = { front: 0, rear: 1, all: 2 };
const PLACEMENT = { front: 0, mid: 1, rear: 2 };
const INSULATION = { stripped: 0, light: 1, standard: 2, comfort: 3, luxury: 4 };
const ROUTING = { single: 0, dual: 1, h: 2, x: 3 };
// Même ordre que `Listener` en C++.
// Même ordre que `TurboSize` en C++.
const TURBO = { small: 0, medium: 1, large: 2 };
const LISTENER = { outside: 0, cabin: 1, bay: 2, front: 3, side: 4, far: 5, pipe: 6 };
const BOX = { synchro: 0, dog: 1, sequential: 2, dual_clutch: 3, converter: 4 };
const FAMILY = { matched: 0, short: 1, close: 2, long: 3, custom: 4 };
const TEETH = { helical: 0, spur: 1 };
// Même ordre que `ShiftMode` et `DriveScenario` en C++.
const SHIFT = { normal: 0, eco: 1, sport: 2, track: 3, paddles: 4 };
const SCENARIO = { city: 0, stop_go: 1, country: 2, motorway: 3, mountain: 4, drag: 5, track: 6, hill_start: 7, towing: 8 };

// ── Les réglages du socle moteur : même ordre que les énumérations C++ et les tableaux de config.ts ──
const START = { warm: 0, cold: 1 };
const GRADE = { ron95: 0, ron98: 1, race102: 2, e85: 3, methanol: 4 };
const LIMITER = { stock: 0, soft: 1, ignition_cut: 2 };
const TRACTION = { off: 0, sport: 1, full: 2 };
const DIESEL_INJECTION = { mechanical: 0, common_rail: 1 };
const VVT = { fixed: 0, phased: 1, two_step: 2, phased_two_step: 3 };
const SPRINGS = { stock: 0, stiff: 1 };
const TURBO_LAYOUT = { single: 0, twin_parallel: 1, twin_sequential: 2, twincharger: 3 };
const BOOST_CONTROL = { spring: 0, electronic: 1, by_gear: 2 };
const INTERCOOLER = { air: 0, water: 1, none: 2 };
const CYCLE = { four_stroke: 0, two_stroke: 1 };
/** Les arguments de `rf_set_tuning`, dans son ordre, à leur défaut : le moteur d'aujourd'hui. */
const TUNING = {
  ambientC: 20, altitudeM: 0, startTemp: 0, compressionRatio: 0, fuelGrade: 0, ignitionOffsetDeg: 0, fullLoadLambda: 0, revLimiter: 0,
  launchRpm: 0, flatShift: 0, tractionControl: 0, dieselInjection: 0, egr: 0, particulateFilter: 0, camDurationDeg: 0, camLiftMm: 0,
  lobeSeparationDeg: 0, valveTiming: 0, valveSprings: 0, turboLayout: 0, variableGeometry: 0, boostControl: 0, intercooler: 0,
  waterInjection: 0, nitrousKw: 0, cycle: 0,
};
// Les noms et l'ordre de `TUNING` sont ceux de la signature de `rf_set_tuning` : un écart décalerait tous les réglages qui suivent.
{
  const source = readFileSync(new URL('../wasm/exports.cpp', import.meta.url), 'utf-8');
  const signature = source.match(/void rf_set_tuning\(([^)]*)\)/)?.[1] ?? '';
  const names = signature.split(',').map((param) => param.trim().split(/\s+/).pop());
  if (names.join(',') !== Object.keys(TUNING).join(',')) {
    console.log(`DIFFÉRENT  rf_set_tuning : ${names.join(',')}\n           check_wasm  : ${Object.keys(TUNING).join(',')}`);
    process.exit(1);
  }
}
/**
 * Pose les réglages du socle : seuls ceux qui changent sont à écrire, `tune(rf, { altitudeM: 2000 })`. À appeler AVANT
 * `rf_set_config` (ou `rf_set_electric`), qui les applique : après, ils attendraient le prochain moteur.
 */
function tune(rf, changes) {
  for (const key of Object.keys(changes)) if (!(key in TUNING)) throw new Error(`réglage inconnu : ${key}`);
  rf.rf_set_tuning(...Object.keys(TUNING).map((key) => changes[key] ?? TUNING[key]));
}

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

// `fullLast` : le natif avance son modèle d'une tranche entière, même sur la dernière, plus courte. Le son d'un turbo suit
// le modèle de si près que cette dernière tranche se compare à condition d'avancer pareil.
function render(rf, seconds, tick, beforeTick, fullLast = true) {
  let hash = 2166136261;
  const total = 44100 * seconds;
  let step = 0;
  for (let done = 0; done < total; done += tick) {
    beforeTick?.(step++);
    const n = Math.min(tick, total - done);
    rf.rf_render(fullLast ? tick : n);
    hash = fnv(hash, new Float32Array(rf.memory.buffer, rf.rf_buffer(), n));
  }
  return hash.toString(16).padStart(8, '0');
}

/** Les deux oreilles, entrelacées comme dans le fichier du rendu natif. */
function renderStereo(rf, seconds, tick, beforeTick) {
  let hash = 2166136261;
  const total = 44100 * seconds;
  let step = 0;
  for (let done = 0; done < total; done += tick) {
    beforeTick?.(step++);
    const n = Math.min(tick, total - done);
    rf.rf_render_stereo(n);
    const left = new Float32Array(rf.memory.buffer, rf.rf_buffer(), n);
    const right = new Float32Array(rf.memory.buffer, rf.rf_buffer_right(), n);
    const both = new Float32Array(n * 2);
    for (let i = 0; i < n; i++) {
      both[i * 2] = left[i];
      both[i * 2 + 1] = right[i];
    }
    hash = fnv(hash, both);
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
  rf.rf_set_car(BODY.city, 1100, 15, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(4);
  rf.rf_set_manual_rpm(5000);
  check('régime tenu', render(rf, 4, 512), native('--cylinders', '4', '--cc', '1360', '--bore', '75', '--redline', '6200', '--scenario', 'steady', '--rpm', '5000'));
}
// La course : `--stroke` ne fait que calculer la cylindrée. 86 mm d'alésage, 110 de course : au-delà de 24 m/s en haut du régime.
{
  const rf = load();
  rf.rf_set_car(BODY.city, 1100, 15, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, Math.round((Math.PI / 4) * 8.6 * 8.6 * 11 * 4), 86, 7200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('tour, longue course', render(rf, 30, 128), native('--cylinders', '4', '--bore', '86', '--stroke', '110', '--redline', '7200', '--scenario', 'lap', '--seconds', '30'));
}
// Tour de 60 s, avec modèle de conduite, et moteur chargé.
{
  const rf = load();
  rf.rf_set_car(BODY.supercar, 1500, 20, DRIVE.all, PLACEMENT.mid, INSULATION.stripped);
  // Comme le préréglage : double embrayage.
  rf.rf_set_gearbox(BOX.dual_clutch, 0, 0, 0, 0, 0, 0, 0, 0);
  rf.rf_set_config(10, LAYOUT.v, 5204, 85, 8500, EXHAUST.straight, 0, 0, 0, 2, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('tour complet, V10', render(rf, 60, 128), native('--preset', 'v10_open', '--scenario', 'lap'));
}
// Rotatif : deux rotors, une explosion par rotor et par tour d'arbre.
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1300, 18, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(2, LAYOUT.rotary, 1308, 84, 9000, EXHAUST.stock, 0, 0, 0, 1, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('tour complet, birotor', render(rf, 60, 128), native('--preset', 'twin_rotor_13', '--scenario', 'lap'));
}
// Rotatif turbo, trois rotors, pour la chaîne du turbo calée sur deux fois la cylindrée.
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1500, 18, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(3, LAYOUT.rotary, 1962, 84, 8000, EXHAUST.sport, 1, 0, 0, 0, 0, 0.8, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(4);
  rf.rf_set_manual_rpm(5000);
  check('régime tenu, trois rotors turbo', render(rf, 4, 512), native('--layout', 'rotary', '--cylinders', '3', '--cc', '1962', '--redline', '8000', '--exhaust', 'sport', '--aspiration', 'turbo', '--boost', '0.8', '--body', 'coupe', '--mass', '1500', '--rim', '18', '--drive', 'rear', '--placement', 'front', '--scenario', 'steady', '--rpm', '5000'));
}
// Turbo et flutter au lever de pied.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1450, 18, DRIVE.all, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.boxer, 2500, 99, 6800, EXHAUST.sport, 1, 0, 0, 1, 1, 1.2, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('tour complet, boxer turbo', render(rf, 60, 128), native('--preset', 'boxer_four_turbo', '--scenario', 'lap'));
}
// Démarrage : le démarreur, puis le moteur qui prend.
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1700, 19, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_config(8, LAYOUT.cross_plane, 5000, 96, 6000, EXHAUST.sport, 0, 0, 0, 1, 0, 1, ROUTING.dual, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(1);
  rf.rf_crank();
  check('démarrage, V8', render(rf, 5, 128), native('--preset', 'v8_cross_plane', '--scenario', 'start'));
}
// Électrique.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1800, 19, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_electric();
  rf.rf_set_mode(3);
  check('tour complet, électrique', render(rf, 60, 128), native('--preset', 'electric', '--scenario', 'lap'));
}
// La boîte : les préréglages comparés, et de quoi repartir d'une voiture neuve au ralenti.
function citadine() {
  const rf = load();
  rf.rf_set_car(BODY.city, 1100, 15, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  return rf;
}
function sixCylinders(kind, family, teeth, limit) {
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1500, 18, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_gearbox(kind, 0, family, 0, teeth, 0, 0, limit, 0);
  rf.rf_set_config(6, LAYOUT.inline, 3246, 87, 8000, EXHAUST.sport, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  return rf;
}
const FRESH = 3; // passer par la balade reconstruit le modèle : une voiture à l'arrêt, moteur au ralenti.
const TICK_S = 128 / 44100;

// Séquentielle à denture droite : crabots, allumage coupé à la montée, sifflement.
{
  const rf = sixCylinders(BOX.sequential, FAMILY.close, TEETH.spur, 600);
  rf.rf_set_mode(FRESH);
  check('tour, séquentielle droite', render(rf, 60, 128), native('--preset', 'inline_six', '--scenario', 'lap', '--gearbox', 'sequential', '--family', 'close', '--teeth', 'spur', '--limit', '600'));
}
// Automatique : le convertisseur et ses embrayages.
{
  const rf = sixCylinders(BOX.converter, FAMILY.matched, TEETH.helical, 0);
  rf.rf_set_mode(FRESH);
  check('tour, convertisseur', render(rf, 60, 128), native('--preset', 'inline_six', '--scenario', 'lap', '--gearbox', 'converter'));
}
// Le claquement au point mort : embrayé, débrayé, embrayé.
{
  const rf = citadine();
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(1);
  const seconds = (step) => (step * 128) / 44100;
  const hash = render(rf, 12, 128, (step) => rf.rf_set_controls(0, 0, seconds(step) > 4 && seconds(step) < 8 ? 1 : 0));
  check('claquement au point mort', hash, native('--preset', 'city_14', '--scenario', 'rattle'));
}
// Les passages à synchros, à la montée puis à la descente.
{
  const rf = citadine();
  rf.rf_set_transmission(1);
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(2);
  rf.rf_shift(1);
  const redline = rf.rf_redline();
  const top = Math.min(rf.rf_gears(), 4);
  const hash = render(rf, 26, 128, (step) => {
    const t = step * TICK_S;
    let pedal = t > 0.3 ? 0.625 : 0;
    if (t < 15) {
      if (rf.rf_gear() < top && !rf.rf_shifting() && rf.rf_rpm() > redline * 0.62) rf.rf_shift(1);
    } else {
      pedal = 0;
      if (rf.rf_gear() > 1 && !rf.rf_shifting() && rf.rf_rpm() < redline * 0.42) rf.rf_shift(-1);
    }
    rf.rf_set_controls(pedal, t > 15 ? 0.125 : 0, 0);
  });
  check('passages à synchros', hash, native('--preset', 'city_14', '--scenario', 'shifts'));
}
// Marche arrière.
{
  const rf = citadine();
  rf.rf_set_transmission(1);
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(2);
  const hash = render(rf, 12, 128, (step) => {
    const t = step * TICK_S;
    if (t > 0.5 && rf.rf_gear() === 0) rf.rf_shift(-1);
    rf.rf_set_controls(t > 1 ? 0.125 : 0, 0, 0);
  });
  check('marche arrière', hash, native('--preset', 'city_14', '--scenario', 'reverse'));
}
// La position d'écoute, en stéréo : chaque endroit a ses retards, ses filtres et ses modes propres.
function fourCylinderCoupe(insulation = INSULATION.light, placement = PLACEMENT.front, exhaust = EXHAUST.sport, routing = ROUTING.single) {
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1500, 18, DRIVE.rear, placement, insulation);
  rf.rf_set_config(6, LAYOUT.v, 3000, 86, 6500, exhaust, 0, 0, 0, 0, 0, 1, routing, TURBO.medium, 0, 0, 400, 0, 0);
  return rf;
}
const coupeArgs = ['--cylinders', '6', '--layout', 'v', '--cc', '3000', '--bore', '86', '--redline', '6500', '--body', 'coupe', '--mass', '1500', '--rim', '18', '--drive', 'rear', '--stereo', '1', '--scenario', 'lap', '--seconds', '20'];
for (const [name, listener] of Object.entries(LISTENER)) {
  const rf = fourCylinderCoupe();
  rf.rf_set_listener(listener);
  rf.rf_set_mode(3);
  check(`stéréo, ${name}`, renderStereo(rf, 20, 128), native(...coupeArgs, '--exhaust', 'sport', '--placement', 'front', '--insulation', 'light', '--routing', 'single', '--listener', name));
}
// Chaque ligne, deux rangées, un moteur ailleurs, une autre caisse : tout ce qui se calcule à l'application d'une voiture.
{
  const rf = fourCylinderCoupe(INSULATION.luxury, PLACEMENT.rear, EXHAUST.no_muffler, ROUTING.x);
  rf.rf_set_listener(LISTENER.cabin);
  rf.rf_set_mode(3);
  check('moteur arrière, pont en X', renderStereo(rf, 20, 128), native(...coupeArgs, '--exhaust', 'no_muffler', '--placement', 'rear', '--insulation', 'luxury', '--routing', 'x', '--listener', 'cabin'));
}
{
  const rf = fourCylinderCoupe(INSULATION.stripped, PLACEMENT.mid, EXHAUST.quiet, ROUTING.h);
  rf.rf_set_listener(LISTENER.side);
  rf.rf_set_mode(3);
  check('moteur central, pont en H', renderStereo(rf, 20, 128), native(...coupeArgs, '--exhaust', 'quiet', '--placement', 'mid', '--insulation', 'stripped', '--routing', 'h', '--listener', 'side'));
}
// La tête qui se déplace à travers la voiture : mêmes pas de chaque côté.
{
  const rf = fourCylinderCoupe();
  rf.rf_set_mode(3);
  const hash = renderStereo(rf, 12, 128, (step) => rf.rf_set_listener_position(-8 + 1.2 * (step * 128) / 44100, 0.3, 1.2));
  check('tête en mouvement', hash, native(...coupeArgs.slice(0, -2), '--seconds', '12', '--exhaust', 'sport', '--placement', 'front', '--insulation', 'light', '--routing', 'single', '--walk', '1'));
}
// La consommation : mêmes pas, mêmes chiffres, au bit près une fois arrondis en float comme le renvoie le WASM.
function nativeFigures(...args) {
  const text = execFileSync(renderPath, ['--out', 'none', '--fuel', '1', ...args]).toString();
  const line = text.split('\n').find((item) => item.startsWith('raw '));
  return Object.fromEntries(line.slice(4).split(' ').map((pair) => {
    const [key, value] = pair.split('=');
    return [key, Math.fround(Number(value))];
  }));
}
/** Autant de blocs entiers que le rendu natif, qui ne raccourcit pas le dernier : le modèle s'arrête au même pas. */
function driveLikeNative(rf, seconds) {
  for (let i = 0; i < Math.ceil((44100 * seconds) / 128); i++) rf.rf_render(128);
}
function checkFigures(label, rf, native) {
  const figures = { fuel_lph: rf.rf_fuel_lph(), fuel_trip_l: rf.rf_fuel_trip_l(), trip_km: rf.rf_trip_km(), oil_trip_ml: rf.rf_oil_trip_ml(), energy_trip_kwh: rf.rf_energy_trip_kwh() };
  for (const [key, value] of Object.entries(figures)) check(`${label}, ${key}`, String(value), String(native[key]));
}
{
  const rf = load();
  rf.rf_set_car(BODY.supercar, 1500, 20, DRIVE.all, PLACEMENT.mid, INSULATION.stripped);
  rf.rf_set_gearbox(BOX.dual_clutch, 0, 0, 0, 0, 0, 0, 0, 0);
  rf.rf_set_config(10, LAYOUT.v, 5204, 85, 8500, EXHAUST.straight, 0, 0, 0, 2, 0, 1, ROUTING.single, 1, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  driveLikeNative(rf, 60);
  checkFigures('consommation, V10', rf, nativeFigures('--preset', 'v10_open', '--scenario', 'lap'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.van, 1950, 16, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_gearbox(BOX.synchro, 0, 0, 0, 0, 0, 0, 0, 1);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 85, 4500, EXHAUST.stock, 1, 1, 0, 0, 0, 1.8, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  driveLikeNative(rf, 60);
  checkFigures('consommation, diesel', rf, nativeFigures('--preset', 'diesel_four', '--scenario', 'lap'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1800, 19, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_electric();
  rf.rf_set_mode(3);
  driveLikeNative(rf, 60);
  checkFigures('consommation, électrique', rf, nativeFigures('--preset', 'electric', '--scenario', 'lap'));
}
// Remise à zéro : plus rien au compteur, et le son continue comme si de rien n'était.
{
  const rf = load();
  rf.rf_set_car(BODY.city, 1100, 15, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, 1, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  driveLikeNative(rf, 10);
  rf.rf_reset_trip();
  check('remise à zéro du trajet', String(rf.rf_fuel_trip_l() + rf.rf_oil_trip_ml() + rf.rf_trip_km()), '0');
}
// Les modes de la boîte automatique et les trajets : logique de passage, pente, virages, remorque.
for (const [mode, scenario] of [['sport', 'mountain'], ['eco', 'country'], ['track', 'track'], ['normal', 'towing']]) {
  const rf = sixCylinders(BOX.dual_clutch, FAMILY.matched, TEETH.helical, 0);
  rf.rf_set_shift_mode(SHIFT[mode]);
  rf.rf_set_scenario(SCENARIO[scenario]);
  rf.rf_set_mode(FRESH);
  check(`trajet ${scenario}, ${mode}`, render(rf, 30, 128), native('--preset', 'inline_six', '--gearbox', 'dual_clutch', '--scenario', 'lap', '--seconds', '30', '--drive-scenario', scenario, '--shift-mode', mode));
}
// Le convertisseur et son pontage, qui dépend du mode.
{
  const rf = sixCylinders(BOX.converter, FAMILY.matched, TEETH.helical, 0);
  rf.rf_set_shift_mode(SHIFT.eco);
  rf.rf_set_scenario(SCENARIO.motorway);
  rf.rf_set_mode(FRESH);
  check('autoroute, convertisseur, éco', render(rf, 30, 128), native('--preset', 'inline_six', '--gearbox', 'converter', '--scenario', 'lap', '--seconds', '30', '--drive-scenario', 'motorway', '--shift-mode', 'eco'));
}
// Pied au plancher au volant : les passages du mode piste, plus courts.
{
  const rf = citadine();
  rf.rf_set_shift_mode(SHIFT.track);
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(2);
  const hash = render(rf, 20, 128, (step) => rf.rf_set_controls(step * TICK_S >= 1 ? 1 : 0, 0, 0));
  check('plein gaz, mode piste', hash, native('--preset', 'city_14', '--scenario', 'launch', '--shift-mode', 'track'));
}
// Électrique : un trajet en pente, sans mode de boîte.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1800, 19, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_electric();
  rf.rf_set_scenario(SCENARIO.mountain);
  rf.rf_set_mode(3);
  check('col, électrique', render(rf, 30, 128), native('--preset', 'electric', '--scenario', 'lap', '--seconds', '30', '--drive-scenario', 'mountain'));
}
// Le retard du turbo : coup de gaz, lever de pied, reprise, le long du modèle. Mêmes phases que `turboStab` en C++.
function turboStab(rf, seconds) {
  let phase = 0;
  let since = 0;
  rf.rf_set_transmission(1);
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(2);
  rf.rf_shift(1);
  const top = Math.min(rf.rf_gears(), 4);
  return render(rf, seconds, 128, (step) => {
    const t = step * TICK_S;
    const engine = rf.rf_rpm();
    let pedal = 0;
    let brake = 0;
    switch (phase) {
      case 0:
        pedal = t > 0.3 ? 0.625 : 0;
        if (rf.rf_gear() < top && !rf.rf_shifting() && engine > 3000) rf.rf_shift(1);
        if (rf.rf_gear() === top && !rf.rf_shifting()) { phase = 1; since = t; }
        break;
      case 1:
        pedal = engine < 1800 ? 0.25 : 0;
        brake = engine > 2000 ? 0.125 : 0;
        if (t - since > 5) { phase = 2; since = t; }
        break;
      case 2:
        pedal = 1;
        if (engine > 4200) { phase = 3; since = t; }
        break;
      case 3:
        if (t - since > 0.35) { phase = 4; since = t; }
        break;
      case 4:
        pedal = 1;
        if (t - since > 1.5) { phase = 5; since = t; }
        break;
      default:
        brake = engine > 1500 ? 0.125 : 0;
        break;
    }
    rf.rf_set_controls(pedal, brake, 0);
  }, true);
}
const turboArgs = ['--cylinders', '4', '--cc', '2000', '--bore', '84', '--redline', '6500', '--aspiration', 'turbo', '--boost', '1.3', '--exhaust', 'sport', '--body', 'compact', '--mass', '1400', '--rim', '18', '--scenario', 'turbo', '--seconds', '24'];
for (const [size, release] of [['small', 0], ['medium', 1], ['large', 0]]) {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, release, 1.3, ROUTING.single, TURBO[size], 0, 0, 400, 0, 0);
  check(`retard du turbo, ${size}`, turboStab(rf, 24), native(...turboArgs, '--turbo', size, '--release', release ? 'flutter' : 'dump_valve'));
}
// La carrosserie des années 80 : son rang dans `Body`, sa ligne, son plan, et l'isolation dans l'habitacle.
function retroCoupe() {
  const rf = load();
  rf.rf_set_car(BODY.retro, 1150, 14, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_config(6, LAYOUT.inline, 2500, 84, 6400, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 1, 0);
  return rf;
}
{
  const rf = retroCoupe();
  rf.rf_set_mode(3);
  check('tour complet, coupé années 80', render(rf, 60, 128), native('--preset', 'retro_coupe', '--scenario', 'lap'));
}
{
  const rf = retroCoupe();
  rf.rf_set_listener(LISTENER.cabin);
  rf.rf_set_mode(3);
  check('habitacle, coupé années 80', renderStereo(rf, 20, 128), native('--preset', 'retro_coupe', '--stereo', '1', '--scenario', 'lap', '--seconds', '20', '--listener', 'cabin'));
}
// Le filtre à air : l'air chaud et la perte sur un turbo, et le bruit de bouche, jusque dans sa réponse calculée.
for (const [name, value] of [['panel', 1], ['cone', 2], ['stacks', 3]]) {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, 0, 1.3, ROUTING.single, TURBO.medium, 0, 0, 400, 0, value);
  check(`retard du turbo, filtre ${name}`, turboStab(rf, 24), native(...turboArgs, '--filter', name));
}
{
  const rf = citadine();
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 3);
  rf.rf_set_mode(FRESH);
  check('tour, cornets', render(rf, 60, 128), native('--preset', 'city_14', '--scenario', 'lap', '--filter', 'stacks'));
}
// Le tube avant la turbine, des deux côtés de la référence : `portable::pow` et `exp` doivent donner le même arbre partout.
for (const mm of [150, 2500]) {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, 0, 1.3, ROUTING.single, TURBO.medium, 0, 0, mm, 0, 0);
  check(`retard du turbo, tube de ${mm} mm`, turboStab(rf, 24), native(...turboArgs, '--turbo', 'medium', '--release', 'dump_valve', '--turbo-pipe', String(mm)));
}
// L'anti-lag : chaque système, le long du même retard du turbo, au son comme à la consommation.
const ANTI_LAG = { retard: 1, air: 2, cut: 3 };
for (const [name, code] of Object.entries(ANTI_LAG)) {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, 0, 1.3, ROUTING.single, TURBO.medium, 0, code, 400, 0, 0);
  check(`anti-lag, ${name}`, turboStab(rf, 24), native(...turboArgs, '--turbo', 'medium', '--release', 'dump_valve', '--antilag', name));
}
// Simple arbre : le son à régime tenu, puis le tour complet, où le couple change aussi. Deux rangées, deux lignes.
{
  const rf = load();
  rf.rf_set_car(BODY.city, 1100, 15, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 1, 0);
  rf.rf_set_mode(4);
  rf.rf_set_manual_rpm(5000);
  check('simple arbre, régime tenu', render(rf, 4, 512), native('--cylinders', '4', '--cc', '1360', '--bore', '75', '--redline', '6200', '--scenario', 'steady', '--rpm', '5000', '--cam', 'sohc'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1700, 19, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_config(8, LAYOUT.cross_plane, 5000, 96, 6000, EXHAUST.sport, 0, 0, 0, 1, 0, 1, ROUTING.dual, TURBO.medium, 0, 0, 400, 1, 0);
  rf.rf_set_mode(3);
  check('simple arbre, tour de V8', render(rf, 60, 128), native('--preset', 'v8_cross_plane', '--cam', 'sohc', '--scenario', 'lap'));
}
// Le compresseur mécanique : le coup de gaz de `turboStab`, puis plein gaz au volant, pour chaque type.
const chargerArgs = ['--cylinders', '4', '--cc', '2000', '--bore', '84', '--redline', '6500', '--aspiration', 'supercharger', '--boost', '1', '--exhaust', 'sport', '--body', 'compact', '--mass', '1400', '--rim', '18'];
const chargerLoad = (kind) => {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 2, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, kind, 0, 400, 0, 0);
  return rf;
};
for (const [name, kind] of [['roots', 0], ['twin_screw', 1], ['centrifugal', 2]]) {
  check(`compresseur, ${name}`, turboStab(chargerLoad(kind), 24), native(...chargerArgs, '--supercharger', name, '--scenario', 'turbo', '--seconds', '24'));
  const rf = chargerLoad(kind);
  rf.rf_set_mode(FRESH);
  rf.rf_set_mode(2);
  const hash = render(rf, 20, 128, (step) => rf.rf_set_controls(step * TICK_S >= 1 ? 1 : 0, 0, 0));
  check(`compresseur, plein gaz, ${name}`, hash, native(...chargerArgs, '--supercharger', name, '--scenario', 'launch'));
}
// Le carburateur : son film, sa pompe et son souffle passent au bit près, comme le reste.
for (const [name, system] of [['single', 1], ['multi', 2]]) {
  const rf = load();
  rf.rf_set_car(BODY.compact, 1200, 16, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6800, EXHAUST.sport, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0, system);
  rf.rf_set_mode(3);
  check(`carburateur, ${name}`, render(rf, 30, 128), native('--cylinders', '4', '--cc', '2000', '--bore', '84', '--redline', '6800', '--exhaust', 'sport', '--body', 'compact', '--mass', '1200', '--rim', '16', '--carb', name, '--scenario', 'lap', '--seconds', '30'));
}
// Les rencontres de réglages : chacune par un chemin que les autres cas ne prennent pas.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 2, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 2);
  check('compresseur derrière un cône', turboStab(rf, 24), native(...chargerArgs, '--supercharger', 'roots', '--filter', 'cone', '--scenario', 'turbo', '--seconds', '24'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, 0, 1.3, ROUTING.single, TURBO.medium, 0, ANTI_LAG.air, 3000, 0, 3, 2);
  check('turbo, tube de 3 m, anti-lag, cornets, corps multiples', turboStab(rf, 24), native(...turboArgs, '--turbo', 'medium', '--release', 'dump_valve', '--antilag', 'air', '--turbo-pipe', '3000', '--filter', 'stacks', '--carb', 'multi'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1300, 18, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(2, LAYOUT.rotary, 1308, 84, 8000, EXHAUST.sport, 2, 0, 0, 0, 0, 0.7, ROUTING.single, TURBO.medium, 1, 0, 400, 1, 2, 1);
  rf.rf_set_mode(3);
  check('birotor à vis, cône, carburateur', render(rf, 20, 128), native('--layout', 'rotary', '--cylinders', '2', '--cc', '1308', '--redline', '8000', '--exhaust', 'sport', '--aspiration', 'supercharger', '--boost', '0.7', '--supercharger', 'twin_screw', '--cam', 'sohc', '--filter', 'cone', '--carb', 'single', '--body', 'coupe', '--mass', '1300', '--rim', '18', '--drive', 'rear', '--insulation', 'standard', '--scenario', 'lap', '--seconds', '20'));
}
// Le socle moteur : tous ses réglages changés à la fois, et les valeurs ajoutées aux énumérations, au son comme à la consommation.
{
  const changed = {
    ambientC: 35, altitudeM: 2000, startTemp: START.cold, compressionRatio: 11.5, fuelGrade: GRADE.e85, ignitionOffsetDeg: -4, fullLoadLambda: 0.85,
    revLimiter: LIMITER.ignition_cut, launchRpm: 4500, flatShift: 1, tractionControl: TRACTION.full, dieselInjection: DIESEL_INJECTION.common_rail, egr: 1,
    particulateFilter: 1, camDurationDeg: 270, camLiftMm: 11.5, lobeSeparationDeg: 108, valveTiming: VVT.phased_two_step, valveSprings: SPRINGS.stiff,
    turboLayout: TURBO_LAYOUT.twincharger, variableGeometry: 1, boostControl: BOOST_CONTROL.by_gear, intercooler: INTERCOOLER.water, waterInjection: 1, nitrousKw: 75,
    cycle: CYCLE.two_stroke,
  };
  const options = ['--ambient', '35', '--altitude', '2000', '--start', 'cold', '--compression', '11.5', '--grade', 'e85', '--advance', '-4', '--lambda', '0.85',
    '--limiter', 'ignition_cut', '--launch', '4500', '--flat-shift', '1', '--traction', 'full', '--diesel-injection', 'common_rail', '--egr', '1', '--dpf', '1',
    '--cam-duration', '270', '--cam-lift', '11.5', '--lsa', '108', '--vvt', 'phased_two_step', '--springs', 'stiff', '--turbo-layout', 'twincharger', '--vgt', '1',
    '--boost-control', 'by_gear', '--intercooler', 'water', '--water', '1', '--nitrous', '75', '--cycle', 'two_stroke'];
  const engineArgs = ['--cylinders', '6', '--layout', 'vr', '--cc', '2800', '--bore', '81', '--redline', '6500', '--exhaust', 'expansion', '--aspiration', 'turbo', '--boost', '1.2',
    '--cam', 'ohv', '--fuel-system', 'direct', '--body', 'compact', '--mass', '1450', '--rim', '18', '--drive', 'all', '--placement', 'front', '--insulation', 'standard'];
  const socle = () => {
    const rf = load();
    rf.rf_set_car(BODY.compact, 1450, 18, DRIVE.all, PLACEMENT.front, INSULATION.standard);
    tune(rf, changed);
    rf.rf_set_config(6, LAYOUT.vr, 2800, 81, 6500, EXHAUST.expansion, 1, 0, 0, 0, 0, 1.2, ROUTING.single, TURBO.medium, 0, 0, 400, CAMSHAFT.ohv, 0, FUEL_SYSTEM.direct);
    rf.rf_set_mode(3);
    return rf;
  };
  check('socle moteur, tout changé', render(socle(), 20, 128), native(...engineArgs, ...options, '--scenario', 'lap', '--seconds', '20'));
  const rf = socle();
  driveLikeNative(rf, 20);
  checkFigures('socle moteur, consommation', rf, nativeFigures(...engineArgs, ...options, '--scenario', 'lap', '--seconds', '20'));
}
// Le socle moteur, sur un préréglage : les options l'emportent sur lui, et une voiture électrique reçoit aussi ses réglages.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1450, 18, DRIVE.all, PLACEMENT.front, INSULATION.standard);
  tune(rf, { ambientC: -10, altitudeM: 3000, startTemp: START.cold, launchRpm: 5000, tractionControl: TRACTION.sport, flatShift: 1 });
  rf.rf_set_config(4, LAYOUT.boxer, 2500, 99, 6800, EXHAUST.sport, 1, 0, 0, 1, 1, 1.2, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('socle moteur, sur un préréglage', render(rf, 20, 128), native('--preset', 'boxer_four_turbo', '--ambient', '-10', '--altitude', '3000', '--start', 'cold', '--launch', '5000', '--traction', 'sport', '--flat-shift', '1', '--scenario', 'lap', '--seconds', '20'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1800, 19, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  tune(rf, { ambientC: 40, altitudeM: 1500, tractionControl: TRACTION.full });
  rf.rf_set_electric();
  rf.rf_set_mode(3);
  check('socle moteur, électrique', render(rf, 20, 128), native('--preset', 'electric', '--ambient', '40', '--altitude', '1500', '--traction', 'full', '--scenario', 'lap', '--seconds', '20'));
}
// La combustion : un taux plus haut qui cliquette sans capteur (carburateurs, le son du cliquetis) et avec (injection, l'avance retirée),
// l'E85 sur un turbo et sa consommation, un diesel et un rotatif plus ou moins comprimés, au démarrage comme en roulant.
{
  const carbArgs = ['--cylinders', '4', '--cc', '2000', '--bore', '84', '--redline', '6800', '--exhaust', 'sport', '--body', 'compact', '--mass', '1200', '--rim', '16'];
  for (const [name, system] of [['multi', FUEL_SYSTEM.multi], ['injection', FUEL_SYSTEM.injection]]) {
    const rf = load();
    rf.rf_set_car(BODY.compact, 1200, 16, DRIVE.front, PLACEMENT.front, INSULATION.standard);
    tune(rf, { compressionRatio: 12.5 });
    rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6800, EXHAUST.sport, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0, system);
    check(`cliquetis à 12,5:1, ${name}`, turboStab(rf, 24), native(...carbArgs, '--carb', name === 'multi' ? 'multi' : 'injection', '--compression', '12.5', '--scenario', 'turbo', '--seconds', '24'));
  }
  const hotArgs = ['--cylinders', '4', '--cc', '2000', '--bore', '84', '--redline', '6500', '--aspiration', 'turbo', '--boost', '1.3', '--exhaust', 'sport', '--body', 'compact', '--mass', '1400', '--rim', '18', '--compression', '10.5', '--grade', 'e85'];
  const ethanol = () => {
    const rf = load();
    rf.rf_set_car(BODY.compact, 1400, 18, DRIVE.front, PLACEMENT.front, INSULATION.standard);
    tune(rf, { compressionRatio: 10.5, fuelGrade: GRADE.e85 });
    rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, 0, 1.3, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
    rf.rf_set_mode(3);
    return rf;
  };
  check('E85 et 10,5:1 sur un turbo', render(ethanol(), 20, 128), native(...hotArgs, '--scenario', 'lap', '--seconds', '20'));
  const rf = ethanol();
  driveLikeNative(rf, 20);
  checkFigures('E85 et 10,5:1, consommation', rf, nativeFigures(...hotArgs, '--scenario', 'lap', '--seconds', '20'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.van, 1950, 16, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_gearbox(BOX.synchro, 0, 0, 0, 0, 0, 0, 0, 1);
  tune(rf, { compressionRatio: 15 });
  rf.rf_set_config(4, LAYOUT.inline, 2000, 85, 4500, EXHAUST.stock, 1, 1, 0, 0, 0, 1.8, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(1);
  rf.rf_crank();
  check('diesel à 15:1, démarrage', render(rf, 5, 128), native('--preset', 'diesel_four', '--compression', '15', '--scenario', 'start'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1300, 18, DRIVE.rear, PLACEMENT.front, INSULATION.standard);
  tune(rf, { compressionRatio: 11 });
  rf.rf_set_config(2, LAYOUT.rotary, 1308, 84, 9000, EXHAUST.sport, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium, 0, 0, 400, 0, 0);
  rf.rf_set_mode(3);
  check('birotor à 11:1', render(rf, 20, 128), native('--layout', 'rotary', '--cylinders', '2', '--cc', '1308', '--redline', '9000', '--exhaust', 'sport', '--body', 'coupe', '--mass', '1300', '--rim', '18', '--drive', 'rear', '--compression', '11', '--scenario', 'lap', '--seconds', '20'));
}
process.exit(failed ? 1 : 0);
