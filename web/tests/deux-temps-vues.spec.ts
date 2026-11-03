import { expect, test } from '@playwright/test';
import * as THREE from 'three';
import type { EngineConfig } from '../src/lib/config';
import { collectorPosition, engineLayout, EnginePose, layoutKey, plenumPosition, stackOf, type EngineLayout, type PoseClock, type PoseGas, type PoseTelemetry } from '../src/lib/engineLayout';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { firingOrder } from '../src/lib/motion';
import { crankAngleForTravel, isTwoStroke, pistonTravel, ROD_OVER_STROKE, twoStrokePorts } from '../src/lib/twoStroke';

/* eslint-disable @typescript-eslint/no-explicit-any */

// La vue d'un deux-temps : sans navigateur, sur `engineLayout` et `EnginePose`, que lisent les deux vues (2D et 3D).

const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
  insulation: 'standard', boxFlywheel: 'single',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig>): EngineConfig => ({ ...BASE, ...over });
/** Un deux-temps de moto : 250 cm³ en deux cylindres de 54 × 54,5, ou ce qu'on lui demande. */
const twoStroke = (over: Partial<EngineConfig> = {}): EngineConfig => make({ cycle: 'two_stroke', cylinders: 2, displacementCc: 250, boreMm: 54, redlineRpm: 9500, ...over });

const TWO_STROKES: Partial<EngineConfig>[] = [
  { cylinders: 2 }, { cylinders: 3, displacementCc: 750, boreMm: 70, redlineRpm: 6500 }, { cylinders: 4, displacementCc: 500, redlineRpm: 12000 },
  { cylinders: 2, redlineRpm: 4500, displacementCc: 600, boreMm: 72 }, { cylinders: 2, layout: 'v' }, { cylinders: 4, layout: 'v', displacementCc: 500 },
  { cylinders: 2, layout: 'boxer', displacementCc: 700, boreMm: 76 }, { cylinders: 6, displacementCc: 1500, boreMm: 66 },
  { cylinders: 2, fuelSystem: 'multi' } as Partial<EngineConfig>, { cylinders: 3, airFilter: 'stacks' } as Partial<EngineConfig>,
];

const wrap360 = (deg: number) => ((deg % 360) + 360) % 360;
const clock = (over: Partial<PoseClock> = {}): PoseClock => ({ cycle: 0, turns: 0, turbineAngle: 0, rpm: 3000, load: 0.5, heat: 0.5, reduced: false, burning: true, ...over });
const telemetry = (over: Partial<PoseTelemetry> = {}): PoseTelemetry => ({ cut: 0, cranking: 0, stalled: 0, rpm: 3000, ...over });

/** Fait tourner la pose et note l'ordre des étincelles : voir moteur.spec.ts. */
function sparksOf(layout: EngineLayout, cycles = 2, step = 0.004): number[] {
  const pose = new EnginePose(layout);
  const sparks: number[] = [];
  let count = 0;
  for (let k = 0; k < cycles / step; k++) {
    pose.update(clock({ cycle: (k * step) % 1, turns: k * step }), telemetry(), null, false, 0.016);
    if (pose.sparkCount !== count) {
      count = pose.sparkCount;
      sparks.push(pose.lastSparkCylinder + 1);
    }
  }
  return sparks;
}

test.describe('la mécanique d\'un deux-temps', () => {
  test('ni soupape, ni came, ni culbuteur, ni chaîne : des lumières, un carter fermé', () => {
    for (const over of TWO_STROKES) {
      const config = twoStroke(over);
      expect(isTwoStroke(config)).toBe(true);
      const layout = engineLayout(config);
      expect(layout.cycleDeg).toBe(360);
      expect(layout.valve.perCylinder).toBe(0);
      expect(layout.valve.slots.length).toBe(0);
      expect(layout.valve.rocker).toBeNull();
      expect(layout.valve.sohc).toBe(false);
      expect(layout.valve.coverTop).toBe(layout.valve.headTop);
      expect(layout.sprocket.z).toEqual([]);
      const ported = layout.twoStroke!;
      expect(ported).not.toBeNull();
      // Les hauteurs sont celles de `twoStrokePorts`, rapportées à la course dessinée.
      const ports = twoStrokePorts(config);
      expect(ported.ports).toEqual(ports);
      expect(ported.exhaustTop - ported.exhaustFloor).toBeCloseTo(ports.exhaustHeight * layout.stroke, 9);
      expect(ported.transferTop - ported.transferFloor).toBeCloseTo(ports.transferHeight * layout.stroke, 9);
      expect(ported.inletTop - ported.inletFloor).toBeCloseTo(ports.inletTravel * layout.stroke, 9);
      // L'échappement s'ouvre avant les transferts ; l'admission est sous eux.
      expect(ported.exhaustTop).toBeGreaterThan(ported.transferTop);
      expect(ported.inletTop).toBeLessThan(ported.transferFloor);
      expect(ported.ductBottom).toBeLessThan(ported.inletFloor);
      // Au point mort haut, la jupe recouvre encore toute la lumière d'échappement : le carter reste étanche.
      expect(layout.crank + layout.rod - layout.pistonBottom).toBeLessThan(ported.exhaustFloor);
      // La bielle du gaz et du C++.
      expect(layout.rod / layout.stroke).toBeCloseTo(ROD_OVER_STROKE, 9);
      // Rien d'un quatre-temps.
      const four = engineLayout(make({ ...over, cycle: 'four_stroke', cylinders: config.cylinders, displacementCc: config.displacementCc, boreMm: config.boreMm, redlineRpm: config.redlineRpm }));
      expect(four.twoStroke).toBeNull();
      expect(four.valve.perCylinder).toBeGreaterThan(0);
    }
  });

  test('la clé du dessin suit le cycle, et le régime maximal sur un deux-temps seulement', () => {
    expect(layoutKey(twoStroke())).not.toBe(layoutKey(twoStroke({ cycle: 'four_stroke' })));
    expect(layoutKey(twoStroke())).not.toBe(layoutKey(twoStroke({ redlineRpm: 7000 })));
    expect(layoutKey(make({ redlineRpm: 6000 }))).toBe(layoutKey(make({ redlineRpm: 7000 })));
    // Un rotatif ne devient pas un deux-temps.
    expect(isTwoStroke({ layout: 'rotary', cycle: 'two_stroke' })).toBe(false);
  });

  test('la pose ouvre chaque lumière exactement aux angles de twoStrokePorts, une fois par tour', () => {
    for (const over of TWO_STROKES) {
      const config = twoStroke(over);
      const layout = engineLayout(config);
      const ported = layout.twoStroke!;
      const { exhaustOpen, transferOpen, inletOpenBefore } = ported.ports;
      const pose = new EnginePose(layout);
      // Une grille fine, et les bords à un dix-millième de degré.
      const angles: number[] = [];
      for (let a = 0; a < 360; a += 0.5) angles.push(a);
      for (const edge of [exhaustOpen, 360 - exhaustOpen, transferOpen, 360 - transferOpen, inletOpenBefore, 360 - inletOpenBefore]) angles.push(edge - 1e-4, edge + 1e-4);
      let wrong = 0;
      for (const cylinder of layout.cylinders) {
        const i = cylinder.index;
        for (const theta of angles) {
          for (const turn of [0, 360]) {
            pose.solve(cylinder.firingDeg + theta + turn);
            if (Math.abs(pose.theta[i] - wrap360(theta)) > 1e-6) wrong++;
            if ((pose.liftEx[i] > 0) !== (theta > exhaustOpen && theta < 360 - exhaustOpen)) wrong++;
            if ((pose.liftIn[i] > 0) !== (theta > transferOpen && theta < 360 - transferOpen)) wrong++;
            if ((pose.inlet[i] > 0) !== (theta < inletOpenBefore || theta > 360 - inletOpenBefore)) wrong++;
          }
        }
        // À l'angle d'ouverture, la tête du piston est au haut de la lumière ; au point mort bas, tout est découvert.
        pose.solve(cylinder.firingDeg + exhaustOpen);
        expect(pose.piston[i] + layout.pistonTop).toBeCloseTo(ported.exhaustTop, 6);
        pose.solve(cylinder.firingDeg + transferOpen);
        expect(pose.piston[i] + layout.pistonTop).toBeCloseTo(ported.transferTop, 6);
        pose.solve(cylinder.firingDeg + 360 - inletOpenBefore);
        expect(pose.piston[i] - layout.pistonBottom).toBeCloseTo(ported.inletFloor, 6);
        pose.solve(cylinder.firingDeg + 180);
        expect(pose.liftEx[i]).toBeCloseTo(1, 9);
        expect(pose.liftIn[i]).toBeCloseTo(1, 9);
        expect(pose.inlet[i]).toBe(0);
        pose.solve(cylinder.firingDeg);
        expect(pose.inlet[i]).toBeCloseTo(1, 9);
        expect(pose.liftEx[i]).toBe(0);
        // La course dessinée est celle de `pistonTravel`, à toute position.
        for (let theta = 0; theta < 360; theta += 17) {
          pose.solve(cylinder.firingDeg + theta);
          expect((layout.crank + layout.rod - pose.piston[i]) / layout.stroke).toBeCloseTo(pistonTravel(theta), 9);
        }
      }
      expect(wrong).toBe(0);
      expect(crankAngleForTravel(1 - ported.ports.exhaustHeight)).toBeCloseTo(exhaustOpen, 9);
    }
  });

  test('les deux cylindres d\'un bicylindre deux-temps sont à 180°, et chacun allume à chaque tour', () => {
    for (const layoutName of ['inline', 'v', 'boxer'] as EngineConfig['layout'][]) {
      const layout = engineLayout(twoStroke({ layout: layoutName }));
      const [a, b] = [layout.cylinders[0], layout.cylinders[1]];
      expect(Math.abs(a.firingDeg - b.firingDeg)).toBe(180);
      // Le maneton suit l'inclinaison du cylindre : φ = β − 360·s/N.
      for (const cylinder of layout.cylinders) expect(cylinder.pinDeg).toBeCloseTo(wrap360(cylinder.betaDeg - (360 * cylinder.slot) / 2), 9);
      const pose = new EnginePose(layout);
      pose.solve(a.firingDeg);
      expect(pose.piston[a.index]).toBeCloseTo(layout.crank + layout.rod, 6);
      expect(pose.piston[b.index]).toBeCloseTo(layout.rod - layout.crank, 6);
      // Un tour plus tard, le même état : le cycle dure 360°.
      const before = Array.from(pose.piston);
      pose.solve(a.firingDeg + 360);
      before.forEach((value, i) => expect(pose.piston[i]).toBeCloseTo(value, 6));
    }
    const inline = engineLayout(twoStroke());
    expect(inline.throwAngles).toEqual([0, 180]);
    // Quatre tours de vilebrequin (deux cycles de `Motion`) : quatre étincelles par cylindre, en alternance.
    const sparks = sparksOf(inline);
    expect(sparks.length).toBe(8);
    sparks.forEach((number, k) => expect(number).toBe(sparks[k % 2]));
    expect(new Set(sparks).size).toBe(2);
  });

  test('N cylindres d\'un deux-temps : 360/N d\'écart, dans l\'ordre d\'allumage, une étincelle par tour et par cylindre', () => {
    for (const over of TWO_STROKES) {
      const layout = engineLayout(twoStroke(over));
      const order = firingOrder(layout.count);
      expect(layout.order).toEqual(order);
      order.forEach((number, slot) => expect(layout.cylinders[number - 1].firingDeg).toBeCloseTo((360 * slot) / layout.count, 9));
      const pose = new EnginePose(layout);
      for (const cylinder of layout.cylinders) {
        for (const turn of [0, 360]) {
          pose.solve(cylinder.firingDeg + turn);
          expect(pose.theta[cylinder.index]).toBeCloseTo(0, 6);
          expect(pose.piston[cylinder.index]).toBeCloseTo(layout.crank + layout.rod, 6);
        }
      }
      const sparks = sparksOf(layout);
      expect(sparks.length).toBe(4 * layout.count);
      const start = order.indexOf(sparks[0]);
      sparks.forEach((number, k) => expect(number).toBe(order[(start + k) % order.length]));
    }
  });

  test('pas d\'injecteur dans la chambre : le mélange arrive par le carter', () => {
    const pose = new EnginePose(engineLayout(twoStroke()));
    for (let k = 0; k < 500; k++) pose.update(clock({ cycle: (k * 0.004) % 1, turns: k * 0.004 }), telemetry(), null, false, 0.016);
    expect(pose.sparkCount).toBeGreaterThan(0);
    expect(pose.sprayCount).toBe(0);
  });

  test('la lumière dessinée est celle du gaz, et le gaz brûle à chaque point mort haut dessiné', () => {
    for (const over of [{ cylinders: 2 }, { cylinders: 3, displacementCc: 750, boreMm: 70, redlineRpm: 6500 }] as Partial<EngineConfig>[]) {
      const config = twoStroke(over);
      const layout = engineLayout(config);
      const net = new GasNetwork();
      net.build(config, firingOrder(config.cylinders));
      const inputs: GasInputs = { rpm: 4000, load: 0.6, boost: 0, release: 0, burning: true, heat: 0.3 };
      const pose = new EnginePose(layout);
      const seconds = 2 / (4000 / 60) / 400;
      const turns = (4000 / 120) * seconds;
      net.advance(seconds, 0, turns, inputs);
      let cycle = turns;
      let worstAngle = 0;
      let worstEx = 0;
      let worstIn = 0;
      const burns = new Array(layout.count).fill(0);
      const burning = new Array(layout.count).fill(false);
      let farFromTop = 0;
      for (let step = 0; step < 800; step++) {
        net.advance(seconds, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
        // Le dernier angle du gaz dans ce pas : le gaz compte 720 par tour, la pose 360.
        pose.solve((cycle - turns + 1) * 720);
        for (const cylinder of layout.cylinders) {
          const i = cylinder.index;
          worstAngle = Math.max(worstAngle, Math.abs(((2 * pose.theta[i] - net.cylTheta[i] + 360) % 720 + 720) % 720 - 360));
          worstEx = Math.max(worstEx, Math.abs(pose.liftEx[i] - net.cylLiftEx[i]));
          worstIn = Math.max(worstIn, Math.abs(pose.liftIn[i] - net.cylLiftIn[i]));
          // Une combustion qui commence : la pose doit être près de son point mort haut.
          const now = net.cylFired[i] === 1 && net.cylBurn[i] > 0.002;
          if (now && !burning[i]) {
            burns[i]++;
            if (pose.theta[i] > 60 && pose.theta[i] < 300) farFromTop++;
          }
          burning[i] = now;
        }
      }
      // Un pas de gaz vaut quelques degrés : à ce pas près.
      expect(worstAngle).toBeLessThan(5);
      expect(worstEx).toBeLessThan(0.1);
      expect(worstIn).toBeLessThan(0.1);
      // Deux cycles de `Motion`, quatre tours : quatre combustions par cylindre, et une cinquième déjà en cours au départ, chacune au point mort haut dessiné.
      for (const count of burns) expect(count).toBeGreaterThanOrEqual(4);
      for (const count of burns) expect(count).toBeLessThanOrEqual(5);
      expect(farFromTop).toBe(0);
    }
  });
});

// ── Les deux vues, sans navigateur : une toile qui ne dessine rien, une scène three.js qu'aucun GPU ne rend ──

/** Un chemin et une toile muets : ils acceptent tout. On compte les tracés, et l'on note les noms de pièces que la vue lit. */
class SilentPath {
  moveTo(): void {}
  lineTo(): void {}
  closePath(): void {}
  arc(): void {}
  bezierCurveTo(): void {}
  quadraticCurveTo(): void {}
  rect(): void {}
  ellipse(): void {}
}
function silentCanvas(calls: { count: number }): CanvasRenderingContext2D {
  const state: Record<string, unknown> = {};
  return new Proxy(state, {
    get: (_, key) => (key === 'createPattern' ? () => null : typeof key === 'string' && key in state ? state[key] : () => { calls.count++; }),
    set: (_, key, value) => {
      state[key as string] = value;
      return true;
    },
  }) as unknown as CanvasRenderingContext2D;
}
const anyTheme: any = new Proxy({}, { get: (_, key) => `#${(String(key).length * 2654435761 >>> 8).toString(16).padStart(6, '0').slice(0, 6)}` });
const painter: any = {
  gasStyle: () => '#c86432',
  airStyle: () => '#6496c8',
  gasRgb: (_theme: unknown, _t: number, _y: number, out: number[]) => { out[0] = 200; out[1] = 100; out[2] = 50; },
};
/** Un gaz qui tourne : de quoi faire dessiner les jets aux lumières. */
function liveGas(count: number): PoseGas {
  const fill = (value: number) => new Array(count).fill(value);
  return { cylinders: count, cylT: fill(900), cylM: fill(1), cylY: fill(0.3), cylBurn: fill(0.4), cylFired: fill(1), cylExhaust: fill(40), cylIntake: fill(30), caseInlet: fill(30) };
}
function fakeStage(): any {
  const surface = (parameters: any) => new THREE.MeshStandardMaterial(parameters);
  return {
    THREE,
    scene: new THREE.Scene(),
    software: false,
    surface,
    materials: {
      steel: surface({}), forged: surface({}), casting: surface({}), alloy: surface({}), rubber: surface({}), brass: surface({}), accent: surface({}),
      ghost: surface({ transparent: true, opacity: 0.2 }), light: new THREE.MeshBasicMaterial(),
    },
  };
}
const fakeMotion: any = { clutchTorque: 0, painter, gas: { net: { n: 0, cylinders: 0, throttleShare: 0 } }, load: 0.5, turbineAngle: 0, chargerAngle: 0, heat: 0.4 };

test.describe('les vues d\'un deux-temps', () => {
  test('l\'écorché 2D se dessine sans came ni chaîne, et nomme ses lumières et son carter', async () => {
    const saved = { path: (globalThis as any).Path2D, document: (globalThis as any).document };
    (globalThis as any).Path2D = SilentPath;
    (globalThis as any).document = { createElement: () => ({ getContext: () => null }) };
    try {
      const { Engine2D } = await import('../src/lib/engine2d');
      for (const over of TWO_STROKES) {
        const layout = engineLayout(twoStroke(over));
        const view = new Engine2D(layout);
        const read = new Set<string>();
        const labels: any = new Proxy({ parts: true }, { get: (target: any, key) => (key === 'parts' ? target.parts : (read.add(String(key)), String(key))) });
        const pose = new EnginePose(layout);
        const calls = { count: 0 };
        for (let k = 0; k < 40; k++) {
          pose.update(clock({ cycle: (k * 0.029) % 1, turns: k * 0.029 }), telemetry(), liveGas(layout.count), true, 0.004);
          view.draw(silentCanvas(calls), k % 2 === 0 ? 800 : 400, k % 2 === 0 ? 430 : 560, pose, painter, anyTheme, labels);
        }
        expect(calls.count).toBeGreaterThan(1000);
        for (const part of ['exhaustPort', 'inletPort', 'transferPort', 'transferDuct', 'crankcase']) expect(read.has(part), `${part} ${JSON.stringify(over)}`).toBe(true);
        for (const part of ['cam', 'chain', 'pan']) expect(read.has(part), `${part} ${JSON.stringify(over)}`).toBe(false);
      }
    } finally {
      (globalThis as any).Path2D = saved.path;
      (globalThis as any).document = saved.document;
    }
  });

  test('la scène 3D n\'a ni soupape, ni came, ni chaîne ; ce qu\'elle montre de chaque lumière suit le piston', async () => {
    const { EngineScene } = await import('../src/lib/engineScene');
    for (const over of TWO_STROKES) {
      const layout = engineLayout(twoStroke(over));
      const ported = layout.twoStroke!;
      const stage = fakeStage();
      const scene = new EngineScene(stage, layout) as any;
      for (const part of ['valvesIn', 'valvesEx', 'springs', 'retainers', 'buckets', 'rockers', 'chain']) expect(scene.d[part], part).toBeNull();
      expect(scene.cams.length).toBe(0);
      expect(scene.chains.length).toBe(0);
      expect(scene.d.transfers.count).toBe(layout.count);
      const keys = scene.labels.map((label: { key: string }) => label.key);
      for (const key of ['engine.part.exhaustPort', 'engine.part.transferPort', 'engine.part.inletPort', 'engine.part.crankcase', 'engine.part.transferDuct', 'engine.part.plug']) expect(keys).toContain(key);
      for (const key of ['engine.part.cam', 'engine.part.valve', 'engine.part.chain', 'engine.part.coil', 'engine.part.pan']) expect(keys).not.toContain(key);
      const pose = new EnginePose(layout);
      let wrong = 0;
      for (let deg = 0; deg < 360; deg += 7) {
        pose.solve(deg);
        scene.update(pose, fakeMotion, anyTheme);
        let expected = 0;
        for (const cylinder of layout.cylinders) expected += (pose.liftEx[cylinder.index] > 0 ? 1 : 0) + (pose.liftIn[cylinder.index] > 0 ? 2 : 0) + (pose.inlet[cylinder.index] > 0 ? 1 : 0);
        if (scene.d.windows.count !== expected) wrong++;
      }
      expect(wrong).toBe(0);
      // Au point mort bas du premier cylindre, sa lumière d'échappement est découverte sur toute sa hauteur (les matrices sont en float32).
      const first = layout.cylinders[0];
      pose.solve(first.firingDeg + 180);
      scene.update(pose, fakeMotion, anyTheme);
      const m = new THREE.Matrix4();
      scene.d.windows.getMatrixAt(0, m);
      const e = m.elements;
      expect(Math.hypot(e[4], e[5], e[6])).toBeCloseTo(ported.exhaustTop - ported.exhaustFloor, 3);
      scene.dispose();
    }
  });
});

// ── Contre-épreuve : un quatre-temps dessiné exactement comme avant le deux-temps ──

/** Une empreinte FNV-1a sur 32 bits : assez pour voir qu'un dessin a changé. */
function fnv(text: string): string {
  let h = 0x811c9dc5;
  for (let k = 0; k < text.length; k++) {
    h ^= text.charCodeAt(k);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h.toString(16).padStart(8, '0');
}

const round = (_: string, v: unknown) => (typeof v === 'number' ? Math.round(v * 1e6) / 1e6 : v);

/** Tout ce que les vues lisent d'un quatre-temps : la disposition (sans sa clé ni ce que le deux-temps y a ajouté), la pose sur deux tours, les étincelles. */
function fourStrokePrint(config: EngineConfig): string {
  const layout = engineLayout(config);
  const { key: _key, cycleDeg: _cycle, twoStroke: _ported, ...shape } = layout as any;
  const parts: string[] = [JSON.stringify(shape, round)];
  parts.push(JSON.stringify([plenumPosition(layout), collectorPosition(layout, 0), layout.cylinders.map((c) => stackOf(layout, c))], round));
  const pose = new EnginePose(layout);
  for (let deg = 0; deg < 720; deg += 37) {
    pose.solve(deg);
    parts.push(JSON.stringify([pose.theta, pose.piston, pose.pinX, pose.pinY, pose.rodAngle, pose.liftIn, pose.liftEx, pose.rockerIn, pose.rockerEx].map((a) => Array.from(a)), round));
  }
  const fired = new EnginePose(layout);
  const sparks: number[] = [];
  let count = 0;
  for (let k = 0; k < 500; k++) {
    fired.update(clock({ cycle: (k * 0.004) % 1, turns: k * 0.004 }), telemetry(), null, false, 0.016);
    if (fired.sparkCount !== count) {
      count = fired.sparkCount;
      sparks.push(fired.lastSparkCylinder);
    }
  }
  parts.push(JSON.stringify([sparks, fired.sprayCount]));
  return fnv(parts.join('\n'));
}

const FOUR_STROKES: Partial<EngineConfig>[] = [
  { cylinders: 2 }, { cylinders: 3 }, { cylinders: 4 }, { cylinders: 5 }, { cylinders: 6 },
  { cylinders: 2, layout: 'boxer' }, { cylinders: 4, layout: 'boxer' }, { cylinders: 6, layout: 'boxer' },
  { cylinders: 2, layout: 'v' }, { cylinders: 4, layout: 'v' }, { cylinders: 6, layout: 'v' }, { cylinders: 8, layout: 'v' }, { cylinders: 8, layout: 'cross_plane' },
  { cylinders: 10, layout: 'v' }, { cylinders: 12, layout: 'v' },
  { cylinders: 4, camshaft: 'sohc' } as Partial<EngineConfig>, { cylinders: 6, layout: 'v', camshaft: 'ohv' } as Partial<EngineConfig>,
  { cylinders: 4, fuel: 'diesel' }, { cylinders: 4, fuelSystem: 'multi' } as Partial<EngineConfig>, { cylinders: 8, layout: 'v', fuelSystem: 'single' } as Partial<EngineConfig>,
  { cylinders: 4, airFilter: 'stacks' } as Partial<EngineConfig>, { cylinders: 6, aspiration: 'turbo' },
];

/** Relevées sur le code d'avant le deux-temps (commit 1d4ba19) : un écart ici change le dessin d'un quatre-temps. */
const FOUR_STROKE_PRINTS = ['74686760', 'a852685b', 'fe73a739', '25572101', 'cc5b48d0', '03bc8e06', '7da99428', '47606131', '28559c3a', 'a3fabebd', '0ccef4cd', 'ed2e79d0', '98596323', '4aeb334b', 'e2eda14d', '5ef0cc03', '5a733310', '1ffcfb93', 'b250ea9c', 'b69808d8', 'b29358e0', '0243cdcd'];

test.describe('un quatre-temps ne change pas', () => {
  test('même disposition, même pose, mêmes étincelles qu\'avant, avec ou sans le champ `cycle`', () => {
    expect(FOUR_STROKES.map((over) => fourStrokePrint(make(over)))).toEqual(FOUR_STROKE_PRINTS);
    expect(FOUR_STROKES.map((over) => fourStrokePrint(make({ ...over, cycle: 'four_stroke' })))).toEqual(FOUR_STROKE_PRINTS);
    for (const over of FOUR_STROKES) {
      const layout = engineLayout(make(over));
      expect(layout.twoStroke).toBeNull();
      expect(layout.cycleDeg).toBe(720);
    }
  });
});
