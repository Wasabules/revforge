import { expect, test, type Page } from '@playwright/test';
import type { EngineConfig } from '../src/lib/config';
import { gasKey } from '../src/lib/fluid';
import { compressionRatioOf, GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * La combustion : le taux de compression et le carburant, la limite de cliquetis de la fiche, et ce que le capteur retire en roulant.
 * Au défaut, rien de cela ne se calcule : ni avance, ni jauge.
 */
const SHOTS = process.env.REVFORGE_SHOTS;
const BASE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const DIESEL = '4.inline.2000.85.4500.stock.turbo.diesel.0.off.dump_valve~compact.1350.18.front.1';

const GAS_BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', routing: 'single', aspiration: 'natural', fuel: 'petrol', multiplier: 0, pops: 'off',
  turboRelease: 'dump_valve', boostBar: 1, turboSize: 'medium', fuelSystem: 'injection', electric: false, body: 'compact', massKg: 1200, rimInch: 16, drive: 'front', boxFlywheel: 'single',
  compressionRatio: 0,
} as unknown as EngineConfig;
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 3000, load: 1, boost: 0, release: 0, burning: true, heat: 0.7 };

/** La plus haute pression du premier cylindre sur un cycle établi, en part de son énergie interne par volume. */
function peakCylinder(config: EngineConfig): number {
  const net = new GasNetwork();
  net.build(config, [1, 3, 4, 2]);
  let cycle = 0;
  let peak = 0;
  for (let t = 0; t < 1; t += FRAME) {
    const turns = (WIDE_OPEN.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, WIDE_OPEN);
    cycle = (cycle + turns) % 1;
    if (t > 0.6) peak = Math.max(peak, net.cylU[0] / net.cylV[0]);
  }
  return peak;
}

async function open(page: Page, hash = BASE) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

test.describe('la combustion, dans le calcul', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le gaz à l\'écran suit le taux, borné comme en C++, et se reconstruit quand il change', () => {
    expect(compressionRatioOf(GAS_BASE, 10.5, 6, 24)).toBe(10.5);
    expect(compressionRatioOf({ ...GAS_BASE, compressionRatio: 13 }, 10.5, 6, 24)).toBe(13);
    // Un diesel ne descend pas sous 14:1, un rotor ne dépasse pas 11,9:1 : `compressionRatioOf` dans combustion.cpp.
    expect(compressionRatioOf({ ...GAS_BASE, compressionRatio: 9 }, 18, 14, 24)).toBe(14);
    expect(compressionRatioOf({ ...GAS_BASE, compressionRatio: 16 }, 9.4, 6, 11.9)).toBe(11.9);
    expect(gasKey({ ...GAS_BASE, compressionRatio: 13 })).not.toBe(gasKey(GAS_BASE));
    const stock = peakCylinder(GAS_BASE);
    const higher = peakCylinder({ ...GAS_BASE, compressionRatio: 13 });
    expect(higher).toBeGreaterThan(stock * 1.1);
  });
});

test('au défaut, la fiche dit la marge au cliquetis, sans jauge ni avance calculée', async ({ page }) => {
  const errors = await open(page);
  await ouvrir(page, 'engine');
  const figures = page.getByTestId('panel-engine').getByTestId('combustion-figures');
  await expect(figures).toContainText('Rendement ×1,000');
  await expect(figures).toContainText('reste 2,0° sous la limite de cliquetis');
  await expect(page.getByTestId('meter-knock')).toHaveCount(0);
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
  expect(await page.evaluate(() => (window as any).__revforge.telemetry().sparkAdvance)).toBe(0);
  expect(errors).toEqual([]);
});

test('plus de compression : la fiche dit qu\'il cliquette, la jauge apparaît, et le moteur tourne avec une avance calculée', async ({ page }) => {
  const errors = await open(page, `${BASE}~~cr=13`);
  await ouvrir(page, 'engine');
  const panel = page.getByTestId('panel-engine');
  await expect(page.getByTestId('open-engine-summary')).toContainText('13,0:1');
  const figures = panel.getByTestId('combustion-figures');
  await expect(figures).toContainText('dépasse la limite de cliquetis');
  await expect(figures).toContainText('le capteur de cliquetis retire');
  await expect(page.getByTestId('meter-knock')).toHaveCount(1);
  if (SHOTS) {
    await panel.getByTestId('section-combustion').scrollIntoViewIfNeeded();
    await page.screenshot({ path: `${SHOTS}/combustion_volet.png` });
  }
  // Des carburateurs n'ont pas de capteur.
  await panel.getByTestId('fuel-system').getByRole('button', { name: 'Un par cylindre' }).click();
  await expect(figures).toContainText('il cliquette');
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().sparkAdvance), { timeout: 15_000 }).toBeGreaterThan(0);
  expect(await page.evaluate(() => (window as any).__revforge.telemetry().knockLimit)).not.toBe(0);
  expect(errors).toEqual([]);
});

test('le carburant : l\'E85 passe dans le lien et le résumé ; un diesel n\'a ni octane ni cliquetis', async ({ page }) => {
  let errors = await open(page);
  await ouvrir(page, 'engine');
  await page.getByTestId('panel-engine').getByTestId('fuel-grade').getByRole('button', { name: 'E85' }).click();
  await expect(page.getByTestId('open-engine-summary')).toContainText('E85');
  await expect.poll(() => page.evaluate(() => decodeURIComponent(location.hash))).toContain('grade=e85');
  await expect(page.getByTestId('panel-engine').getByTestId('combustion-figures')).toContainText('sous la limite');
  expect(errors).toEqual([]);

  errors = await open(page, `${DIESEL}~~cr=16`);
  await ouvrir(page, 'engine');
  const panel = page.getByTestId('panel-engine');
  await expect(panel.getByTestId('fuel-grade')).toHaveCount(0);
  await expect(panel.getByTestId('combustion-figures')).toContainText('ni cliquetis d\'étincelle');
  const efficiency = await page.evaluate(() => (window as any).__revforge.spec().combustion.efficiency);
  expect(efficiency).toBeLessThan(1);
  expect(efficiency).toBeGreaterThan(0.98);
  expect(errors).toEqual([]);
});

test('le volet de la combustion à 360 px reste lisible', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 780 });
  const errors = await open(page, `${BASE}~~cr=13,grade=race102`);
  await ouvrir(page, 'engine');
  const section = page.getByTestId('panel-engine').getByTestId('section-combustion');
  await section.scrollIntoViewIfNeeded();
  const box = await section.boundingBox();
  expect(box?.width ?? 0).toBeLessThanOrEqual(360);
  if (SHOTS) await page.screenshot({ path: `${SHOTS}/combustion_360.png` });
  expect(errors).toEqual([]);
});
