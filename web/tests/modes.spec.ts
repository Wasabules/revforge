import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

interface Telemetry { rpm: number; gear: number; speed: number; shifting: number; boxPending: number; legKind: number; gradePct: number }
const telemetry = (page: Page) => page.evaluate(() => (window as unknown as { __revforge: { telemetry(): Telemetry } }).__revforge.telemetry());

const shift = (page: Page) => page.getByTestId('shift-mode');
const scenario = (page: Page) => page.getByTestId('drive-scenario');

async function open(page: Page, lang: 'fr' | 'en' = 'fr') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test&lang=${lang}`);
  await expect(page.getByTestId('start')).toBeEnabled();
  return errors;
}

async function start(page: Page) {
  await page.getByTestId('start').click();
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 10000 }).toBeGreaterThan(500);
  // Le démarreur a fini : le moteur tourne seul.
  await page.waitForTimeout(800);
}

/** Au plancher depuis l'arrêt : le régime au moment où la boîte quitte le premier rapport. */
async function firstUpshiftRpm(page: Page, pedal: 'full' | 'half'): Promise<number> {
  if (pedal === 'half') await page.keyboard.down('Shift');
  await page.keyboard.down('Space');
  const rpm = await page.evaluate(() => new Promise<number>((resolve, reject) => {
    const revforge = (window as unknown as { __revforge: { telemetry(): Telemetry } }).__revforge;
    let last = 0;
    const started = performance.now();
    const poll = () => {
      const t = revforge.telemetry();
      if (t.shifting && t.boxPending === 2) return resolve(Math.max(last, t.rpm));
      if (t.gear >= 2) return resolve(last);
      last = t.rpm;
      if (performance.now() - started > 25000) return reject(new Error('jamais passé en seconde'));
      requestAnimationFrame(poll);
    };
    poll();
  }));
  await page.keyboard.up('Space');
  await page.keyboard.up('Shift');
  return rpm;
}

test('le mode choisi change le régime où la boîte passe, en roulant', async ({ page }) => {
  const errors = await open(page);
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await expect(shift(page)).toHaveValue('normal');

  const measure = async () => {
    const rpm = await firstUpshiftRpm(page, 'half');
    // Retour à l'arrêt pour la mesure suivante.
    await page.keyboard.down('KeyB');
    await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 20000 }).toBeLessThan(1);
    await page.keyboard.up('KeyB');
    return rpm;
  };

  const normal = await measure();
  await shift(page).selectOption('eco');
  const eco = await measure();
  await shift(page).selectOption('track');
  const track = await measure();

  expect(eco).toBeLessThan(normal - 200);
  expect(track).toBeGreaterThan(normal + 500);
  expect(errors).toEqual([]);
});

test('aux palettes, les boutons apparaissent et le rapport suit', async ({ page }) => {
  const errors = await open(page);
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await expect(page.getByTestId('shift-up')).toHaveCount(0);
  await shift(page).selectOption('paddles');
  await expect(page.getByTestId('shift-up')).toBeVisible();
  await page.keyboard.down('Shift');
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 5000 }).toBe(1);
  await page.getByTestId('shift-up').click();
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 5000 }).toBe(2);
  await page.keyboard.up('Space');
  await page.keyboard.up('Shift');
  expect(errors).toEqual([]);
});

test('le trajet se choisit en roulant, et l\'étape en cours s\'affiche, en français et en anglais', async ({ page }) => {
  const errors = await open(page);
  await start(page);
  await expect(scenario(page)).toHaveCount(0);
  await page.getByRole('button', { name: 'Balade', exact: true }).click();
  await expect(scenario(page)).toHaveValue('city');
  await expect(page.getByTestId('drive-leg')).toContainText('Ville et voie rapide');

  // La voiture roule : elle garde son élan en changeant de trajet.
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeGreaterThan(20);
  const before = (await telemetry(page)).speed;
  await scenario(page).selectOption('mountain');
  expect((await telemetry(page)).speed).toBeGreaterThan(before * 0.6);
  await expect(page.getByTestId('drive-leg')).toContainText('Col de montagne');
  await expect(page.getByTestId('drive-leg')).toContainText('%', { timeout: 10000 });
  await expect.poll(() => telemetry(page).then((t) => t.gradePct), { timeout: 10000 }).toBeGreaterThan(1);

  await ouvrir(page, 'display');
  await page.getByTestId('lang-en').click();
  await expect(page.getByTestId('drive-leg')).toContainText('Mountain pass');
  await expect(page.getByTestId('drive-leg')).not.toContainText('Col de montagne');
  await expect(shift(page).locator('option', { hasText: 'Track' })).toHaveCount(1);
  // Le pilote automatique ne se sert pas des palettes.
  await expect(shift(page).locator('option[value="paddles"]')).toHaveCount(0);
  expect(errors).toEqual([]);
});

test('les choix disparaissent là où ils ne s\'appliquent pas', async ({ page }) => {
  await open(page);
  // Au point mort, ni l'un ni l'autre.
  await expect(shift(page)).toHaveCount(0);
  await expect(scenario(page)).toHaveCount(0);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await expect(shift(page)).toBeVisible();
  await expect(scenario(page)).toHaveCount(0);
  // En manuelle, pas de mode d'automatique.
  await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
  await expect(shift(page)).toHaveCount(0);
  await page.getByRole('button', { name: 'Automatique', exact: true }).click();
  await expect(shift(page)).toBeVisible();
  // En électrique, un seul rapport : pas de mode, mais des trajets.
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('electric');
  await expect(shift(page)).toHaveCount(0);
  await page.getByRole('button', { name: 'Balade', exact: true }).click();
  await expect(shift(page)).toHaveCount(0);
  await expect(scenario(page)).toBeVisible();
});

test('le lien de partage ne porte ni le mode ni le trajet', async ({ page }) => {
  await open(page);
  await page.getByRole('button', { name: 'Balade', exact: true }).click();
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('v8_cross_plane');
  const link = await page.evaluate(() => location.hash);
  await scenario(page).selectOption('track');
  await shift(page).selectOption('sport');
  expect(await page.evaluate(() => location.hash)).toBe(link);
});
