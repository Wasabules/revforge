import { expect, test } from '@playwright/test';
import type { EngineConfig } from '../src/lib/config';
import { engineLayout, layoutKey } from '../src/lib/engineLayout';
import { rotaryKey, rotaryLayout } from '../src/lib/rotary';
import { turboChain, type TurboChain } from '../src/lib/turboChain';

/* Les vues du moteur à deux turbos, sans navigateur : ce que `turboChain` place, et ce qui ne bouge pas au défaut. */
const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', turboPipeMm: 400, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const SIX = { cylinders: 6, displacementCc: 3000, boreMm: 86 } as Partial<EngineConfig>;
const V6 = { cylinders: 6, layout: 'v', displacementCc: 3000, boreMm: 86 } as Partial<EngineConfig>;

const chainOf = (change: Partial<EngineConfig>) => turboChain(engineLayout({ ...BASE, ...change } as EngineConfig));

/**
 * L'empreinte des champs qu'avait `turboChain` avant les biturbos, arrondis au millième de millimètre : un turbo simple doit
 * les rendre exactement. Les valeurs attendues ont été relevées sur le code d'avant le biturbo (FNV-1a du JSON).
 */
const KEYS = ['units', 'bank', 'side', 'x', 'y', 'z', 'compressorZ', 'turbineHousing', 'compressorHousing', 'turbineWheel', 'compressorWheel', 'airbox', 'airboxSize', 'hotFrom', 'hotTo',
  'actuator', 'feedRun', 'hotRun', 'sideZ', 'sideCompressorZ', 'cooler', 'coolerSize', 'coldFrom', 'coldFromDir', 'coldTo', 'xMax', 'yMin', 'yMax', 'zMax', 'sideZMin', 'extraMm', 'sideFolded'];
function roundDeep(value: unknown): unknown {
  if (typeof value === 'number') return Math.round(value * 1000) / 1000;
  if (Array.isArray(value)) return value.map(roundDeep);
  if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, roundDeep(item)]));
  return value;
}
function fingerprint(chain: TurboChain): string {
  const text = JSON.stringify(roundDeep(chain), KEYS);
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h.toString(16).padStart(8, '0');
}
const BEFORE: { name: string; change: Partial<EngineConfig>; print: string }[] = [
  { name: '4 en ligne', change: {}, print: '5cfc6c4b' },
  { name: '6 en ligne', change: SIX, print: '68961a19' },
  { name: 'V6', change: V6, print: '58648494' },
  { name: 'V8', change: { cylinders: 8, layout: 'cross_plane', displacementCc: 4800, boreMm: 96 }, print: '149c6362' },
  { name: 'boxer', change: { layout: 'boxer', displacementCc: 2500, boreMm: 99 }, print: '1edd052e' },
  { name: 'tube de 2,5 m', change: { turboPipeMm: 2500 }, print: '3a665c0a' },
];

test.describe('les vues du biturbo, sans navigateur', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('au défaut, un turbo simple est placé exactement comme avant', () => {
    for (const { name, change, print } of BEFORE) {
      // Sans les champs du biturbo, comme un ancien réglage, et avec leurs valeurs par défaut : même chose.
      expect(fingerprint(chainOf(change)), name).toBe(print);
      expect(fingerprint(chainOf({ ...change, turboLayout: 'single', variableGeometry: false })), name).toBe(print);
      const chain = chainOf(change);
      expect(chain.split, name).toBe(false);
      expect(chain.vanes, name).toBe(false);
      expect(chain.charged, name).toBe(false);
      expect(chain.units.every((unit) => unit.shaft === 0), name).toBe(true);
    }
    // La clé des pièces ne change pas non plus au défaut : la vue ne se reconstruit pas pour rien.
    expect(layoutKey({ ...BASE, turboLayout: 'single', variableGeometry: false } as EngineConfig)).toBe(layoutKey(BASE));
  });

  test('un six en ligne à deux turbos en a deux, un par moitié de cylindres, chacun sur son arbre', () => {
    for (const turboLayout of ['twin_parallel', 'twin_sequential'] as const) {
      const chain = chainOf({ ...SIX, turboLayout });
      expect(chain.units, turboLayout).toHaveLength(2);
      expect(chain.split).toBe(true);
      expect(chain.units.map((unit) => unit.shaft)).toEqual([0, 1]);
      const [rear, front] = chain.units;
      // L'arrière garde la place du turbo simple ; l'avant nourrit les trois premiers cylindres, sans chevaucher l'autre collecteur.
      expect(rear.z).toBe(chainOf(SIX).units[0].z);
      expect(front.z).toBeGreaterThan(rear.z + 2 * 86);
      expect(front.collectorRear).toBeGreaterThan(rear.collectorFront);
      // De face, l'un ne cache pas l'autre.
      expect(Math.hypot(front.x - rear.x, front.y - rear.y)).toBeGreaterThan(rear.turbineHousing * 0.9);
    }
    // Un seul turbo, une seule unité ; et la clé change, pour que la vue se reconstruise.
    expect(chainOf({ ...SIX, turboLayout: 'single' }).units).toHaveLength(1);
    expect(layoutKey({ ...BASE, ...SIX, turboLayout: 'twin_parallel' } as EngineConfig)).not.toBe(layoutKey({ ...BASE, ...SIX } as EngineConfig));
  });

  test('un V à deux turbos garde ses deux turbos, le second sur le second arbre', () => {
    const single = chainOf(V6);
    const twin = chainOf({ ...V6, turboLayout: 'twin_parallel' });
    expect(fingerprint(twin)).toBe(fingerprint(single));
    expect(twin.units.map((unit) => unit.shaft)).toEqual([0, 1]);
    expect(single.units.map((unit) => unit.shaft)).toEqual([0, 0]);
  });

  test("le twincharger dessine aussi le compresseur, et les aubes remplacent la soupape", () => {
    const charged = engineLayout({ ...BASE, turboLayout: 'twincharger', superchargerKind: 'roots' } as EngineConfig);
    expect(charged.charger).toBe('roots');
    expect(charged.twincharger).toBe(true);
    expect(turboChain(charged).charged).toBe(true);
    expect(turboChain(charged).units).toHaveLength(1);
    // Le tube déporté n'existe pas dans le modèle à deux turbos : le dessin reste à la référence.
    expect(engineLayout({ ...BASE, turboLayout: 'twincharger', turboPipeMm: 2500 } as EngineConfig).turboPipeMm).toBe(400);
    const vanes = chainOf({ variableGeometry: true });
    expect(vanes.vanes).toBe(true);
    expect(layoutKey({ ...BASE, variableGeometry: true } as EngineConfig)).not.toBe(layoutKey(BASE));
  });

  test('le 13B-REW séquentiel a deux turbos, et sa vue se reconstruit', () => {
    const rew = { ...BASE, layout: 'rotary', cylinders: 2, displacementCc: 1308, redlineRpm: 8000, boostBar: 0.7 } as EngineConfig;
    expect(rotaryLayout(rew).turbos).toBe(1);
    expect(rotaryLayout({ ...rew, turboLayout: 'twin_sequential' }).turbos).toBe(2);
    expect(rotaryKey({ ...rew, turboLayout: 'single' })).toBe(rotaryKey(rew));
    expect(rotaryKey({ ...rew, turboLayout: 'twin_sequential' })).not.toBe(rotaryKey(rew));
  });
});
