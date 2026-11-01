/**
 * La coupe de l'embrayage, en millimètres : un seul jeu de profils, dessiné tel quel en 2D et tourné autour de l'axe en 3D.
 * Rien ici ne connaît le canvas ni three.js. `x` court le long de l'arbre, 0 sur la face de friction du volant et positif
 * vers la boîte ; `r` part de l'axe.
 */
import { BEARING_BALL, BEARING_CLEARANCE, BEARING_PITCH_R, DCT_OPEN_LIFT, FORK_SLAVE_R, FORK_STUD_R, type ClutchDims, type ClutchFrame, type ClutchTravel } from './clutchMechanism';

export type PartMaterial = 'cast' | 'steel' | 'sheet' | 'friction' | 'ring' | 'race' | 'carrier' | 'brass' | 'shaft' | 'housing' | 'crank' | 'rubber' | 'lever';

/** À quoi une pièce est solidaire : c'est ce qui donne son angle et son déplacement le long de l'arbre. */
export type PartGroup =
  | 'crank' // tourne avec le moteur, ne bouge pas le long de l'arbre
  | 'mass' // ce sur quoi l'embrayage est boulonné : le volant, ou la masse secondaire d'un bi-masse, avec son retard
  | 'plate' // plateau de pression, solidaire du volant, il recule
  | 'hub' // moyeu : la vitesse de la boîte, et il coulisse sur les cannelures
  | 'shaft' // l'arbre primaire : il tourne avec le moyeu, mais ne recule jamais
  | 'liningFront' // garniture, côté volant, avec la flexion des ressorts
  | 'liningRear'
  | 'race' // la bague tournante de la butée
  | 'cage'
  | 'carrier' // ce qui coulisse sans tourner
  | 'static'
  // Double embrayage
  | 'k1Plate' | 'k2Plate' | 'k1Hub' | 'k2Hub' | 'k1Shaft' | 'k2Shaft' | 'k1Lining' | 'k2Lining' | 'k1Race' | 'k2Race' | 'k1Carrier' | 'k2Carrier' | 'k1Cage' | 'k2Cage';

export interface SectionPart {
  id: string;
  material: PartMaterial;
  group: PartGroup;
  /** Contour fermé x, r, x, r… sur le demi-plan r > 0. */
  points: number[];
}

/** Toutes les cotes axiales et ce qui s'en déduit : calcul fait une fois par moteur. */
export interface ClutchLayout {
  dims: ClutchDims;
  T: number;
  liningThickness: number;
  discWidth: number;
  plateX: number;
  plateThickness: number;
  fulcrumX: number;
  sheet: number;
  pivotX: number;
  tipX: number;
  coverIn: number;
  coverOut: number;
  coverOpening: number;
  wallIn: number;
  wallOut: number;
  flangeR: number;
  noseX: number;
  /** Plot d'appui de la fourchette sur le chariot, et sa position le long de l'arbre. */
  padX: number;
  forkX: number;
  guideX: number;
  wallX: number;
  hubR: number;
  springR: number;
  springD: number;
  tubeIn: number;
  tubeOut: number;
  /** Récepteur et maître-cylindre, dans le plan de coupe. */
  slaveFromX: number;
  slaveToX: number;
  slaveR: number;
  masterFromX: number;
  masterToX: number;
  masterR: number;
  pedalX: number;
  pedalR: number;
  bellR: number;
  xMin: number;
  xMax: number;
  rMin: number;
  rMax: number;
  /** Bi-masse : rayon des ressorts en arc, et position axiale de leur centre. */
  arcR: number;
  arcX: number;
  arcD: number;
  boltR: number;
  dct: DctLayout | null;
}

export interface DctLayout {
  k1PlateX: number;
  k1DiscX: number;
  centreX: number;
  k2DiscX: number;
  k2PlateX: number;
  plateThickness: number;
  centreThickness: number;
  leverPivotX: number;
  k1LeverTipR: number;
  k2LeverTipR: number;
  k1LeverPivotR: number;
  k2LeverPivotR: number;
  k1NoseX: number;
  k2NoseX: number;
  hollowR: number;
  tieRodR: number;
  tieRods: number;
}

const CONE = Math.tan((10.5 * Math.PI) / 180);

export function clutchLayout(d: ClutchDims): ClutchLayout {
  const T = d.flywheelThickness;
  const sheet = 2.4;
  const liningThickness = 3.4;
  const discWidth = 7.4;
  const plateX = discWidth;
  const plateThickness = 15;
  const fulcrumX = plateX + plateThickness + 3 + sheet / 2;
  const pivotX = fulcrumX + (d.fulcrumR - d.pivotR) * CONE;
  const tipX = pivotX + (d.pivotR - d.contactR) * CONE;
  const coverIn = pivotX + sheet / 2 + 3;
  const coverOut = coverIn + 3;
  const noseX = tipX + sheet / 2 + BEARING_CLEARANCE;
  const padX = noseX + 24;
  const forkX = padX + 5;
  const guideX = noseX + 8;
  const wallX = noseX + 70;
  const hubR = 22;
  const lo = d.liningOuterR;
  const li = d.liningInnerR;
  const springR = (hubR + 1 + li - 8) / 2 + 1;
  const dct = d.kind === 'dual_clutch' ? dctLayout(d) : null;
  const effectiveWall = dct ? Math.max(wallX, dct.k1NoseX + 62) : wallX;
  const effectiveFork = dct ? dct.k1NoseX + 42 : forkX;
  const slaveFromX = effectiveFork + 16;
  const slaveToX = slaveFromX + 42;
  const masterFromX = slaveToX + 26;
  const masterToX = masterFromX + 48;
  const pedalX = masterToX + 24;
  return {
    dims: d, T, liningThickness, discWidth, plateX, plateThickness, fulcrumX, sheet, pivotX, tipX, coverIn, coverOut,
    coverOpening: d.pivotR - 16 * (d.discDiameter / 228),
    wallIn: lo + 5.5, wallOut: lo + 8.5, flangeR: d.dualMass ? d.flywheelR - 13 : d.flywheelR - 7,
    noseX, padX, forkX: effectiveFork, guideX, wallX: effectiveWall, hubR, springR, springD: 13,
    tubeIn: d.shaftR + 1, tubeOut: d.shaftR + 4.5,
    slaveFromX, slaveToX, slaveR: FORK_SLAVE_R, masterFromX, masterToX, masterR: 150, pedalX, pedalR: 202, bellR: d.flywheelR + 11,
    xMin: -T - 32, xMax: pedalX + 44, rMin: -100, rMax: 208,
    arcR: d.flywheelR - 24, arcX: (-T + 4 - 17) / 2, arcD: 22,
    boltR: 34,
    dct,
  };
}

function dctLayout(d: ClutchDims): DctLayout {
  const plateThickness = 13;
  const centreThickness = 18;
  const disc = 7.4;
  const k1PlateX = 8;
  const k1DiscX = k1PlateX + plateThickness;
  const centreX = k1DiscX + disc;
  const k2DiscX = centreX + centreThickness;
  const k2PlateX = k2DiscX + disc;
  const leverPivotX = k2PlateX + plateThickness + 10;
  return {
    k1PlateX, k1DiscX, centreX, k2DiscX, k2PlateX, plateThickness, centreThickness, leverPivotX,
    k1LeverTipR: 28, k2LeverTipR: 54,
    k1LeverPivotR: d.liningOuterR - 22, k2LeverPivotR: d.liningOuterR - 8,
    k1NoseX: leverPivotX + 14, k2NoseX: leverPivotX + 24,
    hollowR: d.shaftR + 4,
    tieRodR: d.liningOuterR + 7, tieRods: 9,
  };
}

/** Ce que la fourchette a parcouru à une hauteur donnée : elle bascule d'un bloc autour de sa rotule. */
export const forkXAt = (l: ClutchLayout, r: number, angle: number) => l.forkX - (r - FORK_STUD_R) * angle;

/**
 * Position du feuillet moyen du diaphragme à un rayon donné. Du jonc jusqu'à la butée, un levier qui ploie vers le bout ;
 * du jonc jusqu'au plateau, une droite qui suit le plateau tel qu'il est dessiné.
 */
export function diaphragmX(l: ClutchLayout, travel: ClutchTravel, drawnLift: number, r: number): number {
  const d = l.dims;
  if (r >= d.pivotR) {
    const u = (r - d.pivotR) / (d.fulcrumR - d.pivotR);
    return l.pivotX + u * (l.fulcrumX + drawnLift - l.pivotX);
  }
  const u = (d.pivotR - r) / (d.pivotR - d.contactR);
  return l.pivotX + u * (l.tipX - travel.finger + travel.flex - l.pivotX) - travel.flex * u * u;
}

/**
 * Levée telle qu'on la dessine. Les deux millimètres réels font trois pixels à cette échelle : on les multiplie,
 * et l'écran l'indique. La mécanique, les tests et les chiffres gardent la vraie valeur.
 */
export const LIFT_EXAGGERATION = 3;
export const drawnLift = (travel: ClutchTravel) => travel.lift * LIFT_EXAGGERATION;
export const drawnCushion = (travel: ClutchTravel) => travel.cushion * LIFT_EXAGGERATION;
export const drawnGap = (travel: ClutchTravel) => travel.gap * LIFT_EXAGGERATION;

/** Déplacement d'une pièce le long de l'arbre à cette image, tel qu'on le dessine. */
export function groupOffset(group: PartGroup, frame: ClutchFrame): number {
  const t = frame.travel;
  switch (group) {
    case 'plate': return t.lift * LIFT_EXAGGERATION;
    case 'hub':
    case 'liningFront': return (t.gap * LIFT_EXAGGERATION) / 2;
    case 'liningRear': return ((t.gap * LIFT_EXAGGERATION) / 2) + t.cushion * LIFT_EXAGGERATION;
    case 'race':
    case 'cage':
    case 'carrier': return -t.bearing;
    case 'k1Plate': return -frame.k1Open * DCT_OPEN_LIFT * LIFT_EXAGGERATION;
    case 'k2Plate': return frame.k2Open * DCT_OPEN_LIFT * LIFT_EXAGGERATION;
    case 'k1Hub':
    case 'k1Lining': return (-frame.k1Open * DCT_OPEN_LIFT * LIFT_EXAGGERATION) / 2;
    case 'k2Hub':
    case 'k2Lining': return (frame.k2Open * DCT_OPEN_LIFT * LIFT_EXAGGERATION) / 2;
    case 'k1Race':
    case 'k1Carrier':
    case 'k1Cage': return -frame.k1Travel;
    case 'k2Race':
    case 'k2Carrier':
    case 'k2Cage': return -frame.k2Travel;
    default: return 0;
  }
}

/** Angle d'une pièce à cette image. Ce qui ne tourne pas reste à zéro. */
export function groupAngle(group: PartGroup, frame: ClutchFrame): number {
  switch (group) {
    case 'crank': return frame.crankAngle;
    case 'mass':
    case 'plate':
    case 'k1Plate':
    case 'k2Plate': return frame.massAngle;
    case 'hub':
    case 'shaft': return frame.hubAngle;
    case 'liningFront':
    case 'liningRear': return frame.liningAngle;
    case 'race':
    case 'k1Race':
    case 'k2Race': return frame.raceAngle;
    case 'cage':
    case 'k1Cage':
    case 'k2Cage': return frame.cageAngle;
    case 'k1Hub':
    case 'k1Shaft':
    case 'k1Lining': return frame.k1Angle;
    case 'k2Hub':
    case 'k2Shaft':
    case 'k2Lining': return frame.k2Angle;
    default: return 0;
  }
}

/**
 * Levier de double embrayage : la butée pousse le bout, le levier bascule autour de son appui.
 * `k1` : plein levier qui tire le plateau K1 par ses tirants ; sinon il pousse directement K2.
 */
export function dctLeverX(c: DctLayout, frame: ClutchFrame, k1: boolean, r: number): number {
  const tipR = k1 ? c.k1LeverTipR : c.k2LeverTipR;
  const pivotR = k1 ? c.k1LeverPivotR : c.k2LeverPivotR;
  const base = k1 ? c.leverPivotX - 2 : c.leverPivotX + 3;
  const travel = k1 ? frame.k1Travel : frame.k2Travel;
  const nose = k1 ? c.k1NoseX : c.k2NoseX;
  const tip = nose - 1.2 - travel;
  const u = (pivotR - r) / (pivotR - tipR);
  return base + u * (tip - base);
}

const part = (id: string, material: PartMaterial, group: PartGroup, ...points: number[]): SectionPart => ({ id, material, group, points });
const rect = (x0: number, x1: number, r0: number, r1: number) => [x0, r0, x0, r1, x1, r1, x1, r0];

/** Un cercle en profil, pour le jonc de pivot. */
export function circlePoints(x: number, r: number, radius: number, sides = 10): number[] {
  const points: number[] = [];
  for (let i = 0; i < sides; i++) {
    const a = (i / sides) * Math.PI * 2;
    points.push(x + Math.cos(a) * radius, r + Math.sin(a) * radius);
  }
  return points;
}

/** Le volant, ou les deux masses d'un bi-masse, et le vilebrequin qui les porte. */
function flywheelParts(l: ClutchLayout): SectionPart[] {
  const d = l.dims;
  const T = l.T;
  const li = d.liningInnerR;
  const parts: SectionPart[] = [];
  const ringId = d.flywheelR - 7.5;
  parts.push(part('crank', 'crank', 'crank', -T - 30, 0, -T - 30, 25, -T - 10, 25, -T - 10, 44, -T, 44, -T, 15, -T - 14, 15, -T - 14, 0));
  // Le palier pilote, dans le bout du vilebrequin : l'arbre primaire y tourne à sa propre vitesse.
  parts.push(part('pilotOuter', 'race', 'crank', ...rect(-T - 12, -T - 2, 12.6, 15)));
  parts.push(part('pilotInner', 'race', l.dct ? 'k1Shaft' : 'shaft', ...rect(-T - 12, -T - 2, 7.5, 9.6)));
  parts.push(part('pilotNeedles', 'steel', 'crank', ...rect(-T - 11, -T - 3, 9.8, 12.4)));
  if (!d.dualMass) {
    parts.push(part('flywheel', 'cast', 'crank',
      -T, 22, -T, ringId, -T + 13, ringId, -T + 13, ringId + 3, 0, ringId + 3,
      0, li - 3, -2, li - 5, -8, li - 5, -8, 22));
    parts.push(part('ringGear', 'ring', 'crank', ...rect(-T, -T + 12, ringId, d.flywheelR)));
    return parts;
  }
  // Masse primaire : deux tôles qui forment le canal des ressorts, boulonnées au vilebrequin.
  const Ra = l.arcR;
  parts.push(part('dmfPrimary', 'sheet', 'crank',
    -T, 20, -T, Ra + 16, -14, Ra + 16, -14, Ra - 12, -17, Ra - 12, -17, Ra + 13, -T + 4, Ra + 13, -T + 4, 26, -T + 8, 26, -T + 8, 28, -T + 20, 28, -T + 20, 24, -T + 4, 24, -T + 4, 20));
  parts.push(part('ringGear', 'ring', 'crank', ...rect(-T + 1, -T + 13, Ra + 16, d.flywheelR)));
  parts.push(part('dmfBearing', 'race', 'crank', ...rect(-T + 8, -T + 20, 28, 36)));
  // Masse secondaire : la face de friction et son col sur le roulement.
  parts.push(part('dmfSecondary', 'cast', 'mass',
    -T + 8, 36, -T + 8, 41, -13, 41, -13, d.flywheelR - 13, 0, d.flywheelR - 13, 0, li - 3, -2, li - 5, -5, li - 5, -5, 36));
  // Le voile : solidaire de la secondaire, ses bras viennent comprimer les ressorts par leurs extrémités.
  parts.push(part('dmfFlange', 'steel', 'mass', ...rect(l.arcX - 1, l.arcX + 1, 41, Ra - 6)));
  return parts;
}

/** Disque : garnitures, plaques de guidage, voile du moyeu et moyeu cannelé. */
function discParts(l: ClutchLayout, lining: { front: PartGroup; rear: PartGroup; hub: PartGroup }, x0: number, suffix: string, bore: number): SectionPart[] {
  const d = l.dims;
  const li = d.liningInnerR;
  const lo = d.liningOuterR;
  const t = l.liningThickness;
  const w = l.discWidth;
  const h = l.hubR;
  return [
    part('liningFront' + suffix, 'friction', lining.front, ...rect(x0, x0 + t, li, lo)),
    part('liningRear' + suffix, 'friction', lining.rear, ...rect(x0 + w - t, x0 + w, li, lo)),
    // La plaque de guidage côté volant se prolonge par les segments, où les garnitures sont rivetées.
    part('retainerFront' + suffix, 'sheet', lining.front,
      x0 - 1.5, h + 1, x0 - 1.5, li - 12, x0 + t - 0.2, li - 8, x0 + t - 0.2, li - 2, x0 + t + 0.2, li - 2, x0 + t + 0.2, li - 8, x0 + 0.2, li - 12, x0 + 0.2, h + 1),
    part('retainerRear' + suffix, 'sheet', lining.front, ...rect(x0 + w - 0.2, x0 + w + 1.5, h + 1, li - 8)),
    part('hubFlange' + suffix, 'steel', lining.hub, ...rect(x0 + 2.2, x0 + 5.2, h - 2, l.springR + 9)),
    part('hub' + suffix, 'steel', lining.hub, x0 - 6, bore, x0 - 6, h - 4, x0 - 4, h, x0 + 14, h, x0 + 16, h - 4, x0 + 16, bore),
  ];
}

/** La butée : bague tournante côté doigts, billes, bague fixe, et son chariot qui coulisse sur le tube-guide. */
export function bearingParts(n: number, tube: number, rotating: PartGroup, carrier: PartGroup, cage: PartGroup, radial = 0, suffix = ''): SectionPart[] {
  const c = BEARING_PITCH_R + radial; // rayon primitif
  const ball = BEARING_BALL / 2;
  const inner = c - ball;
  const outer = c + ball;
  return [
    // Le nez torique, qui roule sur les doigts sans frotter d'un bord : c'est la bague intérieure qui tourne.
    part('innerRace' + suffix, 'race', rotating,
      n + 0.6, inner - 6.8, n, inner - 4, n + 1, inner - 1, n + 4, inner + 0.8, n + 8.6, inner + 0.8, n + 10, inner + 0.3, n + 12, inner, n + 14, inner + 0.3, n + 15.4, inner + 0.8, n + 20, inner + 0.8, n + 20, inner - 6.8),
    part('outerRace' + suffix, 'race', carrier,
      n + 5, outer + 5, n + 20, outer + 5, n + 20, outer - 0.8, n + 15.4, outer - 0.8, n + 14, outer - 0.3, n + 12, outer, n + 10, outer - 0.3, n + 8.6, outer - 0.8, n + 5, outer - 0.8),
    part('cage' + suffix, 'brass', cage, ...rect(n + 6.4, n + 7.8, c - 1.6, c + 1.6)),
    part('cageRear' + suffix, 'brass', cage, ...rect(n + 16.2, n + 17.6, c - 1.6, c + 1.6)),
    part('seal' + suffix, 'rubber', carrier, ...rect(n + 18.6, n + 19.4, inner + 1.2, outer - 1)),
    // Chariot : un manchon sur le tube, un plat pour la fourchette, une coupelle qui garde la bague fixe.
    // Le jeu radial entre la coupelle et la bague, c'est l'auto-centrage : elle se cale sur l'axe du diaphragme.
    part('carrier' + suffix, 'carrier', carrier,
      n + 21.6, tube + 0.2, n + 21.6, outer + 6.6, n + 4, outer + 6.6, n + 4, outer + 2.5, n + 5.4, outer + 2.5, n + 5.4, outer + 5.8, n + 24, outer + 5.8, n + 24, tube + 3, n + 46, tube + 3, n + 46, tube + 0.2),
  ];
}

/** Toutes les pièces de révolution, fixes ou mobiles, dans leur position embrayée. */
export function sectionParts(l: ClutchLayout): SectionPart[] {
  const d = l.dims;
  const parts = flywheelParts(l);
  const lo = d.liningOuterR;
  const li = d.liningInnerR;
  if (!l.dct) {
    parts.push(...discParts(l, { front: 'liningFront', rear: 'liningRear', hub: 'hub' }, 0, '', d.shaftR));
    const x = l.plateX;
    parts.push(part('plate', 'cast', 'plate',
      x, li - 2, x, lo + 1, x + 12.5, lo + 1, x + 15, lo - 1.5, x + 15, d.fulcrumR + 3, x + 18, d.fulcrumR + 1.2, x + 18, d.fulcrumR - 1.2, x + 15, d.fulcrumR - 3, x + 15, li + 2, x + 11, li - 2));
    parts.push(part('cover', 'sheet', 'mass',
      0, l.flangeR, 3, l.flangeR, 3, l.wallOut, l.coverOut - 3, l.wallOut, l.coverOut, l.wallOut - 3, l.coverOut, l.coverOpening, l.coverIn, l.coverOpening, l.coverIn, l.wallIn - 2, l.coverIn - 2, l.wallIn, 0, l.wallIn));
    parts.push(part('pivotFront', 'steel', 'mass', ...circlePoints(l.pivotX - l.sheet / 2 - 1.3, d.pivotR, 1.3)));
    parts.push(part('pivotRear', 'steel', 'mass', ...circlePoints(l.pivotX + l.sheet / 2 + 1.3, d.pivotR, 1.3)));
    parts.push(...bearingParts(l.noseX, l.tubeOut, 'race', 'carrier', 'cage'));
  } else {
    const c = l.dct;
    const front: PartGroup = 'k1Lining';
    parts.push(...discParts(l, { front, rear: front, hub: 'k1Hub' }, c.k1DiscX, 'K1', d.shaftR));
    parts.push(...discParts(l, { front: 'k2Lining', rear: 'k2Lining', hub: 'k2Hub' }, c.k2DiscX, 'K2', c.hollowR));
    parts.push(part('k1Plate', 'cast', 'k1Plate', ...rect(c.k1PlateX, c.k1PlateX + c.plateThickness, li - 2, lo + 1)));
    // Le plateau K1 est tiré, par-dessus tout le reste, vers son levier.
    parts.push(part('k1Tie', 'sheet', 'k1Plate', ...rect(c.k1PlateX + 2, c.leverPivotX - 3, c.tieRodR - 1, c.tieRodR + 1), ));
    parts.push(part('centre', 'cast', 'mass', ...rect(c.centreX, c.centreX + c.centreThickness, li - 2, lo + 11)));
    parts.push(part('k2Plate', 'cast', 'k2Plate', ...rect(c.k2PlateX, c.k2PlateX + c.plateThickness, li - 2, lo + 1)));
    parts.push(part('cover', 'sheet', 'mass',
      -2, lo + 13, c.leverPivotX + 8, lo + 13, c.leverPivotX + 8, 80, c.leverPivotX + 5.5, 80, c.leverPivotX + 5.5, lo + 10.5, -2, lo + 10.5));
    const tube = c.hollowR + 3.5;
    parts.push(...bearingParts(c.k1NoseX, tube, 'k1Race', 'k1Carrier', 'k1Cage', 0, 'K1'));
    parts.push(...bearingParts(c.k2NoseX, 41.6 + 1, 'k2Race', 'k2Carrier', 'k2Cage', 28, 'K2'));
  }

  // L'arbre primaire et le tube-guide, solidaire de la boîte.
  const wall = l.wallX;
  const shaftEnd = wall + 48;
  const nose = -l.T - 13;
  if (!l.dct) {
    parts.push(part('shaft', 'shaft', 'shaft', nose, 0, nose, 7.5, -T_NECK(l), 7.5, -T_NECK(l) + 3, d.shaftR, shaftEnd, d.shaftR, shaftEnd, 0));
  } else {
    parts.push(part('shaft', 'shaft', 'k1Shaft', nose, 0, nose, 7.5, l.dct.k1DiscX - 14, 7.5, l.dct.k1DiscX - 11, d.shaftR, shaftEnd + 12, d.shaftR, shaftEnd + 12, 0));
    parts.push(part('hollowShaft', 'shaft', 'k2Shaft', ...rect(l.dct.k2DiscX - 6, shaftEnd, d.shaftR + 1, l.dct.hollowR)));
  }
  const tubeIn = l.dct ? l.dct.hollowR + 1 : l.tubeIn;
  const tubeOut = l.dct ? l.dct.hollowR + 3.5 : l.tubeOut;
  const guideStart = l.dct ? l.dct.k1NoseX + 8 : l.guideX;
  parts.push(part('guideTube', 'housing', 'static', ...rect(guideStart, wall + 4, tubeIn, tubeOut)));
  // Le voile avant de la boîte, percé pour le roulement de l'arbre.
  const bearingR = d.shaftR + 16;
  parts.push(part('boxWall', 'housing', 'static', ...rect(wall, wall + 12, bearingR, 110)));
  parts.push(part('inputBearing', 'race', 'static', ...rect(wall + 1, wall + 13, d.shaftR + 1, bearingR)));
  return parts;
}

const T_NECK = (l: ClutchLayout) => 12 + (l.dims.dualMass ? 14 : 0);

/** Le carter d'embrayage, découpé en deux morceaux autour de la fenêtre de la fourchette. */
export function bellProfile(l: ClutchLayout): number[] {
  const flange = -l.T - 6;
  const shoulder = l.dct ? l.dct.k1NoseX + 6 : l.coverOut + 8;
  return [flange, l.bellR + 8, flange + 6, l.bellR + 8, flange + 6, l.bellR, shoulder, l.bellR, l.forkX + 9, 124, l.wallX, 110, l.wallX, 106, l.forkX + 9, 120, shoulder, l.bellR - 4, flange, l.bellR - 4];
}

