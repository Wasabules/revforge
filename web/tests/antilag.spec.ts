import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { ANTI_LAG_CODE, GasNetwork, antiLagFires, type GasInputs } from '../src/lib/fluidGas';

/* eslint-disable @typescript-eslint/no-explicit-any */
const TURBO4 = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
// La même voiture, chaque système : le lien garde sa place dans le groupe de la voiture.
const withAntiLag = (kind: string) => `${TURBO4}_front_standard_single_medium_roots_${kind}_400_dohc_stock`;
const DIESEL = '4.inline.1995.84.4500.stock.turbo.diesel.0.off.dump_valve~compact.1500.17.front.1.8_front_standard_single_medium_roots_retard_400_dohc_stock';

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1.3, turboSize: 'medium', electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
// Ce que `TurboCharger` calcule pied levé à 4000 tr/min, à mi-pression.
const LIFTED: GasInputs = { rpm: 4000, load: 0.3, boost: 0.3, release: 0, burning: true, heat: 0.7, compressorRatio: 1.4, compressorOutK: 330, wastegate: 0 };

function run(net: GasNetwork, inputs: GasInputs, seconds: number, each?: () => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (inputs.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, inputs);
    cycle = (cycle + turns) % 1;
    each?.();
  }
}

const settled = (inputs: GasInputs) => {
  const net = new GasNetwork();
  net.build(BASE, ORDER4);
  run(net, inputs, 0.4);
  return net;
};

/** La température du gaz au collecteur, moyennée sur quelques cycles. */
function collectorK(inputs: GasInputs): number {
  const net = settled(inputs);
  let sum = 0;
  let n = 0;
  run(net, inputs, 0.1, () => {
    sum += net.zT[net.zoneJoin];
    n++;
  });
  return sum / n;
}

test.describe('l\'anti-lag, dans le gaz', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le retard n\'est pas écrit : l\'étincelle part tard, et le gaz arrive plus chaud au collecteur', () => {
    const normal = collectorK(LIFTED);
    const late = collectorK({ ...LIFTED, antiLag: ANTI_LAG_CODE.retard });
    expect(late).toBeGreaterThan(normal + 40);
  });

  test('la coupure laisse passer du mélange imbrûlé, et la flamme du collecteur le chauffe', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    run(net, LIFTED, 0.2);
    const before = net.burntBlowdowns;
    const allBefore = net.blowdowns;
    run(net, { ...LIFTED, antiLag: ANTI_LAG_CODE.cut }, 0.3);
    const burnt = net.burntBlowdowns - before;
    const all = net.blowdowns - allBefore;
    // Au rythme de `antiLagFires`, à un cycle près : une part fixe des bouffées, pas une sur deux.
    expect(burnt / all).toBeGreaterThan(0.3);
    expect(burnt / all).toBeLessThan(0.5);
    const cold = collectorK({ ...LIFTED, antiLag: ANTI_LAG_CODE.cut, antiLagHeatW: 0 });
    const hot = collectorK({ ...LIFTED, antiLag: ANTI_LAG_CODE.cut, antiLagHeatW: 20000 });
    expect(hot).toBeGreaterThan(cold + 100);
  });

  test('l\'injection d\'air prend l\'air après l\'échangeur et le passe au collecteur', () => {
    const inputs = { ...LIFTED, antiLag: ANTI_LAG_CODE.air, antiLagHeatW: 15000, antiLagAirFlow: 0.025 };
    const net = settled(inputs);
    const without = settled(LIFTED);
    expect(net.antiLagAirNow).toBeGreaterThan(0.01);
    expect(without.antiLagAirNow).toBe(0);
    // Plus de gaz à évacuer : la pression monte devant la turbine, et il y fait plus chaud.
    expect(net.zP[net.zoneJoin]).toBeGreaterThan(without.zP[net.zoneJoin]);
    expect(net.zT[net.zoneJoin]).toBeGreaterThan(without.zT[net.zoneJoin]);
    const values: number[] = [...net.temp, ...net.p];
    expect(values.every(Number.isFinite)).toBe(true);
  });

  test('sans anti-lag, rien ne change', () => {
    const plain = settled(LIFTED);
    const zero = settled({ ...LIFTED, antiLag: 0, antiLagHeatW: 0, antiLagAirFlow: 0 });
    expect([...zero.p]).toEqual([...plain.p]);
  });

  test('le motif de la coupure est celui du C++ : deux explosions sur cinq', () => {
    const fired = [0, 1, 2, 3, 4].map(antiLagFires);
    expect(fired).toEqual([true, false, false, true, false]);
  });

  test('rien n\'alloue de plus à chaque pas', () => {
    const grownPerFrame = (inputs: GasInputs) => {
      const net = settled(inputs);
      let cycle = 0;
      let previous = process.memoryUsage().heapUsed;
      let grown = 0;
      for (let i = 0; i < 600; i++) {
        const turns = (inputs.rpm / 120) * FRAME;
        net.advance(FRAME, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
        const now = process.memoryUsage().heapUsed;
        if (now > previous) grown += now - previous;
        previous = now;
      }
      return grown / 600;
    };
    const plain = grownPerFrame(LIFTED);
    const air = grownPerFrame({ ...LIFTED, antiLag: ANTI_LAG_CODE.air, antiLagHeatW: 15000, antiLagAirFlow: 0.025 });
    const cut = grownPerFrame({ ...LIFTED, antiLag: ANTI_LAG_CODE.cut, antiLagHeatW: 15000 });
    expect(air).toBeLessThan(plain + 300);
    expect(cut).toBeLessThan(plain + 300);
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

test.describe('à l\'écran', () => {
  test('le choix ne se montre que sur un turbo à essence, et se résume sur le bouton', async ({ page }) => {
    await open(page, TURBO4);
    await ouvrir(page, 'engine');
    const choice = page.getByRole('group', { name: 'Anti-lag' });
    await expect(choice).toBeVisible();
    await expect(page.getByTestId('antilag-help')).toContainText('injection se coupe');
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Retard');
    await choice.getByRole('button', { name: 'Retard d\'allumage' }).click();
    await expect(page.getByTestId('antilag-help')).toContainText('après le point mort haut');
    await expect(page.getByTestId('antilag-help')).toContainText('usure n\'est pas simulée');
    await expect(page.getByTestId('open-engine-summary')).toContainText('Retard d\'allumage');
    expect(page.url()).toContain('_retard_');
    await page.getByRole('button', { name: 'Atmosphérique' }).click();
    await expect(choice).toBeHidden();
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Retard');
  });

  test('sur un diesel, le choix n\'existe pas, même si le lien le demande', async ({ page }) => {
    await open(page, DIESEL);
    await ouvrir(page, 'engine');
    await expect(page.getByRole('group', { name: 'Taille du turbo' })).toBeVisible();
    await expect(page.getByRole('group', { name: 'Anti-lag' })).toBeHidden();
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Retard');
    await expect(page.getByTestId('live-antilag')).toBeHidden();
  });

  test('l\'indicateur lit la simulation : rien pied posé, allumé pied levé, et l\'arbre reste lancé', async ({ page }) => {
    const errors = await open(page, withAntiLag('retard'));
    await page.getByTestId('start').click();
    await expect(page.getByTestId('start')).toHaveText('Couper le contact');
    await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
    await expect(page.getByTestId('live-antilag')).toBeHidden();
    expect((await tele(page)).antiLag).toBe(0);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.turboRpm), { timeout: 8000 }).toBeGreaterThan(30000);
    await page.keyboard.up('Space');
    await expect.poll(() => tele(page).then((t) => t.antiLag), { timeout: 4000 }).toBe(1);
    await expect(page.getByTestId('live-antilag')).toHaveText('Anti-lag en marche');
    const during = await tele(page);
    expect(during.antiLagLostNm).toBeGreaterThan(0);
    expect(during.antiLagHeatKw).toBeGreaterThan(0);
    await voir(page, 'gas');
    await expect(page.getByTestId('turbo-readout')).toContainText('avant la turbine');
    // Le moteur retombe au ralenti : l'anti-lag s'arrête de lui-même.
    await expect.poll(() => tele(page).then((t) => t.antiLag), { timeout: 15000 }).toBe(0);
    await expect(page.getByTestId('live-antilag')).toBeHidden();
    expect(errors).toEqual([]);
  });

  test('sans anti-lag, pas d\'indicateur', async ({ page }) => {
    await open(page, TURBO4);
    await expect(page.getByTestId('live-boost')).toBeVisible();
    await expect(page.getByTestId('live-antilag')).toBeHidden();
  });
});
