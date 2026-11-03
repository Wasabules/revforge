import type { Exhaust } from './config';

/**
 * Ce qui compose chaque ligne, pour la dessiner : longueur (m, à l'échelle d'une compacte), section par rapport
 * au tube d'entrée, et nature. Les mêmes valeurs que dans core/src/exhaust_line.cpp, où elles font le son.
 */
export type LinePart = 'pipe' | 'cat' | 'resonator' | 'muffler';
export const EXHAUST_LINES: Record<Exhaust, { length: number; area: number; part: LinePart }[]> = {
  stock: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.26, area: 4, part: 'cat' },
    { length: 0.9, area: 1, part: 'pipe' },
    { length: 0.34, area: 5, part: 'resonator' },
    { length: 0.85, area: 1, part: 'pipe' },
    { length: 0.46, area: 11, part: 'muffler' },
    { length: 0.45, area: 1, part: 'pipe' },
  ],
  sport: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.2, area: 3, part: 'cat' },
    { length: 1.25, area: 1.2, part: 'pipe' },
    { length: 0.36, area: 6, part: 'muffler' },
    { length: 0.35, area: 1.2, part: 'pipe' },
  ],
  straight: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 1.6, area: 1.3, part: 'pipe' },
  ],
  no_muffler: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.26, area: 4, part: 'cat' },
    { length: 0.9, area: 1, part: 'pipe' },
    { length: 0.34, area: 5, part: 'resonator' },
    { length: 1.3, area: 1, part: 'pipe' },
  ],
  no_resonator: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.26, area: 4, part: 'cat' },
    { length: 2.09, area: 1, part: 'pipe' },
    { length: 0.46, area: 11, part: 'muffler' },
    { length: 0.45, area: 1, part: 'pipe' },
  ],
  quiet: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.28, area: 4, part: 'cat' },
    { length: 0.6, area: 1, part: 'pipe' },
    { length: 0.34, area: 6, part: 'resonator' },
    { length: 0.6, area: 1, part: 'pipe' },
    { length: 0.4, area: 9, part: 'muffler' },
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 0.5, area: 13, part: 'muffler' },
    { length: 0.5, area: 1, part: 'pipe' },
  ],
  // Socle : la chambre de détente d'un deux-temps se dessine et se calcule comme la ligne directe, comme dans le son.
  expansion: [
    { length: 0.4, area: 1, part: 'pipe' },
    { length: 1.6, area: 1.3, part: 'pipe' },
  ],
};

/** Le diamètre des roues, rapporté au turbo moyen : recopie de `kSizeSmall` et `kSizeLarge` en C++. Il dimensionne le gaz et les dessins. */
export const TURBO_SCALE: Record<'small' | 'medium' | 'large', number> = { small: 0.85, medium: 1, large: 1.2 };
