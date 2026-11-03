/**
 * L'état animé de la voiture : une seule horloge pour toutes les vues, réglée sur la télémétrie.
 * Un moteur à 6000 tr/min fait 50 cycles par seconde, trop pour un écran à 60 images : tout est
 * ralenti du même facteur, sans plafond, pour rester proportionnel au vrai régime.
 */
import { activeClutch, advanceRace, bearingTouches, type RaceState } from './clutchMechanism';
import type { EngineConfig, Exhaust } from './config';
import type { Telemetry } from './engine';
import { GasFlow, gasKey } from './fluid';
import { ANTI_LAG_CODE, GasNetwork, antiLagFires, type GasInputs } from './fluidGas';
import { GasPainter } from './fluidRender';

export const SLOW_MOTION = 12;

/** Cylindres dans l'ordre où ils s'allument (numérotés à partir de 1). */
export const FIRING_ORDERS: Record<number, number[]> = {
  2: [1, 2],
  3: [1, 3, 2],
  4: [1, 3, 4, 2],
  5: [1, 2, 4, 5, 3],
  6: [1, 5, 3, 6, 2, 4],
  8: [1, 8, 4, 3, 6, 5, 7, 2],
  10: [1, 6, 5, 10, 2, 7, 3, 8, 4, 9],
  12: [1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10],
};

export const firingOrder = (cylinders: number) => FIRING_ORDERS[cylinders] ?? FIRING_ORDERS[4];

/** Une bouffée qui parcourt la ligne. `burnt` faux : de l'air pompé, sans combustion. */
export interface Pulse {
  at: number;
  strength: number;
  heat: number;
  burnt: boolean;
  bank: number;
}

const MAX_PULSES = 90;
/**
 * L'arbre d'un turbo fait 2500 tours par seconde : même douze fois ralenti, l'écran ne peut pas le montrer. Sa rotation
 * à l'écran reste proportionnelle à la vraie vitesse et suit `slow`, avec ce ralenti fixe en plus. Le nombre affiché, lui, est le vrai.
 */
const TURBINE_EXTRA_SLOW = 60;
/** Énergie d'une détonation (J) : un milligramme de carburant ou deux, selon ce que la ligne laisse sortir. */
const POP_JOULES: Record<Exhaust, number> = { stock: 45, sport: 90, straight: 130, no_muffler: 120, no_resonator: 50, quiet: 25 };
const CLUTCH_AMBIENT = 60;
/** Inerties thermiques (J/K) et conductances (W/K) : un embrayage à sec encaisse un démarrage, pas une minute de patinage. */
const CLUTCH_SURFACE_J = 900;
const CLUTCH_CORE_J = 9000;
const CLUTCH_INNER_W = 70;
const CLUTCH_OUTER_W = 14;

const MOTION_KEY = 'revforge.motion';

/** Le système demande moins d'animation. Un bureau aux animations coupées le dit aussi au navigateur. */
export const systemReducesMotion = (): boolean => typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;

/** Le choix du visiteur passe avant celui du système. Le stockage peut être refusé : la page s'en passe. */
export function wantsAnimations(): boolean {
  try {
    const stored = localStorage.getItem(MOTION_KEY);
    if (stored === 'on') return true;
    if (stored === 'off') return false;
  } catch {
    // Rien de retenu.
  }
  return !systemReducesMotion();
}

/** Combien de fois tout est ralenti : 1, c'est le temps réel. */
export const SPEEDS = [SLOW_MOTION, 4, 1] as const;
const SPEED_KEY = 'revforge.speed';

export function storedSpeed(): number {
  try {
    const stored = Number(localStorage.getItem(SPEED_KEY));
    if ((SPEEDS as readonly number[]).includes(stored)) return stored;
  } catch {
    // Rien de retenu.
  }
  return SLOW_MOTION;
}

export function rememberSpeed(slow: number): void {
  try {
    localStorage.setItem(SPEED_KEY, String(slow));
  } catch {
    // Le choix vaudra pour cette visite.
  }
}

export function rememberAnimations(on: boolean): void {
  try {
    localStorage.setItem(MOTION_KEY, on ? 'on' : 'off');
  } catch {
    // Le choix vaudra pour cette visite.
  }
}

export class Motion {
  rpm = 0;
  speed = 0;
  load = 0;
  boost = 0;
  turbine = 0;
  release = 0;
  /** L'arbre du turbo (tr/min) et les soupapes : ce que le modèle de conduite calcule, lissé comme le reste. */
  turboRpm = 0;
  /** L'arbre du compresseur mécanique (tr/min), son angle (radians) et ce qu'il prend au vilebrequin (kW) : ce que le modèle calcule, lissé comme le reste. */
  chargerRpm = 0;
  chargerAngle = 0;
  chargerKw = 0;
  chargerSpill = 0;
  wastegate = 0;
  blowoff = 0;
  compressorRatio = 1;
  compressorOutK = 293;
  coolerOutK = 293;
  /** L'anti-lag, tel que le modèle de conduite le dit : en marche, chaleur au collecteur (W), air qu'il y envoie (kg/s). */
  antiLag = false;
  antiLagHeatW = 0;
  antiLagAirFlow = 0;
  private antiLagEvent = 0;
  /** Au-delà de quelques tours par seconde, l'œil ne suit plus les aubes : un disque flou, 0 à 1. */
  turbineBlur = 0;
  wheelRpm = 0;
  slip = 0;
  acceleration = 0;
  /** Température de la ligne, 0 à 1 : lente à monter, encore plus lente à redescendre. */
  heat = 0;
  /** Position dans le cycle à quatre temps, 0 à 1, et nombre total de cycles parcourus. */
  cycle = 0;
  turns = 0;
  /** Angle de l'arbre du turbo et du rotor électrique (radians). */
  turbineAngle = 0;
  rotorAngle = 0;
  /** Vitesse du gaz le long de la ligne, en longueurs par seconde. */
  gasSpeed = 0;
  /** Écoulement de l'air à l'admission, 0 à 1 et plus sous pression. */
  airFlow = 0;
  pulses: Pulse[] = [];
  // L'embrayage : part du couple qui passe, vitesses, couple, puissance dissipée, angle de ses pièces.
  clutchShare = 1;
  inputRpm = 0;
  clutchTorque = 0;
  clutchSlipKw = 0;
  flywheelAngle = 0;
  discAngle = 0;
  /** La boîte : angles des arbres primaire et de sortie (radians), au même ralenti que le reste. */
  outputRpm = 0;
  boxInputAngle = 0;
  boxOutputAngle = 0;
  /** Les mêmes angles, sans jamais revenir à zéro, et celui du volant moteur : une roue qu'on en déduit par un rapport ne saute pas. */
  boxInputTotal = 0;
  boxOutputTotal = 0;
  flywheelTotal = 0;
  /** La bague tournante de la butée : arrêtée loin des doigts, à la vitesse du volant dès qu'elle les touche. */
  readonly releaseRace: RaceState = { rpm: 0, angle: 0, cageAngle: 0 };
  /** Double embrayage : part serrée de K1 (rapports impairs) et de K2. Le couple passe de l'un à l'autre. */
  clutchK1 = 0;
  clutchK2 = 0;
  /** Temps moyen d'une image de la vue de l'embrayage (ms) : ce qu'elle coûte au fil principal. */
  clutchCostMs = 0;
  /** Position du baladeur : 0 au point mort, 1 engagé. Il court pendant le passage. */
  sleeve = 0;
  /** Température (°C) : garnitures et surfaces frottantes, et masse d'acier autour. En temps réel, pas au ralenti. */
  clutchSurface = CLUTCH_AMBIENT;
  clutchCore = CLUTCH_AMBIENT;
  /** Le gaz : réseau 0D/1D, volumes et panache, au temps de cette horloge. */
  readonly gas = new GasFlow();
  readonly painter = new GasPainter(this.gas);
  /** Moins d'animation : pistons, gaz et bouffées restent immobiles. Suit le système, sauf choix du visiteur. */
  reduced = !wantsAnimations();
  /** Facteur de ralenti de toutes les vues et du gaz : 1, c'est le temps réel. */
  slow = storedSpeed();
  private gasInputs: GasInputs = { rpm: 0, load: 0, boost: 0, release: 0, burning: false, heat: 0, compressorRatio: 1, compressorOutK: 293, wastegate: 0, antiLag: 0, antiLagHeatW: 0, antiLagAirFlow: 0 };
  private gasConfig: EngineConfig | null = null;
  // Ce qu'il reste à faire avancer au gaz pour l'image en cours : une jauge sans gaz peut passer la première.
  private gasDue = false;
  private dueSeconds = 0;
  private dueBefore = 0;
  private dueTurns = 0;
  /**
   * Moins d'animation : le gaz visible s'arrête, mais les jauges doivent rester vraies. Elles lisent alors
   * le même modèle sur un réseau à part, sans panache ni particules, qui n'avance que si on le regarde.
   */
  meterWanted = false;
  private meterNet: GasNetwork | null = null;
  private meterKey = '';
  private meterCycle = 0;
  /** Dernier cylindre allumé, et temps écoulé depuis. */
  lastCylinder = 0;
  sinceFiring = 1;
  burning = false;
  flame = 0;
  private lastPops = 0;
  private lastNow = -1;

  /**
   * Peut être appelé par chaque vue : ne compte qu'une fois par image.
   * `gas` faux : une jauge qui n'en a pas besoin. Le gaz attend qu'une vue qui le montre le demande,
   * et un écran sans gaz visible ne le calcule pas.
   */
  update(now: number, t: Telemetry, config: EngineConfig, gas = true): void {
    if (now === this.lastNow) {
      if (gas) this.stepGas(config);
      return;
    }
    const seconds = this.lastNow < 0 ? 0 : Math.min((now - this.lastNow) / 1000, 0.1);
    this.lastNow = now;

    const glide = 1 - Math.exp(-seconds / 0.04);
    this.rpm += (t.rpm - this.rpm) * glide;
    this.load += (t.load - this.load) * glide;
    this.boost += (t.boost - this.boost) * glide;
    this.turbine += (t.turbine - this.turbine) * glide;
    this.release += (t.turboRelease - this.release) * glide;
    this.turboRpm += (t.turboRpm - this.turboRpm) * glide;
    this.chargerRpm += (t.chargerRpm - this.chargerRpm) * glide;
    this.chargerKw += (t.chargerKw - this.chargerKw) * glide;
    this.chargerSpill += (t.chargerSpill - this.chargerSpill) * glide;
    this.wastegate += (t.turboWastegate - this.wastegate) * glide;
    this.blowoff += (t.turboBlowoff - this.blowoff) * glide;
    this.compressorRatio += ((1.01325 + t.turboOutlet) / Math.max(1.01325 + t.turboInlet, 0.5) - this.compressorRatio) * glide;
    this.compressorOutK += (t.turboOutC + 273.15 - this.compressorOutK) * glide;
    this.coolerOutK += (t.turboCoolC + 273.15 - this.coolerOutK) * glide;
    this.antiLag = t.antiLag > 0;
    this.antiLagHeatW += (t.antiLagHeatKw * 1000 - this.antiLagHeatW) * glide;
    this.antiLagAirFlow += (t.antiLagAirFlow - this.antiLagAirFlow) * glide;
    this.wheelRpm += (t.wheelRpm - this.wheelRpm) * glide;
    this.slip += (t.slip - this.slip) * glide;
    this.clutchShare += (t.clutch - this.clutchShare) * glide;
    this.inputRpm += (t.clutchInputRpm - this.inputRpm) * glide;
    this.clutchTorque += (t.clutchTorque - this.clutchTorque) * glide;
    this.clutchSlipKw += (t.clutchSlipKw - this.clutchSlipKw) * glide;
    this.outputRpm += (t.outputRpm - this.outputRpm) * glide;
    this.sleeve += ((t.meshed ? 1 : 0) - this.sleeve) * (1 - Math.exp(-seconds / 0.03));
    if (!this.reduced) {
      this.boxInputAngle = (this.boxInputAngle + ((this.inputRpm / 60 / this.slow) * 2 * Math.PI * seconds)) % (Math.PI * 2);
      this.boxOutputAngle = (this.boxOutputAngle + ((this.outputRpm / 60 / this.slow) * 2 * Math.PI * seconds)) % (Math.PI * 2);
      this.boxInputTotal += (this.inputRpm / 60 / this.slow) * 2 * Math.PI * seconds;
      this.boxOutputTotal += (this.outputRpm / 60 / this.slow) * 2 * Math.PI * seconds;
      this.flywheelTotal += (this.rpm / 60 / this.slow) * 2 * Math.PI * seconds;
    }
    if (seconds > 0) {
      const inner = CLUTCH_INNER_W * (this.clutchSurface - this.clutchCore);
      this.clutchSurface += ((t.clutchSlipKw * 1000 - inner) / CLUTCH_SURFACE_J) * seconds;
      this.clutchCore += ((inner - CLUTCH_OUTER_W * (this.clutchCore - CLUTCH_AMBIENT)) / CLUTCH_CORE_J) * seconds;
    }
    // Un double embrayage garde ses butées en appui : leur bague tourne avec le moteur, pédale ou non.
    const dualClutch = config.boxKind === 'dual_clutch';
    advanceRace(this.releaseRace, dualClutch || bearingTouches(this.clutchShare), this.rpm, seconds, this.slow, this.reduced);
    if (dualClutch) {
      const active = activeClutch(t.gear);
      const shift = 1 - Math.exp(-seconds / 0.06);
      this.clutchK1 += ((active === 1 ? t.clutch : 0) - this.clutchK1) * shift;
      this.clutchK2 += ((active === 2 ? t.clutch : 0) - this.clutchK2) * shift;
    }
    const previous = this.speed;
    this.speed += (t.speed - this.speed) * glide;
    if (seconds > 0) {
      const now2 = (this.speed - previous) / 3.6 / seconds;
      this.acceleration += (now2 - this.acceleration) * (1 - Math.exp(-seconds / 0.25));
    }

    const redline = config.electric ? 12000 : config.redlineRpm;
    const rpmShare = Math.min(this.rpm / redline, 1.1);
    const flow = rpmShare * (0.2 + 0.8 * this.load);

    // Le gaz chauffe sous charge et à haut régime ; il faut bien plus longtemps pour refroidir.
    let wantedHeat = t.rpm > 150 && !config.electric ? 0.25 + 0.75 * flow : 0;
    // Pied levé, l'anti-lag garde la ligne brûlante : c'est la température que le modèle calcule avant la turbine qui compte.
    if (this.antiLag) wantedHeat = Math.max(wantedHeat, Math.min(t.turboGasC / 1000, 1));
    const heatTau = wantedHeat > this.heat ? 5 : 16;
    this.heat += (wantedHeat - this.heat) * (1 - Math.exp(-seconds / heatTau));

    this.gasSpeed = 0.1 + 1.05 * flow;
    const pressure = config.aspiration !== 'natural' ? 1 + this.boost * config.boostBar : 1;
    this.airFlow = flow * pressure;

    if (!this.reduced) {
      const turbineTurns = this.turboRpm / 60 / this.slow / TURBINE_EXTRA_SLOW;
      this.turbineAngle = (this.turbineAngle + turbineTurns * 2 * Math.PI * seconds) % (Math.PI * 2);
      this.turbineBlur = Math.min(1, Math.max(0, (turbineTurns - 6) / 10));
      // Le compresseur : à sa vraie vitesse, comme le vilebrequin. Seule la roue d'un centrifuge a besoin du ralenti de la turbine.
      const chargerTurns = this.chargerRpm / 60 / this.slow / (config.superchargerKind === 'centrifugal' ? TURBINE_EXTRA_SLOW : 1);
      this.chargerAngle = (this.chargerAngle + chargerTurns * 2 * Math.PI * seconds) % (Math.PI * 2);
      this.rotorAngle = (this.rotorAngle + ((this.rpm / 60 / this.slow) * 2 * Math.PI * seconds)) % (Math.PI * 2);
      this.flywheelAngle = (this.flywheelAngle + ((this.rpm / 60 / this.slow) * 2 * Math.PI * seconds)) % (Math.PI * 2);
      this.discAngle = (this.discAngle + ((this.inputRpm / 60 / this.slow) * 2 * Math.PI * seconds)) % (Math.PI * 2);
    }

    const turns = this.reduced ? 0 : (this.rpm / 120 / this.slow) * seconds;
    this.turns += turns;
    const before = this.cycle;
    this.cycle = (this.cycle + turns) % 1;

    this.burning = t.rpm > 150 && !t.cut && !t.cranking && !config.electric;
    this.sinceFiring += seconds;
    if (!config.electric) this.emit(before, turns, config);

    // Les bouffées restent comptées ici : sur place, sans allouer, elles disent ce qui part de chaque cylindre.
    let kept = 0;
    for (let i = 0; i < this.pulses.length; i++) {
      const pulse = this.pulses[i];
      pulse.at += this.gasSpeed * seconds;
      if (pulse.at < 1) this.pulses[kept++] = pulse;
    }
    this.pulses.length = kept;

    if (t.pops !== this.lastPops) {
      this.flame = 1;
      this.lastPops = t.pops;
      if (!this.reduced) this.gas.pop(POP_JOULES[config.exhaust] ?? 90);
    }
    // Une détonation dure une vingtaine de millisecondes : au ralenti, on a le temps de la voir.
    this.flame = Math.max(0, this.flame - seconds * 3);

    this.gasDue = true;
    this.dueSeconds = seconds;
    this.dueBefore = before;
    this.dueTurns = turns;
    if (gas) this.stepGas(config);
  }

  private stepGas(config: EngineConfig): void {
    if (!this.gasDue) return;
    this.gasDue = false;
    this.advanceGas(this.dueSeconds, this.dueBefore, this.dueTurns, config);
  }

  /** Le réseau que lisent les jauges : celui du gaz visible, ou le leur quand les animations sont coupées. */
  get meters(): GasNetwork | null {
    return this.reduced ? this.meterNet : this.gas.net;
  }

  private advanceMeters(seconds: number, config: EngineConfig, inputs: GasInputs): void {
    if (config.electric) return;
    const key = gasKey(config);
    if (key !== this.meterKey) {
      this.meterKey = key;
      this.meterNet ??= new GasNetwork();
      this.meterNet.build(config, firingOrder(config.cylinders));
      if (this.rpm > 300) this.meterNet.prefill(this.heat);
    }
    if (this.rpm <= 20 || seconds <= 0) return;
    const turns = (this.rpm / 120 / this.slow) * seconds;
    this.meterNet!.advance(seconds / this.slow, this.meterCycle, turns, inputs);
    this.meterCycle = (this.meterCycle + turns) % 1;
  }

  private advanceGas(seconds: number, before: number, turns: number, config: EngineConfig): void {
    if (config !== this.gasConfig) {
      this.gasConfig = config;
      this.gas.configure(config, firingOrder(config.cylinders), this.rpm > 300, this.heat);
    }
    const inputs = this.gasInputs;
    inputs.rpm = this.rpm;
    inputs.load = this.load;
    inputs.boost = this.boost;
    // La soupape telle qu'elle est ouverte : c'est elle qui vide le conduit.
    inputs.release = this.blowoff;
    inputs.compressorRatio = this.compressorRatio;
    inputs.compressorOutK = this.compressorOutK;
    inputs.wastegate = this.wastegate;
    inputs.burning = this.burning;
    inputs.heat = this.heat;
    inputs.antiLag = this.antiLag ? ANTI_LAG_CODE[config.antiLag ?? 'off'] : 0;
    inputs.antiLagHeatW = this.antiLagHeatW;
    inputs.antiLagAirFlow = this.antiLagAirFlow;
    if (this.reduced) {
      this.gas.active = false;
      if (this.meterWanted) this.advanceMeters(seconds, config, inputs);
      this.meterWanted = false;
      return;
    }
    // Les animations peuvent revenir en marche : le gaz repart de lui-même.
    this.gas.active = !config.electric;
    this.gas.flame = this.flame;
    this.gas.carSpeed = this.speed / 3.6;
    this.gas.advance(seconds / this.slow, before, turns, inputs);
  }

  /** Une bouffée par explosion, au moment où le cycle atteint le tour de ce cylindre. */
  private emit(before: number, turns: number, config: EngineConfig): void {
    const count = config.cylinders;
    const order = firingOrder(count);
    const banks = config.layout !== 'inline' && count >= 6 ? 2 : 1;
    const from = Math.floor(before * count);
    const crossings = Math.floor((before + turns) * count) - from;
    for (let i = 1; i <= crossings; i++) {
      const slot = (from + i) % count;
      const cylinder = order[slot] - 1;
      this.lastCylinder = cylinder;
      this.sinceFiring = 0;
      const push = 0.3 + 0.7 * this.load + 0.25 * this.boost * config.boostBar;
      // La coupure de cylindres : au même rythme que le son, certaines explosions manquent.
      const fires = this.burning && !(this.antiLag && config.antiLag === 'cut' && !antiLagFires(this.antiLagEvent++));
      this.pulses.push({
        at: 0,
        strength: fires ? Math.min(push, 1.4) : 0.18,
        heat: fires ? 0.3 + 0.7 * this.load : 0,
        burnt: fires,
        bank: cylinder % banks,
      });
    }
    if (this.pulses.length > MAX_PULSES) this.pulses.splice(0, this.pulses.length - MAX_PULSES);
  }

  /** La vitesse de l'arbre, calculée par le bilan de puissance du modèle de conduite. */
  turbineRpm(_config?: EngineConfig): number {
    return this.turboRpm;
  }
}
