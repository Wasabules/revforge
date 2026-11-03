import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { engineLayout, EnginePose } from '../src/lib/engineLayout';
import { GasNetwork } from '../src/lib/fluidGas';
import { firingOrder } from '../src/lib/motion';
import { DISPLACEMENT_LIMITS, PISTON_SPEED_LIMIT, STROKE_LIMITS, displacementFor, engineStroke, moveStroke, pistonSpeed, strokeOutside, strokeRange, strokeShape, strokeThumb, type StrokeAnchor } from '../src/lib/stroke';

/* eslint-disable @typescript-eslint/no-explicit-any */

const CYLINDERS = [2, 3, 4, 5, 6, 8, 10, 12];
const BORES = Array.from({ length: 51 }, (_, i) => 60 + i);
const V10 = { displacementCc: 5204, boreMm: 85, cylinders: 10 };

/** Le moteur après un passage du curseur : le glissé et le relâchement, comme le site. */
function drag(engine: typeof V10, target: number, anchor: StrokeAnchor | null) {
  const live = moveStroke(engine, target, anchor);
  const settled = moveStroke({ ...engine, displacementCc: live.displacementCc }, target, live.anchor);
  expect(settled.displacementCc).toBe(live.displacementCc);
  return { engine: { ...engine, displacementCc: settled.displacementCc }, anchor: settled.anchor };
}

test.describe('le calcul, sans navigateur', () => {
  test('chaque cran du curseur, sur tout moteur, revient sur lui-même avec une cylindrée entière et dans ses limites', () => {
    // Pas d'`expect` dans ces boucles : 65 000 crans en feraient 400 000, et leur coût croît avec leur nombre,
    // au point de figer le test pendant des heures sans que le délai de Playwright puisse l'interrompre.
    const wrong: string[] = [];
    let notches = 0;
    for (const cylinders of CYLINDERS) {
      for (const boreMm of BORES) {
        const range = strokeRange(boreMm, cylinders);
        const at = `${cylinders} × ${boreMm} mm`;
        if (!(range.min < range.max)) wrong.push(`${at} : plage vide`);
        if (range.min < STROKE_LIMITS.min || range.max > STROKE_LIMITS.max) wrong.push(`${at} : plage trop large`);
        // Un navigateur compte ses crans depuis `min` : il doit tomber sur la grille, ou le curseur sauterait.
        if (!Number.isInteger(range.min / range.step)) wrong.push(`${at} : ${range.min} hors de la grille`);
        for (let mm = range.min; mm <= range.max; mm += range.step) {
          notches++;
          const displacementCc = displacementFor(boreMm, cylinders, mm);
          const engine = { displacementCc, boreMm, cylinders };
          if (!Number.isInteger(displacementCc)) wrong.push(`${at}, ${mm} mm : ${displacementCc} cm³ n'est pas entier`);
          if (displacementCc < DISPLACEMENT_LIMITS.min || displacementCc > DISPLACEMENT_LIMITS.max) wrong.push(`${at}, ${mm} mm : ${displacementCc} cm³ hors limites`);
          if (strokeThumb(engine) !== mm) wrong.push(`${at}, ${mm} mm : le curseur revient à ${strokeThumb(engine)}`);
          if (Math.abs(engineStroke(engine) - mm) >= 0.09) wrong.push(`${at}, ${mm} mm : course réelle ${engineStroke(engine)}`);
          if (strokeOutside(engine)) wrong.push(`${at}, ${mm} mm : dit hors plage`);
        }
      }
    }
    expect(wrong.slice(0, 20)).toEqual([]);
    // Le balayage a bien eu lieu.
    expect(notches).toBeGreaterThan(60000);
  });

  test('aller et revenir rend le moteur exact, 5204 cm³ compris, sans dérive', () => {
    const start = strokeThumb(V10);
    expect(engineStroke(V10)).not.toBe(start);
    let now = { engine: V10, anchor: null as StrokeAnchor | null };
    for (let turn = 0; turn < 200; turn++) {
      now = drag(now.engine, start + 7.5, now.anchor);
      expect(now.engine.displacementCc).toBe(displacementFor(85, 10, start + 7.5));
      now = drag(now.engine, start - 12, now.anchor);
      expect(now.engine.displacementCc).toBe(displacementFor(85, 10, start - 12));
      now = drag(now.engine, start, now.anchor);
      expect(now.engine.displacementCc).toBe(5204);
    }
    // Sans ce souvenir, le cran de départ donnerait une autre cylindrée : c'est bien lui qui rend 5204.
    expect(displacementFor(85, 10, start)).not.toBe(5204);
  });

  test('ce souvenir s\'oublie dès que quelqu\'un d\'autre règle le moteur', () => {
    const away = drag(V10, strokeThumb(V10) + 10, null);
    // La cylindrée, puis le retour au cran d'origine : plus rien à restaurer.
    const other = { ...away.engine, displacementCc: 5600 };
    const back = moveStroke(other, strokeThumb(V10), away.anchor);
    expect(back.displacementCc).toBe(displacementFor(85, 10, strokeThumb(V10)));
    expect(back.displacementCc).not.toBe(5204);
    // Le cran où se trouve déjà le curseur ne réécrit pas la cylindrée qu'un autre a choisie.
    const nearby = { ...away.engine, displacementCc: 5200 };
    expect(moveStroke(nearby, strokeThumb(nearby), away.anchor).displacementCc).toBe(5200);
    // L'alésage, à cylindrée inchangée.
    const rebored = { ...away.engine, boreMm: 90 };
    expect(moveStroke(rebored, 80, away.anchor).displacementCc).toBe(displacementFor(90, 10, 80));
    // Les cylindres.
    const fewer = { ...away.engine, cylinders: 8 };
    expect(moveStroke(fewer, 80, away.anchor).displacementCc).toBe(displacementFor(85, 8, 80));
  });

  test('hors plage : rien n\'est modifié, et revenir à la butée ne fait pas bondir la course', () => {
    const long = { displacementCc: 7000, boreMm: 60, cylinders: 12 };
    expect(engineStroke(long)).toBeCloseTo(206.3, 1);
    expect(strokeOutside(long)).toBe(true);
    expect(strokeThumb(long)).toBe(STROKE_LIMITS.max);
    const short = { displacementCc: 600, boreMm: 110, cylinders: 12 };
    expect(strokeOutside(short)).toBe(true);
    expect(strokeThumb(short)).toBe(strokeRange(110, 12).min);
    const moved = drag(long, 129.5, null);
    const back = drag(moved.engine, 130, moved.anchor);
    expect(back.engine.displacementCc).toBe(displacementFor(60, 12, 130));
    expect(engineStroke(back.engine)).toBeCloseTo(130, 0);
  });

  test('une cylindrée limite borne le curseur, et un cran demandé au-delà y revient', () => {
    // 12 cylindres de 110 : 8000 cm³ (le W16 de 7993 cm³ y tient) tombent à 70,2 mm.
    expect(strokeRange(110, 12).max).toBe(70);
    expect(moveStroke({ displacementCc: 6000, boreMm: 110, cylinders: 12 }, 130, null).displacementCc).toBe(displacementFor(110, 12, 70));
    // 2 cylindres de 60 : 600 cm³ font déjà 106 mm.
    expect(strokeRange(60, 2).min).toBe(106.5);
    // Ce moteur, à sa limite, n'est pas hors plage : seule la grille du curseur le sépare de son vrai réglage.
    const smallest = { displacementCc: 600, boreMm: 60, cylinders: 2 };
    expect(strokeOutside(smallest)).toBe(false);
    expect(strokeThumb(smallest)).toBe(106.5);
    expect(moveStroke(smallest, 106.5, null).displacementCc).toBe(600);
  });

  test('carré, super-carré, longue course, et vitesse du piston', () => {
    expect(strokeShape(86, 86.08)).toBe('square');
    expect(strokeShape(96, 69.1)).toBe('over');
    expect(strokeShape(76, 110.2)).toBe('under');
    expect(strokeShape(75, 76.96)).toBe('under');
    // 86 mm à 6800 tr/min : 2 × 0,086 m × 113,3 tr/s.
    expect(pistonSpeed(86, 6800)).toBeCloseTo(19.49, 2);
  });

  test('ce qui recopie le C++ reste juste', () => {
    const powertrain = readFileSync(new URL('../../core/src/powertrain.cpp', import.meta.url), 'utf8');
    expect(powertrain).toContain(`kPistonSpeedLimit = ${PISTON_SPEED_LIMIT}.0;`);
  });

  const BASE = {
    cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 86, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
    multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
    insulation: 'standard', boxFlywheel: 'single', turboSize: 'medium',
  } as unknown as EngineConfig;
  const make = (over: Partial<EngineConfig>): EngineConfig => ({ ...BASE, ...over });

  test('tout ce que le curseur atteint se dessine à l\'échelle', () => {
    const wrong: string[] = [];
    for (const cylinders of CYLINDERS) {
      for (const boreMm of BORES) {
        const range = strokeRange(boreMm, cylinders);
        for (const mm of [range.min, range.max]) {
          const layout = engineLayout(make({ boreMm, cylinders, displacementCc: displacementFor(boreMm, cylinders, mm), layout: cylinders > 6 ? 'v' : 'inline' }));
          if (layout.strokeClamped || Math.abs(layout.stroke - mm) >= 0.09) wrong.push(`${cylinders} × ${boreMm} mm, ${mm} mm : dessiné ${layout.stroke}`);
        }
      }
    }
    expect(wrong.slice(0, 20)).toEqual([]);
  });

  test('la vue et le gaz suivent la course, à cylindrée constante comme à alésage constant', () => {
    const travel = (config: EngineConfig) => {
      const layout = engineLayout(config);
      const pose = new EnginePose(layout);
      let top = -Infinity;
      let bottom = Infinity;
      for (let deg = 0; deg < 720; deg++) {
        pose.solve(deg);
        top = Math.max(top, pose.piston[0]);
        bottom = Math.min(bottom, pose.piston[0]);
      }
      return { layout, travel: top - bottom };
    };
    const gasOf = (config: EngineConfig) => {
      const gas = new GasNetwork() as any;
      gas.build(config, firingOrder(config.cylinders));
      return gas;
    };
    for (const [boreMm, displacementCc] of [[96, 2000], [76, 2000], [86, displacementFor(86, 4, 70)], [86, displacementFor(86, 4, 110)]]) {
      const config = make({ boreMm, displacementCc });
      const stroke = engineStroke(config);
      const drawn = travel(config);
      expect(drawn.travel).toBeCloseTo(stroke, 1);
      expect(drawn.layout.crank).toBeCloseTo(stroke / 2, 6);
      // La hauteur du bloc : la manivelle, la bielle (1,6 course) et le haut du piston.
      expect(drawn.layout.deck).toBeCloseTo(stroke / 2 + stroke * 1.6 + boreMm * 0.36, 6);
      const gas = gasOf(config);
      expect(gas.crank * 2 * 1000).toBeCloseTo(stroke, 6);
      expect(gas.rod * 1000).toBeCloseTo(stroke * 1.6, 6);
      expect(gas.boreArea).toBeCloseTo((Math.PI * (boreMm / 1000) ** 2) / 4, 9);
    }
  });
});

const CARRE = '#m=4.inline.2000.86.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'engine');
  return errors;
}

const slider = (page: Page, name: string) => page.getByRole('slider', { name, exact: true });
const CC = 'Cylindrée';
const BORE = 'Alésage (taille des cylindres)';
const STROKE = 'Course';
/** Ce que dit le curseur : le texte est juste à côté de lui. */
const reading = (page: Page, name: string) => slider(page, name).locator('xpath=preceding-sibling::div/output');
const hash = (page: Page) => page.evaluate(() => location.hash);

test.describe('les trois curseurs', () => {
  test('la course suit la cylindrée, l\'alésage et les cylindres ; elle ne change que la cylindrée', async ({ page }) => {
    const errors = await open(page, CARRE);
    await expect(reading(page, STROKE)).toHaveText('86,1 mm');
    await expect(slider(page, STROKE)).toHaveValue('86');
    await expect(page.getByTestId('stroke-derived')).toContainText('1,00, carré');
    await expect(page.getByTestId('stroke-derived')).toContainText('19,5 m/s');
    await expect(page.getByTestId('open-engine-summary')).toContainText('86×86 mm');

    await slider(page, CC).fill('3000');
    await expect(reading(page, STROKE)).toHaveText('129,1 mm');
    await expect(slider(page, BORE)).toHaveValue('86');
    await slider(page, BORE).fill('100');
    await expect(reading(page, CC)).toHaveText('3000 cm³');
    await expect(reading(page, STROKE)).toHaveText('95,5 mm');
    await expect(page.getByTestId('stroke-derived')).toContainText('super-carré');
    await page.getByRole('button', { name: '6', exact: true }).click();
    await expect(reading(page, CC)).toHaveText('3000 cm³');
    await expect(reading(page, STROKE)).toHaveText('63,7 mm');

    // Le stroker : l'alésage ne bouge pas, la cylindrée suit, et la fiche aussi.
    const hp = await page.evaluate(() => (window as any).__revforge.spec().peakHp);
    await slider(page, STROKE).fill('90');
    await expect(reading(page, BORE)).toHaveText('100 mm');
    await expect(reading(page, CC)).toHaveText('4241 cm³');
    await expect(reading(page, STROKE)).toHaveText('90,0 mm');
    await expect(slider(page, STROKE)).toHaveValue('90');
    expect(await hash(page)).toContain('6.inline.4241.100.');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().peakHp)).toBeGreaterThan(hp * 1.2);
    expect(errors).toEqual([]);
  });

  test('pendant le glissé, la cylindrée suit sans rien écrire ; au relâchement, le lien change', async ({ page }) => {
    await open(page, CARRE);
    const before = await hash(page);
    await slider(page, STROKE).evaluate((node: HTMLInputElement) => {
      node.value = '100';
      node.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await expect(reading(page, CC)).toHaveText('2324 cm³');
    await expect(reading(page, STROKE)).toHaveText('100,0 mm');
    expect(await hash(page)).toBe(before);
    await slider(page, STROKE).dispatchEvent('change');
    await expect.poll(() => hash(page)).toContain('4.inline.2324.86.');
    await expect(slider(page, STROKE)).toHaveValue('100');
  });

  test('un préréglage reste intact et reconnu, même après un aller-retour du curseur', async ({ page }) => {
    await open(page, '');
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption('v10_open');
    await ouvrir(page, 'engine');
    await expect(reading(page, CC)).toHaveText('5204 cm³');
    await expect(reading(page, STROKE)).toHaveText('91,7 mm');
    await expect(slider(page, STROKE)).toHaveValue('91.5');
    const start = await hash(page);
    const preset = page.getByTestId('preset');
    await expect(preset).toHaveValue('v10_open');

    await slider(page, STROKE).fill('95');
    await expect(reading(page, CC)).toHaveText('5391 cm³');
    await expect(preset).not.toHaveValue('v10_open');
    await slider(page, STROKE).fill('91.5');
    await expect(reading(page, CC)).toHaveText('5204 cm³');
    await expect(reading(page, STROKE)).toHaveText('91,7 mm');
    await expect(preset).toHaveValue('v10_open');
    expect((await hash(page)).split('.')[2]).toBe('5204');
    expect(start === '' || (await hash(page)) === start).toBe(true);
  });

  test('le lien de partage garde la course : mêmes valeurs à la relecture, aucun champ de plus', async ({ page }) => {
    await open(page, CARRE);
    await slider(page, STROKE).fill('77.5');
    await expect(reading(page, CC)).toHaveText('1801 cm³');
    const link = await hash(page);
    expect(link.startsWith('#m=4.inline.1801.86.6800.stock.natural.petrol.0.off.dump_valve~')).toBe(true);

    const other = await page.context().newPage();
    await open(other, link);
    await expect(reading(other, CC)).toHaveText('1801 cm³');
    await expect(reading(other, BORE)).toHaveText('86 mm');
    await expect(reading(other, STROKE)).toHaveText('77,5 mm');
    await expect(slider(other, STROKE)).toHaveValue('77.5');
    // Rien n'a réécrit le lien en l'ouvrant.
    expect(await hash(other)).toBe(link);
    await other.close();
  });

  test('un lien hors plage s\'affiche tel qu\'il est, sans rien réécrire', async ({ page }) => {
    const link = '#m=12.v.7000.60.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';
    const errors = await open(page, link);
    await expect(reading(page, CC)).toHaveText('7000 cm³');
    await expect(reading(page, BORE)).toHaveText('60 mm');
    await expect(reading(page, STROKE)).toHaveText('206,3 mm');
    await expect(slider(page, STROKE)).toHaveValue('130');
    await expect(page.getByTestId('stroke-outside')).toBeVisible();
    await expect(page.getByTestId('stroke-fast')).toBeVisible();
    await page.waitForTimeout(200);
    expect(await hash(page)).toBe(link);
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().peakHp)).toBeGreaterThan(0);
    // Le texte s'efface dès qu'on revient dans la plage, et rien d'autre n'a bougé.
    await slider(page, STROKE).fill('100');
    await expect(page.getByTestId('stroke-outside')).toHaveCount(0);
    await expect(reading(page, BORE)).toHaveText('60 mm');
    expect(errors).toEqual([]);
  });

  test('la plage du curseur se resserre pour tenir la cylindrée dans ses limites', async ({ page }) => {
    await open(page, '#m=12.v.6000.110.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1');
    // 8000 cm³ au plus : 70,2 mm sur 12 cylindres de 110, 114,8 sur 12 de 86 (au demi-millimètre du curseur).
    await expect(slider(page, STROKE)).toHaveAttribute('max', '70');
    await slider(page, STROKE).fill('70');
    await expect(reading(page, CC)).toHaveText('7983 cm³');
    await slider(page, BORE).fill('86');
    await expect(slider(page, STROKE)).toHaveAttribute('max', '114.5');
    await expect(reading(page, CC)).toHaveText('7983 cm³');
  });

  test('une vitesse de piston au-delà de la limite du modèle se dit', async ({ page }) => {
    await open(page, CARRE);
    await expect(page.getByTestId('stroke-fast')).toHaveCount(0);
    await slider(page, STROKE).fill('110');
    await expect(page.getByTestId('stroke-derived')).toContainText('longue course');
    await expect(page.getByTestId('stroke-derived')).toContainText('24,9 m/s');
    await expect(page.getByTestId('stroke-fast')).toBeVisible();
  });
});
