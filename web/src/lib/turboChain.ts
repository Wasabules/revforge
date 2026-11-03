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
  /** Déporté : le tube d'échappement du collecteur à la volute, et le tube chaud par le bas du moteur. Vides à la référence ou plus court. */
  feedRun: P3T[];
  hotRun: P3T[];
  /** Le turbo vu de côté : `z` et `compressorZ` avec le long du tube replié, pour que le moteur reste lisible. */
  sideZ: number;
  sideCompressorZ: number;
}

export interface TurboChain {
  units: TurboUnit[];
  /**
   * L'échangeur : centre et demi-tailles. Air-eau, une brique plus petite au même endroit ; sans échangeur, `hasCooler` est faux :
   * la place reste celle où le turbo se dessine dans la coupe de côté, mais rien ne s'y dessine, et les tubes se rejoignent devant le moteur.
   */
  cooler: P3T;
  coolerSize: P3T;
  hasCooler: boolean;
  /** Le tube froid : du haut de l'échangeur (ou de son côté) jusqu'au papillon. */
  coldFrom: P3T;
  coldFromDir: P3T;
  coldTo: P3T;
  /** Plus bas et plus large que le moteur seul : ce qu'il faut pour cadrer une vue. */
  xMax: number;
  yMin: number;
  yMax: number;
  zMax: number;
  /** Le bout de la coupe de côté, tube replié compris. */
  sideZMin: number;
  /** Le tube, en plus de la référence de 400 mm (mm), réel ; et vrai s'il est replié dans la coupe de côté. */
  extraMm: number;
  sideFolded: boolean;
}

/** Une courbe de Catmull-Rom par ces points : un tube qui ne dépend pas de la distance entre ses deux bouts. */
function smoothRun(points: P3T[], steps = 6): P3T[] {
  const kept = points.filter((p, i) => i === 0 || Math.hypot(p[0] - points[i - 1][0], p[1] - points[i - 1][1], p[2] - points[i - 1][2]) > 1);
  const run: P3T[] = [];
  for (let i = 0; i < kept.length - 1; i++) {
    const a = kept[Math.max(i - 1, 0)];
    const b = kept[i];
    const c = kept[i + 1];
    const d = kept[Math.min(i + 2, kept.length - 1)];
    for (let k = i === 0 ? 0 : 1; k <= steps; k++) {
      const u = k / steps;
      const point: P3T = [0, 0, 0];
      for (let n = 0; n < 3; n++) {
        point[n] = 0.5 * (2 * b[n] + (c[n] - a[n]) * u + (2 * a[n] - 5 * b[n] + 4 * c[n] - d[n]) * u * u + (3 * b[n] - a[n] - 3 * c[n] + d[n]) * u * u * u);
      }
      run.push(point);
    }
  }
  return run;
}

export function turboChain(layout: EngineLayout): TurboChain {
  const { bore, deck } = layout;
  const s = layout.turboScale;
  const plenum = plenumPosition(layout);
  const zFrontAll = Math.max(...layout.cylinders.map((c) => c.z)) + bore * 0.5;
  const throttleZ = zFrontAll + bore * 0.5;
  const coolerY = -layout.ring.radius - bore * 0.32;
  const coolerZ = layout.pulleyZ + bore * 0.95;
  // Une brique air-eau : moins de face, plus d'épaisseur, autant de volume que le calcul en garde (`ChargePath`, 0,25 m sur 0,45).
  const water = layout.intercooler === 'air_to_water';
  const hasCooler = layout.intercooler !== 'none';
  const coolerHalfX = bore * (layout.banks === 2 ? 2.2 : 1.9) * (water ? 0.45 : 1);
  const coolerHalfY = bore * 0.52 * (water ? 0.8 : 1);
  const coolerHalfZ = bore * 0.22 * (water ? 1.4 : 1);
  // Sans échangeur, le tube chaud rejoint le tube froid devant le moteur, à mi-hauteur : le chemin le plus court vers le papillon.
  const junctionY = coolerY + bore * 0.52 + bore * 1.2;
  const units: TurboUnit[] = [];
  let xMax = coolerHalfX + bore * 0.2;
  let yMax = 0;
  let yLow = coolerY - coolerHalfY - bore * 0.1;
  let sideZMin = Infinity;
  // Le tube en vraie grandeur. Plus court, le turbo remonte contre le collecteur. Plus long, il recule d'abord au-delà du volant, à sa
  // hauteur, sans rien traverser ; puis il descend sous le niveau de l'échangeur, et tout le reste de la longueur part vers l'arrière.
  const extra = (layout.turboPipeMm ?? 400) - 400;
  // Dans la coupe de côté, trois mètres écraseraient les cylindres : au-delà de deux alésages et demi, le tube se replie, et le dit.
  const sideRoom = bore * 2.5;
  for (let bank = 0; bank < layout.banks; bank++) {
    const cylinders = layout.byBank[bank];
    const collector = collectorPosition(layout, bank);
    const side = Math.sign(collector.x || 1);
    const zRear = cylinders[cylinders.length - 1].z - bore * 0.45;
    const x = collector.x + side * bore * 0.2;
    const y0 = collector.y - bore * 0.55;
    const z0 = zRear + bore * 0.5;
    // Assez loin pour que la boîte à air, le plus en avant de l'ensemble, ait passé le volant.
    const clear = z0 + bore * 2.57 - (layout.ring.z - bore * 0.35);
    const ahead = Math.max(extra, 0);
    const firstBack = Math.min(ahead, clear);
    const down = Math.min(ahead - firstBack, Math.max(y0 - coolerY, 0));
    const back = ahead - down;
    const lift = extra < 0 ? bore * 0.25 * (-extra / 250) : 0;
    const y = y0 - down + lift;
    const z = z0 - back;
    const shownBack = back <= sideRoom ? back : sideRoom + bore * 1.5 * (1 - Math.exp(-(back - sideRoom) / 1000));
    const compressorHousing = bore * 0.5 * s;
    const compressorZ = z + bore * 0.9;
    const airboxSize: P3T = [bore * 0.46, bore * 0.34, bore * 0.72];
    // Au-dessus du turbo ; sur un moteur à plat, au-dessus de la culasse, là où il y a de la place.
    const airbox: P3T = layout.flat ? [side * bore * 1.5, bore * 2.9 - down + lift, compressorZ + bore * 0.95] : [x + side * bore * 0.42, collector.y + bore * 2.4 - down + lift, compressorZ + bore * 0.95];
    xMax = Math.max(xMax, Math.abs(x) + bore * 0.95, Math.abs(airbox[0]) + airboxSize[0]);
    yMax = Math.max(yMax, airbox[1] + airboxSize[1]);
    yLow = Math.min(yLow, y - bore * 1.15);
    const hotFrom: P3T = [x + side * compressorHousing * 0.6, y - compressorHousing * 1.0, compressorZ];
    const hotTo: P3T = hasCooler ? [side * coolerHalfX, coolerY - coolerHalfY * 0.35, coolerZ] : [side * bore * 0.3, junctionY, coolerZ];
    const turbineHousing = bore * 0.46 * s;
    let feedRun: P3T[] = [];
    let hotRun: P3T[] = [];
    if (extra > 0) {
      // Du collecteur, il longe le moteur côté intérieur, puis tombe sur le dessus de la volute : le compresseur est juste devant.
      const feedX = x - side * bore * 0.45;
      const high = Math.max(collector.y - bore * 0.1, y + turbineHousing + bore * 0.3);
      feedRun = smoothRun([
        [collector.x, collector.y - bore * 0.05, z0],
        [feedX, high, z0 - Math.min(back * 0.4, bore * 0.5)],
        [feedX, high, z + bore * 0.1],
        [x - side * bore * 0.2, y + turbineHousing * 0.7, z],
      ]);
      // Le tube chaud descend sous le niveau de l'échangeur, et remonte tout le moteur par le bas jusqu'à lui.
      const under = hotTo[1];
      const reach = coolerZ - bore;
      const along = hotFrom[2] + bore * 1.2;
      hotRun = smoothRun([
        hotFrom,
        [hotFrom[0], Math.max(hotFrom[1] - bore * 0.5, under), hotFrom[2] + bore * 0.25],
        ...(along < reach - bore * 0.2 ? [[hotFrom[0], under, along] as P3T] : []),
        [hotFrom[0], under, reach],
        [hotTo[0] + side * bore * 0.35, hotTo[1], coolerZ - bore * 0.2],
        hotTo,
      ]);
    }
    const sideZ = z0 - shownBack;
    sideZMin = Math.min(sideZMin, sideZ - bore * 0.7);
    units.push({
      bank,
      side,
      x,
      y,
      z,
      compressorZ,
      turbineHousing,
      compressorHousing,
      turbineWheel: bore * 0.26 * s,
      compressorWheel: bore * 0.32 * s,
      airbox,
      airboxSize,
      hotFrom,
      hotTo,
      actuator: [x + side * bore * 0.68 * s, y + bore * 0.2, z + bore * 0.4],
      feedRun,
      hotRun,
      sideZ,
      sideCompressorZ: sideZ + bore * 0.9,
    });
  }
  // Sans turbo d'un côté, le tube froid part de ce côté libre. Sur deux rangées, il part du milieu et monte devant le moteur.
  const oneBank = layout.banks === 1;
  const freeSide = oneBank ? -units[0].side : 0;
  let coldFrom: P3T = oneBank ? [freeSide * coolerHalfX, coolerY + coolerHalfY * 0.35, coolerZ] : [0, coolerY + coolerHalfY, coolerZ];
  let coldFromDir: P3T = oneBank ? [freeSide, 0, 0] : [0, 1, 0];
  if (!hasCooler) {
    coldFrom = oneBank ? [units[0].side * bore * 0.3, junctionY, coolerZ] : [0, junctionY, coolerZ];
    coldFromDir = [0, 1, 0];
  }
  return {
    units,
    cooler: [0, coolerY, coolerZ],
    coolerSize: [coolerHalfX, coolerHalfY, coolerHalfZ],
    hasCooler,
    coldFrom,
    coldFromDir,
    coldTo: [plenum.x, plenum.y, throttleZ + bore * 0.3],
    xMax,
    yMin: yLow,
    yMax: Math.max(yMax, deck * 0),
    zMax: coolerZ + coolerHalfZ + bore * 0.15,
    sideZMin,
    extraMm: extra,
    sideFolded: extra > sideRoom,
  };
}
