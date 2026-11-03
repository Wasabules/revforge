import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/**
 * L'injection, sur la page : les chiffres calculés dans le volet de l'alimentation, là où ils ont un sens, et la jauge du
 * filtre à particules, qui lit la simulation. Voir « L'injection » dans docs/modele.md.
 */
const SHOTS = process.env.REVFORGE_SHOTS;
const BASE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const DIESEL = '4.inline.2000.85.4500.stock.turbo.diesel.0.off.dump_valve~compact.1350.18.front.1';

async function open(page: Page, hash = BASE) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

/** Un nombre lu dans un texte français : « 14,5 » ou « 1 600 ». */
const number = (text: string, after: RegExp) => Number((text.match(after)?.[1] ?? 'NaN').replace(/\s/g, '').replace(',', '.'));

test.describe('l’injection, dans le volet de l’alimentation', () => {
  test('une injection directe d’essence : la charge refroidie, la rampe, la recirculation et le filtre, chiffrés', async ({ page }) => {
    const errors = await open(page);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    await expect(panel.getByTestId('direct-figures')).toHaveCount(0);
    await panel.getByTestId('fuel-system').getByRole('button', { name: 'Injection directe' }).click();
    const direct = panel.getByTestId('direct-figures');
    await expect(direct).toContainText('Charge refroidie de');
    const text = (await direct.textContent()) ?? '';
    // De 10 à 20 K, de 2 à 9 % d'air en plus, 200 bar à pleine charge.
    expect(number(text, /de (\d+)\s*K/)).toBeGreaterThanOrEqual(10);
    expect(number(text, /de (\d+)\s*K/)).toBeLessThanOrEqual(20);
    expect(number(text, /: ([\d,]+)\s*%/)).toBeGreaterThan(2);
    expect(number(text, /: ([\d,]+)\s*%/)).toBeLessThan(9);
    await expect(panel.getByTestId('egr-figures')).toHaveCount(0);
    await panel.getByTestId('egr').getByRole('button', { name: 'Oui' }).click();
    await expect(panel.getByTestId('egr-figures')).toContainText('15');
    await panel.getByTestId('dpf').getByRole('button', { name: 'Oui' }).click();
    await expect(panel.getByTestId('dpf-figures')).toContainText('kPa');
    // Le filtre a sa jauge, qui lit la simulation.
    await expect(page.getByTestId('gauge-bank').getByTestId('gauge-dpf')).toHaveCount(1);
    // Une injection indirecte n'a pas de filtre, même si le réglage reste posé : ni commande, ni jauge.
    await panel.getByTestId('fuel-system').getByRole('button', { name: 'Injection', exact: true }).click();
    await expect(panel.getByTestId('dpf')).toHaveCount(0);
    await expect(page.getByTestId('gauge-dpf')).toHaveCount(0);
    expect(errors).toEqual([]);
  });

  test('un diesel : la pompe mécanique, puis la rampe commune, qui donne du couple en bas et claque moins', async ({ page }) => {
    const errors = await open(page, DIESEL);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    const figures = panel.getByTestId('diesel-injection-figures');
    await expect(figures).toContainText('La pompe atteint');
    await panel.getByTestId('diesel-injection').getByRole('button', { name: 'Rampe commune' }).click();
    await expect(figures).toContainText('de couple en plus');
    const text = (await figures.textContent()) ?? '';
    expect(number(text, /Rampe à ([\d\s]+)\s*bar/)).toBeGreaterThan(1000);
    expect(number(text, /: (\d+)\s*% de couple/)).toBeGreaterThan(10);
    expect(number(text, /de ([\d,]+)\s*dB/)).toBeGreaterThan(3);
    await panel.getByTestId('dpf').getByRole('button', { name: 'Oui' }).click();
    await expect(panel.getByTestId('dpf-figures')).toContainText('avant une régénération');
    if (SHOTS) {
      await page.screenshot({ path: `${SHOTS}/injection_diesel_large.png` });
      await page.setViewportSize({ width: 360, height: 760 });
      await panel.getByTestId('section-fueling').scrollIntoViewIfNeeded();
      await page.screenshot({ path: `${SHOTS}/injection_diesel_tel.png` });
    }
    expect(errors).toEqual([]);
  });

  test('la jauge du filtre suit la suie, moteur en marche', async ({ page }) => {
    const errors = await open(page, DIESEL);
    await ouvrir(page, 'engine');
    await page.getByTestId('panel-engine').getByTestId('dpf').getByRole('button', { name: 'Oui' }).click();
    await page.getByTestId('panel-engine-close').click();
    await page.getByTestId('start').click();
    // À mi-chemin entre deux régénérations : l'aiguille vers 50 %, et une contre-pression lue de la simulation.
    await expect(page.getByTestId('gauge-dpf')).toHaveAttribute('aria-valuenow', /^(4\d|5\d)/, { timeout: 15000 });
    await expect(page.getByTestId('gauge-dpf-state')).toContainText('kPa');
    if (SHOTS) await page.getByTestId('gauge-bank').screenshot({ path: `${SHOTS}/injection_jauge.png` });
    expect(errors).toEqual([]);
  });
});
