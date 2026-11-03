import presetData from '../../../presets/presets.json';
import { presetText, t } from './i18n.svelte';
import { DISPLACEMENT_LIMITS, STROKE_LIMITS, engineStroke } from './stroke';
import { isRotary, ROTOR_COUNTS } from './rotary';
import { builtLayout, VR_CYLINDERS, W_CYLINDERS } from './architecture';
import { isTwoStroke, TWO_STROKE_MAX_CYLINDERS } from './twoStroke';

export { isRotary } from './rotary';
export { isTwoStroke } from './twoStroke';

export type Layout = 'inline' | 'v' | 'cross_plane' | 'boxer' | 'rotary' | 'vr' | 'w';
export type Exhaust = 'stock' | 'sport' | 'straight' | 'no_muffler' | 'no_resonator' | 'quiet' | 'expansion';
export type Routing = 'single' | 'dual' | 'h' | 'x';
export type Placement = 'front' | 'mid' | 'rear';
export type Insulation = 'stripped' | 'light' | 'standard' | 'comfort' | 'luxury';
export type Aspiration = 'natural' | 'turbo' | 'supercharger';
export type Fuel = 'petrol' | 'diesel';
export type Pops = 'off' | 'light' | 'strong';
export type Release = 'dump_valve' | 'flutter';
export type TurboSize = 'small' | 'medium' | 'large';
export type SuperchargerKind = 'roots' | 'twin_screw' | 'centrifugal';
export type AntiLag = 'off' | 'retard' | 'air' | 'cut';
export type Camshaft = 'dohc' | 'sohc' | 'ohv';
export type AirFilter = 'stock' | 'panel' | 'cone' | 'stacks';
export type FuelSystem = 'injection' | 'single' | 'multi' | 'direct';
// Les réglages du socle moteur : mêmes valeurs que les énumérations C++, dans le même ordre.
export type StartTemp = 'warm' | 'cold';
export type FuelGrade = 'ron95' | 'ron98' | 'race102' | 'e85' | 'methanol';
export type RevLimiter = 'stock' | 'soft' | 'ignition_cut';
export type TractionControl = 'off' | 'sport' | 'full';
export type DieselInjection = 'mechanical' | 'common_rail';
export type ValveTiming = 'fixed' | 'phased' | 'two_step' | 'phased_two_step';
export type ValveSprings = 'stock' | 'stiff';
export type TurboLayout = 'single' | 'twin_parallel' | 'twin_sequential' | 'twincharger';
export type BoostControl = 'spring' | 'electronic' | 'by_gear';
export type Intercooler = 'air_to_air' | 'air_to_water' | 'none';
export type EngineCycle = 'four_stroke' | 'two_stroke';
export type Body = 'city' | 'compact' | 'coupe' | 'supercar' | 'van' | 'retro';
export type Drive = 'front' | 'rear' | 'all';
export type BoxKind = 'synchro' | 'dog' | 'sequential' | 'dual_clutch' | 'converter';
export type BoxFamily = 'matched' | 'short' | 'close' | 'long' | 'custom';
export type BoxTeeth = 'helical' | 'spur';
export type BoxFlywheel = 'single' | 'dual_mass';

export interface EngineConfig {
  cylinders: number;
  layout: Layout;
  displacementCc: number;
  boreMm: number;
  redlineRpm: number;
  exhaust: Exhaust;
  aspiration: Aspiration;
  fuel: Fuel;
  /** Conservé pour le format du lien de partage : le régime vient maintenant de la physique. */
  multiplier: number;
  pops: Pops;
  turboRelease: Release;
  /** Pression maximale du turbo, au-dessus de l'atmosphère (bar). */
  boostBar: number;
  /** Taille du turbo : un petit se réveille tôt et s'essouffle en haut, un gros tarde et tient jusqu'au rupteur. */
  turboSize: TurboSize;
  /** Le compresseur mécanique, sur `aspiration: 'supercharger'`. */
  superchargerKind: SuperchargerKind;
  /** L'anti-lag, sur un turbo. */
  antiLag: AntiLag;
  /** Longueur du tube entre les cylindres et la turbine (mm). */
  turboPipeMm: number;
  camshaft: Camshaft;
  airFilter: AirFilter;
  /** L'alimentation : injection, un carburateur sur une pipe commune, un corps par cylindre, injection directe. Sans effet sur un diesel. */
  fuelSystem: FuelSystem;
  // ── Les réglages du socle moteur, dans l'ordre de `TUNING_FIELDS` et de `rf_set_tuning`. 0 marqué « d'origine » : le moteur garde ce qu'il a. ──
  /** Air ambiant (°C). */
  ambientC: number;
  /** Altitude (m). */
  altitudeM: number;
  startTemp: StartTemp;
  /** Taux de compression géométrique ; 0 : d'origine. */
  compressionRatio: number;
  fuelGrade: FuelGrade;
  /** Décalage de la carte d'avance (°). */
  ignitionOffsetDeg: number;
  /** Richesse à pleine charge ; 0 : carte d'origine. */
  fullLoadLambda: number;
  revLimiter: RevLimiter;
  /** Régime tenu par le launch control (tr/min) ; 0 : sans. */
  launchRpm: number;
  flatShift: boolean;
  tractionControl: TractionControl;
  dieselInjection: DieselInjection;
  egr: boolean;
  particulateFilter: boolean;
  /** Durée de came (°) ; 0 : d'origine. */
  camDurationDeg: number;
  /** Levée de came (mm) ; 0 : d'origine. */
  camLiftMm: number;
  /** Angle entre cames (°) ; 0 : d'origine. */
  lobeSeparationDeg: number;
  valveTiming: ValveTiming;
  valveSprings: ValveSprings;
  turboLayout: TurboLayout;
  variableGeometry: boolean;
  boostControl: BoostControl;
  intercooler: Intercooler;
  waterInjection: boolean;
  /** Puissance du kit de protoxyde d'azote (kW) ; 0 : sans. */
  nitrousKw: number;
  cycle: EngineCycle;
  electric: boolean;
  body: Body;
  massKg: number;
  rimInch: number;
  drive: Drive;
  boxKind: BoxKind;
  /** 0 : selon la carrosserie. */
  boxGears: number;
  boxFamily: BoxFamily;
  /** Famille sur mesure : le rapport de chaque vitesse, séparés par des virgules. */
  boxRatios: string;
  /** 0 : le pont est choisi pour atteindre la vitesse de pointe. */
  boxFinal: number;
  boxTeeth: BoxTeeth;
  /** Angle d'hélice (degrés), module (mm), couple admissible (Nm) : 0 laisse le constructeur décider. */
  boxHelix: number;
  boxModule: number;
  boxLimit: number;
  boxFlywheel: BoxFlywheel;
  /** Le chemin des gaz : une ligne, ou deux, reliées ou non. */
  routing: Routing;
  /** Où est monté le moteur. */
  placement: Placement;
  /** L'insonorisation de la caisse. */
  insulation: Insulation;
}

export interface Preset {
  id: string;
  name: string;
  description: string;
  config: EngineConfig;
}

export const LAYOUTS: { value: Layout; label: string }[] = [
  { value: 'inline', get label() { return t('layout.inline'); } },
  { value: 'v', get label() { return t('layout.v'); } },
  { value: 'cross_plane', get label() { return t('layout.cross_plane'); } },
  { value: 'boxer', get label() { return t('layout.boxer'); } },
  { value: 'rotary', get label() { return t('layout.rotary'); } },
  { value: 'vr', get label() { return t('layout.vr'); } },
  { value: 'w', get label() { return t('layout.w'); } },
];
export const EXHAUSTS: { value: Exhaust; label: string; pops: string }[] = [
  { value: 'stock', get label() { return t('exhaust.stock'); }, get pops() { return t('exhaust.stock.pops'); } },
  { value: 'sport', get label() { return t('exhaust.sport'); }, get pops() { return t('exhaust.sport.pops'); } },
  { value: 'straight', get label() { return t('exhaust.straight'); }, get pops() { return t('exhaust.straight.pops'); } },
  { value: 'no_muffler', get label() { return t('exhaust.no_muffler'); }, get pops() { return t('exhaust.no_muffler.pops'); } },
  { value: 'no_resonator', get label() { return t('exhaust.no_resonator'); }, get pops() { return t('exhaust.no_resonator.pops'); } },
  { value: 'quiet', get label() { return t('exhaust.quiet'); }, get pops() { return t('exhaust.quiet.pops'); } },
  { value: 'expansion', get label() { return t('exhaust.expansion'); }, get pops() { return t('exhaust.expansion.pops'); } },
];
export { EXHAUST_LINES, type LinePart } from './exhaustLines';
export const ROUTINGS: { value: Routing; label: string }[] = [
  { value: 'single', get label() { return t('routing.single'); } },
  { value: 'dual', get label() { return t('routing.dual'); } },
  { value: 'h', get label() { return t('routing.h'); } },
  { value: 'x', get label() { return t('routing.x'); } },
];
export const PLACEMENTS: { value: Placement; label: string }[] = [
  { value: 'front', get label() { return t('placement.front'); } },
  { value: 'mid', get label() { return t('placement.mid'); } },
  { value: 'rear', get label() { return t('placement.rear'); } },
];
export const INSULATIONS: { value: Insulation; label: string; help: string }[] = [
  { value: 'stripped', get label() { return t('insulation.stripped'); }, get help() { return t('insulation.stripped.help'); } },
  { value: 'light', get label() { return t('insulation.light'); }, get help() { return t('insulation.light.help'); } },
  { value: 'standard', get label() { return t('insulation.standard'); }, get help() { return t('insulation.standard.help'); } },
  { value: 'comfort', get label() { return t('insulation.comfort'); }, get help() { return t('insulation.comfort.help'); } },
  { value: 'luxury', get label() { return t('insulation.luxury'); }, get help() { return t('insulation.luxury.help'); } },
];
export const ASPIRATIONS: { value: Aspiration; label: string }[] = [
  { value: 'natural', get label() { return t('aspiration.natural'); } },
  { value: 'turbo', get label() { return t('aspiration.turbo'); } },
  { value: 'supercharger', get label() { return t('aspiration.supercharger'); } },
];
export const FUELS: { value: Fuel; label: string }[] = [
  { value: 'petrol', get label() { return t('fuel.petrol'); } },
  { value: 'diesel', get label() { return t('fuel.diesel'); } },
];
export const POPS: { value: Pops; label: string }[] = [
  { value: 'off', get label() { return t('pops.off'); } },
  { value: 'light', get label() { return t('pops.light'); } },
  { value: 'strong', get label() { return t('pops.strong'); } },
];
export const RELEASES: { value: Release; label: string }[] = [
  { value: 'dump_valve', get label() { return t('release.dump_valve'); } },
  { value: 'flutter', get label() { return t('release.flutter'); } },
];
/** La position donne la valeur envoyée au C++ : `TurboSize`. */
export const TURBO_SIZES: { value: TurboSize; label: string; help: string }[] = [
  { value: 'small', get label() { return t('turboSize.small'); }, get help() { return t('turboSize.small.help'); } },
  { value: 'medium', get label() { return t('turboSize.medium'); }, get help() { return t('turboSize.medium.help'); } },
  { value: 'large', get label() { return t('turboSize.large'); }, get help() { return t('turboSize.large.help'); } },
];
export { TURBO_SCALE } from './exhaustLines';
/** La position donne la valeur envoyée au C++ : `SuperchargerKind`. */
export const SUPERCHARGER_KINDS: { value: SuperchargerKind; label: string; help: string }[] = [
  { value: 'roots', get label() { return t('supercharger.roots'); }, get help() { return t('supercharger.roots.help'); } },
  { value: 'twin_screw', get label() { return t('supercharger.twin_screw'); }, get help() { return t('supercharger.twin_screw.help'); } },
  { value: 'centrifugal', get label() { return t('supercharger.centrifugal'); }, get help() { return t('supercharger.centrifugal.help'); } },
];
/** La position donne la valeur envoyée au C++ : `AntiLag`. */
export const ANTI_LAGS: { value: AntiLag; label: string; help: string }[] = [
  { value: 'off', get label() { return t('antiLag.off'); }, get help() { return t('antiLag.off.help'); } },
  { value: 'retard', get label() { return t('antiLag.retard'); }, get help() { return t('antiLag.retard.help'); } },
  { value: 'air', get label() { return t('antiLag.air'); }, get help() { return t('antiLag.air.help'); } },
  { value: 'cut', get label() { return t('antiLag.cut'); }, get help() { return t('antiLag.cut.help'); } },
];
/** La position donne la valeur envoyée au C++ : `Camshaft`. */
export const CAMSHAFTS: { value: Camshaft; label: string; help: string }[] = [
  { value: 'dohc', get label() { return t('camshaft.dohc'); }, get help() { return t('camshaft.dohc.help'); } },
  { value: 'sohc', get label() { return t('camshaft.sohc'); }, get help() { return t('camshaft.sohc.help'); } },
  { value: 'ohv', get label() { return t('camshaft.ohv'); }, get help() { return t('camshaft.ohv.help'); } },
];
/** La position donne la valeur envoyée au C++ : `AirFilter`. */
export const AIR_FILTERS: { value: AirFilter; label: string; help: string }[] = [
  { value: 'stock', get label() { return t('airFilter.stock'); }, get help() { return t('airFilter.stock.help'); } },
  { value: 'panel', get label() { return t('airFilter.panel'); }, get help() { return t('airFilter.panel.help'); } },
  { value: 'cone', get label() { return t('airFilter.cone'); }, get help() { return t('airFilter.cone.help'); } },
  { value: 'stacks', get label() { return t('airFilter.stacks'); }, get help() { return t('airFilter.stacks.help'); } },
];
/** La position donne la valeur envoyée au C++ : `FuelSystem`. */
export const FUEL_SYSTEMS: { value: FuelSystem; label: string; help: string }[] = [
  { value: 'injection', get label() { return t('fuelSystem.injection'); }, get help() { return t('fuelSystem.injection.help'); } },
  { value: 'single', get label() { return t('fuelSystem.single'); }, get help() { return t('fuelSystem.single.help'); } },
  { value: 'multi', get label() { return t('fuelSystem.multi'); }, get help() { return t('fuelSystem.multi.help'); } },
  { value: 'direct', get label() { return t('fuelSystem.direct'); }, get help() { return t('fuelSystem.direct.help'); } },
];
// ── Les réglages du socle moteur. La position dans chaque tableau donne la valeur envoyée au C++. ──
export const START_TEMPS: { value: StartTemp; label: string; help: string }[] = [
  { value: 'warm', get label() { return t('startTemp.warm'); }, get help() { return t('startTemp.warm.help'); } },
  { value: 'cold', get label() { return t('startTemp.cold'); }, get help() { return t('startTemp.cold.help'); } },
];
export const FUEL_GRADES: { value: FuelGrade; label: string; help: string }[] = [
  { value: 'ron95', get label() { return t('fuelGrade.ron95'); }, get help() { return t('fuelGrade.ron95.help'); } },
  { value: 'ron98', get label() { return t('fuelGrade.ron98'); }, get help() { return t('fuelGrade.ron98.help'); } },
  { value: 'race102', get label() { return t('fuelGrade.race102'); }, get help() { return t('fuelGrade.race102.help'); } },
  { value: 'e85', get label() { return t('fuelGrade.e85'); }, get help() { return t('fuelGrade.e85.help'); } },
  { value: 'methanol', get label() { return t('fuelGrade.methanol'); }, get help() { return t('fuelGrade.methanol.help'); } },
];
export const REV_LIMITERS: { value: RevLimiter; label: string; help: string }[] = [
  { value: 'stock', get label() { return t('revLimiter.stock'); }, get help() { return t('revLimiter.stock.help'); } },
  { value: 'soft', get label() { return t('revLimiter.soft'); }, get help() { return t('revLimiter.soft.help'); } },
  { value: 'ignition_cut', get label() { return t('revLimiter.ignition_cut'); }, get help() { return t('revLimiter.ignition_cut.help'); } },
];
export const TRACTION_CONTROLS: { value: TractionControl; label: string; help: string }[] = [
  { value: 'off', get label() { return t('tractionControl.off'); }, get help() { return t('tractionControl.off.help'); } },
  { value: 'sport', get label() { return t('tractionControl.sport'); }, get help() { return t('tractionControl.sport.help'); } },
  { value: 'full', get label() { return t('tractionControl.full'); }, get help() { return t('tractionControl.full.help'); } },
];
export const DIESEL_INJECTIONS: { value: DieselInjection; label: string; help: string }[] = [
  { value: 'mechanical', get label() { return t('dieselInjection.mechanical'); }, get help() { return t('dieselInjection.mechanical.help'); } },
  { value: 'common_rail', get label() { return t('dieselInjection.common_rail'); }, get help() { return t('dieselInjection.common_rail.help'); } },
];
export const VALVE_TIMINGS: { value: ValveTiming; label: string; help: string }[] = [
  { value: 'fixed', get label() { return t('valveTiming.fixed'); }, get help() { return t('valveTiming.fixed.help'); } },
  { value: 'phased', get label() { return t('valveTiming.phased'); }, get help() { return t('valveTiming.phased.help'); } },
  { value: 'two_step', get label() { return t('valveTiming.two_step'); }, get help() { return t('valveTiming.two_step.help'); } },
  { value: 'phased_two_step', get label() { return t('valveTiming.phased_two_step'); }, get help() { return t('valveTiming.phased_two_step.help'); } },
];
export const VALVE_SPRINGS: { value: ValveSprings; label: string; help: string }[] = [
  { value: 'stock', get label() { return t('valveSprings.stock'); }, get help() { return t('valveSprings.stock.help'); } },
  { value: 'stiff', get label() { return t('valveSprings.stiff'); }, get help() { return t('valveSprings.stiff.help'); } },
];
export const TURBO_LAYOUTS: { value: TurboLayout; label: string; help: string }[] = [
  { value: 'single', get label() { return t('turboLayout.single'); }, get help() { return t('turboLayout.single.help'); } },
  { value: 'twin_parallel', get label() { return t('turboLayout.twin_parallel'); }, get help() { return t('turboLayout.twin_parallel.help'); } },
  { value: 'twin_sequential', get label() { return t('turboLayout.twin_sequential'); }, get help() { return t('turboLayout.twin_sequential.help'); } },
  { value: 'twincharger', get label() { return t('turboLayout.twincharger'); }, get help() { return t('turboLayout.twincharger.help'); } },
];
export const BOOST_CONTROLS: { value: BoostControl; label: string; help: string }[] = [
  { value: 'spring', get label() { return t('boostControl.spring'); }, get help() { return t('boostControl.spring.help'); } },
  { value: 'electronic', get label() { return t('boostControl.electronic'); }, get help() { return t('boostControl.electronic.help'); } },
  { value: 'by_gear', get label() { return t('boostControl.by_gear'); }, get help() { return t('boostControl.by_gear.help'); } },
];
export const INTERCOOLERS: { value: Intercooler; label: string; help: string }[] = [
  { value: 'air_to_air', get label() { return t('intercooler.air_to_air'); }, get help() { return t('intercooler.air_to_air.help'); } },
  { value: 'air_to_water', get label() { return t('intercooler.air_to_water'); }, get help() { return t('intercooler.air_to_water.help'); } },
  { value: 'none', get label() { return t('intercooler.none'); }, get help() { return t('intercooler.none.help'); } },
];
export const ENGINE_CYCLES: { value: EngineCycle; label: string; help: string }[] = [
  { value: 'four_stroke', get label() { return t('cycle.four_stroke'); }, get help() { return t('cycle.four_stroke.help'); } },
  { value: 'two_stroke', get label() { return t('cycle.two_stroke'); }, get help() { return t('cycle.two_stroke.help'); } },
];
/** Un réglage tout ou rien, dans une rangée de boutons : `Choices` ne prend pas de booléen. */
export const ON_OFF: { value: 'off' | 'on'; label: string }[] = [
  { value: 'off', get label() { return t('toggle.off'); } },
  { value: 'on', get label() { return t('toggle.on'); } },
];
/** Choisir une carrosserie règle aussi une masse, des jantes et une transmission plausibles, et parfois une isolation. */
export const BODIES: { value: Body; label: string; massKg: number; rimInch: number; drive: Drive; placement: Placement; insulation?: Insulation }[] = [
  { value: 'city', get label() { return t('body.city'); }, massKg: 1100, rimInch: 15, drive: 'front', placement: 'front' },
  { value: 'compact', get label() { return t('body.compact'); }, massKg: 1350, rimInch: 18, drive: 'front', placement: 'front' },
  { value: 'coupe', get label() { return t('body.coupe'); }, massKg: 1600, rimInch: 19, drive: 'rear', placement: 'front' },
  { value: 'supercar', get label() { return t('body.supercar'); }, massKg: 1500, rimInch: 20, drive: 'all', placement: 'mid' },
  { value: 'van', get label() { return t('body.van'); }, massKg: 1950, rimInch: 16, drive: 'front', placement: 'front' },
  { value: 'retro', get label() { return t('body.retro'); }, massKg: 1100, rimInch: 14, drive: 'rear', placement: 'front', insulation: 'light' },
];
export const DRIVES: { value: Drive; label: string }[] = [
  { value: 'front', get label() { return t('drive.front'); } },
  { value: 'rear', get label() { return t('drive.rear'); } },
  { value: 'all', get label() { return t('drive.all'); } },
];
/** La position donne la valeur envoyée au C++ : `GearboxKind`. */
export const BOX_KINDS: { value: BoxKind; label: string; help: string }[] = [
  { value: 'synchro', get label() { return t('boxKind.synchro'); }, get help() { return t('boxKind.synchro.help'); } },
  { value: 'dog', get label() { return t('boxKind.dog'); }, get help() { return t('boxKind.dog.help'); } },
  { value: 'sequential', get label() { return t('boxKind.sequential'); }, get help() { return t('boxKind.sequential.help'); } },
  { value: 'dual_clutch', get label() { return t('boxKind.dual_clutch'); }, get help() { return t('boxKind.dual_clutch.help'); } },
  { value: 'converter', get label() { return t('boxKind.converter'); }, get help() { return t('boxKind.converter.help'); } },
];
export const BOX_FAMILIES: { value: BoxFamily; label: string; help: string }[] = [
  { value: 'matched', get label() { return t('boxFamily.matched'); }, get help() { return t('boxFamily.matched.help'); } },
  { value: 'short', get label() { return t('boxFamily.short'); }, get help() { return t('boxFamily.short.help'); } },
  { value: 'close', get label() { return t('boxFamily.close'); }, get help() { return t('boxFamily.close.help'); } },
  { value: 'long', get label() { return t('boxFamily.long'); }, get help() { return t('boxFamily.long.help'); } },
  { value: 'custom', get label() { return t('boxFamily.custom'); }, get help() { return t('boxFamily.custom.help'); } },
];
export const BOX_TEETH: { value: BoxTeeth; label: string }[] = [
  { value: 'helical', get label() { return t('boxTeeth.helical'); } },
  { value: 'spur', get label() { return t('boxTeeth.spur'); } },
];
export const BOX_FLYWHEELS: { value: BoxFlywheel; label: string }[] = [
  { value: 'single', get label() { return t('boxFlywheel.single'); } },
  { value: 'dual_mass', get label() { return t('boxFlywheel.dual_mass'); } },
];
export type ShiftModeId = 'normal' | 'eco' | 'sport' | 'track' | 'paddles';
/** Dans l'ordre de `ShiftMode` en C++ : la position est la valeur envoyée. Choix de conduite, hors du lien de partage. */
export const SHIFT_MODES: { value: ShiftModeId; label: string; help: string }[] = [
  { value: 'normal', get label() { return t('shiftMode.normal'); }, get help() { return t('shiftMode.normal.help'); } },
  { value: 'eco', get label() { return t('shiftMode.eco'); }, get help() { return t('shiftMode.eco.help'); } },
  { value: 'sport', get label() { return t('shiftMode.sport'); }, get help() { return t('shiftMode.sport.help'); } },
  { value: 'track', get label() { return t('shiftMode.track'); }, get help() { return t('shiftMode.track.help'); } },
  { value: 'paddles', get label() { return t('shiftMode.paddles'); }, get help() { return t('shiftMode.paddles.help'); } },
];
export type DriveScenarioId = 'city' | 'stop_go' | 'country' | 'motorway' | 'mountain' | 'drag' | 'track' | 'hill_start' | 'towing';
/** Dans l'ordre de `DriveScenario` en C++ : la position est la valeur envoyée. Choix de conduite, hors du lien de partage. */
export const DRIVE_SCENARIOS: { value: DriveScenarioId; label: string; help: string }[] = [
  { value: 'city', get label() { return t('scenario.city'); }, get help() { return t('scenario.city.help'); } },
  { value: 'stop_go', get label() { return t('scenario.stop_go'); }, get help() { return t('scenario.stop_go.help'); } },
  { value: 'country', get label() { return t('scenario.country'); }, get help() { return t('scenario.country.help'); } },
  { value: 'motorway', get label() { return t('scenario.motorway'); }, get help() { return t('scenario.motorway.help'); } },
  { value: 'mountain', get label() { return t('scenario.mountain'); }, get help() { return t('scenario.mountain.help'); } },
  { value: 'drag', get label() { return t('scenario.drag'); }, get help() { return t('scenario.drag.help'); } },
  { value: 'track', get label() { return t('scenario.track'); }, get help() { return t('scenario.track.help'); } },
  { value: 'hill_start', get label() { return t('scenario.hill_start'); }, get help() { return t('scenario.hill_start.help'); } },
  { value: 'towing', get label() { return t('scenario.towing'); }, get help() { return t('scenario.towing.help'); } },
];
/** Dans l'ordre de `LegKind` en C++ : ce que renvoie la télémétrie. */
export const LEG_KINDS = ['stopped', 'pullAway', 'cityPace', 'trafficLight', 'expressway', 'cruise', 'overtake', 'stopping', 'queue', 'straight', 'bend', 'climb', 'hairpin', 'summit', 'descent', 'launch', 'heavyBraking', 'corner', 'flatOut', 'hillStart', 'slipRoad', 'exit'] as const;
/** De vraies boîtes, d'après leurs fiches : à monter telles quelles, même sur le mauvais moteur. */
export const BOX_MODELS: { id: string; label: string; note: string; settings: Partial<EngineConfig> }[] = [
  { id: 'psa_ma', get label() { return t('boxModel.psa_ma'); }, get note() { return t('boxModel.psa_ma.note'); },
    settings: { boxKind: 'synchro', boxGears: 5, boxFamily: 'custom', boxRatios: '3.417,1.81,1.276,0.975,0.767', boxFinal: 3.94, boxTeeth: 'helical', boxHelix: 0, boxModule: 0, boxLimit: 170, boxFlywheel: 'single' } },
  { id: 'honda_s2000', get label() { return t('boxModel.honda_s2000'); }, get note() { return t('boxModel.honda_s2000.note'); },
    settings: { boxKind: 'synchro', boxGears: 6, boxFamily: 'custom', boxRatios: '3.133,2.045,1.481,1.161,0.971,0.811', boxFinal: 4.76, boxTeeth: 'helical', boxHelix: 0, boxModule: 0, boxLimit: 300, boxFlywheel: 'single' } },
  { id: 'porsche_pdk', get label() { return t('boxModel.porsche_pdk'); }, get note() { return t('boxModel.porsche_pdk.note'); },
    settings: { boxKind: 'dual_clutch', boxGears: 7, boxFamily: 'custom', boxRatios: '3.75,2.38,1.72,1.34,1.11,0.96,0.84', boxFinal: 3.97, boxTeeth: 'helical', boxHelix: 0, boxModule: 0, boxLimit: 650, boxFlywheel: 'dual_mass' } },
  { id: 'zf_8', get label() { return t('boxModel.zf_8'); }, get note() { return t('boxModel.zf_8.note'); },
    settings: { boxKind: 'converter', boxGears: 8, boxFamily: 'custom', boxRatios: '4.71,3.14,2.11,1.67,1.29,1,0.84,0.67', boxFinal: 3.15, boxTeeth: 'helical', boxHelix: 0, boxModule: 0, boxLimit: 800, boxFlywheel: 'dual_mass' } },
  { id: 'quaife_vag', get label() { return t('boxModel.quaife_vag'); }, get note() { return t('boxModel.quaife_vag.note'); },
    settings: { boxKind: 'sequential', boxGears: 5, boxFamily: 'custom', boxRatios: '2.538,1.611,1.263,1,0.87', boxFinal: 3.526, boxTeeth: 'spur', boxHelix: 0, boxModule: 0, boxLimit: 450, boxFlywheel: 'single' } },
  { id: 'hewland_ftr', get label() { return t('boxModel.hewland_ftr'); }, get note() { return t('boxModel.hewland_ftr.note'); },
    settings: { boxKind: 'sequential', boxGears: 6, boxFamily: 'custom', boxRatios: '2.75,2,1.625,1.333,1.13,1', boxFinal: 3.444, boxTeeth: 'spur', boxHelix: 0, boxModule: 0, boxLimit: 325, boxFlywheel: 'single' } },
];
/** 14 et 16 : seulement en V et en W (`coherentConfig`). */
export const CYLINDERS = [2, 3, 4, 5, 6, 8, 10, 12, 14, 16];
export { ROTOR_COUNTS } from './rotary';
export const LIMITS = {
  displacementCc: DISPLACEMENT_LIMITS,
  boreMm: { min: 60, max: 110, step: 1 },
  /** La plage complète : `strokeRange` la resserre pour que la cylindrée reste dans ses limites. */
  strokeMm: STROKE_LIMITS,
  redlineRpm: { min: 3000, max: 12000, step: 100 },
  boostBar: { min: 0.3, max: 3.5, step: 0.1 },
  turboPipeMm: { min: 150, max: 3000, step: 50 },
  massKg: { min: 700, max: 3000, step: 25 },
  rimInch: { min: 14, max: 22, step: 1 },
  boxGears: { min: 2, max: 8, step: 1 },
  boxRatio: { min: 0.4, max: 6, step: 0.01 },
  boxFinal: { min: 1.5, max: 7, step: 0.01 },
  boxHelix: { min: 5, max: 40, step: 1 },
  boxModule: { min: 1.25, max: 5, step: 0.05 },
  boxLimit: { min: 100, max: 1200, step: 10 },
  // Les réglages du socle moteur. Ceux qui acceptent 0 (« d'origine » ou « sans ») le disent dans `TUNING_FIELDS`.
  ambientC: { min: -30, max: 50, step: 1 },
  altitudeM: { min: 0, max: 4500, step: 100 },
  compressionRatio: { min: 4, max: 24, step: 0.1 },
  ignitionOffsetDeg: { min: -10, max: 10, step: 1 },
  fullLoadLambda: { min: 0.7, max: 1.1, step: 0.01 },
  launchRpm: { min: 2000, max: 7000, step: 100 },
  camDurationDeg: { min: 180, max: 320, step: 2 },
  camLiftMm: { min: 5, max: 16, step: 0.1 },
  lobeSeparationDeg: { min: 100, max: 120, step: 1 },
  nitrousKw: { min: 0, max: 150, step: 5 },
};

const DEFAULTS: EngineConfig = {
  cylinders: 4,
  layout: 'inline',
  displacementCc: 2000,
  boreMm: 84,
  redlineRpm: 6800,
  exhaust: 'stock',
  aspiration: 'natural',
  fuel: 'petrol',
  multiplier: 0,
  pops: 'off',
  turboRelease: 'dump_valve',
  boostBar: 1,
  turboSize: 'medium',
  superchargerKind: 'roots',
  antiLag: 'off',
  turboPipeMm: 400,
  camshaft: 'dohc',
  airFilter: 'stock',
  fuelSystem: 'injection',
  ambientC: 20,
  altitudeM: 0,
  startTemp: 'warm',
  compressionRatio: 0,
  fuelGrade: 'ron95',
  ignitionOffsetDeg: 0,
  fullLoadLambda: 0,
  revLimiter: 'stock',
  launchRpm: 0,
  flatShift: false,
  tractionControl: 'off',
  dieselInjection: 'mechanical',
  egr: false,
  particulateFilter: false,
  camDurationDeg: 0,
  camLiftMm: 0,
  lobeSeparationDeg: 0,
  valveTiming: 'fixed',
  valveSprings: 'stock',
  turboLayout: 'single',
  variableGeometry: false,
  boostControl: 'spring',
  intercooler: 'air_to_air',
  waterInjection: false,
  nitrousKw: 0,
  cycle: 'four_stroke',
  electric: false,
  body: 'city',
  massKg: 1100,
  rimInch: 15,
  drive: 'front',
  boxKind: 'synchro',
  boxGears: 0,
  boxFamily: 'matched',
  boxRatios: '',
  boxFinal: 0,
  boxTeeth: 'helical',
  boxHelix: 0,
  boxModule: 0,
  boxLimit: 0,
  boxFlywheel: 'single',
  routing: 'single',
  placement: 'front',
  insulation: 'standard',
};

export const PRESETS: Preset[] = (presetData as Record<string, unknown>[]).map((entry) => {
  const { id, name, description, ...config } = entry;
  return {
    id: id as string,
    // L'anglais vient du fichier, le français du dictionnaire : relus à chaque affichage, ils suivent la langue.
    get name() { return presetText(id as string, 'name', name as string); },
    get description() { return presetText(id as string, 'description', description as string); },
    config: { ...DEFAULTS, ...(config as Partial<EngineConfig>) },
  };
});

export const CUSTOM_ID = 'custom';

export function sameConfig(a: EngineConfig, b: EngineConfig): boolean {
  const sameCar = a.body === b.body && a.massKg === b.massKg && a.rimInch === b.rimInch && a.drive === b.drive && a.placement === b.placement && a.insulation === b.insulation;
  if (a.electric || b.electric) return a.electric === b.electric && sameCar;
  return (Object.keys(DEFAULTS) as (keyof EngineConfig)[]).every((key) => a[key] === b[key]);
}

export function findPreset(config: EngineConfig): Preset | undefined {
  return PRESETS.find((preset) => sameConfig(preset.config, config));
}

export function cylinderCc(c: EngineConfig): number {
  return c.displacementCc / c.cylinders;
}

/** Déduite de la cylindrée d'un cylindre et de son alésage. */
export function strokeMm(c: EngineConfig): number {
  return engineStroke(c);
}

/** Le nombre le plus proche dans une liste, le plus grand à égalité. */
const nearest = (list: number[], value: number) => list.reduce((best, item) => (Math.abs(item - value) <= Math.abs(best - value) ? item : best), list[0]);

/**
 * Un rotatif n'a que 2, 3 ou 4 rotors et ne brûle que de l'essence ; un VR a 5 ou 6 cylindres, un W 8, 12 ou 16, et au-delà de
 * 12 seuls un V et un W existent. Un deux-temps balayé par son carter n'a ni diesel ni suralimentation, ni vilebrequin à plat
 * croisé, ni VR, ni W, et au plus 6 cylindres (`coherentTwoStroke` en C++). Tout ce qui change d'architecture ou lit un lien passe
 * par ici. La disposition choisie reste quand elle existe, le nombre de cylindres va au plus proche qui existe.
 */
export function coherentConfig(c: EngineConfig): EngineConfig {
  if (c.electric) return c;
  const engine = coherentEngine(c);
  // Deux turbos, un twincharger ou des aubes : l'anti-lag et le tube déporté ne sont modélisés que sur un turbo simple (C++ : `antiLagEngine_`).
  if (!usesTwinTurbo(engine) || (engine.antiLag === DEFAULTS.antiLag && engine.turboPipeMm === DEFAULTS.turboPipeMm)) return engine;
  return { ...engine, antiLag: DEFAULTS.antiLag, turboPipeMm: DEFAULTS.turboPipeMm };
}

/** Deux turbos, un twincharger ou des aubes : un autre modèle tient les arbres (recopie d'`usesTwinTurbo`, twin_turbo.hpp). */
export function usesTwinTurbo(c: Pick<EngineConfig, 'aspiration' | 'turboLayout' | 'variableGeometry'>): boolean {
  return c.aspiration === 'turbo' && (c.turboLayout !== 'single' || c.variableGeometry);
}

function coherentEngine(c: EngineConfig): EngineConfig {
  if (isTwoStroke(c)) {
    const cylinders = Math.min(c.cylinders, TWO_STROKE_MAX_CYLINDERS);
    const layout = c.layout === 'cross_plane' || c.layout === 'vr' || c.layout === 'w' ? 'v' : c.layout;
    if (cylinders === c.cylinders && layout === c.layout && c.fuel === 'petrol' && c.aspiration === 'natural') return c;
    return { ...c, cylinders, layout, fuel: 'petrol', aspiration: 'natural' };
  }
  if (!isRotary(c)) {
    const counts = c.layout === 'vr' ? VR_CYLINDERS : c.layout === 'w' ? W_CYLINDERS : c.layout === 'v' ? CYLINDERS : CYLINDERS.filter((n) => n <= 12);
    const cylinders = counts.includes(c.cylinders) ? c.cylinders : nearest(counts, c.cylinders);
    return cylinders === c.cylinders ? c : { ...c, cylinders };
  }
  const cylinders = ROTOR_COUNTS.includes(c.cylinders) ? c.cylinders : 2;
  if (cylinders === c.cylinders && c.fuel === 'petrol') return c;
  return { ...c, cylinders, fuel: 'petrol' };
}

/** Les dispositions qui existent à ce nombre de cylindres : quand on le choisit, une disposition qui n'y existe pas devient un V. */
export function layoutForCylinders(layout: Layout, cylinders: number): Layout {
  if (layout === 'rotary') return layout;
  return builtLayout(layout, cylinders);
}

const index = (list: { value: string }[], value: string) => list.findIndex((item) => item.value === value);

/** Arguments de rf_set_config, dans l'ordre du C. */
export function configArgs(c: EngineConfig): number[] {
  return [
    c.cylinders,
    index(LAYOUTS, c.layout),
    c.displacementCc,
    c.boreMm,
    c.redlineRpm,
    index(EXHAUSTS, c.exhaust),
    index(ASPIRATIONS, c.aspiration),
    index(FUELS, c.fuel),
    c.multiplier,
    index(POPS, c.pops),
    index(RELEASES, c.turboRelease),
    c.boostBar,
    index(ROUTINGS, c.routing),
    index(TURBO_SIZES, c.turboSize),
    index(SUPERCHARGER_KINDS, c.superchargerKind),
    index(ANTI_LAGS, c.antiLag),
    c.turboPipeMm,
    index(CAMSHAFTS, c.camshaft),
    index(AIR_FILTERS, c.airFilter),
    index(FUEL_SYSTEMS, c.fuelSystem),
  ];
}

// ── Les réglages du socle moteur : une seule table pour `rf_set_tuning` et le lien de partage ──

type TuningKey = 'ambientC' | 'altitudeM' | 'startTemp' | 'compressionRatio' | 'fuelGrade' | 'ignitionOffsetDeg' | 'fullLoadLambda' | 'revLimiter' | 'launchRpm' | 'flatShift' | 'tractionControl' | 'dieselInjection' | 'egr' | 'particulateFilter' | 'camDurationDeg' | 'camLiftMm' | 'lobeSeparationDeg' | 'valveTiming' | 'valveSprings' | 'turboLayout' | 'variableGeometry' | 'boostControl' | 'intercooler' | 'waterInjection' | 'nitrousKw' | 'cycle';
interface TuningField {
  key: TuningKey;
  /** Son nom dans le lien de partage. Ne change jamais : les liens déjà partagés en dépendent. */
  code: string;
  /** Une énumération : la position dans le tableau est la valeur envoyée au C++. */
  list?: { value: string }[];
  /** Un nombre : bornes du curseur, et du lien qui le relit. */
  limits?: { min: number; max: number; step: number };
  /** 0 veut dire « d'origine » ou « sans », même hors des bornes. */
  zero?: boolean;
}

/** Dans l'ordre des arguments de `rf_set_tuning` (wasm/exports.cpp) et des champs ajoutés à la fin d'`EngineConfig`. */
export const TUNING_FIELDS: readonly TuningField[] = [
  { key: 'ambientC', code: 'amb', limits: LIMITS.ambientC },
  { key: 'altitudeM', code: 'alt', limits: LIMITS.altitudeM },
  { key: 'startTemp', code: 'start', list: START_TEMPS },
  { key: 'compressionRatio', code: 'cr', limits: LIMITS.compressionRatio, zero: true },
  { key: 'fuelGrade', code: 'grade', list: FUEL_GRADES },
  { key: 'ignitionOffsetDeg', code: 'adv', limits: LIMITS.ignitionOffsetDeg },
  { key: 'fullLoadLambda', code: 'lambda', limits: LIMITS.fullLoadLambda, zero: true },
  { key: 'revLimiter', code: 'lim', list: REV_LIMITERS },
  { key: 'launchRpm', code: 'launch', limits: LIMITS.launchRpm, zero: true },
  { key: 'flatShift', code: 'flat' },
  { key: 'tractionControl', code: 'tc', list: TRACTION_CONTROLS },
  { key: 'dieselInjection', code: 'dinj', list: DIESEL_INJECTIONS },
  { key: 'egr', code: 'egr' },
  { key: 'particulateFilter', code: 'dpf' },
  { key: 'camDurationDeg', code: 'dur', limits: LIMITS.camDurationDeg, zero: true },
  { key: 'camLiftMm', code: 'lift', limits: LIMITS.camLiftMm, zero: true },
  { key: 'lobeSeparationDeg', code: 'lsa', limits: LIMITS.lobeSeparationDeg, zero: true },
  { key: 'valveTiming', code: 'vvt', list: VALVE_TIMINGS },
  { key: 'valveSprings', code: 'springs', list: VALVE_SPRINGS },
  { key: 'turboLayout', code: 'turbos', list: TURBO_LAYOUTS },
  { key: 'variableGeometry', code: 'vgt' },
  { key: 'boostControl', code: 'bc', list: BOOST_CONTROLS },
  { key: 'intercooler', code: 'ic', list: INTERCOOLERS },
  { key: 'waterInjection', code: 'water' },
  { key: 'nitrousKw', code: 'nos', limits: LIMITS.nitrousKw },
  { key: 'cycle', code: 'cycle', list: ENGINE_CYCLES },
];

/** Arguments de rf_set_tuning, dans l'ordre du C : à envoyer juste avant rf_set_config ou rf_set_electric, qui les appliquent. */
export function tuningArgs(c: EngineConfig): number[] {
  return TUNING_FIELDS.map((field) => {
    const value = c[field.key];
    if (field.list) return index(field.list, value as string);
    if (typeof value === 'boolean') return value ? 1 : 0;
    return value as number;
  });
}

/** Arguments de rf_set_car, dans l'ordre du C. */
export function carArgs(c: EngineConfig): number[] {
  return [index(BODIES, c.body), c.massKg, c.rimInch, index(DRIVES, c.drive), index(PLACEMENTS, c.placement), index(INSULATIONS, c.insulation)];
}

/** Arguments de rf_set_gearbox, dans l'ordre du C. */
export function gearboxArgs(c: EngineConfig): number[] {
  return [
    index(BOX_KINDS, c.boxKind),
    c.boxGears,
    index(BOX_FAMILIES, c.boxFamily),
    c.boxFinal,
    index(BOX_TEETH, c.boxTeeth),
    c.boxHelix,
    c.boxModule,
    c.boxLimit,
    index(BOX_FLYWHEELS, c.boxFlywheel),
  ];
}

/** Le rapport de chaque vitesse demandé, en famille sur mesure. */
export function gearRatios(c: EngineConfig): number[] {
  return c.boxRatios ? c.boxRatios.split(',').map(Number).filter(Number.isFinite) : [];
}

// ── Lien de partage : le moteur tient dans l'adresse ──

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

// Les champs ajoutés suivent après un « _ » : la pression, avant eux, peut contenir un point. Toujours à la fin, pour que les anciens liens restent valides.
function encodeCar(c: EngineConfig): string {
  return `${c.body}.${c.massKg}.${c.rimInch}.${c.drive}.${c.boostBar}_${c.placement}_${c.insulation}_${c.routing}_${c.turboSize}_${c.superchargerKind}_${c.antiLag}_${c.turboPipeMm}_${c.camshaft}_${c.airFilter}_${c.fuelSystem}`;
}

export function encodeConfig(c: EngineConfig): string {
  // Les réglages du socle, s'il y en a, viennent après la boîte : un groupe vide la remplace si elle est celle par défaut.
  const tuning = encodeTuning(c);
  if (c.electric) return `electric~${encodeCar(c)}${tuning ? `~~${tuning}` : ''}`;
  const box = encodeBox(c);
  return [c.cylinders, c.layout, c.displacementCc, c.boreMm, c.redlineRpm, c.exhaust, c.aspiration, c.fuel, c.multiplier, c.pops, c.turboRelease].join('.') + `~${encodeCar(c)}${tuning ? `${box || '~'}~${tuning}` : box}`;
}

/**
 * Les réglages du socle, après un troisième `~` : seulement ceux qui diffèrent du défaut, en `code=valeur` séparés par des
 * virgules (`amb=35,grade=e85,flat=1`). Rien du tout si tous sont au défaut : le lien d'un moteur d'avant ne change pas.
 * Un code inconnu ou une valeur hors liste est ignoré : le lien reste lisible si un réglage s'ajoute ou disparaît.
 */
function encodeTuning(c: EngineConfig): string {
  return TUNING_FIELDS.filter((field) => c[field.key] !== DEFAULTS[field.key])
    .map((field) => {
      const value = c[field.key];
      return `${field.code}=${typeof value === 'boolean' ? (value ? 1 : 0) : value}`;
    })
    .join(',');
}

function decodeTuning(text: string | undefined): Partial<EngineConfig> {
  if (!text) return {};
  const read: Partial<Record<TuningKey, string | number | boolean>> = {};
  for (const pair of text.split(',')) {
    const [code, value = ''] = pair.split('=');
    const field = TUNING_FIELDS.find((item) => item.code === code);
    if (!field) continue;
    if (field.list) {
      if (field.list.some((item) => item.value === value)) read[field.key] = value;
    } else if (typeof DEFAULTS[field.key] === 'boolean') {
      if (value === '0' || value === '1') read[field.key] = value === '1';
    } else {
      const n = Number(value);
      if (value === '' || !Number.isFinite(n)) continue;
      read[field.key] = field.zero && n === 0 ? 0 : clamp(n, field.limits!.min, field.limits!.max);
    }
  }
  return read as Partial<EngineConfig>;
}

/** La boîte, après un second `~` : rien si elle est celle par défaut, et les anciens liens n'en ont pas. */
function encodeBox(c: EngineConfig): string {
  const parts = [c.boxKind, c.boxGears, c.boxFamily, c.boxFinal, c.boxTeeth, c.boxHelix, c.boxModule, c.boxLimit, c.boxFlywheel, c.boxRatios.replaceAll(',', '_')];
  const text = parts.join(',');
  const standard = [DEFAULTS.boxKind, DEFAULTS.boxGears, DEFAULTS.boxFamily, DEFAULTS.boxFinal, DEFAULTS.boxTeeth, DEFAULTS.boxHelix, DEFAULTS.boxModule, DEFAULTS.boxLimit, DEFAULTS.boxFlywheel, ''].join(',');
  return text === standard ? '' : `~${text}`;
}

function decodeBox(text: string | undefined): Partial<EngineConfig> {
  if (!text) return {};
  const [kind, gears, family, final, teeth, helix, moduleMm, limit, flywheel, ratios = ''] = text.split(',');
  const pickFrom = <T extends string>(list: { value: T }[], value: string, fallback: T): T => list.find((item) => item.value === value)?.value ?? fallback;
  const numberOr = (value: string, low: number, high: number) => {
    const n = Number(value);
    return Number.isFinite(n) && n > 0 ? clamp(n, low, high) : 0;
  };
  return {
    boxKind: pickFrom(BOX_KINDS, kind, DEFAULTS.boxKind),
    boxGears: numberOr(gears, LIMITS.boxGears.min, LIMITS.boxGears.max),
    boxFamily: pickFrom(BOX_FAMILIES, family, DEFAULTS.boxFamily),
    boxFinal: numberOr(final, LIMITS.boxFinal.min, LIMITS.boxFinal.max),
    boxTeeth: pickFrom(BOX_TEETH, teeth, DEFAULTS.boxTeeth),
    boxHelix: numberOr(helix, LIMITS.boxHelix.min, LIMITS.boxHelix.max),
    boxModule: numberOr(moduleMm, LIMITS.boxModule.min, LIMITS.boxModule.max),
    boxLimit: numberOr(limit, LIMITS.boxLimit.min, LIMITS.boxLimit.max),
    boxFlywheel: pickFrom(BOX_FLYWHEELS, flywheel, DEFAULTS.boxFlywheel),
    boxRatios: ratios
      .split('_')
      .map(Number)
      .filter((ratio) => Number.isFinite(ratio) && ratio > 0)
      .map((ratio) => clamp(ratio, LIMITS.boxRatio.min, LIMITS.boxRatio.max))
      .join(','),
  };
}

function decodeCar(text: string | undefined): Partial<EngineConfig> {
  // Les anciens liens n'ont pas de voiture : on garde celle par défaut.
  if (!text) return {};
  // « twin_screw » contient le séparateur : on le met à part le temps de couper. Le format du lien ne change pas.
  const [main, placement, insulation, routing, turboSize, chargerField, antiLag, turboPipe, camshaft, airFilter, fuelSystem] = text.replace('_twin_screw', '_twin-screw').split('_');
  const superchargerKind = chargerField?.replace('-', '_');
  // La pression peut contenir un point : tout ce qui suit la transmission lui appartient.
  const [body, mass, rim, drive, ...pressure] = main.split('.');
  const bar = Number(pressure.join('.'));
  const chosenBody = BODIES.find((item) => item.value === body)?.value;
  const chosenDrive = DRIVES.find((item) => item.value === drive)?.value;
  if (!chosenBody || !chosenDrive || !Number.isFinite(Number(mass)) || !Number.isFinite(Number(rim))) return {};
  return {
    body: chosenBody,
    drive: chosenDrive,
    massKg: clamp(Number(mass), LIMITS.massKg.min, LIMITS.massKg.max),
    rimInch: clamp(Number(rim), LIMITS.rimInch.min, LIMITS.rimInch.max),
    ...(pressure.length && Number.isFinite(bar) ? { boostBar: clamp(bar, LIMITS.boostBar.min, LIMITS.boostBar.max) } : {}),
    // Les anciens liens n'ont rien de tout cela : la carrosserie dit où est le moteur, le reste garde sa valeur.
    placement: PLACEMENTS.find((item) => item.value === placement)?.value ?? BODIES.find((item) => item.value === body)!.placement,
    ...(INSULATIONS.some((item) => item.value === insulation) ? { insulation: insulation as Insulation } : {}),
    ...(ROUTINGS.some((item) => item.value === routing) ? { routing: routing as Routing } : {}),
    ...(TURBO_SIZES.some((item) => item.value === turboSize) ? { turboSize: turboSize as TurboSize } : {}),
    ...(SUPERCHARGER_KINDS.some((item) => item.value === superchargerKind) ? { superchargerKind: superchargerKind as SuperchargerKind } : {}),
    ...(ANTI_LAGS.some((item) => item.value === antiLag) ? { antiLag: antiLag as AntiLag } : {}),
    ...(turboPipe !== undefined && Number.isFinite(Number(turboPipe)) ? { turboPipeMm: clamp(Number(turboPipe), LIMITS.turboPipeMm.min, LIMITS.turboPipeMm.max) } : {}),
    ...(CAMSHAFTS.some((item) => item.value === camshaft) ? { camshaft: camshaft as Camshaft } : {}),
    ...(AIR_FILTERS.some((item) => item.value === airFilter) ? { airFilter: airFilter as AirFilter } : {}),
    ...(FUEL_SYSTEMS.some((item) => item.value === fuelSystem) ? { fuelSystem: fuelSystem as FuelSystem } : {}),
  };
}

export function decodeConfig(link: string): EngineConfig | null {
  const [text, carText, boxText, tuningText] = link.split('~');
  const car = { ...decodeCar(carText), ...decodeBox(boxText), ...decodeTuning(tuningText) };
  if (text === 'electric') return { ...DEFAULTS, electric: true, ...car };
  // Le multiplicateur peut contenir un point : on lit depuis les deux bouts.
  const parts = text.split('.');
  if (parts.length < 11) return null;
  const [cylinders, layout, cc, bore, redline, exhaust, aspiration, fuel] = parts;
  const pops = parts[parts.length - 2];
  const release = parts[parts.length - 1];
  const multiplier = Number(parts.slice(8, parts.length - 2).join('.'));
  const known = <T extends string>(list: { value: T }[], value: string): T | null =>
    list.find((item) => item.value === value)?.value ?? null;
  const parsed = {
    layout: known(LAYOUTS, layout),
    exhaust: known(EXHAUSTS, exhaust),
    aspiration: known(ASPIRATIONS, aspiration),
    fuel: known(FUELS, fuel),
    pops: known(POPS, pops),
    turboRelease: known(RELEASES, release),
  };
  if (Object.values(parsed).some((value) => value === null)) return null;
  if (!CYLINDERS.includes(Number(cylinders))) return null;
  const numbers = [Number(cc), Number(bore), Number(redline), multiplier];
  if (numbers.some((value) => !Number.isFinite(value))) return null;
  return coherentConfig({
    ...DEFAULTS,
    ...car,
    ...(parsed as Pick<EngineConfig, 'layout' | 'exhaust' | 'aspiration' | 'fuel' | 'pops' | 'turboRelease'>),
    cylinders: Number(cylinders),
    displacementCc: clamp(numbers[0], LIMITS.displacementCc.min, LIMITS.displacementCc.max),
    boreMm: clamp(numbers[1], LIMITS.boreMm.min, LIMITS.boreMm.max),
    redlineRpm: clamp(numbers[2], LIMITS.redlineRpm.min, LIMITS.redlineRpm.max),
    multiplier: 0,
  });
}
