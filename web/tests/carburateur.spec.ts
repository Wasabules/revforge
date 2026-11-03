import { expect, test, type Page } from '@playwright/test';
import { carburetorOf } from '../src/lib/carburetor';
import type { EngineConfig } from '../src/lib/config';
import { engineLayout, EnginePose, type PoseClock } from '../src/lib/engineLayout';
import { gasKey } from '../src/lib/fluid';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { ouvrir, voir } from './aide';

const SHOTS = process.env.REVFORGE_SHOTS;

const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', routing: 'single', aspiration: 'natural', fuel: 'petrol', multiplier: 0, pops: 'off',
  turboRelease: 'dump_valve', boostBar: 1, turboSize: 'medium', fuelSystem: 'injection', electric: false, body: 'compact', massKg: 1200, rimInch: 16, drive: 'front', boxFlywheel: 'single',
} as unknown as EngineConfig;
const ORDER4 = [1, 3, 4, 2];
const FRAME = 1 / 60 / 12;
const WIDE_OPEN: GasInputs = { rpm: 6000, load: 1, boost: 0, release: 0, burning: true, heat: 0.7 };

/** La pression moyenne du plénum à plein gaz, une fois le réseau établi. */
function plenumPressure(config: EngineConfig): number {
  const net = new GasNetwork();
  net.build(config, ORDER4);
  let cycle = 0;
  let sum = 0;
  let n = 0;
  for (let t = 0; t < 1; t += FRAME) {
    const turns = (WIDE_OPEN.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, WIDE_OPEN);
    cycle = (cycle + turns) % 1;
    if (t > 0.5) {
      sum += net.zP[net.zonePlenum];
      n++;
    }
  }
  return sum / n;
}

test.describe('le carburateur, dans le calcul', () => {
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le dimensionnement recopie celui du C++, et un diesel n\'en a pas', () => {
    expect(carburetorOf(BASE)).toBeNull();
    expect(carburetorOf({ ...BASE, fuel: 'diesel', fuelSystem: 'single' })).toBeNull();
    expect(carburetorOf({ ...BASE, electric: true, fuelSystem: 'multi' })).toBeNull();
    // Mêmes chiffres que carburetorSpecOf : 10 √(2 × 5,984) et 20 √(0,5 × 5,984).
    const single = carburetorOf({ ...BASE, fuelSystem: 'single' });
    expect(single?.throats).toBe(1);
    expect((single?.throatM ?? 0) * 1000).toBeCloseTo(34.6, 1);
    const multi = carburetorOf({ ...BASE, fuelSystem: 'multi' });
    expect(multi?.throats).toBe(4);
    expect((multi?.throatM ?? 0) * 1000).toBeCloseTo(34.6, 1);
  });

  test('le dessin n\'a plus de jet d\'injection, et se reconstruit quand on change', () => {
    const sprays = (config: EngineConfig) => {
      const pose = new EnginePose(engineLayout(config));
      let cycle = 0;
      for (let i = 0; i < 400; i++) {
        cycle = (cycle + 0.01) % 1;
        const clock: PoseClock = { cycle, turns: cycle * 2, turbineAngle: 0, rpm: 3000, load: 0.5, heat: 0.5, reduced: false, burning: true } as PoseClock;
        pose.update(clock, { cut: 0, cranking: 0, stalled: 0, rpm: 3000 }, null, false, 1 / 60);
      }
      return pose.sprayCount;
    };
    expect(sprays(BASE)).toBeGreaterThan(0);
    expect(sprays({ ...BASE, fuelSystem: 'multi' })).toBe(0);
    expect(engineLayout({ ...BASE, fuelSystem: 'single' }).carb?.kind).toBe('single');
    expect(engineLayout(BASE).carb).toBeNull();
    expect(engineLayout({ ...BASE, fuelSystem: 'multi' }).key).not.toBe(engineLayout(BASE).key);
  });

  test('à l\'écran, le venturi est un vrai col : il coûte de la pression, plus s\'il est unique', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['duct']);
    // Changer d'alimentation reconstruit le réseau : sinon le col n'apparaît qu'au prochain rechargement.
    expect(gasKey({ ...BASE, fuelSystem: 'single' })).not.toBe(gasKey(BASE));
    net.build({ ...BASE, fuelSystem: 'single' }, ORDER4);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['duct', 'venturi', 'duct']);
    const single = net.intakeParts[1].area;
    expect(single).toBeLessThan(1);
    net.build({ ...BASE, fuelSystem: 'multi' }, ORDER4);
    // Un corps par cylindre garde le rapport de son col à sa pipe : moins serré qu'un col pour tous.
    expect(net.intakeParts[1].area).toBeLessThan(1);
    expect(net.intakeParts[1].area).toBeGreaterThan(single);

    const injected = plenumPressure(BASE);
    const oneCarb = plenumPressure({ ...BASE, fuelSystem: 'single' });
    const perCylinder = plenumPressure({ ...BASE, fuelSystem: 'multi' });
    test.info().annotations.push({ type: 'plénum', description: `${injected} / ${oneCarb} / ${perCylinder} Pa` });
    expect(oneCarb).toBeLessThan(injected - 1500);
    expect(oneCarb).toBeLessThan(perCylinder);
    expect(perCylinder).toBeLessThan(injected);
    // Aucun col ne fabrique de la pression, et aucun n'étouffe le moteur.
    expect(oneCarb).toBeGreaterThan(injected * 0.8);
  });

  test('monté soufflé, le col suit le tube froid sans déranger la chaîne du turbo', () => {
    const net = new GasNetwork();
    const turbo = { ...BASE, aspiration: 'turbo', fuelSystem: 'single', boostBar: 1 } as EngineConfig;
    net.build(turbo, ORDER4);
    expect(net.intakeParts.map((part) => part.kind)).toEqual(['filter', 'inlet', 'hot', 'cooler', 'cold', 'venturi', 'duct']);
    const cold = net.intakeParts[4];
    expect(net.releaseCell).toBeGreaterThanOrEqual(cold.first);
    expect(net.releaseCell).toBeLessThan(cold.first + cold.count);
    // Le réseau reste stable, comme sans le col.
    const inputs: GasInputs = { rpm: 5500, load: 1, boost: 1, release: 0, burning: true, heat: 0.7, compressorRatio: 2, compressorOutK: 380, wastegate: 0.5 };
    let cycle = 0;
    for (let t = 0; t < 0.5; t += FRAME) {
      const turns = (inputs.rpm / 120) * FRAME;
      net.advance(FRAME, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
    }
    expect(Number.isFinite(net.zP[net.zonePlenum])).toBe(true);
    // Le rapport de 2 du compresseur arrive au plénum, moins la perte du col.
    expect(net.zP[net.zonePlenum]).toBeGreaterThan(1.5e5);
    expect(net.zP[net.zonePlenum]).toBeLessThan(2.1e5);
  });
});

async function choisir(page: Page, legend: string, label: string) {
  await page.getByRole('group', { name: legend }).getByRole('button', { name: label, exact: true }).click();
}

test.describe('le carburateur, sur le site', () => {
  test('choix, aide, résumé, fiche et lien de partage ; masqué sur un diesel et une électrique', async ({ page }) => {
    await page.goto('./');
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('fuel-system')).toBeVisible();
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Carburateur');
    await choisir(page, 'Alimentation', 'Carburateur');
    await expect(page.getByTestId('carb-spec')).toContainText('Venturi 1 ×');
    await expect(page.getByTestId('fuel-system')).toContainText('pipe commune');
    await expect(page.getByTestId('open-engine-summary')).toContainText('Carburateur');
    await expect.poll(() => page.url()).toContain('_single');

    await page.goto(page.url());
    await page.reload();
    await ouvrir(page, 'engine');
    await expect(page.getByRole('group', { name: 'Alimentation' }).getByRole('button', { name: 'Carburateur', exact: true })).toHaveAttribute('aria-pressed', 'true');

    await choisir(page, 'Alimentation', 'Un par cylindre');
    await expect(page.getByTestId('carb-spec')).toContainText('Venturi 4 ×');
    await expect(page.getByTestId('open-engine-summary')).toContainText('Un par cylindre');

    await choisir(page, 'Carburant', 'Diesel');
    await expect(page.getByTestId('fuel-system')).toHaveCount(0);
    await expect(page.getByTestId('open-engine-summary')).not.toContainText('Un par cylindre');
  });

  for (const [name, system] of [['single', 'Carburateur'], ['multi', 'Un par cylindre']] as const) {
    for (const engine of ['l4', 'v8'] as const) {
      test(`la vue du moteur se dessine : ${name}, ${engine}`, async ({ page }) => {
        const errors: string[] = [];
        page.on('pageerror', (error) => errors.push(error.message));
        await page.goto('./');
        await ouvrir(page, 'engine');
        if (engine === 'v8') {
          await choisir(page, 'Cylindres', '8');
          await choisir(page, 'Architecture', 'V cross-plane');
        }
        await choisir(page, 'Alimentation', system);
        await voir(page, 'engine');
        await page.waitForTimeout(1500);
        expect(errors).toEqual([]);
        if (!SHOTS) return;
        await page.getByTestId('open-engine').click();
        await page.waitForTimeout(800);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue_${name}_${engine}.png` });
        // De l'autre côté : le carburateur est du côté de l'admission.
        const box = (await page.getByTestId('engine-view').boundingBox())!;
        await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
        await page.mouse.down();
        await page.mouse.move(box.x + box.width / 2 + 260, box.y + box.height / 2, { steps: 6 });
        await page.mouse.up();
        await page.waitForTimeout(500);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/dos_${name}_${engine}.png` });
        await page.getByTestId('engine-mode-2d').click();
        await page.waitForTimeout(800);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue2d_${name}_${engine}.png` });
        await voir(page, 'gas');
        await page.waitForTimeout(1000);
        await page.screenshot({ path: `${SHOTS}/gaz_${name}_${engine}.png` });
        await voir(page, 'engine');
        await page.setViewportSize({ width: 360, height: 760 });
        await page.waitForTimeout(600);
        await page.screenshot({ path: `${SHOTS}/tel_${name}_${engine}.png` });
      });
    }
  }
});
