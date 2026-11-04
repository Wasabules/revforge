/**
 * L'architecture d'un moteur à pistons : où est chaque cylindre, quand il allume, dans quelle culasse il souffle. Du calcul pur,
 * sans DOM ni texte, recopie de `core/src/architecture.cpp` : le dessin (`engineLayout.ts`), le gaz (`fluidGas.ts`) et les bouffées
 * (`motion.ts`) lisent la même chose que le son. `architectures.spec.ts` compare les tables, ligne à ligne.
 *
 * Vu de face, x à droite : la culasse 0 est à gauche (axe incliné de −β), la 1 à droite. Un VR a une seule culasse sur deux rangées
 * proches ; un W en a deux, chacune sur deux rangées. Le vilebrequin tourne dans le sens des aiguilles d'une montre.
 */
import type { EngineConfig, Layout } from './config';

/** Cylindres dans l'ordre où ils s'allument (numérotés à partir de 1), par nombre : en ligne et en V. */
export const FIRING_ORDERS: Record<number, number[]> = {
  2: [1, 2],
  3: [1, 3, 2],
  4: [1, 3, 4, 2],
  5: [1, 2, 4, 5, 3],
  6: [1, 5, 3, 6, 2, 4],
  8: [1, 8, 4, 3, 6, 5, 7, 2],
  10: [1, 6, 5, 10, 2, 7, 3, 8, 4, 9],
  12: [1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10],
  14: [1, 8, 2, 9, 4, 11, 6, 13, 7, 14, 5, 12, 3, 10],
  16: [1, 9, 6, 14, 2, 10, 5, 13, 8, 16, 3, 11, 7, 15, 4, 12],
};

export const firingOrder = (cylinders: number): number[] => FIRING_ORDERS[cylinders] ?? FIRING_ORDERS[4];

/** Une ligne de `kEntries` (architecture.cpp) : ordre, culasses de l'avant vers l'arrière, rangée de chaque numéro. */
interface Entry {
  layout: Layout;
  cylinders: number;
  headAngleDeg: number;
  rowAngleDeg: number;
  rowOffsetMm: number;
  sharedPins: boolean;
  order: number[];
  left: number[];
  right: number[];
  rows: string;
}

const entry = (layout: Layout, cylinders: number, headAngleDeg: number, rowAngleDeg: number, rowOffsetMm: number, sharedPins: boolean, order: number[], left: number[], right: number[], rows: string): Entry =>
  ({ layout, cylinders, headAngleDeg, rowAngleDeg, rowOffsetMm, sharedPins, order, left, right, rows });

/** Recopie de `kEntries` : même ordre, mêmes nombres. */
export const ENTRIES: readonly Entry[] = [
  entry('v', 2, 90, 0, 0, false, [1, 2], [1], [2], ''),
  entry('v', 3, 60, 0, 0, false, [1, 3, 2], [1, 2], [3], ''),
  entry('v', 4, 60, 0, 0, false, [1, 3, 4, 2], [1, 2], [4, 3], ''),
  entry('v', 5, 72, 0, 0, false, [1, 2, 4, 5, 3], [1, 2, 3], [4, 5], ''),
  entry('v', 6, 60, 0, 0, false, [1, 5, 3, 6, 2, 4], [1, 2, 3], [6, 5, 4], ''),
  entry('v', 8, 90, 0, 0, false, [1, 8, 4, 3, 6, 5, 7, 2], [1, 4, 7, 6], [8, 3, 2, 5], ''),
  entry('cross_plane', 8, 90, 0, 0, false, [1, 8, 4, 3, 6, 5, 7, 2], [2, 4, 6, 8], [1, 3, 5, 7], ''),
  entry('v', 10, 90, 0, 0, false, [1, 6, 5, 10, 2, 7, 3, 8, 4, 9], [1, 2, 3, 4, 5], [6, 7, 8, 9, 10], ''),
  entry('v', 12, 60, 0, 0, false, [1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10], [1, 2, 3, 4, 5, 6], [7, 8, 9, 10, 11, 12], ''),
  entry('vr', 5, 0, 15, 12.5, false, [1, 2, 4, 5, 3], [1, 2, 3, 4, 5], [], '01010'),
  entry('vr', 6, 0, 15, 12.5, false, [1, 5, 3, 6, 2, 4], [1, 2, 3, 4, 5, 6], [], '010101'),
  entry('w', 8, 72, 15, 12.5, false, [1, 5, 4, 8, 6, 3, 7, 2], [1, 2, 3, 4], [5, 6, 7, 8], '01010101'),
  entry('w', 12, 72, 15, 12.5, false, [1, 12, 5, 8, 3, 10, 6, 7, 2, 11, 4, 9], [1, 2, 3, 4, 5, 6], [7, 8, 9, 10, 11, 12], '010101010101'),
  entry('w', 16, 90, 15, 12.5, false, [1, 14, 9, 4, 7, 12, 15, 6, 13, 8, 3, 16, 11, 2, 5, 10], [1, 2, 3, 4, 5, 6, 7, 8], [9, 10, 11, 12, 13, 14, 15, 16], '0101010101010101'),
  entry('v', 14, 45, 0, 0, true, [1, 8, 2, 9, 4, 11, 6, 13, 7, 14, 5, 12, 3, 10], [1, 2, 3, 4, 5, 6, 7], [8, 9, 10, 11, 12, 13, 14], ''),
  entry('v', 16, 45, 0, 0, true, [1, 9, 6, 14, 2, 10, 5, 13, 8, 16, 3, 11, 7, 15, 4, 12], [1, 2, 3, 4, 5, 6, 7, 8], [9, 10, 11, 12, 13, 14, 15, 16], ''),
];

export interface Architecture {
  /** La disposition réellement construite : un VR ou un W qui n'existe pas à ce nombre se construit en V. */
  layout: Layout;
  cylinders: number;
  /** Culasses, donc collecteurs : une en ligne et en VR, deux en V, en W et à plat. */
  heads: number;
  headAngleDeg: number;
  rowAngleDeg: number;
  rowOffsetMm: number;
  sharedPins: boolean;
  order: number[];
  /** Angle du vilebrequin au point mort haut d'allumage de chaque rang de l'ordre : 0, puis croissant, sous 720. */
  firingDeg: number[];
  /** Par numéro − 1 : culasse, rangée dans la culasse, rang le long du vilebrequin, inclinaison de l'axe (°, sens horaire). */
  head: number[];
  row: number[];
  position: number[];
  axisDeg: number[];
  /** Vrai si tous les intervalles sont égaux. */
  even: boolean;
}

const entryOf = (layout: Layout, cylinders: number) => ENTRIES.find((item) => item.layout === layout && item.cylinders === cylinders);

/** Recopie de `builtLayout` : VR5 et VR6, W8, W12 et W16 existent ; ailleurs, et au-delà de 12 cylindres hors W, un V. */
export function builtLayout(layout: Layout, cylinders: number): Layout {
  if (layout === 'rotary') return layout;
  if (layout === 'vr' || layout === 'w') return entryOf(layout, cylinders) ? layout : 'v';
  if (cylinders > 12) return 'v';
  return layout;
}

/** Les nombres de cylindres d'un VR et d'un W : ceux qui existent. */
export const VR_CYLINDERS = ENTRIES.filter((item) => item.layout === 'vr').map((item) => item.cylinders);
export const W_CYLINDERS = ENTRIES.filter((item) => item.layout === 'w').map((item) => item.cylinders);

const cache = new Map<string, Architecture>();

/** Recopie de `architectureOf`. Un rotatif n'en a pas : `cylinders` vaut 0. Le résultat est partagé : ne pas le modifier. */
export function architectureOf(config: Pick<EngineConfig, 'layout' | 'cylinders'>): Architecture {
  const key = `${config.layout}|${config.cylinders}`;
  const known = cache.get(key);
  if (known) return known;
  const built = build(config.layout, config.cylinders);
  cache.set(key, built);
  return built;
}

function build(wanted: Layout, count: number): Architecture {
  const cylinders = Math.min(Math.max(Math.trunc(count), 1), 16);
  const layout = builtLayout(wanted, cylinders);
  const zeros = () => new Array<number>(cylinders).fill(0);
  const a: Architecture = {
    layout, cylinders, heads: 1, headAngleDeg: 0, rowAngleDeg: 0, rowOffsetMm: 0, sharedPins: false,
    order: [], firingDeg: zeros(), head: zeros(), row: zeros(), position: zeros(), axisDeg: zeros(), even: true,
  };
  if (layout === 'rotary') return { ...a, cylinders: 0, firingDeg: [], head: [], row: [], position: [], axisDeg: [] };
  const found = entryOf(layout === 'cross_plane' && cylinders !== 8 ? 'v' : layout, cylinders);
  const sequential = Array.from({ length: cylinders }, (_, i) => i + 1);
  const inlineOrder = FIRING_ORDERS[cylinders] && cylinders <= 12 ? FIRING_ORDERS[cylinders] : sequential;
  if (layout === 'inline' || (!found && layout !== 'boxer')) {
    a.order = [...inlineOrder];
    if (layout !== 'inline') {
      a.heads = cylinders >= 2 ? 2 : 1;
      a.headAngleDeg = a.heads === 2 ? 90 : 0;
      const front = Math.ceil(cylinders / 2);
      for (let n = 1; n <= cylinders; n++) {
        a.head[n - 1] = n <= front ? 0 : 1;
        a.position[n - 1] = n <= front ? n - 1 : n - 1 - front;
      }
    } else {
      for (let n = 1; n <= cylinders; n++) a.position[n - 1] = n - 1;
    }
  } else if (layout === 'boxer') {
    a.order = [...inlineOrder];
    a.heads = cylinders >= 2 ? 2 : 1;
    a.headAngleDeg = 180;
    const half = Math.floor(cylinders / 2);
    for (let r = 0; r < Math.ceil(cylinders / 2); r++) {
      let left: number;
      let right: number;
      if (cylinders % 2 === 1) {
        left = r + 1;
        right = r + 1 + Math.ceil(cylinders / 2) <= cylinders ? r + 1 + Math.ceil(cylinders / 2) : 0;
      } else if (cylinders === 4 || cylinders === 6) {
        left = r + 1;
        right = cylinders - r;
      } else {
        left = a.order[r];
        right = a.order[r + half];
      }
      a.head[left - 1] = 0;
      a.position[left - 1] = 2 * r;
      if (right > 0) {
        a.head[right - 1] = 1;
        a.position[right - 1] = 2 * r + 1;
      }
    }
  } else if (found) {
    a.order = [...found.order];
    a.headAngleDeg = found.headAngleDeg;
    a.rowAngleDeg = found.rowAngleDeg;
    a.rowOffsetMm = found.rowOffsetMm;
    a.sharedPins = found.sharedPins;
    a.heads = found.right.length === 0 ? 1 : 2;
    found.left.forEach((n, k) => {
      a.head[n - 1] = 0;
      a.position[n - 1] = k;
    });
    found.right.forEach((n, k) => {
      a.head[n - 1] = 1;
      a.position[n - 1] = k;
    });
    if (found.rows) for (let n = 1; n <= cylinders; n++) a.row[n - 1] = found.rows[n - 1] === '1' ? 1 : 0;
  }
  for (let n = 1; n <= cylinders; n++) {
    const headAxis = a.heads === 2 ? (a.head[n - 1] === 0 ? -0.5 : 0.5) * a.headAngleDeg : 0;
    const rowAxis = a.rowAngleDeg > 0 ? (a.row[n - 1] === 0 ? -0.5 : 0.5) * a.rowAngleDeg : 0;
    a.axisDeg[n - 1] = headAxis + rowAxis;
  }
  if (a.sharedPins && a.heads === 2) {
    const pairs = cylinders / 2;
    for (let s = 0; s < cylinders; s++) a.firingDeg[s] = (720 * Math.floor(s / 2)) / pairs + (s % 2 === 1 ? a.headAngleDeg : 0);
    a.even = a.firingDeg.every((deg, s) => Math.abs(deg - (s * 720) / cylinders) < 1e-9);
    // Égaux quand même (V16 à 45°) : le même calcul que tous les autres, au bit près.
    if (a.even) for (let s = 0; s < cylinders; s++) a.firingDeg[s] = (s * 720) / cylinders;
  } else {
    // Le même calcul qu'avant cette table : (s × 720) / N, au bit près.
    for (let s = 0; s < cylinders; s++) a.firingDeg[s] = (s * 720) / cylinders;
  }
  return a;
}

/** L'ordre d'allumage de ce moteur : celui de son architecture, celui d'un VR ou d'un W compris. */
export const firingOrderOf = (config: Pick<EngineConfig, 'layout' | 'cylinders'>): number[] =>
  config.layout === 'rotary' ? firingOrder(config.cylinders) : architectureOf(config).order;

/** L'intervalle après chaque rang de l'ordre (°). */
export function firingIntervals(a: Architecture): number[] {
  return a.firingDeg.map((deg, s) => (s + 1 < a.cylinders ? a.firingDeg[s + 1] : 720) - deg);
}
