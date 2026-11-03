import { mkdirSync, readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { EXHAUST_LINES } from '../src/lib/exhaustLines';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { firingOrder } from '../src/lib/motion';
import { firingsPerCycle, sweptCc } from '../src/lib/rotary';
import {
  CHAMBER_SILENCER_AREA, CHAMBER_SILENCER_M, CHAMBER_TAIL_AREA, CHAMBER_TAIL_M, CRANKCASE_COMPRESSION, crankAngleForTravel, EXHAUST_WIDTH, expansionChamber,
  GEOMETRIC_COMPRESSION, INLET_WIDTH, isTwoStroke, pistonTravel, PREMIX_RATIO, ROD_OVER_STROKE, TRANSFER_WIDTH, twoStrokePorts,
} from '../src/lib/twoStroke';

/* eslint-disable @typescript-eslint/no-explicit-any */

const BASE = {
  cylinders: 3, layout: 'inline', displacementCc: 841, boreMm: 70, redlineRpm: 5000, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'retro', massKg: 813, rimInch: 15, drive: 'front', placement: 'front',
  insulation: 'light', boxFlywheel: 'single', turboSize: 'medium', fuelSystem: 'single', cycle: 'two_stroke', airFilter: 'stock',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig> = {}): EngineConfig => ({ ...BASE, ...over });
const SONS = process.env.SONS ?? 'test-results/deux-temps';

const cpp = (file: string) => readFileSync(`../core/src/${file}`, 'utf8');
const constant = (source: string, name: string) => Number(new RegExp(`constexpr (?:double|int|float) ${name} = ([\\d.e+-]+)`).exec(source)?.[1]);

test.describe('la géométrie, sans navigateur', () => {
  test('les lumières et la chambre recopient le C++', () => {
    const source = cpp('two_stroke.cpp');
    // Les hauteurs d'un moteur de tourisme (4500 tr/min) et d'un moteur de course (9500).
    expect(twoStrokePorts({ redlineRpm: 4500 }).exhaustHeight).toBeCloseTo(constant(source, 'kExhaustHeightTouring'), 9);
    expect(twoStrokePorts({ redlineRpm: 9500 }).exhaustHeight).toBeCloseTo(constant(source, 'kExhaustHeightRacing'), 9);
    expect(twoStrokePorts({ redlineRpm: 4500 }).transferHeight).toBeCloseTo(constant(source, 'kTransferHeightTouring'), 9);
    expect(twoStrokePorts({ redlineRpm: 9500 }).transferHeight).toBeCloseTo(constant(source, 'kTransferHeightRacing'), 9);
    expect(twoStrokePorts({ redlineRpm: 4500 }).inletTravel).toBeCloseTo(constant(source, 'kInletTravelTouring'), 9);
    expect(twoStrokePorts({ redlineRpm: 9500 }).inletTravel).toBeCloseTo(constant(source, 'kInletTravelRacing'), 9);
    expect(ROD_OVER_STROKE).toBe(constant(source, 'kRodOverStroke'));
    expect(EXHAUST_WIDTH).toBe(constant(source, 'kExhaustWidth'));
    expect(TRANSFER_WIDTH).toBe(constant(source, 'kTransferWidth'));
    expect(INLET_WIDTH).toBe(constant(source, 'kInletWidth'));
    expect(GEOMETRIC_COMPRESSION).toBe(constant(source, 'kGeometricCompression'));
    expect(CRANKCASE_COMPRESSION).toBe(constant(source, 'kCrankcaseCompression'));
    const line = cpp('exhaust_line.cpp');
    expect(CHAMBER_SILENCER_M).toBe(constant(line, 'kChamberSilencerM'));
    expect(CHAMBER_SILENCER_AREA).toBe(constant(line, 'kChamberSilencerArea'));
    expect(CHAMBER_TAIL_M).toBe(constant(line, 'kChamberTailM'));
    expect(CHAMBER_TAIL_AREA).toBe(constant(line, 'kChamberTailArea'));
    expect(PREMIX_RATIO).toBe(Number(/kPremixRatio = ([\d.]+)/.exec(readFileSync('../core/include/revforge/two_stroke.hpp', 'utf8'))?.[1]));
    // Les chiffres que donne le C++ pour le préréglage (sonde `twoStrokePortsOf`, `expansionChamberFor`).
    const ports = twoStrokePorts(BASE);
    expect(ports.exhaustOpen).toBeCloseTo(107.2, 1);
    expect(ports.transferOpen).toBeCloseTo(124.0, 1);
    expect(ports.inletOpenBefore).toBeCloseTo(68.5, 1);
    expect(ports.trappedCompression).toBeCloseTo(5.59, 2);
    const chamber = expansionChamber(make({ exhaust: 'expansion' }));
    expect(chamber.header).toBeCloseTo(0.274, 3);
    expect(chamber.diffuser).toBeCloseTo(0.386, 3);
    expect(chamber.belly).toBeCloseTo(0.173, 3);
    expect(chamber.baffle).toBeCloseTo(0.366, 3);
    expect(chamber.stinger).toBeCloseTo(0.272, 3);
    expect(chamber.headerDiameter * 1000).toBeCloseTo(37.8, 1);
    expect(chamber.bellyDiameter * 1000).toBeCloseTo(104.1, 1);
    expect(chamber.soundMs).toBeCloseTo(502, 0);
  });

  test('une explosion par tour et par cylindre ; le piston découvre les lumières', () => {
    expect(isTwoStroke(BASE)).toBe(true);
    expect(isTwoStroke({ layout: 'rotary', cycle: 'two_stroke' })).toBe(false);
    expect(firingsPerCycle(BASE)).toBe(6);
    expect(sweptCc(BASE)).toBe(1682);
    expect(firingsPerCycle({ ...BASE, cycle: 'four_stroke' })).toBe(3);
    const ports = twoStrokePorts(BASE);
    expect(pistonTravel(ports.exhaustOpen)).toBeCloseTo(1 - ports.exhaustHeight, 9);
    expect(pistonTravel(360 - ports.exhaustOpen)).toBeCloseTo(1 - ports.exhaustHeight, 9);
    expect(crankAngleForTravel(0.5)).toBeGreaterThan(80);
    expect(crankAngleForTravel(0.5)).toBeLessThan(90);
  });

  test('la chambre de détente dessinée est un cône qui s\'ouvre puis se referme, pas une ligne droite', () => {
    const line = EXHAUST_LINES.expansion;
    const parts = line.map((item) => item.part);
    expect(parts).toContain('diffuser');
    expect(parts).toContain('belly');
    expect(parts).toContain('baffle');
    expect(parts).toContain('stinger');
    const area = (part: string) => line.filter((item) => item.part === part).map((item) => item.area);
    const diffuser = area('diffuser');
    const baffle = area('baffle');
    for (let k = 1; k < diffuser.length; k++) expect(diffuser[k]).toBeGreaterThan(diffuser[k - 1]);
    for (let k = 1; k < baffle.length; k++) expect(baffle[k]).toBeLessThan(baffle[k - 1]);
    expect(area('belly')[0]).toBeGreaterThan(5 * area('stinger')[0]);
  });
});

const RUNNING: GasInputs = { rpm: 3000, load: 1, boost: 0, release: 0, burning: true, heat: 0.6, turnsTotal: 0 };

function drive(net: GasNetwork, inputs: GasInputs, cycles: number, each?: () => void) {
  const rate = inputs.rpm / 120;
  const dt = 1 / 240;
  let turns = inputs.turnsTotal ?? 0;
  let cycle = turns % 1;
  const end = turns + cycles;
  while (turns < end) {
    const step = rate * dt;
    turns += step;
    inputs.turnsTotal = turns;
    net.advance(dt, cycle, step, inputs);
    cycle = (cycle + step) % 1;
    each?.();
  }
}

test.describe('le gaz, sans navigateur', () => {
  test('une bouffée par tour et par cylindre, un carter qui pompe, et du mélange frais qui file à l\'échappement', () => {
    const net = new GasNetwork();
    net.build(BASE, firingOrder(3));
    expect(net.cylinders).toBe(3);
    expect(net.isTwoStroke).toBe(true);
    expect(net.crankZone).toBeGreaterThan(0);
    const inputs = { ...RUNNING };
    drive(net, inputs, 2);
    const before = net.blowdowns;
    let caseHigh = 0;
    let caseLow = 1e9;
    let transfer = 0;
    let inlet = 0;
    let freshOut = 0;
    drive(net, inputs, 3, () => {
      const z = net.crankZone;
      caseHigh = Math.max(caseHigh, net.zP[z]);
      caseLow = Math.min(caseLow, net.zP[z]);
      transfer = Math.max(transfer, net.cylTransfer[0]);
      inlet = Math.max(inlet, net.caseInlet[0]);
      freshOut = Math.max(freshOut, 1 - net.exitY);
    });
    // Trois cycles de deux tours : deux bouffées par cylindre et par cycle.
    expect(net.blowdowns - before).toBeGreaterThanOrEqual(3 * 2 * 3 - 1);
    expect(net.blowdowns - before).toBeLessThanOrEqual(3 * 2 * 3 + 1);
    // Le carter monte au-dessus de l'atmosphère piston en bas, et tombe en dessous piston en haut.
    expect(caseHigh).toBeGreaterThan(1.15e5);
    expect(caseLow).toBeLessThan(0.98e5);
    expect(transfer).toBeGreaterThan(0);
    expect(inlet).toBeGreaterThan(0);
    // Le balayage pousse du mélange frais jusqu'à la sortie.
    expect(freshOut).toBeGreaterThan(0.05);
    for (let c = 0; c < net.cylinders; c++) {
      expect(net.cylT[c]).toBeGreaterThan(200);
      expect(net.cylT[c]).toBeLessThan(4000);
    }
  });

  test('la chambre de détente de ce moteur est dans la ligne du gaz, cônes compris', () => {
    const net = new GasNetwork();
    net.build(make({ exhaust: 'expansion' }), firingOrder(3));
    const parts = net.sections.map((section) => section.part ?? '');
    expect(parts).toContain('diffuser');
    expect(parts).toContain('baffle');
    const belly = net.sections.find((section) => section.part === 'belly')!;
    const header = net.sections[0];
    expect(belly.diameter).toBeGreaterThan(2 * header.diameter);
    const inputs = { ...RUNNING };
    drive(net, inputs, 4);
    expect(net.totalMass()).toBeGreaterThan(0);
  });
});

async function open(page: Page, hash = ''): Promise<string[]> {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text());
  });
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

async function choosePreset(page: Page) {
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('two_stroke_triple');
}

test.describe('sur le site', () => {
  test('le préréglage : deux temps, ni distribution, ni suralimentation, ni diesel ; au plus 6 cylindres', async ({ page }) => {
    const errors = await open(page);
    await choosePreset(page);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    await expect(page.getByTestId('open-engine-summary')).toContainText('deux temps');
    await expect(page.getByTestId('two-stroke-charging')).toBeVisible();
    await expect(panel.getByText('Carburant', { exact: true })).toHaveCount(0);
    await expect(page.getByTestId('section-valvetrain')).toHaveCount(0);
    const cylinders = panel.locator('fieldset').filter({ hasText: 'Cylindres' }).first().getByRole('button');
    await expect(cylinders).toHaveText(['2', '3', '4', '5', '6']);
    // Retour au quatre-temps : la distribution et la suralimentation reviennent.
    await panel.getByRole('button', { name: 'Quatre temps' }).click();
    await expect(page.getByTestId('two-stroke-charging')).toHaveCount(0);
    await expect(page.getByTestId('section-valvetrain')).toBeVisible();
    const spec = await page.evaluate(() => (window as any).__revforge?.spec?.() ?? null);
    expect(spec).not.toBeNull();
    expect(errors).toEqual([]);
  });

  test('la fiche : le couple vient du cycle d\'un tour, et la chambre de détente se calcule sur le moteur', async ({ page }) => {
    const errors = await open(page);
    await choosePreset(page);
    const spec = await page.evaluate(() => (window as any).__revforge?.spec?.() ?? null);
    // Le C++ donne 56,5 Nm à 3050 tr/min et 20,2 kW à 3800 (la Saab 96 publiait 79 Nm et 28 kW).
    expect(spec.peakTorque).toBeGreaterThan(50);
    expect(spec.peakTorque).toBeLessThan(62);
    await ouvrir(page, 'exhaust');
    await page.getByTestId('panel-exhaust').getByRole('button', { name: 'Chambre de détente' }).click();
    // Le nombre suit la langue : « 4 400 » en français, avec une espace fine.
    await expect(page.getByTestId('expansion-help')).toContainText(/4\D?400 tr\/min/);
    // Le lien de partage garde le cycle et la chambre.
    const link = await page.evaluate(() => (window as any).__revforge.encodeConfig((window as any).__revforge.config()));
    const back = await page.evaluate((text) => (window as any).__revforge.decodeConfig(text), link);
    expect(back.cycle).toBe('two_stroke');
    expect(back.exhaust).toBe('expansion');
    expect(errors).toEqual([]);
  });

  test('en marche : l\'huile part avec l\'essence, et la ligne montre la chambre et sa fumée bleue', async ({ page }) => {
    mkdirSync(SONS, { recursive: true });
    const errors = await open(page);
    await choosePreset(page);
    await ouvrir(page, 'exhaust');
    await page.getByTestId('panel-exhaust').getByRole('button', { name: 'Chambre de détente' }).click();
    await page.keyboard.press('Escape');
    await voir(page, 'gas');
    await page.getByTestId('start').click();
    await page.waitForFunction(() => (window as any).__revforge?.telemetry().rpm > 500, null, { timeout: 15000 });
    await page.waitForTimeout(2500);
    const telemetry = await page.evaluate(() => (window as any).__revforge.telemetry());
    // 1 volume d'huile pour 50 d'essence : en masse, le rapport des densités (0,85 et 0,745).
    expect(telemetry.fuelLph).toBeGreaterThan(0);
    expect(telemetry.oilGph / (telemetry.fuelLph * 745)).toBeCloseTo(0.85 / 0.745 / 50, 2);
    await page.screenshot({ path: `${SONS}/gaz_chambre.png` });
    await page.setViewportSize({ width: 360, height: 800 });
    await page.waitForTimeout(600);
    await page.screenshot({ path: `${SONS}/gaz_chambre_360.png`, fullPage: true });
    const scrollable = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1);
    expect(scrollable).toBe(false);
    expect(errors).toEqual([]);
  });
});
