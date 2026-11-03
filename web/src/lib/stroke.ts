// La course n'est jamais stockée : cylindrée et alésage restent les réglages, et le lien de partage ne change pas.
// Ce module fait le lien entre les trois, sans DOM ni runes, pour être testé sans navigateur.

/** Ce dont la course se déduit : un `EngineConfig` convient. */
export interface EngineShape {
  displacementCc: number;
  boreMm: number;
  cylinders: number;
}

export const DISPLACEMENT_LIMITS = { min: 600, max: 7000, step: 50 };

/**
 * De 40 mm (un V8 de Formule 1 de 2,4 L, 98 mm d'alésage) à 130 mm (une Bugatti Royale, 125 × 130).
 * Le pas de 0,5 mm garde l'aller-retour exact : sur le plus petit cylindre, 2 × 60 mm d'alésage, 1 mm vaut 5,65 cm³,
 * donc arrondir la cylindrée à l'entier déplace la course d'au plus 0,09 mm, moins que le demi-pas. Il tombe aussi juste en binaire.
 */
export const STROKE_LIMITS = { min: 40, max: 130, step: 0.5 };

/** `kPistonSpeedLimit` en C++ (m/s) : au-delà, le moteur simulé respire moins bien. */
export const PISTON_SPEED_LIMIT = 24;

/** Plus près du carré que ces 2 %, un moteur est dit carré : un usage, pas une norme. */
const SQUARE_WITHIN = 0.02;

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

/** La cylindrée exacte de cette course, en cm³ : π/4 × alésage² × course × cylindres. */
function sweptCc(boreMm: number, cylinders: number, strokeMm: number): number {
  const boreCm = boreMm / 10;
  return ((Math.PI / 4) * boreCm * boreCm * strokeMm * cylinders) / 10;
}

/** La course d'un moteur (mm), telle que le C++ la déduit (`EngineConfig::strokeMm`). */
export function engineStroke(e: EngineShape): number {
  const boreCm = e.boreMm / 10;
  return ((e.displacementCc / e.cylinders) / ((Math.PI / 4) * boreCm * boreCm)) * 10;
}

/** La plage du curseur pour cet alésage et ces cylindres : la cylindrée n'en sort pas. Toujours sur la grille du pas. */
export function strokeRange(boreMm: number, cylinders: number): { min: number; max: number; step: number } {
  const { step } = STROKE_LIMITS;
  const perMm = sweptCc(boreMm, cylinders, 1);
  return {
    min: Math.max(STROKE_LIMITS.min, Math.ceil(DISPLACEMENT_LIMITS.min / perMm / step) * step),
    max: Math.min(STROKE_LIMITS.max, Math.floor(DISPLACEMENT_LIMITS.max / perMm / step) * step),
    step,
  };
}

/** La cylindrée (cm³ entiers, dans ses limites) pour cette course. `displacementForStroke` en C++, sans les limites. */
export function displacementFor(boreMm: number, cylinders: number, strokeMm: number): number {
  return clamp(Math.round(sweptCc(boreMm, cylinders, strokeMm)), DISPLACEMENT_LIMITS.min, DISPLACEMENT_LIMITS.max);
}

/** Où poser le curseur : le cran le plus proche, dans la plage. Ne touche à aucun réglage. */
export function strokeThumb(e: EngineShape): number {
  const range = strokeRange(e.boreMm, e.cylinders);
  return clamp(Math.round(engineStroke(e) / range.step) * range.step, range.min, range.max);
}

/** Vrai si un préréglage, un lien ou les autres curseurs mènent là où celui de la course ne va pas. */
export function strokeOutside(e: EngineShape): boolean {
  const { min, max } = STROKE_LIMITS;
  return e.displacementCc < Math.round(sweptCc(e.boreMm, e.cylinders, min)) || e.displacementCc > Math.round(sweptCc(e.boreMm, e.cylinders, max));
}

/** Le moteur tel qu'il était quand on a pris le curseur : y revenir le rend exactement, 5204 cm³ compris. */
export interface StrokeAnchor {
  displacementCc: number;
  boreMm: number;
  cylinders: number;
  /** Le cran de départ. Hors plage, il n'y en a pas : revenir à une butée ne doit pas faire bondir la course. */
  thumb: number | null;
  /** La dernière cylindrée écrite par le curseur : si elle a changé depuis, quelqu'un d'autre a réglé le moteur. */
  written: number;
}

/**
 * Le curseur est posé sur `targetMm` : l'alésage reste, la cylindrée s'ajuste.
 * Pur et idempotent : le glissé puis le relâchement, sur le même cran, donnent la même cylindrée.
 */
export function moveStroke(e: EngineShape, targetMm: number, anchor: StrokeAnchor | null): { displacementCc: number; anchor: StrokeAnchor } {
  const held = anchor && anchor.boreMm === e.boreMm && anchor.cylinders === e.cylinders && anchor.written === e.displacementCc;
  const from: StrokeAnchor = held
    ? anchor
    : { displacementCc: e.displacementCc, boreMm: e.boreMm, cylinders: e.cylinders, thumb: strokeOutside(e) ? null : strokeThumb(e), written: e.displacementCc };
  const range = strokeRange(e.boreMm, e.cylinders);
  const target = clamp(targetMm, range.min, range.max);
  const displacementCc = target === from.thumb ? from.displacementCc : displacementFor(e.boreMm, e.cylinders, target);
  return { displacementCc, anchor: { ...from, written: displacementCc } };
}

export type StrokeShape = 'over' | 'square' | 'under';

/** Super-carré : l'alésage dépasse la course. Longue course : l'inverse. */
export function strokeShape(boreMm: number, strokeMm: number): StrokeShape {
  const ratio = boreMm / strokeMm;
  if (Math.abs(ratio - 1) <= SQUARE_WITHIN) return 'square';
  return ratio > 1 ? 'over' : 'under';
}

/** Vitesse moyenne du piston (m/s) : il parcourt deux courses par tour. */
export function pistonSpeed(strokeMm: number, rpm: number): number {
  return ((2 * strokeMm) / 1000) * (rpm / 60);
}
