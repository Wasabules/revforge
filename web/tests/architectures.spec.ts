import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import { ouvrir } from './aide';
import { architectureOf, builtLayout, ENTRIES, firingIntervals, firingOrderOf } from '../src/lib/architecture';
import type { EngineConfig } from '../src/lib/config';
import { DEG, engineLayout, EnginePose } from '../src/lib/engineLayout';
import { GasFlow } from '../src/lib/fluid';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { Motion } from '../src/lib/motion';

/* eslint-disable @typescript-eslint/no-explicit-any */

const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
  insulation: 'standard', boxFlywheel: 'single',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig>): EngineConfig => ({ ...BASE, ...over });
const NEW: Partial<EngineConfig>[] = [
  { cylinders: 5, layout: 'vr', displacementCc: 2324, boreMm: 81 }, { cylinders: 6, layout: 'vr', displacementCc: 2792, boreMm: 81 },
  { cylinders: 8, layout: 'w', displacementCc: 3999, boreMm: 84 }, { cylinders: 12, layout: 'w', displacementCc: 5998, boreMm: 84 },
  { cylinders: 16, layout: 'w', displacementCc: 7993, boreMm: 86 }, { cylinders: 14, layout: 'v', displacementCc: 7000, boreMm: 86 },
  { cylinders: 16, layout: 'v', displacementCc: 7413, boreMm: 76 },
];
const wrap360 = (deg: number) => ((deg % 360) + 360) % 360;
const RUNNING: GasInputs = { rpm: 4000, load: 1, boost: 0, release: 0, burning: true, heat: 0.6 };
const FRAME = 1 / 60 / 12;

test.describe('architectures, sans navigateur', () => {
  test('la table du site est celle du C++, ligne à ligne', () => {
    const source = readFileSync(new URL('../../core/src/architecture.cpp', import.meta.url), 'utf-8');
    const LAYOUT: Record<string, string> = { V: 'v', CrossPlane: 'cross_plane', Vr: 'vr', W: 'w' };
    const list = (text: string) => (text.trim() ? text.split(',').map((item) => Number(item.trim())) : []);
    const rows = [...source.matchAll(/\{Layout::(\w+), (\d+), ([\d.]+), ([\d.]+), ([\d.]+), (true|false), \{([^}]*)\}, \{([^}]*)\}, \{([^}]*)\}, "([01]*)"\}/g)].map((m) => ({
      layout: LAYOUT[m[1]], cylinders: Number(m[2]), headAngleDeg: Number(m[3]), rowAngleDeg: Number(m[4]), rowOffsetMm: Number(m[5]), sharedPins: m[6] === 'true',
      order: list(m[7]), left: list(m[8]), right: list(m[9]), rows: m[10],
    }));
    expect(rows.length).toBe(ENTRIES.length);
    expect(rows).toEqual(ENTRIES.map((entry) => ({ ...entry })));
  });

  test('chaque cylindre passe au point mort haut à son angle d\'allumage, désaxage compris', () => {
    for (const over of NEW) {
      const layout = engineLayout(make(over));
      const pose = new EnginePose(layout);
      expect(layout.order).toEqual(firingOrderOf(make(over)));
      for (const cylinder of layout.cylinders) {
        // Au point mort haut, le piston est au plus loin du vilebrequin : la dérivée de sa course s'annule.
        const at = cylinder.firingDeg;
        pose.solve(at - 0.5);
        const before = pose.piston[cylinder.index];
        pose.solve(at + 0.5);
        const after = pose.piston[cylinder.index];
        pose.solve(at);
        expect(Math.abs(after - before)).toBeLessThan(1e-3 * layout.crank);
        expect(pose.piston[cylinder.index]).toBeGreaterThan(Math.max(before, after) - 1e-9);
        // La bielle garde sa longueur, et l'axe de piston reste sur l'axe désaxé.
        const i = cylinder.index;
        expect(Math.hypot(pose.wristX[i] - pose.pinX[i], pose.wristY[i] - pose.pinY[i])).toBeCloseTo(layout.rod, 6);
        const b = cylinder.betaDeg * DEG;
        expect(pose.wristX[i] * Math.cos(b) - pose.wristY[i] * Math.sin(b)).toBeCloseTo(cylinder.offset, 6);
      }
    }
  });

  test('les manetons d\'un VR6 s\'écartent de 22°, ceux d\'un W12 de 12°, d\'un W8 de 18° ; le W16 et le V16 les partagent', () => {
    // VR6 : les paires d'un six en ligne (1/6, 2/5, 3/4) s'écartent de l'angle entre les points morts hauts des deux rangées,
    // 15° plus deux fois asin(12,5 / (l + r)) : 21,85° avec la bielle de 164 mm, un peu plus avec celle de 1,6 course du dessin.
    const vr6 = engineLayout(make(NEW[1]));
    const expected = 15 + (2 * Math.asin(12.5 / (vr6.rod + vr6.crank))) / DEG;
    for (const [a, b] of [[1, 6], [2, 5], [3, 4]]) {
      const gap = Math.abs(((vr6.cylinders[a - 1].pinDeg - vr6.cylinders[b - 1].pinDeg + 540) % 360) - 180);
      expect(gap).toBeCloseTo(expected, 6);
    }
    expect(expected).toBeGreaterThan(21);
    expect(expected).toBeLessThan(23);
    // Les autres : l'écart des deux manetons d'une même manivelle (SSP 248 : « splitpin » +12° et −18°).
    expect(engineLayout(make(NEW[3])).splitDeg).toBe(12);
    expect(engineLayout(make(NEW[2])).splitDeg).toBe(18);
    expect(engineLayout(make(NEW[4])).splitDeg).toBe(0);
    expect(engineLayout(make(NEW[6])).splitDeg).toBe(0);
    expect(engineLayout(make(NEW[5])).splitDeg).toBe(0);
    // Un W : deux bielles par manivelle ; un VR, une seule, sur sept paliers pour six cylindres.
    expect(engineLayout(make(NEW[3])).throws.length).toBe(6);
    expect(vr6.throws.length).toBe(6);
    expect(vr6.mains.length).toBe(7);
    expect(vr6.banks).toBe(1);
  });

  test('les intervalles : égaux partout, sauf le V14 à 45° ; chaque culasse du W16 et du W8 reçoit ses bouffées inégalement', () => {
    expect(new Set(firingIntervals(architectureOf({ layout: 'v', cylinders: 14 })).map((v) => v.toFixed(2)))).toEqual(new Set(['45.00', '57.86']));
    for (const [layout, cylinders, gap] of [['vr', 6, 120], ['w', 12, 60], ['w', 16, 45], ['v', 16, 45], ['w', 8, 90]] as const) {
      const a = architectureOf({ layout, cylinders });
      expect(a.even).toBe(true);
      expect(firingIntervals(a).every((v) => Math.abs(v - gap) < 1e-9)).toBe(true);
    }
    const perHead = (layout: 'w' | 'v', cylinders: number) => {
      const a = architectureOf({ layout, cylinders });
      const angles = a.order.map((n, s) => ({ head: a.head[n - 1], deg: a.firingDeg[s] })).filter((item) => item.head === 0).map((item) => item.deg);
      return new Set(angles.map((deg, k) => ((k + 1 < angles.length ? angles[k + 1] : angles[0] + 720) - deg).toFixed(0)));
    };
    expect(perHead('w', 12)).toEqual(new Set(['120']));
    expect(perHead('w', 8)).toEqual(new Set(['180', '270', '90']));
    expect(perHead('w', 16)).toEqual(new Set(['135', '45', '90']));
    expect(perHead('v', 16)).toEqual(new Set(['90']));
  });

  test('un VR ou un W qui n\'existe pas se construit en V, et le choix des cylindres reste cohérent', () => {
    expect(builtLayout('vr', 8)).toBe('v');
    expect(builtLayout('w', 10)).toBe('v');
    expect(builtLayout('inline', 16)).toBe('v');
    expect(builtLayout('w', 16)).toBe('w');
  });

  test('le gaz : chaque cylindre souffle dans le collecteur de sa culasse, et 16 cylindres tiennent dans une image', () => {
    // Le V12 d'abord : il chauffe le compilateur, et sert de référence.
    for (const over of [{ cylinders: 12, layout: 'v', displacementCc: 6500, boreMm: 90 }, NEW[6], NEW[4]] as Partial<EngineConfig>[]) {
      const config = make({ ...over, exhaust: 'sport', body: 'supercar', redlineRpm: 8500 });
      const net = new GasNetwork();
      net.build(config, firingOrderOf(config));
      const a = architectureOf(config);
      for (let i = 0; i < config.cylinders; i++) expect(net.bank[i]).toBe(a.head[i]);
      const gas = new GasFlow();
      gas.configure(config, firingOrderOf(config), true, 0.6);
      const inputs = { ...RUNNING, rpm: 6000 };
      let cycle = 0;
      for (let f = 0; f < 600; f++) {
        const turns = (inputs.rpm / 120) * FRAME;
        gas.wantChambers = true;
        gas.advance(FRAME, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
      }
      console.log(`gaz, ${config.cylinders} cylindres ${config.layout} : ${gas.costMs.toFixed(2)} ms par image`);
      // Ce qu'on vise : 4 ms ; la machine des tests peut être chargée.
      expect(gas.costMs).toBeLessThan(12);
    }
  });

  test('un VR a ses collecteurs dans une seule culasse, la rangée loin de l\'échappement un peu plus longue', () => {
    const config = make({ ...NEW[1], exhaust: 'sport' });
    const net = new GasNetwork();
    net.build(config, firingOrderOf(config));
    expect(net.banks).toBe(1);
    const lengths = Array.from({ length: 6 }, (_, i) => net.runnerCells[i]);
    // Rangée 0 (impairs) loin de l'échappement : son conduit traverse la culasse.
    expect(Math.min(lengths[0], lengths[2], lengths[4])).toBeGreaterThan(Math.max(lengths[1], lengths[3], lengths[5]));
  });

  test('les bouffées d\'un V14 partent à ses vrais intervalles, chacune dans la ligne de sa culasse', () => {
    const motion = new Motion();
    motion.reduced = false;
    motion.slow = 1;
    const config = make({ ...NEW[5], exhaust: 'sport' });
    const t = { rpm: 1200, load: 0.5, cut: 0, cranking: 0 } as any;
    const filled = new Proxy(t, { get: (target, key) => (key in target ? target[key] : 0) });
    // Un cycle exactement, en petits pas : 14 bouffées, 7 par culasse.
    let now = 0;
    motion.update(now, filled, config, false);
    motion.rpm = 1200;
    const pulses: { bank: number }[] = [];
    for (let k = 0; k < 600; k++) {
      now += 1000 / 600 / (1200 / 120);
      const before = motion.pulses.length;
      motion.update(now, filled, config, false);
      for (let p = before; p < motion.pulses.length; p++) pulses.push(motion.pulses[p]);
    }
    expect(pulses.length).toBeGreaterThanOrEqual(13);
    expect(pulses.length).toBeLessThanOrEqual(15);
    const left = pulses.filter((p) => p.bank === 0).length;
    expect(Math.abs(left - pulses.length / 2)).toBeLessThanOrEqual(1);
  });
});

test.describe('architectures, dans la page', () => {
  test('choisir un W16 : la vue suit, l\'ordre réel s\'affiche, et le lien de partage le garde', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    await page.goto('./?test');
    await ouvrir(page, 'engine');
    const block = page.getByTestId('section-block');
    await block.getByRole('button', { name: 'W', exact: true }).click();
    await expect(page.getByTestId('layout-help')).toBeVisible();
    await block.getByRole('button', { name: '16', exact: true }).click();
    await page.waitForFunction(() => /^#?16\.w\./.test(decodeURIComponent(location.hash).replace(/^#/, '')) || /16\.w\./.test(location.hash));
    expect(errors).toEqual([]);
  });

  test('passer à 14 cylindres sur un moteur en ligne en fait un V, et un VR à 8 cylindres aussi', async ({ page }) => {
    await page.goto('./?test');
    await ouvrir(page, 'engine');
    const block = page.getByTestId('section-block');
    await block.getByRole('button', { name: '14', exact: true }).click();
    await page.waitForFunction(() => /14\.v\./.test(location.hash));
    await block.getByRole('button', { name: 'VR', exact: true }).click();
    // Un VR à 14 n'existe pas : le plus proche, 6.
    await page.waitForFunction(() => /6\.vr\./.test(location.hash));
  });
});

// Quelques chiffres d'angle, pour mémoire dans le rapport : pas de contrôle de plus.
test('les angles des manetons du VR6, en clair', () => {
  const vr6 = engineLayout(make(NEW[1]));
  console.log('VR6, manetons :', vr6.cylinders.map((c) => `${c.number}:${wrap360(c.pinDeg).toFixed(1)}`).join(' '));
});
