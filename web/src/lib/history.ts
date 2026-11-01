/**
 * Les dernières minutes de télémétrie, pour les courbes dans le temps. Mémoire fixe, aucune allocation :
 * écrire un échantillon ne coûte que quelques affectations, panneau fermé ou non, et rien n'est dessiné
 * tant que personne ne regarde.
 */
import type { Telemetry } from './engine';
import type { Motion } from './motion';

export const HISTORY_HZ = 30;
const CAPACITY = HISTORY_HZ * 180;

export type ChannelKey =
  | 'rpm' | 'inputRpm' | 'speed' | 'gear' | 'torque' | 'clutchTorque' | 'power' | 'load' | 'boost' | 'clutchSlipRpm' | 'clutchSlipKw' | 'clutchTemp' | 'tyreSlip' | 'levelDbA';

const CHANNELS: ChannelKey[] = ['rpm', 'inputRpm', 'speed', 'gear', 'torque', 'clutchTorque', 'power', 'load', 'boost', 'clutchSlipRpm', 'clutchSlipKw', 'clutchTemp', 'tyreSlip', 'levelDbA'];

export class TelemetryHistory {
  readonly capacity = CAPACITY;
  readonly times = new Float64Array(CAPACITY);
  readonly data = {} as Record<ChannelKey, Float32Array>;
  /** Nombre total d'échantillons écrits. */
  written = 0;
  /** Le niveau en dB(A) n'existe que si la simulation le donne : sinon, pas de courbe du tout. */
  hasLevel = false;
  /** Dessins faits par les graphiques : nul tant que le panneau est fermé. */
  draws = 0;
  private motion: Motion;

  constructor(motion: Motion) {
    this.motion = motion;
    for (const key of CHANNELS) this.data[key] = new Float32Array(CAPACITY);
  }

  push(t: Telemetry, now: number): void {
    const at = this.written % CAPACITY;
    const d = this.data;
    this.times[at] = now;
    d.rpm[at] = t.rpm;
    d.inputRpm[at] = t.clutchInputRpm;
    d.speed[at] = t.speed;
    d.gear[at] = t.gear;
    d.torque[at] = t.torque;
    d.clutchTorque[at] = t.clutchTorque;
    d.power[at] = t.power;
    d.load[at] = t.load;
    d.boost[at] = t.boost;
    d.clutchSlipRpm[at] = Math.abs(t.rpm - t.clutchInputRpm);
    d.clutchSlipKw[at] = t.clutchSlipKw;
    d.clutchTemp[at] = this.motion.clutchSurface;
    d.tyreSlip[at] = t.slip;
    const level = (t as Telemetry & { levelDbA?: number }).levelDbA;
    if (typeof level === 'number') {
      this.hasLevel = true;
      d.levelDbA[at] = level;
    }
    this.written++;
  }

  /** Nombre d'échantillons encore en mémoire. */
  get length(): number {
    return Math.min(this.written, CAPACITY);
  }

  /** Index du `k`-ième échantillon en partant du plus récent (0 : le dernier écrit). */
  indexFromEnd(k: number): number {
    return (this.written - 1 - k + 2 * CAPACITY) % CAPACITY;
  }
}
