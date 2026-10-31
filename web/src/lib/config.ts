import presetData from '../../../presets/presets.json';

export type Layout = 'inline' | 'v' | 'cross_plane' | 'boxer';
export type Exhaust = 'stock' | 'sport' | 'straight';
export type Aspiration = 'natural' | 'turbo';
export type Fuel = 'petrol' | 'diesel';
export type Pops = 'off' | 'light' | 'strong';
export type Release = 'dump_valve' | 'flutter';

export interface EngineConfig {
  cylinders: number;
  layout: Layout;
  displacementCc: number;
  boreMm: number;
  redlineRpm: number;
  exhaust: Exhaust;
  aspiration: Aspiration;
  fuel: Fuel;
  /** 0 : la plage de la voiture couvre celle du moteur. */
  multiplier: number;
  pops: Pops;
  turboRelease: Release;
  electric: boolean;
}

export interface Preset {
  id: string;
  name: string;
  description: string;
  config: EngineConfig;
}

export const LAYOUTS: { value: Layout; label: string }[] = [
  { value: 'inline', label: 'En ligne' },
  { value: 'v', label: 'En V' },
  { value: 'cross_plane', label: 'V cross-plane' },
  { value: 'boxer', label: 'Boxer' },
];
export const EXHAUSTS: { value: Exhaust; label: string; pops: string }[] = [
  { value: 'stock', label: "D'origine", pops: 'Rares et sourdes, retenues par le silencieux.' },
  { value: 'sport', label: 'Sport', pops: 'Franches, avec du corps.' },
  { value: 'straight', label: 'Ligne directe', pops: 'Violentes et sèches, rien ne les retient.' },
];
export const ASPIRATIONS: { value: Aspiration; label: string }[] = [
  { value: 'natural', label: 'Atmosphérique' },
  { value: 'turbo', label: 'Turbo' },
];
export const FUELS: { value: Fuel; label: string }[] = [
  { value: 'petrol', label: 'Essence' },
  { value: 'diesel', label: 'Diesel' },
];
export const POPS: { value: Pops; label: string }[] = [
  { value: 'off', label: 'Aucun' },
  { value: 'light', label: 'Léger' },
  { value: 'strong', label: 'Fort' },
];
export const RELEASES: { value: Release; label: string }[] = [
  { value: 'dump_valve', label: 'Dump valve' },
  { value: 'flutter', label: 'Flutter' },
];
export const CYLINDERS = [2, 3, 4, 5, 6, 8, 10, 12];
export const LIMITS = {
  displacementCc: { min: 600, max: 7000, step: 50 },
  boreMm: { min: 60, max: 110, step: 1 },
  redlineRpm: { min: 3000, max: 12000, step: 100 },
  multiplier: { min: 0.5, max: 4, step: 0.05 },
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
  electric: false,
};

export const PRESETS: Preset[] = (presetData as Record<string, unknown>[]).map((entry) => {
  const { id, name, description, ...config } = entry;
  return {
    id: id as string,
    name: name as string,
    description: description as string,
    config: { ...DEFAULTS, ...(config as Partial<EngineConfig>) },
  };
});

export const CUSTOM_ID = 'custom';

export function sameConfig(a: EngineConfig, b: EngineConfig): boolean {
  if (a.electric || b.electric) return a.electric === b.electric;
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
  ];
}

// ── Lien de partage : le moteur tient dans l'adresse ──

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

export function encodeConfig(c: EngineConfig): string {
  if (c.electric) return 'electric';
  return [c.cylinders, c.layout, c.displacementCc, c.boreMm, c.redlineRpm, c.exhaust, c.aspiration, c.fuel, c.multiplier, c.pops, c.turboRelease].join('.');
}

export function decodeConfig(text: string): EngineConfig | null {
  if (text === 'electric') return { ...DEFAULTS, electric: true };
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
    ...(parsed as Pick<EngineConfig, 'layout' | 'exhaust' | 'aspiration' | 'fuel' | 'pops' | 'turboRelease'>),
    cylinders: Number(cylinders),
    displacementCc: clamp(numbers[0], LIMITS.displacementCc.min, LIMITS.displacementCc.max),
    boreMm: clamp(numbers[1], LIMITS.boreMm.min, LIMITS.boreMm.max),
    redlineRpm: clamp(numbers[2], LIMITS.redlineRpm.min, LIMITS.redlineRpm.max),
    multiplier: numbers[3] === 0 ? 0 : clamp(numbers[3], LIMITS.multiplier.min, LIMITS.multiplier.max),
  };
}
