/**
 * Tout le gaz d'une voiture, à l'horloge de `Motion` : le réseau 0D/1D (cylindres, tubes), la
 * recirculation dans les volumes et le panache à la sortie, en 2D, et les particules qui montrent la
 * matière. Le temps physique avance `SLOW_MOTION` fois moins vite que le temps réel, comme tout le
 * reste des vues : à 6000 tr/min, une onde de pression traverse la ligne en quatre images, le gaz en
 * une demi-seconde.
 * Rien n'alloue d'une image à l'autre, sauf quand le moteur change.
 */
import { camKey } from './cam';
import type { EngineConfig } from './config';
import { CELL_INLET, CELL_OPEN, CELL_WALL, FluidField } from './fluidField';
import { GasNetwork, type GasInputs, type PlacedSection } from './fluidGas';

/** Panache : 1,4 m sur 0,9 m derrière la voiture. */
const PLUME_NX = 84;
const PLUME_NY = 56;
export const PLUME_CELL_M = 0.0165;
/** Hauteur de la sortie au-dessus du sol, en cellules. */
export const PLUME_EXIT_ROW = 19;
/** Le tube dépasse dans la grille : le calcul remonte plusieurs cellules en un pas, et doit retomber dans le tube. */
export const PLUME_PIPE_CELLS = 5;
/** Au-delà, le jet à l'écran ne change plus grand-chose et coûte des pas de calcul. */
const JET_LIMIT = 60;
const CHAMBER_LIMIT = 60;
const MAX_PARTICLES = 320;
const TUBE_SPACING = 0.03;
const BUDGET_MS = 4;
const SLEEP_HEAT = 0.03;

/** Un volume de la ligne : son écoulement interne n'existe que pour l'affichage, piloté par le réseau. */
export interface Chamber {
  section: PlacedSection;
  index: number;
  field: FluidField;
  inletCells: Int32Array;
  outletCells: Int32Array;
  /** Colonnes de chicanes, pour le dessin : abscisse en cellules, lignes pleines. */
  baffles: { column: number; from: number; to: number }[];
  /** Nid d'abeille : de telle à telle colonne. */
  monolithFrom: number;
  monolithTo: number;
}

function buildChamber(section: PlacedSection, index: number, pipeDiameter: number, kindOfExhaust: EngineConfig['exhaust']): Chamber {
  const nx = Math.min(Math.max(Math.round(section.length / 0.012), 22), 46);
  const h = section.length / nx;
  const ny = Math.min(Math.max(Math.round(section.diameter / h), 8), 28) + 2;
  const field = new FluidField(nx, ny, h);
  field.iterations = 10;
  field.confinement = 0.4;
  field.dragTau = 0.03;
  field.coolTau = 0.5;
  field.smokeTau = 6;
  const middle = (ny - 1) / 2;
  const port = Math.max(1, Math.round((0.5 * pipeDiameter * Math.sqrt(section.kind === 'muffler' ? 1.2 : 1)) / h));
  const inPort = (j: number) => Math.abs(j - middle) <= port;
  const inlet: number[] = [];
  const outlet: number[] = [];
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      const c = i + j * nx;
      if (j === 0 || j === ny - 1) field.kind[c] = CELL_WALL;
      else if (i === nx - 1) {
        if (inPort(j)) {
          field.kind[c] = CELL_OPEN;
          outlet.push(c);
        } else field.kind[c] = CELL_WALL;
      } else if (i <= 2) {
        if (inPort(j)) {
          field.kind[c] = CELL_INLET;
          inlet.push(c);
        } else if (i === 0) field.kind[c] = CELL_WALL;
      }
    }
  }
  const baffles: Chamber['baffles'] = [];
  let monolithFrom = 0;
  let monolithTo = 0;
  if (section.kind === 'muffler') {
    // Chicanes : décalées, sauf sur une ligne sport où elles sont percées au centre.
    const staggered = kindOfExhaust !== 'sport';
    const count = staggered ? 3 : 2;
    for (let b = 1; b <= count; b++) {
      const column = Math.round((nx * b) / (count + 1));
      const gap = Math.max(2, Math.round(ny * 0.28));
      let open0: number;
      let open1: number;
      if (staggered) {
        if (b % 2 === 1) {
          open0 = ny - 1 - gap;
          open1 = ny - 2;
        } else {
          open0 = 1;
          open1 = gap;
        }
      } else {
        open0 = Math.round(middle - gap / 2);
        open1 = Math.round(middle + gap / 2);
      }
      for (let j = 1; j < ny - 1; j++) {
        if (j >= open0 && j <= open1) continue;
        field.kind[column + j * nx] = CELL_WALL;
      }
      baffles.push({ column, from: open0, to: open1 });
    }
  } else if (section.kind === 'cat') {
    monolithFrom = Math.round(nx * 0.22);
    monolithTo = Math.round(nx * 0.78);
    field.straightener = new Uint8Array(nx * ny);
    for (let j = 1; j < ny - 1; j++) {
      for (let i = monolithFrom; i <= monolithTo; i++) field.straightener[i + j * nx] = 1;
    }
  }
  field.finalize();
  return { section, index, field, inletCells: Int32Array.from(inlet), outletCells: Int32Array.from(outlet), baffles, monolithFrom, monolithTo };
}

/** Générateur déterministe, pour que deux passages donnent la même image et que les tests ne dépendent pas du hasard. */
class Lfsr {
  private state = 0x2f6e2b1;
  next(): number {
    let x = this.state;
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    this.state = x | 0;
    return (x >>> 0) / 4294967296;
  }
}

/** Ce qui impose de reconstruire le réseau : les réglages qui changent ses pièces, et l'air du jour dans lequel il s'ouvre. */
export const gasKey = (config: EngineConfig): string =>
  `${config.cylinders}|${config.layout}|${config.displacementCc}|${config.boreMm}|${config.exhaust}|${config.aspiration}|${config.fuel}|${config.body}|${config.boostBar}|${config.turboRelease}|${config.turboSize}|${config.redlineRpm}|${config.turboPipeMm}|${config.camshaft}|${config.fuelSystem}|${config.airFilter}|${config.placement}|${config.superchargerKind}|${config.antiLag}|${config.turboLayout}|${config.ambientC}|${config.altitudeM}|${config.compressionRatio}|${camKey(config)}`;

export class GasFlow {
  readonly net = new GasNetwork();
  readonly plume: FluidField;
  private plumeInlet = new Int32Array(0);
  /** Lignes occupées par le tube, dans le panache. */
  plumeFrom = PLUME_EXIT_ROW;
  plumeTo = PLUME_EXIT_ROW;
  chambers: Chamber[] = [];
  /** Volume de chaque tronçon ; `null` pour un tube. */
  chamberOf: (Chamber | null)[] = [];
  /** Faux quand il n'y a rien à calculer : moteur électrique, ou animation réduite. */
  active = false;
  private key = '';
  private sleeping = true;
  private sinceMoving = 0;

  // ── Particules : la matière. Tronçon (-1 : libre, 100 et plus : conduit d'admission). ──
  count = MAX_PARTICLES;
  pSection = new Int16Array(MAX_PARTICLES).fill(-1);
  /** Tube : position (m) depuis le début du tronçon et écart à l'axe (-1 à 1). Volume : cellules de sa grille. */
  pX = new Float32Array(MAX_PARTICLES);
  pY = new Float32Array(MAX_PARTICLES);
  pBurnt = new Float32Array(MAX_PARTICLES);
  private pAge = new Float32Array(MAX_PARTICLES);
  private cursor = 0;
  private spawnIn = new Float64Array(16);
  private spawnOut = new Float64Array(16);
  private random = new Lfsr();
  /** Conduit d'admission, découpé par le compresseur : première cellule, nombre de cellules. */
  ducts: { first: number; count: number; id: number }[] = [];

  // ── Mesures ──
  /** Coût moyen par image, en millisecondes : tout le gaz, puis le réseau, le panache et les volumes. */
  costMs = 0;
  costNetMs = 0;
  costPlumeMs = 0;
  costChambersMs = 0;
  /** Qualité : 2 pleine, 1 volumes et panache une image sur deux, 0 sans les volumes. */
  quality = 2;
  private overBudget = 0;
  private underBudget = 0;
  private frame = 0;
  private plumeTime = 0;
  private chamberTime = 0;
  /** Les vues disent ce dont elles ont besoin : rien n'est calculé pour une vue hors écran. */
  wantChambers = false;
  /** Pas de temps physique de la dernière image (s). */
  lastSeconds = 0;

  /** Force de la flamme à la sortie, 0 à 1 : vient de `Motion.flame`. */
  flame = 0;
  /** Vitesse de la voiture (m/s) : l'air qu'elle traverse balaie le panache. */
  carSpeed = 0;
  private jetWander = 0;

  constructor() {
    const field = new FluidField(PLUME_NX, PLUME_NY, PLUME_CELL_M, true, true);
    field.gravity = 9.81;
    field.confinement = 0.35;
    field.dragTau = 0.07;
    field.coolTau = 0.32;
    field.smokeTau = 0.5;
    field.flameTau = 0.03;
    field.iterations = 8;
    field.turbulence = 0.09;
    this.plume = field;
  }

  /**
   * Reconstruit tout quand le moteur change vraiment. `order` : ordre d'allumage.
   * Moteur en marche, la ligne repart pleine et chaude : `heat` dit à quel point.
   */
  configure(config: EngineConfig, order: number[], running = false, heat = 0): void {
    this.active = !config.electric;
    const key = gasKey(config);
    if (key === this.key) return;
    this.key = key;
    if (!this.active) return;
    const net = this.net;
    net.build(config, order);
    // Le panache monte dans l'air du jour : sa poussée se compte sur sa température.
    this.plume.ambient = net.tAmb;

    this.chambers = [];
    this.chamberOf = [];
    net.sections.forEach((section, index) => {
      if (section.kind === 'cat' || section.kind === 'res' || section.kind === 'muffler') {
        const chamber = buildChamber(section, index, net.pipeDiameter, config.exhaust);
        this.chambers.push(chamber);
        this.chamberOf.push(chamber);
      } else this.chamberOf.push(null);
    });
    if (this.spawnIn.length < net.sections.length + 4) {
      this.spawnIn = new Float64Array(net.sections.length + 4);
      this.spawnOut = new Float64Array(net.sections.length + 4);
    }
    this.spawnIn.fill(0);
    this.spawnOut.fill(0);
    this.pSection.fill(-1);
    this.ducts = [];
    if (net.compressorCell >= 0) {
      this.ducts.push({ first: net.intakeFirst, count: net.compressorCell - net.intakeFirst, id: 100 });
      this.ducts.push({ first: net.compressorCell, count: net.intakeFirst + net.intakeCount - net.compressorCell, id: 101 });
    } else {
      this.ducts.push({ first: net.intakeFirst, count: net.intakeCount, id: 100 });
    }

    // Panache : sol en bas, arrière de la voiture à gauche, le reste à l'air libre.
    const field = this.plume;
    const nx = field.nx;
    const ny = field.ny;
    field.kind.fill(0);
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const c = i + j * nx;
        if (j === 0 || i === 0) field.kind[c] = CELL_WALL;
        else if (i === nx - 1 || j === ny - 1) field.kind[c] = CELL_OPEN;
      }
    }
    const half = Math.max(1, Math.round((0.5 * net.pipeDiameter * Math.sqrt(net.sections[net.sections.length - 1].area)) / field.h));
    this.plumeFrom = PLUME_EXIT_ROW - half;
    this.plumeTo = PLUME_EXIT_ROW + half;
    const inlet: number[] = [];
    for (let j = this.plumeFrom; j <= this.plumeTo; j++) {
      for (let i = 1; i <= PLUME_PIPE_CELLS; i++) {
        const c = i + j * nx;
        field.kind[c] = CELL_INLET;
        inlet.push(c);
      }
    }
    this.plumeInlet = Int32Array.from(inlet);
    field.finalize();
    field.clear();
    this.sleeping = !running && heat < SLEEP_HEAT;
    this.sinceMoving = 0;
    if (running) {
      net.prefill(heat);
      for (const chamber of this.chambers) {
        const before = net.temp[chamber.section.first] - net.tAmb;
        chamber.field.temp.fill(before);
        chamber.field.smoke.fill(1);
      }
    }
  }

  /** Une détonation dans la fin de ligne. */
  pop(joules: number): void {
    if (this.active) this.net.pop(joules);
    this.sleeping = false;
  }

  /** `seconds` : temps physique. Le cycle passe de `cycle` à `cycle + turns`. */
  advance(seconds: number, cycle: number, turns: number, inputs: GasInputs): void {
    this.lastSeconds = seconds;
    if (!this.active || seconds <= 0) return;
    // À l'arrêt, une fois tout retombé et refroidi, plus aucun calcul : la page au repos ne coûte rien.
    if (inputs.rpm > 20) {
      this.sinceMoving = 0;
      this.sleeping = false;
    } else if (!this.sleeping) {
      this.sinceMoving += seconds;
      if (this.sinceMoving > 3 && inputs.heat < SLEEP_HEAT) {
        this.sleeping = true;
        this.plume.clear();
        for (const chamber of this.chambers) chamber.field.clear();
        this.pSection.fill(-1);
      }
    }
    if (this.sleeping) return;
    const t0 = performance.now();
    this.net.advance(seconds, cycle, turns, inputs);
    const t1 = performance.now();

    this.frame++;
    this.plumeTime += seconds;
    this.chamberTime += seconds;
    const everyOther = this.quality < 2;
    // Tant que le jet est lent, une image sur deux suffit : ni l'œil ni la grille n'y perdent.
    const slowJet = Math.abs(this.net.exitU) < 24;
    if ((!everyOther && !slowJet) || this.frame % 2 === 0) {
      this.stepPlume(this.plumeTime, inputs);
      this.plumeTime = 0;
    }
    const t2 = performance.now();
    if (this.wantChambers && this.quality > 0 && (!everyOther || this.frame % 2 === 1)) {
      this.stepChambers(this.chamberTime);
      this.chamberTime = 0;
    }
    this.moveParticles(seconds);
    this.wantChambers = false;
    const t3 = performance.now();

    const blend = 0.05;
    this.costNetMs += (t1 - t0 - this.costNetMs) * blend;
    this.costPlumeMs += (t2 - t1 - this.costPlumeMs) * blend;
    this.costChambersMs += (t3 - t2 - this.costChambersMs) * blend;
    this.costMs += (t3 - t0 - this.costMs) * blend;
    this.tuneQuality();
  }

  /** Une machine trop lente perd d'abord en détail, jamais en fluidité. */
  private tuneQuality(): void {
    if (this.costMs > BUDGET_MS) {
      this.overBudget++;
      this.underBudget = 0;
      if (this.overBudget > 90 && this.quality > 0) {
        this.quality--;
        this.overBudget = 0;
        this.plume.iterations = this.quality === 2 ? 8 : 6;
      }
    } else if (this.costMs < BUDGET_MS * 0.4) {
      this.underBudget++;
      this.overBudget = 0;
      if (this.underBudget > 600 && this.quality < 2) {
        this.quality++;
        this.underBudget = 0;
        this.plume.iterations = this.quality === 2 ? 8 : 6;
      }
    } else {
      this.overBudget = 0;
      this.underBudget = 0;
    }
  }

  private stepPlume(seconds: number, inputs: GasInputs): void {
    const net = this.net;
    const field = this.plume;
    const speed = JET_LIMIT * Math.tanh(net.exitU / JET_LIMIT);
    const temp = net.exitT - net.tAmb;
    // Ce qu'on voit sortir : presque rien sur un moteur chaud, de la vapeur à froid, des suies d'un diesel en charge.
    const burnt = net.exitY;
    const coldness = Math.max(0, Math.min(1, (0.32 - inputs.heat) / 0.26));
    const light = speed > 0 ? burnt * (0.17 + 0.75 * coldness * coldness) : 0;
    const dark = speed > 0 && net.isDiesel ? burnt * Math.max(0, inputs.load - 0.45) * 1.8 : 0;
    const flame = speed > -5 ? this.flame : 0;
    // Au-delà, le panache est de toute façon balayé : inutile de faire remonter le calcul hors du tube.
    field.windX = this.carSpeed < 30 ? this.carSpeed : 30;
    if (dark > 0.005) field.sootActive = true;
    if (flame > 0.005) field.flameActive = true;
    const inlet = this.plumeInlet;
    // Un jet réel n'est jamais parfaitement symétrique : un peu de turbulence à la sortie, qui suit sa vitesse.
    this.jetWander += (this.random.next() * 2 - 1 - this.jetWander) * 0.25;
    const wander = this.jetWander * 0.16 * speed;
    const spread = 0.05 * speed;
    const middle = (this.plumeFrom + this.plumeTo) / 2;
    const half = Math.max((this.plumeTo - this.plumeFrom) / 2, 1);
    for (let k = 0; k < inlet.length; k++) {
      const c = inlet[k];
      field.u[c] = speed;
      const row = Math.floor(c / field.nx);
      field.v[c] = c % field.nx === PLUME_PIPE_CELLS ? wander + spread * ((row - middle) / half) : 0;
      field.temp[c] = temp;
      field.smoke[c] = light;
      if (field.sootActive && field.soot) field.soot[c] = dark;
      if (field.flameActive && field.flame) field.flame[c] = flame;
    }
    field.step(seconds);
    if (field.flameActive && flame <= 0.005 && this.frame % 90 === 0 && field.flame) {
      // Plus de flamme depuis longtemps : ce traceur ne coûte plus rien.
      let top = 0;
      for (let c = 0; c < field.count; c++) if (field.flame[c] > top) top = field.flame[c];
      if (top < 0.01) field.flameActive = false;
    }
    if (field.sootActive && dark <= 0.005 && this.frame % 90 === 0 && field.soot) {
      let top = 0;
      for (let c = 0; c < field.count; c++) if (field.soot[c] > top) top = field.soot[c];
      if (top < 0.01) field.sootActive = false;
    }
  }

  private stepChambers(seconds: number): void {
    const net = this.net;
    for (const chamber of this.chambers) {
      const { section, field } = chamber;
      const before = section.first - 1;
      const after = section.first + section.count;
      const uIn = CHAMBER_LIMIT * Math.tanh(net.u[before] / CHAMBER_LIMIT);
      const tIn = net.temp[before] - net.tAmb;
      const yIn = net.y[before];
      const tOut = net.temp[after] - net.tAmb;
      const yOut = net.y[after];
      const uOut = net.u[after];
      const steps = Math.min(4, 1 + Math.floor((Math.abs(uIn) * seconds) / (3 * field.h)));
      const dt = seconds / steps;
      for (let s = 0; s < steps; s++) {
        for (let k = 0; k < chamber.inletCells.length; k++) {
          const c = chamber.inletCells[k];
          field.u[c] = uIn;
          field.v[c] = 0;
          field.temp[c] = tIn;
          field.smoke[c] = yIn;
        }
        // Ce qui revient par la sortie arrive à l'état du tube d'après.
        if (uOut < 0) {
          for (let k = 0; k < chamber.outletCells.length; k++) {
            const c = chamber.outletCells[k];
            field.temp[c] = tOut;
            field.smoke[c] = yOut;
          }
        }
        field.step(dt);
      }
    }
  }

  private take(): number {
    // Une place libre, ou à défaut la plus ancienne : jamais d'allocation.
    for (let tries = 0; tries < 24; tries++) {
      const k = this.cursor;
      this.cursor = (this.cursor + 1) % MAX_PARTICLES;
      if (this.pSection[k] < 0) return k;
    }
    const k = this.cursor;
    this.cursor = (this.cursor + 1) % MAX_PARTICLES;
    return k;
  }

  private moveParticles(seconds: number): void {
    const net = this.net;
    const { pSection, pX, pY, pBurnt, pAge } = this;
    const cell = net.cell;
    const random = this.random;

    // Naissances : aux bouts de chaque tronçon, autant que le gaz y a parcouru de chemin.
    for (let s = 0; s < net.sections.length; s++) {
      const section = net.sections[s];
      const chamber = this.chamberOf[s];
      const spacing = chamber ? TUBE_SPACING * 2.2 : TUBE_SPACING;
      const inner = section.first - (s > 0 ? 1 : 0);
      const flowIn = chamber ? net.u[inner] : net.u[section.first];
      const flowOut = chamber ? net.u[section.first + section.count] : net.u[section.first + section.count - 1];
      if (flowIn > 0) {
        this.spawnIn[s] += flowIn * seconds;
        while (this.spawnIn[s] >= spacing) {
          this.spawnIn[s] -= spacing;
          const k = this.take();
          pSection[k] = s;
          pAge[k] = 0;
          pBurnt[k] = net.y[section.first];
          if (chamber) {
            const field = chamber.field;
            const row = (field.ny - 1) / 2 + (random.next() * 2 - 1) * Math.max(1, 0.5 * net.pipeDiameter / field.h);
            pX[k] = 3 + random.next();
            pY[k] = row;
          } else {
            pX[k] = random.next() * Math.min(flowIn * seconds, cell);
            pY[k] = random.next() * 1.5 - 0.75;
          }
        }
      }
      if (flowOut < 0) {
        this.spawnOut[s] -= flowOut * seconds;
        while (this.spawnOut[s] >= spacing) {
          this.spawnOut[s] -= spacing;
          const k = this.take();
          pSection[k] = s;
          pAge[k] = 0;
          // Depuis l'air libre, à la fin de la ligne : de l'air frais qui rentre.
          pBurnt[k] = s === net.sections.length - 1 ? 0 : net.y[section.first + section.count - 1];
          if (chamber) {
            const field = chamber.field;
            const row = (field.ny - 1) / 2 + (random.next() * 2 - 1) * Math.max(1, 0.5 * net.pipeDiameter / field.h);
            pX[k] = field.nx - 2 - random.next();
            pY[k] = row;
          } else {
            pX[k] = section.count * cell - random.next() * Math.min(-flowOut * seconds, cell);
            pY[k] = random.next() * 1.5 - 0.75;
          }
        }
      }
    }
    for (let d = 0; d < this.ducts.length; d++) {
      const duct = this.ducts[d];
      const slot = net.sections.length + d;
      const flowIn = net.u[duct.first];
      if (flowIn > 0) {
        this.spawnIn[slot] += flowIn * seconds;
        while (this.spawnIn[slot] >= TUBE_SPACING * 1.2) {
          this.spawnIn[slot] -= TUBE_SPACING * 1.2;
          const k = this.take();
          pSection[k] = duct.id;
          pAge[k] = 0;
          pBurnt[k] = 0;
          pX[k] = random.next() * Math.min(flowIn * seconds, cell);
          pY[k] = random.next() * 1.5 - 0.75;
        }
      }
      const last = duct.first + duct.count - 1;
      if (net.u[last] < 0) {
        this.spawnOut[slot] -= net.u[last] * seconds;
        while (this.spawnOut[slot] >= TUBE_SPACING * 1.2) {
          this.spawnOut[slot] -= TUBE_SPACING * 1.2;
          const k = this.take();
          pSection[k] = duct.id;
          pAge[k] = 0;
          pBurnt[k] = 0;
          pX[k] = duct.count * cell - random.next() * Math.min(-net.u[last] * seconds, cell);
          pY[k] = random.next() * 1.5 - 0.75;
        }
      }
    }

    // Déplacement : chaque particule suit la vitesse locale du gaz, ondes comprises.
    for (let k = 0; k < MAX_PARTICLES; k++) {
      const s = pSection[k];
      if (s < 0) continue;
      pAge[k] += seconds;
      if (s >= 100) {
        const duct = this.ducts[s - 100];
        if (!duct) {
          pSection[k] = -1;
          continue;
        }
        const position = pX[k] / cell;
        const index = position < 0 ? 0 : position >= duct.count ? duct.count - 1 : position | 0;
        const next = pX[k] + net.u[duct.first + index] * seconds;
        if (next < 0 || next > duct.count * cell) pSection[k] = -1;
        else pX[k] = next;
        continue;
      }
      const section = net.sections[s];
      if (!section) {
        pSection[k] = -1;
        continue;
      }
      const chamber = this.chamberOf[s];
      if (chamber) {
        const field = chamber.field;
        const k1 = seconds / field.h;
        const x = pX[k];
        const y = pY[k];
        const hx = x < 0 ? 0 : x > field.nx - 1.01 ? field.nx - 1.01 : x;
        const hy = y < 0 ? 0 : y > field.ny - 1.01 ? field.ny - 1.01 : y;
        // Lecture bilinéaire de la vitesse, en ligne.
        const cellX = hx | 0;
        const cellY = hy | 0;
        const fx = hx - cellX;
        const fy = hy - cellY;
        const at = cellX + cellY * field.nx;
        const a00 = (1 - fx) * (1 - fy);
        const a10 = fx * (1 - fy);
        const a01 = (1 - fx) * fy;
        const a11 = fx * fy;
        const fu = field.u;
        const fv = field.v;
        const nextX = x + k1 * (a00 * fu[at] + a10 * fu[at + 1] + a01 * fu[at + field.nx] + a11 * fu[at + field.nx + 1]);
        const nextY = y + k1 * (a00 * fv[at] + a10 * fv[at + 1] + a01 * fv[at + field.nx] + a11 * fv[at + field.nx + 1]);
        const ix = Math.round(nextX);
        const iy = Math.round(nextY);
        // Un volume peut retenir longtemps un peu de gaz : au bout d'un moment, il s'est mélangé au reste.
        if (nextX < 2 || nextX > field.nx - 1.5 || pAge[k] > 0.22 || iy < 1 || iy > field.ny - 2 || field.kind[ix + iy * field.nx] === CELL_WALL) pSection[k] = -1;
        else {
          pX[k] = nextX;
          pY[k] = nextY;
        }
        continue;
      }
      const position = pX[k] / cell;
      const index = position < 0 ? 0 : position >= section.count ? section.count - 1 : position | 0;
      const at = section.first + index;
      // Le gaz va plus vite au centre du tube qu'au bord : approximation visuelle, le calcul est à une dimension.
      const profile = 1.12 - 0.3 * pY[k] * pY[k];
      const next = pX[k] + net.u[at] * profile * seconds;
      pBurnt[k] += (net.y[at] - pBurnt[k]) * 0.1;
      if (next < 0 || next > section.count * cell) pSection[k] = -1;
      else pX[k] = next;
    }
  }

}
