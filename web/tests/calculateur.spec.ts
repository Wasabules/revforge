import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * Le calculateur (voir « Le calculateur » dans docs/modele.md) : ce qu'il fait se lit dans la télémétrie, rien n'est déduit
 * de la pédale ; sa carte se lit dans son volet, calculée par le C++.
 */
const BASE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const TURBO = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

async function start(page: Page) {
  await page.getByTestId('start').click();
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 15_000 }).toBeGreaterThan(500);
}

const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());

/** Le plus haut de chaque champ, relevé à chaque image pendant `ms`. */
const highest = (page: Page, keys: string[], ms: number) => page.evaluate(async ({ keys, ms }) => {
  const top: Record<string, number> = Object.fromEntries(keys.map((key) => [key, -Infinity]));
  const until = performance.now() + ms;
  while (performance.now() < until) {
    const now = (window as any).__revforge.telemetry();
    for (const key of keys) top[key] = Math.max(top[key], now[key]);
    await new Promise((resolve) => requestAnimationFrame(resolve));
  }
  return top;
}, { keys, ms });

test.describe('le calculateur', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('au rupteur, la coupure d\'allumage détone dans la ligne, celui d\'origine non', async ({ page }) => {
    const pops = async (hash: string) => {
      const errors = await open(page, hash);
      await start(page);
      const before = (await telemetry(page)).pops;
      await page.keyboard.down('Space');
      const top = await highest(page, ['ecuLimiter', 'pops'], 4000);
      await page.keyboard.up('Space');
      expect(top.ecuLimiter).toBe(1);
      expect(errors).toEqual([]);
      return top.pops - before;
    };
    expect(await pops(BASE)).toBe(0);
    expect(await pops(`${BASE}~~lim=ignition_cut`)).toBeGreaterThanOrEqual(3);
  });

  test('le launch control tient le régime à l\'arrêt, son voyant s\'allume, la pression monte, et il lâche au départ', async ({ page }) => {
    const errors = await open(page, `${TURBO}~~launch=4500`);
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.keyboard.down('KeyB');
    await page.keyboard.down('Space');
    await expect(page.getByTestId('live-launch')).toBeVisible({ timeout: 5000 });
    await page.waitForTimeout(2500);
    const held = await telemetry(page);
    expect(held.ecuLaunch).toBe(1);
    expect(held.speed).toBeLessThan(1);
    expect(held.rpm).toBeGreaterThan(4000);
    expect(held.rpm).toBeLessThan(4800);
    expect(held.boost).toBeGreaterThan(0.5);
    await page.keyboard.up('KeyB');
    await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 5000 }).toBeGreaterThan(20);
    expect((await telemetry(page)).ecuLaunch).toBe(0);
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  test('sa carte se lit dans le volet, calculée : 6° de retard retirent 3,4 % de couple et chauffent le gaz', async ({ page }) => {
    const figures = async (hash: string) => {
      await open(page, hash);
      await ouvrir(page, 'engine');
      return (await page.getByTestId('ecu-figures').textContent()) ?? '';
    };
    const stock = await figures(BASE);
    expect(stock).toContain('×1,000');
    const late = await figures(`${BASE}~~adv=-6`);
    // Intégration : le décalage passe par la carte du cliquetis (combustion), lue du C++ au régime de la fiche. Son cycle rend
    // son meilleur travail environ 9° plus tôt que son critère de meilleur couple : 6° de retard y coûtent 3,4 %, là où la
    // parabole de `sparkTorqueShare` en comptait 0,8 (1 − (6/68)²). Voir « Quand les réglages se rencontrent (2) ».
    expect(late).toContain('×0,966');
    const gas = (text: string) => text.match(/gaz d'échappement (\d[\d\s ]*) °C \((\d[\d\s ]*) °C/)!.slice(1).map((value) => Number(value.replace(/\D/g, '')));
    const [tuned, origin] = gas(late);
    expect(tuned).toBeGreaterThan(origin);
    expect(gas(stock)[0]).toBe(gas(stock)[1]);
  });

  test('un carburateur dose lui-même : le volet ne propose pas de richesse, et la remplace par une explication', async ({ page }) => {
    await open(page, `${BASE}_front_standard_single_medium_roots_off_400_dohc_stock_single`);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    await expect(panel.getByTestId('lambda-carb')).toBeVisible();
    await expect(panel.getByTestId('lambda')).toHaveCount(0);
    await expect(panel.getByTestId('advance')).toBeVisible();
  });
});
