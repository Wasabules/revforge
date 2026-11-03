import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';

/* eslint-disable @typescript-eslint/no-explicit-any */
/**
 * Deux turbos, twincharger, aubes (`twin_turbo.cpp`) : la télémétrie de chaque arbre, le gaz à l'écran avec deux turbines et
 * deux chemins d'air, la jauge, le volet. Les vues 3D et 2D sont dans `biturbo-vues.spec.ts`.
 */
const SHOTS = process.env.REVFORGE_SHOTS;
const SIX_LINK = '6.inline.3000.86.6800.sport.turbo.petrol.0.off.dump_valve~coupe.1500.18.rear.1';
const link = (tuning: string) => `${SIX_LINK}~~${tuning}`;

const SIX: EngineConfig = {
  cylinders: 6, layout: 'inline', displacementCc: 3000, boreMm: 86, redlineRpm: 6800, exhaust: 'sport', aspiration: 'turbo', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, turboSize: 'medium', turboPipeMm: 400, electric: false, body: 'coupe', massKg: 1500, rimInch: 18, drive: 'rear',
  turboLayout: 'single',
} as EngineConfig;
const ORDER6 = [1, 5, 3, 6, 2, 4];
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 5000, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.0, compressorOutK: 400, wastegate: 0.3, compressorRatio2: 2.0, wastegate2: 0.3, twinValve: 1, secondaryOnline: true };

function drive(net: GasNetwork, seconds: number, inputs: GasInputs, each?: () => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (inputs.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, inputs);
    cycle = (cycle + turns) % 1;
    each?.();
  }
}

test.describe('deux turbos, le gaz sans navigateur', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('un turbo simple garde son réseau : ni seconde turbine, ni second chemin d\'air', () => {
    const single = new GasNetwork();
    single.build(SIX, ORDER6);
    const { turboLayout: _unused, ...old } = SIX;
    const before = new GasNetwork();
    before.build(old as EngineConfig, ORDER6);
    expect(single.hasTwin).toBe(false);
    expect(single.n).toBe(before.n);
    expect(single.banks).toBe(1);
    expect(single.intake2Count).toBe(0);
    expect(single.compressor2Cell).toBe(-1);
  });

  test('en parallèle : une turbine par moitié du moteur, deux chemins d\'air, et le gaz passe par les deux', () => {
    const net = new GasNetwork();
    net.build({ ...SIX, turboLayout: 'twin_parallel' }, ORDER6);
    expect(net.hasTwin).toBe(true);
    expect(net.banks).toBe(2);
    expect(net.frontRear).toBe(true);
    expect(net.intake2Count).toBe(net.intakeCount);
    expect(net.compressor2Cell).toBeGreaterThan(net.intakeFirst + net.intakeCount);
    // Les trois cylindres de l'avant d'un côté, les trois de l'arrière de l'autre.
    expect([...net.bank]).toEqual([0, 0, 0, 1, 1, 1]);
    // Le volume des deux chemins : celui de deux turbos d'une moitié de moteur, comme `chargeVolumeM3` en C++.
    const single = new GasNetwork();
    single.build(SIX, ORDER6);
    const chain = (n: GasNetwork, first: number, count: number) => {
      let v = 0;
      for (let i = first; i < first + count; i++) v += 1 / n.invV[i];
      return v;
    };
    const both = chain(net, net.intakeFirst, net.intakeCount) + chain(net, net.intake2First, net.intake2Count);
    const cpp = (v: number) => Math.min(Math.max(1.5e-3 + 2.5 * v, 2.5e-3), 14e-3);
    expect(both / chain(single, single.intakeFirst, single.intakeCount)).toBeCloseTo((2 * cpp(1.5e-3)) / cpp(3e-3), 1);
    drive(net, 0.5, WIDE_OPEN);
    let first = 0;
    let second = 0;
    let compressor2 = 0;
    drive(net, 0.2, WIDE_OPEN, () => {
      first += net.turbineFlow;
      second += net.turbineFlow2;
      compressor2 += net.compressorFlow2;
    });
    expect(first).toBeGreaterThan(0);
    expect(second).toBeGreaterThan(0);
    expect(second / first).toBeGreaterThan(0.7);
    expect(second / first).toBeLessThan(1.4);
    expect(compressor2).toBeGreaterThan(0);
    expect([...net.p, ...net.temp, ...net.zP].every(Number.isFinite)).toBe(true);
  });

  test('en séquence, clapet fermé : le second chemin d\'air ne débite pas, et le gaz du second collecteur passe au premier', () => {
    const net = new GasNetwork();
    net.build({ ...SIX, turboLayout: 'twin_sequential' }, ORDER6);
    const closed: GasInputs = { ...WIDE_OPEN, rpm: 3000, twinValve: 0.02, secondaryOnline: false, compressorRatio2: 1.05 };
    drive(net, 0.5, closed);
    let throttle2 = 0;
    let turbine1 = 0;
    let turbine2 = 0;
    let cross = 0;
    drive(net, 0.2, closed, () => {
      throttle2 += net.throttleFlow2;
      turbine1 += net.turbineFlow;
      turbine2 += net.turbineFlow2;
      cross += net.crossFlow;
    });
    expect(Math.abs(throttle2)).toBeLessThan(1e-9);
    expect(turbine2).toBeLessThan(0.1 * turbine1);
    // Du second collecteur vers le premier : le sens négatif.
    expect(cross).toBeLessThan(0);
    // Vanne et clapet ouverts : les deux en charge.
    drive(net, 0.5, { ...closed, twinValve: 1, secondaryOnline: true, compressorRatio2: 2 });
    let both = 0;
    drive(net, 0.2, { ...closed, twinValve: 1, secondaryOnline: true, compressorRatio2: 2 }, () => (both += net.turbineFlow2));
    expect(both).toBeGreaterThan(0.4 * turbine1);
  });
});

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  // Douze agents testent en même temps : le chargement peut prendre plus que les 5 s par défaut.
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur', { timeout: 30_000 });
  return errors;
}

async function start(page: Page, hash: string) {
  const errors = await open(page, hash);
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => tele(page).then((t) => t.rpm), { timeout: 20_000 }).toBeGreaterThan(500);
  return errors;
}

const tele = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const shot = async (page: Page, name: string) => {
  if (SHOTS) await page.screenshot({ path: `${SHOTS}/${name}.png` });
};

test.describe('deux turbos, sur la page', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le volet : le compresseur d\'un twincharger se choisit, l\'anti-lag et le tube ne restent que sur un turbo simple', async ({ page }) => {
    const errors = await open(page, link('turbos=twincharger'));
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    await expect(panel.getByTestId('twincharger-kind')).toBeVisible();
    await expect(panel.getByTestId('twin-only-single')).toBeVisible();
    await expect(panel.getByTestId('antilag-help')).toHaveCount(0);
    await expect(panel.getByTestId('turbo-pipe-help')).toHaveCount(0);
    await panel.getByTestId('turbo-layout').getByRole('button', { name: 'Un seul' }).click();
    await expect(panel.getByTestId('twincharger-kind')).toHaveCount(0);
    await expect(panel.getByTestId('antilag-help')).toBeVisible();
    await expect(panel.getByTestId('turbo-pipe-help')).toBeVisible();
    expect(errors).toEqual([]);
  });

  test('en parallèle : deux arbres dans la télémétrie et sur la jauge, deux turbines dans le gaz', async ({ page }) => {
    await page.setViewportSize({ width: 1500, height: 1300 });
    const errors = await start(page, link('turbos=twin_parallel'));
    const idle = await tele(page);
    expect(idle.twinStage).toBe(2);
    expect(idle.twinOnline).toBe(1);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.turbo2Rpm), { timeout: 5000 }).toBeGreaterThan(30000);
    const full = await tele(page);
    // Deux arbres pareils, deux vitesses voisines mais pas égales : les collecteurs ne sont pas identiques.
    expect(full.turbo2Rpm / full.turboRpm).toBeGreaterThan(0.8);
    expect(full.turbo2Rpm / full.turboRpm).toBeLessThan(1.25);
    expect(full.turbo2GasC).toBeGreaterThan(100);
    await expect(page.getByTestId('gauge-turbine')).toContainText('Turbos');
    await shot(page, 'biturbo-tableau');
    await voir(page, 'gas');
    await page.waitForTimeout(1200);
    const net = await page.evaluate(() => {
      const n = (window as any).__revforge.motion.gas.net;
      return { twin: n.hasTwin, flow2: n.turbineFlow2, compressor2: n.compressorFlow2, ok: [...n.temp, ...n.p].every(Number.isFinite) };
    });
    expect(net.twin).toBe(true);
    expect(net.ok).toBe(true);
    expect(net.flow2).toBeGreaterThan(0);
    await shot(page, 'biturbo-gaz');
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  // Un démarrage par test : trois dans un même test dépassaient ses 60 s sur une machine saturée.
  test('en séquence : le primaire seul au ralenti, clapet du secondaire fermé', async ({ page }) => {
    const errors = await start(page, link('turbos=twin_sequential'));
    const idle = await tele(page);
    expect(idle.twinStage).toBe(0);
    expect(idle.twinOnline).toBe(0);
    expect(errors).toEqual([]);
  });

  test('en twincharger : l\'embrayage se ferme et les rotors tournent', async ({ page }) => {
    const errors = await start(page, link('turbos=twincharger'));
    await page.keyboard.down('Space');
    // Au point mort, le moteur traverse en une fraction de seconde la plage où le compresseur est embrayé : on regarde de près.
    const seen = await page.evaluate(async () => {
      let clutch = 0;
      let rotor = 0;
      for (let i = 0; i < 100; i++) {
        const t = (window as any).__revforge.telemetry();
        clutch = Math.max(clutch, t.chargerClutch);
        rotor = Math.max(rotor, t.chargerRpm);
        await new Promise((resolve) => setTimeout(resolve, 20));
      }
      return { clutch, rotor };
    });
    expect(seen.clutch).toBe(1);
    expect(seen.rotor).toBeGreaterThan(1000);
    await voir(page, 'gas');
    await page.waitForTimeout(800);
    await shot(page, 'twincharger-gaz');
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  test('à aubes : leur ouverture se lit, sans soupape', async ({ page }) => {
    const errors = await start(page, link('vgt=1'));
    await page.keyboard.down('Space');
    await page.waitForTimeout(1500);
    const vanes = await tele(page);
    expect(vanes.turboVane).toBeGreaterThanOrEqual(0);
    expect(vanes.turboVane).toBeLessThanOrEqual(1);
    expect(vanes.turboWastegate).toBe(0);
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  for (const [name, tuning] of [['parallele', 'turbos=twin_parallel'], ['twincharger', 'turbos=twincharger'], ['aubes', 'vgt=1']]) {
    test(`la vue du moteur dessine le montage, ${name}`, async ({ page }) => {
      await page.setViewportSize({ width: 1500, height: 1300 });
      const errors = await start(page, link(tuning));
      await page.keyboard.down('Space');
      await voir(page, 'engine');
      await expect(page.getByTestId('engine-view')).toBeVisible();
      await page.waitForTimeout(1500);
      const mode = await page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.mode());
      await page.getByTestId('engine-names').click();
      await page.waitForTimeout(300);
      if (SHOTS) await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/moteur-${name}-${mode}.png` });
      const flatButton = page.getByRole('button', { name: /Écorché|2D/ }).first();
      if (await flatButton.count()) {
        await flatButton.click();
        await page.waitForTimeout(600);
        if (SHOTS) await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/moteur-${name}-2d.png` });
      }
      await page.keyboard.up('Space');
      expect(errors).toEqual([]);
    });
  }

  test('à la largeur d\'un téléphone, deux turbos tiennent dans la vue du gaz', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 780 });
    const errors = await start(page, link('turbos=twin_sequential'));
    await page.keyboard.down('Space');
    await voir(page, 'gas');
    await page.waitForTimeout(1000);
    await shot(page, 'biturbo-gaz-telephone');
    const wide = await page.evaluate(() => document.scrollingElement!.scrollWidth);
    expect(wide).toBeLessThanOrEqual(361);
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });
});
