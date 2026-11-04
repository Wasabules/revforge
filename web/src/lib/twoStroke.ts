/**
 * Le deux-temps : sa géométrie et sa cinématique, du calcul pur, sans DOM ni three.js ni texte. Le gaz, les vues et le dessin de la
 * voiture lisent la même chose, et les tests le vérifient sans navigateur. Recopie de `core/src/two_stroke.cpp` : une erreur ici
 * change ce qu'on voit, jamais le son (voir « Le deux-temps » dans docs/modele.md).
 *
 * Les angles se comptent en degrés de vilebrequin depuis le point mort haut d'explosion ; une explosion par tour et par cylindre.
 */
import type { EngineConfig } from './config';

/** Vrai sur un deux-temps : des lumières au lieu des soupapes, un carter qui pompe. Un rotatif garde son propre cycle. */
export const isTwoStroke = (config: Pick<EngineConfig, 'layout'> & { cycle?: EngineConfig['cycle'] }): boolean =>
  config.cycle === 'two_stroke' && config.layout !== 'rotary';

/** Le nombre de cylindres d'un deux-temps : chacun allume deux fois en deux tours, et le son en compte au plus 12. */
export const TWO_STROKE_MAX_CYLINDERS = 6;

// ── Recopies de core/src/two_stroke.cpp ──
const EXHAUST_HEIGHT_TOURING = 0.28;
const EXHAUST_HEIGHT_RACING = 0.42;
const TRANSFER_HEIGHT_TOURING = 0.17;
const TRANSFER_HEIGHT_RACING = 0.24;
const INLET_TRAVEL_TOURING = 0.36;
const INLET_TRAVEL_RACING = 0.48;
const TOURING_RPM = 4500;
const RACING_SPAN_RPM = 5000;
/** Bielle de deux courses. */
export const ROD_OVER_STROKE = 2;
/** Largeurs des lumières, en part de l'alésage. */
export const EXHAUST_WIDTH = 0.62;
export const TRANSFER_WIDTH = 0.95;
export const INLET_WIDTH = 0.6;
/** Taux géométrique et taux du carter. */
export const GEOMETRIC_COMPRESSION = 7.5;
export const CRANKCASE_COMPRESSION = 1.35;
/** L'huile mélangée à l'essence : 1 volume pour 50 (`kPremixRatio`). */
export const PREMIX_RATIO = 50;
/** Le gaz de la chambre de détente, et ses proportions (`kChamberGasK`…). */
const CHAMBER_GAS_K = 650;
const HEADER_OVER_PORT = 1.1;
const BELLY_OVER_HEADER = 2.75;
const STINGER_OVER_HEADER = 0.6;
const STINGER_LENGTH_OVER_DIAMETER = 12;
const HEADER_SHARE = 0.27;
const DIFFUSER_SHARE = 0.38;
const BELLY_SHARE = 0.17;
const BAFFLE_SHARE = 0.36;
const TUNED_SHARE = 0.88;
const VALVE_OPEN_DEG = 240;
const VALVE_PULSE_DEG = 20;
const VALVE_RETURN_DEG = 30;
const VALVE_AREA_SHARE = 0.19;
/** Derrière la pointe : un silencieux garni de dix fois sa section, puis une sortie (`kChamberSilencerM`…). */
export const CHAMBER_SILENCER_M = 0.4;
export const CHAMBER_SILENCER_AREA = 10;
export const CHAMBER_TAIL_M = 0.5;
export const CHAMBER_TAIL_AREA = 1.3;

const DEG = Math.PI / 180;
const clamp = (v: number, low: number, high: number) => (v < low ? low : v > high ? high : v);

/** La course du piston depuis le point mort haut, en part de la course, à cet angle de vilebrequin (°). */
export function pistonTravel(crankDeg: number, rodOverStroke = ROD_OVER_STROKE): number {
  const a = crankDeg * DEG;
  const s = Math.sin(a);
  const lever = 1 / (2 * rodOverStroke);
  return 0.5 * (1 - Math.cos(a)) + rodOverStroke * (1 - Math.sqrt(1 - lever * lever * s * s));
}

/** L'angle (0 à 180°) où le piston qui descend a parcouru cette part de la course. */
export function crankAngleForTravel(share: number, rodOverStroke = ROD_OVER_STROKE): number {
  let low = 0;
  let high = 180;
  for (let i = 0; i < 48; i++) {
    const middle = 0.5 * (low + high);
    if (pistonTravel(middle, rodOverStroke) < share) low = middle;
    else high = middle;
  }
  return 0.5 * (low + high);
}

export interface TwoStrokePorts {
  /** Hauteurs, en part de la course depuis le haut du piston au point mort bas ; l'admission, la course restant jusqu'au haut. */
  exhaustHeight: number;
  transferHeight: number;
  inletTravel: number;
  /** Ouvertures (° après le point mort haut) ; l'admission, avant lui. Les fermetures en sont symétriques. */
  exhaustOpen: number;
  transferOpen: number;
  inletOpenBefore: number;
  trappedCompression: number;
}

/** Les lumières de ce moteur : plus hautes sur un moteur qui tourne vite (`twoStrokePortsOf`). */
export function twoStrokePorts(config: Pick<EngineConfig, 'redlineRpm'>): TwoStrokePorts {
  const k = clamp((config.redlineRpm - TOURING_RPM) / RACING_SPAN_RPM, 0, 1);
  const exhaustHeight = EXHAUST_HEIGHT_TOURING + (EXHAUST_HEIGHT_RACING - EXHAUST_HEIGHT_TOURING) * k;
  const transferHeight = TRANSFER_HEIGHT_TOURING + (TRANSFER_HEIGHT_RACING - TRANSFER_HEIGHT_TOURING) * k;
  const inletTravel = INLET_TRAVEL_TOURING + (INLET_TRAVEL_RACING - INLET_TRAVEL_TOURING) * k;
  return {
    exhaustHeight,
    transferHeight,
    inletTravel,
    exhaustOpen: crankAngleForTravel(1 - exhaustHeight),
    transferOpen: crankAngleForTravel(1 - transferHeight),
    inletOpenBefore: crankAngleForTravel(inletTravel),
    trappedCompression: 1 + (1 - exhaustHeight) * (GEOMETRIC_COMPRESSION - 1),
  };
}

/** La chambre de détente d'un cylindre (m) : recopie d'`expansionChamberFor`. */
export interface ExpansionChamber {
  header: number;
  diffuser: number;
  belly: number;
  baffle: number;
  stinger: number;
  headerDiameter: number;
  bellyDiameter: number;
  stingerDiameter: number;
  soundMs: number;
  tunedRpm: number;
}

export const chamberSoundMs = (): number => Math.sqrt(1.35 * 287 * CHAMBER_GAS_K);

export function expansionChamber(config: Pick<EngineConfig, 'layout' | 'cylinders' | 'displacementCc' | 'boreMm' | 'redlineRpm'> & { cycle?: EngineConfig['cycle'] }): ExpansionChamber {
  const bore = config.boreMm / 1000;
  const twoStroke = isTwoStroke(config);
  const cylinders = Math.max(twoStroke ? Math.min(config.cylinders, TWO_STROKE_MAX_CYLINDERS) : config.cylinders, 1);
  const stroke = (config.displacementCc * 1e-6) / cylinders / ((Math.PI / 4) * bore * bore);
  let portArea = VALVE_AREA_SHARE * (Math.PI / 4) * bore * bore;
  let travelDeg = VALVE_OPEN_DEG - VALVE_PULSE_DEG - VALVE_RETURN_DEG;
  if (twoStroke) {
    const ports = twoStrokePorts(config);
    portArea = EXHAUST_WIDTH * bore * ports.exhaustHeight * stroke;
    const halfOpen = 360 - crankAngleForTravel(1 - 0.5 * ports.exhaustHeight);
    travelDeg = halfOpen - ports.transferOpen;
  }
  const header = HEADER_OVER_PORT * Math.sqrt((4 * portArea) / Math.PI);
  const soundMs = chamberSoundMs();
  const tunedRpm = TUNED_SHARE * config.redlineRpm;
  const length = (soundMs * travelDeg) / (12 * tunedRpm);
  return {
    header: HEADER_SHARE * length,
    diffuser: DIFFUSER_SHARE * length,
    belly: BELLY_SHARE * length,
    baffle: BAFFLE_SHARE * length,
    stinger: STINGER_LENGTH_OVER_DIAMETER * STINGER_OVER_HEADER * header,
    headerDiameter: header,
    bellyDiameter: BELLY_OVER_HEADER * header,
    stingerDiameter: STINGER_OVER_HEADER * header,
    soundMs,
    tunedRpm,
  };
}

/** Une pièce de la chambre, pour le dessin et le gaz à l'écran : longueur (m), section rapportée au tube de base, nature. */
export type ChamberPart = 'pipe' | 'diffuser' | 'belly' | 'baffle' | 'stinger' | 'muffler';

/**
 * La ligne d'une chambre de détente, une par cylindre en parallèle (leurs sections s'additionnent), rapportée au tube de base de ce
 * moteur (`baseArea`, en m²). Les cônes se coupent en `diffuserSteps` et `baffleSteps` marches : le son en compte 8 et 6, le gaz à
 * l'écran moins, ses cellules faisant 3 cm.
 */
export function expansionLine(config: Parameters<typeof expansionChamber>[0], baseArea: number, diffuserSteps = 4, baffleSteps = 3): { length: number; area: number; part: ChamberPart }[] {
  const c = expansionChamber(config);
  const count = isTwoStroke(config) ? Math.min(config.cylinders, TWO_STROKE_MAX_CYLINDERS) : config.cylinders;
  const share = (diameter: number) => (count * Math.PI * diameter * diameter) / 4 / baseArea;
  const parts: { length: number; area: number; part: ChamberPart }[] = [{ length: c.header, area: share(c.headerDiameter), part: 'pipe' }];
  for (let k = 0; k < diffuserSteps; k++) {
    const d = c.headerDiameter + ((c.bellyDiameter - c.headerDiameter) * (k + 0.5)) / diffuserSteps;
    parts.push({ length: c.diffuser / diffuserSteps, area: share(d), part: 'diffuser' });
  }
  parts.push({ length: c.belly, area: share(c.bellyDiameter), part: 'belly' });
  for (let k = 0; k < baffleSteps; k++) {
    const d = c.bellyDiameter + ((c.stingerDiameter - c.bellyDiameter) * (k + 0.5)) / baffleSteps;
    parts.push({ length: c.baffle / baffleSteps, area: share(d), part: 'baffle' });
  }
  parts.push({ length: c.stinger, area: share(c.stingerDiameter), part: 'stinger' });
  parts.push({ length: CHAMBER_SILENCER_M, area: share(c.stingerDiameter) * CHAMBER_SILENCER_AREA, part: 'muffler' });
  parts.push({ length: CHAMBER_TAIL_M, area: share(c.stingerDiameter) * CHAMBER_TAIL_AREA, part: 'pipe' });
  return parts;
}

/** Le tube de base d'un moteur (m²) : 50 mm pour 2 litres balayés en deux tours, comme `ExhaustLine` et le gaz. */
export function baseLineArea(sweptLitres: number): number {
  const diameter = Math.min(Math.max(0.05 * Math.sqrt(sweptLitres / 2), 0.032), 0.09);
  return (Math.PI * diameter * diameter) / 4;
}
