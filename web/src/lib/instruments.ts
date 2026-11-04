/**
 * Ce que partagent les jauges. Elles écrivent directement dans le DOM, une fois par image au plus et seulement
 * ce qui a changé : ni rendu de composant à 30 Hz, ni texte fabriqué à chaque image.
 */
import type { EngineConfig } from './config';
import type { Telemetry } from './engine';
import { lang, type Lang } from './i18n.svelte';
import type { Motion } from './motion';

/** Un point du cadran. L'angle est en degrés, 0 à droite, dans le sens des aiguilles. */
export function polar(cx: number, cy: number, radius: number, angle: number): [number, number] {
  const radians = (angle * Math.PI) / 180;
  return [cx + radius * Math.cos(radians), cy + radius * Math.sin(radians)];
}

export function arcPath(cx: number, cy: number, radius: number, from: number, to: number): string {
  if (to - from < 0.01) return '';
  const [x1, y1] = polar(cx, cy, radius, from);
  const [x2, y2] = polar(cx, cy, radius, to);
  return `M ${x1.toFixed(2)} ${y1.toFixed(2)} A ${radius} ${radius} 0 ${to - from > 180 ? 1 : 0} 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`;
}

/** Une graduation lisible : 1, 2 ou 5 fois une puissance de dix, pour environ `count` intervalles. */
export function niceStep(span: number, count: number): number {
  const raw = span / Math.max(count, 1);
  const power = 10 ** Math.floor(Math.log10(raw || 1));
  const scaled = raw / power;
  return (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10) * power;
}

/** Longueur et départ d'un trait, sur `pathLength` 1000 : une table évite de fabriquer ce texte à chaque image. */
export const DASH_LENGTH: string[] = Array.from({ length: 1001 }, (_, i) => `${i} 2000`);
export const DASH_OFFSET: string[] = Array.from({ length: 1001 }, (_, i) => `${-i}`);
/** La même table pour une largeur ou une position sur une barre de 1000 unités. */
export const WIDTHS: string[] = Array.from({ length: 1001 }, (_, i) => String(i));

export type Tick = (text: boolean, aria: boolean) => void;

const TEXT_MS = 66;
const ARIA_MS = 1000;

/** L'horloge d'un groupe de jauges : l'aiguille à chaque image, les chiffres assez lentement pour être lus. */
export class InstrumentClock {
  private ticks: Tick[] = [];
  private textAt = 0;
  private ariaAt = 0;

  add(tick: Tick): () => void {
    this.ticks.push(tick);
    return () => {
      const index = this.ticks.indexOf(tick);
      if (index >= 0) this.ticks.splice(index, 1);
    };
  }

  run(now: number): void {
    const text = now >= this.textAt;
    if (text) this.textAt = now + TEXT_MS;
    const aria = now >= this.ariaAt;
    if (aria) this.ariaAt = now + ARIA_MS;
    for (let i = 0; i < this.ticks.length; i++) this.ticks[i](text, aria);
  }

  /** Un chiffre à l'écran : réécrit quand sa valeur arrondie change, ou la langue. */
  number(element: Element, read: () => number, format: (value: number) => string, quantum = 1): () => void {
    let last = NaN;
    let lastLang: Lang | null = null;
    return this.add((text) => {
      if (!text) return;
      const rounded = Math.round(read() / quantum);
      const current = lang();
      if (rounded === last && current === lastLang) return;
      last = rounded;
      lastLang = current;
      element.textContent = format(rounded * quantum);
    });
  }
}

/** Ce que les jauges lisent, calculé une fois par image. Rien n'est inventé : tout vient de la télémétrie ou du modèle du gaz. */
export class Readings {
  /** Pression au plénum, relative à l'atmosphère (bar) : négative en dépression, positive sous pression. */
  manifoldBar = 0;
  /** Contre-pression au collecteur d'échappement, relative à l'atmosphère (bar), moyennée sur les bouffées. */
  backBar = 0;
  /** Température du gaz au collecteur et à la sortie (°C). */
  collectorC = 20;
  tailpipeC = 20;
  /** Accélération longitudinale (g) : la dérivée de la vitesse de la voiture. */
  accelG = 0;
  /** Sortie du son, crête sur la dernière période, en dB sous la pleine échelle. */
  peakDb = -60;
  /** Eau et huile (°C), du réseau thermique : l'air ambiant tant que le son n'a pas démarré. */
  coolantC = 20;
  oilC = 20;
  private lastNow = -1;

  constructor(private motion: Motion) {}

  update(now: number, t: Telemetry, config: EngineConfig, running: boolean): void {
    if (now === this.lastNow) return;
    const seconds = this.lastNow < 0 ? 0 : Math.min((now - this.lastNow) / 1000, 0.1);
    this.lastNow = now;
    const motion = this.motion;
    this.accelG = motion.acceleration / 9.81;
    this.peakDb = t.peak > 1e-3 ? 20 * Math.log10(t.peak) : -60;
    // Le réseau thermique tourne même moteur coupé : il refroidit. Avant le premier son, le moteur est tel qu'on l'a choisi :
    // chaud, à la référence (eau 90 °C, huile 100 °C, celles de `kWarmCoolantC` et `kWarmOilC`), ou froid, à l'air ambiant.
    const cold = config.startTemp === 'cold';
    this.coolantC = running ? t.coolantC : cold ? config.ambientC : 90;
    this.oilC = running ? t.oilC : cold ? config.ambientC : 100;

    const net = config.electric ? null : motion.meters;
    const ready = net !== null && net.n > 0 && net.zP[net.zonePlenum] > 0;
    const turning = running && t.rpm > 150 && !t.cranking && !t.stalled;
    // Moteur à l'arrêt, plus de dépression ni de pression : le collecteur est à l'air libre.
    const manifold = ready && turning ? (net.zP[net.zonePlenum] - net.pAtm) / 1e5 : 0;
    const back = ready && turning ? (net.zP[net.zoneJoin] - net.pAtm) / 1e5 : 0;
    this.manifoldBar += (manifold - this.manifoldBar) * (1 - Math.exp(-seconds / 0.06));
    // Les bouffées se suivent au temps du gaz : la moyenne aussi, pour que l'aiguille ne batte pas au ralenti.
    this.backBar += (back - this.backBar) * (1 - Math.exp(-seconds / Math.max(0.05 * motion.slow, 0.08)));
    if (ready) {
      const glide = 1 - Math.exp(-seconds / 0.15);
      this.collectorC += (net.zT[net.zoneJoin] - 273.15 - this.collectorC) * glide;
      this.tailpipeC += (net.exitT - 273.15 - this.tailpipeC) * glide;
    }
  }
}

/** Pour se repérer : ce qui fait un niveau comparable, en dB(A). */
export const LEVEL_LANDMARKS = [30, 45, 60, 75, 90, 105, 120, 140] as const;
export type LevelLandmark = (typeof LEVEL_LANDMARKS)[number];

export const nearestLandmark = (db: number): LevelLandmark =>
  LEVEL_LANDMARKS.reduce((best, item) => (Math.abs(item - db) < Math.abs(best - db) ? item : best));
