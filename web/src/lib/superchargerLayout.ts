/**
 * Où est le compresseur mécanique, en millimètres, dans le repère du moteur (z vers l'avant). Calculé une fois par moteur,
 * pour les vues 2D et 3D : elles dessinent la même chose.
 *
 * Un volumétrique (Roots, à vis) est posé sur le collecteur d'admission, ses rotors dans l'axe du vilebrequin. Un centrifuge
 * est sur un coin du bloc, sa roue tournée vers l'avant. Dans les deux cas, une courroie part de la poulie du vilebrequin.
 */
import type { SuperchargerKind } from './config';
import { plenumPosition, type EngineLayout } from './engineLayout';

export type P3C = [number, number, number];

export interface ChargerUnit {
  kind: SuperchargerKind;
  volumetric: boolean;
  /** Centre du carter. */
  x: number;
  y: number;
  z: number;
  /** Demi-tailles du carter d'un volumétrique ; rayon de la volute d'un centrifuge dans `halfX`. */
  halfX: number;
  halfY: number;
  halfZ: number;
  /** Rayon extérieur d'un rotor, et demi-écart entre les deux axes. */
  rotorRadius: number;
  rotorSpacing: number;
  /** Lobes de chaque rotor. Le second tourne en sens inverse, à proportion de ses lobes. */
  lobesA: number;
  lobesB: number;
  /** Combien l'hélice tourne sur la longueur du rotor (radians). */
  twist: number;
  /** La roue du centrifuge. */
  wheelRadius: number;
  /** La poulie sur l'arbre, et le plan de la courroie. */
  pulleyX: number;
  pulleyY: number;
  pulleyRadius: number;
  beltZ: number;
  /** Poulie du vilebrequin : la même que celle de la courroie des accessoires. */
  crankPulley: number;
  /** Boîte à air : centre et demi-tailles. */
  airbox: P3C;
  airboxSize: P3C;
  /** Du carter au papillon : l'air comprimé, chauffé par le compresseur. */
  outFrom: P3C;
  outTo: P3C;
  /** Ce qu'il faut pour cadrer une vue. */
  xMax: number;
  yMax: number;
  zMax: number;
}

/** Le contour d'un rotor à lobes, dans le sens trigonométrique, centré sur son axe. */
export function lobedOutline(lobes: number, radius: number, points = 48): [number, number][] {
  const out: [number, number][] = [];
  for (let k = 0; k < points; k++) {
    const a = (k / points) * Math.PI * 2;
    const r = radius * (0.7 + 0.3 * Math.cos(lobes * a));
    out.push([Math.cos(a) * r, Math.sin(a) * r]);
  }
  return out;
}

export function chargerUnit(layout: EngineLayout, kind: SuperchargerKind): ChargerUnit {
  const { bore, deck } = layout;
  const plenum = plenumPosition(layout);
  const zFront = Math.max(...layout.cylinders.map((c) => c.z));
  const zRear = Math.min(...layout.cylinders.map((c) => c.z));
  const beltZ = layout.pulleyZ + bore * 0.24;
  const crankPulley = bore * 0.72;
  const throttleZ = zFront + bore;

  if (kind === 'centrifugal') {
    // Sur un coin du bloc, du côté de l'admission, au plus court de la poulie du vilebrequin.
    const side = plenum.x !== 0 ? Math.sign(plenum.x) : 1;
    const R = bore * 0.62;
    const x = side * Math.max(Math.abs(plenum.x) * 0.85, bore * 1.9);
    const y = Math.max(deck * 0.55, bore * 0.8);
    const z = layout.pulleyZ - bore * 0.5;
    const airbox: P3C = [x + side * bore * 0.5, y + R + bore * 0.65, z - bore * 0.2];
    const airboxSize: P3C = [bore * 0.42, bore * 0.3, bore * 0.6];
    return {
      kind,
      volumetric: false,
      x,
      y,
      z,
      halfX: R,
      halfY: R,
      halfZ: bore * 0.3,
      rotorRadius: 0,
      rotorSpacing: 0,
      lobesA: 0,
      lobesB: 0,
      twist: 0,
      wheelRadius: bore * 0.44,
      pulleyX: x,
      pulleyY: y,
      pulleyRadius: bore * 0.2,
      beltZ,
      crankPulley,
      airbox,
      airboxSize,
      outFrom: [x - side * R * 0.7, y + R * 0.7, z],
      outTo: [plenum.x, plenum.y, throttleZ],
      xMax: Math.abs(x) + R + bore * 0.5,
      yMax: Math.max(airbox[1] + airboxSize[1], y + R),
      zMax: beltZ + bore * 0.2,
    };
  }

  const screw = kind === 'twin_screw';
  const halfX = bore * 0.62;
  const halfY = bore * 0.42;
  const halfZ = Math.min(((zFront - zRear) / 2 + bore * 0.5) * 0.8, bore * (screw ? 1.6 : 1.4));
  const x = plenum.x;
  const y = plenum.y + plenum.radius + halfY * 0.85;
  const z = (zFront + zRear) / 2;
  const airboxSize: P3C = [bore * 0.42, bore * 0.26, bore * 0.6];
  const airbox: P3C = [x, y + halfY + airboxSize[1] + bore * 0.2, z - halfZ * 0.5];
  return {
    kind,
    volumetric: true,
    x,
    y,
    z,
    halfX,
    halfY,
    halfZ,
    rotorRadius: bore * 0.3,
    rotorSpacing: bore * 0.27,
    lobesA: 3,
    lobesB: screw ? 5 : 3,
    // Un Roots moderne : 60° d'hélice. Une vis : près d'un demi-tour.
    twist: screw ? 2.8 : 1.05,
    wheelRadius: 0,
    pulleyX: x,
    pulleyY: y,
    pulleyRadius: bore * 0.26,
    beltZ,
    crankPulley,
    airbox,
    airboxSize,
    outFrom: [x, y - halfY, z],
    outTo: [plenum.x, plenum.y, z],
    xMax: Math.abs(x) + halfX + bore * 0.1,
    yMax: airbox[1] + airboxSize[1],
    zMax: beltZ + bore * 0.2,
  };
}
