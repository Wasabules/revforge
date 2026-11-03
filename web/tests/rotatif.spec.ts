import { mkdirSync, readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { GasNetwork, type GasInputs } from '../src/lib/fluidGas';
import { firingOrder } from '../src/lib/motion';
import { chamberArea, chamberShare, CYCLE_PER_SHAFT, firingsPerCycle, housingPoint, isRotary, ROTOR_COUNTS, rotaryLayout, RotaryPose, rotaryShape, rotorOrder, SHAFT_EVC, SHAFT_EVO, SHAFT_IVC, SHAFT_IVO, SHAFT_PORT_SWEEP } from '../src/lib/rotary';

/* eslint-disable @typescript-eslint/no-explicit-any */

const BASE = {
  cylinders: 2, layout: 'rotary', displacementCc: 1308, boreMm: 84, redlineRpm: 9000, exhaust: 'sport', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'light', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'coupe', massKg: 1300, rimInch: 18, drive: 'rear', placement: 'front',
  insulation: 'standard', boxFlywheel: 'single', turboSize: 'medium',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig> = {}): EngineConfig => ({ ...BASE, ...over });
const SONS = process.env.SONS ?? 'test-results/rotatif';

test.describe('la géométrie, sans navigateur', () => {
  test('R 105, e 15, b 80 redonnent 654 cm³ ; le C++ et le site donnent les mêmes dimensions', () => {
    const shape = rotaryShape((3 * Math.sqrt(3) * 105 * 15 * 80) / 1000);
    expect(shape.radius).toBeCloseTo(105, 6);
    expect(shape.eccentricity).toBeCloseTo(15, 6);
    expect(shape.width).toBeCloseTo(80, 6);
    expect(isRotary(BASE)).toBe(true);
    expect(isRotary({ layout: 'boxer' })).toBe(false);
    expect(firingsPerCycle(BASE)).toBe(4);
    expect(firingsPerCycle({ layout: 'inline', cylinders: 5 })).toBe(5);
  });

  test('l\'ordre des rotors est celui du gaz, sur chaque nombre proposé', () => {
    for (const rotors of ROTOR_COUNTS) expect(rotorOrder(rotors)).toEqual(firingOrder(rotors));
  });

  test('les sommets du rotor restent sur le carter, et le flanc ne le traverse jamais', () => {
    for (const rotors of ROTOR_COUNTS) {
      const layout = rotaryLayout(make({ cylinders: rotors, displacementCc: 654 * rotors }));
      const pose = new RotaryPose(layout);
      const { radius: r, eccentricity: e } = layout;
      const p = [0, 0];
      const inside = (x: number, y: number) => {
        // Le nombre de fois qu'un rayon traverse le contour.
        let hit = false;
        const poly = layout.housing;
        for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
          if (poly[i][1] > y !== poly[j][1] > y && x < ((poly[j][0] - poly[i][0]) * (y - poly[i][1])) / (poly[j][1] - poly[i][1]) + poly[i][0]) hit = !hit;
        }
        return hit;
      };
      // Un `expect` par point dure des minutes : on compte, et on vérifie à la fin.
      let offTrack = 0;
      let crossing = 0;
      for (let shaft = 0; shaft < 1080; shaft += 7) {
        pose.solve(shaft);
        const beta = (pose.rotorDeg[0] * Math.PI) / 180;
        const cos = Math.cos(beta);
        const sin = Math.sin(beta);
        for (let k = 0; k < 3; k++) {
          // Le sommet : à 60° + 120°·k du haut, dans le repère du rotor. Il doit tomber sur la courbe H(u).
          const u = beta + ((60 + 120 * k) * Math.PI) / 180;
          housingPoint(p, r, e, u);
          const ax = pose.rotorX[0] + r * Math.sin(u);
          const ay = pose.rotorY[0] + r * Math.cos(u);
          if (Math.hypot(ax - p[0], ay - p[1]) >= 1e-6) offTrack++;
        }
        // 3 flancs, 40 points chacun : on saute le sommet, qui touche par construction.
        for (let j = 0; j < layout.rotor.length; j++) {
          if (j % 40 < 2) continue;
          const q = layout.rotor[j];
          if (!inside(pose.rotorX[0] + q[0] * cos + q[1] * sin, pose.rotorY[0] - q[0] * sin + q[1] * cos)) crossing++;
        }
      }
      expect(offTrack).toBe(0);
      expect(crossing).toBe(0);
    }
  });

  test('le volume d\'une chambre, mesuré sur le dessin, suit la loi du gaz et donne la cylindrée', () => {
    const layout = rotaryLayout(make());
    const smallest = chamberArea(layout, 0, 0);
    const largest = chamberArea(layout, 270, 0);
    expect(((largest - smallest) * layout.width) / 1000).toBeCloseTo(layout.chamberCc, -1);
    expect(Math.abs((largest - smallest) * layout.width / 1000 - layout.chamberCc) / layout.chamberCc).toBeLessThan(0.02);
    for (let shaft = 0; shaft < 1080; shaft += 45) {
      const share = (chamberArea(layout, shaft, 0) - smallest) / (largest - smallest);
      expect(Math.abs(share - chamberShare(shaft))).toBeLessThan(0.03);
    }
    // Les trois faces se suivent, à un tour d'arbre l'une de l'autre.
    expect(chamberArea(layout, 360, 1)).toBeCloseTo(smallest, 3);
    expect(chamberArea(layout, 720, 2)).toBeCloseTo(smallest, 3);
  });

  test('3 pour 2 : le rotor tourne au tiers de l\'arbre, et les primitifs roulent sans glisser', () => {
    const layout = rotaryLayout(make());
    expect(layout.rotorTeeth / layout.fixedTeeth).toBe(1.5);
    // L'excentricité est exactement l'écart entre les primitifs.
    expect(layout.rotorGearRadius - layout.fixedRadius).toBeCloseTo(layout.eccentricity, 9);
    expect(layout.fixedRadius / layout.rotorGearRadius).toBeCloseTo(layout.fixedTeeth / layout.rotorTeeth, 9);
    const pose = new RotaryPose(layout);
    pose.solve(1080);
    expect(pose.rotorDeg[0]).toBeCloseTo(360, 9);
    // Un tour de l'arbre fait un tour de l'excentrique, et ramène le centre du rotor au même point.
    pose.solve(360);
    expect(pose.rotorY[0]).toBeCloseTo(layout.eccentricity, 9);
    expect(pose.rotorX[0]).toBeCloseTo(0, 9);
  });

  test('à l\'explosion, la face est en haut sous les bougies, et le centre du rotor aussi', () => {
    for (const rotors of ROTOR_COUNTS) {
      const layout = rotaryLayout(make({ cylinders: rotors, displacementCc: 654 * rotors }));
      const pose = new RotaryPose(layout);
      for (const slot of layout.slots) {
        for (let face = 0; face < 3; face++) {
          pose.solve(slot.firingDeg + 360 * face);
          const c = slot.index * 3 + face;
          expect(pose.theta[c]).toBeCloseTo(0, 6);
          expect(pose.rotorY[slot.index]).toBeCloseTo(layout.eccentricity, 6);
          expect(pose.share[c]).toBeCloseTo(0, 9);
          // La face est celle dont le milieu vise le haut.
          expect(((pose.rotorDeg[slot.index] - 120 * face) % 360 + 360) % 360).toBeCloseTo(0, 6);
        }
      }
    }
  });

  test('le calage du gaz recopie celui du son', () => {
    const source = readFileSync('../core/src/engine_sound.cpp', 'utf8');
    const value = (name: string) => Number(new RegExp(`${name} = \\((\\d+)\\.0f - kRotaryOrigin\\)`).exec(source)?.[1]);
    expect(value('kPortEvo')).toBe(SHAFT_EVO);
    expect(value('kPortEvc')).toBe(SHAFT_EVC);
    expect(value('kPortIvo')).toBe(SHAFT_IVO);
    expect(value('kPortIvc')).toBe(SHAFT_IVC);
    expect(Number(/kPortSweep = (\d+)\.0f/.exec(source)?.[1])).toBe(SHAFT_PORT_SWEEP);
    // La lumière s'ouvre au sommet d'avant, se ferme au sommet d'arrière, 120° de rotor plus loin : ce calage ne laisse qu'une largeur.
    expect(SHAFT_PORT_SWEEP).toBe(SHAFT_EVC - SHAFT_EVO - 360);
  });
});

const RUNNING: GasInputs = { rpm: 3000, load: 0.8, boost: 0, release: 0, burning: true, heat: 0.6, turnsTotal: 0 };

/** Fait avancer le réseau d'un nombre de cycles de deux tours, à l'horloge de `Motion`. */
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
  test('une chambre, à peine ouverte par sa lumière : une bouffée par explosion, deux par rotor en deux tours', () => {
    for (const rotors of ROTOR_COUNTS) {
      const net = new GasNetwork();
      net.build(make({ cylinders: rotors, displacementCc: 654 * rotors }), firingOrder(rotors));
      expect(net.cylinders).toBe(3 * rotors);
      expect(net.runners).toBe(rotors);
      const inputs = { ...RUNNING };
      drive(net, inputs, 2);
      const before = net.blowdowns;
      const burnt = net.burntBlowdowns;
      drive(net, inputs, 3);
      expect(net.blowdowns - before).toBeGreaterThanOrEqual(3 * 2 * rotors - 1);
      expect(net.blowdowns - before).toBeLessThanOrEqual(3 * 2 * rotors + 1);
      expect(net.burntBlowdowns - burnt).toBeGreaterThanOrEqual(3 * 2 * rotors - 2);
    }
  });

  test('le gaz suit la pose : même angle de chambre, même volume', () => {
    const config = make();
    const layout = rotaryLayout(config);
    const pose = new RotaryPose(layout);
    const net = new GasNetwork();
    net.build(config, firingOrder(2));
    const inputs = { ...RUNNING };
    drive(net, inputs, 1.37);
    pose.solve((inputs.turnsTotal ?? 0) * 720);
    for (let c = 0; c < net.cylinders; c++) {
      expect(Math.abs(net.cylTheta[c] / CYCLE_PER_SHAFT - pose.theta[c])).toBeLessThan(6);
      const clearance = 654e-6 / 8.4;
      const expected = clearance + 654e-6 * pose.share[c];
      expect(Math.abs(net.cylV[c] - expected) / expected).toBeLessThan(0.06);
    }
  });

  test('des lumières : ouvertes bien plus longtemps qu\'une soupape, à front raide, et se chevauchant sur un rotor', () => {
    const net = new GasNetwork();
    net.build(make(), firingOrder(2));
    const inputs = { ...RUNNING };
    let open = 0;
    let total = 0;
    let overlap = 0;
    drive(net, inputs, 6, () => {
      total++;
      if (net.cylLiftEx[0] > 0) open++;
      if (net.cylLiftEx[0] > 0 && net.cylLiftEx[1] + net.cylLiftEx[2] > 0) overlap++;
    });
    // 393° d'arbre sur un cycle de 1080° : 36 %.
    expect(open / total).toBeGreaterThan(0.3);
    expect(open / total).toBeLessThan(0.42);
    expect(overlap).toBeGreaterThan(0);
  });

  test('le réseau reste stable et n\'invente pas de masse', () => {
    const net = new GasNetwork();
    net.build(make({ aspiration: 'turbo' }), firingOrder(2));
    const inputs = { ...RUNNING, rpm: 6000, load: 1, boost: 0.8 };
    drive(net, inputs, 8);
    for (let c = 0; c < net.cylinders; c++) {
      expect(net.cylP[c]).toBeGreaterThan(5000);
      expect(net.cylT[c]).toBeGreaterThan(200);
      expect(net.cylT[c]).toBeLessThan(5000);
    }
    expect(net.totalMass()).toBeGreaterThan(0);
    expect(net.isDiesel).toBe(false);
  });

  test('un diesel demandé sur un rotatif reste à l\'essence', () => {
    const net = new GasNetwork();
    net.build(make({ fuel: 'diesel' }), firingOrder(2));
    expect(net.isDiesel).toBe(false);
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
  await page.getByTestId('preset').selectOption('twin_rotor_13');
  await ouvrir(page, 'engine');
}

const LINK = '2.rotary.1308.84.9000.sport.natural.petrol.0.light.dump_valve~coupe.1300.18.rear.1';

test.describe('sur le site', () => {
  test('le lien de partage fonctionne par le nom, et les anciens liens restent valides', async ({ page }) => {
    const errors = await open(page, `#m=${LINK}`);
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('summary')).toContainText('2 rotors');
    await expect(page.getByTestId('open-engine-summary')).toContainText('rotors');
    // Un lien écrit à la main vers ce qui n'existe pas revient à ce qui existe.
    await page.goto(`./?test#m=${LINK.replace('2.rotary', '6.rotary').replace('.petrol.', '.diesel.')}`);
    await page.reload();
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('summary')).toContainText('2 rotors');
    await expect(page.getByTestId('rotary-help')).toBeVisible();
    // Un ancien lien, d'avant le rotatif, donne toujours son moteur.
    await page.goto('./?test#m=4.boxer.2500.99.6800.sport.turbo.petrol.0.light.flutter~compact.1450.18.all.1.2');
    await page.reload();
    await ouvrir(page, 'engine');
    await expect(page.getByTestId('summary')).toContainText('4 cylindres');
    expect(errors).toEqual([]);
  });

  test('choisir le rotatif : ni alésage, ni carburant, de 2 à 4 rotors ; et retour aux cylindres', async ({ page }) => {
    await open(page, '#m=6.v.3000.84.7000.stock.natural.diesel.0.off.dump_valve~compact.1350.18.front.1');
    await ouvrir(page, 'engine');
    const panel = page.getByTestId('panel-engine');
    await expect(panel.getByText('Alésage (taille')).toBeVisible();
    await panel.getByRole('button', { name: 'Rotatif' }).click();
    await expect(page.getByTestId('summary')).toContainText('2 rotors');
    await expect(panel.getByText('Alésage (taille')).toHaveCount(0);
    await expect(panel.getByText('Carburant')).toHaveCount(0);
    const rotors = panel.locator('fieldset').filter({ hasText: 'Rotors' }).getByRole('button');
    await expect(rotors).toHaveText(['2', '3', '4']);
    await rotors.nth(1).click();
    await expect(page.getByTestId('summary')).toContainText('3 rotors');
    await expect(page.getByTestId('open-engine-summary')).toContainText('3 rotors');
    await panel.getByRole('button', { name: 'Boxer' }).click();
    await expect(panel.getByText('Alésage (taille')).toBeVisible();
    await expect(page.getByTestId('summary')).toContainText('3 cylindres');
  });

  test('la vue : un rotor qui tourne au tiers de l\'arbre, des étincelles, sans erreur, en 3D et en 2D', async ({ page }) => {
    mkdirSync(SONS, { recursive: true });
    const errors = await open(page, `#m=${LINK}`);
    await voir(page, 'engine');
    await page.getByTestId('start').click();
    await page.waitForFunction(() => (window as any).__revforge?.telemetry().rpm > 500, null, { timeout: 15000 });
    await page.waitForFunction(() => (document.querySelector('[data-testid=engine-view]') as any)?.__engine?.scene?.()?.update, null, { timeout: 15000 });
    const seen = await page.evaluate(() => new Promise<any>((resolve) => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const until = performance.now() + 3000;
      const out = { sparks: 0, flames: 0, frames: 0, firstShaft: -1, lastShaft: 0, firstRotor: 0, lastRotor: 0, gasLive: 0 };
      const loop = () => {
        const pose = d.rotary();
        if (out.firstShaft < 0) {
          out.firstShaft = pose.shaftDeg;
          out.firstRotor = pose.rotorDeg[0];
          out.sparks = pose.sparkCount;
        }
        out.lastShaft = pose.shaftDeg;
        out.lastRotor = pose.rotorDeg[0];
        out.flames += d.scene().drawnFlames ?? 0;
        if (pose.gasLive) out.gasLive++;
        out.frames++;
        if (performance.now() < until) requestAnimationFrame(loop);
        else {
          out.sparks = pose.sparkCount - out.sparks;
          resolve(out);
        }
      };
      loop();
    }));
    const turned = seen.lastShaft - seen.firstShaft;
    expect(turned).toBeGreaterThan(90);
    expect((seen.lastRotor - seen.firstRotor) / turned).toBeCloseTo(1 / 3, 6);
    // Une étincelle par rotor et par tour d'arbre, à une près aux bords.
    expect(Math.abs(seen.sparks - (2 * turned) / 360)).toBeLessThanOrEqual(2);
    expect(seen.gasLive).toBeGreaterThan(0);
    await page.waitForTimeout(600);
    await page.screenshot({ path: `${SONS}/vue3d_large.png` });
    await page.getByTestId('engine-names').click();
    await page.waitForTimeout(300);
    await page.getByTestId('engine-view').screenshot({ path: `${SONS}/vue3d_noms.png` });
    await page.getByTestId('engine-names').click();
    await page.getByTestId('engine-mode-2d').click();
    await page.waitForTimeout(600);
    await page.getByTestId('engine-view').screenshot({ path: `${SONS}/vue2d_large.png` });
    await page.getByTestId('engine-names').click();
    await page.waitForTimeout(300);
    await page.getByTestId('engine-view').screenshot({ path: `${SONS}/vue2d_noms.png` });
    await page.getByTestId('engine-names').click();
    await expect(page.locator('figcaption').filter({ hasText: 'Rotatif à 2 rotors' })).toBeVisible();
    await voir(page, 'gas');
    await page.waitForTimeout(1500);
    await page.screenshot({ path: `${SONS}/gaz_large.png` });
    await page.setViewportSize({ width: 360, height: 780 });
    await voir(page, 'engine');
    await page.waitForTimeout(600);
    await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
    await page.screenshot({ path: `${SONS}/vue2d_tel.png` });
    const scrollable = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth + 1);
    expect(scrollable).toBe(false);
    await page.getByTestId('engine-mode-3d').click();
    await page.waitForTimeout(800);
    await page.screenshot({ path: `${SONS}/vue3d_tel.png` });
    await ouvrir(page, 'engine');
    await page.waitForTimeout(200);
    await page.screenshot({ path: `${SONS}/volet_tel.png` });
    expect(errors).toEqual([]);
  });

  test('la fiche : le préréglage est dans la liste, et ses chiffres viennent du cycle d\'un tour', async ({ page }) => {
    const errors = await open(page);
    await choosePreset(page);
    await expect(page.getByTestId('summary')).toContainText('2 rotors');
    const spec = await page.evaluate(() => (window as any).__revforge?.spec?.() ?? null);
    if (spec) {
      expect(spec.peakHp).toBeGreaterThan(190);
      expect(spec.peakHp).toBeLessThan(290);
    }
    expect(errors).toEqual([]);
  });
});
