import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
/** Ce qu'une jauge affiche : le signe moins typographique et la virgule française redeviennent un nombre. */
const shown = async (page: Page, testId: string) => {
  const text = (await page.getByTestId(testId).textContent())!;
  return Number(text.replace(/[^\d,.\-−+]/g, '').replace('−', '-').replace(',', '.'));
};
/** Où écrire les captures : hors du dépôt, quand l'agent le demande. */
const SHOTS = process.env.REVFORGE_SHOTS;

async function open(page: Page, hash = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test${hash ? `#m=${hash}` : ''}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

async function start(page: Page) {
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 10_000 }).toBeGreaterThan(500);
}

test.describe('la thermique', () => {
  test('un départ à froid part de l’air ambiant, avec ralenti accéléré, essence et frottements en plus', async ({ page }) => {
    // Le départ à froid est désormais celui du site : une nuit dehors. Le choisir ne change rien, et le dit.
    const errors = await open(page);
    await ouvrir(page, 'car');
    await page.getByTestId('panel-car').getByTestId('start-temp').getByRole('button', { name: 'Une nuit' }).click();
    await expect(page.getByTestId('panel-car').getByTestId('start-temp').getByRole('button', { name: 'Une nuit' })).toHaveAttribute('aria-pressed', 'true');
    await page.keyboard.press('Escape');
    await expect(page.getByTestId('panel-car')).toBeHidden();
    // Avant le premier son, la jauge dit l'air ambiant : rien n'a chauffé.
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBe(20);
    await expect.poll(() => shown(page, 'gauge-oil-value')).toBe(20);
    await start(page);
    // Le calculateur vise plus haut tant que l'eau est froide, et le compteur paie l'enrichissement.
    await expect.poll(() => telemetry(page).then((t) => t.cranking === 0 && t.coldIdleRpm > 100), { timeout: 10_000 }).toBe(true);
    const t = await telemetry(page);
    expect(t.coolantC).toBeLessThan(30);
    expect(t.coldEnrichment).toBeGreaterThan(0.1);
    expect(t.coldFrictionNm).toBeGreaterThan(0);
    expect(t.oilViscosity).toBeGreaterThan(80);
    await expect(page.getByTestId('gauge-coolant-sub')).toContainText('ralenti +');
    await expect(page.getByTestId('gauge-coolant-sub')).toContainText('essence +');
    await expect(page.getByTestId('gauge-oil-sub')).toContainText('frottements +');
    await expect(page.getByTestId('gauge-oil-sub')).toContainText('mPa·s');
    // Le ralenti tient au-dessus de celui du moteur chaud : le régime le montre.
    await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5_000 }).toBeGreaterThan(900);
    if (SHOTS) {
      await page.setViewportSize({ width: 1600, height: 1000 });
      await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/jauges-froid-large.png` });
      await page.screenshot({ path: `${SHOTS}/page-froid-large.png`, fullPage: true });
      await page.setViewportSize({ width: 360, height: 800 });
      await page.getByTestId('gauge-bank').scrollIntoViewIfNeeded();
      await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/jauges-froid-360.png` });
    }
    expect(errors).toEqual([]);
  });

  test('un départ à chaud : les jauges à la référence, et rien du moteur froid', async ({ page }) => {
    // Le site part d'une nuit dehors : le moteur qui vient de rouler se choisit.
    const errors = await open(page);
    await ouvrir(page, 'car');
    await page.getByTestId('panel-car').getByTestId('start-temp').getByRole('button', { name: 'Vient de rouler' }).click();
    await page.keyboard.press('Escape');
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBe(90);
    await expect.poll(() => shown(page, 'gauge-oil-value')).toBe(100);
    await start(page);
    await expect.poll(() => telemetry(page).then((t) => t.cranking), { timeout: 10_000 }).toBe(0);
    const t = await telemetry(page);
    expect(t.coolantC).toBeGreaterThan(80);
    expect(t.coldIdleRpm).toBe(0);
    expect(t.coldEnrichment).toBe(0);
    // Finitions : le réseau agit aussi parti chaud, mais l'huile reste près de 100 °C : à peine un écart de frottements.
    expect(Math.abs(t.coldFrictionNm)).toBeLessThan(0.05 * t.warmFrictionNm);
    await expect(page.getByTestId('gauge-coolant-sub')).toContainText('Calorstat');
    await expect(page.getByTestId('gauge-coolant-sub')).not.toContainText('ralenti');
    await expect(page.getByTestId('gauge-oil-sub')).not.toContainText('frottements +');
    expect(errors).toEqual([]);
  });

  test('l’électrique n’a ni eau ni huile à montrer', async ({ page }) => {
    const errors = await open(page, 'electric~compact.1800.19.rear');
    await expect(page.getByTestId('gauge-bank')).toBeVisible();
    await expect(page.getByTestId('gauge-coolant')).toHaveCount(0);
    await expect(page.getByTestId('gauge-oil')).toHaveCount(0);
    expect(errors).toEqual([]);
  });
});
