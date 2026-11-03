/**
 * La mécanique du moteur dessiné : où est chaque cylindre, à quel angle est chaque maneton, et où en est
 * chaque pièce à un instant donné. Du calcul pur, sans DOM ni three.js : les vues 2D et 3D lisent le même `EnginePose`,
 * et les tests le vérifient sans navigateur.
 *
 * Tout part de l'ordre d'allumage, celui que suit le gaz (`FIRING_ORDERS`) : on ne choisit pas l'angle des manetons,
 * on le déduit. Le cylindre au rang s de l'ordre passe au point mort haut d'allumage à l'angle 720·s/N. Vu de face,
 * le vilebrequin tourne dans le sens des aiguilles d'une montre, et les angles se comptent depuis le haut dans ce sens.
 * Un maneton d'angle φ est donc à φ+θ quand le moteur est à θ ; il est au point mort haut d'un cylindre incliné
 * de β quand φ+θ = β. D'où φ = β − 720·s/N, modulo 360.
 *
 * Ce calcul donne une vraie manivelle à condition de placer chaque numéro sur la bonne rangée : voir `placementOf`.
 */
import type { EngineConfig } from './config';
import { TURBO_SCALE } from './exhaustLines';
import { EVC, EVO, ignitionAdvance, IVC, IVO } from './fluidGas';
import { firingOrder } from './motion';
import { valveSizes } from './valves';

export const DEG = Math.PI / 180;

/** Fin de l'injection d'essence : directe, pendant l'admission, bien avant la compression. */
const PETROL_INJECTION_DEG = IVO + 40;
/** Module des dents de couronne (mm) : 12 au pouce diamétral, la norme des démarreurs. */
const RING_MODULE = 2.117;

/** Le placement des numéros : rangée A (β = −b), rangée B (β = +b), de l'avant vers l'arrière. */
interface Placement {
  halfDeg: number;
  a: number[];
  b: number[];
}

const span = (from: number, to: number) => Array.from({ length: to - from + 1 }, (_, i) => from + i);

/**
 * L'ordre d'allumage de `FIRING_ORDERS` suppose une numérotation précise : celle du Small Block pour le V8,
 * une rangée après l'autre pour les V10 et V12 (BMW S85, Ferrari), en tour de moteur pour les V4, V6 et boxers
 * (VW, Porsche). Avec une numérotation paire/impaire partout, comme `GasNetwork.bank`, ces ordres ne donnent aucun
 * vilebrequin réel : voilà pourquoi la rangée dessinée ne suit pas toujours la parité du numéro.
 */
function placementOf(layout: EngineConfig['layout'], count: number, order: number[]): Placement {
  if (layout === 'boxer') {
    if (count % 2 === 0) {
      // Deux cylindres opposés, à 360° d'écart dans le cycle : leurs pistons s'écartent et se rapprochent ensemble.
      const half = count / 2;
      const rows = span(0, half - 1).map((r) => [order[r], order[r + half]]);
      if (count === 4) return { halfDeg: 90, a: [1, 2], b: [4, 3] };
      if (count === 6) return { halfDeg: 90, a: [1, 2, 3], b: [6, 5, 4] };
      return { halfDeg: 90, a: rows.map((r) => r[0]), b: rows.map((r) => r[1]) };
    }
    const front = Math.ceil(count / 2);
    return { halfDeg: 90, a: span(1, front), b: span(front + 1, count) };
  }
  switch (count) {
    case 2: return { halfDeg: 45, a: [1], b: [2] };
    case 3: return { halfDeg: 30, a: [1, 2], b: [3] };
    case 4: return { halfDeg: 30, a: [1, 2], b: [4, 3] };
    case 5: return { halfDeg: 36, a: [1, 2, 3], b: [4, 5] };
    case 6: return { halfDeg: 30, a: [1, 2, 3], b: [6, 5, 4] };
    case 8:
      // Plat : deux quatre-en-ligne qui alternent (Ferrari). Croisé : les manetons du Small Block, impairs à droite vus de face.
      return layout === 'cross_plane' ? { halfDeg: 45, a: [2, 4, 6, 8], b: [1, 3, 5, 7] } : { halfDeg: 45, a: [1, 4, 7, 6], b: [8, 3, 2, 5] };
    case 10: return { halfDeg: 45, a: span(1, 5), b: span(6, 10) };
    case 12: return { halfDeg: 30, a: span(1, 6), b: span(7, 12) };
    default: {
      const front = Math.ceil(count / 2);
      return { halfDeg: 45, a: span(1, front), b: span(front + 1, count) };
    }
  }
}

export interface Cylinder {
  /** Indice à partir de 0, celui de `GasNetwork`. */
  index: number;
  /** Numéro, à partir de 1, celui de l'ordre d'allumage. */
  number: number;
  /** Rangée dessinée. */
  bank: number;
  row: number;
  /** Rang dans l'ordre d'allumage, et angle du moteur à son point mort haut d'allumage (0 à 720). */
  slot: number;
  firingDeg: number;
  /** Inclinaison de l'axe, depuis la verticale, vue de face. */
  betaDeg: number;
  /** Position le long du vilebrequin (mm) ; l'avant est du côté des z positifs. */
  z: number;
  /** Angle du maneton, 0 à 360. */
  pinDeg: number;
  /** De quel côté de la culasse est l'admission : −1 ou +1 en x local. */
  intakeSide: number;
}

export interface CrankPin {
  deg: number;
  z: number;
  width: number;
}

/** Une manivelle : deux flasques et ce qu'il y a entre : un maneton, ou deux décalés sur un V à manetons séparés. */
export interface CrankThrow {
  zFront: number;
  zRear: number;
  pins: CrankPin[];
}

export interface MainJournal {
  z: number;
  width: number;
}

export interface EngineLayout {
  key: string;
  count: number;
  order: number[];
  /** 1 ou 2 rangées. */
  banks: number;
  rows: number;
  /** Demi-angle entre les rangées : 0 en ligne, 90 à plat. */
  halfDeg: number;
  flat: boolean;
  fuel: EngineConfig['fuel'];
  turbo: boolean;
  /** Diamètre des roues du turbo, rapporté au turbo moyen. */
  turboScale: number;
  /** Le tube entre les cylindres et la turbine (mm) : 400 est la place où le turbo a toujours été dessiné. */
  turboPipeMm: number;
  /** Une soupape de décharge : sans elle (flutter), l'air revient par le compresseur. */
  releaseValve: boolean;
  /** Le filtre à air, et la longueur de ses cornets s'il en a, de la soupape à la bouche (mm) : recopie `stackLengthM`. */
  airFilter: NonNullable<EngineConfig['airFilter']>;
  stackMm: number;
  dualMass: boolean;
  bore: number;
  /** Course dessinée ; `strokeReal` si les réglages sortent de ce qu'on peut dessiner à l'échelle. */
  stroke: number;
  strokeReal: number;
  strokeClamped: boolean;
  /** Rayon de manivelle et longueur de bielle : course × 1,6, comme `GasNetwork`. */
  crank: number;
  rod: number;
  /** Axe du piston vers le haut de sa tête, et vers le bas de la jupe. */
  pistonTop: number;
  pistonBottom: number;
  /** Plan de joint : distance entre l'axe du vilebrequin et la culasse, le long du cylindre. */
  deck: number;
  rodWidth: number;
  mainRadius: number;
  pinRadius: number;
  webWidth: number;
  cylinders: Cylinder[];
  /** Les cylindres par rangée, dans l'ordre de l'avant vers l'arrière. */
  byBank: Cylinder[][];
  throws: CrankThrow[];
  mains: MainJournal[];
  frontZ: number;
  rearZ: number;
  /** Écart des deux manetons d'un même maneton double (degrés), 0 s'ils sont communs. */
  splitDeg: number;
  /** Écart d'angle de chaque manivelle à la première, pour l'affichage. */
  throwAngles: number[];
  // ── Distribution, en repère de rangée : y sur l'axe du cylindre, x en travers ──
  valve: ValveGeometry;
  /** Couronne du volant : dents du C++ (`EngineConfig::toProfile`), rayon primitif. */
  ring: { z: number; width: number; teeth: number; radius: number };
  pinion: { teeth: number; radius: number; angle: number; x: number; y: number };
  sprocket: { crankTeeth: number; camTeeth: number; crankRadius: number; camRadius: number; z: number[] };
  pulleyZ: number;
}

export interface ValveGeometry {
  /** Inclinaison de chaque soupape sur l'axe (degrés). */
  tiltDeg: number;
  /** Position en travers et le long du vilebrequin, à l'assise. */
  x: number;
  zOffset: number;
  /** Hauteur de l'assise au-dessus du plan de joint. */
  seatRise: number;
  radiusIn: number;
  radiusEx: number;
  stem: number;
  stemRadius: number;
  /** Levée maximale (mm). */
  lift: number;
  /** Rayon de base de la came, et distance de l'assise à l'axe de came. */
  camBase: number;
  camFromSeat: number;
  /** Position de l'axe de came, en repère de rangée, du côté +1. */
  camX: number;
  camY: number;
  /** Sommet du toit de la chambre, au-dessus du plan de joint. */
  roofRise: number;
  /** Un seul arbre par rangée, deux soupapes par cylindre et des culbuteurs. */
  sohc: boolean;
  perCylinder: number;
  /**
   * Les soupapes d'un cylindre, dans l'ordre des instances. La position le long du vilebrequin vaut `zSign × côté × zOffset` :
   * ainsi une culasse tournée d'un demi-tour autour de l'axe du cylindre reste la même pièce.
   */
  slots: readonly ValveSlot[];
  /** L'angle de ce qui appuie sur la came, à donner à `lobeNoseDeg`. Double arbre : l'inclinaison de la soupape. */
  followerDeg: number;
  /** Hauteur du bossage : la levée de la soupape, divisée par le rapport du culbuteur. */
  camLift: number;
  /** Haut de la culasse, demi-largeur et haut du couvre-culasse, en repère de rangée. */
  headTop: number;
  coverW: number;
  coverTop: number;
  /** La bougie (ou l'injecteur) : décalée le long du vilebrequin et inclinée vers l'échappement, pour éviter l'arbre central. */
  plugZ: number;
  plugTiltDeg: number;
  /** Les culbuteurs d'un simple arbre, du côté +1 : l'autre côté s'en déduit par un demi-tour. */
  rocker: RockerGeometry | null;
}

export interface ValveSlot {
  exhaust: boolean;
  zSign: number;
}

/** Un levier du premier genre : la came pousse un bras vers le haut, l'axe est au milieu, l'autre bras pousse la queue de soupape. */
export interface RockerGeometry {
  pivotX: number;
  pivotY: number;
  /** Longueur du bras côté soupape, du bras côté came, et leur rapport. */
  tipArm: number;
  padArm: number;
  ratio: number;
  /** Directions des deux bras, soupape fermée (degrés, sens trigonométrique depuis +x). */
  tipDeg: number;
  padDeg: number;
  /** Direction de la came vers le patin, depuis l'axe du cylindre (degrés). */
  contactDeg: number;
  width: number;
  /** De quoi résoudre la position du culbuteur : voir `rockerAngle`. */
  seatAlong: number;
}

const wrap360 = (deg: number) => ((deg % 360) + 360) % 360;
const wrap720 = (deg: number) => ((deg % 720) + 720) % 720;

/** Levée d'une soupape (0 à 1) : la même loi que `GasNetwork.stepCylinders`. */
export function valveLift(thetaDeg: number, open: number, close: number): number {
  if (!(thetaDeg > open && thetaDeg < close)) return 0;
  const s = Math.sin((Math.PI * (thetaDeg - open)) / (close - open));
  return s * s;
}

/** Distance entre l'axe du vilebrequin et l'axe de piston, à l'angle `alphaDeg` depuis le point mort haut. */
export function pistonDistance(crank: number, rod: number, alphaDeg: number): number {
  const a = alphaDeg * DEG;
  const s = crank * Math.sin(a);
  return crank * Math.cos(a) + Math.sqrt(rod * rod - s * s);
}

/** Le rayon de came à `relDeg` degrés de came du sommet : levée d'une soupape ouverte de `open` à `close`, vue depuis la came. */
export function lobeRadius(base: number, lift: number, relDeg: number, open: number, close: number): number {
  const crankOffset = 2 * Math.abs(((relDeg + 180) % 360 + 360) % 360 - 180);
  const half = (close - open) / 2;
  if (crankOffset >= half) return base;
  const c = Math.cos((Math.PI * crankOffset) / (2 * half));
  return base + lift * c * c;
}

/** Numéro de cylindre, angle de came du sommet de son bossage. Vaut pour la came d'admission ou d'échappement. */
export function lobeNoseDeg(cylinder: Cylinder, tiltDeg: number, side: number, open: number, close: number): number {
  const valveAxis = cylinder.betaDeg + side * tiltDeg;
  return wrap360(valveAxis + 180 - ((open + close) / 2 + cylinder.firingDeg) / 2);
}

/**
 * De combien le culbuteur a tourné (radians, sens horaire vu du côté +1) pour une levée de soupape : son bras reste posé
 * sur la queue, qui coulisse sur son axe. Résolu exactement : l'angle n'est pas petit, une vingtaine de degrés à pleine levée.
 */
export function rockerAngle(valve: ValveGeometry, liftMm: number): number {
  const rocker = valve.rocker;
  if (!rocker) return 0;
  const tilt = valve.tiltDeg * DEG;
  const share = (valve.stem - liftMm - rocker.seatAlong) / rocker.tipArm;
  return rocker.tipDeg * DEG - (Math.asin(share) - tilt);
}

/** Le culbuteur en coupe, axe à l'origine et soupape fermée : `+x` vers la soupape. Les arêtes du bas portent sur la came et la queue. */
export function rockerOutline(rocker: RockerGeometry, bore: number): [number, number][] {
  const at = (deg: number, length: number, up = 0): [number, number] => [Math.cos(deg * DEG) * length, Math.sin(deg * DEG) * length + up];
  const body = bore * 0.075;
  const boss = bore * 0.075;
  return [
    at(rocker.padDeg, rocker.padArm + bore * 0.02),
    at(rocker.padDeg, rocker.padArm),
    at(rocker.padDeg, rocker.padArm * 0.45, -bore * 0.012),
    [-boss * 0.5, -boss],
    [boss * 0.5, -boss],
    at(rocker.tipDeg, rocker.tipArm * 0.6, -bore * 0.012),
    at(rocker.tipDeg, rocker.tipArm),
    at(rocker.tipDeg, rocker.tipArm + bore * 0.025, bore * 0.02),
    at(rocker.tipDeg, rocker.tipArm, body),
    [boss * 0.5, boss],
    [-boss * 0.5, boss],
    at(rocker.padDeg, rocker.padArm + bore * 0.02, body),
  ];
}

/** Les dents du démarreur et de la couronne : recopie de `EngineConfig::toProfile`. */
export function starterTeeth(cylinders: number, displacementCc: number): { ring: number; pinion: number } {
  const litres = displacementCc / 1000;
  const ring = Math.trunc(Math.min(170, Math.max(110, 105 + 9 * litres)));
  const pinion = cylinders >= 8 || litres > 4.5 ? 9 : 10;
  return { ring, pinion };
}

/** Ce qui fait changer les pièces du moteur : à clé égale, les mêmes maillages. */
/** Recopie `stackLengthM` (core/src/air_filter.cpp) : accordée à 72 % du régime maximal. Une erreur ne change que le dessin. */
export function stackLengthMm(boreMm: number, redlineRpm: number): number {
  const tuned = (1.5 * 0.72 * redlineRpm) / 25;
  const length = 343 / (4 * tuned) - 0.61 * 0.25 * (boreMm / 1000);
  return Math.min(Math.max(length, 0.12), 0.5) * 1000;
}

export function layoutKey(config: EngineConfig): string {
  if (config.electric) return 'electric';
  // La longueur des cornets dépend du régime qu'on vise : eux seuls font reconstruire le moteur quand il change.
  const filter = config.airFilter === 'stacks' ? `stacks${config.redlineRpm}` : config.airFilter ?? 'stock';
  return `${config.cylinders}|${config.layout}|${config.displacementCc}|${config.boreMm}|${config.fuel}|${config.aspiration}|${config.turboSize}|${config.turboRelease}|${config.boxFlywheel}|${filter}|${config.turboPipeMm}|${config.camshaft ?? 'dohc'}`;
}

/** Deux arbres, chacun au-dessus de ses soupapes, qu'il attaque directement par un poussoir. */
function twinCamValves(bore: number, deck: number): ValveGeometry {
  const tiltDeg = 20;
  const tilt = tiltDeg * DEG;
  const valveX = bore * 0.24;
  const seatRise = (bore * 0.5 - valveX) * Math.tan(tilt);
  const stem = bore * 1.2;
  const camBase = bore * 0.17;
  const camFromSeat = stem + bore * 0.05 + camBase;
  const lift = bore * 0.115;
  const camX = valveX + Math.sin(tilt) * camFromSeat;
  const camY = deck + seatRise + Math.cos(tilt) * camFromSeat;
  return {
    tiltDeg,
    x: valveX,
    zOffset: bore * 0.24,
    seatRise,
    radiusIn: bore * 0.19,
    radiusEx: bore * 0.16,
    stem,
    stemRadius: bore * 0.028,
    lift,
    camBase,
    camFromSeat,
    camX,
    camY,
    roofRise: bore * 0.5 * Math.tan(tilt),
    sohc: false,
    perCylinder: 4,
    slots: [{ exhaust: false, zSign: 1 }, { exhaust: false, zSign: -1 }, { exhaust: true, zSign: 1 }, { exhaust: true, zSign: -1 }],
    followerDeg: tiltDeg,
    camLift: lift,
    headTop: camY + camBase * 0.2,
    coverW: camX + camBase + bore * 0.06,
    coverTop: camY + camBase + lift + bore * 0.1,
    plugZ: 0,
    plugTiltDeg: 0,
    rocker: null,
  };
}

/**
 * Un seul arbre, au milieu de la culasse, et deux soupapes en travers : leurs têtes ont la taille que donne `valveSizes`.
 * Chaque culbuteur a son axe entre l'arbre et la soupape. Tout se déduit de la queue de soupape : le bras est perpendiculaire
 * à la queue à mi-levée, pour glisser le moins possible, et le patin est perpendiculaire au rayon de came à mi-levée.
 */
function singleCamValves(bore: number, deck: number): ValveGeometry {
  const tiltDeg = 20;
  const tilt = tiltDeg * DEG;
  const valveX = bore * 0.25;
  const seatRise = (bore * 0.5 - valveX) * Math.tan(tilt);
  const stem = bore * 1.1;
  const lift = bore * 0.115;
  const sizes = valveSizes('sohc', bore);
  const tipArm = bore * 0.33;
  const padArm = bore * 0.2357;
  const contactDeg = 35;
  const halfTurn = Math.asin(lift / 2 / tipArm) / DEG;
  const tipDeg = -tiltDeg + halfTurn;
  const padDeg = 180 - contactDeg + halfTurn;
  const tipX = valveX + Math.sin(tilt) * stem;
  const tipY = deck + seatRise + Math.cos(tilt) * stem;
  const pivotX = tipX - tipArm * Math.cos(tipDeg * DEG);
  const pivotY = tipY - tipArm * Math.sin(tipDeg * DEG);
  const padX = pivotX + padArm * Math.cos(padDeg * DEG);
  const padY = pivotY + padArm * Math.sin(padDeg * DEG);
  const camBase = padX / Math.sin(contactDeg * DEG);
  const camY = padY - camBase * Math.cos(contactDeg * DEG);
  const camLift = (lift * padArm) / tipArm;
  // Ce qui reste à la queue, le long de son axe, depuis l'assise : `rockerAngle` en a besoin pour résoudre l'angle exact.
  const seatAlong = stem - tipArm * Math.sin(halfTurn * DEG);
  return {
    tiltDeg,
    x: valveX,
    zOffset: bore * 0.085,
    seatRise,
    radiusIn: sizes.intakeMm / 2,
    radiusEx: sizes.exhaustMm / 2,
    stem,
    stemRadius: bore * 0.03,
    lift,
    camBase,
    camFromSeat: stem,
    camX: 0,
    camY,
    roofRise: bore * 0.5 * Math.tan(tilt),
    sohc: true,
    perCylinder: 2,
    slots: [{ exhaust: false, zSign: 1 }, { exhaust: true, zSign: 1 }],
    followerDeg: contactDeg + 180,
    camLift,
    headTop: camY + camBase * 0.2,
    coverW: tipX + bore * 0.14,
    coverTop: camY + camBase + camLift + bore * 0.18,
    plugZ: bore * 0.3,
    plugTiltDeg: 30,
    rocker: { pivotX, pivotY, tipArm, padArm, ratio: tipArm / padArm, tipDeg, padDeg, contactDeg, width: bore * 0.09, seatAlong },
  };
}

export function engineLayout(config: EngineConfig): EngineLayout {
  const count = config.cylinders;
  const order = firingOrder(count);
  const inline = config.layout === 'inline';
  const flat = config.layout === 'boxer';
  const bore = config.boreMm;
  const boreCm = bore / 10;
  const strokeReal = ((config.displacementCc / count) / ((Math.PI / 4) * boreCm * boreCm)) * 10;
  // Des réglages extrêmes donnent 5 mm ou un mètre de course : on dessine jusqu'à ces proportions, et la légende le dit.
  // Elles couvrent tout ce que le curseur de course atteint : 40 mm sur 110 d'alésage, 130 mm sur 60.
  const stroke = Math.min(bore * 2.2, Math.max(bore * 0.36, strokeReal));
  const crank = stroke / 2;
  const rod = stroke * 1.6;
  const pistonTop = bore * 0.36;
  const pistonBottom = bore * 0.3;
  const deck = crank + rod + pistonTop;

  const placement: Placement = inline ? { halfDeg: 0, a: span(1, count), b: [] } : placementOf(config.layout, count, order);
  const banks = placement.b.length > 0 ? 2 : 1;
  const rows = Math.max(placement.a.length, placement.b.length);
  const halfDeg = placement.halfDeg;

  // ── Longueur : ce qui se suit le long de l'arbre ──
  const mainRadius = bore * 0.29;
  const pinRadius = bore * 0.24;
  let rowPitch: number;
  let mainWidth: number;
  let webWidth: number;
  if (inline) {
    rowPitch = bore * 1.17;
    mainWidth = bore * 0.3;
    webWidth = bore * 0.2;
  } else if (flat) {
    rowPitch = bore * 1.3;
    mainWidth = bore * 0.18;
    webWidth = bore * 0.12;
  } else {
    rowPitch = bore * 1.22;
    mainWidth = bore * 0.28;
    webWidth = bore * 0.18;
  }
  const rowZ = (row: number) => ((rows - 1) / 2 - row) * rowPitch;

  // Le maneton de chaque cylindre. On calcule d'abord, pour savoir qui partage quoi.
  const pinOf = (bankSign: number, slot: number) => wrap360(bankSign * halfDeg - (slot * 720) / count);
  const slotOf = (n: number) => order.indexOf(n);

  const throws: CrankThrow[] = [];
  const cylinders: Cylinder[] = new Array(count);
  const place = (n: number, bank: number, row: number, z: number) => {
    const slot = slotOf(n);
    const beta = banks === 1 ? 0 : (bank === 0 ? -halfDeg : halfDeg);
    cylinders[n - 1] = {
      index: n - 1,
      number: n,
      bank,
      row,
      slot,
      firingDeg: (slot * 720) / count,
      betaDeg: beta,
      z,
      pinDeg: pinOf(beta === 0 ? 0 : Math.sign(beta), slot),
      // Admission au milieu du V, ou au-dessus d'un boxer ; à gauche d'un moteur en ligne.
      intakeSide: beta < 0 ? 1 : -1,
    };
  };

  let splitDeg = 0;
  let pinWidth: number;
  if (inline || flat) {
    // Une manivelle par cylindre, un palier entre chacune.
    const throwPitch = inline ? rowPitch : rowPitch / 2;
    pinWidth = throwPitch - mainWidth - 2 * webWidth;
    const entries: { n: number; bank: number; row: number; z: number }[] = [];
    placement.a.forEach((n, row) => entries.push({ n, bank: 0, row, z: inline ? rowZ(row) : rowZ(row) + rowPitch / 4 }));
    placement.b.forEach((n, row) => entries.push({ n, bank: 1, row, z: rowZ(row) - rowPitch / 4 }));
    entries.sort((p, q) => q.z - p.z);
    for (const entry of entries) {
      place(entry.n, entry.bank, entry.row, entry.z);
      const half = pinWidth / 2 + webWidth;
      throws.push({ zFront: entry.z + half, zRear: entry.z - half, pins: [{ deg: cylinders[entry.n - 1].pinDeg, z: entry.z, width: pinWidth }] });
    }
  } else {
    // Un V : deux bielles par manivelle, sur un maneton commun ou sur deux manetons décalés.
    pinWidth = rowPitch - mainWidth - 2 * webWidth;
    const flying = bore * 0.05;
    for (let row = 0; row < rows; row++) {
      const na = placement.a[row];
      const nb = placement.b[row];
      const z = rowZ(row);
      const half = pinWidth / 2 + webWidth;
      if (na === undefined || nb === undefined) {
        const n = na ?? nb;
        place(n, na === undefined ? 1 : 0, row, z);
        throws.push({ zFront: z + half, zRear: z - half, pins: [{ deg: cylinders[n - 1].pinDeg, z, width: pinWidth }] });
        continue;
      }
      const degA = pinOf(-1, slotOf(na));
      const degB = pinOf(1, slotOf(nb));
      const offset = Math.abs(((degB - degA + 540) % 360) - 180);
      if (offset < 1) {
        const q = pinWidth / 4;
        place(na, 0, row, z + q);
        place(nb, 1, row, z - q);
        throws.push({ zFront: z + half, zRear: z - half, pins: [{ deg: degA, z, width: pinWidth }] });
      } else {
        splitDeg = Math.max(splitDeg, offset);
        const each = (pinWidth - flying) / 2;
        const q = (each + flying) / 2;
        place(na, 0, row, z + q);
        place(nb, 1, row, z - q);
        throws.push({ zFront: z + half, zRear: z - half, pins: [{ deg: degA, z: z + q, width: each }, { deg: degB, z: z - q, width: each }] });
      }
    }
  }
  const rodWidth = banks === 2 && !flat ? pinWidth / 2 - bore * 0.015 : pinWidth - bore * 0.015;

  const mains: MainJournal[] = [];
  for (let k = 0; k < throws.length; k++) mains.push({ z: throws[k].zFront + mainWidth / 2, width: mainWidth });
  mains.push({ z: throws[throws.length - 1].zRear - mainWidth / 2, width: mainWidth });
  const frontZ = mains[0].z + mainWidth / 2;
  const rearZ = mains[mains.length - 1].z - mainWidth / 2;

  // ── Volant, démarreur ──
  const teeth = starterTeeth(count, config.displacementCc);
  const ringRadius = (teeth.ring * RING_MODULE) / 2;
  const pinionRadius = (teeth.pinion * RING_MODULE) / 2;
  const ringZ = rearZ - bore * 0.22;
  const pinionAngle = 205 * DEG;
  const pinionDistance = ringRadius + pinionRadius;

  // ── Distribution : deux arbres à cames en tête, quatre soupapes par cylindre ; ou un seul, deux soupapes et des culbuteurs ──
  const valve = config.camshaft === 'sohc' ? singleCamValves(bore, deck) : twinCamValves(bore, deck);

  const crankTeeth = 19;
  const camTeeth = 38;
  const crankSprocket = bore * 0.28;

  const byBank: Cylinder[][] = [];
  for (let b = 0; b < banks; b++) byBank.push(cylinders.filter((c) => c.bank === b).sort((p, q) => q.z - p.z));

  const firstDeg = throws[0].pins[0].deg;
  const throwAngles = throws.map((item) => Math.round(wrap360(item.pins[0].deg - firstDeg)));

  return {
    key: layoutKey(config),
    count,
    order,
    banks,
    rows,
    halfDeg,
    flat,
    fuel: config.fuel,
    turbo: config.aspiration === 'turbo',
    turboScale: TURBO_SCALE[config.turboSize ?? 'medium'],
    turboPipeMm: config.turboPipeMm ?? 400,
    releaseValve: config.turboRelease !== 'flutter',
    airFilter: config.airFilter ?? 'stock',
    stackMm: stackLengthMm(config.boreMm, config.redlineRpm),
    dualMass: config.boxFlywheel === 'dual_mass',
    bore,
    stroke,
    strokeReal,
    strokeClamped: Math.abs(stroke - strokeReal) > 0.01,
    crank,
    rod,
    pistonTop,
    pistonBottom,
    deck,
    rodWidth,
    mainRadius,
    pinRadius,
    webWidth,
    cylinders,
    byBank,
    throws,
    mains,
    frontZ,
    rearZ,
    splitDeg: Math.round(splitDeg),
    throwAngles,
    valve,
    ring: { z: ringZ, width: bore * 0.14, teeth: teeth.ring, radius: ringRadius },
    pinion: { teeth: teeth.pinion, radius: pinionRadius, angle: pinionAngle, x: Math.cos(pinionAngle) * pinionDistance, y: Math.sin(pinionAngle) * pinionDistance },
    sprocket: { crankTeeth, camTeeth, crankRadius: crankSprocket, camRadius: crankSprocket * 2, z: banks === 2 ? [frontZ + bore * 0.3, frontZ + bore * 0.12] : [frontZ + bore * 0.18] },
    pulleyZ: frontZ + bore * 0.62,
  };
}

/** D'un repère de rangée (x en travers, y sur l'axe) vers la vue de face. `out[0]`, `out[1]` : x, y. */
export function bankToFront(out: { [i: number]: number }, betaDeg: number, x: number, y: number): void {
  const b = betaDeg * DEG;
  const c = Math.cos(b);
  const s = Math.sin(b);
  out[0] = x * c + y * s;
  out[1] = -x * s + y * c;
}

/** Le plénum d'admission, vue de face : sur le côté d'un moteur en ligne, au milieu d'un V ou au-dessus d'un boxer. */
export function plenumPosition(layout: EngineLayout): { x: number; y: number; radius: number } {
  const { bore, deck } = layout;
  const radius = bore * 0.3;
  if (layout.banks === 1) return { x: -bore * 2.09, y: deck + bore * 0.62, radius };
  if (layout.flat) return { x: 0, y: bore * 2.0, radius };
  const p = [0, 0];
  bankToFront(p, layout.halfDeg, -bore * 1.04, deck + bore * 0.62);
  return { x: 0, y: p[1] + bore * 0.9, radius };
}

/**
 * Le cornet d'un cylindre, vu de face : de sa bride sur la culasse à sa bouche. Vers l'extérieur sur une rangée seule,
 * vers le haut entre les deux rangées d'un V. La bride compte déjà dans la longueur : le conduit dans la culasse en prend une part.
 */
export function stackOf(layout: EngineLayout, cylinder: Cylinder): { from: [number, number]; to: [number, number]; radius: number } {
  const { bore, deck } = layout;
  const out = [0, 0];
  bankToFront(out, cylinder.betaDeg, cylinder.intakeSide, 0.1);
  if (layout.banks === 2) out[1] += 1.4;
  const norm = Math.hypot(out[0], out[1]) || 1;
  const from = [0, 0];
  bankToFront(from, cylinder.betaDeg, cylinder.intakeSide * bore * 1.04, deck + bore * 0.62);
  const visible = Math.max(layout.stackMm - bore * 0.7, bore * 1.2);
  return { from: [from[0], from[1]], to: [from[0] + (out[0] / norm) * visible, from[1] + (out[1] / norm) * visible], radius: bore * 0.15 };
}

/** Le collecteur d'échappement d'une rangée, vue de face : à l'extérieur, un peu sous la culasse. */
export function collectorPosition(layout: EngineLayout, bank: number): { x: number; y: number } {
  const { bore, deck } = layout;
  const beta = layout.banks === 1 ? 0 : bank === 0 ? -layout.halfDeg : layout.halfDeg;
  const side = -layout.byBank[bank][0].intakeSide;
  const p = [0, 0];
  bankToFront(p, beta, side * (bore * 1.76), deck - bore * 0.45);
  return { x: p[0], y: p[1] };
}

// ── La chaîne de distribution ──

export interface ChainPath {
  /** Longueur du tour (mm). */
  length: number;
  /** Points réguliers le long du tour : x, y, angle de la tangente. */
  x: Float64Array;
  y: Float64Array;
  angle: Float64Array;
  step: number;
  links: number;
  pitch: number;
}

/**
 * Le tour d'une chaîne autour de ses pignons, parcouru dans le sens du vilebrequin : portions droites tangentes
 * extérieures et arcs. `circles` : x, y, rayon, dans n'importe quel ordre, tous sur le pourtour.
 */
export function chainPath(circles: readonly (readonly [number, number, number])[], pitchWanted: number): ChainPath {
  const cx = circles.reduce((sum, c) => sum + c[0], 0) / circles.length;
  const cy = circles.reduce((sum, c) => sum + c[1], 0) / circles.length;
  // Sens des aiguilles d'une montre autour du centre.
  const loop = [...circles].sort((p, q) => Math.atan2(q[1] - cy, q[0] - cx) - Math.atan2(p[1] - cy, p[0] - cx));
  const n = loop.length;
  const tangentAngle: number[] = [];
  for (let k = 0; k < n; k++) {
    const a = loop[k];
    const b = loop[(k + 1) % n];
    const dx = b[0] - a[0];
    const dy = b[1] - a[1];
    const length = Math.hypot(dx, dy);
    // Normale vers l'extérieur : à gauche du sens de marche.
    tangentAngle.push(Math.atan2(dy, dx) + Math.acos((a[2] - b[2]) / length));
  }
  // Une ligne brisée fine, puis un rééchantillonnage régulier.
  const px: number[] = [];
  const py: number[] = [];
  for (let k = 0; k < n; k++) {
    const c = loop[k];
    const arriving = tangentAngle[(k + n - 1) % n];
    let leaving = tangentAngle[k];
    while (leaving > arriving) leaving -= Math.PI * 2;
    const arc = arriving - leaving;
    const samples = Math.max(2, Math.ceil(arc * 18));
    for (let s = 0; s <= samples; s++) {
      const a = arriving - (arc * s) / samples;
      px.push(c[0] + Math.cos(a) * c[2]);
      py.push(c[1] + Math.sin(a) * c[2]);
    }
  }
  let total = 0;
  const lengths: number[] = [];
  for (let k = 0; k < px.length; k++) {
    const j = (k + 1) % px.length;
    const d = Math.hypot(px[j] - px[k], py[j] - py[k]);
    lengths.push(d);
    total += d;
  }
  const links = Math.max(8, Math.round(total / pitchWanted / 2) * 2);
  const count = links * 4;
  const step = total / count;
  const x = new Float64Array(count);
  const y = new Float64Array(count);
  const angle = new Float64Array(count);
  let at = 0;
  let walked = 0;
  for (let k = 0; k < count; k++) {
    const want = k * step;
    while (walked + lengths[at] < want && at < px.length - 1) {
      walked += lengths[at];
      at++;
    }
    const j = (at + 1) % px.length;
    const f = lengths[at] > 0 ? (want - walked) / lengths[at] : 0;
    x[k] = px[at] + (px[j] - px[at]) * f;
    y[k] = py[at] + (py[j] - py[at]) * f;
    angle[k] = Math.atan2(py[j] - py[at], px[j] - px[at]);
  }
  return { length: total, x, y, angle, step, links, pitch: total / links };
}

/** Les pignons d'une chaîne sur une rangée : le vilebrequin, puis les deux cames, ou la seule. */
export function chainCircles(layout: EngineLayout, bank: number): [number, number, number][] {
  const beta = layout.banks === 1 ? 0 : bank === 0 ? -layout.halfDeg : layout.halfDeg;
  const out: [number, number, number][] = [[0, 0, layout.sprocket.crankRadius]];
  const p = [0, 0];
  for (const side of layout.valve.sohc ? [0] : [-1, 1]) {
    bankToFront(p, beta, side * layout.valve.camX, layout.valve.camY);
    out.push([p[0], p[1], layout.sprocket.camRadius]);
  }
  return out;
}

// ── L'état d'un instant ──

/** Ce que l'horloge `Motion` donne. */
export interface PoseClock {
  cycle: number;
  /** Cycles parcourus depuis le début, sans repli : ce qui défile sans fin (chaîne, poulies). */
  turns: number;
  /** Angle de l'arbre du turbo (radians). */
  turbineAngle: number;
  /** Disque flou quand l'œil ne suit plus les aubes, wastegate et décharge ouvertes (0 à 1), températures de l'air (K). */
  turbineBlur?: number;
  wastegate?: number;
  blowoff?: number;
  compressorOutK?: number;
  coolerOutK?: number;
  rpm: number;
  load: number;
  heat: number;
  reduced: boolean;
  burning: boolean;
}

/** Ce que la télémétrie dit de l'état du moteur. */
export interface PoseTelemetry {
  cut: number;
  cranking: number;
  stalled: number;
  rpm: number;
}

/** Ce que le gaz calcule dans chaque cylindre : `GasNetwork`, s'il tourne. */
export interface PoseGas {
  cylinders: number;
  cylT: ArrayLike<number>;
  cylM: ArrayLike<number>;
  cylY: ArrayLike<number>;
  cylBurn: ArrayLike<number>;
  cylFired: ArrayLike<number>;
  cylExhaust: ArrayLike<number>;
  cylIntake: ArrayLike<number>;
}

export type EngineState = 'stopped' | 'cranking' | 'cut' | 'stalled' | 'running';

export function engineState(telemetry: PoseTelemetry): EngineState {
  if (telemetry.cranking) return 'cranking';
  if (telemetry.stalled) return 'stalled';
  if (telemetry.rpm < 20) return 'stopped';
  if (telemetry.cut) return 'cut';
  return 'running';
}

/** Allumage sous tension : pas de bougie ni d'injection sans combustion demandée, sauf au démarreur. */
export function firingAllowed(clock: PoseClock, telemetry: PoseTelemetry): boolean {
  if (telemetry.cut || clock.reduced || clock.rpm < 20) return false;
  return clock.burning || !!telemetry.cranking;
}

/** Une bougie de préchauffage chauffe au démarreur, puis tant que le moteur est froid. */
export function glowWanted(diesel: boolean, cranking: boolean, rpm: number, heat: number, runSeconds: number): boolean {
  if (!diesel) return false;
  if (cranking) return true;
  return rpm > 150 && heat < 0.35 && runSeconds < 30;
}

const SPARK_SECONDS = 0.09;
const SPRAY_SECONDS = 0.16;
const GLOW_TAU = 1.1;

/** Toutes les pièces mobiles à cet instant : calculé une fois par image, lu par les deux vues. Rien n'alloue. */
export class EnginePose {
  layout: EngineLayout;
  /** Angle du moteur (degrés, 0 à 720), des cames (0 à 360). */
  crankDeg = 0;
  camDeg = 0;
  /** Tours de vilebrequin depuis le début, continus : la chaîne avance sans à-coup au repli des 720°. */
  chainTurns = 0;
  /** Par cylindre, indice de `GasNetwork`. */
  theta: Float64Array;
  alpha: Float64Array;
  piston: Float64Array;
  /** Maneton, vue de face. */
  pinX: Float64Array;
  pinY: Float64Array;
  /** Axe de piston, vue de face. */
  wristX: Float64Array;
  wristY: Float64Array;
  /** Angle de la bielle dans la vue de face (radians, depuis la verticale, sens horaire), du maneton vers le piston. */
  rodAngle: Float64Array;
  liftIn: Float64Array;
  liftEx: Float64Array;
  /** L'angle de chaque culbuteur (radians), s'il y en a : voir `rockerAngle`. */
  rockerIn: Float64Array;
  rockerEx: Float64Array;
  /** Étincelle et jet de carburant, 1 à leur début puis ils s'éteignent. */
  spark: Float64Array;
  spray: Float64Array;
  /** Le gaz : température (K), part brûlée, flamme (0 à 1 de la masse brûlée, 0 sans combustion), jets aux soupapes (−1 à 1). */
  gasT: Float64Array;
  gasBurnt: Float64Array;
  flame: Float64Array;
  jetIn: Float64Array;
  jetEx: Float64Array;
  gasLive = false;
  /** Pignon de démarreur : 0 en retrait, 1 engagé. Son angle, tiré de la couronne quand il est en prise (radians). */
  starter = 0;
  pinionAngle = 0;
  turbineAngle = 0;
  turbineBlur = 0;
  wastegate = 0;
  blowoff = 0;
  compressorOutK = 293;
  coolerOutK = 293;
  /** Bougie de préchauffage, 0 froide, 1 à blanc. */
  glow = 0;
  state: EngineState = 'stopped';
  /** Étincelles comptées, et la dernière : de quoi vérifier l'ordre. */
  sparkCount = 0;
  lastSparkCylinder = -1;
  sprayCount = 0;
  private previousCycle = -1;
  private runSeconds = 0;

  constructor(layout: EngineLayout) {
    this.layout = layout;
    const n = layout.count;
    const array = () => new Float64Array(n);
    this.theta = array();
    this.alpha = array();
    this.piston = array();
    this.pinX = array();
    this.pinY = array();
    this.wristX = array();
    this.wristY = array();
    this.rodAngle = array();
    this.liftIn = array();
    this.liftEx = array();
    this.rockerIn = array();
    this.rockerEx = array();
    this.spark = array();
    this.spray = array();
    this.gasT = array().fill(293);
    this.gasBurnt = array();
    this.flame = array();
    this.jetIn = array();
    this.jetEx = array();
    this.solve(0);
  }

  /** La mécanique seule, à l'angle du moteur `crankDeg` : aucune horloge, donc testable pièce par pièce. */
  solve(crankDeg: number): void {
    const layout = this.layout;
    const { crank, rod } = layout;
    this.crankDeg = wrap720(crankDeg);
    this.camDeg = this.crankDeg / 2;
    for (const cylinder of layout.cylinders) {
      const i = cylinder.index;
      const theta = wrap720(crankDeg - cylinder.firingDeg);
      const pinDeg = cylinder.pinDeg + crankDeg;
      const alpha = pinDeg - cylinder.betaDeg;
      const distance = pistonDistance(crank, rod, alpha);
      const pinRad = pinDeg * DEG;
      const betaRad = cylinder.betaDeg * DEG;
      const pinX = crank * Math.sin(pinRad);
      const pinY = crank * Math.cos(pinRad);
      const wristX = distance * Math.sin(betaRad);
      const wristY = distance * Math.cos(betaRad);
      this.theta[i] = theta;
      this.alpha[i] = alpha;
      this.piston[i] = distance;
      this.pinX[i] = pinX;
      this.pinY[i] = pinY;
      this.wristX[i] = wristX;
      this.wristY[i] = wristY;
      this.rodAngle[i] = Math.atan2(wristX - pinX, wristY - pinY);
      this.liftIn[i] = valveLift(theta, IVO, IVC);
      this.liftEx[i] = valveLift(theta, EVO, EVC);
      if (layout.valve.sohc) {
        this.rockerIn[i] = rockerAngle(layout.valve, this.liftIn[i] * layout.valve.lift);
        this.rockerEx[i] = rockerAngle(layout.valve, this.liftEx[i] * layout.valve.lift);
      }
    }
    const ring = layout.ring;
    const pinion = layout.pinion;
    // La dent de la couronne passe dans le creux du pignon, et ils tournent en sens inverse.
    const psi = pinion.angle;
    const toothAngle = (Math.PI * 2) / pinion.teeth;
    this.pinionAngle = psi + Math.PI - toothAngle * (0.5 - (psi * ring.teeth) / (Math.PI * 2)) + (crankDeg * DEG * ring.teeth) / pinion.teeth;
  }

  /** Une image. `seconds` : temps écoulé, seulement pour éteindre ce qui brille et pousser le pignon. */
  update(clock: PoseClock, telemetry: PoseTelemetry, gas: PoseGas | null, gasActive: boolean, seconds: number): void {
    const layout = this.layout;
    const crankDeg = clock.cycle * 720;
    const before = this.previousCycle < 0 ? clock.cycle : this.previousCycle;
    this.previousCycle = clock.cycle;
    // Le moteur avance au plus d'un cycle entre deux images : au-delà, l'horloge a sauté.
    const advanced = ((clock.cycle - before + 1) % 1) * 720;
    this.solve(crankDeg);
    this.chainTurns = clock.turns * 2;
    this.turbineAngle = clock.turbineAngle;
    this.turbineBlur = clock.turbineBlur ?? 0;
    this.wastegate = clock.wastegate ?? 0;
    this.blowoff = clock.blowoff ?? 0;
    this.compressorOutK = clock.compressorOutK ?? 293;
    this.coolerOutK = clock.coolerOutK ?? 293;

    this.state = engineState(telemetry);
    const cranking = !!telemetry.cranking;
    const diesel = layout.fuel === 'diesel';
    const firing = firingAllowed(clock, telemetry);
    const fade = seconds > 0 ? seconds : 0;
    const advance = ignitionAdvance(diesel, clock.rpm);
    const fromAngle = crankDeg - advanced;
    for (let i = 0; i < layout.count; i++) {
      const cylinder = layout.cylinders[i];
      this.spark[i] = Math.max(0, this.spark[i] - fade / SPARK_SECONDS);
      this.spray[i] = Math.max(0, this.spray[i] - fade / SPRAY_SECONDS);
      if (!firing || advanced <= 0) continue;
      // L'étincelle (ou le début de l'injection d'un diesel) tombe avant le point mort haut. Elle ne doit pas être manquée
      // même si une image saute plusieurs dizaines de degrés.
      const sparkAt = wrap720(cylinder.firingDeg - advance - fromAngle);
      if (sparkAt < advanced) {
        if (diesel) {
          this.spray[i] = 1;
          this.sprayCount++;
        } else {
          this.spark[i] = 1;
          this.sparkCount++;
          this.lastSparkCylinder = i;
        }
      }
      if (!diesel) {
        const sprayAt = wrap720(cylinder.firingDeg + PETROL_INJECTION_DEG - fromAngle);
        if (sprayAt < advanced) {
          this.spray[i] = 1;
          this.sprayCount++;
        }
      }
    }
    if (!firing) {
      // La coupure éteint tout de suite ce qui restait d'étincelle ou de jet.
      this.spark.fill(0);
      this.spray.fill(0);
    }

    this.runSeconds = clock.rpm > 150 ? this.runSeconds + fade : 0;
    const glowing = glowWanted(diesel, cranking, clock.rpm, clock.heat, this.runSeconds);
    this.glow += ((glowing ? 1 : 0) - this.glow) * (1 - Math.exp(-fade / GLOW_TAU));
    this.starter += ((cranking ? 1 : 0) - this.starter) * (1 - Math.exp(-fade / 0.05));

    const live = !!gas && gasActive && gas.cylinders === layout.count && clock.rpm > 20;
    this.gasLive = live;
    if (live && gas) {
      const rate = Math.max(clock.rpm * 0.105, 5);
      for (let i = 0; i < layout.count; i++) {
        const mass = Math.max(gas.cylM[i], 1e-9);
        this.gasT[i] = gas.cylT[i];
        this.gasBurnt[i] = gas.cylY[i] / mass;
        const burn = gas.cylBurn[i];
        // La flamme vient du calcul : rien tant que le mélange n'a pas été allumé.
        this.flame[i] = gas.cylFired[i] === 1 && burn > 0.002 && burn < 0.998 && firing ? burn : 0;
        this.jetIn[i] = gas.cylIntake[i] / (mass * rate);
        this.jetEx[i] = gas.cylExhaust[i] / (mass * rate);
      }
    } else {
      this.flame.fill(0);
      this.jetIn.fill(0);
      this.jetEx.fill(0);
    }
  }
}

/** Le moment de l'étincelle, angle de ce cylindre dans son cycle (0 au point mort haut, sur 720). */
export function sparkThetaDeg(diesel: boolean, rpm: number): number {
  return wrap720(-ignitionAdvance(diesel, rpm));
}
