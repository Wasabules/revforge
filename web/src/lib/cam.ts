/**
 * La came, pour le dessin et le gaz à l'écran : recopie de `makeCamProfile`, `camProfileOf` et `highCamProfileOf`
 * (core/src/cam.cpp). Une erreur ici ne change que ce qu'on voit ; `came.spec.ts` compare ces calages à ceux du C++.
 *
 * Les angles sont en degrés du cycle de 720°, 0 au point mort haut d'allumage, lus à 1 mm de levée. Le son et le gaz à
 * l'écran n'ont pas tout à fait le même calage d'origine (`kEvo`… et `EVO`…, à 10° près) : on leur ajoute à chacun ce que la
 * came change, si bien qu'à la came d'origine ils restent exactement ce qu'ils étaient.
 */
import type { EngineConfig } from './config';
import { isRotary } from './rotary';

export const CAM_STOCK_DURATION_DEG = 240;
export const CAM_STOCK_LSA_DEG = 110;
export const CAM_REFERENCE_LIFT_MM = 1;
export const CAM_TWO_STEP_SPREAD_DEG = 25;
export const CAM_TWO_STEP_LIFT_SPREAD = 0.15;
/** Ce que le déphaseur peut décaler de part et d'autre du calage de base. Recopie `kCamPhaserAuthorityDeg`. */
export const CAM_PHASER_AUTHORITY_DEG = 20;
const HALF_SPAN_MAX_DEG = 210;
const LIFT_PER_BORE = 0.12;

export interface CamProfile {
  durationDeg: number;
  liftMm: number;
  lobeSeparationDeg: number;
  intakeCenterDeg: number;
  exhaustCenterDeg: number;
  halfSpanDeg: number;
  ivo: number;
  ivc: number;
  evo: number;
  evc: number;
}

/** (1 − x²)²(1 − x⁴) : 1 au nez, nulle aux sièges avec sa vitesse et son accélération. */
export function camLiftShare(x: number): number {
  if (!(x > -1 && x < 1)) return 0;
  const x2 = x * x;
  const u = 1 - x2;
  return u * u * (1 - x2 * x2);
}

function shareAt(share: number): number {
  let low = 0;
  let high = 1;
  for (let i = 0; i < 60; i++) {
    const middle = 0.5 * (low + high);
    if (camLiftShare(middle) > share) low = middle;
    else high = middle;
  }
  return 0.5 * (low + high);
}

const clamp = (v: number, low: number, high: number) => (v < low ? low : v > high ? high : v);

export function makeCamProfile(durationDeg: number, liftMm: number, lobeSeparationDeg: number, intakeCenterDeg: number): CamProfile {
  const reference = shareAt(clamp(CAM_REFERENCE_LIFT_MM / liftMm, 0, 0.99));
  const halfSpanDeg = Math.min((0.5 * durationDeg) / reference, HALF_SPAN_MAX_DEG);
  const duration = Math.min(durationDeg, 2 * halfSpanDeg * reference);
  const exhaustCenterDeg = 2 * lobeSeparationDeg - intakeCenterDeg;
  return {
    durationDeg: duration,
    liftMm,
    lobeSeparationDeg,
    intakeCenterDeg,
    exhaustCenterDeg,
    halfSpanDeg,
    ivo: 360 + intakeCenterDeg - duration / 2,
    ivc: 360 + intakeCenterDeg + duration / 2,
    evo: 360 - exhaustCenterDeg - duration / 2,
    evc: 360 - exhaustCenterDeg + duration / 2,
  };
}

type CamConfig = Pick<EngineConfig, 'layout' | 'boreMm' | 'camDurationDeg' | 'camLiftMm' | 'lobeSeparationDeg' | 'valveTiming' | 'cycle'>;

/** Ni un rotatif, ni un deux-temps n'ont de soupapes. */
export function camHasValves(config: CamConfig): boolean {
  return !isRotary(config) && config.cycle !== 'two_stroke';
}

export function camIsStock(config: CamConfig): boolean {
  if (!camHasValves(config)) return true;
  return !config.camDurationDeg && !config.camLiftMm && !config.lobeSeparationDeg && (config.valveTiming ?? 'fixed') === 'fixed';
}

export function camTwoStep(config: CamConfig): boolean {
  return camHasValves(config) && (config.valveTiming === 'two_step' || config.valveTiming === 'phased_two_step');
}

export function camPhased(config: CamConfig): boolean {
  return camHasValves(config) && (config.valveTiming === 'phased' || config.valveTiming === 'phased_two_step');
}

export function stockCamProfile(config: CamConfig): CamProfile {
  return makeCamProfile(CAM_STOCK_DURATION_DEG, LIFT_PER_BORE * config.boreMm, CAM_STOCK_LSA_DEG, CAM_STOCK_LSA_DEG);
}

function chosenCamProfile(config: CamConfig): CamProfile {
  const duration = config.camDurationDeg > 0 ? config.camDurationDeg : CAM_STOCK_DURATION_DEG;
  const lift = config.camLiftMm > 0 ? config.camLiftMm : LIFT_PER_BORE * config.boreMm;
  const separation = config.lobeSeparationDeg > 0 ? config.lobeSeparationDeg : CAM_STOCK_LSA_DEG;
  return makeCamProfile(duration, lift, separation, separation);
}

function spreadCamProfile(config: CamConfig, side: number): CamProfile {
  const chosen = chosenCamProfile(config);
  return makeCamProfile(
    clamp(chosen.durationDeg + side * CAM_TWO_STEP_SPREAD_DEG, 180, 320),
    clamp(chosen.liftMm * (1 + side * CAM_TWO_STEP_LIFT_SPREAD), 5, 16),
    chosen.lobeSeparationDeg,
    chosen.intakeCenterDeg,
  );
}

/** Le profil de base : celui du bas sur une came à deux profils. */
export function camProfileOf(config: CamConfig): CamProfile {
  return camTwoStep(config) ? spreadCamProfile(config, -1) : chosenCamProfile(config);
}

export function highCamProfileOf(config: CamConfig): CamProfile {
  return camTwoStep(config) ? spreadCamProfile(config, 1) : chosenCamProfile(config);
}

/** Ce que la came change au calage d'origine, en degrés : 0 partout sur la came d'origine. `phaserDeg` avance l'admission. */
export interface CamShift {
  evo: number;
  evc: number;
  ivo: number;
  ivc: number;
  /** Levée de cette came sur celle d'origine : le dessin en suit la hauteur. */
  lift: number;
}

export const NO_CAM_SHIFT: CamShift = { evo: 0, evc: 0, ivo: 0, ivc: 0, lift: 1 };

export function camShift(config: CamConfig, high = false, phaserDeg = 0): CamShift {
  if (camIsStock(config)) return NO_CAM_SHIFT;
  const stock = stockCamProfile(config);
  const cam = high ? highCamProfileOf(config) : camProfileOf(config);
  return {
    evo: cam.evo - stock.evo,
    evc: cam.evc - stock.evc,
    ivo: cam.ivo - stock.ivo - phaserDeg,
    ivc: cam.ivc - stock.ivc - phaserDeg,
    lift: cam.liftMm / stock.liftMm,
  };
}

/** Ce qui change les pièces à l'écran : à clé égale, la même came. */
export function camKey(config: CamConfig): string {
  if (camIsStock(config)) return '';
  return `${config.camDurationDeg}|${config.camLiftMm}|${config.lobeSeparationDeg}|${config.valveTiming}`;
}
