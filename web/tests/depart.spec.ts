import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * Le moteur au départ : par défaut, le site part d'une voiture garée dehors depuis la nuit, tout à l'air ambiant. Une demi-heure
 * ou deux heures d'arrêt se calculent par le réseau thermique moteur coupé (C++), et les jauges le montrent avant le premier
 * démarrage. Moteur coupé, son suspendu, le réseau rattrape le temps passé en reprenant.
 */
const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const spec = (page: Page) => page.evaluate(() => (window as any).__revforge.spec());
/** Ce qu'une jauge affiche : le signe moins typographique et la virgule française redeviennent un nombre. */
const shown = async (page: Page, testId: string) => {
  const text = (await page.getByTestId(testId).textContent())!;
  return Number(text.replace(/[^\d,.\-−+]/g, '').replace('−', '-').replace(',', '.'));
};
const SHOTS = process.env.REVFORGE_SHOTS;
/** « eau −30 » : le texte écrit le moins typographique. */
const water = (celsius: number) => new RegExp(`eau ${celsius < 0 ? '[−-]' : ''}${Math.abs(celsius)}\\D`);

async function open(page: Page, changes: Record<string, unknown> = {}) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  // Sous Firefox, la navigation qui suit interrompt le chargement du worker de la fiche, et l'instrumentation de Playwright
  // (`chrome://juggler/…`) l'écrit dans la console : ce n'est pas une erreur du site.
  page.on('console', (message) => message.type() === 'error' && !message.text().includes('chrome://juggler/') && errors.push(message.text()));
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  if (Object.keys(changes).length) {
    const hash = await page.evaluate((next) => {
      const { encodeConfig, config } = (window as any).__revforge;
      return encodeConfig({ ...config(), ...next });
    }, changes);
    await page.goto('about:blank');
    await page.goto(`./?test#m=${hash}`);
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  }
  return errors;
}

async function start(page: Page) {
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 10_000 }).toBeGreaterThan(500);
}

test.describe('le départ', () => {
  test('par défaut, une nuit dehors par −30 °C : les jauges lisent l’air, puis deux heures d’arrêt se calculent', async ({ page }) => {
    const errors = await open(page, { ambientC: -30 });
    // Une nuit est le défaut du site : rien dans le lien, rien dans le résumé.
    expect(await page.evaluate(() => location.hash)).not.toContain('start=');
    await expect(page.getByTestId('open-car-summary')).not.toContainText('départ');
    // Avant le premier démarrage, l'eau et l'huile sont à l'air : plus de 90 et 100 °C écrits d'avance.
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBe(-30);
    await expect.poll(() => shown(page, 'gauge-oil-value')).toBe(-30);
    await ouvrir(page, 'car');
    const panel = page.getByTestId('panel-car');
    await expect(panel.getByTestId('start-temp').getByRole('button', { name: 'Une nuit' })).toHaveAttribute('aria-pressed', 'true');
    // Les choix, dans l'ordre du temps passé à l'arrêt.
    await expect(panel.getByTestId('start-temp').getByRole('button')).toHaveText(['Vient de rouler', '30 min', '2 h', 'Une nuit']);
    await expect(panel.getByTestId('start-state')).toContainText(water(-30));

    // Deux heures d'arrêt : le C++ calcule l'eau et l'huile, entre l'air et le moteur chaud ; les jauges les montrent.
    await panel.getByTestId('start-temp').getByRole('button', { name: '2 h' }).click();
    await expect(page.getByTestId('open-car-summary')).toContainText('départ : 2 h');
    expect(await page.evaluate(() => location.hash)).toContain('start=two_hours');
    await expect.poll(() => spec(page).then((s) => s.startCoolantC)).toBeGreaterThan(-20);
    const parked = await spec(page);
    expect(parked.startCoolantC).toBeLessThan(60);
    expect(parked.startOilC).toBeGreaterThan(-20);
    expect(parked.startOilC).toBeLessThan(60);
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBe(Math.round(parked.startCoolantC));
    await expect.poll(() => shown(page, 'gauge-oil-value')).toBe(Math.round(parked.startOilC));
    await expect(panel.getByTestId('start-state')).toContainText(water(Math.round(parked.startCoolantC)));
    // Une demi-heure : plus chaud encore, moins que le moteur qui vient de rouler.
    await panel.getByTestId('start-temp').getByRole('button', { name: '30 min' }).click();
    await expect.poll(() => spec(page).then((s) => s.startCoolantC)).toBeGreaterThan(parked.startCoolantC + 20);
    expect((await spec(page)).startCoolantC).toBeLessThan(90);
    if (SHOTS) {
      await page.setViewportSize({ width: 1600, height: 1000 });
      await panel.getByTestId('start-temp').screenshot({ path: `${SHOTS}/depart-volet-large.png` });
      await page.screenshot({ path: `${SHOTS}/depart-page-large.png` });
      await page.setViewportSize({ width: 360, height: 800 });
      await panel.getByTestId('start-temp').scrollIntoViewIfNeeded();
      await panel.getByTestId('start-temp').screenshot({ path: `${SHOTS}/depart-volet-360.png` });
    }
    // Le moteur démarre de là.
    await page.keyboard.press('Escape');
    await start(page);
    const t = await telemetry(page);
    expect(Math.abs(t.coolantC - (await spec(page)).startCoolantC)).toBeLessThan(5);
    expect(errors).toEqual([]);
  });

  test('moteur coupé, son suspendu : les jauges refroidissent, et le moteur repart de la température atteinte', async ({ page }) => {
    const errors = await open(page, { startTemp: 'warm' });
    await start(page);
    // Couper le contact : le moteur finit de tourner, puis le son se suspend, et le modèle avec lui.
    await page.getByTestId('start').click();
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.engine.rest !== null), { timeout: 10_000 }).toBe(true);
    const stopped = await page.evaluate(() => (window as any).__revforge.engine.rest);
    expect(stopped.coolantC).toBeGreaterThan(85);
    // Une heure plus tard pour les jauges : le C++ de la page, même réseau, moteur coupé.
    const later = await page.evaluate(() => (window as any).__revforge.engine.restingThermal(performance.now() + 3600e3));
    expect(later.coolantC).toBeLessThan(stopped.coolantC - 15);
    expect(later.coolantC).toBeGreaterThan(30);
    // Le temps passé suspendu : on le recule d'une heure, et le worklet le rattrape en reprenant.
    await page.evaluate(() => {
      const engine = (window as any).__revforge.engine;
      engine.rest.since -= 3600e3;
      engine.restShown = null;
    });
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBeLessThan(stopped.coolantC - 15);
    await start(page);
    const t = await telemetry(page);
    expect(t.coolantC).toBeLessThan(stopped.coolantC - 15);
    expect(Math.abs(t.coolantC - later.coolantC)).toBeLessThan(3);
    expect(errors).toEqual([]);
  });

  test('changer l’état au départ, moteur coupé, remet le moteur à l’état choisi', async ({ page }) => {
    const errors = await open(page, { startTemp: 'warm', ambientC: 5 });
    await start(page);
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.engine.rest !== null), { timeout: 10_000 }).toBe(true);
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBeGreaterThan(80);
    // Une nuit dehors : le moteur est à l'air, quoi qu'il ait fait avant.
    await ouvrir(page, 'car');
    await page.getByTestId('panel-car').getByTestId('start-temp').getByRole('button', { name: 'Une nuit' }).click();
    await page.keyboard.press('Escape');
    await expect.poll(() => shown(page, 'gauge-coolant-value')).toBe(5);
    await start(page);
    expect((await telemetry(page)).coolantC).toBeLessThan(7);
    expect(errors).toEqual([]);
  });
});
