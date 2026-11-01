import { expect, test, type Page } from '@playwright/test';

/* eslint-disable @typescript-eslint/no-explicit-any */

const ELECTRIC = '#m=electric~compact.1800.19.rear';

const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
/** Ce qu'affiche une case : la virgule française redevient un nombre, le tiret vaut zéro. */
const shown = async (page: Page, testId: string) => {
  const text = (await page.getByTestId(testId).textContent())!.trim().split(/\s/)[0];
  return text === '—' ? 0 : Number(text.replace(/\s/g, '').replace(',', '.'));
};

async function start(page: Page, hash = '') {
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
}

test.describe('la consommation estimée', () => {
  test("avant le contact, tout est à zéro et l'étiquette dit que c'est une estimation", async ({ page }) => {
    await page.goto('./?test');
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    await expect(page.getByTestId('conso')).toContainText('estimation');
    await expect(page.getByTestId('conso')).toContainText('Huile');
    expect(await shown(page, 'conso-lph')).toBe(0);
    expect(await shown(page, 'conso-l100')).toBe(0);
    expect(await shown(page, 'conso-trip')).toBe(0);
    expect(await shown(page, 'conso-oil-gph')).toBe(0);
    await expect(page.getByTestId('conso-avg')).toContainText('—');
  });

  test('monte au ralenti, plus encore avec les gaz, et le total avance', async ({ page }) => {
    await start(page);
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await expect.poll(() => telemetry(page).then((t) => t.cranking === 0 && t.rpm > 500 && t.rpm < 1300), { timeout: 10_000 }).toBe(true);
    await expect.poll(() => shown(page, 'conso-lph'), { timeout: 8_000 }).toBeGreaterThan(0.2);
    // Le ralenti accéléré du démarrage retombe en une seconde environ.
    await expect.poll(() => telemetry(page).then((t) => t.fuelLph), { timeout: 8_000 }).toBeLessThan(2);
    const idle = await telemetry(page).then((t) => t.fuelLph);
    expect(idle).toBeGreaterThan(0.2);
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.fuelLph), { timeout: 3_000 }).toBeGreaterThan(idle * 3);
    await page.keyboard.up('Space');
    expect(await telemetry(page).then((t) => t.fuelTripL)).toBeGreaterThan(0);
    expect(await telemetry(page).then((t) => t.oilGph)).toBeGreaterThan(0);
    await expect.poll(() => shown(page, 'conso-oil-gph')).toBeGreaterThan(0);
  });

  test('tombe à zéro pied levé en roulant, moteur entraîné par les roues', async ({ page }) => {
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 20_000 }).toBeGreaterThan(70);
    await page.keyboard.up('Space');
    await expect.poll(() => telemetry(page).then((t) => t.fuelLph === 0 && t.speed > 30 && t.gear > 0), { timeout: 5_000 }).toBe(true);
    await expect.poll(() => shown(page, 'conso-lph'), { timeout: 2_000 }).toBe(0);
    // Au-delà de 100 m parcourus, des L/100 km moyens ont un sens : ils s'affichent.
    await expect.poll(() => shown(page, 'conso-avg'), { timeout: 8_000 }).toBeGreaterThan(1);
    expect(await telemetry(page).then((t) => t.co2GPerKm)).toBeGreaterThan(20);
  });

  test('le bouton remet le trajet à zéro, et le contact coupé arrête tout', async ({ page }) => {
    await start(page);
    await expect.poll(() => telemetry(page).then((t) => t.fuelTripL), { timeout: 8_000 }).toBeGreaterThan(0);
    await page.getByTestId('conso-reset').click();
    await expect.poll(async () => {
      const t = await telemetry(page);
      return t.fuelTripL < 0.0002 && t.oilTripMl < 0.01;
    }, { timeout: 2_000 }).toBe(true);
    await page.getByTestId('start').click();
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    await expect.poll(() => shown(page, 'conso-lph'), { timeout: 2_000 }).toBe(0);
    await expect.poll(() => shown(page, 'conso-oil-gph'), { timeout: 2_000 }).toBe(0);
  });

  test('changer de moteur repart de zéro', async ({ page }) => {
    await start(page);
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.fuelTripL), { timeout: 10_000 }).toBeGreaterThan(0.002);
    await page.keyboard.up('Space');
    const before = await telemetry(page).then((t) => t.fuelTripL);
    await page.getByTestId('open-share').click();
    await page.getByTestId('preset').selectOption('v8_cross_plane');
    // Le nouveau moteur brûle aussitôt : le total repart de zéro, sans rester à zéro.
    await expect.poll(() => telemetry(page).then((t) => t.fuelTripL), { timeout: 3_000 }).toBeLessThan(before / 2);
  });

  test("l'électrique montre des kWh, et ni essence ni huile", async ({ page }) => {
    await start(page, ELECTRIC);
    await expect(page.getByTestId('conso-energy')).toContainText('kWh/100 km');
    await expect(page.getByTestId('conso-lph')).toHaveCount(0);
    await expect(page.getByTestId('conso-oil-gph')).toHaveCount(0);
    await expect(page.getByTestId('conso')).not.toContainText('Huile');
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.energyKwhPer100km), { timeout: 10_000 }).toBeGreaterThan(1);
    await page.keyboard.up('Space');
    await expect.poll(() => telemetry(page).then((t) => t.energyTripKwh), { timeout: 5_000 }).toBeGreaterThan(0);
    const t = await telemetry(page);
    expect(t.fuelLph).toBe(0);
    expect(t.oilGph).toBe(0);
  });

  test("en anglais comme en français : les libellés suivent, et les nombres aussi", async ({ page }) => {
    await page.goto('./?test&lang=en');
    await expect(page.getByTestId('conso')).toContainText('estimate');
    await expect(page.getByTestId('conso')).toContainText('Oil');
    await expect(page.getByTestId('conso-reset')).toHaveText('Reset trip');
    await page.goto('./?test&lang=fr');
    await expect(page.getByTestId('conso')).toContainText('estimation');
    await expect(page.getByTestId('conso')).toContainText('Huile');
    await expect(page.getByTestId('conso-reset')).toHaveText('Remettre à zéro');
    await expect(page.getByTestId('conso-trip')).toContainText('0,00');
  });
});
