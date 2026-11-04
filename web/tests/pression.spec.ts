import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */
const TURBO4 = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
// La même voiture, régulée par le calculateur : le réglage passe par le groupe du socle, après un troisième `~`.
const ELECTRONIC = `${TURBO4}~~bc=electronic`;
const CHARGER = '4.inline.2000.84.6500.sport.supercharger.petrol.0.off.dump_valve~compact.1400.18.front.1';
// Les captures ne servent qu'à regarder : seulement si l'on demande où les écrire.
const SHOTS = process.env.REVFORGE_SHOTS;

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

const tele = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const link = (page: Page) => page.evaluate(() => (window as any).__revforge.encodeConfig((window as any).__revforge.config()));

test.describe('la régulation de pression', () => {
  test('le choix ne se montre que sur un turbo, se résume sur le bouton et passe dans le lien', async ({ page }) => {
    const errors = await open(page, TURBO4);
    await ouvrir(page, 'engine');
    const choice = page.getByTestId('panel-engine').getByTestId('boost-control');
    await expect(choice).toBeVisible();
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('régulation');
    await choice.getByRole('button', { name: 'Électronique' }).click();
    await expect(choice).toContainText('pression absolue');
    await expect(page.getByTestId('open-engine-summary')).toContainText('régulation : Électronique');
    expect(await link(page)).toContain('bc=electronic');
    await choice.getByRole('button', { name: 'Selon le rapport' }).click();
    await expect(choice).toContainText('pneus');
    expect(await link(page)).toContain('bc=by_gear');
    expect(errors).toEqual([]);

    // Un compresseur n'a pas de wastegate : rien à régler, rien à lire.
    await open(page, CHARGER);
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('panel-engine').getByTestId('boost-control')).toHaveCount(0);
  });

  test('ressort : pas d\'électrovanne, et la cible est la pression réglée au-dessus de l\'air', async ({ page }) => {
    const errors = await open(page, TURBO4);
    await page.getByTestId('start').click();
    await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
    const t = await tele(page);
    expect(t.boostDuty).toBe(-1);
    expect(t.boostTargetBar).toBeCloseTo(1.3 + 1.01325, 3);
    expect(t.boostChargeBar).toBeGreaterThan(0.9);
    await expect(page.getByTestId('gauge-boost-duty')).toHaveCount(0);
    expect(errors).toEqual([]);
  });

  test('électronique : l\'électrovanne travaille, et le graphique montre la cible à côté de la pression', async ({ page }) => {
    const errors = await open(page, ELECTRONIC);
    await page.getByTestId('start').click();
    await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.turboRpm), { timeout: 8000 }).toBeGreaterThan(30000);
    const t = await tele(page);
    expect(t.boostDuty).toBeGreaterThanOrEqual(0);
    expect(t.boostDuty).toBeLessThanOrEqual(1);
    // Une pression absolue : celle de la calibration, au niveau de la mer.
    expect(t.boostTargetBar).toBeCloseTo(1.3 + 1.01325, 3);
    await expect(page.getByTestId('gauge-boost-duty')).toContainText('électrovanne');
    await page.keyboard.up('Space');

    await page.getByTestId('charts-toggle').click();
    await page.getByTestId('charts-body').getByLabel('Pression du turbo').check();
    const chart = page.getByTestId('chart-boost');
    await expect(chart).toBeVisible();
    await expect(chart.locator('.legend li')).toHaveCount(2);
    await expect(chart.locator('.legend')).toContainText('Cible');
    // Relative à l'atmosphère, comme la pression réelle sur le même axe.
    await expect(chart.locator('.legend')).toContainText('1,3 bar');
    if (SHOTS) {
      await chart.scrollIntoViewIfNeeded();
      await page.screenshot({ path: `${SHOTS}/pression-graphique.png`, fullPage: false });
      await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/pression-jauges.png` });
    }
    expect(errors).toEqual([]);
  });

  test('à 360 px, la jauge et le graphique restent lisibles', async ({ page }) => {
    test.skip(!SHOTS, 'une capture à regarder, rien à vérifier de plus');
    await page.setViewportSize({ width: 360, height: 780 });
    await open(page, ELECTRONIC);
    await page.getByTestId('start').click();
    await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.turboRpm), { timeout: 8000 }).toBeGreaterThan(30000);
    await page.keyboard.up('Space');
    await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/pression-jauges-360.png` });
    await page.getByTestId('charts-toggle').click();
    await page.getByTestId('charts-body').getByLabel('Pression du turbo').check();
    await page.getByTestId('chart-boost').screenshot({ path: `${SHOTS}/pression-graphique-360.png` });
  });
});
