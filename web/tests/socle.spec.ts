import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

/**
 * Le socle moteur : une trentaine de réglages posés de bout en bout, sans effet sur le calcul (voir CLAUDE.md). Ce qui est
 * vérifié ici : le lien les garde tous, les anciens liens se lisent, l'ordre est le même qu'en C++, et chaque commande
 * n'apparaît que là où elle a un sens.
 */
const BASE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
const DIESEL = '4.inline.2000.85.4500.stock.turbo.diesel.0.off.dump_valve~compact.1350.18.front.1';
const TURBO = '4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
const ROTARY = '2.rotary.1308.84.9000.stock.natural.petrol.0.off.dump_valve~coupe.1300.18.rear.1';

/** Chaque réglage du socle, loin de son défaut. */
const CHANGED: Record<string, string | number | boolean> = {
  ambientC: -12, altitudeM: 2400, startTemp: 'cold', compressionRatio: 12.3, fuelGrade: 'methanol', ignitionOffsetDeg: -7, fullLoadLambda: 0.82,
  revLimiter: 'ignition_cut', launchRpm: 4800, flatShift: true, tractionControl: 'sport', dieselInjection: 'common_rail', egr: true, particulateFilter: true,
  camDurationDeg: 286, camLiftMm: 12.4, lobeSeparationDeg: 106, valveTiming: 'two_step', valveSprings: 'stiff', turboLayout: 'twin_sequential',
  variableGeometry: true, boostControl: 'electronic', intercooler: 'none', waterInjection: true, nitrousKw: 110, cycle: 'two_stroke',
};

async function open(page: Page, hash = BASE) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

const link = (page: Page) => page.evaluate(() => decodeURIComponent(location.hash.replace(/^#m=/, '')));

test.describe('le lien de partage', () => {
  test('tous les réglages du socle changés à la fois font l’aller-retour, avec les valeurs ajoutées aux anciennes listes', async ({ page }) => {
    await open(page);
    const result = await page.evaluate((changed) => {
      const { encodeConfig, decodeConfig, config, tuningFields } = (window as any).__revforge;
      const wrong: string[] = [];
      const compare = (next: any) => {
        const back = decodeConfig(encodeConfig(next));
        for (const key of Object.keys(next)) if (back?.[key] !== next[key]) wrong.push(`${encodeConfig(next)} : ${key} ${back?.[key]} au lieu de ${next[key]}`);
      };
      const start = config();
      // Tout à la fois, sur un moteur qui prend les nouvelles valeurs des anciennes listes, avec et sans boîte réglée.
      const all = { ...start, ...changed, layout: 'w', cylinders: 12, exhaust: 'expansion', camshaft: 'ohv', fuelSystem: 'direct', aspiration: 'turbo' };
      compare(all);
      compare({ ...all, boxKind: 'sequential', boxGears: 6 });
      compare({ ...start, layout: 'vr', cylinders: 6 });
      // Chacun seul : le groupe ne s'écrit que pour lui, et ne décale aucun autre champ.
      for (const [key, value] of Object.entries(changed)) {
        compare({ ...start, [key]: value });
        compare({ ...start, [key]: value, boxFlywheel: 'dual_mass' });
      }
      // Une voiture électrique garde ses conditions et son antipatinage.
      compare({ ...start, electric: true, ambientC: 41, altitudeM: 900, tractionControl: 'full' });
      return { wrong, keys: tuningFields.map((field: any) => field.key), codes: tuningFields.map((field: any) => field.code), sample: encodeConfig(all) };
    }, CHANGED);
    expect(result.wrong.slice(0, 5)).toEqual([]);
    // Le test change vraiment tous les réglages de la table, et chacun a son propre code.
    expect([...result.keys].sort()).toEqual(Object.keys(CHANGED).sort());
    expect(new Set(result.codes).size).toBe(result.codes.length);
    // Compact : seulement ce qui diffère, après un troisième `~`.
    expect(result.sample.split('~')).toHaveLength(4);
    expect(result.sample.split('~')[3]).toContain('amb=-12');
  });

  test('au défaut, le lien est celui d’avant : pas de groupe en plus, ni pour un préréglage, ni pour un ancien lien', async ({ page }) => {
    await open(page);
    const result = await page.evaluate((base) => {
      const { encodeConfig, decodeConfig } = (window as any).__revforge;
      return { base: encodeConfig(decodeConfig(base)), electric: encodeConfig(decodeConfig('electric~compact.1800.19.rear')) };
    }, BASE);
    expect(result.base).toBe(`${BASE}_front_standard_single_medium_roots_off_400_dohc_stock_injection`);
    expect(result.electric).toBe('electric~compact.1800.19.rear.1_front_standard_single_medium_roots_off_400_dohc_stock_injection');
    // La page elle-même : un préréglage ouvert ne porte aucun réglage du socle.
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption('v8_cross_plane');
    expect((await link(page)).split('~').length).toBeLessThanOrEqual(3);
  });

  test('les anciens liens prennent les défauts, et un lien abîmé ne casse rien', async ({ page }) => {
    await open(page);
    const read = (hash: string) => page.evaluate((text) => (window as any).__revforge.decodeConfig(text), hash);
    const old = await read(TURBO);
    expect(old.ambientC).toBe(20);
    expect(old.altitudeM).toBe(0);
    expect(old.startTemp).toBe('warm');
    expect(old.compressionRatio).toBe(0);
    expect(old.intercooler).toBe('air_to_air');
    expect(old.cycle).toBe('four_stroke');
    // Bornes, 0 « d'origine », valeurs inconnues et codes inconnus : rien ne passe qui ne soit valide.
    const odd = await read(`${BASE}~~amb=99,alt=-50,cr=0,lambda=0.5,grade=kerosene,zzz=1,flat=2,launch=0,lift=40,dur=,nos=abc`);
    expect(odd.ambientC).toBe(50);
    expect(odd.altitudeM).toBe(0);
    expect(odd.compressionRatio).toBe(0);
    expect(odd.fullLoadLambda).toBe(0.7);
    expect(odd.fuelGrade).toBe('ron95');
    expect(odd.flatShift).toBe(false);
    expect(odd.launchRpm).toBe(0);
    expect(odd.camLiftMm).toBe(16);
    expect(odd.camDurationDeg).toBe(0);
    expect(odd.nitrousKw).toBe(0);
    // La boîte reste lue à sa place, devant le groupe du socle.
    const both = await read(`${BASE}~sequential,6,close,0,spur,0,0,500,single,~egr=1`);
    expect(both.boxKind).toBe('sequential');
    expect(both.egr).toBe(true);
  });
});

test.describe('le même ordre partout', () => {
  test('`TUNING_FIELDS` suit la signature de `rf_set_tuning` et les champs ajoutés à `EngineConfig`', async ({ page }) => {
    await open(page);
    const keys: string[] = await page.evaluate(() => (window as any).__revforge.tuningFields.map((field: any) => field.key));
    const exports = readFileSync(new URL('../../wasm/exports.cpp', import.meta.url), 'utf-8');
    const signature = exports.match(/void rf_set_tuning\(([^)]*)\)/)![1].split(',').map((param) => param.trim().split(/\s+/).pop());
    expect(signature).toEqual(keys);
    // Les champs d'`EngineConfig`, après `fuelSystem` : même ordre, puisque gen_presets.py les écrit à la suite.
    const header = readFileSync(new URL('../../core/include/revforge/engine_config.hpp', import.meta.url), 'utf-8');
    const struct = header.slice(header.indexOf('struct EngineConfig {'));
    const tail = struct.slice(struct.indexOf('\n', struct.indexOf('FuelSystem fuelSystem')), struct.indexOf('bool operator=='));
    const fields = [...tail.matchAll(/^\s+[\w:]+\s+(\w+)\s*=/gm)].map((match) => match[1]);
    expect(fields).toEqual(keys);
    // Et ce que la page envoie : un nombre par réglage, l'énumération par sa position.
    const args: number[] = await page.evaluate((changed) => {
      const { tuningArgs, config } = (window as any).__revforge;
      return tuningArgs({ ...config(), ...changed });
    }, CHANGED);
    expect(args).toEqual([-12, 2400, 1, 12.3, 4, -7, 0.82, 2, 4800, 1, 1, 1, 1, 1, 286, 12.4, 106, 2, 1, 2, 1, 1, 2, 1, 110, 1]);
  });
});

test.describe('les commandes, là où elles ont un sens', () => {
  test('un atmosphérique à essence : ses sections, sans ce qui est diesel ou turbo', async ({ page }) => {
    const errors = await open(page);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    for (const id of ['block', 'combustion', 'valvetrain', 'fueling', 'charging', 'ecu']) await expect(panel.getByTestId(`section-${id}`)).toBeVisible();
    for (const id of ['cycle', 'compression', 'fuel-grade', 'cam-duration', 'cam-lift', 'lsa', 'valve-timing', 'valve-springs', 'fuel-system', 'egr', 'water', 'nitrous', 'advance', 'lambda', 'rev-limiter', 'launch', 'flat-shift', 'traction']) {
      await expect(panel.getByTestId(id), id).toBeVisible();
    }
    for (const id of ['diesel-injection', 'dpf', 'turbo-layout', 'vgt', 'boost-control', 'intercooler']) await expect(panel.getByTestId(id), id).toHaveCount(0);
    // L'injection directe amène le filtre à particules.
    await panel.getByTestId('fuel-system').getByRole('button', { name: 'Injection directe' }).click();
    await expect(panel.getByTestId('dpf')).toBeVisible();
    expect(errors).toEqual([]);
  });

  test('un diesel turbo : injection, filtre et suralimentation, sans avance, richesse, octane, eau ni protoxyde', async ({ page }) => {
    const errors = await open(page, DIESEL);
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    for (const id of ['diesel-injection', 'dpf', 'egr', 'turbo-layout', 'vgt', 'boost-control', 'intercooler', 'compression', 'rev-limiter', 'launch']) await expect(panel.getByTestId(id), id).toBeVisible();
    for (const id of ['fuel-system', 'fuel-grade', 'advance', 'lambda', 'water', 'nitrous']) await expect(panel.getByTestId(id), id).toHaveCount(0);
    expect(errors).toEqual([]);
  });

  test('un rotatif et un deux-temps n’ont pas de distribution ; la chambre de détente se propose au deux-temps', async ({ page }) => {
    const errors = await open(page, ROTARY);
    await ouvrir(page, 'engine');
    let panel = page.getByTestId('panel-engine');
    await expect(panel.getByTestId('section-valvetrain')).toHaveCount(0);
    await expect(panel.getByTestId('cycle')).toHaveCount(0);
    await expect(panel.getByTestId('egr')).toHaveCount(0);

    await open(page);
    await ouvrir(page, 'exhaust');
    await expect(page.getByTestId('panel-exhaust').getByRole('button', { name: 'Chambre de détente' })).toHaveCount(0);
    await ouvrir(page, 'engine');
    panel = page.getByTestId('panel-engine');
    await panel.getByTestId('cycle').getByRole('button', { name: 'Deux temps' }).click();
    await expect(panel.getByTestId('section-valvetrain')).toHaveCount(0);
    await expect(page.getByTestId('open-engine-summary')).toContainText('deux temps');
    await ouvrir(page, 'exhaust');
    await page.getByTestId('panel-exhaust').getByRole('button', { name: 'Chambre de détente' }).click();
    expect(await link(page)).toMatch(/^4\.inline\.2000\.84\.6800\.expansion\./);
    expect(errors).toEqual([]);
  });

  test('les conditions, dans le volet de la voiture : l’électrique garde l’air et l’altitude, pas le départ à froid', async ({ page }) => {
    let errors = await open(page);
    await ouvrir(page, 'car');
    const panel = page.getByTestId('panel-car');
    for (const id of ['ambient', 'altitude', 'start-temp']) await expect(panel.getByTestId(id), id).toBeVisible();
    await panel.getByTestId('start-temp').getByRole('button', { name: 'Froid' }).click();
    await expect(page.getByTestId('open-car-summary')).toContainText('départ à froid');
    expect(await link(page)).toContain('~~start=cold');
    expect(errors).toEqual([]);

    errors = await open(page, 'electric~compact.1800.19.rear');
    await ouvrir(page, 'car');
    await expect(page.getByTestId('panel-car').getByTestId('ambient')).toBeVisible();
    await expect(page.getByTestId('panel-car').getByTestId('altitude')).toBeVisible();
    await expect(page.getByTestId('panel-car').getByTestId('start-temp')).toHaveCount(0);
    expect(errors).toEqual([]);
  });
});

test('un réglage choisi passe dans le lien et le résumé, se relit, et le moteur démarre avec', async ({ page }) => {
  const errors = await open(page, TURBO);
  await ouvrir(page, 'engine');
  const panel = page.getByTestId('panel-engine');
  // Le résumé ne dit rien de ce qui est au défaut.
  const before = await page.getByTestId('open-engine-summary').textContent();
  expect(before).not.toContain('·  ·');
  await panel.getByTestId('turbo-layout').getByRole('button', { name: 'Deux en parallèle' }).click();
  await panel.getByTestId('compression').getByRole('button', { name: 'Réglé' }).click();
  await panel.getByTestId('launch').getByRole('button', { name: 'Monté' }).click();
  await panel.getByTestId('flat-shift').getByRole('button', { name: 'Oui' }).click();
  const summary = page.getByTestId('open-engine-summary');
  await expect(summary).toContainText('Deux en parallèle');
  await expect(summary).toContainText('9,5:1');
  await expect(summary).toContainText('launch 4000 tr/min');
  await expect(summary).toContainText('passage à fond');
  const shared = await link(page);
  expect(shared.split('~')[3]).toBe('cr=9.5,launch=4000,flat=1,turbos=twin_parallel');

  // Relu depuis le lien : les mêmes choix, et le moteur tourne (le worklet reçoit `rf_set_tuning`).
  await open(page, shared);
  await expect(page.getByTestId('open-engine-summary')).toContainText('Deux en parallèle');
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
  expect(errors).toEqual([]);
});
