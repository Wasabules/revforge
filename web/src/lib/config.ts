import presetData from '../../../presets/presets.json';
import { presetText, t } from './i18n.svelte';

export type Layout = 'inline' | 'v' | 'cross_plane' | 'boxer';
export type Exhaust = 'stock' | 'sport' | 'straight' | 'no_muffler' | 'no_resonator' | 'quiet';
export type Routing = 'single' | 'dual' | 'h' | 'x';
export type Placement = 'front' | 'mid' | 'rear';
export type Insulation = 'stripped' | 'light' | 'standard' | 'comfort' | 'luxury';
export type Aspiration = 'natural' | 'turbo';
export type Fuel = 'petrol' | 'diesel';
export type Pops = 'off' | 'light' | 'strong';
export type Release = 'dump_valve' | 'flutter';
export type TurboSize = 'small' | 'medium' | 'large';
export type Body = 'city' | 'compact' | 'coupe' | 'supercar' | 'van';
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
];
export const EXHAUSTS: { value: Exhaust; label: string; pops: string }[] = [
  { value: 'stock', get label() { return t('exhaust.stock'); }, get pops() { return t('exhaust.stock.pops'); } },
  { value: 'sport', get label() { return t('exhaust.sport'); }, get pops() { return t('exhaust.sport.pops'); } },
  { value: 'straight', get label() { return t('exhaust.straight'); }, get pops() { return t('exhaust.straight.pops'); } },
  { value: 'no_muffler', get label() { return t('exhaust.no_muffler'); }, get pops() { return t('exhaust.no_muffler.pops'); } },
  { value: 'no_resonator', get label() { return t('exhaust.no_resonator'); }, get pops() { return t('exhaust.no_resonator.pops'); } },
  { value: 'quiet', get label() { return t('exhaust.quiet'); }, get pops() { return t('exhaust.quiet.pops'); } },
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
/** Choisir une carrosserie règle aussi une masse, des jantes et une transmission plausibles. */
export const BODIES: { value: Body; label: string; massKg: number; rimInch: number; drive: Drive; placement: Placement }[] = [
  { value: 'city', get label() { return t('body.city'); }, massKg: 1100, rimInch: 15, drive: 'front', placement: 'front' },
  { value: 'compact', get label() { return t('body.compact'); }, massKg: 1350, rimInch: 18, drive: 'front', placement: 'front' },
  { value: 'coupe', get label() { return t('body.coupe'); }, massKg: 1600, rimInch: 19, drive: 'rear', placement: 'front' },
  { value: 'supercar', get label() { return t('body.supercar'); }, massKg: 1500, rimInch: 20, drive: 'all', placement: 'mid' },
  { value: 'van', get label() { return t('body.van'); }, massKg: 1950, rimInch: 16, drive: 'front', placement: 'front' },
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
export const CYLINDERS = [2, 3, 4, 5, 6, 8, 10, 12];
export const LIMITS = {
  displacementCc: { min: 600, max: 7000, step: 50 },
  boreMm: { min: 60, max: 110, step: 1 },
  redlineRpm: { min: 3000, max: 12000, step: 100 },
  boostBar: { min: 0.3, max: 3.5, step: 0.1 },
  massKg: { min: 700, max: 3000, step: 25 },
  rimInch: { min: 14, max: 22, step: 1 },
  boxGears: { min: 2, max: 8, step: 1 },
  boxRatio: { min: 0.4, max: 6, step: 0.01 },
  boxFinal: { min: 1.5, max: 7, step: 0.01 },
  boxHelix: { min: 5, max: 40, step: 1 },
  boxModule: { min: 1.25, max: 5, step: 0.05 },
  boxLimit: { min: 100, max: 1200, step: 10 },
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
  const boreCm = c.boreMm / 10;
  return (cylinderCc(c) / ((Math.PI / 4) * boreCm * boreCm)) * 10;
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
  ];
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
  return `${c.body}.${c.massKg}.${c.rimInch}.${c.drive}.${c.boostBar}_${c.placement}_${c.insulation}_${c.routing}_${c.turboSize}`;
}

export function encodeConfig(c: EngineConfig): string {
  if (c.electric) return `electric~${encodeCar(c)}`;
  return [c.cylinders, c.layout, c.displacementCc, c.boreMm, c.redlineRpm, c.exhaust, c.aspiration, c.fuel, c.multiplier, c.pops, c.turboRelease].join('.') + `~${encodeCar(c)}${encodeBox(c)}`;
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
  const [main, placement, insulation, routing, turboSize] = text.split('_');
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
  };
}

export function decodeConfig(link: string): EngineConfig | null {
  const [text, carText, boxText] = link.split('~');
  const car = { ...decodeCar(carText), ...decodeBox(boxText) };
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
  return {
    ...DEFAULTS,
    ...car,
    ...(parsed as Pick<EngineConfig, 'layout' | 'exhaust' | 'aspiration' | 'fuel' | 'pops' | 'turboRelease'>),
    cylinders: Number(cylinders),
    displacementCc: clamp(numbers[0], LIMITS.displacementCc.min, LIMITS.displacementCc.max),
    boreMm: clamp(numbers[1], LIMITS.boreMm.min, LIMITS.boreMm.max),
    redlineRpm: clamp(numbers[2], LIMITS.redlineRpm.min, LIMITS.redlineRpm.max),
    multiplier: 0,
  };
}
