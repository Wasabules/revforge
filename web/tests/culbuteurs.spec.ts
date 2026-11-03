import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { chainCircles, chainPath, DEG, engineLayout, EnginePose, layoutKey, lifterTop, lobeNoseDeg, lobeRadius, PUSHROD_ROCKER_RATIO, rockerAngle, rockerPad, valveSide } from '../src/lib/engineLayout';
import { gasKey } from '../src/lib/fluid';
import { EVC, EVO, IVC, IVO } from '../src/lib/fluidGas';
import { twoValves, valveSizes } from '../src/lib/valves';

/* eslint-disable @typescript-eslint/no-explicit-any */

// Les captures vont où l'on veut les regarder (`REVFORGE_SHOTS`), jamais dans le dépôt.
const SHOTS = process.env.REVFORGE_SHOTS;

const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
  insulation: 'standard', boxFlywheel: 'single', camshaft: 'ohv',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig>): EngineConfig => ({ ...BASE, ...over });
const FAMILIES: Partial<EngineConfig>[] = [
  { cylinders: 4 }, { cylinders: 6 }, { cylinders: 6, layout: 'v' }, { cylinders: 8, layout: 'cross_plane' }, { cylinders: 8, layout: 'v' },
  { cylinders: 4, layout: 'boxer' }, { cylinders: 2, layout: 'v' }, { cylinders: 12, layout: 'v' },
];

test.describe('culbuteurs, sans navigateur', () => {
  test('deux soupapes en ligne, de la taille de celles du C++, et un rapport de culbuteur recopié', () => {
    const source = readFileSync(new URL('../../core/src/valve_float.cpp', import.meta.url), 'utf-8');
    expect(PUSHROD_ROCKER_RATIO).toBe(Number(/constexpr double kOhvRatio = ([\d.]+);/.exec(source)![1]));
    expect(twoValves('ohv')).toBe(true);
    expect(twoValves('sohc')).toBe(true);
    expect(twoValves('dohc')).toBe(false);
    for (const bore of [70, 86, 101.6]) expect(valveSizes('ohv', bore)).toEqual(valveSizes('sohc', bore));
    for (const family of FAMILIES) {
      const layout = engineLayout(make(family));
      const { valve } = layout;
      expect(valve.inline).toBe(true);
      expect(valve.pushrod).not.toBeNull();
      expect(valve.perCylinder).toBe(2);
      expect(valve.rocker!.ratio).toBe(PUSHROD_ROCKER_RATIO);
      expect(valve.camLift * PUSHROD_ROCKER_RATIO).toBeCloseTo(valve.lift, 6);
      for (const cylinder of layout.cylinders) {
        // En ligne : les deux soupapes du même côté en travers, de part et d'autre le long du vilebrequin.
        expect(valveSide(valve, cylinder, false)).toBe(valveSide(valve, cylinder, true));
      }
      // Les têtes tiennent dans l'alésage, le long du vilebrequin.
      expect(valve.zOffset + valve.radiusIn).toBeLessThan(layout.bore / 2);
      // Les pièces changent : la vue et le gaz doivent se reconstruire.
      expect(layoutKey(make({ ...family, camshaft: 'ohv' }))).not.toBe(layoutKey(make({ ...family, camshaft: 'sohc' })));
      expect(gasKey(make({ ...family, camshaft: 'ohv' }))).not.toBe(gasKey(make({ ...family, camshaft: 'sohc' })));
    }
  });

  test('l\'arbre dans le bloc : un seul pour tout le moteur, au-dessus des manivelles, au bout d\'une chaîne courte', () => {
    for (const family of FAMILIES) {
      const layout = engineLayout(make(family));
      const sohc = engineLayout(make({ ...family, camshaft: 'sohc' }));
      const { valve } = layout;
      // Vu de face, chaque rangée voit l'arbre au même endroit.
      const seen = layout.byBank.map((bank) => {
        const beta = layout.banks === 1 ? 0 : bank[0].bank === 0 ? -layout.halfDeg : layout.halfDeg;
        const x = valveSide(valve, bank[0], false) * valve.camX;
        const b = beta * DEG;
        return [x * Math.cos(b) + valve.camY * Math.sin(b), -x * Math.sin(b) + valve.camY * Math.cos(b)];
      });
      for (const point of seen) {
        expect(point[0]).toBeCloseTo(seen[0][0], 6);
        expect(point[1]).toBeCloseTo(seen[0][1], 6);
      }
      // Les manivelles et leurs contrepoids passent sous lui, et son bossage aussi.
      const swing = Math.max(layout.crank * 1.25 + layout.bore * 0.1, layout.mainRadius * 1.6);
      expect(Math.hypot(seen[0][0], seen[0][1])).toBeGreaterThan(swing + valve.camBase + valve.camLift);
      // Une seule roue, plus près du vilebrequin qu'en tête : la chaîne est plus courte.
      expect(layout.sprocket.z.length).toBe(1);
      const circles = chainCircles(layout, 0);
      expect(circles.length).toBe(2);
      expect(Math.hypot(circles[1][0], circles[1][1])).toBeCloseTo(Math.hypot(seen[0][0], seen[0][1]), 6);
      const pitch = (layout.sprocket.crankRadius * 2 * Math.PI) / layout.sprocket.crankTeeth;
      expect(chainPath(circles, pitch).length).toBeLessThan(chainPath(chainCircles(sohc, 0), pitch).length);
    }
  });

  test('le poussoir repose sur son bossage, la tige garde sa longueur, le culbuteur pose sur la queue, à tout angle', () => {
    const lifted = [0, 0];
    const pad = [0, 0];
    for (const family of FAMILIES) {
      for (const boreMm of [74, 88, 102]) {
        const layout = engineLayout(make({ ...family, boreMm, displacementCc: (2000 * (family.cylinders ?? 4)) / 4 }));
        const pose = new EnginePose(layout);
        const { valve } = layout;
        const rocker = valve.rocker!;
        const rod = valve.pushrod!;
        let shortest = Infinity;
        let longest = 0;
        let swing = 0;
        // Les écarts, cumulés puis vérifiés une fois : des dizaines de milliers d'`expect` ralentissent tant le rapport qu'il n'en finit pas.
        let lobeMiss = 0;
        let angleMiss = 0;
        let lean = 0;
        let stemMiss = 0;
        for (let crankDeg = 0; crankDeg < 720; crankDeg += 9) {
          pose.solve(crankDeg);
          for (const cylinder of layout.cylinders) {
            for (const exhaust of [false, true]) {
              const share = exhaust ? pose.liftEx[cylinder.index] : pose.liftIn[cylinder.index];
              const side = valveSide(valve, cylinder, exhaust);
              const open = exhaust ? EVO : IVO;
              const close = exhaust ? EVC : IVC;
              // Le bossage, dans l'axe du poussoir, après `camDeg` de rotation : il pousse exactement de la levée divisée par le rapport.
              const nose = lobeNoseDeg(cylinder, valve.followerDeg, side, open, close);
              const towardsLifter = cylinder.betaDeg + side * valve.followerDeg + 180 - pose.camDeg;
              lobeMiss = Math.max(lobeMiss, Math.abs(lobeRadius(valve.camBase, valve.camLift, towardsLifter - nose, open, close) - (valve.camBase + valve.camLift * share)));
              const angle = exhaust ? pose.rockerEx[cylinder.index] : pose.rockerIn[cylinder.index];
              angleMiss = Math.max(angleMiss, Math.abs(angle - rockerAngle(valve, share * valve.lift)));
              swing = Math.max(swing, angle);
              lifterTop(valve, share, lifted);
              rockerPad(valve, angle, pad);
              const length = Math.hypot(pad[0] - lifted[0], pad[1] - lifted[1]);
              shortest = Math.min(shortest, length);
              longest = Math.max(longest, length);
              // La tige reste presque dans l'axe de son poussoir.
              lean = Math.max(lean, Math.abs(Math.atan2(pad[0] - lifted[0], pad[1] - lifted[1]) - Math.atan2(rod.axisX, rod.axisY)));
              // Le bout du culbuteur suit la queue de soupape, qui coulisse sur son axe.
              const tip = rocker.tipDeg * DEG - angle;
              const tipX = rocker.pivotX + rocker.tipArm * Math.cos(tip);
              const tipY = rocker.pivotY + rocker.tipArm * Math.sin(tip);
              const tilt = valve.tiltDeg * DEG;
              const along = (tipX - valve.x) * Math.sin(tilt) + (tipY - layout.deck - valve.seatRise) * Math.cos(tilt);
              stemMiss = Math.max(stemMiss, Math.abs(along - (valve.stem - share * valve.lift)));
            }
          }
        }
        // La tige monte à côté de l'alésage : au plan de joint, elle est hors du cylindre.
        lifterTop(valve, 0, lifted);
        rockerPad(valve, 0, pad);
        const atDeck = lifted[0] + ((pad[0] - lifted[0]) * (layout.deck - lifted[1])) / (pad[1] - lifted[1]);
        expect(Math.abs(atDeck)).toBeGreaterThan(boreMm / 2);
        expect(lobeMiss).toBeLessThan(1e-6);
        expect(angleMiss).toBeLessThan(1e-9);
        expect(lean).toBeLessThan(6 * DEG);
        expect(stemMiss).toBeLessThan(1e-6);
        // Une tige rigide : sa longueur ne bouge que de ce que le culbuteur et le poussoir ne suivent pas tout à fait la même droite.
        expect(longest - shortest).toBeLessThan(boreMm * 0.03);
        expect(shortest).toBeGreaterThan(boreMm * 0.8);
        expect(swing).toBeGreaterThan(10 * DEG);
        expect(swing).toBeLessThan(25 * DEG);
      }
    }
  });
});

// ── Le navigateur ──

const OHV_V8 = '#m=8.cross_plane.5000.96.6000.sport.natural.petrol.0.off.dump_valve~coupe.1600.18.rear.1_natural_standard_dual_medium_roots_off_400_ohv_stock';
const OHV_FOUR = '#m=4.inline.2000.86.6500.stock.natural.petrol.0.off.dump_valve~compact.1200.15.front.1_natural_standard_single_medium_roots_off_400_ohv_stock';

async function open(page: Page, hash: string) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
  return errors;
}

const ready3d = (page: Page) => page.waitForFunction(() => {
  const d = (document.querySelector('[data-testid=engine-view]') as any)?.__engine;
  return d && d.live3d() && d.scene() && d.drawn > 0;
}, null, { timeout: 30_000 });

test.describe('culbuteurs, dans la page', () => {
  test('la vue : un arbre dans le bloc, un poussoir et une tige par soupape, qui montent avec la came, en 3D et en 2D', async ({ page }) => {
    // Deux moteurs, deux modes, et des captures sur demande : plus long qu'un test ordinaire.
    test.setTimeout(180_000);
    for (const [hash, name, count] of [[OHV_V8, 'v8', 8], [OHV_FOUR, 'quatre', 4]] as const) {
      await page.goto('about:blank');
      const errors = await open(page, hash);
      await ready3d(page);
      const shape = await page.evaluate(() => {
        const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
        const scene = d.scene();
        return { inline: d.layout().valve.inline, cams: scene.cams.length, chains: scene.chains.length, lifters: scene.d.lifters?.count ?? 0, pushrods: scene.d.pushrods?.count ?? 0, rockers: scene.d.rockers?.count ?? 0, buckets: scene.d.buckets?.count ?? 0 };
      });
      expect(shape).toEqual({ inline: true, cams: 1, chains: 1, lifters: count * 2, pushrods: count * 2, rockers: count * 2, buckets: 0 });
      await page.getByTestId('start').click();
      await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
      await expect.poll(() => page.evaluate(() => {
        const pose = (document.querySelector('[data-testid=engine-view]') as any).__engine.pose();
        return Math.max(...pose.rockerIn, ...pose.rockerEx);
      }), { timeout: 15_000 }).toBeGreaterThan(0.01);
      if (SHOTS) {
        await page.waitForTimeout(600);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue3d_${name}.png` });
        await page.getByTestId('engine-names').click();
        await page.waitForTimeout(300);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue3d_noms_${name}.png` });
        await page.getByTestId('engine-names').click();
      }
      await page.getByTestId('engine-mode-2d').click();
      await expect.poll(() => page.getByTestId('engine-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
        const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
        let painted = 0;
        for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
        return painted;
      })).toBeGreaterThan(5000);
      if (SHOTS) {
        await page.waitForTimeout(600);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue2d_${name}.png` });
        await page.getByTestId('engine-names').click();
        await page.waitForTimeout(300);
        await page.getByTestId('engine-view').screenshot({ path: `${SHOTS}/vue2d_noms_${name}.png` });
        await page.getByTestId('engine-names').click();
      }
      await page.getByTestId('engine-mode-3d').click();
      expect(errors).toEqual([]);
    }
  });

  test('le volet : culbuteurs et ressorts, l\'affolement calculé, au-dessus puis au-dessous de la zone rouge', async ({ page }) => {
    const errors = await open(page, '#m=4.inline.2000.86.6500.stock.natural.petrol.0.off.dump_valve~compact.1200.15.front.1');
    await ouvrir(page, 'engine');
    const floatRpm = async () => Number((await page.getByTestId('valve-float').textContent())!.match(/(\d[\d\s  ]*) tr\/min/)![1].replace(/\D/g, ''));
    await expect(page.getByTestId('valve-float')).toContainText('au-dessus de la zone rouge');
    const twin = await floatRpm();
    expect(twin).toBeGreaterThan(6500);
    await page.getByRole('group', { name: 'Distribution' }).getByRole('button', { name: 'Culbuteurs' }).click();
    await expect(page.getByTestId('open-engine-summary')).toContainText('Culbuteurs');
    await expect(page.getByTestId('camshaft-help')).toContainText('tiges');
    await page.waitForFunction(() => /_ohv_/.test(location.hash));
    // Plus de masse en mouvement, une chaîne plus souple : la même marge de dessin en garde moins.
    await expect.poll(floatRpm).toBeLessThan(twin);
    await expect(page.getByTestId('valve-parts')).toContainText('g en mouvement');
    const stock = await floatRpm();
    await page.getByTestId('valve-springs').getByRole('button', { name: 'Renforcés' }).click();
    await expect(page.getByTestId('open-engine-summary')).toContainText('ressorts renforcés');
    await expect.poll(floatRpm).toBeGreaterThan(stock * 1.1);
    await page.waitForFunction(() => /springs=stiff/.test(location.hash));
    if (SHOTS) await page.getByTestId('panel-engine').screenshot({ path: `${SHOTS}/volet_culbuteurs.png` });
    expect(errors).toEqual([]);
  });

  test('une came pointue sur des ressorts d\'origine : l\'affolement passe sous la zone rouge, et le volet le dit', async ({ page }) => {
    const errors = await open(page, `${OHV_FOUR}~~lift=12.5,dur=200`);
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('valve-float')).toContainText('sous la zone rouge');
    if (SHOTS) {
      await page.getByTestId('section-valvetrain').screenshot({ path: `${SHOTS}/volet_affolement.png` });
      await page.setViewportSize({ width: 360, height: 780 });
      await page.waitForTimeout(300);
      await page.getByTestId('section-valvetrain').scrollIntoViewIfNeeded();
      await page.screenshot({ path: `${SHOTS}/volet_tel.png` });
      const scrollable = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1);
      expect(scrollable).toBe(false);
      await page.keyboard.press('Escape');
      await voir(page, 'engine');
      await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
      await page.waitForTimeout(800);
      await page.screenshot({ path: `${SHOTS}/vue_tel.png` });
    }
    expect(errors).toEqual([]);
  });
});
