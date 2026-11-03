import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { ouvrir } from './aide';

const LINK = '6.inline.2500.84.6400.stock.natural.petrol.0.off.dump_valve~retro.1150.14.rear.1_front_light_single';

/** Ce que dit le source de chaque carrosserie, côté physique. */
function bodySpecsOf() {
  const source = readFileSync(new URL('../../core/src/powertrain.cpp', import.meta.url), 'utf-8');
  const specs: Record<string, { exhaustScale: number; length: number; height: number; rearAxle: number; wheelbase: number }> = {};
  for (const match of source.matchAll(/static const BodySpec (\w+)\{([^}]*)\}/g)) {
    const v = match[2].split(',').map((item) => parseFloat(item));
    specs[match[1]] = { wheelbase: v[1], exhaustScale: v[4], length: v[7], height: v[9], rearAxle: v[10] };
  }
  return specs;
}

test('sans navigateur : le dessin et le gaz recopient bien chaque carrosserie du C++', () => {
  const specs = bodySpecsOf();
  expect(Object.keys(specs)).toContain('retro');
  const stage = readFileSync(new URL('../src/components/CarStage.svelte', import.meta.url), 'utf-8');
  const gas = readFileSync(new URL('../src/lib/fluidGas.ts', import.meta.url), 'utf-8');
  const scales = gas.match(/LINE_SCALE[^=]*= \{([^}]*)\}/)![1];
  for (const [name, spec] of Object.entries(specs)) {
    const shape = stage.match(new RegExp(`${name}: \\{\\s*length: ([\\d.]+), height: ([\\d.]+), rearAxle: ([\\d.]+), wheelbase: ([\\d.]+)`));
    expect(shape, name).not.toBeNull();
    expect(shape!.slice(1).map(Number), name).toEqual([spec.length, spec.height, spec.rearAxle, spec.wheelbase]);
    expect(Number(scales.match(new RegExp(`${name}: ([\\d.]+)`))![1]), name).toBe(spec.exhaustScale);
  }
  // La position donne la valeur envoyée au C++ : la nouvelle est la dernière de chaque liste.
  const header = readFileSync(new URL('../../core/include/revforge/powertrain.hpp', import.meta.url), 'utf-8');
  expect(header).toMatch(/enum class Body \{[^}]*Van, Retro \}/);
  const config = readFileSync(new URL('../src/lib/config.ts', import.meta.url), 'utf-8');
  const bodies = config.slice(config.indexOf('export const BODIES'), config.indexOf('export const DRIVES'));
  expect([...bodies.matchAll(/value: '(\w+)'/g)].map((item) => item[1])).toEqual(['city', 'compact', 'coupe', 'supercar', 'van', 'retro']);
});

const pressed = (page: Page, group: string, name: string) => page.getByRole('group', { name: group, exact: true }).getByRole('button', { name, exact: true });

test('choisir la carrosserie règle masse, jantes, transmission et isolation, et rend celle-ci en la quittant', async ({ page }) => {
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  await pressed(page, 'Carrosserie', 'Coupé années 80').click();
  await expect(pressed(page, 'Carrosserie', 'Coupé années 80')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed(page, 'Transmission', 'Propulsion')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed(page, 'Insonorisation', 'Sportive')).toHaveAttribute('aria-pressed', 'true');
  const hash = await page.evaluate(() => location.hash);
  expect(hash).toContain('~retro.1100.14.rear.');
  expect(hash).toContain('_front_light_');
  // La fiche suit : cinq rapports, sans qu'on les ait demandés.
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().ratios.length)).toBe(5);
  // En repartant, l'isolation imposée s'en va ; une isolation choisie à la main reste.
  await pressed(page, 'Carrosserie', 'Compacte').click();
  await expect(pressed(page, 'Insonorisation', 'Normale')).toHaveAttribute('aria-pressed', 'true');
  await pressed(page, 'Insonorisation', 'Confort').click();
  await pressed(page, 'Carrosserie', 'Utilitaire').click();
  await expect(pressed(page, 'Insonorisation', 'Confort')).toHaveAttribute('aria-pressed', 'true');
});

test('un lien avec cette carrosserie se relit, et les anciens liens restent valides', async ({ page }) => {
  await page.goto(`./?test#m=${LINK}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  await expect(pressed(page, 'Carrosserie', 'Coupé années 80')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed(page, 'Insonorisation', 'Sportive')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.getByTestId('car-stage')).toHaveAttribute('aria-label', /Coupé années 80/);
  // Le lien réécrit par la page garde tout : la relecture donne la même voiture.
  await pressed(page, 'Insonorisation', 'Course').click();
  const hash = await page.evaluate(() => location.hash);
  expect(hash).toContain('~retro.1150.14.rear.1_front_stripped_single');
  await page.goto('about:blank');
  await page.goto(`./?test${hash}`);
  await ouvrir(page, 'car');
  await expect(pressed(page, 'Carrosserie', 'Coupé années 80')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed(page, 'Insonorisation', 'Course')).toHaveAttribute('aria-pressed', 'true');

  // Une carrosserie inconnue laisse celle par défaut, comme tout lien mal formé.
  await page.goto('about:blank');
  await page.goto('./?test#m=4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~inconnue.1100.14.rear.1');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  await expect(pressed(page, 'Carrosserie', 'Citadine')).toHaveAttribute('aria-pressed', 'true');
  // Un ancien lien, sans les champs ajoutés.
  await page.goto('about:blank');
  await page.goto('./?test#m=6.v.3000.86.6500.sport.natural.petrol.0.off.dump_valve~coupe.1600.19.rear.1');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  await expect(pressed(page, 'Carrosserie', 'Coupé')).toHaveAttribute('aria-pressed', 'true');
});

test('le préréglage monte cette carrosserie, et son image se distingue des autres', async ({ page }) => {
  const shot = async (body: string) => {
    await page.goto('about:blank');
    await page.goto(`./?test#m=4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~${body}.1150.14.rear.1`);
    await expect(page.getByTestId('car-stage')).toBeVisible();
    // Le haut de l'image seulement : au-dessus des balises, rien n'y bouge moteur coupé.
    return page.getByTestId('car-stage').locator('canvas').evaluate(async (canvas: HTMLCanvasElement) => {
      await new Promise((done) => setTimeout(done, 400));
      const top = document.createElement('canvas');
      top.width = canvas.width;
      top.height = Math.floor(canvas.height * 0.45);
      top.getContext('2d')!.drawImage(canvas, 0, 0);
      return top.toDataURL();
    });
  };
  // La même jante et la même masse : seule la caisse change l'image. Deux fois la même image : sinon, ce test ne prouverait rien.
  const retro = await shot('retro');
  expect(await shot('retro')).toBe(retro);
  expect(retro).not.toBe(await shot('compact'));
  expect(retro).not.toBe(await shot('coupe'));

  await page.goto('about:blank');
  await page.goto('./?test');
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('retro_coupe');
  await expect.poll(() => page.evaluate(() => location.hash)).toContain('~retro.1150.14.rear.');
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().ratios.length)).toBe(5);
});

test('téléphone : le choix de carrosserie ne déborde pas', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 740 });
  await page.goto(`./?test#m=${LINK}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  // Le volet défile et coupe ce qui dépasse : on regarde les boutons eux-mêmes, pas sa largeur.
  const buttons = page.getByRole('group', { name: 'Carrosserie', exact: true }).getByRole('button');
  await expect(buttons).toHaveCount(6);
  for (const button of await buttons.all()) {
    const box = (await button.boundingBox())!;
    expect(box.x).toBeGreaterThanOrEqual(0);
    expect(box.x + box.width).toBeLessThanOrEqual(360);
  }
});
