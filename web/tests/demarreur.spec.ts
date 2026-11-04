import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * Le démarreur (cold_start.hpp) : la batterie froide et l'huile épaisse lancent lentement, une huile d'été ne laisse pas partir
 * la Cadillac, et la clé se tient au doigt. Moteur chaud, le démarrage est celui d'avant.
 */
const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const SHOTS = process.env.REVFORGE_SHOTS;

async function open(page: Page, changes: Record<string, unknown> = {}, preset = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  if (preset) {
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption(preset);
  }
  // Le réglage voulu passe par le lien de partage, comme un lien qu'on aurait reçu.
  const hash = await page.evaluate((next) => {
    const { encodeConfig, config } = (window as any).__revforge;
    return encodeConfig({ ...config(), ...next });
  }, changes);
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

/** Le temps que le démarreur tourne, de la clé à la prise, lu à la télémétrie (30 fois par seconde). */
async function crankSeconds(page: Page) {
  await page.getByTestId('start').click();
  await expect.poll(() => telemetry(page).then((t) => t.cranking), { timeout: 5_000 }).toBe(1);
  const from = Date.now();
  let lowest = 99;
  let amps = 0;
  await expect.poll(async () => {
    const t = await telemetry(page);
    if (t.cranking) {
      lowest = Math.min(lowest, t.batteryV);
      amps = Math.max(amps, t.starterA);
    }
    return t.cranking === 0 && t.rpm > 500;
  }, { timeout: 15_000, intervals: [30] }).toBe(true);
  return { seconds: (Date.now() - from) / 1000, lowest, amps };
}

test.describe('le démarreur', () => {
  test('par -30 °C, la batterie froide et l’huile épaisse peinent, puis le moteur prend ; chaud, rien n’a changé', async ({ page }) => {
    const errors = await open(page, { ambientC: -30, startTemp: 'cold' });
    // Avant la clé, la jauge de la batterie est au repos, pleine.
    await expect(page.getByTestId('gauge-battery-sub')).toContainText('Au repos');
    const cold = await crankSeconds(page);
    expect(cold.seconds).toBeGreaterThan(1.2);
    expect(cold.lowest).toBeLessThan(8.6);
    expect(cold.amps).toBeGreaterThan(150);
    // Le moteur tourne : l'alternateur tient la batterie.
    await expect.poll(() => telemetry(page).then((t) => t.batteryV), { timeout: 3_000 }).toBeGreaterThan(14);
    if (SHOTS) {
      await page.setViewportSize({ width: 1600, height: 1000 });
      await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/jauges-batterie-large.png` });
      await page.setViewportSize({ width: 360, height: 800 });
      await page.getByTestId('gauge-bank').scrollIntoViewIfNeeded();
      await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/jauges-batterie-360.png` });
      await page.setViewportSize({ width: 1280, height: 800 });
    }
    expect(errors).toEqual([]);

    const warmErrors = await open(page, { ambientC: 20, startTemp: 'warm' });
    const warm = await crankSeconds(page);
    // Le démarrage d'origine : 0,7 à 1 s de démarreur, batterie de référence.
    expect(warm.seconds).toBeLessThan(1.3);
    expect(warm.lowest).toBeGreaterThan(9);
    expect(warmErrors).toEqual([]);
  });

  test('une huile d’été par -25 °C : la Cadillac ne démarre pas, le voyant le dit, et la clé se tient pour réessayer', async ({ page }) => {
    test.setTimeout(60_000);
    const errors = await open(page, { ambientC: -25, startTemp: 'cold' }, 'v16_cadillac');
    await page.getByTestId('start').click();
    await expect.poll(() => telemetry(page).then((t) => t.cranking), { timeout: 5_000 }).toBe(1);
    // Le calculateur tient le démarreur 10 s, puis renonce.
    await expect(page.getByTestId('start-failed')).toBeVisible({ timeout: 15_000 });
    await expect(page.getByTestId('gauge-battery-sub')).toContainText("n'a pas pris");
    const restart = page.getByTestId('restart');
    await expect(restart).toBeVisible();
    // La clé tenue au doigt : le démarreur tourne tant qu'on appuie.
    const box = (await restart.boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await expect.poll(() => telemetry(page).then((t) => t.cranking), { timeout: 3_000 }).toBe(1);
    await expect(page.getByTestId('gauge-battery-sub')).toContainText('Démarreur');
    await expect(page.getByTestId('start-failed')).toBeHidden();
    await page.mouse.up();
    expect(errors).toEqual([]);
  });

  test('l’huile se choisit dans les conditions, va dans le lien et dans le résumé', async ({ page }) => {
    const errors = await open(page);
    await ouvrir(page, 'car');
    const oil = page.getByTestId('panel-car').getByTestId('oil-grade');
    await oil.getByRole('button', { name: '20W-50' }).click();
    await expect(oil).toContainText('−15 °C');
    await expect.poll(() => page.evaluate(() => decodeURIComponent(location.hash))).toContain('oil=20w50');
    await expect(page.getByTestId('open-car-summary')).toContainText('huile 20W-50');
    if (SHOTS) {
      await page.setViewportSize({ width: 360, height: 800 });
      await oil.scrollIntoViewIfNeeded();
      await page.getByTestId('panel-car').screenshot({ path: `${SHOTS}/volet-huile-360.png` });
    }
    expect(errors).toEqual([]);
  });
});
