/**
 * La boîte comme mécanisme : quelles pièces, où, et comment elles tournent. Tout découle de la fiche calculée par le
 * C++ (`BoxSpec`) : les dents, l'entraxe, le module, l'hélice. Aucune dépendance au DOM ni au moteur de rendu : la vue
 * 2D et la vue 3D dessinent la même chose, et les tests lisent les mêmes nombres.
 *
 * Repère, en millimètres : x le long des arbres, de l'embrayage vers l'arrière de la boîte ; y vers le haut ; z vers
 * l'observateur. Une roue tourne autour de +x : un angle positif va de y vers z. Sa dent 0 est à l'angle du corps
 * qui la porte, plus sa phase.
 */
import type { BoxSpec } from './engine';
import type { MessageKey } from './i18n.svelte';

export type BoxForm = 'synchro' | 'dog' | 'sequential' | 'dual_clutch' | 'converter' | 'reducer';
const FORMS: BoxForm[] = ['synchro', 'dog', 'sequential', 'dual_clutch', 'converter', 'reducer'];

export type BodyRole = 'input' | 'inputOuter' | 'lay' | 'output' | 'outputLay' | 'prop' | 'loose' | 'idler' | 'ring' | 'planet' | 'annulus' | 'sleeve' | 'flywheel' | 'flywheelSecondary' | 'pump';

/** Un corps rigide qui tourne : un arbre et ce qui lui est fixé, une roue folle, un baladeur. */
export interface Body {
  id: number;
  role: BodyRole;
  /** L'axe passe par (y, z), parallèle à x. Pour le pont conique, l'axe est parallèle à z et passe par (x, y). */
  y: number;
  z: number;
  x: number;
  axis: 'x' | 'z';
  /** Satellite : son axe tourne autour du planétaire, avec le porte-satellites. */
  orbit?: { carrier: number; radius: number; angle: number };
}

export type WheelRole = 'driver' | 'driven' | 'inputPinion' | 'inputWheel' | 'reversePinion' | 'reverseIdler' | 'reverseWheel' | 'finalPinion' | 'finalRing' | 'sun' | 'planet' | 'annulus';

export interface Wheel {
  id: number;
  body: number;
  role: WheelRole;
  /** La vitesse qu'elle sert : 1 à 8, -1 la marche arrière, 0 aucune. */
  gear: number;
  teeth: number;
  /** Rayon primitif (mm), au plan de fonctionnement : les deux rayons d'une paire font l'entraxe. */
  radius: number;
  /** Module apparent et module réel (mm). Le premier donne le pas, le second la hauteur de dent. */
  moduleT: number;
  moduleN: number;
  /** Angle d'hélice (rad), signé : une paire porte deux sens opposés. Zéro en denture droite. */
  helix: number;
  x: number;
  width: number;
  y: number;
  z: number;
  phase: number;
  /** Denture intérieure : la couronne d'un train épicycloïdal. */
  internal: boolean;
  /** Couple conique : demi-angle du cône primitif (rad). Les dents s'éloignent du sommet vers `bevelDir`. */
  bevel: { cone: number; dir: 1 | -1; apex: number; width: number } | null;
  /** Monté sur l'arbre avec un roulement : il faut un alésage et un moyeu. */
  free: boolean;
}

export interface Mesh {
  a: number;
  b: number;
  /** Direction du point de contact, vue de chaque roue. */
  contactA: number;
  contactB: number;
  kind: 'external';
}

export interface SelectorSide {
  gear: number;
  /** Le corps auquel le baladeur lie l'arbre : une roue folle, ou l'arbre primaire en prise directe. */
  body: number;
  wheel: number;
  x: number;
}

/** Un moyeu claveté sur l'arbre et son baladeur. `pos` va de -1 (côté gauche engagé) à 1 (côté droit). */
export interface Selector {
  id: number;
  x: number;
  y: number;
  z: number;
  left: SelectorSide | null;
  right: SelectorSide | null;
  sleeveBody: number;
  shaftBody: number;
  hubRadius: number;
  sleeveRadius: number;
  /** Largeur du moyeu, course du baladeur, largeur de la couronne d'engagement (mm). */
  hubWidth: number;
  travel: number;
  dogWidth: number;
  /** La fourchette suit le baladeur depuis le rail ou le barillet. */
  railY: number;
  railZ: number;
  /** Arbre de sélection pour une double embrayage : le pair ou l'impair. */
  parity: 0 | 1 | -1;
}

export interface Shaft {
  body: number;
  role: BodyRole;
  x0: number;
  x1: number;
  y: number;
  z: number;
  radius: number;
  /** Rayon de l'alésage : l'arbre creux d'une double embrayage. */
  bore: number;
}

export interface Bearing {
  x: number;
  y: number;
  z: number;
  inner: number;
  outer: number;
  width: number;
  body: number;
}

export interface PlanetSet {
  x: number;
  width: number;
  y: number;
  sun: number;
  annulus: number;
  planets: number[];
  ringBody: number;
  planetBodies: number[];
  carrierRadius: number;
}

export interface ChainLink {
  from: number;
  to: number;
  factor: number;
}

export interface Anchor {
  key: MessageKey;
  params?: Record<string, string | number>;
  x: number;
  y: number;
  z: number;
  /** Repère secondaire : affiché seulement au survol s'il y a trop de monde. */
  minor: boolean;
}

export interface GearPairView {
  gear: number;
  driver: number;
  driven: number;
  direct: boolean;
  /** Rapport tel qu'il se lit sur les dents dessinées, étage d'entrée compris. */
  drawnRatio: number;
}

export interface Reverse {
  pinion: number;
  idler: number;
  wheel: number;
  idlerBody: number;
  pinionBody: number;
  /** Le pignon intermédiaire coulisse d'autant pour sortir de sa prise. */
  slide: number;
  fork: { railY: number; railZ: number };
}

export interface Drum {
  x0: number;
  x1: number;
  y: number;
  z: number;
  radius: number;
  /** Une position par rapport : R, N, 1, 2, ... ; angle entre deux positions. */
  positions: number[];
  step: number;
  neutral: number;
}

export interface FinalDrive {
  pinion: number;
  ring: number;
  kind: 'helical' | 'bevel';
  ringBody: number;
  /** Centre de la couronne. */
  x: number;
  y: number;
  z: number;
  caseRadius: number;
  caseWidth: number;
}

export interface GearboxLayout {
  form: BoxForm;
  drive: 'front' | 'rear' | 'all';
  flywheel: 'single' | 'dual_mass' | 'none';
  threeShafts: boolean;
  center: number;
  face: number;
  moduleMm: number;
  helixDeg: number;
  spur: boolean;
  bodies: Body[];
  wheels: Wheel[];
  meshes: Mesh[];
  selectors: Selector[];
  shafts: Shaft[];
  bearings: Bearing[];
  pairs: GearPairView[];
  reverse: Reverse | null;
  planets: PlanetSet[];
  final: FinalDrive | null;
  /** Intégrale : la sortie vers l'avant. Le modèle n'en connaît que le rendement : un boîtier et un arbre, sans dents. */
  transfer: { body: number; x0: number; x1: number; y: number; z: number; radius: number } | null;
  drum: Drum | null;
  chain: ChainLink[];
  /** Après le moyeu : de l'arbre aux baladeurs vers la sortie, puis vers le pont. */
  after: ChainLink[];
  anchors: Anchor[];
  /** Corps de chaque arbre. */
  input: number;
  inputOuter: number;
  lay: number;
  /** Ce qui porte les moyeux, et ce que la télémétrie appelle l'arbre de sortie. */
  hubBody: number;
  output: number;
  /** Sens de la sortie quand la voiture avance : -1 si elle tourne à l'inverse du primaire. */
  outputDir: number;
  /** Rapport de la sortie à l'arbre des moyeux. */
  hubToOutput: number;
  flywheelBody: number;
  flywheelSecondaryBody: number;
  /** Rayon et position du volant moteur. */
  flywheelGeom: { x: number; width: number; radius: number; y: number; ringTeeth: number };
  clutchX: number;
  /** Les quatre roues folles que suit le modèle du claquement : corps par position, ou -1. */
  rattle: number[];
  /** L'enveloppe du carter autour des arbres. */
  bounds: { x0: number; x1: number; y0: number; y1: number; z0: number; z1: number };
  /** Niveau d'huile (y) : les roues du bas y trempent. */
  oilY: number;
  /** Nombre de dents de la couronne d'engagement du modèle. */
  dogTeeth: number;
  coneRadius: number;
}

export interface BoxOptions {
  flywheel: 'single' | 'dual_mass';
  drive: 'front' | 'rear' | 'all';
  starterRing?: number;
}

const TAU = Math.PI * 2;
const SPIRAL_DEG = 35; // Comme `finalHelix` dans gearbox.cpp, pour un couple conique.

/** Dents de trains épicycloïdaux publiées pour une boîte à huit rapports : dessinées, pas simulées. */
const PLANET_TEETH: [number, number][] = [[48, 96], [48, 96], [69, 111], [23, 85]];
const PLANET_COUNT = 3;

export const planetSetsFor = (gears: number): number => (gears <= 4 ? 2 : gears <= 6 ? 3 : 4);

export function buildGearbox(box: BoxSpec, options: BoxOptions): GearboxLayout {
  const form = FORMS[box.kind] ?? 'synchro';
  const spur = box.helix <= 0;
  const center = Math.max(box.center, 30);
  const helix = (box.helix * Math.PI) / 180;
  const face = Math.max(box.faceMm, center * 0.22);
  const layout: GearboxLayout = {
    form,
    drive: options.drive,
    flywheel: form === 'reducer' ? 'none' : options.flywheel,
    threeShafts: box.threeShafts,
    center,
    face,
    moduleMm: box.moduleMm,
    helixDeg: box.helix,
    spur,
    bodies: [],
    wheels: [],
    meshes: [],
    selectors: [],
    shafts: [],
    bearings: [],
    pairs: [],
    reverse: null,
    planets: [],
    final: null,
    transfer: null,
    drum: null,
    chain: [],
    after: [],
    anchors: [],
    input: 0,
    inputOuter: -1,
    lay: -1,
    hubBody: 0,
    output: 0,
    outputDir: -1,
    hubToOutput: 1,
    flywheelBody: -1,
    flywheelSecondaryBody: -1,
    flywheelGeom: { x: 0, width: 0, radius: 0, y: 0, ringTeeth: options.starterRing ?? 0 },
    clutchX: 0,
    rattle: [-1, -1, -1, -1],
    bounds: { x0: 0, x1: 0, y0: 0, y1: 0, z0: 0, z1: 0 },
    oilY: 0,
    dogTeeth: box.dogTeeth,
    coneRadius: box.coneRadiusMm,
  };
  if (box.gears.length === 0) return layout;
  const b = new Builder(layout, box, helix);
  if (form === 'converter') b.planetary();
  else if (form === 'reducer') b.reducer();
  else b.layshaftBox();
  b.finish();
  return layout;
}

class Builder {
  readonly center: number;
  readonly face: number;
  /** Rayons des arbres, limités par le plus petit pied de dent. */
  inputR = 0;
  outputR = 0;

  constructor(readonly l: GearboxLayout, readonly box: BoxSpec, readonly helix: number) {
    this.center = l.center;
    this.face = l.face;
  }

  body(role: BodyRole, y: number, z: number, x = 0, axis: 'x' | 'z' = 'x'): number {
    const id = this.l.bodies.length;
    this.l.bodies.push({ id, role, y, z, x, axis });
    return id;
  }

  wheel(role: WheelRole, gear: number, body: number, teeth: number, radius: number, x: number, width: number, helix: number, free = false): number {
    const owner = this.l.bodies[body];
    const moduleT = (2 * radius) / Math.max(teeth, 1);
    const id = this.l.wheels.length;
    this.l.wheels.push({
      id, body, role, gear, teeth, radius, moduleT, moduleN: moduleT * Math.cos(helix), helix, x, width, y: owner.y, z: owner.z, phase: 0, internal: false, bevel: null, free,
    });
    return id;
  }

  /** Deux roues en prise : la phase de la seconde se déduit, pour que ses creux reçoivent les dents de la première. */
  mesh(a: number, b: number, contactA: number, contactB: number, kind: Mesh['kind'] = 'external'): void {
    const wa = this.l.wheels[a];
    const wb = this.l.wheels[b];
    const ta = (wa.teeth * (contactA - wa.phase)) / TAU;
    wb.phase = contactB - (TAU / wb.teeth) * (0.5 - ta);
    this.l.meshes.push({ a, b, contactA, contactB, kind });
  }

  /** Paire parallèle : `a` et `b` sur des axes distincts, le contact sur la ligne des centres. */
  parallelMesh(a: number, b: number): void {
    const wa = this.l.wheels[a];
    const wb = this.l.wheels[b];
    const phi = Math.atan2(wb.z - wa.z, wb.y - wa.y);
    this.mesh(a, b, phi, phi + Math.PI);
  }

  link(list: ChainLink[], from: number, to: number, teethFrom: number, teethTo: number): void {
    list.push({ from, to, factor: -teethFrom / teethTo });
  }

  /** Une boîte à arbres parallèles : manuelle, à crabots, séquentielle, à double embrayage. */
  layshaftBox(): void {
    const { box, l, center, face } = this;
    const dct = l.form === 'dual_clutch';
    const three = box.threeShafts;
    const gears = box.gears;
    const m = this.moduleForTeeth(gears);
    const smallest = Math.min(...gears.filter((g) => !g.direct).map((g) => (center * Math.min(g.driver, g.driven)) / (g.driver + g.driven) - 1.25 * m), center * 0.3);
    this.inputR = clamp(center * 0.16, 6, Math.max(smallest * 0.9, 6));
    this.outputR = clamp(center * 0.2, 7, Math.max(smallest * 0.95, 7));
    const yTop = center / 2;
    const yBottom = -center / 2;

    // Trois arbres : primaire et sortie sur le même axe en haut, intermédiaire en bas. Deux arbres : primaire en haut, sortie en bas.
    const coaxial = three && !dct;
    const inner = this.body('input', yTop, 0);
    l.input = inner;
    let outer = -1;
    if (dct) {
      outer = this.body('inputOuter', yTop, 0);
      l.inputOuter = outer;
    }
    let lay = -1;
    if (coaxial) {
      lay = this.body('lay', yBottom, 0);
      l.lay = lay;
      this.link(l.chain, inner, lay, box.inputDriver, box.inputDriven);
    }
    const hub = this.body(dct && three ? 'outputLay' : 'output', coaxial ? yTop : yBottom, 0);
    l.hubBody = hub;
    l.output = hub;
    l.outputDir = coaxial ? 1 : -1;
    const hubY = l.bodies[hub].y;

    const ringWidth = Math.max(face * 0.2, 3);
    const hubWidth = Math.max(face * 0.5, 8);
    const gap = Math.max(face * 0.1, 1.5);
    const slot = face + ringWidth + gap;

    let x = 0;
    let finalX = 0;
    const startsWithFinal = !box.hypoid && !coaxial;
    const finalWidth = face * 1.15;
    const bearingWidth = Math.max(face * 0.5, 8);
    if (startsWithFinal) {
      finalX = 6 + bearingWidth + finalWidth / 2;
      x = finalX + finalWidth / 2 + gap;
    } else {
      x = 6 + bearingWidth;
    }

    // Paire en prise constante, en tête de boîte longitudinale.
    if (coaxial) {
      const r1 = (center * box.inputDriver) / (box.inputDriver + box.inputDriven);
      const w1 = this.wheel('inputPinion', 0, inner, box.inputDriver, r1, x + face / 2, face, this.helix);
      const w2 = this.wheel('inputWheel', 0, lay, box.inputDriven, center - r1, x + face / 2, face, -this.helix);
      this.parallelMesh(w1, w2);
      x += face + gap;
    }

    const groups = arrangeGears(gears.map((g) => g.gear), dct, three, gears.find((g) => g.direct)?.gear ?? 0);
    const directGear = gears.find((g) => g.direct)?.gear ?? 0;
    const maxHub = Math.min(box.coneRadiusMm > 0 ? box.coneRadiusMm : center * 0.45, center - this.inputR - 5);
    const sleeveRadius = Math.max(maxHub * 1.04, this.outputR + 6);
    const hubRadius = Math.max(sleeveRadius * 0.82, this.outputR + 4);
    const biggestDriven = Math.max(...gears.map((g) => (g.direct ? 0 : (center * g.driven) / (g.driver + g.driven))), sleeveRadius);
    const railDistance = biggestDriven + 9;
    const railY = hubY - railDistance * 0.72;
    const railZ = railDistance * 0.69;

    const driverShaft = (gear: number) => {
      if (coaxial) return lay;
      if (dct) return gear % 2 === 0 ? outer : inner;
      return inner;
    };

    /** La paire d'un rapport, à sa place. Renvoie le côté du baladeur. */
    const addPair = (gear: number, wheelX: number): SelectorSide => {
      const g = gears[gear - 1];
      let driverTeeth = g.driver;
      let drivenTeeth = g.driven;
      // Double embrayage longitudinale : un seul axe ne peut pas être en prise directe avec les deux arbres. Les dents qui
      // annulent l'étage d'entrée donnent ce rapport de 1.
      if (g.direct && dct) {
        driverTeeth = box.inputDriven;
        drivenTeeth = box.inputDriver;
      }
      const owner = driverShaft(gear);
      if (g.direct && !dct) {
        l.pairs.push({ gear, driver: 0, driven: 0, direct: true, drawnRatio: 1 });
        return { gear, body: inner, wheel: -1, x: wheelX };
      }
      const r1 = (center * driverTeeth) / (driverTeeth + drivenTeeth);
      const pinion = this.wheel('driver', gear, owner, driverTeeth, r1, wheelX, face, this.helix);
      const looseBody = this.body('loose', hubY, 0, wheelX);
      const wheel = this.wheel('driven', gear, looseBody, drivenTeeth, center - r1, wheelX, face, -this.helix, true);
      this.parallelMesh(pinion, wheel);
      this.link(l.chain, owner, looseBody, driverTeeth, drivenTeeth);
      const stage = three ? box.inputDriven / Math.max(box.inputDriver, 1) : 1;
      l.pairs.push({ gear, driver: pinion, driven: wheel, direct: false, drawnRatio: (drivenTeeth / driverTeeth) * stage });
      if (gear - 1 < 4) l.rattle[gear - 1] = looseBody;
      return { gear, body: looseBody, wheel, x: wheelX };
    };

    const addReverse = (startX: number): number => {
      const [zp, zi, zw] = box.reverseTeeth;
      if (zp <= 0 || zi <= 0 || zw <= 0) return startX;
      const width = face * 0.85;
      const clearance = 2.2 * box.moduleMm;
      // Le modèle prend ses dents au module de la boîte : pignon et roue dépasseraient parfois l'entraxe. On les dessine au module qui passe.
      const k = Math.min(box.moduleMm, (2 * (center - clearance)) / (zp + zw));
      const rp = (k * zp) / 2;
      const rw = (k * zw) / 2;
      const ri = (k * zi) / 2;
      const pinionBody = coaxial ? lay : dct ? outer : inner;
      const pinionY = l.bodies[pinionBody].y;
      const wheelY = hubY;
      // Le triangle : du pignon à l'intermédiaire, de l'intermédiaire à la roue.
      const a = rp + ri;
      const c = rw + ri;
      const base = Math.abs(pinionY - wheelY);
      const along = (a * a - c * c + base * base) / (2 * base);
      const off = Math.sqrt(Math.max(a * a - along * along, 1));
      const dirY = wheelY > pinionY ? 1 : -1;
      const idlerY = pinionY + dirY * along;
      const idlerZ = off;
      const wheelX = startX + width / 2;
      const pinion = this.wheel('reversePinion', -1, pinionBody, zp, rp, wheelX, width, 0);
      const idlerBody = this.body('idler', idlerY, idlerZ, wheelX);
      const idler = this.wheel('reverseIdler', -1, idlerBody, zi, ri, wheelX, width, 0);
      const wheel = this.wheel('reverseWheel', -1, hub, zw, rw, wheelX, width, 0);
      this.parallelMesh(pinion, idler);
      this.parallelMesh(idler, wheel);
      l.reverse = { pinion, idler, wheel, idlerBody, pinionBody, slide: width + 3, fork: { railY, railZ } };
      return startX + 2 * width + 3 + gap;
    };

    let reverseDone = false;
    let selectorId = 0;
    for (const group of groups) {
      if (group.reverseBefore && !reverseDone) {
        x = addReverse(x);
        reverseDone = true;
      }
      const [leftGear, rightGear] = group.gears;
      // La prise directe n'a pas de roue : sa couronne d'engagement est taillée au dos du pignon du primaire.
      const directLeft = leftGear > 0 && leftGear === directGear && !dct;
      const leftRoom = leftGear > 0 ? (directLeft ? ringWidth + gap : slot) : 0;
      const hubX = x + gap + leftRoom + hubWidth / 2;
      const left = leftGear > 0 ? addPair(leftGear, directLeft ? hubX - hubWidth / 2 - gap - ringWidth / 2 : hubX - hubWidth / 2 - ringWidth - gap - face / 2) : null;
      const right = rightGear > 0 ? addPair(rightGear, hubX + hubWidth / 2 + ringWidth + gap + face / 2) : null;
      const width = 2 * gap + leftRoom + hubWidth + (rightGear > 0 ? slot : 0);
      const sleeveBody = this.body('sleeve', hubY, 0, hubX);
      const parity = dct ? ((leftGear || rightGear) % 2 === 0 ? 0 : 1) : -1;
      l.selectors.push({
        id: selectorId++, x: hubX, y: hubY, z: 0, left, right, sleeveBody, shaftBody: hub, hubRadius, sleeveRadius, hubWidth, travel: ringWidth + gap, dogWidth: ringWidth, railY, railZ, parity: parity as 0 | 1 | -1,
      });
      x += width;
    }
    if (!reverseDone) x = addReverse(x);
    const lastWheelEnd = x;
    // Place pour le roulement du bout, au-delà de la dernière roue.
    const gearsEnd = lastWheelEnd + bearingWidth + 2;

    // Les arbres.
    const firstSelector = l.selectors[0];
    const firstIsDirect = coaxial && firstSelector?.left?.wheel === -1;
    const inputEnd = coaxial ? (firstIsDirect ? firstSelector.x - hubWidth / 2 - 1 : (l.wheels[0].x + face / 2 + gap / 2)) : gearsEnd;
    const evens = dct ? l.wheels.filter((w) => w.body === outer) : [];
    const evenEnd = evens.length > 0 ? Math.max(...evens.map((w) => w.x + w.width / 2)) + 3 : 40;
    if (dct) {
      l.shafts.push({ body: outer, role: 'inputOuter', x0: -12, x1: evenEnd, y: yTop, z: 0, radius: this.inputR * 1.45, bore: this.inputR * 1.05 });
      l.shafts.push({ body: inner, role: 'input', x0: -18, x1: gearsEnd, y: yTop, z: 0, radius: this.inputR, bore: 0 });
    } else {
      l.shafts.push({ body: inner, role: 'input', x0: -18, x1: inputEnd, y: yTop, z: 0, radius: this.inputR, bore: 0 });
    }
    if (coaxial) l.shafts.push({ body: lay, role: 'lay', x0: 4, x1: gearsEnd, y: yBottom, z: 0, radius: this.inputR, bore: 0 });
    const hubStart = coaxial ? inputEnd + 1 : 4;
    let hubEnd = gearsEnd;

    let tail = hub;
    if (dct && three) {
      // L'étage d'entrée du modèle devient un étage de sortie, en bout de boîte : même produit, donc même rapport.
      const r1 = (center * box.inputDriver) / (box.inputDriver + box.inputDriven);
      const wx = lastWheelEnd + bearingWidth * 0.2 + face / 2;
      const a = this.wheel('inputPinion', 0, hub, box.inputDriver, r1, wx, face, this.helix);
      tail = this.body('output', yTop, 0, wx);
      const c = this.wheel('inputWheel', 0, tail, box.inputDriven, center - r1, wx, face, -this.helix);
      this.parallelMesh(a, c);
      this.link(l.after, hub, tail, box.inputDriver, box.inputDriven);
      l.shafts.push({ body: tail, role: 'output', x0: wx - face / 2 - 1, x1: wx + face / 2 + 4, y: yTop, z: 0, radius: this.outputR, bore: 0 });
      l.output = tail;
      l.outputDir = 1;
      l.hubToOutput = -box.inputDriver / box.inputDriven;
      hubEnd = wx + face / 2 + 2;
    }
    l.shafts.push({ body: hub, role: dct && three ? 'outputLay' : 'output', x0: hubStart, x1: hubEnd, y: hubY, z: 0, radius: this.outputR, bore: 0 });

    // Le pont.
    const endX = hubEnd;
    if (box.hypoid) {
      this.bevelFinal(tail, l.bodies[tail].y, endX + 4);
    } else {
      this.helicalFinal(tail, finalX, finalWidth, l.bodies[tail].y);
    }

    // Roulements aux deux bouts de chaque arbre.
    const bearing = (shaft: Shaft) => {
      const w = Math.max(this.face * 0.5, 8);
      const outerR = shaft.radius * 1.75 + 3;
      l.bearings.push({ x: shaft.x0 + w / 2 + (shaft.x0 < 0 ? 12 : 0), y: shaft.y, z: shaft.z, inner: shaft.radius, outer: outerR, width: w, body: shaft.body });
      if (shaft.role !== 'inputOuter') l.bearings.push({ x: shaft.x1 - w / 2, y: shaft.y, z: shaft.z, inner: shaft.radius, outer: outerR, width: w, body: shaft.body });
    };
    l.shafts.forEach(bearing);

    if (l.form === 'sequential') {
      const positions: number[] = [];
      for (let i = -1; i <= gears.length; i++) positions.push(i);
      const lastHubX = Math.max(...l.selectors.map((s) => s.x));
      const firstHubX = Math.min(...l.selectors.map((s) => s.x));
      l.drum = {
        x0: firstHubX - 12,
        x1: lastHubX + 12,
        y: railY,
        z: railZ,
        radius: Math.max(center * 0.14, 8),
        positions,
        step: TAU / (positions.length + 2),
        neutral: 1,
      };
    }

    this.input(-22);
  }

  moduleForTeeth(gears: BoxSpec['gears']): number {
    const g = gears.find((item) => !item.direct);
    const sum = g ? g.driver + g.driven : 40;
    return ((2 * this.center) / sum) * Math.cos(this.helix);
  }

  /** Le volant, l'embrayage, à gauche de l'arbre primaire. */
  input(rightOf: number): void {
    const { l, center } = this;
    const y = l.bodies[l.input].y;
    const width = Math.max(center * 0.3, 16);
    const radius = Math.max(center * 1.08, 60);
    const x = rightOf - width / 2 - (l.form === 'dual_clutch' ? 34 : 18);
    l.flywheelBody = this.body('flywheel', y, 0, x);
    l.flywheelSecondaryBody = this.body('flywheelSecondary', y, 0, x);
    l.flywheelGeom = { x, width, radius, y, ringTeeth: this.l.flywheelGeom.ringTeeth };
    l.clutchX = rightOf - (l.form === 'dual_clutch' ? 16 : 8);
  }

  /** Pont de traction : une paire hélicoïdale, la couronne loin derrière, sur le différentiel. */
  helicalFinal(tail: number, x: number, width: number, shaftY: number): void {
    const { box, l } = this;
    const zp = box.finalPinion;
    const zr = box.finalRing;
    if (zp <= 0 || zr <= 0) return;
    const m = (box.moduleMm * 1.1) / Math.cos(this.helix);
    const rp = (zp * m) / 2;
    const rr = (zr * m) / 2;
    const c = rp + rr;
    const phi = (-122 * Math.PI) / 180;
    const ringY = shaftY + c * Math.cos(phi);
    const ringZ = c * Math.sin(phi);
    const ringBody = this.body('ring', ringY, ringZ, x);
    const pinion = this.wheel('finalPinion', 0, tail, zp, rp, x, width, this.helix);
    const ring = this.wheel('finalRing', 0, ringBody, zr, rr, x, width, -this.helix);
    this.parallelMesh(pinion, ring);
    this.link(l.after, tail, ringBody, zp, zr);
    l.final = { pinion, ring, kind: 'helical', ringBody, x, y: ringY, z: ringZ, caseRadius: rr * 0.52, caseWidth: width * 3.2 };
  }

  /** Pont arrière ou intégrale : couple conique en bout d'arbre, axes à 90°. Le décalage hypoïde n'est pas dessiné. */
  bevelFinal(tail: number, shaftY: number, startX: number): void {
    const { box, l } = this;
    const zp = box.finalPinion;
    const zr = box.finalRing;
    if (zp <= 0 || zr <= 0) return;
    // Le module de pont du modèle (`contactRatios`, 1,1 fois celui de la boîte).
    const m = box.moduleMm * 1.1;
    const rp = (zp * m) / 2;
    const rr = (zr * m) / 2;
    const cone = Math.atan2(rp, rr);
    const distance = Math.hypot(rp, rr);
    const width = distance * 0.3;
    const apexX = startX + 18 + rr;
    const spiral = (SPIRAL_DEG * Math.PI) / 180;
    const ringBody = this.body('ring', shaftY, 0, apexX, 'z');
    const pinion = this.wheel('finalPinion', 0, tail, zp, rp, apexX - rr, width, spiral);
    const wp = l.wheels[pinion];
    wp.bevel = { cone, dir: -1, apex: apexX, width };
    const ring = this.wheel('finalRing', 0, ringBody, zr, rr, apexX, width, -spiral);
    const wr = l.wheels[ring];
    // La couronne est derrière l'axe du pignon, ses dents tournées vers l'observateur.
    wr.bevel = { cone: Math.PI / 2 - cone, dir: -1, apex: 0, width };
    // Le contact, côté -z de l'axe du pignon ; vu de la couronne, côté -x.
    this.mesh(pinion, ring, -Math.PI / 2, Math.PI);
    l.after.push({ from: tail, to: ringBody, factor: -zp / zr });
    l.final = { pinion, ring, kind: 'bevel', ringBody, x: apexX, y: shaftY, z: 0, caseRadius: rr * 0.55, caseWidth: rr * 0.9 };
    // Sur une vraie voiture, un arbre de transmission relie la boîte au pont : ici un tronçon, le pont est ramené contre la boîte.
    const prop = Math.max(rp * 0.5, 5);
    l.shafts.push({ body: tail, role: 'prop', x0: startX - 6, x1: apexX - rr + width * 0.2, y: shaftY, z: 0, radius: prop, bore: 0 });
    if (l.drive === 'all') {
      // La boîte de transfert : même vitesse que la sortie, vers l'essieu avant, donc vers l'embrayage.
      const y = shaftY - this.center * 0.95;
      const z = this.center * 0.55;
      const body = this.body('prop', y, z);
      l.after.push({ from: tail, to: body, factor: 1 });
      l.transfer = { body, x0: startX - this.center * 2.4, x1: startX + 10, y, z, radius: Math.max(prop * 0.8, 4) };
    }
  }

  /** Automatique : convertisseur, puis trains épicycloïdaux en ligne. Le simulateur ne calcule que le rapport : c'est un schéma. */
  planetary(): void {
    const { box, l, center } = this;
    const count = planetSetsFor(box.gears.length);
    const y = 0;
    const input = this.body('input', y, 0);
    const output = this.body('output', y, 0);
    l.input = input;
    l.hubBody = output;
    l.output = output;
    l.outputDir = 1;
    const ringR = center * 0.92;
    const width = Math.max(this.face * 0.75, 12);
    const pitch = width + Math.max(this.face * 0.85, 12);
    let x = 14;
    for (let i = 0; i < count; i++) {
      const [zs, zr] = PLANET_TEETH[(PLANET_TEETH.length - count + i) % PLANET_TEETH.length];
      const zp = (zr - zs) / 2;
      const module = (2 * ringR) / zr;
      const rs = (zs * module) / 2;
      const rp = (zp * module) / 2;
      const sx = x + width / 2;
      const sun = this.wheel('sun', 0, input, zs, rs, sx, width, 0);
      const annulusBody = this.body('annulus', y, 0, sx);
      const annulus = this.wheel('annulus', 0, annulusBody, zr, ringR, sx, width, 0);
      l.wheels[annulus].internal = true;
      const planets: number[] = [];
      const planetBodies: number[] = [];
      const orbitR = rs + rp;
      for (let n = 0; n < PLANET_COUNT; n++) {
        const psi = (n / PLANET_COUNT) * TAU;
        const pb = this.body('planet', y, 0, sx);
        l.bodies[pb].orbit = { carrier: output, radius: orbitR, angle: psi };
        const p = this.wheel('planet', 0, pb, zp, rp, sx, width, 0);
        // Vu du planétaire, le contact est dans la direction de l'axe du satellite ; vu du satellite, à l'opposé.
        const ts = (zs * (psi - l.wheels[sun].phase)) / TAU;
        l.wheels[p].phase = psi + Math.PI - (TAU / zp) * (0.5 - ts);
        planets.push(p);
        planetBodies.push(pb);
      }
      // Couronne : le contact avec le premier satellite fixe sa phase, les autres suivent par la condition de montage.
      const first = l.wheels[planets[0]];
      const tp = (first.teeth * (0 - first.phase)) / TAU;
      l.wheels[annulus].phase = 0 - (TAU / zr) * (tp - 0.5);
      l.planets.push({ x: sx, width, y, sun, annulus, planets, ringBody: annulusBody, planetBodies, carrierRadius: orbitR });
      x += pitch;
    }
    const end = x;
    l.shafts.push({ body: input, role: 'input', x0: -50, x1: end - pitch + width + 4, y, z: 0, radius: center * 0.13, bore: 0 });
    l.shafts.push({ body: output, role: 'output', x0: 12, x1: end + 8, y, z: 0, radius: center * 0.2, bore: center * 0.14 });
    const w = Math.max(this.face * 0.5, 8);
    l.bearings.push({ x: 2, y, z: 0, inner: center * 0.13, outer: center * 0.3, width: w, body: input });
    l.bearings.push({ x: end + 8 - w / 2, y, z: 0, inner: center * 0.2, outer: center * 0.4, width: w, body: output });

    if (box.hypoid) {
      this.outputR = center * 0.2;
      this.bevelFinal(output, y, end + 8);
    } else {
      this.helicalFinal(output, end + 14 + this.face * 0.6, this.face * 1.15, y);
      const shaft = l.shafts[l.shafts.length - 1];
      shaft.x1 = end + 20 + this.face * 1.2;
    }
    this.input(-22);
    l.flywheelGeom.x = -86;
    l.flywheelGeom.width = 12;
    l.clutchX = -40;
  }

  /** Électrique : deux étages cylindriques, le moteur en bout d'arbre, sans volant ni baladeur. */
  reducer(): void {
    const { box, l } = this;
    const g = box.gears[0];
    const m = box.moduleMm / Math.cos(this.helix);
    const z1 = g.driver;
    const z2 = g.driven;
    const c1 = ((z1 + z2) * m) / 2;
    const input = this.body('input', c1 / 2, 0);
    const lay = this.body('output', -c1 / 2, 0);
    l.input = input;
    l.hubBody = lay;
    l.output = lay;
    l.outputDir = -1;
    const smallest = Math.min((z1 * m) / 2, (box.finalPinion * m) / 2) - 1.25 * box.moduleMm;
    this.inputR = clamp(c1 * 0.09, 6, Math.max(smallest * 0.8, 6));
    this.outputR = this.inputR * 1.15;
    const face = this.face;
    const x1 = 10 + face / 2;
    const x2 = x1 + face + 14;
    const a = this.wheel('driver', 1, input, z1, (z1 * m) / 2, x1, face, this.helix);
    const c = this.wheel('driven', 1, lay, z2, (z2 * m) / 2, x1, face, -this.helix);
    this.parallelMesh(a, c);
    this.link(l.chain, input, lay, z1, z2);
    l.pairs.push({ gear: 1, driver: a, driven: c, direct: false, drawnRatio: z2 / z1 });
    l.shafts.push({ body: input, role: 'input', x0: -18, x1: x1 + face / 2 + 6, y: c1 / 2, z: 0, radius: this.inputR, bore: 0 });
    l.shafts.push({ body: lay, role: 'output', x0: 2, x1: x2 + face * 1.2 + 6, y: -c1 / 2, z: 0, radius: this.outputR, bore: 0 });
    const w = Math.max(face * 0.5, 8);
    l.bearings.push({ x: 2, y: c1 / 2, z: 0, inner: this.inputR, outer: this.inputR * 1.9, width: w, body: input });
    l.bearings.push({ x: x1 + face / 2 + 6 - w / 2, y: c1 / 2, z: 0, inner: this.inputR, outer: this.inputR * 1.9, width: w, body: input });
    l.bearings.push({ x: 6, y: -c1 / 2, z: 0, inner: this.outputR, outer: this.outputR * 1.9, width: w, body: lay });
    l.bearings.push({ x: x2 + face * 1.2 + 6 - w / 2, y: -c1 / 2, z: 0, inner: this.outputR, outer: this.outputR * 1.9, width: w, body: lay });
    this.helicalFinal2(lay, x2, face * 1.2, -c1 / 2, m);
    l.flywheelGeom = { x: -74, width: 96, radius: Math.max(c1 * 0.62, 55), y: c1 / 2, ringTeeth: 0 };
    l.clutchX = -18;
  }

  /** Second étage du réducteur : même module que le premier, pas le module de pont d'une boîte. */
  helicalFinal2(tail: number, x: number, width: number, shaftY: number, m: number): void {
    const { box, l } = this;
    const rp = (box.finalPinion * m) / 2;
    const rr = (box.finalRing * m) / 2;
    const c = rp + rr;
    const phi = (-115 * Math.PI) / 180;
    const ringY = shaftY + c * Math.cos(phi);
    const ringZ = c * Math.sin(phi);
    const ringBody = this.body('ring', ringY, ringZ, x);
    const pinion = this.wheel('finalPinion', 0, tail, box.finalPinion, rp, x, width, this.helix);
    const ring = this.wheel('finalRing', 0, ringBody, box.finalRing, rr, x, width, -this.helix);
    this.parallelMesh(pinion, ring);
    this.link(l.after, tail, ringBody, box.finalPinion, box.finalRing);
    l.final = { pinion, ring, kind: 'helical', ringBody, x, y: ringY, z: ringZ, caseRadius: rr * 0.52, caseWidth: width * 3 };
  }

  finish(): void {
    const { l, center } = this;
    let x0 = Infinity;
    let x1 = -Infinity;
    let y0 = Infinity;
    let y1 = -Infinity;
    let z0 = Infinity;
    let z1 = -Infinity;
    for (const w of l.wheels) {
      if (w.role === 'finalRing' || w.role === 'finalPinion') continue;
      const outer = w.radius + w.moduleN + 2;
      x0 = Math.min(x0, w.x - w.width / 2);
      x1 = Math.max(x1, w.x + w.width / 2 + (w.role === 'reverseIdler' ? l.reverse?.slide ?? 0 : 0));
      y0 = Math.min(y0, w.y - outer);
      y1 = Math.max(y1, w.y + outer);
      z0 = Math.min(z0, w.z - outer);
      z1 = Math.max(z1, w.z + outer);
    }
    for (const s of l.selectors) {
      y0 = Math.min(y0, s.railY - 6);
      z1 = Math.max(z1, s.railZ + 6);
    }
    if (!Number.isFinite(x0)) {
      x0 = 0;
      x1 = 100;
      y0 = -center;
      y1 = center;
      z0 = -center;
      z1 = center;
    }
    const pad = 6;
    l.bounds = { x0: x0 - pad, x1: x1 + pad, y0: y0 - pad, y1: y1 + pad, z0: z0 - pad, z1: z1 + pad };
    l.oilY = y0 + (y1 - y0) * 0.2;
    this.anchors();
  }

  anchors(): void {
    const { l } = this;
    const add = (key: MessageKey, x: number, y: number, z: number, minor = false, params?: Record<string, string | number>) => l.anchors.push({ key, x, y, z, minor, params });
    const inShaft = l.shafts.find((s) => s.role === 'input');
    if (inShaft) add('box.part.input', inShaft.x0 + 6, inShaft.y + inShaft.radius, inShaft.z);
    const outerShaft = l.shafts.find((s) => s.role === 'inputOuter');
    if (outerShaft) add('box.part.inputOuter', (outerShaft.x0 + outerShaft.x1) / 2, outerShaft.y + outerShaft.radius, outerShaft.z);
    const layShaft = l.shafts.find((s) => s.role === 'lay');
    if (layShaft) add('box.part.lay', layShaft.x0 + 8, layShaft.y - layShaft.radius, layShaft.z);
    const outShaft = l.shafts.find((s) => s.role === 'output' || s.role === 'outputLay');
    if (outShaft) add('box.part.output', outShaft.x1 - 6, outShaft.y - outShaft.radius, outShaft.z);
    const firstSel = l.selectors[0];
    if (firstSel) {
      add(l.form === 'synchro' ? 'box.part.sleeve' : 'box.part.dogRing', firstSel.x, firstSel.y + firstSel.sleeveRadius, firstSel.z);
      if (l.form === 'synchro') add('box.part.synchro', firstSel.x - firstSel.hubWidth / 2, firstSel.y - firstSel.sleeveRadius, firstSel.z, true);
      add('box.part.fork', firstSel.x, firstSel.railY, firstSel.railZ, true);
    }
    if (l.reverse) add('box.part.idler', l.wheels[l.reverse.idler].x, l.wheels[l.reverse.idler].y, l.wheels[l.reverse.idler].z, true);
    if (l.final) {
      const ring = l.wheels[l.final.ring];
      add('box.part.ring', l.final.x, l.final.y - ring.radius * 0.5, l.final.z, false, { teeth: ring.teeth });
      add('box.part.pinion', l.wheels[l.final.pinion].x, l.wheels[l.final.pinion].y, l.wheels[l.final.pinion].z, true, { teeth: l.wheels[l.final.pinion].teeth });
    }
    if (l.transfer) add('box.part.transfer', l.transfer.x1 - 10, l.transfer.y, l.transfer.z);
    if (l.drum) add('box.part.drum', (l.drum.x0 + l.drum.x1) / 2, l.drum.y, l.drum.z);
    if (l.planets[0]) {
      add('box.part.planet', l.planets[0].x, l.planets[0].y + l.wheels[l.planets[0].annulus].radius, 0);
    }
    const fw = l.flywheelGeom;
    if (l.form === 'reducer') add('box.part.rotor', fw.x, fw.y + fw.radius * 0.7, 0);
    else if (l.form === 'converter') add('box.part.converter', -60, fw.y + fw.radius * 0.8, 0);
    else if (l.flywheel === 'dual_mass') add('box.part.dualMass', fw.x, fw.y + fw.radius, 0);
    else add('box.part.flywheel', fw.x, fw.y + fw.radius, 0);
    for (const b of l.bearings.slice(0, 1)) add('box.part.bearing', b.x, b.y + b.outer, b.z, true);
    add('box.part.oil', (l.bounds.x0 + l.bounds.x1) / 2, l.oilY, l.bounds.z1 * 0.6, true);
  }
}

const clamp = (value: number, low: number, high: number) => Math.max(low, Math.min(high, value));

interface Group {
  gears: [number, number];
  reverseBefore: boolean;
}

/**
 * L'ordre des rapports le long des arbres, par moyeu. Double embrayage : les pairs près de l'embrayage, sur l'arbre
 * creux, puis les impairs sur l'arbre plein. Boîte longitudinale : la prise directe contre le primaire, puis en
 * descendant jusqu'à la première ; les rapports longs à l'arrière. Boîte transversale : de la première à la dernière.
 */
export function arrangeGears(gears: number[], dct: boolean, three: boolean, direct: number): Group[] {
  const out: Group[] = [];
  const twoByTwo = (list: number[], reverseAfterFirst: boolean) => {
    for (let i = 0; i < list.length; i += 2) {
      const pair: [number, number] = [list[i], list[i + 1] ?? 0];
      out.push({ gears: pair, reverseBefore: reverseAfterFirst && i === 2 });
    }
  };
  if (dct) {
    const evens = gears.filter((g) => g % 2 === 0);
    const odds = gears.filter((g) => g % 2 === 1);
    twoByTwo(evens, true);
    // Sans second groupe de pairs, la marche arrière passe avant les impairs : elle reste sur l'arbre creux.
    if (evens.length <= 2 && out.length > 0) {
      const before = out.length;
      twoByTwo(odds, false);
      if (out[before]) out[before].reverseBefore = true;
    } else {
      twoByTwo(odds, false);
    }
    return out;
  }
  if (three && direct > 0) {
    const below = gears.filter((g) => g <= direct).reverse();
    const above = gears.filter((g) => g > direct);
    twoByTwo(below, false);
    twoByTwo(above, false);
    return out;
  }
  if (three) {
    twoByTwo([...gears].reverse(), false);
    return out;
  }
  twoByTwo(gears, true);
  return out;
}

/** Ce que la boîte fait à cet instant, lu de la télémétrie et de l'horloge partagée. */
export interface BoxFrame {
  inputTotal: number;
  outputTotal: number;
  flywheelTotal: number;
  inputRpm: number;
  outputRpm: number;
  rpm: number;
  gear: number;
  meshed: boolean;
  step: number;
  pending: number;
  coneSlip: number;
  grinding: number;
  rattle: number;
  loose0: number;
  loose1: number;
  loose2: number;
  loose3: number;
  throttle: number;
  clutch: number;
  clutchTorque: number;
  frozen: boolean;
}

export const newFrame = (): BoxFrame => ({
  inputTotal: 0, outputTotal: 0, flywheelTotal: 0, inputRpm: 0, outputRpm: 0, rpm: 0, gear: 0, meshed: false, step: 0, pending: 0, coneSlip: 0, grinding: 0, rattle: 0, loose0: 0, loose1: 0, loose2: 0, loose3: 0, throttle: 0, clutch: 1, clutchTorque: 0, frozen: false,
});

/** Course du baladeur, en part de sa course totale : la bague touche le cône ici. */
export const RING_CONTACT = 0.55;
/** Une pleine course prend au moins ce temps à l'écran : un passage de 50 ms doit se voir. */
const SLEEVE_TRAVEL_S = 0.16;
/** Les rampes d'un double volant : couple par degré, et débattement (valeurs d'ordre de grandeur, dessinées). */
export const DMF_NM_PER_DEG = 12;
export const DMF_MAX_DEG = 45;
/** Part du pas que traverse une roue folle en claquant : le vrai jeu serait invisible, on l'agrandit sans faire se toucher les dents. */
export const RATTLE_PITCH_SHARE = 0.05;
/** Le temps minimal à l'écran du baladeur contre le cône, ou contre les crabots. */
const SYNC_SHOWN_S = 0.2;
const DOGS_SHOWN_S = 0.07;

/**
 * Les angles de toutes les pièces. Le primaire vient de l'horloge partagée ; le reste s'en déduit par les dents, ce
 * qui garde les paires en prise à toute vitesse et à tout rapport. Rien n'est alloué à chaque image.
 */
export class GearboxMotion {
  readonly angles: Float64Array;
  readonly rpm: Float64Array;
  /** Position de chaque baladeur, de -1 à 1, et sa cible. */
  readonly pos: Float64Array;
  readonly target: Float64Array;
  /** Le baladeur secoué quand il force, en part de sa course. */
  shake = 0;
  /** Sortie de l'intermédiaire de marche arrière : 0 dégagé, 1 en prise. */
  reverseIn = 0;
  /** Position du barillet, en positions : 0 la marche arrière. */
  drum = 0;
  /** Le cliquet : 0 au repos, 1 au bout de sa course. */
  pawl = 0;
  /** Rapport auquel la sortie est liée, 0 aucun, et rapport présélectionné d'une double embrayage. */
  engaged = 0;
  preselected = 0;
  /** Part du couple sur l'embrayage de l'arbre creux (double embrayage). */
  clutchMix = 0;
  /** Choc à l'engagement, de 1 à 0. */
  clunk = 0;
  /** Écart de vitesses au baladeur qui s'engage (tr/min), et le côté où il frotte. */
  syncGapRpm = 0;
  syncSelector = -1;
  /** Compression des ressorts d'un double volant, de -1 à 1. */
  windup = 0;
  /** Le rapport d'où l'on vient, le temps du relais d'embrayages. */
  private held = 0;
  private lastInput = 0;
  private lastOutput = 0;
  private lastGear = 0;
  private lastMeshed = false;
  private started = false;
  private lockGear = 0;
  private lockOffset = 0;
  private residual = 0;
  private otherOmega = 0;
  private time = 0;
  private preselectWanted = 0;
  private preselectSince = 0;
  private reverseOffset = 0;
  /** Le passage montré : rapport visé (0 aucun), étape, temps passé dans l'étape, et le rapport où le baladeur est engagé. */
  private seqTo = 0;
  private seqPhase = 0;
  private seqTime = 0;
  private shownGear = 0;
  private readonly sideSelector = new Int32Array(9).fill(-1);
  private readonly sideSign = new Int32Array(9);
  private readonly sideBody = new Int32Array(9).fill(-1);
  private readonly preFactor = new Float64Array(9);
  private readonly rattlePitch = new Float64Array(4);
  private readonly frameLoose = new Float64Array(4);

  constructor(readonly layout: GearboxLayout) {
    this.angles = new Float64Array(layout.bodies.length);
    this.rpm = new Float64Array(layout.bodies.length);
    this.pos = new Float64Array(layout.selectors.length);
    this.target = new Float64Array(layout.selectors.length);
    layout.selectors.forEach((selector, i) => {
      if (selector.left) {
        this.sideSelector[selector.left.gear] = i;
        this.sideSign[selector.left.gear] = -1;
        this.sideBody[selector.left.gear] = selector.left.body;
      }
      if (selector.right) {
        this.sideSelector[selector.right.gear] = i;
        this.sideSign[selector.right.gear] = 1;
        this.sideBody[selector.right.gear] = selector.right.body;
      }
    });
    for (const pair of layout.pairs) {
      if (pair.direct) continue;
      this.preFactor[pair.gear] = -layout.wheels[pair.driven].teeth / layout.wheels[pair.driver].teeth;
    }
    layout.rattle.forEach((body, i) => {
      const wheel = layout.wheels.find((w) => w.body === body);
      this.rattlePitch[i] = wheel ? TAU / wheel.teeth : 0;
    });
  }

  /** Rien d'alloué à chaque image : le baladeur et le côté de chaque rapport sont dans des tables. */
  private setTarget(gear: number, share: number): void {
    if (gear > 0 && gear <= 8 && this.sideSelector[gear] >= 0) this.target[this.sideSelector[gear]] = this.sideSign[gear] * share;
  }

  /** Où chaque baladeur doit aller, d'après l'étape du passage simulée. */
  private aim(frame: BoxFrame, dct: boolean, seconds: number): void {
    this.target.fill(0);
    const { step, gear, meshed, pending } = frame;
    if (dct) {
      // Les deux arbres ont chacun leur rapport engagé.
      if (step === 7 && this.held) this.setTarget(this.held, 1);
      if (gear !== 0 && (meshed || step >= 5)) this.setTarget(gear, 1);
      else if (pending !== 0 && step >= 3) this.setTarget(pending, step === 4 ? RING_CONTACT : step === 3 ? 0.4 : 1);
      if (this.preselected !== 0 && this.preselected !== gear && this.preselected !== this.held) this.setTarget(this.preselected, 1);
      return;
    }
    if (frame.frozen) {
      if (meshed && gear > 0) this.setTarget(gear, 1);
      return;
    }
    this.followShift(frame, seconds);
  }

  /**
   * Le passage tel qu'on le montre. Le modèle peut le boucler en 50 ms, soit deux valeurs de télémétrie : la vue garde
   * l'ordre de ses étapes (sortie, course, cône, engagement) et donne à chacune le temps d'être vue. La sortie reste
   * libre jusqu'à ce que le baladeur arrive vraiment.
   */
  private followShift(frame: BoxFrame, seconds: number): void {
    const { step, gear, meshed, pending } = frame;
    const shownSide = this.shownGear > 0 ? this.sideSelector[this.shownGear] : -1;
    const shownIn = shownSide >= 0 && this.pos[shownSide] * this.sideSign[this.shownGear] > 0.85;
    if (this.seqTo === 0) {
      if (step >= 1 && pending > 0 && pending !== this.shownGear) this.startShift(pending);
      else if (meshed && gear > 0 && gear !== this.shownGear && shownIn) this.startShift(gear);
      else if (meshed && gear > 0 && gear !== this.shownGear) this.startShift(gear);
    }
    if (this.seqTo === 0) {
      if (meshed && gear > 0) this.setTarget(gear, 1);
      else if (this.shownGear > 0 && step === 1) this.setTarget(this.shownGear, 1);
      if (!meshed && step === 0) this.shownGear = 0;
      if (step === 2) this.shownGear = 0;
      return;
    }
    const to = this.seqTo;
    const abandoned = step === 0 && !meshed && gear !== to;
    if (abandoned) {
      this.seqTo = 0;
      this.shownGear = 0;
      return;
    }
    this.seqTime += seconds;
    const toSide = this.sideSelector[to];
    const at = toSide >= 0 ? this.pos[toSide] * this.sideSign[to] : 0;
    switch (this.seqPhase) {
      case 0: {
        // Le baladeur attend sur son rapport que l'embrayage soit ouvert, puis il en sort.
        const leaving = step >= 2 || !meshed || gear === to;
        if (this.shownGear > 0 && !leaving) {
          this.setTarget(this.shownGear, 1);
        } else if (this.shownGear <= 0 || Math.abs(this.pos[shownSide]) < 0.03) {
          this.nextPhase();
        }
        break;
      }
      case 1:
        this.setTarget(to, 0.4);
        if (at >= 0.39) this.nextPhase();
        break;
      case 2: {
        // Sur le cône : le baladeur reste bloqué par la bague tant que le modèle synchronise, et au moins le temps de le voir.
        const cones = this.layout.form === 'synchro';
        this.setTarget(to, cones ? RING_CONTACT : 0.5);
        if ((!cones || step === 0 || step >= 5) && this.seqTime > (cones ? SYNC_SHOWN_S : DOGS_SHOWN_S)) this.nextPhase();
        break;
      }
      default:
        this.setTarget(to, 1);
        if (at > 0.99 && meshed && gear === to) {
          this.shownGear = to;
          this.seqTo = 0;
        }
    }
  }

  private startShift(to: number): void {
    this.seqTo = to;
    this.seqPhase = 0;
    this.seqTime = 0;
  }

  private nextPhase(): void {
    this.seqPhase++;
    this.seqTime = 0;
  }

  /** La position de chaque baladeur selon le barillet : c'est lui qui mène les fourchettes sur une séquentielle. */
  private drumPositions(): void {
    const layout = this.layout;
    const drum = layout.drum!;
    const selectors = layout.selectors;
    for (let i = 0; i < selectors.length; i++) {
      this.pos[i] = trackAt(selectors[i], drum, this.drum);
    }
  }

  update(frame: BoxFrame, seconds: number): void {
    const layout = this.layout;
    if (layout.bodies.length === 0) return;
    const { angles, rpm, pos, target } = this;
    if (!this.started) {
      this.started = true;
      this.lastInput = frame.inputTotal;
      this.lastOutput = frame.outputTotal;
      this.lastGear = frame.gear;
      this.lastMeshed = frame.meshed;
    }
    this.time += seconds;
    const dInput = frame.inputTotal - this.lastInput;
    const dOutput = frame.outputTotal - this.lastOutput;
    this.lastInput = frame.inputTotal;
    this.lastOutput = frame.outputTotal;

    const dct = layout.form === 'dual_clutch';
    if (frame.step !== 7 && frame.gear !== this.lastGear) this.held = 0;
    if (frame.step === 7 && this.lastGear !== frame.gear && this.lastGear !== 0) this.held = this.lastGear;
    if (dct) this.choosePreselected(frame, seconds);
    this.aim(frame, dct, seconds);

    // Les pièces qui coulissent : à la vitesse d'un passage ralenti juste assez pour se voir.
    const stepMax = frame.frozen ? 4 : seconds / SLEEVE_TRAVEL_S;
    if (layout.drum) {
      this.moveDrum(frame, stepMax);
      this.drumPositions();
    } else {
      for (let i = 0; i < pos.length; i++) pos[i] += clamp(target[i] - pos[i], -stepMax, stepMax);
    }
    if (layout.reverse) {
      const wanted = frame.gear === -1 && (frame.meshed || frame.step >= 5) ? 1 : frame.pending === -1 && frame.step >= 3 ? 0.5 : 0;
      this.reverseIn += clamp(wanted - this.reverseIn, -stepMax, stepMax);
    }
    this.shake = frame.grinding > 5 ? 0.05 * Math.sin(this.time * 90) * Math.min(frame.grinding / 80, 1) : 0;
    this.clunk = Math.max(0, this.clunk - seconds / 0.18);

    if (layout.form === 'converter') {
      this.updatePlanetary(frame, dInput, dOutput);
    } else if (layout.form === 'reducer') {
      angles[layout.input] += dInput;
      rpm[layout.input] = frame.inputRpm;
      this.follow(layout.chain, -1);
      this.follow(layout.after, -1);
      // Un réducteur n'a rien à désengager : son seul étage est toujours en prise.
      this.engaged = 1;
    } else {
      this.updateLayshaft(frame, dInput, dOutput, seconds, dct);
    }
    this.updateInput(frame, seconds);
    this.lastGear = frame.gear;
    this.lastMeshed = frame.meshed;
  }

  private follow(links: ChainLink[], skip: number): void {
    const { angles, rpm, layout } = this;
    for (let i = 0; i < links.length; i++) {
      const link = links[i];
      if (link.to === skip) continue;
      angles[link.to] = angles[link.from] * link.factor;
      rpm[link.to] = rpm[link.from] * link.factor;
      const r = layout.rattle;
      for (let k = 0; k < r.length; k++) {
        if (r[k] === link.to) angles[link.to] += this.rattleOffset(k);
      }
    }
  }

  private rattleOffset(index: number): number {
    return this.frameLoose[index] * RATTLE_PITCH_SHARE * this.rattlePitch[index];
  }

  private choosePreselected(frame: BoxFrame, seconds: number): void {
    const count = this.layout.pairs.length;
    const g = frame.gear;
    let wanted = 0;
    if (g > 0) {
      wanted = frame.throttle > 0.02 ? (g < count ? g + 1 : g - 1) : (g > 1 ? g - 1 : g + 1);
    }
    if (wanted !== this.preselectWanted) {
      this.preselectWanted = wanted;
      this.preselectSince = 0;
    }
    this.preselectSince += seconds;
    // Le calculateur ne change pas d'avis à chaque variation de la pédale.
    if (this.preselected === 0 || this.preselectSince > 0.35 || this.preselected === g) this.preselected = wanted;
  }

  private moveDrum(frame: BoxFrame, stepMax: number): void {
    const drum = this.layout.drum!;
    // Position voulue : au point mort ou sur le rapport, et le rapport visé dès que le passage a commencé.
    let wanted = frame.gear === -1 ? 0 : frame.gear > 0 ? drum.neutral + frame.gear : drum.neutral;
    if (!frame.meshed && frame.step >= 3 && frame.pending !== 0) wanted = frame.pending < 0 ? 0 : drum.neutral + frame.pending;
    const before = this.drum;
    this.drum += clamp(wanted - this.drum, -stepMax * 1.4, stepMax * 1.4);
    if (this.drum !== before) this.pawl = 1;
    else this.pawl = Math.max(0, this.pawl - stepMax);
  }

  /** Une boîte à baladeurs : le primaire fait tourner tout ce qui est en prise, baladeur engagé ou non. */
  private updateLayshaft(frame: BoxFrame, dInput: number, dOutput: number, seconds: number, dct: boolean): void {
    const layout = this.layout;
    const { angles, rpm, pos } = this;
    const hub = layout.hubBody;
    this.frameLoose[0] = frame.loose0;
    this.frameLoose[1] = frame.loose1;
    this.frameLoose[2] = frame.loose2;
    this.frameLoose[3] = frame.loose3;

    // Les arbres : le primaire tourne au régime de la télémétrie. Sur une double embrayage, celui dont l'embrayage tient le couple.
    const gearForShaft = frame.gear !== 0 ? frame.gear : frame.pending;
    const onOuter = dct && (gearForShaft === -1 || (gearForShaft > 0 && gearForShaft % 2 === 0));
    const active = onOuter ? layout.inputOuter : layout.input;
    const other = dct ? (onOuter ? layout.input : layout.inputOuter) : -1;
    angles[active] += dInput;
    rpm[active] = frame.inputRpm;
    if (dct) {
      const wanted = onOuter ? 1 : 0;
      this.clutchMix += clamp(wanted - this.clutchMix, -seconds / 0.3, seconds / 0.3);
    }
    // La roue en prise reste fixe sur son arbre : elle ne claque pas.
    this.quietEngaged();
    this.follow(layout.chain, -1);

    // Le baladeur a fini sa course et le modèle dit qu'il est en prise : la sortie est liée à la roue, sinon elle va sa route.
    const g = frame.gear;
    const hasSide = g > 0 && this.sideSelector[g] >= 0;
    const lockedHere = frame.meshed && g !== 0 && (g === -1 ? this.reverseIn > 0.85 : hasSide && pos[this.sideSelector[g]] * this.sideSign[g] > 0.85);
    const previousHub = angles[hub];

    if (frame.meshed && !this.lastMeshed) this.noteClunk(frame);

    this.syncSelector = -1;
    this.syncGapRpm = 0;
    const syncing = this.seqTo > 0 ? this.seqTo : frame.step >= 3 && frame.step <= 5 ? frame.pending : 0;
    if (syncing > 0 && this.sideSelector[syncing] >= 0) {
      this.syncSelector = this.sideSelector[syncing];
      const body = this.sideBody[syncing];
      this.syncGapRpm = Math.abs(rpm[body]) - Math.abs(frame.outputRpm / layout.hubToOutput);
    }

    if (lockedHere) {
      if (frame.gear === -1 && layout.reverse) {
        this.lockReverse(frame);
      } else {
        const body = this.sideBody[g];
        const source = angles[body];
        if (this.lockGear !== frame.gear) {
          const pitch = TAU / Math.max(layout.dogTeeth, 1);
          const offset = previousHub - source;
          this.lockOffset = Math.round(offset / pitch) * pitch;
          this.residual = offset - this.lockOffset;
          this.lockGear = frame.gear;
        }
        this.residual *= Math.exp(-seconds / 0.05);
        angles[hub] = source + this.lockOffset + this.residual;
        rpm[hub] = rpm[body];
        this.engaged = frame.gear;
        if (this.seqTo === 0) this.shownGear = frame.gear;
      }
    } else {
      this.lockGear = 0;
      this.engaged = 0;
      angles[hub] += (layout.outputDir * dOutput) / layout.hubToOutput;
      rpm[hub] = (layout.outputDir * frame.outputRpm) / layout.hubToOutput;
      if (frame.gear === -1 && layout.reverse && this.reverseIn > 0.5) this.spinIdler();
    }

    // L'autre arbre d'une double embrayage : entraîné par la sortie à travers le rapport présélectionné, sinon il s'arrête doucement.
    if (dct && other >= 0) {
      const pre = this.preselected;
      const belongs = pre > 0 && this.sideSelector[pre] >= 0 && (pre % 2 === 0) !== onOuter;
      const dHub = angles[hub] - previousHub;
      if (belongs && pos[this.sideSelector[pre]] * this.sideSign[pre] > 0.85) {
        const factor = this.preFactor[pre];
        angles[other] += dHub * factor;
        rpm[other] = rpm[hub] * factor;
        this.otherOmega = rpm[other];
      } else {
        const decay = Math.exp(-seconds / 1.2);
        this.otherOmega *= decay;
        rpm[other] = this.otherOmega;
        if (!frame.frozen) angles[other] += ((this.otherOmega / 60) * TAU * seconds) / 12;
      }
      this.follow(layout.chain, -1);
    }
    // Le baladeur tourne avec son moyeu.
    const selectors = layout.selectors;
    for (let i = 0; i < selectors.length; i++) {
      angles[selectors[i].sleeveBody] = angles[hub];
      rpm[selectors[i].sleeveBody] = rpm[hub];
    }
    this.follow(layout.after, -1);
  }

  private quietEngaged(): void {
    if (this.engaged <= 0) return;
    const index = this.engaged - 1;
    if (index < 4) this.frameLoose[index] = 0;
  }

  private spinIdler(): void {
    const reverse = this.layout.reverse!;
    const { angles, rpm } = this;
    const wheels = this.layout.wheels;
    const factor = -wheels[reverse.pinion].teeth / wheels[reverse.idler].teeth;
    angles[reverse.idlerBody] = angles[reverse.pinionBody] * factor;
    rpm[reverse.idlerBody] = rpm[reverse.pinionBody] * factor;
  }

  /** Marche arrière : du pignon à l'intermédiaire, de l'intermédiaire à la roue fixée sur la sortie. */
  private lockReverse(frame: BoxFrame): void {
    const layout = this.layout;
    const reverse = layout.reverse!;
    const { angles, rpm } = this;
    this.spinIdler();
    const wheels = layout.wheels;
    const factor = -wheels[reverse.idler].teeth / wheels[reverse.wheel].teeth;
    const source = angles[reverse.idlerBody] * factor;
    if (this.lockGear !== -1) {
      const pitch = TAU / wheels[reverse.wheel].teeth;
      const offset = angles[layout.hubBody] - source;
      this.reverseOffset = Math.round(offset / pitch) * pitch;
      this.lockGear = -1;
    }
    angles[layout.hubBody] = source + this.reverseOffset;
    rpm[layout.hubBody] = rpm[reverse.idlerBody] * factor;
    this.engaged = frame.gear;
  }

  private noteClunk(frame: BoxFrame): void {
    const dog = this.layout.form === 'dog' || this.layout.form === 'sequential';
    this.clunk = dog ? 1 : frame.step >= 5 ? 0.35 : 0.2;
  }

  /** Une automatique : le planétaire à la turbine, le porte-satellites à la sortie, la couronne déduite de Willis. */
  private updatePlanetary(frame: BoxFrame, dInput: number, dOutput: number): void {
    const layout = this.layout;
    const { angles, rpm } = this;
    angles[layout.input] += dInput;
    rpm[layout.input] = frame.inputRpm;
    angles[layout.hubBody] += layout.outputDir * dOutput;
    rpm[layout.hubBody] = layout.outputDir * frame.outputRpm;
    const sun = angles[layout.input];
    const carrier = angles[layout.hubBody];
    for (let i = 0; i < layout.planets.length; i++) {
      const set = layout.planets[i];
      const zs = layout.wheels[set.sun].teeth;
      const zr = layout.wheels[set.annulus].teeth;
      const zp = layout.wheels[set.planets[0]].teeth;
      angles[set.ringBody] = ((zs + zr) * carrier - zs * sun) / zr;
      rpm[set.ringBody] = ((zs + zr) * rpm[layout.hubBody] - zs * rpm[layout.input]) / zr;
      const spin = carrier - (zs / zp) * (sun - carrier);
      for (let n = 0; n < set.planetBodies.length; n++) {
        angles[set.planetBodies[n]] = spin;
        rpm[set.planetBodies[n]] = rpm[layout.hubBody] - (zs / zp) * (rpm[layout.input] - rpm[layout.hubBody]);
      }
    }
    this.follow(layout.after, -1);
  }

  /** Le volant et son amortisseur : le couple transmis comprime les ressorts d'un double volant. */
  private updateInput(frame: BoxFrame, seconds: number): void {
    const layout = this.layout;
    if (layout.flywheelBody < 0) return;
    const { angles, rpm } = this;
    angles[layout.flywheelBody] = frame.flywheelTotal;
    rpm[layout.flywheelBody] = frame.rpm;
    if (layout.flywheel === 'dual_mass') {
      const wanted = clamp(frame.clutchTorque / DMF_NM_PER_DEG / DMF_MAX_DEG, -1, 1);
      this.windup += (wanted - this.windup) * (frame.frozen ? 1 : 1 - Math.exp(-seconds / 0.06));
    } else {
      this.windup = 0;
    }
    angles[layout.flywheelSecondaryBody] = frame.flywheelTotal - (this.windup * DMF_MAX_DEG * Math.PI) / 180;
    rpm[layout.flywheelSecondaryBody] = frame.rpm;
  }
}

/** Le chemin d'une fourchette dans le barillet : ce baladeur à gauche ou à droite à la position de ses rapports, au centre sinon. */
export function trackAt(selector: Selector, drum: Drum, position: number): number {
  const gearAt = (index: number) => drum.positions[Math.max(0, Math.min(drum.positions.length - 1, index))];
  const low = Math.floor(position);
  const share = position - low;
  const value = (index: number) => {
    const gear = gearAt(index);
    if (gear > 0 && selector.left?.gear === gear) return -1;
    if (gear > 0 && selector.right?.gear === gear) return 1;
    return 0;
  };
  const a = value(low);
  const c = value(low + 1);
  // Au milieu du passage entre deux positions, tous les baladeurs passent par le centre : on sort d'un rapport avant d'entrer dans l'autre.
  if (a !== 0 && c !== 0) return share < 0.5 ? a * (1 - share * 2) : c * (share * 2 - 1);
  return a + (c - a) * share;
}

/** Écart à la prise d'une paire : 0 quand les creux de l'une sont exactement sur les dents de l'autre. Pour les tests. */
export function meshError(layout: GearboxLayout, mesh: Mesh, angles: ArrayLike<number>): number {
  const a = layout.wheels[mesh.a];
  const b = layout.wheels[mesh.b];
  const angleA = angles[a.body] + a.phase;
  const angleB = angles[b.body] + b.phase;
  const ta = (a.teeth * (mesh.contactA - angleA)) / TAU;
  const tb = (b.teeth * (mesh.contactB - angleB)) / TAU;
  const value = ta + tb;
  const frac = value - Math.floor(value);
  return Math.abs(frac - 0.5);
}

/** La vitesse d'une roue rapportée à son pignon menant, telle que les dents la donnent. */
export const wheelRatio = (layout: GearboxLayout, pair: GearPairView): number => layout.wheels[pair.driver].teeth / layout.wheels[pair.driven].teeth;

/** La même mesure pour un train épicycloïdal : le plus grand écart à la prise, satellite par satellite. Pour les tests. */
export function planetError(layout: GearboxLayout, angles: ArrayLike<number>): number {
  let worst = 0;
  const off = (value: number) => Math.abs(value - Math.floor(value) - 0.5);
  for (const set of layout.planets) {
    const sun = layout.wheels[set.sun];
    const annulus = layout.wheels[set.annulus];
    const carrier = angles[layout.output];
    set.planets.forEach((id, n) => {
      const planet = layout.wheels[id];
      const psi = carrier + (n / set.planets.length) * TAU;
      const spin = angles[planet.body] + planet.phase;
      const ts = (sun.teeth * (psi - angles[sun.body] - sun.phase)) / TAU;
      const tp = (planet.teeth * (psi + Math.PI - spin)) / TAU;
      worst = Math.max(worst, off(ts + tp));
      const tpRing = (planet.teeth * (psi - spin)) / TAU;
      const tr = (annulus.teeth * (psi - angles[annulus.body] - annulus.phase)) / TAU;
      worst = Math.max(worst, off(tpRing - tr));
    });
  }
  return worst;
}
