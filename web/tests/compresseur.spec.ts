import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import { engineLayout } from '../src/lib/engineLayout';
import type { EngineConfig } from '../src/lib/config';
import { GasNetwork, P_ATM, type GasInputs } from '../src/lib/fluidGas';
import { chargerUnit } from '../src/lib/superchargerLayout';

/* eslint-disable @typescript-eslint/no-explicit-any */
const NATURAL = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const link = (kind: string, cylinders = '4.inline.2000.84.6800', bar = '1') => `${cylinders}.stock.supercharger.petrol.0.off.dump_valve~compact.1350.18.front.${bar}_front_standard_single_medium_${kind}`;
const SHOTS = process.env.REVFORGE_SHOTS;

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'supercharger', superchargerKind: 'roots', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, turboSize: 'medium', electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
} as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;

function drive(net: GasNetwork, inputs: GasInputs, seconds: number) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (inputs.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, inputs);
    cycle = (cycle + turns) % 1;
  }
}

test.describe('le gaz du compresseur, dans le calcul', () => {
  test('le chemin de l\'air passe par le compresseur, sans turbine ni décharge', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    expect(net.hasTurbine).toBe(false);
    expect(net.compressorCell).toBeGreaterThan(0);
    expect(net.releaseCell).toBe(-1);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['filter', 'inlet', 'hot', 'cooler', 'cold']);
    const turbo = new GasNetwork();
    turbo.build({ ...BASE, aspiration: 'turbo' }, ORDER4);
    expect(turbo.hasTurbine).toBe(true);
    expect(turbo.releaseCell).toBeGreaterThan(0);
  });

  test('plein gaz, le plénum a la pression du compresseur et l\'air y arrive chaud puis refroidi', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    drive(net, { rpm: 5000, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.05, compressorOutK: 400 }, 0.5);
    expect((net.zP[net.zonePlenum] - P_ATM) / 1e5).toBeGreaterThan(0.5);
    expect(net.temp[net.compressorCell]).toBeGreaterThan(net.temp[net.intakeFirst + net.intakeCount - 1]);
    // Pied levé, soupape ouverte : pas de pression, et donc le vide d'un papillon fermé.
    const light = new GasNetwork();
    light.build(BASE, ORDER4);
    drive(light, { rpm: 3000, load: 0.2, boost: 0.02, release: 0, burning: true, heat: 0.3, compressorRatio: 1.02, compressorOutK: 295 }, 0.5);
    expect(light.zP[light.zonePlenum]).toBeLessThan(P_ATM);
  });

  test('la ligne s\'ouvre directement sur le collecteur : pas de contre-pression d\'une turbine', () => {
    const inputs: GasInputs = { rpm: 5500, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2.05, compressorOutK: 400, wastegate: 0 };
    const back = (config: EngineConfig) => {
      const net = new GasNetwork();
      net.build(config, ORDER4);
      drive(net, inputs, 0.4);
      let sum = 0;
      let n = 0;
      for (let k = 0; k < 40; k++) {
        drive(net, inputs, 0.002);
        sum += net.zP[net.zoneJoin];
        n++;
      }
      return sum / n;
    };
    expect(back(BASE)).toBeLessThan(back({ ...BASE, aspiration: 'turbo' }));
  });
});

test.describe('sa place sur le moteur', () => {
  test('le compresseur est au-dessus du collecteur d\'admission, ou sur un coin du bloc', () => {
    for (const config of [BASE, { ...BASE, cylinders: 8, layout: 'cross_plane', displacementCc: 6200, boreMm: 103 } as EngineConfig, { ...BASE, layout: 'boxer' } as EngineConfig]) {
      const layout = engineLayout(config);
      expect(layout.charger).toBe('roots');
      const roots = chargerUnit(layout, 'roots');
      expect(roots.volumetric).toBe(true);
      expect(roots.y).toBeGreaterThan(layout.deck);
      expect(roots.beltZ).toBeGreaterThan(layout.pulleyZ);
      const wheel = chargerUnit(layout, 'centrifugal');
      expect(wheel.volumetric).toBe(false);
      expect(wheel.wheelRadius).toBeGreaterThan(0);
      // Une vis : trois lobes contre cinq, bien plus d'hélice.
      const screw = chargerUnit(layout, 'twin_screw');
      expect(screw.lobesB).toBe(5);
      expect(screw.twist).toBeGreaterThan(roots.twist);
    }
    expect(engineLayout({ ...BASE, aspiration: 'natural' }).charger).toBeNull();
    expect(engineLayout({ ...BASE, aspiration: 'turbo' }).charger).toBeNull();
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

async function start(page: Page, hash: string) {
  const errors = await open(page, hash);
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => tele(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

const tele = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
const spec = (page: Page) => page.evaluate(() => (window as any).__revforge.spec());
const shot = async (page: Page, name: string) => {
  if (SHOTS) await page.screenshot({ path: `${SHOTS}/${name}.png` });
};

test.describe('à l\'écran', () => {
  test('le type et la pression se choisissent dans le volet moteur, et changent la fiche', async ({ page }) => {
    const errors = await open(page, NATURAL);
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Roots');
    await page.getByRole('group', { name: 'Alimentation' }).getByRole('button', { name: 'Compresseur' }).click();
    const kinds = page.getByRole('group', { name: 'Type de compresseur' });
    await expect(kinds).toBeVisible();
    await expect(page.getByText('Pression du compresseur')).toBeVisible();
    await expect(page.getByText('Taille du turbo')).toHaveCount(0);
    await expect(page.getByTestId('open-engine-summary')).toContainText('Roots');
    await expect.poll(() => spec(page).then((s) => s.chargerVolume)).toBeGreaterThan(0.3);
    const roots = await spec(page);
    expect(roots.chargerRatio).toBeGreaterThan(1);
    await shot(page, 'volet-roots');
    await kinds.getByRole('button', { name: 'Centrifuge' }).click();
    await expect(page.getByTestId('open-engine-summary')).toContainText('Centrifuge');
    await expect.poll(() => spec(page).then((s) => s.chargerWheelMm)).toBeGreaterThan(30);
    const wheel = await spec(page);
    // Le couple d'un centrifuge arrive bien plus haut.
    expect(wheel.peakTorqueRpm).toBeGreaterThan(roots.peakTorqueRpm + 500);
    expect(wheel.chargerRatio).toBeGreaterThan(5);
    expect(errors).toEqual([]);
  });

  test('le type voyage dans le lien', async ({ page }) => {
    await open(page, link('twin_screw'));
    await expect.poll(() => spec(page).then((s) => s.chargerVolume)).toBeGreaterThan(0.3);
    await expect(page.getByTestId('open-engine-summary')).toContainText('À vis');
    const screw = await spec(page);
    await open(page, link('roots'));
    await expect.poll(() => spec(page).then((s) => s.chargerVolume)).toBeGreaterThan(0.3);
    expect((await spec(page)).chargerVolume).toBeGreaterThan(screw.chargerVolume);
  });

  test('plein gaz, la pression arrive tout de suite, et l\'arbre suit le vilebrequin', async ({ page }) => {
    const errors = await start(page, link('roots'));
    await page.waitForTimeout(600);
    const idle = await tele(page);
    // Au ralenti, soupape ouverte : presque pas de pression, et presque rien pris au vilebrequin.
    expect(idle.boost).toBeLessThan(0.1);
    expect(idle.chargerKw).toBeLessThan(1);
    expect(idle.chargerSpill).toBeGreaterThan(0.5);
    expect(idle.turboRpm).toBe(0);
    const ratio = (await spec(page)).chargerRatio;
    expect(idle.chargerRpm / idle.rpm).toBeCloseTo(ratio, 0);
    await page.keyboard.down('Space');
    await expect.poll(() => tele(page).then((t) => t.boost), { timeout: 3000 }).toBeGreaterThan(0.7);
    const full = await tele(page);
    expect(full.chargerKw).toBeGreaterThan(2);
    expect(full.turboOutC).toBeGreaterThan(full.turboCoolC);
    await expect(page.getByTestId('gauge-turbine')).toContainText('Compresseur');
    await expect(page.getByTestId('live-boost')).toBeVisible();
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  for (const kind of ['roots', 'twin_screw', 'centrifugal']) {
    test(`les vues montrent le compresseur, ${kind}`, async ({ page }) => {
      await page.setViewportSize({ width: 1500, height: 1300 });
      const errors = await start(page, link(kind));
      await page.keyboard.down('Space');
      await page.waitForTimeout(600);
      await shot(page, `tableau-${kind}`);
      await voir(page, 'engine');
      const view = page.getByTestId('engine-view');
      await expect(view).toBeVisible();
      await page.waitForTimeout(1200);
      const state = await page.evaluate(() => {
        const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
        return { labels: (d.scene()?.labels ?? []).map((l: { key: string }) => l.key), charger: !!d.flat()?.charger, chain: !!d.flat()?.chain, mode: d.mode() };
      });
      expect(state.chain).toBe(false);
      if (state.mode === '3d') expect(state.labels).toContain('engine.part.compressor');
      await page.getByTestId('engine-names').click();
      await page.waitForTimeout(300);
      await shot(page, `moteur-${kind}-${state.mode}`);
      // La coupe 2D, si elle n'est pas déjà celle-là.
      const flatButton = page.getByRole('button', { name: /Écorché|2D/ }).first();
      if (await flatButton.count()) {
        await flatButton.click();
        await page.waitForTimeout(500);
        await shot(page, `moteur-${kind}-2d`);
      }
      await voir(page, 'gas');
      await page.waitForTimeout(800);
      await expect(page.getByTestId('turbo-readout')).toContainText('Compresseur');
      const net = await page.evaluate(() => {
        const n = (window as any).__revforge.motion.gas.net;
        const values: number[] = [...n.temp, ...n.p];
        return { ok: values.every(Number.isFinite), parts: n.intakeParts.length, turbine: n.hasTurbine };
      });
      expect(net.ok).toBe(true);
      expect(net.parts).toBe(5);
      expect(net.turbine).toBe(false);
      await shot(page, `gaz-${kind}`);
      await page.keyboard.up('Space');
      expect(errors).toEqual([]);
    });
  }

  test('à la largeur d\'un téléphone', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 780 });
    const errors = await start(page, link('roots'));
    await page.keyboard.down('Space');
    await voir(page, 'gas');
    await page.waitForTimeout(800);
    await shot(page, 'telephone-gaz');
    await voir(page, 'engine');
    await page.waitForTimeout(1000);
    await shot(page, 'telephone-moteur');
    await page.keyboard.up('Space');
    expect(errors).toEqual([]);
  });

  test('les graphiques montrent la pression du compresseur', async ({ page }) => {
    const errors = await start(page, link('roots'));
    await page.getByTestId('charts-toggle').click();
    await expect(page.getByTestId('charts-body')).toContainText('Pression du compresseur');
    expect(errors).toEqual([]);
  });
});
