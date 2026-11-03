import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';
import { ambientAirOf, isaPressurePa } from '../src/lib/atmosphere';
import { GasNetwork, P_ATM, T_AMB, type GasInputs } from '../src/lib/fluidGas';
import type { EngineConfig } from '../src/lib/config';

/**
 * L'air du jour (voir « L'air ambiant » dans docs/modele.md) : la même atmosphère qu'en C++, le gaz à l'écran qui s'ouvre
 * sur elle, et ce que la page en montre. Dans l'air de référence, rien ne change.
 */
const NATURAL = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const shots = process.env.ATMO_SHOTS;

async function chevaux(page: Page): Promise<number> {
  const text = (await page.getByTestId('spec-hp').textContent()) ?? '';
  return Number(text.match(/\d+/)?.[0]);
}

// ── Sans navigateur ──

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 5500, load: 1, boost: 0, release: 0, burning: true, heat: 0.7 };

/** Les pressions moyennes, pied au plancher (Pa) : le plénum, la bouche de l'admission, la sortie de la ligne. */
function plenumPa(config: EngineConfig): { net: GasNetwork; mean: number; mouth: number; exit: number } {
  const net = new GasNetwork();
  net.build(config, ORDER4);
  let cycle = 0;
  let sum = 0;
  let mouth = 0;
  let exit = 0;
  let count = 0;
  for (let t = 0; t < 0.12; t += FRAME) {
    const turns = (WIDE_OPEN.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, WIDE_OPEN);
    cycle = (cycle + turns) % 1;
    if (t > 0.04) {
      sum += net.zP[net.zonePlenum];
      mouth += net.p[net.intakeFirst];
      exit += net.exitP;
      count++;
    }
  }
  return { net, mean: sum / count, mouth: mouth / count, exit: exit / count };
}

test('l’atmosphère type est celle du C++, exactement l’ancien air à 20 °C et 0 m', () => {
  const reference = ambientAirOf({});
  expect(reference.pressurePa).toBe(P_ATM);
  expect(reference.temperatureK).toBe(T_AMB);
  expect(reference.soundSpeedMs).toBe(343);
  // Atmosphère type internationale, Pa.
  for (const [altitude, pressure] of [[1000, 89874.6], [2000, 79495.2], [3000, 70108.5], [4500, 57728.3]]) {
    expect(Math.abs(isaPressurePa(altitude) / pressure - 1)).toBeLessThan(2e-5);
  }
  expect(ambientAirOf({ ambientC: -30 }).temperatureK).toBe(243);
});

test('le gaz à l’écran s’ouvre sur l’air du jour', () => {
  const sea = plenumPa(BASE);
  expect(sea.net.pAtm).toBe(P_ATM);
  expect(sea.net.tAmb).toBe(T_AMB);
  const high = plenumPa({ ...BASE, altitudeM: 3000 });
  expect(high.net.pAtm).toBeCloseTo(isaPressurePa(3000), 6);
  // Pied au plancher, le plénum suit l'atmosphère : la même part d'elle, à quelques pour cent.
  expect(high.mean / sea.mean).toBeGreaterThan(0.69 * 0.95);
  expect(high.mean / sea.mean).toBeLessThan(0.69 * 1.05);
  // L'air entre par la bouche, et le gaz sort de la ligne, à l'atmosphère du jour.
  for (const [here, there] of [[high.mouth, sea.mouth], [high.exit, sea.exit]]) {
    expect(here / there).toBeGreaterThan(0.69 * 0.97);
    expect(here / there).toBeLessThan(0.69 * 1.03);
  }
  // Dans l'air froid, l'admission est froide aussi.
  const cold = plenumPa({ ...BASE, ambientC: -25 });
  expect(cold.net.inletK).toBe(248);
});

// ── Dans la page ──

test.describe('les conditions', () => {
  test('l’altitude se lit dans le volet de la voiture, et se paie sur la fiche', async ({ page }) => {
    await page.goto(`./?test#m=${NATURAL}`);
    await ouvrir(page, 'car');
    const air = page.getByTestId('ambient-air');
    await air.scrollIntoViewIfNeeded();
    await expect(air).toContainText(/1\s?013 hPa/);
    await expect(air).toContainText('100 %');
    const sea = await chevaux(page);

    await page.goto('about:blank');
    await page.goto(`./?test#m=${NATURAL}~~alt=3000,amb=5`);
    await ouvrir(page, 'car');
    await page.getByTestId('ambient-air').scrollIntoViewIfNeeded();
    await expect(page.getByTestId('ambient-air')).toContainText('701 hPa');
    await expect(page.getByTestId('ambient-air')).toContainText('son à 334 m/s');
    await expect(page.getByTestId('open-car-summary')).toContainText(/3\s?000/);
    // Pression en moins, un peu d'air froid en plus : ce que donne le calcul, pas un pourcentage écrit quelque part.
    await expect.poll(() => chevaux(page)).toBeLessThan(sea * 0.72);
    expect(await chevaux(page)).toBeGreaterThan(sea * 0.6);
    if (shots) await page.getByTestId('section-conditions').screenshot({ path: `${shots}/conditions.png` });
  });

  test('sur téléphone, la ligne de l’air tient dans la largeur', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 760 });
    await page.goto(`./?test#m=${NATURAL}~~alt=4500,amb=-30`);
    await ouvrir(page, 'car');
    const air = page.getByTestId('ambient-air');
    await air.scrollIntoViewIfNeeded();
    const box = await air.boundingBox();
    expect(box!.x).toBeGreaterThanOrEqual(0);
    expect(box!.x + box!.width).toBeLessThanOrEqual(360);
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(360);
    if (shots) await page.getByTestId('section-conditions').screenshot({ path: `${shots}/conditions_tel.png` });
  });
});
