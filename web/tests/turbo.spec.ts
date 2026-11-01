import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { GasNetwork, P_ATM, type GasInputs } from '../src/lib/fluidGas';

/* eslint-disable @typescript-eslint/no-explicit-any */
const NATURAL = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const TURBO4 = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
const TURBO_V6 = '6.v.3000.86.6500.sport.turbo.petrol.0.off.dump_valve~coupe.1600.19.rear.1.2';

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
// Ce que `TurboCharger` calcule à 5500 tr/min, plein gaz, 1,3 bar : le rapport, et 140 °C en sortie de compresseur.
const WIDE_OPEN: GasInputs = { rpm: 5500, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.4, compressorOutK: 413, wastegate: 0.5 };

function drive(net: GasNetwork, inputs: GasInputs, seconds: number, each?: () => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (inputs.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, inputs);
    cycle = (cycle + turns) % 1;
    each?.();
  }
}

function mean(net: GasNetwork, inputs: GasInputs, read: () => number): number {
  let sum = 0;
  let n = 0;
  drive(net, inputs, 0.08, () => {
    sum += read();
    n++;
  });
  return sum / n;
}

test.describe('la chaîne d\'admission, dans le calcul', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('un moteur atmosphérique garde son simple conduit', () => {
    const net = new GasNetwork();
    net.build({ ...BASE, aspiration: 'natural' }, ORDER4);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['duct']);
    expect(net.compressorCell).toBe(-1);
    expect(net.coolerFirst).toBe(-1);
  });

  test('sur un turbo, l\'air passe par tous les organes, dans l\'ordre', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['filter', 'inlet', 'hot', 'cooler', 'cold']);
    const parts = net.intakeParts;
    // La section de l'échangeur et de la boîte à air : des volumes, pas des tubes.
    expect(parts[3].area).toBeGreaterThan(parts[2].area);
    expect(parts[0].area).toBeGreaterThan(parts[1].area);
    // Un volume de charge du même ordre que dans le C++, 6,5 L sur un 2 L.
    let volume = 0;
    for (const part of parts.slice(2)) {
      for (let k = 0; k < part.count; k++) volume += 1 / net.invV[part.first + k];
    }
    expect(volume * 1000).toBeGreaterThan(4.5);
    expect(volume * 1000).toBeLessThan(9);
  });

  test('l\'air sort chaud du compresseur, plus frais de l\'échangeur, et y perd de la pression', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    drive(net, WIDE_OPEN, 0.5);
    const inlet = mean(net, WIDE_OPEN, () => net.temp[net.compressorCell - 2]);
    const hot = mean(net, WIDE_OPEN, () => net.temp[net.coolerInCell]);
    const cold = mean(net, WIDE_OPEN, () => net.temp[net.coolerOutCell]);
    const pIn = mean(net, WIDE_OPEN, () => net.p[net.coolerFirst]);
    const pOut = mean(net, WIDE_OPEN, () => net.p[net.coolerOutCell]);
    expect(inlet).toBeLessThan(310);
    expect(hot).toBeGreaterThan(inlet + 60);
    expect(cold).toBeLessThan(hot - 40);
    expect(cold).toBeLessThan(345);
    const drop = (pIn - pOut) / 1e5;
    expect(drop).toBeGreaterThan(0.06);
    expect(drop).toBeLessThan(0.3);
  });

  test('le plénum suit la pression que le modèle de conduite demande, pas une autre', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    drive(net, WIDE_OPEN, 0.5);
    const plenum = mean(net, WIDE_OPEN, () => net.zP[net.zonePlenum]);
    const before = mean(net, WIDE_OPEN, () => net.p[net.intakeFirst + net.intakeCount - 1]);
    // 1 + 1,3 bar au-dessus de l'atmosphère, à 12 % près : le gaz et la voiture parlent du même turbo.
    expect(plenum / (P_ATM + 1.3e5)).toBeGreaterThan(0.88);
    expect(plenum / (P_ATM + 1.3e5)).toBeLessThan(1.12);
    // Papillon ouvert : presque la même pression des deux côtés.
    expect(before / plenum).toBeGreaterThan(0.95);
    expect(before / plenum).toBeLessThan(1.1);
    // Papillon fermé : la pression reste avant, plus après.
    const shut = { ...WIDE_OPEN, load: 0.05, burning: false };
    drive(net, shut, 0.05);
    const afterLift = net.zP[net.zonePlenum];
    expect(afterLift).toBeLessThan(plenum * 0.75);
    expect(net.p[net.intakeFirst + net.intakeCount - 1]).toBeGreaterThan(afterLift);
  });

  test('sans soupape ouverte, l\'air revient par le compresseur', () => {
    const lift = (release: EngineConfig['turboRelease'], valve: number) => {
      const net = new GasNetwork();
      net.build({ ...BASE, turboRelease: release }, ORDER4);
      drive(net, WIDE_OPEN, 0.4);
      let back = 0;
      let vented = 0;
      // Le pompage : le rapport tombe, et rien d'autre n'évacue l'air sous pression.
      drive(net, { ...WIDE_OPEN, load: 0, boost: 0.5, compressorRatio: 1.5, compressorOutK: 330, release: valve, burning: false, wastegate: 0 }, 0.03, () => {
        back = Math.min(back, net.compressorFlow);
        vented = Math.max(vented, net.releaseFlow);
      });
      return { back, vented };
    };
    expect(lift('flutter', 0).back).toBeLessThan(-0.01);
    expect(lift('flutter', 0).vented).toBe(0);
    expect(lift('dump_valve', 1).vented).toBeGreaterThan(0.01);
    expect(lift('dump_valve', 1).back).toBeGreaterThan(lift('flutter', 0).back);
  });

  test('un gros turbo a une turbine plus large : moins de pression avant elle', () => {
    const before = (size: 'small' | 'large') => {
      const net = new GasNetwork();
      net.build({ ...BASE, turboSize: size }, ORDER4);
      drive(net, { ...WIDE_OPEN, wastegate: 0 }, 0.3);
      return mean(net, { ...WIDE_OPEN, wastegate: 0 }, () => net.zP[net.zoneJoin]);
    };
    expect(before('small')).toBeGreaterThan(before('large') * 1.05);
  });
});

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  // Un autre lien sur la même page ne la recharge pas : on en sort d'abord.
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

async function start(page: Page, hash: string) {
  const errors = await open(page, hash);
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

const tele = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const motion = (page: Page) => page.evaluate(() => {
  const m = (window as any).__revforge.motion;
  return { rpm: m.turboRpm, gate: m.wastegate, valve: m.blowoff, angle: m.turbineAngle, slow: m.slow, ratio: m.compressorRatio, outK: m.compressorOutK, net: m.gas.net.n };
});

const engine = (page: Page) => page.evaluate(() => {
  const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
  return { labels: (d.scene()?.labels ?? []).map((l: { key: string }) => l.key), chain: !!d.flat()?.chain, mode: d.mode() };
});

test.describe('à l\'écran', () => {
  test('la taille du turbo voyage dans le lien, et les anciens liens restent valides', async ({ page }) => {
    const wheel = () => page.evaluate(() => (window as any).__revforge.spec().turboWheelMm);
    await open(page, `${TURBO4}_front_standard_single_large`);
    await expect.poll(wheel).toBeGreaterThan(0);
    const large = await wheel();
    // Un lien d'avant le réglage de taille : le turbo moyen.
    await open(page, `${TURBO4}_front_standard_single`);
    await expect.poll(wheel).toBeGreaterThan(0);
    const medium = await wheel();
    expect(large / medium).toBeCloseTo(1.2, 1);
    // Et d'avant le routage, l'emplacement et tout le reste.
    await open(page, TURBO4);
    await expect.poll(wheel).toBeCloseTo(medium, 1);
  });

  test('la 3D ne montre le chemin de l\'air que sur un turbo', async ({ page }) => {
    const parts = async (hash: string) => {
      await open(page, hash);
      await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
      await page.waitForFunction(() => {
        const d = (document.querySelector('[data-testid=engine-view]') as any)?.__engine;
        return d && d.live3d() && d.scene() && d.drawn > 0;
      }, null, { timeout: 30_000 });
      return engine(page);
    };
    const turbo = await parts(TURBO4);
    for (const key of ['engine.part.compressor', 'engine.part.turbine', 'engine.part.shaft', 'engine.part.intercooler', 'engine.part.airbox', 'engine.part.maf', 'engine.part.chargeHot', 'engine.part.chargeCold', 'engine.part.blowoff', 'engine.part.wastegate']) {
      expect(turbo.labels).toContain(key);
    }
    const natural = await parts(NATURAL);
    expect(natural.labels).not.toContain('engine.part.compressor');
    expect(natural.labels).not.toContain('engine.part.intercooler');
    // Sur un V, un turbo par rangée, et toujours un seul échangeur.
    await open(page, TURBO_V6);
    await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
    await page.waitForFunction(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any)?.__engine;
      return d && d.live3d() && d.scene() && d.drawn > 0 && d.scene().turboWheels.length === 2;
    }, null, { timeout: 30_000 });
  });

  test('la 2D et le gaz aussi', async ({ page }) => {
    await open(page, TURBO4);
    await page.getByTestId('engine-mode-2d').click();
    await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
    await expect.poll(() => engine(page).then((e) => e.chain)).toBe(true);
    await voir(page, 'gas');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.motion.gas.net.intakeParts.length)).toBe(5);
    await open(page, NATURAL);
    await voir(page, 'engine');
    await page.getByTestId('engine-mode-2d').click();
    await expect.poll(() => engine(page).then((e) => e.chain)).toBe(false);
    await voir(page, 'gas');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.motion.gas.net.intakeParts.length)).toBe(1);
  });

  test('sur la voiture aussi, et seulement sur un turbo', async ({ page }) => {
    const shown = () => page.getByTestId('car-stage').locator('canvas').evaluate((canvas) => (canvas as unknown as { __labels?: string[] }).__labels ?? []);
    await open(page, TURBO4);
    await expect.poll(shown).toContain('Échangeur');
    expect(await shown()).toContain('Boîte à air');
    await open(page, NATURAL);
    await expect.poll(async () => (await shown()).length).toBeGreaterThan(0);
    expect(await shown()).not.toContain('Échangeur');
  });

  test('la soupape et la wastegate suivent la télémétrie : pression, puis lever de pied', async ({ page }) => {
    const errors = await start(page, TURBO4);
    await page.waitForTimeout(1000);
    expect((await motion(page)).valve).toBeLessThan(0.05);
    await page.keyboard.down('Space');
    // Au point mort, le moteur finit au rupteur : l'arbre monte, puis la wastegate s'ouvre si la pression arrive.
    await expect.poll(() => tele(page).then((t) => t.turboRpm), { timeout: 8000 }).toBeGreaterThan(30000);
    const spinning = await tele(page);
    expect(spinning.turboOutC).toBeGreaterThan(spinning.turboCoolC - 1);
    expect(spinning.boost).toBeGreaterThanOrEqual(0);
    await page.keyboard.up('Space');
    await page.waitForTimeout(250);
    // Le moteur retombe : l'arbre continue, et la télémétrie arrive dans `Motion`.
    const after = await tele(page);
    const shown = await motion(page);
    expect(after.turboRpm).toBeGreaterThan(spinning.turboRpm * 0.2);
    expect(shown.rpm).toBeGreaterThan(0);
    expect(Math.abs(shown.valve - after.turboBlowoff)).toBeLessThan(0.5);
    expect(Math.abs(shown.gate - after.turboWastegate)).toBeLessThan(0.5);
    expect(errors).toEqual([]);
  });

  test('au vrai temps, l\'arbre tourne d\'autant plus vite, et rien ne casse', async ({ page }) => {
    const errors = await start(page, TURBO4);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.turboRpm), { timeout: 8000 }).toBeGreaterThan(30000);
    // L'angle à l'écran dépend du ralenti : on le mesure sur quelques images.
    const measure = (slow: number) => page.evaluate(async (wanted) => {
      const m = (window as any).__revforge.motion;
      m.slow = wanted;
      let total = 0;
      let last = m.turbineAngle;
      let frames = 0;
      const started = performance.now();
      while (performance.now() - started < 300) {
        await new Promise(requestAnimationFrame);
        // Une vitesse par image : modulo un tour, en restant sous le demi-tour au ralenti.
        let step = m.turbineAngle - last;
        if (step < 0) step += Math.PI * 2;
        total += step;
        last = m.turbineAngle;
        frames++;
      }
      return { total, frames, rpm: m.turboRpm, blur: m.turbineBlur };
    }, slow);
    const fast = await measure(1);
    expect(fast.total).toBeGreaterThan(0);
    expect(Number.isFinite(fast.total)).toBe(true);
    expect(fast.blur).toBeGreaterThanOrEqual(0);
    await voir(page, 'gas');
    await page.waitForTimeout(500);
    const net = await page.evaluate(() => {
      const n = (window as any).__revforge.motion.gas.net;
      const values: number[] = [...n.temp, ...n.p];
      return { ok: values.every(Number.isFinite), parts: n.intakeParts.length };
    });
    expect(net.ok).toBe(true);
    expect(net.parts).toBe(5);
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  test('la taille se choisit dans les réglages du moteur et change la fiche', async ({ page }) => {
    await open(page, TURBO4);
    await ouvrir(page, 'engine');
    const size = page.getByRole('group', { name: 'Taille du turbo' });
    await expect(size).toBeVisible();
    await expect(page.getByTestId('turbo-size-help')).toContainText('Dimensionné');
    const peak = () => page.evaluate(() => (window as any).__revforge.spec().turboWheelMm);
    const medium = await peak();
    await size.getByRole('button', { name: 'Gros' }).click();
    await expect.poll(peak).toBeGreaterThan(medium);
    expect(page.url()).toContain('_large');
    await page.getByRole('button', { name: 'Atmosphérique' }).click();
    await expect(size).toBeHidden();
  });
});
