/**
 * Le gaz dans le moteur et ses tubes : un vrai calcul, en temps physique, à la place de taches dessinées.
 *
 *  - Tubes : écoulement compressible à une dimension, à section variable (Euler quasi-1D, volumes
 *    finis, flux HLLC). La pression y voyage à la vitesse du son, la matière à sa propre vitesse,
 *    et les ondes se réfléchissent là où la section change, comme sur le vrai moteur.
 *  - Cylindres : bilan de masse et d'énergie à zéro dimension (remplissage et vidange), volume du
 *    système bielle-manivelle, combustion de Wiebe, soupapes en écoulement compressible.
 *  - Collecteur, plénum : volumes à zéro dimension où les tubes se rejoignent.
 *
 * Rien ici n'alloue pendant le calcul. Ce modèle sert à l'affichage : il suit l'état de `DrivingModel`
 * (régime, remplissage, coupure, et sur un turbo le rapport de pression, la sortie du compresseur et les soupapes),
 * il ne le remplace pas et n'agit jamais sur le son. Les turbos sont ceux du C++ : un, ou deux (`twin_turbo.cpp`), chacun
 * avec sa turbine au bout de sa rangée et son chemin d'air jusqu'au plénum.
 */
import { architectureOf, type Architecture } from './architecture';
import { ambientAirOf } from './atmosphere';
import { carburetorOf } from './carburetor';
import type { EngineConfig, Exhaust, Layout } from './config';
import { EXHAUST_LINES, TURBO_SCALE } from './exhaustLines';
import { WALL_RISE_K } from './heat';
import { sohcSectionShare, twoValves } from './valves';
import { camShift } from './cam';
import { CYCLE_PER_SHAFT, EXHAUST_PORT_SHARE, firingsPerCycle, INTAKE_PORT_SHARE, isRotary, rotaryCompression, rotaryShape, SHAFT_EVC, SHAFT_EVO, SHAFT_INTAKE_SWEEP, SHAFT_IVC, SHAFT_IVO, SHAFT_PORT_SWEEP, sweptCc, WALL_RATIO } from './rotary';

export const GAMMA = 1.36;
const G1 = GAMMA - 1;
const R = 287;
const CV = R / G1;
const CP = GAMMA * CV;
export const P_ATM = 101325;
export const T_AMB = 293;
const RHO_ATM = P_ATM / (R * T_AMB);
const E_ATM = P_ATM / G1;
/** Taille d'une cellule (m) : 3 cm suffisent pour une bouffée longue de plusieurs dizaines de centimètres. */
const CELL_SMALL = 0.03;
/** À partir de huit cylindres, les collecteurs pèsent lourd dans le calcul : des cellules un peu plus grandes. */
const CELL_LARGE = 0.036;
const CFL = 0.8;
const PRESSURE_FLOOR = 8000;
const DENSITY_FLOOR = 0.02;
const WALL_EVERY = 8;
const POP_SECONDS = 0.005;
/** Au pire (image très longue), on laisse filer le temps du gaz plutôt que de faire décrocher la page. */
const MAX_STEPS_PER_FRAME = 240;

const CRITICAL_RATIO = (2 / (GAMMA + 1)) ** (GAMMA / G1);
const FLOW_FACTOR = (2 * GAMMA) / G1;
const P_EXP_A = 2 / GAMMA;
const P_EXP_B = (GAMMA + 1) / GAMMA;
const COMPRESSOR_EXP = G1 / GAMMA;
const SHOCK_FACTOR = (GAMMA + 1) / (2 * GAMMA);

// Calage : 0° = point mort haut d'allumage, sur 720°.
export const EVO = 135;
export const EVC = 378;
export const IVO = 342;
export const IVC = 590;

/**
 * Le taux de compression du gaz à l'écran : celui d'origine à 0, sinon le réglage, borné à ce qui existe. Recopie `compressionRatioOf`
 * (combustion.cpp) : un diesel sous 14:1 ne s'allume plus, un rotor n'a plus de creux au-delà de 11,9:1. Une erreur ne change que ce qu'on voit.
 */
export function compressionRatioOf(config: EngineConfig, stock: number, low: number, high: number): number {
  const set = config.compressionRatio ?? 0;
  return set > 0 ? Math.min(Math.max(set, low), high) : stock;
}

/** Avance à l'allumage ou à l'injection (degrés avant le point mort haut). */
export function ignitionAdvance(diesel: boolean, rpm: number): number {
  const rpmShare = Math.min(rpm / 7000, 1.4);
  return diesel ? 8 + 8 * rpmShare : 12 + 22 * rpmShare;
}

/** Durée de la combustion (degrés). */
export function burnDegrees(diesel: boolean, rpm: number): number {
  const rpmShare = Math.min(rpm / 7000, 1.4);
  return diesel ? 62 + 18 * rpmShare : 42 + 18 * rpmShare;
}

export type SectionKind = 'tube' | 'cat' | 'res' | 'muffler' | 'tail';

export interface LineSection {
  kind: SectionKind;
  /** Longueur (m). */
  length: number;
  /** Section, rapportée au tube de base. */
  area: number;
  /** Les tubes suivent la longueur de la voiture, pas les volumes. */
  stretches: boolean;
}

/**
 * Géométrie des lignes, lue dans `EXHAUST_LINES`. Le collecteur garde sa longueur, les tubes suivent celle de
 * la voiture ; une sortie qui suit un volume garde aussi la sienne, celle d'une ligne sans silencieux s'allonge.
 */
function sectionsOf(exhaust: Exhaust): LineSection[] {
  const parts = EXHAUST_LINES[exhaust];
  const muffled = parts.some((item) => item.part === 'muffler');
  return parts.map(({ part, length, area }, i) => {
    if (part === 'cat') return { kind: 'cat', length, area, stretches: false };
    if (part === 'resonator') return { kind: 'res', length, area, stretches: false };
    if (part === 'muffler') return { kind: 'muffler', length, area, stretches: false };
    if (i === parts.length - 1) return { kind: 'tail', length, area, stretches: !muffled };
    return { kind: 'tube', length, area, stretches: i > 0 };
  });
}

export const LINE_SECTIONS = Object.fromEntries((Object.keys(EXHAUST_LINES) as Exhaust[]).map((exhaust) => [exhaust, sectionsOf(exhaust)])) as Record<Exhaust, LineSection[]>;

/** Longueur de ligne de chaque carrosserie : recopie de `BodySpec::exhaustScale`. */
const LINE_SCALE: Record<EngineConfig['body'], number> = { city: 0.85, compact: 1, coupe: 1.1, supercar: 0.45, van: 1.25, retro: 0.98 };

/** Un tronçon placé sur la ligne. */
export interface PlacedSection extends LineSection {
  /** Première cellule, et leur nombre. */
  first: number;
  count: number;
  /** Position sur la ligne (m). */
  from: number;
  to: number;
  /** Diamètre (m). */
  diameter: number;
}

/** Ce que le gaz reçoit du modèle de conduite à chaque image. */
export interface GasInputs {
  rpm: number;
  load: number;
  /** Pression du turbo, 0 à 1 de la pression réglée. */
  boost: number;
  /** Décharge du turbo en cours. */
  release: number;
  /** La combustion a lieu : faux à la coupure d'injection, au rupteur, au démarreur. */
  burning: boolean;
  /** Température de la ligne, 0 à 1. */
  heat: number;
  /** Rapport de pression du compresseur, tel que le calcule `TurboCharger`. Absent : déduit de `boost`. */
  compressorRatio?: number;
  /** Sortie du compresseur (K), selon le même calcul. Absent : déduite du rapport et d'un rendement de 72 %. */
  compressorOutK?: number;
  /** Ouverture de la wastegate, 0 à 1. Absente : déduite de `boost`. */
  wastegate?: number;
  /** L'anti-lag en marche, tel que le calcule `TurboCharger` : `ANTI_LAG_CODE`, 0 sinon. */
  antiLag?: number;
  /** Chaleur libérée au collecteur (W), et air qu'il y reçoit (kg/s). */
  antiLagHeatW?: number;
  antiLagAirFlow?: number;
  /** Cycles de deux tours parcourus depuis le début, sans repli : un rotatif en a besoin, son cycle dure trois tours. */
  turnsTotal?: number;
  /**
   * Deux turbos (`twin_turbo.cpp`) : rapport de pression du second compresseur, ouverture de sa soupape, vanne d'échappement du
   * secondaire d'un séquentiel (0 fermée, 1 ouverte) et son clapet d'admission. Absents : les mêmes que le premier, tout ouvert.
   */
  compressorRatio2?: number;
  wastegate2?: number;
  twinValve?: number;
  secondaryOnline?: boolean;
}

/**
 * Les entrées d'une image, recopiées une fois dans un objet de forme fixe : les pas de calcul les relisent une centaine
 * de fois par image. Lu directement sur des objets de formes diverses, chaque nombre était recopié dans un neuf :
 * 3 ko par image de plus, dès que l'appelant avait passé quelques formes différentes.
 */
class FrameInputs implements GasInputs {
  rpm = 0;
  load = 0;
  boost = 0;
  release = 0;
  burning = false;
  heat = 0;
  compressorRatio: number | undefined = undefined;
  compressorOutK: number | undefined = undefined;
  wastegate: number | undefined = undefined;
  antiLag: number | undefined = undefined;
  antiLagHeatW: number | undefined = undefined;
  antiLagAirFlow: number | undefined = undefined;
  turnsTotal: number | undefined = undefined;
  compressorRatio2: number | undefined = undefined;
  wastegate2: number | undefined = undefined;
  twinValve: number | undefined = undefined;
  secondaryOnline: boolean | undefined = undefined;

  copy(from: GasInputs): this {
    this.rpm = from.rpm;
    this.load = from.load;
    this.boost = from.boost;
    this.release = from.release;
    this.burning = from.burning;
    this.heat = from.heat;
    this.compressorRatio = from.compressorRatio;
    this.compressorOutK = from.compressorOutK;
    this.wastegate = from.wastegate;
    this.antiLag = from.antiLag;
    this.antiLagHeatW = from.antiLagHeatW;
    this.antiLagAirFlow = from.antiLagAirFlow;
    this.turnsTotal = from.turnsTotal;
    this.compressorRatio2 = from.compressorRatio2;
    this.wastegate2 = from.wastegate2;
    this.twinValve = from.twinValve;
    this.secondaryOnline = from.secondaryOnline;
    return this;
  }
}

/** Le même ordre que `AntiLag` en C++. */
export const ANTI_LAG_CODE = { off: 0, retard: 1, air: 2, cut: 3 } as const;
/** Allumage après le point mort haut (degrés), comme en C++ : 55° et 40° de retard sur un allumage normal, autour de 20° avant. */
const ANTI_LAG_LATE_RETARD = 35;
const ANTI_LAG_LATE_AIR = 20;
/** Le rythme de la coupure, dans l'ordre d'allumage : même motif que `antiLagFires` en C++. */
export function antiLagFires(event: number): boolean {
  return ((event % 5) * 2) % 5 < 2;
}

export type IntakePartKind = 'duct' | 'filter' | 'inlet' | 'hot' | 'cooler' | 'cold' | 'venturi';

/** Un tronçon de la chaîne d'admission : du filtre au papillon, dans le sens de l'air. */
export interface IntakePart {
  kind: IntakePartKind;
  first: number;
  count: number;
  /** Section, rapportée au conduit. */
  area: number;
}

/** Le chemin de l'air d'un turbo, dimensionné pour 2 L : longueur (m), section rapportée au conduit, frottement (1/s). */
const TURBO_INTAKE: { kind: IntakePartKind; length: number; area: number; drag: number; wall: number }[] = [
  // Boîte à air et cartouche : un grand volume, et presque toute la perte d'avant le compresseur.
  { kind: 'filter', length: 0.24, area: 8, drag: 1100, wall: 0.1 },
  // Conduit d'entrée, avec le débitmètre d'air : rien n'y arrive d'autre que de l'air frais.
  { kind: 'inlet', length: 0.35, area: 1, drag: 0, wall: 0.1 },
  // Volute du compresseur et tube chaud : l'air y arrive comprimé et à plus de 100 °C.
  { kind: 'hot', length: 0.45, area: 1, drag: 0, wall: 0.1 },
  // L'échangeur : un grand volume qui freine et refroidit. Sa perte de 0,1 à 0,2 bar au point nominal se règle ici.
  { kind: 'cooler', length: 0.45, area: 4, drag: 1500, wall: 3 },
  // Tube froid, jusqu'au papillon : c'est là que s'ouvre la décharge.
  { kind: 'cold', length: 0.5, area: 1, drag: 0, wall: 0.1 },
];

/**
 * Ce que le filtre change au gaz à l'écran. `drop` recopie `airFilterDropShare` (core/src/air_filter.cpp), et multiplie le
 * frottement ; `mouth`, la section de la boîte sur un turbo (la section de `TURBO_INTAKE` en vaut 8) : sans boîte, l'air entre
 * par une petite bouche. `duct`, la longueur du conduit sans turbo, boîte et prise d'air comprises ; `bay`, la part de
 * l'échauffement du compartiment. Le volume entre le compresseur et le papillon ne bouge pas : voir `chargeVolumeM3`.
 */
const AIR_FILTER_GAS: Record<NonNullable<EngineConfig['airFilter']>, { drop: number; mouth: number; duct: number; bay: number }> = {
  stock: { drop: 1, mouth: 8, duct: 0.7, bay: 0 },
  panel: { drop: 2550 / 3000, mouth: 8, duct: 0.7, bay: 0 },
  cone: { drop: 1210 / 3000, mouth: 3, duct: 0.45, bay: 1 },
  stacks: { drop: 200 / 3000, mouth: 1.4, duct: 0.3, bay: 1 },
};
/** L'échauffement du compartiment au point nominal (K) : recopie `kBayRiseFrontK` et ses voisines. */
const BAY_RISE_K: Record<'front' | 'mid' | 'rear', number> = { front: 12, mid: 20, rear: 16 };
/**
 * Le chemin de l'air d'un compresseur mécanique : mêmes pièces, en plus court, le compresseur étant posé sur le collecteur. Même volume
 * que `chargeVolumeM3` en C++ (2,6 L sur 2 L), et une perte de l'échangeur de l'ordre de `kRatedCoolerBar` (0,05 bar).
 */
const CHARGER_INTAKE: typeof TURBO_INTAKE = [
  { kind: 'filter', length: 0.24, area: 8, drag: 1100, wall: 0.1 },
  { kind: 'inlet', length: 0.25, area: 1, drag: 0, wall: 0.1 },
  { kind: 'hot', length: 0.2, area: 1, drag: 0, wall: 0.1 },
  { kind: 'cooler', length: 0.2, area: 4, drag: 1400, wall: 3 },
  { kind: 'cold', length: 0.2, area: 1, drag: 0, wall: 0.1 },
];

/**
 * Les autres échangeurs : recopie de `ChargePath` (core/src/charge_air.cpp), mêmes longueurs, même volume rapporté à celui de
 * l'air-air d'origine. Air-eau sur un turbo : une brique près du papillon, la moitié de la perte du faisceau, un tube froid court.
 * Aucun : un seul tube du compresseur au papillon. Sur un compresseur, l'air-eau garde le chemin d'origine, déjà court.
 */
const TURBO_INTAKE_WATER: typeof TURBO_INTAKE = [
  TURBO_INTAKE[0],
  TURBO_INTAKE[1],
  TURBO_INTAKE[2],
  { kind: 'cooler', length: 0.25, area: 4, drag: 750, wall: 3 },
  { kind: 'cold', length: 0.2, area: 1, drag: 0, wall: 0.1 },
];
const TURBO_INTAKE_NONE: typeof TURBO_INTAKE = [TURBO_INTAKE[0], TURBO_INTAKE[1], TURBO_INTAKE[2], { kind: 'cold', length: 0.35, area: 1, drag: 0, wall: 0.1 }];
const CHARGER_INTAKE_NONE: typeof TURBO_INTAKE = [CHARGER_INTAKE[0], CHARGER_INTAKE[1], CHARGER_INTAKE[2], CHARGER_INTAKE[4]];

/** Le chemin de l'air entre le filtre et le papillon, selon la machine et l'échangeur. */
export function intakeChainOf(config: Pick<EngineConfig, 'aspiration' | 'intercooler'>): typeof TURBO_INTAKE | null {
  const cooler = config.intercooler ?? 'air_to_air';
  if (config.aspiration === 'turbo') return cooler === 'none' ? TURBO_INTAKE_NONE : cooler === 'air_to_water' ? TURBO_INTAKE_WATER : TURBO_INTAKE;
  if (config.aspiration === 'supercharger') return cooler === 'none' ? CHARGER_INTAKE_NONE : CHARGER_INTAKE;
  return null;
}

/**
 * Longueur de chaque collecteur (m) : mêmes ordres de grandeur que `EngineConfig::toProfile`. Un VR, un W et plus de douze
 * cylindres passent par leur architecture, comme `applyArchitecture` : la culasse, puis la rangée loin de l'échappement, dont le
 * conduit traverse la culasse de l'écart des deux rangées au plan de joint (`rowGap`).
 */
function headerLength(layout: Layout, cylinder: number, architecture: Architecture | null, rowGap: number): number {
  if (architecture) {
    const head = architecture.heads === 2 ? architecture.head[cylinder] : 0;
    const base = architecture.heads === 2 ? (head === 0 ? 0.55 : 0.72) : 0.62;
    if (architecture.rowAngleDeg === 0) return base;
    // L'échappement sort à droite d'une culasse seule, à l'extérieur d'un W.
    const exhaustSide = architecture.heads === 2 ? (head === 0 ? -1 : 1) : 1;
    const rowSide = architecture.row[cylinder] === 0 ? -1 : 1;
    return rowSide === exhaustSide ? base : base + rowGap;
  }
  switch (layout) {
    // Une lumière par rotor, tout de suite un court tube : `headerM` de `applyRotary`.
    case 'rotary': return 0.42;
    case 'inline': return 0.62;
    case 'v': return cylinder % 2 === 0 ? 0.55 : 0.72;
    case 'cross_plane': return cylinder % 2 === 0 ? 0.5 : 0.86;
    case 'boxer': return cylinder % 2 === 0 ? 0.48 : 1.05;
    case 'vr': return 0.62;
    case 'w': return cylinder % 2 === 0 ? 0.55 : 0.72;
  }
}

// Entrées et sorties des petits calculs : des tableaux. Passer un nombre à virgule à une fonction que le
// compilateur ne déplie pas l'alloue, et ces fonctions sont appelées des milliers de fois par image.
const riemann = new Float64Array(10);
const flux = new Float64Array(3);
const orifice = new Float64Array(4);
const reservoir = new Float64Array(2);

/**
 * Plus la pression est haute, plus l'étranglement laisse passer : sonique au col, puis ça ne monte plus.
 * Entrée `orifice` : pression amont, température amont, pression aval. Sortie `orifice[3]` : kg/s par m².
 */
function orificeFlux(): void {
  const pUp = orifice[0];
  let ratio = orifice[2] / pUp;
  if (ratio < CRITICAL_RATIO) ratio = CRITICAL_RATIO;
  if (ratio >= 1) {
    orifice[3] = 0;
    return;
  }
  const term = ratio ** P_EXP_A - ratio ** P_EXP_B;
  orifice[3] = (pUp / Math.sqrt(R * orifice[1])) * Math.sqrt(FLOW_FACTOR * (term > 0 ? term : 0));
}

/**
 * Flux HLLC entre deux états (`riemann` : densité, vitesse, pression, son, énergie, à gauche puis à droite) :
 * il garde net le contact entre gaz chaud et gaz froid. Vitesses d'onde selon la pression (Toro) :
 * l'estimation simple rate les chocs forts, et laissait la fin d'une ligne directe sortir en
 * supersonique sans jamais sentir l'air extérieur.
 */
function hllc(): void {
  const rL = riemann[0];
  const uL = riemann[1];
  const pL = riemann[2];
  const cL = riemann[3];
  const eL = riemann[4];
  const rR = riemann[5];
  const uR = riemann[6];
  const pR = riemann[7];
  const cR = riemann[8];
  const eR = riemann[9];
  const pStar = 0.5 * (pL + pR) - 0.125 * (uR - uL) * (rL + rR) * (cL + cR);
  const qL = pStar > pL ? Math.sqrt(1 + SHOCK_FACTOR * (pStar / pL - 1)) : 1;
  const qR = pStar > pR ? Math.sqrt(1 + SHOCK_FACTOR * (pStar / pR - 1)) : 1;
  const sL = uL - cL * qL;
  const sR = uR + cR * qR;
  if (sL >= 0) {
    flux[0] = rL * uL;
    flux[1] = rL * uL * uL + pL;
    flux[2] = uL * (eL + pL);
    return;
  }
  if (sR <= 0) {
    flux[0] = rR * uR;
    flux[1] = rR * uR * uR + pR;
    flux[2] = uR * (eR + pR);
    return;
  }
  const dL = rL * (sL - uL);
  const dR = rR * (sR - uR);
  const sM = (pR - pL + dL * uL - dR * uR) / (dL - dR);
  if (sM >= 0) {
    const rS = dL / (sL - sM);
    const eS = rS * (eL / rL + (sM - uL) * (sM + pL / dL));
    flux[0] = rL * uL + sL * (rS - rL);
    flux[1] = rL * uL * uL + pL + sL * (rS * sM - rL * uL);
    flux[2] = uL * (eL + pL) + sL * (eS - eL);
  } else {
    const rS = dR / (sR - sM);
    const eS = rS * (eR / rR + (sM - uR) * (sM + pR / dR));
    flux[0] = rR * uR + sR * (rS - rR);
    flux[1] = rR * uR * uR + pR + sR * (rS * sM - rR * uR);
    flux[2] = uR * (eR + pR) + sR * (eS - eR);
  }
}

/** Le réseau de gaz d'un moteur : cylindres, collecteurs, ligne, admission. */
export class GasNetwork {
  // ── Cellules des tubes, toutes à la suite ──
  n = 0;
  rho = new Float64Array(0);
  mom = new Float64Array(0);
  ene = new Float64Array(0);
  trc = new Float64Array(0);
  /** Densité et énergie figées au début du pas : tous les flux partent du même état. */
  private rho0 = new Float64Array(0);
  private ene0 = new Float64Array(0);
  /** Pression, vitesse, célérité du son, part de gaz brûlé, température : recalculés à chaque pas. */
  p = new Float64Array(0);
  u = new Float64Array(0);
  c = new Float64Array(0);
  y = new Float64Array(0);
  temp = new Float64Array(0);
  area = new Float64Array(0);
  invV = new Float64Array(0);
  /** Section de la face entre i et i+1 ; 0 quand il n'y a pas de passage. */
  private faceA = new Float64Array(0);
  /** Différence de section de part et d'autre de la cellule : la force de pression sur les parois. */
  private dA = new Float64Array(0);
  private dragRate = new Float64Array(0);
  private wallRate = new Float64Array(0);
  private wallT = new Float64Array(0);
  private wallDecay = new Float64Array(0);
  private dragCells = new Int32Array(0);
  /** Faces où la section change d'un coup : elles ont leur propre calcul. */
  private stepFaces = new Int32Array(0);

  // ── Volumes à zéro dimension ──
  zoneCount = 0;
  zM = new Float64Array(0);
  zE = new Float64Array(0);
  zY = new Float64Array(0);
  zV = new Float64Array(0);
  zP = new Float64Array(0);
  zT = new Float64Array(0);
  // Tube relié à un volume : cellule, volume, section, côté (+1 : le volume est après la cellule).
  private linkCell = new Int32Array(0);
  private linkZone = new Int32Array(0);
  private linkSide = new Int8Array(0);
  private linkArea = new Float64Array(0);

  // ── Cylindres ──
  cylinders = 0;
  bank = new Int8Array(0);
  /** Avance de phase de chaque cylindre dans le cycle (0 à 1). */
  private phase = new Float64Array(0);
  cylM = new Float64Array(0);
  cylU = new Float64Array(0);
  cylY = new Float64Array(0);
  cylV = new Float64Array(0);
  cylP = new Float64Array(0);
  cylT = new Float64Array(0);
  /** Part de carburant déjà brûlée dans ce cycle (Wiebe). */
  cylBurn = new Float64Array(0);
  /** Débit par la soupape d'échappement (kg/s, sortant) et d'admission (kg/s, entrant). */
  cylExhaust = new Float64Array(0);
  cylIntake = new Float64Array(0);
  /** Levée des soupapes, 0 à 1. */
  cylLiftEx = new Float64Array(0);
  cylLiftIn = new Float64Array(0);
  /** Angle dans le cycle (degrés, sur 720). */
  cylTheta = new Float64Array(0);
  /** Le cylindre a allumé dans ce cycle. */
  cylFired = new Uint8Array(0);
  private cylHeat = new Float64Array(0);
  private cylArmed = new Uint8Array(0);
  private cylWasOpen = new Uint8Array(0);
  private runnerFirst = new Int32Array(0);
  /** Nombre de cellules de chaque tube de collecteur. */
  runnerCells = new Int32Array(0);
  /** Ouvertures de soupape d'échappement depuis le début : une par explosion, dans l'ordre d'allumage. */
  blowdowns = 0;
  burntBlowdowns = 0;
  /** L'air que l'anti-lag fait passer au collecteur en ce moment (kg/s). */
  antiLagAirNow = 0;
  lastBlowdownCylinder = 0;

  /** Tubes de collecteur : un par cylindre, ou un par rotor, ses trois chambres passant par la même lumière. */
  runners = 0;
  private rotary = false;
  /** Sur un rotatif, le cycle d'une chambre dure trois tours d'arbre, et non deux : 2/3, 1 sinon. */
  private cycleScale = 1;
  private advanceScale = 1;
  /** Explosions par cycle de deux tours. */
  private firings = 0;
  /** Calage du moteur, en degrés de son cycle : soupapes ou lumières. */
  private evo = EVO;
  private evc = EVC;
  private ivo = IVO;
  private ivc = IVC;
  /** Balayage d'une lumière par le sommet du rotor (degrés de cycle) ; 0 : une soupape. */
  private sweepEx = 0;
  private sweepIn = 0;
  private wallGain = 14;

  // ── Géométrie ──
  private boreArea = 0;
  private crank = 0;
  private rod = 0;
  private clearance = 0;
  private exhaustValve = 0;
  private intakeValve = 0;
  private diesel = false;
  /** Pas de turbine sur la ligne : un compresseur prend sa puissance au vilebrequin. */
  get hasTurbine(): boolean {
    return this.turbo;
  }
  /** Deux turbines, deux chemins d'air. */
  get hasTwin(): boolean {
    return this.twin;
  }
  /** Le carburant de ce moteur : un diesel en charge rejette des suies. */
  get isDiesel(): boolean {
    return this.diesel;
  }
  private turbo = false;
  /** Un compresseur mécanique : le chemin de l'air d'un turbo, sans turbine ni décharge. */
  private charger = false;
  private airChain: typeof TURBO_INTAKE | null = null;
  /**
   * Deux turbos : une turbine au bout de chaque rangée (un moteur en ligne est coupé en deux, avant et arrière), deux chemins
   * d'air, chacun avec son compresseur, jusqu'au plénum. En séquentiel, un tube relie les deux collecteurs, et la vanne du
   * secondaire règle ce qui passe par sa turbine.
   */
  private twin = false;
  private sequential = false;
  /** Coupé en avant et arrière : un moteur en ligne à deux turbos. */
  frontRear = false;
  private flutter = false;
  private antiLagEvent = 0;
  private boostBar = 0;

  /** Taille d'une cellule (m). */
  cell = CELL_SMALL;
  /** Diamètre du tube de base (m). */
  pipeDiameter = 0.05;
  /** Rangées qui se rejoignent. */
  banks = 1;
  /** Tronçons de la ligne, de la sortie du collecteur à l'air libre. */
  sections: PlacedSection[] = [];
  lineLength = 0;
  /** Première cellule de la ligne commune, et leur nombre. */
  lineFirst = 0;
  lineCount = 0;
  /** Tubes qui joignent chaque rangée à la ligne commune : première cellule. */
  bankFirst = new Int32Array(0);
  bankCells = 0;
  /** Déporté au-delà de 400 mm, le tube jusqu'à la turbine : première cellule, et leur nombre (0 à la référence ou plus court). */
  pipeFirst = 0;
  pipeCells = 0;
  /** Conduit d'admission : du filtre au papillon, compresseur compris. */
  intakeFirst = 0;
  intakeCount = 0;
  /** Cellule juste après le compresseur (turbo seulement) ; -1 sinon. */
  compressorCell = -1;
  /** Cellule où s'ouvre la décharge du turbo ; -1 sinon. */
  releaseCell = -1;
  /** Les tronçons de l'admission, du filtre au papillon. Sans turbo, un seul conduit. */
  intakeParts: IntakePart[] = [];
  /** Échangeur : première cellule, et leur nombre. */
  coolerFirst = -1;
  coolerCount = 0;
  /** Le second chemin d'air d'un biturbo : ses cellules, ses tronçons, son compresseur ; vide sinon. */
  intake2First = 0;
  intake2Count = 0;
  intakeParts2: IntakePart[] = [];
  compressor2Cell = -1;
  /** Numéros des volumes : jonction (ou unique collecteur), plénum. */
  zoneJoin = 0;
  zonePlenum = 0;
  /** Le volume juste avant la turbine : la jonction elle-même, sauf si un tube déporté les sépare. */
  zoneTurbine = 0;

  // ── Organes ──
  private turbineArea = 0;
  /** Séquentiel : la section du tube qui relie les deux collecteurs (m²). */
  private crossArea = 0;
  private throttleMax = 0;
  private releaseArea = 0;
  /** Débit moyen demandé par les cylindres (kg/s), pour doser le papillon. */
  private demand = 0;
  /** Ouverture du papillon (0 à 1), débit à travers lui, à travers la turbine, la décharge, le compresseur (kg/s). */
  throttleShare = 0;
  throttleFlow = 0;
  turbineFlow = 0;
  wastegateFlow = 0;
  releaseFlow = 0;
  compressorFlow = 0;
  /** Puissance reprise par la turbine (W). */
  turbinePower = 0;
  /** Le second turbo d'un biturbo : débits de sa turbine, de sa soupape, de son compresseur, de son papillon (kg/s), puissance (W). */
  turbineFlow2 = 0;
  wastegateFlow2 = 0;
  compressorFlow2 = 0;
  throttleFlow2 = 0;
  throttleShare2 = 0;
  turbinePower2 = 0;
  /** Ce qui passe d'un collecteur à l'autre, en séquentiel (kg/s, du premier vers le second). */
  crossFlow = 0;
  /** État à l'air libre, pour le panache. */
  exitU = 0;
  exitT = T_AMB;
  exitY = 0;
  exitP = P_ATM;
  exitArea = 1e-3;

  /** Détonation en cours dans la fin de ligne : énergie restante (J) et durée restante (s). */
  private popEnergy = 0;
  private popLeft = 0;
  private popFirst = 0;
  private popCount = 0;

  private substeps = 0;
  private wallClock = 0;
  // État du pas en cours : des champs plutôt que des arguments, pour ne rien allouer.
  private dt = 0;
  private fastest = 1;
  private cycleNow = 0;
  private turnRate = 0;
  private boostNow = 1;
  private boost2Now = 1;
  private outKNow = 0;
  /** Débit voulu à travers les deux papillons d'un biturbo (kg/s), pour le pas en cours. */
  private throttleWant = 0;
  /** La densité de l'air à l'entrée de l'admission : plus faible sous le capot. */
  private inletRho = RHO_ATM;
  /** Sa température (K), pour qui la montre. */
  inletK = T_AMB;
  /**
   * L'air du jour (`atmosphere.ts`) : pression (Pa) et température (K) de l'atmosphère, sa masse volumique, son énergie par
   * volume, et le rapport qui ramène une pression relative du turbo à l'atmosphère du jour. Exactement `P_ATM`, `T_AMB`… à 20 °C, 0 m.
   */
  pAtm = P_ATM;
  tAmb = T_AMB;
  private rhoAtm = RHO_ATM;
  private eAtm = E_ATM;
  private boostScale = 1;
  /** Pas de calcul faits à la dernière image. */
  lastSteps = 0;
  /** Temps physique simulé depuis la dernière construction (s). */
  time = 0;
  /** Les entrées de l'image en cours : voir `FrameInputs`. */
  private readonly frame = new FrameInputs();

  /** (Re)construit le réseau pour ce moteur. `order` : ordre d'allumage, cylindres numérotés depuis 1. */
  build(config: EngineConfig, order: number[]): void {
    const rotary = isRotary(config);
    this.rotary = rotary;
    // Sur un rotatif, une « cylindre » du calcul est une chambre : trois par rotor, sur un cycle de trois tours d'arbre.
    const count = rotary ? 3 * config.cylinders : config.cylinders;
    const runners = rotary ? config.cylinders : count;
    this.runners = runners;
    this.firings = firingsPerCycle(config);
    // Tubes, plénum et turbine se dimensionnent sur l'air avalé en deux tours d'arbre.
    const litres = sweptCc(config) / 1000;
    const volume = litres * 1e-3;
    this.pipeDiameter = Math.min(Math.max(0.05 * Math.sqrt(litres / 2), 0.032), 0.09);
    const base = (Math.PI * this.pipeDiameter * this.pipeDiameter) / 4;
    this.cylinders = count;
    const CELL_M = count >= 8 ? CELL_LARGE : CELL_SMALL;
    this.cell = CELL_M;
    this.turbo = config.aspiration === 'turbo';
    this.charger = config.aspiration === 'supercharger';
    this.airChain = this.turbo || this.charger ? intakeChainOf(config) : null;
    this.twin = this.turbo && (config.turboLayout === 'twin_parallel' || config.turboLayout === 'twin_sequential');
    this.sequential = this.twin && config.turboLayout === 'twin_sequential';
    this.diesel = config.fuel === 'diesel' && !rotary;
    this.flutter = config.turboRelease === 'flutter';
    this.boostBar = config.boostBar;
    // Les culasses : une en ligne et en VR, deux en V, en W et à plat. Chaque cylindre souffle dans le collecteur de la sienne.
    const architecture = rotary ? null : architectureOf(config);
    this.banks = architecture && architecture.heads === 2 && count >= 4 ? 2 : 1;
    // Deux turbos : deux collecteurs, quoi qu'il en soit. Un moteur en ligne se coupe en avant et arrière, comme en C++.
    if (this.twin && runners >= 2) this.banks = 2;
    // Une seule culasse (en ligne, VR) se coupe en avant et arrière, comme en C++ ; deux culasses gardent la leur.
    this.frontRear = this.twin && this.banks === 2 && (architecture ? architecture.heads === 1 : config.layout === 'inline');
    // Un VR, un W, plus de douze cylindres : leurs collecteurs suivent leur architecture (`applyArchitecture`).
    const own = architecture && (config.layout === 'vr' || config.layout === 'w' || count > 12) ? architecture : null;
    // L'air du jour : le tube s'ouvre sur lui, l'admission l'avale, la soupape du turbo tient sa pression au-dessus de lui.
    const air = ambientAirOf(config);
    this.pAtm = air.pressurePa;
    this.tAmb = air.temperatureK;
    this.rhoAtm = this.pAtm / (R * this.tAmb);
    this.eAtm = this.pAtm / G1;
    this.boostScale = P_ATM / this.pAtm;
    const filter = AIR_FILTER_GAS[config.airFilter ?? 'stock'];
    const engineAt = config.placement ?? (config.body === 'supercar' ? 'mid' : 'front');
    this.inletK = this.tAmb + filter.bay * BAY_RISE_K[engineAt];
    this.inletRho = (this.rhoAtm * this.tAmb) / this.inletK;
    const carb = carburetorOf(config);

    // Géométrie du cylindre.
    const bore = config.boreMm / 1000;
    const cylinderVolume = volume / count;
    this.boreArea = (Math.PI * bore * bore) / 4;
    const stroke = cylinderVolume / this.boreArea;
    this.crank = stroke / 2;
    this.rod = stroke * 1.6;
    const ratio = compressionRatioOf(config, this.diesel ? 18 : this.airChain ? 9.5 : 10.5, this.diesel ? 14 : 6, 24);
    this.clearance = cylinderVolume / (ratio - 1);
    this.exhaustValve = 0.19 * this.boreArea;
    this.intakeValve = 0.24 * this.boreArea;
    if (twoValves(config.camshaft ?? 'dohc') && !rotary) {
      // Deux soupapes au lieu de quatre, sous un simple arbre comme sous des culbuteurs : le même calcul de section que le C++ (`valves.ts`).
      const share = sohcSectionShare(config.boreMm);
      this.exhaustValve *= share.exhaust;
      this.intakeValve *= share.intake;
    }
    this.cycleScale = 1;
    this.advanceScale = 1;
    // La came : ce qu'elle change au calage d'origine, lu à 1 mm (cam.ts). Rien sur la came d'origine.
    const cam = camShift(config);
    this.evo = EVO + cam.evo;
    this.evc = EVC + cam.evc;
    this.ivo = IVO + cam.ivo;
    this.ivc = IVC + cam.ivc;
    this.sweepEx = 0;
    this.sweepIn = 0;
    this.wallGain = 14;
    if (rotary) {
      // Le volume d'une chambre est V₀ + V/2·(1 − cos(2A/3)) : en degrés de cycle, la loi d'un piston à bielle infinie.
      const shape = rotaryShape(config.displacementCc / config.cylinders);
      const chamber = (config.displacementCc / config.cylinders) * 1e-6;
      const fakeStroke = 2 * shape.eccentricity * 1e-3;
      this.boreArea = chamber / fakeStroke;
      this.crank = fakeStroke / 2;
      this.rod = fakeStroke * 5000;
      this.clearance = chamber / (compressionRatioOf(config, rotaryCompression(config.aspiration), 6, 11.9) - 1);
      const across = shape.radius * shape.width * 1e-6;
      this.exhaustValve = EXHAUST_PORT_SHARE * across;
      this.intakeValve = INTAKE_PORT_SHARE * across;
      this.cycleScale = CYCLE_PER_SHAFT;
      this.advanceScale = CYCLE_PER_SHAFT;
      this.evo = SHAFT_EVO * CYCLE_PER_SHAFT;
      this.evc = SHAFT_EVC * CYCLE_PER_SHAFT;
      this.ivo = SHAFT_IVO * CYCLE_PER_SHAFT;
      this.ivc = SHAFT_IVC * CYCLE_PER_SHAFT;
      this.sweepEx = SHAFT_PORT_SWEEP * CYCLE_PER_SHAFT;
      this.sweepIn = SHAFT_INTAKE_SWEEP * CYCLE_PER_SHAFT;
      // La chambre allongée a presque deux fois plus de paroi pour le même volume.
      this.wallGain = 14 * WALL_RATIO;
    }

    // Collecteurs : un tube par cylindre, plus fins que la ligne qu'ils alimentent.
    const runnerArea = base * Math.min(1, 1.15 / Math.sqrt(runners));
    const runnerCells = new Int32Array(runners);
    let cells = 0;
    // L'écart des deux rangées d'une culasse au plan de joint, comme `applyArchitecture` : bielle de 1,6 course, désaxage compris.
    const halfRow = own ? (own.rowAngleDeg / 2) * (Math.PI / 180) : 0;
    const rowGap = own ? 2 * ((2.1 * stroke + 0.36 * bore) * Math.sin(halfRow) + (own.rowOffsetMm / 1000) * Math.cos(halfRow)) : 0;
    for (let i = 0; i < runners; i++) {
      runnerCells[i] = Math.max(8, Math.round(headerLength(config.layout, i, own, rowGap) / CELL_M));
      cells += runnerCells[i];
    }
    this.runnerCells = runnerCells;

    // Les rangées d'un V se rejoignent après un court tube.
    const bankCells = this.banks === 2 ? Math.round(0.4 / CELL_M) : 0;
    this.bankCells = bankCells;
    const bankFirst = new Int32Array(this.banks);
    const bankStart = cells;
    for (let b = 0; b < this.banks; b++) bankFirst[b] = bankStart + b * bankCells;
    this.bankFirst = bankFirst;
    cells += this.banks === 2 ? 2 * bankCells : 0;

    // La ligne commune, à l'échelle de la voiture.
    const scale = LINE_SCALE[config.body] ?? 1;
    const lineFirst = cells;
    const placed: PlacedSection[] = [];
    let at = 0;
    for (const section of LINE_SECTIONS[config.exhaust]) {
      const length = section.length * (section.stretches ? scale : 1);
      const n = Math.max(3, Math.round(length / CELL_M));
      placed.push({ ...section, length, first: cells, count: n, from: at, to: at + n * CELL_M, diameter: this.pipeDiameter * Math.sqrt(section.area) });
      cells += n;
      at += n * CELL_M;
    }
    this.sections = placed;
    this.lineFirst = lineFirst;
    this.lineCount = cells - lineFirst;
    this.lineLength = at;

    // Ce que le tube a en plus de la référence : le gaz le parcourt, et le compresseur part avec la turbine.
    const pipeExtra = this.turbo && !this.twin ? ((config.turboPipeMm ?? 400) - 400) / 1000 : 0;

    // Admission : un simple conduit sans turbo ; avec, la vraie chaîne, coupée par le compresseur.
    const intakeFirst = cells;
    // Deux chemins d'air, chacun de la section qui donne au tout le volume du C++ : deux turbos d'une moitié de moteur ont chacun
    // leurs conduits (`chargeVolumeM3` d'une moitié, deux fois).
    const chargeVolume = (v: number) => Math.min(Math.max(1.5e-3 + 2.5 * v, 2.5e-3), 14e-3);
    const intakeArea = base * 1.2 * (this.twin ? chargeVolume(volume / 2) / chargeVolume(volume) : 1);
    const parts: IntakePart[] = [];
    if (this.airChain) {
      let cell = intakeFirst;
      for (const part of this.airChain) {
        // Déporté, c'est le tube chaud qui rejoint l'échangeur de plus loin : même volume en plus qu'en C++ (`chargeVolumeM3`).
        const length = part.kind === 'hot' && pipeExtra > 0 ? part.length + pipeExtra : part.length;
        const count = Math.max(3, Math.round(length / CELL_M));
        parts.push({ kind: part.kind, first: cell, count, area: part.kind === 'filter' ? filter.mouth : part.area });
        cell += count;
      }
    } else {
      parts.push({ kind: 'duct', first: intakeFirst, count: Math.round((carb ? 0.25 : filter.duct) / CELL_M), area: 1 });
    }
    // Le carburateur : le col du venturi, puis sa pipe. Ce réseau n'a qu'un conduit : plusieurs corps y sont un seul col, qui garde le rapport
    // de chacun à sa pipe. La pipe reprend la section du conduit, celle que le papillon ouvre sur le plénum.
    // Monté soufflé sur un turbo : après le tube froid. Le rétrécissement se paie par le calcul des changements de section.
    if (carb) {
      const last = parts[parts.length - 1];
      const after = last.first + last.count;
      const overPipe = (carb.throatM / carb.pipeM) * (carb.throatM / carb.pipeM);
      const throatArea = carb.kind === 'single' ? Math.min((Math.PI * carb.throatM * carb.throatM) / 4 / intakeArea, overPipe) : overPipe;
      const venturiCount = Math.max(3, Math.round(0.06 / CELL_M));
      parts.push({ kind: 'venturi', first: after, count: venturiCount, area: throatArea });
      parts.push({ kind: 'duct', first: after + venturiCount, count: Math.max(3, Math.round((carb.kind === 'single' ? 0.39 : 0.15) / CELL_M)), area: 1 });
    }
    // Le second chemin d'air, à la suite : mêmes pièces, son propre compresseur, son propre papillon sur le même plénum.
    const parts2: IntakePart[] = [];
    if (this.twin && this.airChain) {
      const after = parts[parts.length - 1];
      let cell = after.first + after.count;
      for (const part of this.airChain) {
        const count = Math.max(3, Math.round(part.length / CELL_M));
        parts2.push({ kind: part.kind, first: cell, count, area: part.kind === 'filter' ? filter.mouth : part.area });
        cell += count;
      }
    }
    this.intakeParts2 = parts2;
    this.intakeParts = parts;
    const lastPart = parts[parts.length - 1];
    const coldPart = parts.find((part) => part.kind === 'cold');
    this.intakeFirst = intakeFirst;
    this.intakeCount = lastPart.first + lastPart.count - intakeFirst;
    const hotPart = parts.find((part) => part.kind === 'hot');
    const coolerPart = parts.find((part) => part.kind === 'cooler');
    this.compressorCell = hotPart ? hotPart.first : -1;
    this.coolerFirst = coolerPart ? coolerPart.first : -1;
    this.coolerCount = coolerPart ? coolerPart.count : 0;
    // La décharge et la vanne d'air de l'anti-lag : seulement sur un turbo. Un compresseur n'a que sa soupape de dérivation, interne.
    this.releaseCell = this.turbo && coldPart ? coldPart.first + coldPart.count - Math.round(0.25 / CELL_M) : -1;
    cells += this.intakeCount;
    this.intake2First = cells;
    this.intake2Count = parts2.length > 0 ? parts2[parts2.length - 1].first + parts2[parts2.length - 1].count - cells : 0;
    this.compressor2Cell = parts2.find((part) => part.kind === 'hot')?.first ?? -1;
    cells += this.intake2Count;

    // Après tout le reste : à la référence, aucune cellule ne change de numéro.
    const pipeCells = pipeExtra > 0 ? Math.max(3, Math.round(pipeExtra / CELL_M)) : 0;
    this.pipeFirst = cells;
    this.pipeCells = pipeCells;
    cells += pipeCells;

    const n = cells;
    this.n = n;
    this.rho = new Float64Array(n);
    this.mom = new Float64Array(n);
    this.ene = new Float64Array(n);
    this.trc = new Float64Array(n);
    this.rho0 = new Float64Array(n);
    this.ene0 = new Float64Array(n);
    this.p = new Float64Array(n);
    this.u = new Float64Array(n);
    this.c = new Float64Array(n);
    this.y = new Float64Array(n);
    this.temp = new Float64Array(n);
    this.area = new Float64Array(n);
    this.invV = new Float64Array(n);
    this.faceA = new Float64Array(n);
    this.dA = new Float64Array(n);
    this.dragRate = new Float64Array(n);
    this.wallRate = new Float64Array(n);
    this.wallT = new Float64Array(n).fill(this.tAmb);
    this.wallDecay = new Float64Array(n);

    const runnerFirst = new Int32Array(count);
    let index = 0;
    for (let i = 0; i < runners; i++) {
      runnerFirst[i] = index;
      for (let k = 0; k < runnerCells[i]; k++, index++) {
        this.area[index] = runnerArea;
        this.wallDecay[index] = 0.5 + 0.5 * (k / runnerCells[i]);
        if (k + 1 < runnerCells[i]) this.faceA[index] = runnerArea;
      }
    }
    if (rotary) {
      // Les trois chambres d'un rotor soufflent dans le même tube.
      for (let c = count - 1; c >= 0; c--) runnerFirst[c] = runnerFirst[Math.floor(c / 3)];
    }
    this.runnerFirst = runnerFirst;
    for (let b = 0; b < (this.banks === 2 ? 2 : 0); b++) {
      for (let k = 0; k < bankCells; k++) {
        const cell = bankFirst[b] + k;
        this.area[cell] = base;
        this.wallDecay[cell] = 0.45;
        if (k + 1 < bankCells) this.faceA[cell] = base;
      }
    }
    const dragList: number[] = [];
    const steps: number[] = [];
    placed.forEach((section, s) => {
      const sectionArea = base * section.area;
      // Le nid d'abeille freine fort et de façon égale ; les laines et chicanes un peu moins.
      const drag = section.kind === 'cat' ? 2400 : section.kind === 'muffler' ? 1500 : section.kind === 'res' ? 150 : 0;
      for (let k = 0; k < section.count; k++) {
        const cell = section.first + k;
        this.area[cell] = sectionArea;
        this.dragRate[cell] = drag;
        if (drag > 0) dragList.push(cell);
        this.wallDecay[cell] = Math.exp(-(section.from + k * CELL_M) / 2.6) * 0.45;
        const nextArea = k + 1 < section.count ? sectionArea : s + 1 < placed.length ? base * placed[s + 1].area : 0;
        if (nextArea === sectionArea) this.faceA[cell] = sectionArea;
        else if (nextArea > 0) steps.push(cell);
      }
    });
    const shapeChain = (list: IntakePart[], compressorAt: number) => list.forEach((part, p) => {
      const areaHere = intakeArea * part.area;
      const nextArea = p + 1 < list.length ? intakeArea * list[p + 1].area : 0;
      const spec = this.airChain?.find((item) => item.kind === part.kind);
      for (let k = 0; k < part.count; k++) {
        const cell = part.first + k;
        this.area[cell] = areaHere;
        // Sans turbo, le filtre à air tient dans les premières cellules.
        const drag = (spec ? spec.drag : p === 0 && k < 4 ? 700 : 0) * (spec && spec.kind !== 'filter' ? 1 : filter.drop);
        if (drag > 0) {
          this.dragRate[cell] = drag;
          dragList.push(cell);
        }
        this.wallDecay[cell] = 0;
        if (k + 1 < part.count) this.faceA[cell] = areaHere;
        else if (nextArea > 0 && nextArea !== areaHere) steps.push(cell);
        // Le compresseur a sa propre face : entre deux tronçons de même section, seule celle-là reste à part.
        else if (nextArea > 0 && list[p + 1].first !== compressorAt) this.faceA[cell] = areaHere;
      }
    });
    shapeChain(parts, this.compressorCell);
    shapeChain(parts2, this.compressor2Cell);
    for (let k = 0; k < pipeCells; k++) {
      const cell = this.pipeFirst + k;
      this.area[cell] = base;
      this.wallDecay[cell] = 0.45 * Math.exp(-(k * CELL_M) / 2.6);
      if (k + 1 < pipeCells) this.faceA[cell] = base;
    }
    this.dragCells = Int32Array.from(dragList);
    this.stepFaces = Int32Array.from(steps);
    for (let i = 0; i < n; i++) {
      this.invV[i] = 1 / (this.area[i] * CELL_M);
      // Échange de chaleur : plus fort dans un petit tube, faible dans un grand volume.
      this.wallRate[i] = 0.05 / Math.max(Math.sqrt((4 * this.area[i]) / Math.PI), 0.02);
    }
    // Sur la chaîne d'un turbo, le tube chaud garde sa chaleur et seul l'échangeur en prend.
    for (const part of parts2) {
      const spec = this.airChain?.find((item) => item.kind === part.kind);
      if (!spec) continue;
      for (let k = 0; k < part.count; k++) this.wallRate[part.first + k] = spec.wall;
    }
    for (const part of parts) {
      const spec = this.airChain?.find((item) => item.kind === part.kind);
      if (!spec) continue;
      for (let k = 0; k < part.count; k++) this.wallRate[part.first + k] = spec.wall;
    }

    // Volumes : collecteur par rangée, éventuelle jonction, plénum. Volute du turbo dans le dernier avant la ligne.
    const zones = this.banks + (this.banks === 2 ? 1 : 0) + (pipeCells > 0 ? 1 : 0) + 1;
    this.zoneCount = zones;
    this.zM = new Float64Array(zones);
    this.zE = new Float64Array(zones);
    this.zY = new Float64Array(zones);
    this.zV = new Float64Array(zones);
    this.zP = new Float64Array(zones);
    this.zT = new Float64Array(zones);
    const zoneBank = new Int32Array(this.banks);
    for (let b = 0; b < this.banks; b++) zoneBank[b] = b;
    this.zoneJoin = this.banks === 2 ? 2 : 0;
    this.zonePlenum = zones - 1;
    this.zoneTurbine = pipeCells > 0 ? zones - 2 : this.zoneJoin;
    this.zV[this.zonePlenum] = Math.max(volume * 1.3, 0.8e-3);

    // Liens tube-volume.
    const linkCell: number[] = [];
    const linkZone: number[] = [];
    const linkSide: number[] = [];
    const linkArea: number[] = [];
    const link = (cell: number, zone: number, side: number, area: number) => {
      linkCell.push(cell);
      linkZone.push(zone);
      linkSide.push(side);
      linkArea.push(area);
      this.zV[zone] += area * 2.5 * CELL_M;
    };
    for (let i = 0; i < runners; i++) {
      const b = this.banks === 2 ? (this.frontRear ? (i < runners / 2 ? 0 : 1) : architecture && architecture.heads === 2 ? architecture.head[i] : i % 2) : 0;
      link(runnerFirst[i] + runnerCells[i] - 1, zoneBank[b], 1, runnerArea);
    }
    if (this.banks === 2) {
      for (let b = 0; b < 2; b++) {
        // Deux turbos : la turbine de la rangée, et non un raccord, ouvre le collecteur sur son tube.
        if (!this.twin) link(bankFirst[b], zoneBank[b], -1, base);
        link(bankFirst[b] + bankCells - 1, this.zoneJoin, 1, base);
      }
    }
    // Sans turbo, le collecteur s'ouvre directement sur la ligne ; avec, la turbine les sépare.
    if (this.turbo) {
      const peakFlow = volume * RHO_ATM * (1 + 0.9 * config.boostBar) * (config.redlineRpm / 120) * 0.85;
      const upstream = P_ATM * (1.2 + 1.2 * config.boostBar);
      orifice[0] = upstream;
      orifice[1] = 1200;
      orifice[2] = upstream * 0.45;
      orificeFlux();
      this.turbineArea = peakFlow / orifice[3];
      if (this.twin) {
        // Une volute au bout de chaque collecteur ; les tubes d'après se rejoignent, puis la ligne.
        for (let b = 0; b < 2; b++) this.zV[zoneBank[b]] += volume * 0.75;
        link(lineFirst, this.zoneJoin, -1, base * placed[0].area);
      } else {
        // Plus court que la référence, ce qui manque au tube manque à ce volume ; déporté, le tube a ses cellules, et la volute reste contre la turbine.
        this.zV[this.zoneJoin] += volume * 1.5 + (pipeExtra < 0 ? base * pipeExtra : 0);
        if (pipeCells > 0) {
          link(this.pipeFirst, this.zoneJoin, -1, base);
          link(this.pipeFirst + pipeCells - 1, this.zoneTurbine, 1, base);
          this.zV[this.zoneTurbine] += volume * 0.5;
        }
      }
      // Les roues sont d'autant plus petites que le turbo l'est : même loi qu'en C++ (D²).
      // Comme en C++ aussi, sa turbine ne laisse passer qu'une part du gaz au point nominal : la soupape prend le reste.
      // Le coefficient est calé pour retrouver la pression avant la turbine du C++, à 15 % près au point nominal.
      const size = TURBO_SCALE[config.turboSize ?? 'medium'];
      this.turbineArea *= size * size * (this.diesel ? 0.5 : 0.33);
      // Deux turbines, chacune pour la moitié du gaz.
      if (this.twin) this.turbineArea *= 0.5;
      this.crossArea = this.sequential ? base : 0;
    } else {
      link(lineFirst, this.zoneJoin, -1, base * placed[0].area);
    }
    this.linkCell = Int32Array.from(linkCell);
    this.linkZone = Int32Array.from(linkZone);
    this.linkSide = Int8Array.from(linkSide);
    this.linkArea = Float64Array.from(linkArea);

    // Force de pression sur les parois : les faces absentes comptent pour une section nulle.
    const left = new Float64Array(n);
    const right = new Float64Array(n);
    for (let i = 0; i < n - 1; i++) {
      right[i] = this.faceA[i];
      left[i + 1] = this.faceA[i];
    }
    for (const cell of steps) {
      // Chaque côté d'un changement de section garde sa propre section : la force sur le rebord est dans le flux.
      right[cell] = this.area[cell];
      left[cell + 1] = this.area[cell + 1];
    }
    for (let l = 0; l < linkCell.length; l++) {
      if (linkSide[l] > 0) right[linkCell[l]] = linkArea[l];
      else left[linkCell[l]] = linkArea[l];
    }
    // Les deux bouts à l'air libre.
    const lastCell = lineFirst + this.lineCount - 1;
    right[lastCell] = this.area[lastCell];
    left[intakeFirst] = this.area[intakeFirst];
    // Le papillon : voir `stepOrgans`.
    right[intakeFirst + this.intakeCount - 1] = this.area[intakeFirst + this.intakeCount - 1];
    // Le compresseur est une face ouverte des deux côtés.
    if (this.airChain) {
      right[this.compressorCell - 1] = intakeArea;
      left[this.compressorCell] = intakeArea;
    }
    // Le second chemin d'air : son entrée à l'air libre, son papillon, son compresseur.
    if (this.intake2Count > 0) {
      const last2 = this.intake2First + this.intake2Count - 1;
      left[this.intake2First] = this.area[this.intake2First];
      right[last2] = this.area[last2];
      right[this.compressor2Cell - 1] = intakeArea;
      left[this.compressor2Cell] = intakeArea;
    }
    for (let i = 0; i < n; i++) this.dA[i] = right[i] - left[i];
    this.exitArea = this.area[lastCell];

    this.throttleMax = intakeArea;
    this.releaseArea = intakeArea * 0.55;

    // Fin de ligne : là où s'enflamme le carburant imbrûlé.
    this.popCount = Math.max(3, Math.round(0.2 / CELL_M));
    this.popFirst = lastCell - Math.round(0.08 / CELL_M) - this.popCount;
    this.popEnergy = 0;
    this.popLeft = 0;

    // Cylindres.
    this.bank = new Int8Array(count);
    this.phase = new Float64Array(count);
    this.cylM = new Float64Array(count);
    this.cylU = new Float64Array(count);
    this.cylY = new Float64Array(count);
    this.cylV = new Float64Array(count);
    this.cylP = new Float64Array(count);
    this.cylT = new Float64Array(count);
    this.cylBurn = new Float64Array(count);
    this.cylExhaust = new Float64Array(count);
    this.cylIntake = new Float64Array(count);
    this.cylLiftEx = new Float64Array(count);
    this.cylLiftIn = new Float64Array(count);
    this.cylTheta = new Float64Array(count);
    this.cylFired = new Uint8Array(count);
    this.cylHeat = new Float64Array(count);
    this.cylArmed = new Uint8Array(count).fill(1);
    this.cylWasOpen = new Uint8Array(count);
    for (let i = 0; i < count; i++) {
      this.bank[i] = this.banks === 2 ? (this.frontRear ? (i < count / 2 ? 0 : 1) : architecture && architecture.heads === 2 ? architecture.head[i] : i % 2) : 0;
      // Le cylindre au rang s de l'ordre d'allumage passe au point mort haut quand le cycle vaut s/N, ou à son angle s'ils sont inégaux.
      const slot = order.indexOf(i + 1);
      this.phase[i] = architecture && !architecture.even && slot >= 0 ? 1 - architecture.firingDeg[slot] / 720 : 1 - slot / count;
      if (rotary) {
        // La face k du rotor au rang s explose quand l'arbre vaut s·360/rotors + 360·k, sur un cycle de 1080°.
        const rotor = Math.floor(i / 3);
        const face = i % 3;
        const shaft = (order.indexOf(rotor + 1) * 360) / config.cylinders + 360 * face;
        this.phase[i] = 1 - shaft / 1080;
      }
    }
    this.reset();
  }

  /** Tout au repos, à la température de l'air. */
  reset(): void {
    for (let i = 0; i < this.n; i++) {
      this.rho[i] = this.rhoAtm;
      this.mom[i] = 0;
      this.ene[i] = this.eAtm;
      this.trc[i] = 0;
    }
    for (let z = 0; z < this.zoneCount; z++) {
      this.zM[z] = this.rhoAtm * this.zV[z];
      this.zE[z] = (this.pAtm / G1) * this.zV[z];
      this.zY[z] = 0;
    }
    for (let c = 0; c < this.cylinders; c++) {
      const v = this.volumeAt(this.phase[c] * 4 * Math.PI);
      this.cylV[c] = v;
      this.cylM[c] = this.rhoAtm * v;
      this.cylU[c] = (this.pAtm / G1) * v;
      this.cylY[c] = 0;
      this.cylBurn[c] = 0;
      this.cylFired[c] = 0;
    }
    this.demand = 0;
    this.time = 0;
    this.blowdowns = 0;
    this.burntBlowdowns = 0;
    this.primitives();
    this.refreshZones();
    this.refreshDisplay();
  }

  /**
   * Remplit la ligne comme si le moteur tournait déjà : changer de pièces en marche ne doit pas
   * repartir d'une ligne froide, que le ralenti mettrait des dizaines de secondes à remplir à l'écran.
   */
  prefill(heat: number): void {
    const hot = WALL_RISE_K * heat;
    const last = this.lineFirst + this.lineCount;
    for (let i = 0; i < this.n; i++) {
      if (i >= last && i < this.pipeFirst) continue;
      const t = this.tAmb + 120 + hot * this.wallDecay[i];
      const r = this.pAtm / (R * t);
      this.rho[i] = r;
      this.mom[i] = 0;
      this.ene[i] = this.eAtm;
      this.trc[i] = r;
    }
    for (let z = 0; z < this.zonePlenum; z++) {
      const t = this.tAmb + 120 + hot * 0.45;
      this.zM[z] = (this.pAtm / (R * t)) * this.zV[z];
      this.zE[z] = (this.pAtm / G1) * this.zV[z];
      this.zY[z] = this.zM[z];
    }
    this.primitives();
    this.refreshZones();
    this.refreshDisplay();
  }

  private volumeAt(radians: number): number {
    const s = Math.sin(radians);
    const r = this.crank;
    const l = this.rod;
    const fromTop = r + l - r * Math.cos(radians) - Math.sqrt(l * l - r * r * s * s);
    return this.clearance + this.boreArea * fromTop;
  }

  /** Une détonation dans la fin de ligne : `joules` libérés en quelques millisecondes. */
  pop(joules: number): void {
    this.popEnergy += joules;
    this.popLeft = POP_SECONDS;
  }

  /** Recalcule pression, vitesse et son dans chaque cellule, et retient la plus grande vitesse d'onde. */
  private primitives(): void {
    const { rho, mom, ene, trc, p, u, c, y, rho0, ene0 } = this;
    let fastest = 1;
    for (let i = 0; i < this.n; i++) {
      let r = rho[i];
      // Garde-fou : une valeur aberrante ne doit jamais contaminer le reste du tube.
      if (!(r > DENSITY_FLOOR) || !(ene[i] > 0)) {
        if (!(r >= 0)) r = this.rhoAtm;
        r = Math.max(r, DENSITY_FLOOR);
        rho[i] = r;
        mom[i] = 0;
        ene[i] = this.eAtm;
        trc[i] = 0;
      }
      const ru = mom[i];
      const speed = ru / r;
      let pressure = G1 * (ene[i] - 0.5 * ru * speed);
      if (!(pressure > PRESSURE_FLOOR)) {
        pressure = PRESSURE_FLOOR;
        ene[i] = pressure / G1 + 0.5 * ru * speed;
      }
      const sound = Math.sqrt((GAMMA * pressure) / r);
      rho0[i] = r;
      ene0[i] = ene[i];
      p[i] = pressure;
      u[i] = speed;
      c[i] = sound;
      y[i] = trc[i] / r;
      const wave = (speed < 0 ? -speed : speed) + sound;
      if (wave > fastest) fastest = wave;
    }
    this.fastest = fastest;
  }

  private refreshZones(): void {
    for (let z = 0; z < this.zoneCount; z++) {
      // Même garde-fou que pour les cellules.
      if (!(this.zM[z] > 1e-7) || !(this.zE[z] > 0)) {
        this.zM[z] = this.rhoAtm * this.zV[z];
        this.zE[z] = this.eAtm * this.zV[z];
        this.zY[z] = 0;
      }
      const pressure = (G1 * this.zE[z]) / this.zV[z];
      this.zP[z] = pressure;
      this.zT[z] = pressure / ((this.zM[z] / this.zV[z]) * R);
    }
  }

  /** Avance de `seconds` de temps physique. Le cycle va de `cycle` à `cycle + turns` : c'est l'horloge de `Motion`. */
  advance(seconds: number, cycle: number, turns: number, given: GasInputs): void {
    if (this.n === 0) return;
    const inputs = this.frame.copy(given);
    this.setWalls(inputs.heat);
    let left = seconds;
    let steps = 0;
    this.turnRate = turns / Math.max(seconds, 1e-9);
    while (left > 1e-9 && steps < MAX_STEPS_PER_FRAME) {
      this.primitives();
      this.refreshZones();
      let dt = (CFL * this.cell) / this.fastest;
      if (dt > left) dt = left;
      this.dt = dt;
      // Le cycle de `Motion` se replie tous les deux tours : une chambre a besoin de leur nombre total.
      this.cycleNow = (this.rotary ? (inputs.turnsTotal ?? 0) - turns : cycle) + turns * (seconds > 0 ? 1 - left / seconds : 0);
      this.stepCylinders(inputs);
      this.stepOrgans(inputs);
      this.stepFluxes();
      this.stepSources();
      left -= dt;
      this.time += dt;
      steps++;
    }
    this.lastSteps = steps;
    this.primitives();
    this.refreshZones();
    this.refreshDisplay();
  }

  private setWalls(heat: number): void {
    // Sous ce modèle, la température des parois vient de `Motion.heat`, qui avance en temps réel.
    const hot = WALL_RISE_K * heat;
    for (let i = 0; i < this.n; i++) this.wallT[i] = this.tAmb + hot * this.wallDecay[i];
  }

  private stepCylinders(inputs: GasInputs): void {
    const { rho, mom, ene, trc, p, u, c: sound, invV, area, dt, cycleNow: cycle, turnRate: turnsPerSecond, crank, rod, clearance, boreArea } = this;
    const burning = inputs.burning;
    const load = inputs.load;
    const plenum = this.zonePlenum;
    let plenumOut = 0;
    const wallCylinder = this.tAmb + 160 * inputs.heat;
    const advance = ignitionAdvance(this.diesel, inputs.rpm) * this.advanceScale;
    const duration = burnDegrees(this.diesel, inputs.rpm);
    const antiLag = inputs.antiLag ?? 0;
    // Le retard n'est pas écrit dans le gaz : l'étincelle part tard, la combustion de Wiebe finit soupape ouverte.
    const late = antiLag === ANTI_LAG_CODE.retard ? ANTI_LAG_LATE_RETARD : antiLag === ANTI_LAG_CODE.air ? ANTI_LAG_LATE_AIR : 0;
    const { evo, evc, ivo, ivc, sweepEx, sweepIn, wallGain, cycleScale } = this;
    for (let c = 0; c < this.cylinders; c++) {
      let phase = cycle * cycleScale + this.phase[c];
      phase -= Math.floor(phase);
      const theta = phase * 720;
      this.cylTheta[c] = theta;
      // Bielle et manivelle, en ligne : c'est un des calculs les plus fréquents.
      const radians = phase * 4 * Math.PI;
      const sine = Math.sin(radians);
      const volume = clearance + boreArea * (crank + rod - crank * Math.cos(radians) - Math.sqrt(rod * rod - crank * crank * sine * sine));
      // Même garde-fou que pour les cellules : un cylindre aberrant repart à l'état de l'air.
      if (!(this.cylM[c] > 1e-9) || !(this.cylU[c] > 0)) {
        this.cylM[c] = this.rhoAtm * this.cylV[c];
        this.cylU[c] = this.eAtm * this.cylV[c];
        this.cylY[c] = 0;
      }
      let mass = this.cylM[c];
      let energy = this.cylU[c];
      let burnt = this.cylY[c];
      let pressure = (G1 * energy) / this.cylV[c];
      energy -= pressure * (volume - this.cylV[c]);
      this.cylV[c] = volume;

      // Combustion : le mélange allumé avant le point mort haut brûle sur quelques dizaines de degrés.
      const sinceSpark = late > 0 ? (theta >= late ? theta - late : theta + 720 - late) : theta >= 720 - advance ? theta - (720 - advance) : theta + advance;
      if (sinceSpark < duration + 2) {
        if (this.cylArmed[c] === 1) {
          this.cylArmed[c] = 0;
          this.cylBurn[c] = 0;
          // La coupure de cylindres : ce mélange-là traverse sans brûler, et finit au collecteur.
          const fires = burning && (antiLag !== ANTI_LAG_CODE.cut || antiLagFires(this.antiLagEvent++));
          this.cylFired[c] = fires ? 1 : 0;
          const air = Math.max(mass - burnt, 0);
          const richness = this.diesel ? 0.1 + 0.72 * load : 1;
          this.cylHeat[c] = fires ? air * 3.0e6 * richness * (this.diesel ? 0.72 : 0.66) : 0;
        }
        if (this.cylFired[c] === 1) {
          const t = sinceSpark / duration;
          const x = t >= 1 ? 1 : 1 - Math.exp(-5 * t * t * t);
          const gain = x - this.cylBurn[c];
          if (gain > 0) {
            energy += this.cylHeat[c] * gain;
            burnt += (mass - burnt) * (gain / Math.max(1 - this.cylBurn[c], 1e-3));
            this.cylBurn[c] = x;
          }
        }
      } else {
        this.cylBurn[c] = 0;
        if (sinceSpark > duration + 20) this.cylArmed[c] = 1;
      }

      // Les parois reprennent une partie de la chaleur.
      pressure = (G1 * energy) / volume;
      let temperature = pressure / ((mass / volume) * R);
      energy -= wallGain * dt * (temperature - wallCylinder) * mass * CV;

      // Soupape d'échappement : la vidange pousse le gaz dans le collecteur.
      let exhaustFlow = 0;
      if (theta > evo && theta < evc) {
        // Une soupape suit sa came ; une lumière est découverte d'un bord à l'autre par le sommet du rotor.
        const s = Math.sin((Math.PI * (theta - evo)) / (evc - evo));
        let lift = s * s;
        if (sweepEx > 0) {
          const opening = (theta - evo) / sweepEx;
          const closing = (evc - theta) / sweepEx;
          lift = opening < closing ? opening : closing;
          if (lift > 1) lift = 1;
        }
        this.cylLiftEx[c] = lift;
        if (this.cylWasOpen[c] === 0) {
          this.cylWasOpen[c] = 1;
          this.blowdowns++;
          this.lastBlowdownCylinder = c;
          if (this.cylFired[c] === 1) this.burntBlowdowns++;
        }
        const k = this.runnerFirst[c];
        const opening = this.exhaustValve * lift;
        pressure = (G1 * energy) / volume;
        temperature = pressure / ((mass / volume) * R);
        if (pressure >= p[k]) {
          orifice[0] = pressure;
          orifice[1] = temperature;
          orifice[2] = p[k];
          orificeFlux();
          const flow = opening * orifice[3];
          const limit = 0.8 * (pressure - p[k]) * Math.min(volume, 1 / invV[k]) / (GAMMA * R * temperature);
          const dm = Math.min(flow * dt, limit, 0.25 * mass);
          exhaustFlow = dm / dt;
          const enthalpy = CP * temperature;
          const share = burnt / mass;
          mass -= dm;
          energy -= dm * enthalpy;
          burnt -= dm * share;
          const w = invV[k] * dm;
          const speed = Math.min(exhaustFlow / (rho[k] * area[k]), sound[k]);
          rho[k] += w;
          ene[k] += w * enthalpy + 0.5 * w * speed * speed;
          trc[k] += w * share;
          mom[k] += w * speed;
        } else {
          const tk = p[k] / (rho[k] * R);
          orifice[0] = p[k];
          orifice[1] = tk;
          orifice[2] = pressure;
          orificeFlux();
          const flow = opening * orifice[3];
          const limit = 0.8 * (p[k] - pressure) * Math.min(volume, 1 / invV[k]) / (GAMMA * R * tk);
          const dm = Math.min(flow * dt, limit, 0.2 * rho[k] / invV[k]);
          exhaustFlow = -dm / dt;
          const w = invV[k] * dm;
          const enthalpy = (ene[k] + p[k]) / rho[k];
          const share = trc[k] / rho[k];
          rho[k] -= w;
          ene[k] -= w * enthalpy;
          trc[k] -= w * share;
          mom[k] -= w * u[k];
          mass += dm;
          energy += dm * enthalpy;
          burnt += dm * share;
        }
      } else {
        this.cylLiftEx[c] = 0;
        this.cylWasOpen[c] = 0;
      }
      this.cylExhaust[c] = exhaustFlow;

      // Soupape d'admission : le cylindre aspire dans le plénum, et le gaz brûlé peut y refouler.
      let intakeFlow = 0;
      if (theta > ivo && theta < ivc) {
        const s = Math.sin((Math.PI * (theta - ivo)) / (ivc - ivo));
        let lift = s * s;
        if (sweepIn > 0) {
          const opening = (theta - ivo) / sweepIn;
          const closing = (ivc - theta) / sweepIn;
          lift = opening < closing ? opening : closing;
          if (lift > 1) lift = 1;
        }
        this.cylLiftIn[c] = lift;
        const opening = this.intakeValve * lift;
        pressure = (G1 * energy) / volume;
        temperature = pressure / ((mass / volume) * R);
        const pz = this.zP[plenum];
        const tz = this.zT[plenum];
        if (pz >= pressure) {
          orifice[0] = pz;
          orifice[1] = tz;
          orifice[2] = pressure;
          orificeFlux();
          const flow = opening * orifice[3];
          const limit = 0.8 * (pz - pressure) * volume / (GAMMA * R * tz);
          const dm = Math.min(flow * dt, limit, 0.1 * this.zM[plenum]);
          intakeFlow = dm / dt;
          const enthalpy = CP * tz;
          const share = this.zY[plenum] / this.zM[plenum];
          this.zM[plenum] -= dm;
          this.zE[plenum] -= dm * enthalpy;
          this.zY[plenum] -= dm * share;
          mass += dm;
          energy += dm * enthalpy;
          burnt += dm * share;
        } else {
          orifice[0] = pressure;
          orifice[1] = temperature;
          orifice[2] = pz;
          orificeFlux();
          const flow = opening * orifice[3];
          const limit = 0.8 * (pressure - pz) * volume / (GAMMA * R * temperature);
          const dm = Math.min(flow * dt, limit, 0.2 * mass);
          intakeFlow = -dm / dt;
          const enthalpy = CP * temperature;
          const share = burnt / mass;
          mass -= dm;
          energy -= dm * enthalpy;
          burnt -= dm * share;
          this.zM[plenum] += dm;
          this.zE[plenum] += dm * enthalpy;
          this.zY[plenum] += dm * share;
        }
      } else {
        this.cylLiftIn[c] = 0;
      }
      this.cylIntake[c] = intakeFlow;
      plenumOut += intakeFlow;

      if (energy < 150 * mass * CV) energy = 150 * mass * CV;
      this.cylM[c] = mass;
      this.cylU[c] = energy;
      this.cylY[c] = burnt < 0 ? 0 : burnt > mass ? mass : burnt;
      this.cylP[c] = (G1 * energy) / volume;
      this.cylT[c] = (this.cylP[c] * volume) / (mass * R);
    }
    // Moyenne sur deux allumages : assez lente pour ne pas suivre chaque cylindre.
    const interval = 2 / Math.max(turnsPerSecond * this.firings, 1);
    const smooth = dt / (dt + Math.max(interval, 0.01));
    this.demand += (plenumOut - this.demand) * smooth;
  }

  /** Papillon, compresseur, turbine, décharge : les passages qui séparent deux parties du réseau. */
  private stepOrgans(inputs: GasInputs): void {
    if (this.twin) {
      this.stepTwinOrgans(inputs);
      return;
    }
    const { rho, mom, ene, trc, p, u, invV, area, dt } = this;
    const plenum = this.zonePlenum;
    const firstIntake = this.intakeFirst;
    const lastIntake = firstIntake + this.intakeCount - 1;
    const chained = this.airChain !== null;
    // La pression relative du turbo, posée sur l'atmosphère du jour : `boostScale` vaut exactement 1 au niveau de la mer.
    const boostFactor = chained ? 1 + inputs.boost * this.boostBar * this.boostScale : 1;
    // Le turbo calculé par `TurboCharger` ou le compresseur par `Supercharger` : c'est lui que sent la voiture. Sans lui, le rapport se déduit de la pression.
    const compressorRatio = chained ? (inputs.compressorRatio ?? boostFactor) : 1;

    // Papillon : il s'ouvre juste assez pour tenir la pression que demande le remplissage réel.
    const wanted = this.diesel ? this.pAtm * boostFactor : this.pAtm * (0.18 + 0.82 * Math.min(inputs.load, 1)) * boostFactor;
    const pZone = this.zP[plenum];
    const tZone = this.zT[plenum];
    // Une face de section variable entre le conduit et le plénum : grande ouverte, c'est un simple
    // raccord ; presque fermée, le gaz y file en jet. Même calcul stable que partout ailleurs.
    reservoir[0] = this.zM[plenum] / this.zV[plenum];
    reservoir[1] = pZone;
    this.reservoirFlux(lastIntake, 1);
    const unit = flux[0];
    let opening = this.throttleMax;
    // Grand ouvert sous pression : il laisse aussi revenir l'air que les soupapes refoulent, et le plénum reste au niveau du tube.
    if (!this.diesel && !(chained && inputs.load > 0.9)) {
      if (unit > 1) {
        const want = this.demand + ((wanted - pZone) * this.zV[plenum]) / (R * tZone * 0.006);
        opening = Math.min(Math.max(want / unit, 0), this.throttleMax);
      } else if (pZone > wanted * 1.02) {
        // Le plénum est au-dessus de ce qu'on demande : le papillon est fermé, presque rien ne reflue.
        opening = this.throttleMax * 0.015;
      }
    }
    this.throttleShare = opening / this.throttleMax;
    const throttleFlow = opening * unit;
    const moved = throttleFlow * dt;
    const w = invV[lastIntake];
    const share = moved > 0 ? trc[lastIntake] / rho[lastIntake] : this.zY[plenum] / this.zM[plenum];
    const energyMoved = opening * flux[2] * dt;
    rho[lastIntake] -= moved * w;
    ene[lastIntake] -= energyMoved * w;
    trc[lastIntake] -= moved * share * w;
    // Sur le papillon lui-même, la pression du conduit : la force sur la partie fermée.
    mom[lastIntake] -= (opening * flux[1] + (this.throttleMax - opening) * p[lastIntake]) * dt * w;
    this.zM[plenum] += moved;
    this.zE[plenum] += energyMoved;
    this.zY[plenum] += moved * share;
    this.throttleFlow = throttleFlow;
    // Le plénum tiédit au contact du moteur. Sur un turbo, c'est l'air de l'échangeur qui compte : il ne fait que tiédir.
    this.zE[plenum] += (dt / (chained ? 0.6 : 0.05)) * (this.tAmb + 30 * inputs.heat - tZone) * this.zM[plenum] * CV;

    if (!chained) return;

    this.boostNow = compressorRatio;
    this.outKNow = inputs.compressorOutK ?? 0;
    this.compressorFace();
    // Un compresseur n'a ni décharge ni turbine : l'échappement ne le voit pas.
    if (!this.turbo) return;

    // Décharge : une soupape qui ouvre le conduit sur l'air libre, sauf en flutter où tout reflue par le compresseur.
    let releaseFlow = 0;
    if (!this.flutter && inputs.release > 0.02) {
      const v = this.releaseCell;
      if (p[v] > this.pAtm) {
        const tv = p[v] / (rho[v] * R);
        orifice[0] = p[v];
        orifice[1] = tv;
        orifice[2] = this.pAtm;
        orificeFlux();
        const flow = this.releaseArea * Math.min(inputs.release, 1) * orifice[3];
        const dm = Math.min(flow * dt, 0.2 * rho[v] / invV[v]);
        releaseFlow = dm / dt;
        const w = invV[v] * dm;
        const enthalpy = (ene[v] + p[v]) / rho[v];
        rho[v] -= w;
        ene[v] -= w * enthalpy;
        mom[v] -= w * u[v];
      }
    }
    this.releaseFlow = releaseFlow;
    this.stepAntiLag(inputs);

    // Turbine : la détente y prend de l'énergie, et la volute lisse les bouffées. La soupape de décharge la contourne.
    const join = this.zoneTurbine;
    const first = this.lineFirst;
    const pz = this.zP[join];
    const tz = this.zT[join];
    let turbineFlow = 0;
    let wastegateFlow = 0;
    let power = 0;
    if (pz >= p[first]) {
      orifice[0] = pz;
      orifice[1] = tz;
      orifice[2] = p[first];
      orificeFlux();
      const unit = orifice[3];
      // La soupape de décharge du C++, avec le même passage : 2 fois celui de la turbine une fois ouverte.
      const gate = inputs.wastegate !== undefined ? inputs.wastegate * 2 : inputs.boost > 0.9 ? Math.min((inputs.boost - 0.9) / 0.08, 1) * 0.6 : 0;
      const flowT = this.turbineArea * unit;
      const flowW = this.turbineArea * gate * unit;
      const limit = Math.min(0.2 * this.zM[join], 0.8 * (pz - p[first]) * Math.min(this.zV[join], 1 / invV[first]) / (GAMMA * R * tz));
      const total = Math.min((flowT + flowW) * dt, limit);
      const share = flowT + flowW > 0 ? total / ((flowT + flowW) * dt) : 0;
      turbineFlow = flowT * share;
      wastegateFlow = flowW * share;
      const ratio = Math.max(p[first] / pz, CRITICAL_RATIO * 0.6);
      const taken = 0.68 * CP * tz * (1 - ratio ** COMPRESSOR_EXP);
      const enthalpy = CP * tz;
      const burntShare = this.zY[join] / this.zM[join];
      const dmT = turbineFlow * dt;
      const dmW = wastegateFlow * dt;
      power = turbineFlow * taken;
      this.zM[join] -= total;
      this.zE[join] -= total * enthalpy;
      this.zY[join] -= total * burntShare;
      const w = invV[first];
      const speed = Math.min(total / dt / (rho[first] * area[first]), this.c[first]);
      rho[first] += w * total;
      ene[first] += w * (dmT * (enthalpy - taken) + dmW * enthalpy + 0.5 * total * speed * speed);
      trc[first] += w * total * burntShare;
      mom[first] += w * total * speed;
    } else {
      const tf = p[first] / (rho[first] * R);
      orifice[0] = p[first];
      orifice[1] = tf;
      orifice[2] = pz;
      orificeFlux();
      const flow = this.turbineArea * orifice[3];
      const dm = Math.min(flow * dt, 0.2 * rho[first] / invV[first], 0.8 * (p[first] - pz) * Math.min(this.zV[join], 1 / invV[first]) / (GAMMA * R * tf));
      turbineFlow = -dm / dt;
      const w = invV[first] * dm;
      const enthalpy = (ene[first] + p[first]) / rho[first];
      const share = trc[first] / rho[first];
      rho[first] -= w;
      ene[first] -= w * enthalpy;
      trc[first] -= w * share;
      mom[first] -= w * u[first];
      this.zM[join] += dm;
      this.zE[join] += dm * enthalpy;
      this.zY[join] += dm * share;
    }
    this.turbineFlow = turbineFlow;
    this.wastegateFlow = wastegateFlow;
    this.turbinePower = power;
  }


  /**
   * Deux turbos : un papillon par chemin d'air sur le même plénum, chacun pour la moitié de ce que demandent les cylindres ; un
   * compresseur par chemin ; une turbine au bout de chaque collecteur, avec sa soupape. En séquentiel, la vanne du secondaire
   * règle sa turbine, son clapet ferme son chemin d'air, et un tube relie les deux collecteurs.
   */
  private stepTwinOrgans(inputs: GasInputs): void {
    const { rho, mom, ene, trc, p, u, invV, area, dt } = this;
    const plenum = this.zonePlenum;
    const boostFactor = 1 + inputs.boost * this.boostBar;
    const compressorRatio = inputs.compressorRatio ?? boostFactor;
    const wanted = this.diesel ? P_ATM * boostFactor : P_ATM * (0.18 + 0.82 * Math.min(inputs.load, 1)) * boostFactor;
    const pZone = this.zP[plenum];
    const tZone = this.zT[plenum];
    this.throttleWant = this.demand + ((wanted - pZone) * this.zV[plenum]) / (R * tZone * 0.006);
    const online = inputs.secondaryOnline !== false;
    for (let chain = 0; chain < 2; chain++) {
      const last = chain === 0 ? this.intakeFirst + this.intakeCount - 1 : this.intake2First + this.intake2Count - 1;
      reservoir[0] = this.zM[plenum] / this.zV[plenum];
      reservoir[1] = pZone;
      this.reservoirFlux(last, 1);
      const unit = flux[0];
      let opening = this.throttleMax;
      if (!this.diesel && inputs.load <= 0.9) {
        if (unit > 1) opening = Math.min(Math.max((0.5 * this.throttleWant) / unit, 0), this.throttleMax);
        else if (pZone > wanted * 1.02) opening = this.throttleMax * 0.015;
      }
      // Le clapet du secondaire, fermé tant qu'il ne pousse pas sur la charge : son chemin d'air reste clos.
      if (chain === 1 && !online) opening = 0;
      const throttleFlow = opening * unit;
      const moved = throttleFlow * dt;
      const w = invV[last];
      const share = moved > 0 ? trc[last] / rho[last] : this.zY[plenum] / this.zM[plenum];
      const energyMoved = opening * flux[2] * dt;
      rho[last] -= moved * w;
      ene[last] -= energyMoved * w;
      trc[last] -= moved * share * w;
      mom[last] -= (opening * flux[1] + (this.throttleMax - opening) * p[last]) * dt * w;
      this.zM[plenum] += moved;
      this.zE[plenum] += energyMoved;
      this.zY[plenum] += moved * share;
      if (chain === 0) {
        this.throttleShare = opening / this.throttleMax;
        this.throttleFlow = throttleFlow;
      } else {
        this.throttleShare2 = opening / this.throttleMax;
        this.throttleFlow2 = throttleFlow;
      }
    }
    this.zE[plenum] += (dt / 0.6) * (T_AMB + 30 * inputs.heat - tZone) * this.zM[plenum] * CV;

    this.boostNow = compressorRatio;
    this.boost2Now = inputs.compressorRatio2 ?? compressorRatio;
    this.outKNow = inputs.compressorOutK ?? 0;
    this.compressorFaceAt(0);
    this.compressorFaceAt(1);

    // La soupape de décharge, commune, sur le premier chemin.
    let releaseFlow = 0;
    if (!this.flutter && inputs.release > 0.02) {
      const v = this.releaseCell;
      if (p[v] > P_ATM) {
        orifice[0] = p[v];
        orifice[1] = p[v] / (rho[v] * R);
        orifice[2] = P_ATM;
        orificeFlux();
        const dm = Math.min(this.releaseArea * Math.min(inputs.release, 1) * orifice[3] * dt, 0.2 * rho[v] / invV[v]);
        releaseFlow = dm / dt;
        const w = invV[v] * dm;
        const enthalpy = (ene[v] + p[v]) / rho[v];
        rho[v] -= w;
        ene[v] -= w * enthalpy;
        mom[v] -= w * u[v];
      }
    }
    this.releaseFlow = releaseFlow;

    // Séquentiel : les deux collecteurs sont reliés, le gaz va vers la turbine qui le laisse passer.
    this.crossFlow = 0;
    if (this.sequential) {
      const high = this.zP[0] >= this.zP[1] ? 0 : 1;
      const low = 1 - high;
      orifice[0] = this.zP[high];
      orifice[1] = this.zT[high];
      orifice[2] = this.zP[low];
      orificeFlux();
      const dm = Math.min(this.crossArea * orifice[3] * dt, 0.2 * this.zM[high], 0.4 * (this.zP[high] - this.zP[low]) * Math.min(this.zV[high], this.zV[low]) / (GAMMA * R * this.zT[high]));
      if (dm > 0) {
        const share = this.zY[high] / this.zM[high];
        const enthalpy = CP * this.zT[high];
        this.zM[high] -= dm;
        this.zE[high] -= dm * enthalpy;
        this.zY[high] -= dm * share;
        this.zM[low] += dm;
        this.zE[low] += dm * enthalpy;
        this.zY[low] += dm * share;
        this.crossFlow = high === 0 ? dm / dt : -dm / dt;
      }
    }

    // Une turbine au bout de chaque collecteur, vers le tube de sa rangée.
    for (let b = 0; b < 2; b++) {
      const zone = b;
      const first = this.bankFirst[b];
      const pz = this.zP[zone];
      const tz = this.zT[zone];
      // La vanne d'échappement du secondaire : fermée, sa turbine ne voit presque rien passer.
      const valve = b === 1 && this.sequential ? Math.min(Math.max(inputs.twinValve ?? 1, 0), 1) : 1;
      const turbineArea = this.turbineArea * valve;
      let turbineFlow = 0;
      let wastegateFlow = 0;
      let power = 0;
      if (pz >= p[first]) {
        orifice[0] = pz;
        orifice[1] = tz;
        orifice[2] = p[first];
        orificeFlux();
        const unit = orifice[3];
        const opened = b === 0 ? inputs.wastegate : inputs.wastegate2;
        const gate = opened !== undefined ? opened * 2 : inputs.boost > 0.9 ? Math.min((inputs.boost - 0.9) / 0.08, 1) * 0.6 : 0;
        const flowT = turbineArea * unit;
        const flowW = turbineArea * gate * unit;
        const limit = Math.min(0.2 * this.zM[zone], 0.8 * (pz - p[first]) * Math.min(this.zV[zone], 1 / invV[first]) / (GAMMA * R * tz));
        const total = Math.min((flowT + flowW) * dt, limit);
        const share = flowT + flowW > 0 ? total / ((flowT + flowW) * dt) : 0;
        turbineFlow = flowT * share;
        wastegateFlow = flowW * share;
        const ratio = Math.max(p[first] / pz, CRITICAL_RATIO * 0.6);
        const taken = 0.68 * CP * tz * (1 - ratio ** COMPRESSOR_EXP);
        const enthalpy = CP * tz;
        const burntShare = this.zY[zone] / this.zM[zone];
        power = turbineFlow * taken;
        this.zM[zone] -= total;
        this.zE[zone] -= total * enthalpy;
        this.zY[zone] -= total * burntShare;
        const w = invV[first];
        const speed = Math.min(total / dt / (rho[first] * area[first]), this.c[first]);
        rho[first] += w * total;
        ene[first] += w * (turbineFlow * dt * (enthalpy - taken) + wastegateFlow * dt * enthalpy + 0.5 * total * speed * speed);
        trc[first] += w * total * burntShare;
        mom[first] += w * total * speed;
      } else if (turbineArea > 0) {
        const tf = p[first] / (rho[first] * R);
        orifice[0] = p[first];
        orifice[1] = tf;
        orifice[2] = pz;
        orificeFlux();
        const dm = Math.min(turbineArea * orifice[3] * dt, 0.2 * rho[first] / invV[first], 0.8 * (p[first] - pz) * Math.min(this.zV[zone], 1 / invV[first]) / (GAMMA * R * tf));
        turbineFlow = -dm / dt;
        const w = invV[first] * dm;
        const enthalpy = (ene[first] + p[first]) / rho[first];
        const share = trc[first] / rho[first];
        rho[first] -= w;
        ene[first] -= w * enthalpy;
        trc[first] -= w * share;
        mom[first] -= w * u[first];
        this.zM[zone] += dm;
        this.zE[zone] += dm * enthalpy;
        this.zY[zone] += dm * share;
      }
      if (b === 0) {
        this.turbineFlow = turbineFlow;
        this.wastegateFlow = wastegateFlow;
        this.turbinePower = power;
      } else {
        this.turbineFlow2 = turbineFlow;
        this.wastegateFlow2 = wastegateFlow;
        this.turbinePower2 = power;
      }
    }
  }

  /** Flux à toutes les faces : les mêmes pour les deux voisines, donc masse et énergie sont conservées. */
  private stepFluxes(): void {
    const { rho, mom, ene, trc, rho0, ene0, p, u, c, y, invV, faceA, dA, dt } = this;
    const n = this.n;
    for (let i = 0; i < n - 1; i++) {
      const face = faceA[i];
      if (face === 0) continue;
      const j = i + 1;
      const rL = rho0[i];
      const eL = ene0[i];
      const rR = rho0[j];
      const eR = ene0[j];
      const uL = u[i];
      const uR = u[j];
      const pL = p[i];
      const pR = p[j];
      const cL = c[i];
      const cR = c[j];
      // HLLC en ligne : c'est le cœur du calcul. Au milieu d'un tube, les vitesses d'onde simples suffisent.
      const aL = uL - cL;
      const bL = uR - cR;
      const sL = aL < bL ? aL : bL;
      const aR = uL + cL;
      const bR = uR + cR;
      const sR = aR > bR ? aR : bR;
      let fm: number;
      let fo: number;
      let fe: number;
      if (sL >= 0) {
        fm = rL * uL;
        fo = fm * uL + pL;
        fe = uL * (eL + pL);
      } else if (sR <= 0) {
        fm = rR * uR;
        fo = fm * uR + pR;
        fe = uR * (eR + pR);
      } else {
        const dL = rL * (sL - uL);
        const dR = rR * (sR - uR);
        const sM = (pR - pL + dL * uL - dR * uR) / (dL - dR);
        if (sM >= 0) {
          const rS = dL / (sL - sM);
          const eS = rS * (eL / rL + (sM - uL) * (sM + pL / dL));
          fm = rL * uL + sL * (rS - rL);
          fo = rL * uL * uL + pL + sL * (rS * sM - rL * uL);
          fe = uL * (eL + pL) + sL * (eS - eL);
        } else {
          const rS = dR / (sR - sM);
          const eS = rS * (eR / rR + (sM - uR) * (sM + pR / dR));
          fm = rR * uR + sR * (rS - rR);
          fo = rR * uR * uR + pR + sR * (rS * sM - rR * uR);
          fe = uR * (eR + pR) + sR * (eS - eR);
        }
      }
      const m = face * fm * dt;
      const mo = face * fo * dt;
      const en = face * fe * dt;
      const tr = m * (m > 0 ? y[i] : y[j]);
      const wi = invV[i];
      const wj = invV[j];
      rho[i] -= m * wi;
      mom[i] -= mo * wi;
      ene[i] -= en * wi;
      trc[i] -= tr * wi;
      rho[j] += m * wj;
      mom[j] += mo * wj;
      ene[j] += en * wj;
      trc[j] += tr * wj;
    }
    // Force de pression sur les parois : fonds de tube, faces absentes.
    for (let i = 0; i < n; i++) if (dA[i] !== 0) mom[i] += dt * invV[i] * p[i] * dA[i];

    this.stepAreaChanges();

    // Tubes reliés aux volumes.
    for (let l = 0; l < this.linkCell.length; l++) {
      const i = this.linkCell[l];
      const z = this.linkZone[l];
      const sign = this.linkSide[l];
      const face = this.linkArea[l];
      reservoir[0] = this.zM[z] / this.zV[z];
      reservoir[1] = this.zP[z];
      this.reservoirFlux(i, sign);
      // Sens positif : du tube vers le volume.
      const m = sign * face * flux[0] * dt;
      const en = sign * face * flux[2] * dt;
      const share = m > 0 ? y[i] : this.zY[z] / this.zM[z];
      const w = invV[i];
      rho[i] -= m * w;
      ene[i] -= en * w;
      trc[i] -= m * share * w;
      mom[i] -= sign * face * flux[1] * dt * w;
      this.zM[z] += m;
      this.zE[z] += en;
      this.zY[z] += m * share;
    }

    // Les deux bouts à l'air libre : l'extérieur impose sa pression, et son air si ça rentre.
    this.openEnd(this.lineFirst + this.lineCount - 1, 1);
    this.openEnd(this.intakeFirst, -1);
    if (this.intake2Count > 0) this.openEnd(this.intake2First, -1);
  }

  /**
   * Là où la section change d'un coup. Le grand côté y vaut un réservoir vu du petit tube : la face prend
   * la vitesse du petit tube, et la pression du grand corrigée de Bernoulli si le gaz s'engouffre, de
   * Borda-Carnot s'il en sort en jet. Résoudre directement entre les deux cellules inventerait une
   * contre-pression de 0,3 bar par volume, et amplifier la vitesse du grand côté rendrait le calcul instable.
   * Une onde y retrouve les coefficients de la théorie, ce que vérifie un test.
   */
  private stepAreaChanges(): void {
    const { rho, mom, ene, trc, rho0, ene0, p, u, c, y, invV, area, stepFaces, dt } = this;
    for (let k = 0; k < stepFaces.length; k++) {
      const i = stepFaces[k];
      const j = i + 1;
      const bigLeft = area[i] > area[j];
      const big = bigLeft ? i : j;
      const small = bigLeft ? j : i;
      const aB = area[big];
      const aS = area[small];
      const rB = rho0[big];
      const pB = p[big];
      const uB = u[big];
      const uF = u[small];
      const share = aS / aB;
      const dynamic = rB * uF * uF;
      const towardSmall = bigLeft ? uF > 0 : uF < 0;
      let pF = pB - (towardSmall ? 0.5 * dynamic * (1 - share * share) : dynamic * share * (1 - share));
      if (pF < 0.5 * pB) pF = 0.5 * pB;
      const rF = towardSmall ? rB * (pF / pB) ** (1 / GAMMA) : rB;
      const cF = Math.sqrt((GAMMA * pF) / rF);
      const eF = pF / G1 + 0.5 * rF * uF * uF;
      const big0 = bigLeft ? 0 : 5;
      const small0 = bigLeft ? 5 : 0;
      riemann[big0] = rF;
      riemann[big0 + 1] = uF;
      riemann[big0 + 2] = pF;
      riemann[big0 + 3] = cF;
      riemann[big0 + 4] = eF;
      riemann[small0] = rho0[small];
      riemann[small0 + 1] = uF;
      riemann[small0 + 2] = p[small];
      riemann[small0 + 3] = c[small];
      riemann[small0 + 4] = ene0[small];
      hllc();
      const m = aS * flux[0] * dt;
      const en = aS * flux[2] * dt;
      const tr = m * (m > 0 ? y[i] : y[j]);
      const moS = aS * flux[1] * dt;
      // Le grand côté ne sent de ce flux que ce qui s'écarte de l'équilibre : le rebord encaisse le reste.
      const moB = (aB * (rB * uB * uB + pB) + aS * (flux[1] - rF * uF * uF - pF)) * dt;
      const wi = invV[i];
      const wj = invV[j];
      rho[i] -= m * wi;
      ene[i] -= en * wi;
      trc[i] -= tr * wi;
      mom[i] -= (bigLeft ? moB : moS) * wi;
      rho[j] += m * wj;
      ene[j] += en * wj;
      trc[j] += tr * wj;
      mom[j] += (bigLeft ? moS : moB) * wj;
    }
  }

  /** Ce que l'anti-lag apporte au collecteur, hors du cycle : la chaleur d'une flamme, et l'air pris après l'échangeur. */
  private stepAntiLag(inputs: GasInputs): void {
    const code = inputs.antiLag ?? 0;
    if (code !== ANTI_LAG_CODE.air && code !== ANTI_LAG_CODE.cut) return;
    const { rho, ene, mom, p, u, invV, dt } = this;
    const join = this.zoneJoin;
    this.zE[join] += (inputs.antiLagHeatW ?? 0) * dt;
    const flow = inputs.antiLagAirFlow ?? 0;
    this.antiLagAirNow = 0;
    if (code !== ANTI_LAG_CODE.air || flow <= 0) return;
    const v = this.releaseCell;
    const dm = Math.min(flow * dt, 0.2 * rho[v] / invV[v]);
    const w = invV[v] * dm;
    const enthalpy = (ene[v] + p[v]) / rho[v];
    rho[v] -= w;
    ene[v] -= w * enthalpy;
    mom[v] -= w * u[v];
    this.zM[join] += dm;
    this.zE[join] += dm * enthalpy;
    this.antiLagAirNow = dm / dt;
  }

  /** Compresseur : une face comme les autres, mais la pression y saute du rapport que fixe le turbo. */
  private compressorFace(): void {
    this.compressorFaceAt(0);
  }

  /** `chain` : 0 le premier chemin d'air, 1 le second d'un biturbo. */
  private compressorFaceAt(chain: number): void {
    const { rho, mom, ene, trc, rho0, ene0, p, u, c, y, invV, area, dt } = this;
    const ratio = chain === 0 ? this.boostNow : this.boost2Now;
    const b = chain === 0 ? this.compressorCell : this.compressor2Cell;
    const a = b - 1;
    const pB = p[b] / ratio;
    const rB = rho0[b] * ratio ** (-1 / GAMMA);
    riemann[0] = rho0[a];
    riemann[1] = u[a];
    riemann[2] = p[a];
    riemann[3] = c[a];
    riemann[4] = ene0[a];
    riemann[5] = rB;
    riemann[6] = u[b];
    riemann[7] = pB;
    riemann[8] = Math.sqrt((GAMMA * pB) / rB);
    riemann[9] = pB / G1 + 0.5 * rB * u[b] * u[b];
    hllc();
    const face = area[a];
    const m = face * flux[0] * dt;
    // L'air sort plus chaud qu'il n'entre : selon le calcul du turbo s'il est connu, sinon à 72 % de rendement.
    const inK = p[a] / (rho0[a] * R);
    const work = this.outKNow > 0 ? Math.max(this.outKNow / inK, 1) : 1 + (ratio ** COMPRESSOR_EXP - 1) / 0.72;
    const tr = m * (m > 0 ? y[a] : y[b]);
    rho[a] -= m * invV[a];
    trc[a] -= tr * invV[a];
    ene[a] -= face * flux[2] * dt * invV[a];
    mom[a] -= face * flux[1] * dt * invV[a];
    rho[b] += m * invV[b];
    trc[b] += tr * invV[b];
    ene[b] += face * flux[2] * work * dt * invV[b];
    mom[b] += face * flux[1] * ratio * dt * invV[b];
    if (chain === 0) this.compressorFlow = m / dt;
    else this.compressorFlow2 = m / dt;
  }

  /**
   * Flux entre une cellule et un réservoir au repos (`reservoir` : densité, pression). Le gaz qui entre
   * dans le tube y arrive avec sa pression totale (Bernoulli) ; celui qui sort forme un jet, à la pression du réservoir.
   */
  private reservoirFlux(i: number, side: number): void {
    const speed = this.u[i];
    const rZ = reservoir[0];
    const pZ = reservoir[1];
    let pG = pZ;
    let rG = rZ;
    if (speed * side < 0) {
      pG = pZ - 0.5 * rZ * speed * speed;
      if (pG < 0.4 * pZ) pG = 0.4 * pZ;
      rG = rZ * (pG / pZ) ** (1 / GAMMA);
    }
    const ghost = side > 0 ? 5 : 0;
    const cell = side > 0 ? 0 : 5;
    riemann[ghost] = rG;
    riemann[ghost + 1] = speed;
    riemann[ghost + 2] = pG;
    riemann[ghost + 3] = Math.sqrt((GAMMA * pG) / rG);
    riemann[ghost + 4] = pG / G1 + 0.5 * rG * speed * speed;
    riemann[cell] = this.rho0[i];
    riemann[cell + 1] = speed;
    riemann[cell + 2] = this.p[i];
    riemann[cell + 3] = this.c[i];
    riemann[cell + 4] = this.ene0[i];
    hllc();
  }

  private openEnd(i: number, side: number): void {
    const { rho, mom, ene, trc, y, invV, area, dt } = this;
    const face = area[i];
    // L'admission prend l'air là où le filtre va le chercher ; l'échappement, s'il aspire, celui du dehors.
    reservoir[0] = side < 0 ? this.inletRho : this.rhoAtm;
    reservoir[1] = this.pAtm;
    this.reservoirFlux(i, side);
    const m = side * face * flux[0] * dt;
    const w = invV[i];
    rho[i] -= m * w;
    ene[i] -= side * face * flux[2] * dt * w;
    mom[i] -= side * face * flux[1] * dt * w;
    trc[i] -= m * (m > 0 ? y[i] : 0) * w;
  }

  /** Frottement des organes à chaque pas ; parois, plus lentes, tous les quelques pas. */
  private stepSources(): void {
    const { mom, ene, rho, invV, dragCells, dragRate, dt } = this;
    for (let k = 0; k < dragCells.length; k++) {
      const i = dragCells[k];
      // Implicite : le frottement reste stable même très fort. L'énergie perdue reste dans le gaz, en chaleur.
      mom[i] /= 1 + dragRate[i] * dt;
    }
    if (this.popLeft > 0 && this.popEnergy > 0) {
      const portion = Math.min(dt / this.popLeft, 1);
      const joules = this.popEnergy * portion;
      for (let k = 0; k < this.popCount; k++) {
        const i = this.popFirst + k;
        ene[i] += (joules / this.popCount) * invV[i];
      }
      this.popEnergy -= joules;
      this.popLeft -= dt;
    }
    this.substeps++;
    this.wallClock += dt;
    if (this.substeps < WALL_EVERY) return;
    const elapsed = this.wallClock;
    this.substeps = 0;
    this.wallClock = 0;
    const { p, u, wallRate, wallT } = this;
    for (let i = 0; i < this.n; i++) {
      const speed = u[i] < 0 ? -u[i] : u[i];
      // Échange de chaleur et frottement suivent la vitesse du gaz (Dittus-Boelter, simplifié).
      const rate = wallRate[i] * (5 + 0.6 * speed);
      const r = rho[i];
      const t = p[i] / (r * R);
      let exchanged = rate * elapsed;
      if (exchanged > 0.8) exchanged = 0.8;
      ene[i] -= exchanged * r * CV * (t - wallT[i]);
      mom[i] /= 1 + 0.2 * wallRate[i] * speed * elapsed;
    }
  }

  /** Première cellule du collecteur de ce cylindre, côté soupape. */
  runnerStart(cylinder: number): number {
    return this.runnerFirst[cylinder];
  }

  /** Cellule juste avant l'entrée de l'échangeur : l'air chaud, tel qu'il sort du compresseur. */
  get coolerInCell(): number {
    return this.coolerFirst - 1;
  }

  /** Cellule juste après l'échangeur : l'air refroidi. */
  get coolerOutCell(): number {
    return this.coolerFirst + this.coolerCount;
  }

  /** Après le calcul : température et état à la sortie, pour les vues. */
  private refreshDisplay(): void {
    const { p, rho, temp } = this;
    for (let i = 0; i < this.n; i++) temp[i] = p[i] / (rho[i] * R);
    const last = this.lineFirst + this.lineCount - 1;
    this.exitU = this.u[last];
    this.exitT = temp[last];
    this.exitY = this.y[last];
    this.exitP = p[last];
  }

  /** Masse totale de gaz dans le réseau : le contrôle des calculs. */
  totalMass(): number {
    let mass = 0;
    for (let i = 0; i < this.n; i++) mass += this.rho[i] / this.invV[i];
    for (let z = 0; z < this.zoneCount; z++) mass += this.zM[z];
    for (let c = 0; c < this.cylinders; c++) mass += this.cylM[c];
    return mass;
  }
}
