/**
 * Le moteur rotatif : sa géométrie et sa cinématique, du calcul pur, sans DOM ni three.js ni texte. Les vues 2D et 3D et le gaz
 * lisent la même chose, et les tests le vérifient sans navigateur.
 *
 * Vue de face : x à droite, y en haut, z vers l'avant. Comme sur un moteur à pistons, l'arbre tourne dans le sens des aiguilles
 * d'une montre et les angles se comptent depuis le haut dans ce sens. L'arbre à l'angle A porte le centre du rotor à e·(sin A, cos A),
 * et le rotor a tourné de A/3. Une couronne de 30 dents roule sur un pignon fixe de 20 : c'est ce qui donne ce tiers.
 *
 * Le carter est la trace d'un sommet du rotor : H(u) = R·(sin u, cos u) − e·(sin 3u, cos 3u), resserrée en haut et en bas.
 * En haut, les bougies et le point mort haut d'explosion ; en bas, le point mort haut d'échange, entre les deux lumières.
 */
import type { EngineConfig } from './config';
import { isTwoStroke } from './twoStroke';

const DEG = Math.PI / 180;

/** Vrai sur un rotatif : ni alésage, ni course, ni arbre à cames, ni diesel. Point de contact des autres réglages. */
export const isRotary = (config: Pick<EngineConfig, 'layout'>): boolean => config.layout === 'rotary';

/** Le nombre de rotors qu'on peut choisir. `cylinders` les porte. */
export const ROTOR_COUNTS = [2, 3, 4];

/** L'ordre d'allumage des rotors : celui de `FIRING_ORDERS` à ce nombre (motion.ts dépend du gaz, qui dépend d'ici). */
const ROTOR_ORDERS: Record<number, number[]> = { 2: [1, 2], 3: [1, 3, 2], 4: [1, 3, 4, 2] };
export const rotorOrder = (rotors: number): number[] => ROTOR_ORDERS[rotors] ?? ROTOR_ORDERS[2];

/** Explosions en un cycle de deux tours d'arbre : une par cylindre, deux par rotor ou par cylindre d'un deux-temps (`firingsPerTwoTurns`). */
export const firingsPerCycle = (config: Pick<EngineConfig, 'layout' | 'cylinders'> & { cycle?: EngineConfig['cycle'] }): number =>
  isRotary(config) || isTwoStroke(config) ? 2 * config.cylinders : config.cylinders;

/** Le volume balayé en deux tours d'arbre (cm³) : deux fois la cylindrée d'un rotatif ou d'un deux-temps. Recopie de `sweptCcPerTwoTurns`. */
export const sweptCc = (config: Pick<EngineConfig, 'layout' | 'displacementCc'> & { cycle?: EngineConfig['cycle'] }): number =>
  (isRotary(config) || isTwoStroke(config) ? 2 : 1) * config.displacementCc;

// ── Les chiffres communs : recopies de core/src/engine_config.cpp et engine_sound.cpp ──

const ECCENTRICITY_SHARE = 15 / 105;
const WIDTH_SHARE = 80 / 105;

/** Rayon générateur, excentricité, largeur (mm), aux proportions du moteur de référence. V = 3√3·R·e·b par chambre. */
export function rotaryShape(chamberCc: number): { radius: number; eccentricity: number; width: number } {
  const radius = Math.cbrt((chamberCc * 1000) / (3 * Math.sqrt(3) * ECCENTRICITY_SHARE * WIDTH_SHARE));
  return { radius, eccentricity: radius * ECCENTRICITY_SHARE, width: radius * WIDTH_SHARE };
}

export const rotaryCompression = (aspiration: EngineConfig['aspiration']): number => (aspiration !== 'natural' ? 8.5 : 9.4);

/**
 * Calage en degrés d'arbre depuis le point mort haut d'explosion de la chambre, sur son cycle de 1080° : `kPortEvo`… du son.
 * Le gaz compte en degrés de cycle, 720 pour un cycle : deux tiers de ces valeurs.
 */
export const SHAFT_EVO = 195;
export const SHAFT_EVC = 588;
export const SHAFT_IVO = 572;
export const SHAFT_IVC = 850;
export const CYCLE_PER_SHAFT = 2 / 3;
/** Le sommet balaye la lumière d'échappement en 33° d'arbre : la seule largeur qui rende ce calage possible, (EVC − EVO − 360)/3 en degrés de rotor. */
export const SHAFT_PORT_SWEEP = 33;
/** La lumière d'admission, sur le flanc du carter, est découverte par le flanc du rotor : plus longue à ouvrir (supposé). */
export const SHAFT_INTAKE_SWEEP = 65;
/** Sections des lumières, en part de R·b : 14 cm² à l'échappement, 25 cm² à l'admission sur le moteur de référence (supposé, à 30 % près). */
export const EXHAUST_PORT_SHARE = 0.167;
export const INTAKE_PORT_SHARE = 0.3;
/** Surface sur volume de la chambre, face au cylindre carré : `rotaryWallRatio` en C++, à 9,4 de compression. */
export const WALL_RATIO = 1.8;

/** Où le sommet qui ouvre la lumière d'échappement touche le carter (degrés de u), et sa largeur. */
export const EXHAUST_PORT_U = SHAFT_EVO / 3 + 60;
export const EXHAUST_PORT_WIDTH_U = SHAFT_PORT_SWEEP / 3;
export const INTAKE_PORT_U = (SHAFT_IVC / 3 - 60 + (SHAFT_IVO / 3 + 60)) / 2;

/** Le point du carter à l'angle u (radians). `out[0]`, `out[1]` : x, y. */
export function housingPoint(out: { [i: number]: number }, radius: number, eccentricity: number, u: number): void {
  out[0] = radius * Math.sin(u) - eccentricity * Math.sin(3 * u);
  out[1] = radius * Math.cos(u) - eccentricity * Math.cos(3 * u);
}

/** Le flanc du rotor, dans son repère : un arc qui passe par deux sommets, à `flankRise` du centre sur son milieu (`face` : l'angle de son milieu, degrés). */
function flankPoints(radius: number, rise: number, faceDeg: number, count: number): [number, number][] {
  // Dans le repère de la face : milieu sur +y. Les sommets à ±60°, soit x = ±R·sin60, y = R·cos60.
  const halfChord = radius * Math.sin(60 * DEG);
  const chordY = radius * Math.cos(60 * DEG);
  const sagitta = rise - chordY;
  const arc = (halfChord * halfChord + sagitta * sagitta) / (2 * sagitta);
  const centerY = rise - arc;
  const spread = Math.asin(halfChord / arc);
  const out: [number, number][] = [];
  const f = faceDeg * DEG;
  for (let k = 0; k < count; k++) {
    // De gauche à droite vu du centre, dans le sens des aiguilles : l'angle suit celui des sommets.
    const a = -spread + (2 * spread * k) / count;
    const x = arc * Math.sin(a);
    const y = centerY + arc * Math.cos(a);
    out.push([x * Math.cos(f) + y * Math.sin(f), -x * Math.sin(f) + y * Math.cos(f)]);
  }
  return out;
}

export interface RotorSlot {
  index: number;
  /** Numéro dans l'ordre d'allumage, à partir de 1. */
  number: number;
  /** Angle de l'arbre à son explosion de la face 0 (degrés), et position sur l'arbre (mm, l'avant du côté des z positifs). */
  firingDeg: number;
  z: number;
}

export interface RotaryLayout {
  key: string;
  rotors: number;
  /** Trois chambres par rotor : c'est aussi le découpage de `GasNetwork`. */
  chambers: number;
  order: number[];
  radius: number;
  eccentricity: number;
  width: number;
  /** Rapport de forme, et volume d'une chambre (cm³). */
  chamberCc: number;
  /** Du centre au milieu d'un flanc (mm). */
  flankRise: number;
  /** Le carter, le contour du rotor, parcourus dans le sens des aiguilles. */
  housing: [number, number][];
  rotor: [number, number][];
  /** Pignon fixe et couronne du rotor : dents et rayons primitifs (2e et 3e). */
  fixedTeeth: number;
  rotorTeeth: number;
  fixedRadius: number;
  rotorGearRadius: number;
  /** Excentrique du rotor et tourillon de l'arbre (mm). */
  lobeRadius: number;
  mainRadius: number;
  slots: RotorSlot[];
  frontZ: number;
  rearZ: number;
  /** Plaque latérale, entre deux rotors. */
  plate: number;
  /** Couronne de volant et pignon du démarreur : les mêmes dents que le C++, sur le double de la cylindrée. */
  ring: { z: number; width: number; teeth: number; radius: number };
  pinion: { teeth: number; radius: number; angle: number; x: number; y: number };
  turbo: boolean;
  charger: boolean;
  /** Le carburateur, s'il y en a un : un col commun, ou un par rotor. */
  carb: 'single' | 'multi' | null;
  airFilter: NonNullable<EngineConfig['airFilter']>;
  dualMass: boolean;
}

const RING_MODULE = 2.117;
export const FIXED_TEETH = 20;
export const ROTOR_TEETH = 30;

/** Les dents du démarreur : recopie de `EngineConfig::toProfile`, sur le volume balayé. */
function starterTeeth(rotors: number, displacementCc: number): { ring: number; pinion: number } {
  const litres = (2 * displacementCc) / 1000;
  return { ring: Math.trunc(Math.min(170, Math.max(110, 105 + 9 * litres))), pinion: rotors >= 8 || litres > 4.5 ? 9 : 10 };
}

/** Ce qui fait changer les pièces : à clé égale, les mêmes maillages. */
export function rotaryKey(config: EngineConfig): string {
  return `rotary|${config.cylinders}|${config.displacementCc}|${config.aspiration}|${config.boxFlywheel}|${config.superchargerKind}|${config.airFilter}|${config.fuelSystem}|${config.turboPipeMm}|${config.turboSize}`;
}

export function rotaryLayout(config: EngineConfig): RotaryLayout {
  const rotors = config.cylinders;
  const chamberCc = config.displacementCc / rotors;
  const { radius, eccentricity, width } = rotaryShape(chamberCc);
  const e = eccentricity;
  const housing: [number, number][] = [];
  const p = [0, 0];
  const points = 180;
  for (let k = 0; k < points; k++) {
    housingPoint(p, radius, e, (k / points) * Math.PI * 2);
    housing.push([p[0], p[1]]);
  }
  // Juste assez de jeu pour que le flanc frôle le carter en haut et en bas, sans jamais le toucher.
  const flankRise = radius - 2 * e - radius * 0.012;
  const rotor: [number, number][] = [];
  // Trois flancs, de la face 0 vers les aiguilles : milieux à 0°, 120°, 240° dans le repère du rotor.
  for (const face of [0, 120, 240]) rotor.push(...flankPoints(radius, flankRise, face, 40));

  const order = rotorOrder(rotors);
  const plate = width * 0.28;
  const pitch = width + plate;
  const slots: RotorSlot[] = [];
  for (let i = 0; i < rotors; i++) {
    const number = i + 1;
    slots.push({ index: i, number, firingDeg: (order.indexOf(number) * 360) / rotors, z: ((rotors - 1) / 2 - i) * pitch });
  }
  const frontZ = slots[0].z + pitch / 2;
  const rearZ = slots[rotors - 1].z - pitch / 2;
  const teeth = starterTeeth(rotors, config.displacementCc);
  const ringRadius = (teeth.ring * RING_MODULE) / 2;
  const pinionRadius = (teeth.pinion * RING_MODULE) / 2;
  const pinionAngle = 205 * DEG;
  return {
    key: rotaryKey(config),
    rotors,
    chambers: 3 * rotors,
    order,
    radius,
    eccentricity: e,
    width,
    chamberCc,
    flankRise,
    housing,
    rotor,
    fixedTeeth: FIXED_TEETH,
    rotorTeeth: ROTOR_TEETH,
    fixedRadius: 2 * e,
    rotorGearRadius: 3 * e,
    lobeRadius: 2.47 * e,
    mainRadius: 1.43 * e,
    slots,
    frontZ,
    rearZ,
    plate,
    ring: { z: rearZ - width * 0.32, width: width * 0.16, teeth: teeth.ring, radius: ringRadius },
    pinion: { teeth: teeth.pinion, radius: pinionRadius, angle: pinionAngle, x: Math.cos(pinionAngle) * (ringRadius + pinionRadius), y: Math.sin(pinionAngle) * (ringRadius + pinionRadius) },
    turbo: config.aspiration === 'turbo',
    charger: config.aspiration === 'supercharger',
    carb: config.fuelSystem === 'single' || config.fuelSystem === 'multi' ? config.fuelSystem : null,
    airFilter: config.airFilter ?? 'stock',
    dualMass: config.boxFlywheel === 'dual_mass',
  };
}

/** Part de la cylindrée d'une chambre à son angle de cycle (degrés d'arbre depuis l'explosion) : la loi de `GasNetwork`. */
export function chamberShare(shaftDeg: number): number {
  return 0.5 * (1 - Math.cos((2 * shaftDeg * DEG) / 3));
}

/** Aire de la chambre de la face `face` (mm²), rotor à l'angle d'arbre A (degrés) : le polygone entre le flanc et le carter. Pour vérifier la loi de volume. */
export function chamberArea(layout: RotaryLayout, shaftDeg: number, face: number): number {
  const { radius, eccentricity: e } = layout;
  const beta = shaftDeg / 3;
  const center = (shaftDeg % 1080) * DEG;
  const cx = e * Math.sin(center);
  const cy = e * Math.cos(center);
  const outline: [number, number][] = [];
  const p = [0, 0];
  // Le long du carter, du sommet d'avant au sommet d'arrière de cette face, dans le sens contraire des aiguilles.
  const middle = beta - 120 * face;
  const steps = 120;
  for (let k = 0; k <= steps; k++) {
    housingPoint(p, radius, e, (middle + 60 - (120 * k) / steps) * DEG);
    outline.push([p[0], p[1]]);
  }
  // Puis le flanc, de l'arrière vers l'avant, dans le repère de la voiture.
  const flank = flankPoints(radius, layout.flankRise, middle, 120);
  for (const q of flank) outline.push([cx + q[0], cy + q[1]]);
  let area = 0;
  for (let k = 0; k < outline.length; k++) {
    const a = outline[k];
    const b = outline[(k + 1) % outline.length];
    area += a[0] * b[1] - b[0] * a[1];
  }
  return Math.abs(area) / 2;
}

// ── L'état d'un instant ──

export interface RotaryClock {
  turns: number;
  rpm: number;
  heat: number;
  reduced: boolean;
  burning: boolean;
}

export interface RotaryTelemetry {
  cut: number;
  cranking: number;
  stalled: number;
  rpm: number;
}

export interface RotaryGas {
  cylinders: number;
  cylT: ArrayLike<number>;
  cylM: ArrayLike<number>;
  cylY: ArrayLike<number>;
  cylBurn: ArrayLike<number>;
  cylFired: ArrayLike<number>;
  cylExhaust: ArrayLike<number>;
  cylIntake: ArrayLike<number>;
}

const SPARK_SECONDS = 0.09;
const ADVANCE_DEG = 18;

/** Toutes les pièces mobiles à cet instant, lues par les deux vues. Rien n'alloue. */
export class RotaryPose {
  readonly layout: RotaryLayout;
  /** Angle de l'arbre (degrés, sans repli). */
  shaftDeg = 0;
  /** Par rotor : angle de son excentrique, centre du rotor, angle du rotor (degrés). */
  eccentricDeg: Float64Array;
  rotorX: Float64Array;
  rotorY: Float64Array;
  rotorDeg: Float64Array;
  /** Étincelle de chaque rotor, 1 à son début puis elle s'éteint. */
  spark: Float64Array;
  /** Par chambre, indice de `GasNetwork` : où elle en est de son cycle (degrés d'arbre, 0 à 1080), part de cylindrée, gaz. */
  theta: Float64Array;
  share: Float64Array;
  gasT: Float64Array;
  gasBurnt: Float64Array;
  flame: Float64Array;
  jetIn: Float64Array;
  jetEx: Float64Array;
  gasLive = false;
  starter = 0;
  pinionAngle = 0;
  sparkCount = 0;
  lastSparkRotor = -1;
  private previousDeg = -1;

  constructor(layout: RotaryLayout) {
    this.layout = layout;
    const n = layout.rotors;
    const c = layout.chambers;
    this.eccentricDeg = new Float64Array(n);
    this.rotorX = new Float64Array(n);
    this.rotorY = new Float64Array(n);
    this.rotorDeg = new Float64Array(n);
    this.spark = new Float64Array(n);
    this.theta = new Float64Array(c);
    this.share = new Float64Array(c);
    this.gasT = new Float64Array(c).fill(293);
    this.gasBurnt = new Float64Array(c);
    this.flame = new Float64Array(c);
    this.jetIn = new Float64Array(c);
    this.jetEx = new Float64Array(c);
    this.solve(0);
  }

  /** La mécanique seule, à cet angle de l'arbre : aucune horloge, donc testable pièce par pièce. */
  solve(shaftDeg: number): void {
    const layout = this.layout;
    const e = layout.eccentricity;
    this.shaftDeg = shaftDeg;
    for (let r = 0; r < layout.rotors; r++) {
      const local = shaftDeg - layout.slots[r].firingDeg;
      const a = local * DEG;
      this.eccentricDeg[r] = local;
      this.rotorX[r] = e * Math.sin(a);
      this.rotorY[r] = e * Math.cos(a);
      this.rotorDeg[r] = local / 3;
      for (let k = 0; k < 3; k++) {
        const c = r * 3 + k;
        const theta = (((local - 360 * k) % 1080) + 1080) % 1080;
        this.theta[c] = theta;
        this.share[c] = chamberShare(theta);
      }
    }
    const ring = layout.ring;
    const pinion = layout.pinion;
    this.pinionAngle = pinion.angle + Math.PI - ((Math.PI * 2) / pinion.teeth) * (0.5 - (pinion.angle * ring.teeth) / (Math.PI * 2)) + (shaftDeg * DEG * ring.teeth) / pinion.teeth;
  }

  /** Une image. `seconds` : le temps écoulé, seulement pour éteindre ce qui brille et pousser le pignon. */
  update(clock: RotaryClock, telemetry: RotaryTelemetry, gas: RotaryGas | null, gasActive: boolean, seconds: number): void {
    const layout = this.layout;
    const shaft = clock.turns * 720;
    const before = this.previousDeg < 0 ? shaft : this.previousDeg;
    this.previousDeg = shaft;
    this.solve(shaft);
    const fade = seconds > 0 ? seconds : 0;
    const firing = !telemetry.cut && !clock.reduced && clock.rpm >= 20 && (clock.burning || !!telemetry.cranking);
    for (let r = 0; r < layout.rotors; r++) {
      this.spark[r] = Math.max(0, this.spark[r] - fade / SPARK_SECONDS);
      if (!firing || shaft <= before) continue;
      // Une étincelle par tour d'arbre, à chaque rotor : elle tombe peu avant le point mort haut et ne doit jamais être manquée.
      const phaseBefore = before - layout.slots[r].firingDeg + ADVANCE_DEG;
      const phaseNow = shaft - layout.slots[r].firingDeg + ADVANCE_DEG;
      if (Math.floor(phaseNow / 360) > Math.floor(phaseBefore / 360)) {
        this.spark[r] = 1;
        this.sparkCount++;
        this.lastSparkRotor = r;
      }
    }
    if (!firing) this.spark.fill(0);
    this.starter += ((telemetry.cranking ? 1 : 0) - this.starter) * (1 - Math.exp(-fade / 0.05));

    const live = !!gas && gasActive && gas.cylinders === layout.chambers && clock.rpm > 20;
    this.gasLive = live;
    if (live && gas) {
      const rate = Math.max(clock.rpm * 0.105, 5);
      for (let c = 0; c < layout.chambers; c++) {
        const mass = Math.max(gas.cylM[c], 1e-9);
        this.gasT[c] = gas.cylT[c];
        this.gasBurnt[c] = gas.cylY[c] / mass;
        const burn = gas.cylBurn[c];
        this.flame[c] = gas.cylFired[c] === 1 && burn > 0.002 && burn < 0.998 && firing ? burn : 0;
        this.jetIn[c] = gas.cylIntake[c] / (mass * rate);
        this.jetEx[c] = gas.cylExhaust[c] / (mass * rate);
      }
    } else {
      this.flame.fill(0);
      this.jetIn.fill(0);
      this.jetEx.fill(0);
    }
  }
}
