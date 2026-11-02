import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import { GasNetwork, P_ATM, type GasInputs } from '../src/lib/fluidGas';
import type { EngineConfig } from '../src/lib/config';

const NATURAL = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const TURBO = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
const V8 = '8.cross_plane.5000.92.7000.sport.natural.petrol.0.off.dump_valve~coupe.1650.19.rear.1';
const shots = process.env.FILTRE_SHOTS;

/** Le lien de partage : le filtre est le dernier de son groupe, après les `_`. */
const link = (config: string, filter: string) => `./?test#m=${config}_front_standard_single_medium_roots_off_400_dohc_${filter}`;

async function chevaux(page: Page): Promise<number> {
  const text = (await page.getByTestId('spec-hp').textContent()) ?? '';
  return Number(text.match(/\d+/)?.[0]);
}

test.describe('le filtre à air', () => {
  test('il se choisit dans le volet du moteur, s’y explique et se lit sur le bouton', async ({ page }) => {
    await page.goto(`./?test#m=${NATURAL}`);
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('air-filter-help')).toContainText('papier');
    const figures = page.getByTestId('air-filter-figures');
    await expect(figures).toContainText(/\d,\d kPa/);
    await expect(figures).toContainText('20 °C');
    const summary = page.getByTestId('open-engine-summary');
    await expect(summary).not.toContainText('Cône');

    const paper = await chevaux(page);
    await page.getByRole('button', { name: "Cône à l'air libre" }).click();
    await expect(page.getByTestId('air-filter-help')).toContainText('chaud');
    await expect(figures).toContainText(/3\d °C/);
    await expect(summary).toContainText('Cône');
    await expect(page.getByTestId('gauge-intake-air')).toHaveCount(1);
    // L'air chaud se paie sur la fiche : un petit écart, pas un miracle.
    await expect.poll(() => chevaux(page)).toBeLessThan(paper);
    expect(await chevaux(page)).toBeGreaterThan(paper * 0.93);

    await page.getByRole('button', { name: 'Cornets' }).click();
    await expect(summary).toContainText('Cornets');
    // Le filtre voyage avec le lien de partage.
    await page.goto('about:blank');
    await page.goto(link(NATURAL, 'stacks'));
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('air-filter-help')).toContainText('ouverts');
    if (shots) await page.screenshot({ path: `${shots}/volet_cornets.png`, fullPage: false });
  });

  test('sa forme se voit, en coupe comme en 3D', async ({ page }) => {
    test.skip(!shots, 'captures seulement : FILTRE_SHOTS donne le dossier');
    test.setTimeout(540_000);
    await page.setViewportSize({ width: 1600, height: 1000 });
    for (const [name, config] of [['atmo', NATURAL], ['turbo', TURBO], ['v8', V8]] as const) {
      for (const filter of ['stock', 'panel', 'cone', 'stacks']) {
        await page.goto('about:blank');
        await page.goto(link(config, filter));
        await voir(page, 'engine');
        const view = page.getByTestId('engine-view');
        await expect(view).toBeVisible();
        await page.getByTestId('engine-mode-3d').click();
        await page.waitForTimeout(1500);
        await view.screenshot({ path: `${shots}/moteur_${name}_${filter}_3d.png` });
        await page.getByTestId('engine-mode-2d').click();
        await page.waitForTimeout(400);
        await view.screenshot({ path: `${shots}/moteur_${name}_${filter}_2d.png` });
      }
    }
  });

  test('sur téléphone, le choix tient dans la largeur', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 760 });
    await page.goto(`./?test#m=${NATURAL}`);
    await ouvrir(page, 'engine');
    const help = page.getByTestId('air-filter-figures');
    await help.scrollIntoViewIfNeeded();
    const box = await help.boundingBox();
    expect(box!.x).toBeGreaterThanOrEqual(0);
    expect(box!.x + box!.width).toBeLessThanOrEqual(360);
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(360);
    if (shots) await page.screenshot({ path: `${shots}/volet_tel.png` });
  });
});

// ── Le gaz à l'écran, sans navigateur ──

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 5500, load: 1, boost: 0, release: 0, burning: true, heat: 0.7 };

function drive(net: GasNetwork, seconds: number, each?: () => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (WIDE_OPEN.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, WIDE_OPEN);
    cycle = (cycle + turns) % 1;
    each?.();
  }
}

/** La dépression moyenne à l'entrée du plénum, à plein gaz : ce que coûte le filtre, à l'écran. */
function plenumDepression(filter: EngineConfig['airFilter']): { depression: number; net: GasNetwork } {
  const net = new GasNetwork();
  net.build({ ...BASE, airFilter: filter }, ORDER4);
  let sum = 0;
  let count = 0;
  drive(net, 0.08);
  drive(net, 0.08, () => {
    sum += P_ATM - net.p[net.intakeFirst + net.intakeCount - 1];
    count++;
  });
  return { depression: sum / count, net };
}

test('le gaz à l’écran freine moins, et entre plus chaud, derrière un cône', () => {
  const paper = plenumDepression('stock');
  const cone = plenumDepression('cone');
  const stacks = plenumDepression('stacks');
  expect(paper.net.inletK).toBe(293);
  expect(cone.net.inletK).toBeGreaterThan(300);
  expect(cone.depression).toBeLessThan(paper.depression);
  expect(stacks.depression).toBeLessThan(paper.depression);
});

test('sur un turbo, le volume de charge ne bouge pas avec le filtre', () => {
  const chargeVolume = (filter: EngineConfig['airFilter']) => {
    const net = new GasNetwork();
    net.build({ ...BASE, aspiration: 'turbo', airFilter: filter }, ORDER4);
    let volume = 0;
    for (const part of net.intakeParts) {
      if (part.kind === 'hot' || part.kind === 'cooler' || part.kind === 'cold') {
        for (let k = 0; k < part.count; k++) volume += 1 / net.invV[part.first + k];
      }
    }
    return volume;
  };
  expect(chargeVolume('cone')).toBe(chargeVolume('stock'));
  expect(chargeVolume('stacks')).toBe(chargeVolume('stock'));
});

test('le gaz à l’écran n’alloue pas plus derrière des cornets', () => {
  const net = new GasNetwork();
  net.build({ ...BASE, airFilter: 'stacks' }, ORDER4);
  drive(net, 0.02);
  const before = process.memoryUsage().heapUsed;
  drive(net, 0.05);
  // Pas assez pour un objet par appel : voir le test du tas de `turbo.spec.ts`.
  expect(process.memoryUsage().heapUsed - before).toBeLessThan(4_000_000);
});
