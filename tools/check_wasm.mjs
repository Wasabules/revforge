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
const EXHAUST = { stock: 0, sport: 1, straight: 2, no_muffler: 3, no_resonator: 4, quiet: 5 };
const BODY = { city: 0, compact: 1, coupe: 2, supercar: 3, van: 4 };
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
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium);
  rf.rf_set_mode(4);
  rf.rf_set_manual_rpm(5000);
  check('régime tenu', render(rf, 4, 512), native('--cylinders', '4', '--cc', '1360', '--bore', '75', '--redline', '6200', '--scenario', 'steady', '--rpm', '5000'));
}
// Tour de 60 s, avec modèle de conduite, et moteur chargé.
{
  const rf = load();
  rf.rf_set_car(BODY.supercar, 1500, 20, DRIVE.all, PLACEMENT.mid, INSULATION.stripped);
  // Comme le préréglage : double embrayage.
  rf.rf_set_gearbox(BOX.dual_clutch, 0, 0, 0, 0, 0, 0, 0, 0);
  rf.rf_set_config(10, LAYOUT.v, 5204, 85, 8500, EXHAUST.straight, 0, 0, 0, 2, 0, 1, ROUTING.single, TURBO.medium);
  rf.rf_set_mode(3);
  check('tour complet, V10', render(rf, 60, 128), native('--preset', 'v10_open', '--scenario', 'lap'));
}
// Turbo et flutter au lever de pied.
{
  const rf = load();
  rf.rf_set_car(BODY.compact, 1450, 18, DRIVE.all, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_config(4, LAYOUT.boxer, 2500, 99, 6800, EXHAUST.sport, 1, 0, 0, 1, 1, 1.2, ROUTING.single, TURBO.medium);
  rf.rf_set_mode(3);
  check('tour complet, boxer turbo', render(rf, 60, 128), native('--preset', 'boxer_four_turbo', '--scenario', 'lap'));
}
// Démarrage : le démarreur, puis le moteur qui prend.
{
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1700, 19, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_config(8, LAYOUT.cross_plane, 5000, 96, 6000, EXHAUST.sport, 0, 0, 0, 1, 0, 1, ROUTING.dual, TURBO.medium);
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
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium);
  return rf;
}
function sixCylinders(kind, family, teeth, limit) {
  const rf = load();
  rf.rf_set_car(BODY.coupe, 1500, 18, DRIVE.rear, PLACEMENT.front, INSULATION.light);
  rf.rf_set_gearbox(kind, 0, family, 0, teeth, 0, 0, limit, 0);
  rf.rf_set_config(6, LAYOUT.inline, 3246, 87, 8000, EXHAUST.sport, 0, 0, 0, 0, 0, 1, ROUTING.single, TURBO.medium);
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
  rf.rf_set_config(6, LAYOUT.v, 3000, 86, 6500, exhaust, 0, 0, 0, 0, 0, 1, routing, TURBO.medium);
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
  rf.rf_set_config(10, LAYOUT.v, 5204, 85, 8500, EXHAUST.straight, 0, 0, 0, 2, 0, 1, ROUTING.single, 1);
  rf.rf_set_mode(3);
  driveLikeNative(rf, 60);
  checkFigures('consommation, V10', rf, nativeFigures('--preset', 'v10_open', '--scenario', 'lap'));
}
{
  const rf = load();
  rf.rf_set_car(BODY.van, 1950, 16, DRIVE.front, PLACEMENT.front, INSULATION.standard);
  rf.rf_set_gearbox(BOX.synchro, 0, 0, 0, 0, 0, 0, 0, 1);
  rf.rf_set_config(4, LAYOUT.inline, 2000, 85, 4500, EXHAUST.stock, 1, 1, 0, 0, 0, 1.8, ROUTING.single, TURBO.medium);
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
  rf.rf_set_config(4, LAYOUT.inline, 1360, 75, 6200, EXHAUST.stock, 0, 0, 0, 0, 0, 1, ROUTING.single, 1);
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
  rf.rf_set_config(4, LAYOUT.inline, 2000, 84, 6500, EXHAUST.sport, 1, 0, 0, 0, release, 1.3, ROUTING.single, TURBO[size]);
  check(`retard du turbo, ${size}`, turboStab(rf, 24), native(...turboArgs, '--turbo', size, '--release', release ? 'flutter' : 'dump_valve'));
}
process.exit(failed ? 1 : 0);
