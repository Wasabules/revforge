import { expect, test, type Page } from '@playwright/test';

declare global {
  interface Window {
    __revforge: {
      engine: { level(): number };
      telemetry(): { rpm: number; speed: number; gear: number; carRpm: number; peak: number };
    };
  }
}

const telemetry = (page: Page) => page.evaluate(() => window.__revforge.telemetry());
const level = (page: Page) => page.evaluate(() => window.__revforge.engine.level());

async function start(page: Page) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

test('démarre, fait du son, monte en régime et se coupe', async ({ page }) => {
  const errors = await start(page);
  await expect.poll(() => level(page), { timeout: 5000 }).toBeGreaterThan(0.005);

  const idle = (await telemetry(page)).rpm;
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(idle + 2000);
  await page.keyboard.up('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeLessThan(idle + 200);

  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  expect(errors).toEqual([]);
});

test('la pédale à la souris fait monter le régime', async ({ page }) => {
  await start(page);
  const pedal = page.getByRole('button', { name: 'Accélérateur' });
  const box = (await pedal.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + 8);
  await page.mouse.down();
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(2500);
  await page.mouse.up();
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeLessThan(1500);
});

test('conduite : la voiture avance et passe les rapports, le frein l\'arrête', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 15000 }).toBeGreaterThanOrEqual(2);
  expect((await telemetry(page)).speed).toBeGreaterThan(10);
  await page.keyboard.up('Space');
  await page.keyboard.down('b');
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeLessThan(0.5);
  await page.keyboard.up('b');
});

test('balade : le pilote automatique démarre seul', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Balade', exact: true }).click();
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeGreaterThan(10);
});

test('tous les préréglages jouent, sans erreur ni silence', async ({ page }) => {
  const errors = await start(page);
  const select = page.getByTestId('preset');
  const ids = await select.locator('option').evaluateAll((options) => options.map((option) => (option as HTMLOptionElement).value));
  expect(ids.length).toBeGreaterThanOrEqual(15);
  for (const id of ids) {
    await select.selectOption(id);
    await expect.poll(() => level(page), { message: id, timeout: 5000 }).toBeGreaterThan(0.002);
  }
  expect(errors).toEqual([]);
});

test('régler un moteur crée « Mon moteur », et le lien le restitue', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Ligne directe' }).click();
  await page.getByRole('button', { name: 'Turbo', exact: true }).click();
  await page.getByRole('button', { name: 'Flutter' }).click();
  await page.getByRole('button', { name: '8', exact: true }).click();
  await expect(page.getByTestId('preset')).toHaveValue('custom');
  await expect(page.getByTestId('summary')).toContainText('8 cylindres');
  const url = page.url();
  expect(url).toContain('#m=8.');

  const other = await page.context().newPage();
  await other.goto(url);
  await expect(other.getByTestId('summary')).toContainText('8 cylindres');
  await expect(other.getByRole('button', { name: 'Flutter' })).toHaveAttribute('aria-pressed', 'true');
  await expect(other.getByRole('button', { name: 'Ligne directe' })).toHaveAttribute('aria-pressed', 'true');
  await other.close();
});

test('un lien invalide retombe sur un moteur par défaut', async ({ page }) => {
  await page.goto('./#m=abc.def');
  await expect(page.getByTestId('preset')).toHaveValue('four_cyl_sport');
});

test('le multiplicateur change réellement la courbe', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(page.getByTestId('preview')).toContainText('tr/min');
  const before = await page.getByTestId('preview').textContent();
  const slider = page.getByRole('slider', { name: /Multiplicateur/ });
  await slider.fill('3');
  const after = await page.getByTestId('preview').textContent();
  expect(after).not.toEqual(before);
});

test('exporte un WAV valide', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const [download] = await Promise.all([page.waitForEvent('download'), page.getByTestId('export-blips').click()]);
  expect(download.suggestedFilename()).toMatch(/^revforge-.*coups-de-gaz\.wav$/);
  const path = await download.path();
  const { readFileSync } = await import('node:fs');
  const bytes = readFileSync(path);
  expect(bytes.subarray(0, 4).toString()).toBe('RIFF');
  expect(bytes.length).toBe(44 + 44100 * 14 * 2);
  const samples = new Int16Array(bytes.buffer, bytes.byteOffset + 44, 44100 * 14);
  expect(Math.max(...samples.slice(44100, 88200))).toBeGreaterThan(500);
});

test('la pédale est visible sans défiler, même sur un petit écran', async ({ page }) => {
  for (const [width, height] of [[1280, 720], [390, 844]]) {
    await page.setViewportSize({ width, height });
    await page.goto('./');
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    const box = (await page.getByRole('button', { name: 'Accélérateur' }).boundingBox())!;
    expect(box.y + box.height).toBeLessThanOrEqual(height);
  }
});

test('aucun débordement horizontal sur téléphone', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 740 });
  await page.goto('./');
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});
