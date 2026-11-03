import { expect, test } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { engineLayout } from '../src/lib/engineLayout';
import { GasFlow } from '../src/lib/fluid';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { turboChain, type P3T } from '../src/lib/turboChain';

/* eslint-disable @typescript-eslint/no-explicit-any */
const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', turboPipeMm: 400, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 5500, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.4, compressorOutK: 413, wastegate: 0.5 };
const TURBO4 = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';

function drive(net: GasNetwork, seconds: number, each?: () => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (WIDE_OPEN.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, WIDE_OPEN);
    cycle = (cycle + turns) % 1;
    each?.();
  }
}

function runLength(run: P3T[]): number {
  let total = 0;
  for (let i = 1; i < run.length; i++) total += Math.hypot(run[i][0] - run[i - 1][0], run[i][1] - run[i - 1][1], run[i][2] - run[i - 1][2]);
  return total;
}

const ENGINES: { name: string; config: Partial<EngineConfig> }[] = [
  { name: '4 en ligne', config: {} },
  { name: '6 en ligne', config: { cylinders: 6, displacementCc: 3000, boreMm: 86 } },
  { name: 'V6', config: { cylinders: 6, layout: 'v', displacementCc: 3000, boreMm: 86 } },
  { name: 'V8', config: { cylinders: 8, layout: 'cross_plane', displacementCc: 4800, boreMm: 96 } },
  { name: 'boxer', config: { layout: 'boxer', displacementCc: 2500, boreMm: 99 } },
  { name: 'gros turbo', config: { turboSize: 'large' } },
];

test.describe('le tube du turbo, sans navigateur', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('à la référence, le réseau du gaz et la place du turbo ne changent pas', () => {
    const with400 = new GasNetwork();
    with400.build(BASE, ORDER4);
    const without = new GasNetwork();
    const { turboPipeMm: _unused, ...old } = BASE;
    without.build(old as EngineConfig, ORDER4);
    expect(with400.n).toBe(without.n);
    expect(with400.pipeCells).toBe(0);
    expect(with400.zoneTurbine).toBe(with400.zoneJoin);
    const unit = turboChain(engineLayout(BASE)).units[0];
    expect(unit.feedRun).toEqual([]);
    expect(unit.hotRun).toEqual([]);
    expect(unit.sideZ).toBe(unit.z);
  });

  test('déporté, le gaz parcourt le tube, s\'y refroidit, et l\'air revient d\'autant', () => {
    const near = new GasNetwork();
    near.build(BASE, ORDER4);
    const far = new GasNetwork();
    far.build({ ...BASE, turboPipeMm: 2400 }, ORDER4);
    expect(far.pipeCells * far.cell).toBeCloseTo(2, 1);
    expect(far.zoneTurbine).not.toBe(far.zoneJoin);
    // Le même volume en plus qu'en C++ : la section de l'admission, sur la longueur ajoutée.
    const hot = (net: GasNetwork) => net.intakeParts.find((part) => part.kind === 'hot')!.count * net.cell;
    expect(hot(far) - hot(near)).toBeCloseTo(2, 1);
    drive(far, 0.6);
    let before = 0;
    let after = 0;
    let n = 0;
    drive(far, 0.1, () => {
      before += far.zT[far.zoneJoin];
      after += far.zT[far.zoneTurbine];
      n++;
    });
    expect(after / n).toBeLessThan(before / n);
    expect(far.turbinePower).toBeGreaterThan(0);
    for (let i = 0; i < far.n; i++) expect(Number.isFinite(far.p[i])).toBe(true);
  });

  test('l\'onde met le temps du tube à arriver à la turbine', () => {
    const swingAtTurbine = (mm: number) => {
      const net = new GasNetwork();
      net.build({ ...BASE, turboPipeMm: mm }, ORDER4);
      drive(net, 0.5);
      let low = Infinity;
      let high = -Infinity;
      drive(net, 0.06, () => {
        low = Math.min(low, net.zP[net.zoneTurbine]);
        high = Math.max(high, net.zP[net.zoneTurbine]);
      });
      return high - low;
    };
    // Les bouffées s'effacent le long du tube : c'est ce qu'en dit aussi le C++.
    expect(swingAtTurbine(3000)).toBeLessThan(swingAtTurbine(400) * 0.8);
  });

  test('plus court, le volume avant la turbine rétrécit', () => {
    const volume = (mm: number) => {
      const net = new GasNetwork();
      net.build({ ...BASE, turboPipeMm: mm }, ORDER4);
      return net.zV[net.zoneTurbine];
    };
    expect(volume(150)).toBeLessThan(volume(400));
  });

  test('trois mètres de tube n\'allouent rien, et tiennent dans une image', () => {
    // Le tas d'une image dépend de ce que les tests précédents ont fait compiler : sur `main` comme ici, le même réseau passe
    // de 2,2 ko par image à froid à 4,5 ko après des réglages variés, tube ou pas. Le seuil absolu reste celui
    // d'`interface.spec.ts`, qui passe tôt ; ici, on compare le tube à son absence, dans le même état, lot après lot.
    const flows = [400, 3000].map((turboPipeMm) => {
      const gas = new GasFlow();
      gas.configure({ ...BASE, turboPipeMm }, ORDER4);
      return { gas, cycle: 0, lots: [] as number[] };
    });
    // Le meilleur relevé, sur le tube : d'autres calculs sur la machine ne ralentissent qu'une partie des images.
    let cost = Infinity;
    const run = (flow: (typeof flows)[number], frames: number) => {
      for (let f = 0; f < frames; f++) {
        const turns = (WIDE_OPEN.rpm / 120) * FRAME;
        flow.gas.wantChambers = true;
        flow.gas.advance(FRAME, flow.cycle, turns, WIDE_OPEN);
        flow.cycle = (flow.cycle + turns) % 1;
        if (flow === flows[1]) cost = Math.min(cost, flow.gas.costMs);
      }
    };
    for (const flow of flows) run(flow, 600);
    // La médiane, comme `filtre.spec.ts` : une allocation par pas se retrouve dans chaque lot, le ramasse-miettes ne fait que des creux isolés.
    for (let lot = 0; lot < 30; lot++) {
      for (const flow of flows) {
        const before = process.memoryUsage().heapUsed;
        run(flow, 100);
        flow.lots.push((process.memoryUsage().heapUsed - before) / 100);
      }
    }
    const [near, far] = flows.map((flow) => flow.lots.sort((a, b) => a - b)[15]);
    expect(far - near).toBeLessThan(500);
    // Largement au-dessus de ce qu'on vise (4 ms) : la machine des tests peut être chargée.
    expect(cost).toBeLessThan(12);
  });

  for (const engine of ENGINES) {
    test(`vues : le turbo s'éloigne sans traverser le moteur, ${engine.name}`, () => {
      const config = { ...BASE, ...engine.config };
      const reference = turboChain(engineLayout(config));
      let previousZ = Infinity;
      let previousSide = Infinity;
      for (const mm of [450, 600, 800, 1000, 1500, 2000, 2500, 3000]) {
        const layout = engineLayout({ ...config, turboPipeMm: mm });
        const chain = turboChain(layout);
        const { bore, ring } = layout;
        chain.units.forEach((unit, b) => {
          // Le tube dessiné mesure ce qu'on a demandé, à l'à-peu-près des coudes : c'est de la vraie grandeur.
          const start = reference.units[b];
          const moved = Math.hypot(unit.y - start.y, unit.z - start.z);
          expect(Math.abs(moved)).toBeGreaterThan((mm - 400) * 0.6);
          expect(moved).toBeLessThan((mm - 400) * 1.01);
          expect(runLength(unit.feedRun)).toBeGreaterThan((mm - 400) * 0.75);

          // Assez loin, turbine, compresseur et boîte à air sont passés derrière le volant : rien ne peut plus toucher le moteur.
          const reach = Math.max(unit.compressorZ + bore * 0.3, unit.airbox[2] + unit.airboxSize[2]);
          if (reach > ring.z - ring.width) {
            // Encore à côté, seule la hauteur et le côté d'origine le tiennent à l'écart.
            expect(unit.y).toBe(start.y);
            expect(unit.x).toBe(start.x);
            const reachOut = Math.hypot(unit.x, unit.y) - unit.compressorHousing;
            expect(reachOut).toBeGreaterThan(ring.radius * 0.95);
          }
          // Les tubes contournent le bloc : aucun point à l'intérieur de son gabarit.
          const inner = Math.abs(start.x) * 0.5;
          for (const p of [...unit.hotRun, ...unit.feedRun.slice(2)]) {
            const insideLength = p[2] < layout.frontZ && p[2] > layout.rearZ;
            expect(insideLength && Math.abs(p[0]) < inner && p[1] > -ring.radius && p[1] < layout.deck).toBe(false);
          }
          // Le tube chaud ne fait pas d'écart : un mètre de large, ce serait une courbe qui s'emballe.
          for (const p of unit.hotRun) {
            expect(Math.abs(p[0])).toBeLessThan(Math.abs(unit.hotFrom[0]) + bore);
            expect(p[1]).toBeGreaterThan(chain.yMin - bore);
          }
          // Dans le cadre des coupes.
          expect(unit.y - bore).toBeGreaterThanOrEqual(chain.yMin - 1);
          expect(unit.sideZ - bore * 0.24).toBeGreaterThanOrEqual(chain.sideZMin);
          expect(unit.airbox[1] + unit.airboxSize[1]).toBeLessThanOrEqual(chain.yMax + 1);
        });
        expect(chain.units[0].z + chain.units[0].y).toBeLessThan(previousZ);
        expect(chain.units[0].sideZ).toBeLessThanOrEqual(previousSide);
        previousZ = chain.units[0].z + chain.units[0].y;
        previousSide = chain.units[0].sideZ;
        // Trois mètres repliés restent lisibles : la coupe de côté garde le moteur à plus du tiers de sa largeur.
        const span = chain.zMax - chain.sideZMin;
        expect(span).toBeLessThan((layout.frontZ - layout.rearZ) * 2.6 + bore * 6);
        expect(chain.sideFolded).toBe(mm - 400 > bore * 2.5);
      }
      // Plus court, il remonte contre le collecteur, sans tube à ajouter.
      const close = turboChain(engineLayout({ ...config, turboPipeMm: 150 }));
      expect(close.units[0].y).toBeGreaterThan(reference.units[0].y);
      expect(close.units[0].feedRun).toEqual([]);
    });
  }
});

test.describe('le tube du turbo, sur la page', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le curseur n\'existe que sur un turbo, se résume sur le bouton et tient dans le lien', async ({ page }) => {
    await page.goto(`./?test#m=${TURBO4}`);
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('tube');
    await ouvrir(page, 'engine');
    const slider = page.getByRole('slider', { name: 'Tube jusqu\'au turbo' });
    await expect(slider).toHaveValue('400');
    await expect(page.getByTestId('turbo-pipe-help')).toContainText('Des cylindres à la turbine');
    await slider.fill('2500');
    await slider.dispatchEvent('change');
    await expect(page.getByTestId('open-engine-summary')).toContainText('tube de 2,5 m');
    await expect.poll(() => page.url()).toContain('_2500_');
    const url = page.url();
    const other = await page.context().newPage();
    await other.goto(url);
    await expect(other.getByTestId('start')).toHaveText('Démarrer le moteur');
    await expect(other.getByTestId('open-engine-summary')).toContainText('tube de 2,5 m');
    await ouvrir(other, 'engine');
    await expect(other.getByRole('slider', { name: 'Tube jusqu\'au turbo' })).toHaveValue('2500');
    await other.close();

    // Atmosphérique : pas de curseur, et le résumé n'en parle pas.
    await page.getByRole('button', { name: 'Atmosphérique' }).click();
    await expect(slider).toHaveCount(0);
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('tube');
  });

  test('un tube déporté se voit en 3D, en 2D, dans le gaz, et ne casse rien en marche', async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
    await page.goto(`./?test#m=${TURBO4.replace('.1.3', '.1.3_front_standard_single_medium_roots_off_3000_dohc_stock')}`);
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    await page.getByTestId('start').click();
    await voir(page, 'engine');
    await voir(page, 'gas');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.motion?.gas?.net?.pipeCells ?? -1), { timeout: 8000 }).toBeGreaterThan(50);
    expect(errors).toEqual([]);
  });
});
