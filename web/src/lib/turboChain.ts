/**
 * Où est chaque pièce du chemin de l'air d'un turbo, en millimètres, dans le repère du moteur (z vers l'avant).
 * Calculé une fois par moteur, pour les vues 2D et 3D : elles dessinent la même chose.
 *
 * Un turbo par rangée sur un V ou un boxer, comme le collecteur : chacun a son arbre, sa boîte à air et son tube chaud.
 * Ils partagent un seul échangeur, à l'avant et en bas, et le tube froid remonte de là au papillon.
 */
import { collectorPosition, plenumPosition, type EngineLayout } from './engineLayout';

export type P3T = [number, number, number];

export interface TurboUnit {
  bank: number;
  /** Côté de l'échappement : +1 ou -1 en x. */
  side: number;
  /** Centre de la roue de turbine ; le compresseur est à `compressorZ` sur le même arbre. */
  x: number;
  y: number;
  z: number;
  compressorZ: number;
  /** Rayons des carters et des roues (mm). */
  turbineHousing: number;
  compressorHousing: number;
  turbineWheel: number;
  compressorWheel: number;
  /** Boîte à air : centre et demi-tailles. */
  airbox: P3T;
  airboxSize: P3T;
  /** Le tube chaud : sa sortie sur la volute, son arrivée sur l'échangeur. */
  hotFrom: P3T;
  hotTo: P3T;
  /** L'actionneur de la wastegate, et son axe sur le carter. */
  actuator: P3T;
}

export interface TurboChain {
  units: TurboUnit[];
  /** L'échangeur : centre et demi-tailles. */
  cooler: P3T;
  coolerSize: P3T;
  /** Le tube froid : du haut de l'échangeur (ou de son côté) jusqu'au papillon. */
  coldFrom: P3T;
  coldFromDir: P3T;
  coldTo: P3T;
  /** Plus bas et plus large que le moteur seul : ce qu'il faut pour cadrer une vue. */
  xMax: number;
  yMin: number;
  yMax: number;
  zMax: number;
}

export function turboChain(layout: EngineLayout): TurboChain {
  const { bore, deck } = layout;
  const s = layout.turboScale;
  const plenum = plenumPosition(layout);
  const zFrontAll = Math.max(...layout.cylinders.map((c) => c.z)) + bore * 0.5;
  const throttleZ = zFrontAll + bore * 0.5;
  const coolerY = -layout.ring.radius - bore * 0.32;
  const coolerZ = layout.pulleyZ + bore * 0.95;
  const coolerHalfX = bore * (layout.banks === 2 ? 2.2 : 1.9);
  const coolerHalfY = bore * 0.52;
  const coolerHalfZ = bore * 0.22;
  const units: TurboUnit[] = [];
  let xMax = coolerHalfX + bore * 0.2;
  let yMax = 0;
  for (let bank = 0; bank < layout.banks; bank++) {
    const cylinders = layout.byBank[bank];
    const collector = collectorPosition(layout, bank);
    const side = Math.sign(collector.x || 1);
    const zRear = cylinders[cylinders.length - 1].z - bore * 0.45;
    const x = collector.x + side * bore * 0.2;
    const y = collector.y - bore * 0.55;
    const z = zRear + bore * 0.5;
    const compressorHousing = bore * 0.5 * s;
    const compressorZ = z + bore * 0.9;
    const airboxSize: P3T = [bore * 0.46, bore * 0.34, bore * 0.72];
    // Au-dessus du turbo ; sur un moteur à plat, au-dessus de la culasse, là où il y a de la place.
    const airbox: P3T = layout.flat ? [side * bore * 1.5, bore * 2.9, compressorZ + bore * 0.95] : [x + side * bore * 0.42, collector.y + bore * 2.4, compressorZ + bore * 0.95];
    xMax = Math.max(xMax, Math.abs(x) + bore * 0.95, Math.abs(airbox[0]) + airboxSize[0]);
    yMax = Math.max(yMax, airbox[1] + airboxSize[1]);
    units.push({
      bank,
      side,
      x,
      y,
      z,
      compressorZ,
      turbineHousing: bore * 0.46 * s,
      compressorHousing,
      turbineWheel: bore * 0.26 * s,
      compressorWheel: bore * 0.32 * s,
      airbox,
      airboxSize,
      hotFrom: [x + side * compressorHousing * 0.6, y - compressorHousing * 1.0, compressorZ],
      hotTo: [side * coolerHalfX, coolerY - coolerHalfY * 0.35, coolerZ],
      actuator: [x + side * bore * 0.68 * s, y + bore * 0.2, z + bore * 0.4],
    });
  }
  // Sans turbo d'un côté, le tube froid part de ce côté libre. Sur deux rangées, il part du milieu et monte devant le moteur.
  const oneBank = layout.banks === 1;
  const freeSide = oneBank ? -units[0].side : 0;
  const coldFrom: P3T = oneBank ? [freeSide * coolerHalfX, coolerY + coolerHalfY * 0.35, coolerZ] : [0, coolerY + coolerHalfY, coolerZ];
  const coldFromDir: P3T = oneBank ? [freeSide, 0, 0] : [0, 1, 0];
  return {
    units,
    cooler: [0, coolerY, coolerZ],
    coolerSize: [coolerHalfX, coolerHalfY, coolerHalfZ],
    coldFrom,
    coldFromDir,
    coldTo: [plenum.x, plenum.y, throttleZ + bore * 0.3],
    xMax,
    yMin: coolerY - coolerHalfY - bore * 0.1,
    yMax: Math.max(yMax, deck * 0),
    zMax: coolerZ + coolerHalfZ + bore * 0.15,
  };
}
