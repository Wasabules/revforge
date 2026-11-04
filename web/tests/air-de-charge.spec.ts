import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { engineLayout } from '../src/lib/engineLayout';
import { GasNetwork } from '../src/lib/fluidGas';
import { turboChain } from '../src/lib/turboChain';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * L'air de charge (core/src/charge_air.cpp) : l'échangeur change le chemin de l'air à l'écran comme il change `chargeVolumeM3`
 * en C++, et le protoxyde a sa bouteille, lue de la simulation.
 */
const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', turboPipeMm: 400, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const TURBO4 = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
const NATURAL = '4.inline.1998.86.7300.sport.natural.petrol.0.off.dump_valve~compact.1250.17.front.1';

/** Le volume entre le compresseur et le papillon, à l'écran : tubes et échangeur, sans la boîte à air ni le conduit d'entrée. */
function chargeVolume(net: GasNetwork): number {
  let volume = 0;
  for (const part of net.intakeParts) {
    if (part.kind !== 'hot' && part.kind !== 'cooler' && part.kind !== 'cold') continue;
    for (let k = 0; k < part.count; k++) volume += net.area[part.first + k] * net.cell;
  }
  return volume;
}

test.describe('l\'air de charge, sans navigateur', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le chemin de l\'air suit l\'échangeur, du même volume qu\'en C++', () => {
    const build = (config: Partial<EngineConfig>) => {
      const net = new GasNetwork();
      net.build({ ...BASE, ...config } as EngineConfig, ORDER4);
      return net;
    };
    const stock = build({});
    const water = build({ intercooler: 'air_to_water' });
    const none = build({ intercooler: 'none' });
    // Au défaut, le même réseau qu'avant le réglage.
    const { intercooler: _unused, ...old } = { ...BASE, intercooler: 'air_to_air' } as EngineConfig;
    const before = new GasNetwork();
    before.build(old as EngineConfig, ORDER4);
    expect(stock.n).toBe(before.n);
    expect(stock.coolerCount).toBeGreaterThan(0);
    // `ChargePath` : 2,75 conduits sur l'air-air, 1,65 sur l'air-eau, 0,8 sans échangeur, à la discrétisation près.
    expect(chargeVolume(water) / chargeVolume(stock)).toBeCloseTo(1.65 / 2.75, 1);
    expect(chargeVolume(none) / chargeVolume(stock)).toBeCloseTo(0.8 / 2.75, 1);
    expect(none.coolerCount).toBe(0);
    expect(none.intakeParts.some((part) => part.kind === 'cooler')).toBe(false);
    // Le compresseur mécanique, sans échangeur : son tube seul.
    const charger = build({ aspiration: 'supercharger', intercooler: 'none' });
    expect(charger.intakeParts.map((part) => part.kind)).toEqual(['filter', 'inlet', 'hot', 'cold']);
  });

  test('sans échangeur, le gaz chaud arrive au papillon ; avec, il est refroidi', () => {
    const run = (intercooler: EngineConfig['intercooler']) => {
      const net = new GasNetwork();
      net.build({ ...BASE, intercooler } as EngineConfig, ORDER4);
      let cycle = 0;
      const frame = 1 / 60 / 12;
      const inputs = { rpm: 5500, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.3, compressorOutK: 413, wastegate: 0.5 };
      for (let t = 0; t < 1.2; t += frame) {
        const turns = (inputs.rpm / 120) * frame;
        net.advance(frame, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
      }
      return net.temp[net.intakeFirst + net.intakeCount - 1];
    };
    expect(run('none')).toBeGreaterThan(run('air_to_air') + 20);
  });

  test('les vues : pas d\'échangeur dessiné sans échangeur, une brique plus petite à l\'eau', () => {
    const stock = turboChain(engineLayout(BASE));
    const water = turboChain(engineLayout({ ...BASE, intercooler: 'air_to_water' }));
    const none = turboChain(engineLayout({ ...BASE, intercooler: 'none' }));
    expect(stock.hasCooler).toBe(true);
    expect(water.hasCooler).toBe(true);
    expect(none.hasCooler).toBe(false);
    expect(water.coolerSize[0]).toBeLessThan(stock.coolerSize[0]);
    // Sans échangeur, le tube chaud et le tube froid se rejoignent.
    expect(none.units[0].hotTo[1]).toBe(none.coldFrom[1]);
    expect(none.units[0].hotTo[2]).toBe(none.coldFrom[2]);
  });
});

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

test.describe('l\'air de charge, sur la page', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('la bouteille de protoxyde : sa jauge, son régime, elle se vide pied au plancher et se remplit', async ({ page }) => {
    const errors = await open(page, `${NATURAL}~~nos=75`);
    await expect(page.getByTestId('gauge-nitrous')).toBeVisible();
    await expect(page.getByTestId('gauge-charge-air')).toBeAttached();
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('nitrous-figures')).toContainText(/3\s000 tr\/min/);
    await page.getByTestId('start').click();
    await expect(page.getByTestId('start')).toHaveText('Couper le contact');
    await expect.poll(() => tele(page).then((t) => t.rpm), { timeout: 15000 }).toBeGreaterThan(500);
    const full = (await tele(page)).nitrousKg;
    expect(full).toBeCloseTo(4.54, 1);
    expect((await tele(page)).nitrousBar).toBeGreaterThan(40);
    // Pied au plancher au point mort : au-delà du régime d'activation, le kit s'ouvre et la charge refroidit.
    await page.keyboard.down('Space');
    // Au rupteur, le kit se ferme à chaque coupure : on lit la charge pendant qu'il débite.
    await expect.poll(() => tele(page).then((t) => (t.nitrousFlow > 5 ? t.chargeC : 99)), { timeout: 8000 }).toBeLessThan(15);
    await page.keyboard.up('Space');
    await expect.poll(() => tele(page).then((t) => t.nitrousFlow), { timeout: 4000 }).toBe(0);
    expect((await tele(page)).nitrousKg).toBeLessThan(full);
    // Le volet du moteur est encore ouvert, et la barre du bas peut passer devant : le bouton se déclenche directement.
    await page.getByTestId('nitrous-refill').dispatchEvent('click');
    await expect.poll(() => tele(page).then((t) => t.nitrousKg)).toBeCloseTo(full, 2);
    expect(errors).toEqual([]);
  });

  test('sans échangeur : la vue du gaz et la vue du moteur se dessinent, sans échangeur', async ({ page }) => {
    const errors = await open(page, `${TURBO4}~~ic=none`);
    await expect(page.getByTestId('open-engine-summary')).toContainText('Aucun');
    await voir(page, 'gas');
    await page.getByTestId('start').click();
    await expect.poll(() => tele(page).then((t) => t.rpm), { timeout: 15000 }).toBeGreaterThan(500);
    await page.waitForTimeout(1500);
    await voir(page, 'engine');
    await page.waitForTimeout(1500);
    expect(errors).toEqual([]);
  });
});
