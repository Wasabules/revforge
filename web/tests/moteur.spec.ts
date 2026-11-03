import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { chainCircles, chainPath, DEG, engineLayout, EnginePose, engineState, firingAllowed, glowWanted, layoutKey, lobeNoseDeg, lobeRadius, pistonDistance, rockerAngle, sparkThetaDeg, starterTeeth, valveLift, type PoseClock, type PoseTelemetry } from '../src/lib/engineLayout';
import { gasKey } from '../src/lib/fluid';
import { EVC, EVO, GasNetwork, ignitionAdvance, IVC, IVO, type GasInputs } from '../src/lib/fluidGas';
import { firingOrder } from '../src/lib/motion';
import { sohcSectionShare, VALVE_BRIDGE_FOUR_MM, VALVE_BRIDGE_TWO_MM, VALVE_DISCHARGE, VALVE_EDGE_MM, VALVE_EXHAUST_REACH, VALVE_EXHAUST_SHARE, VALVE_INTAKE_REACH, VALVE_LIFT_PER_BORE, VALVE_THROAT_PER_HEAD, valveSizes } from '../src/lib/valves';

/* eslint-disable @typescript-eslint/no-explicit-any */

const BASE = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', placement: 'front',
  insulation: 'standard', boxFlywheel: 'single',
} as unknown as EngineConfig;
const make = (over: Partial<EngineConfig>): EngineConfig => ({ ...BASE, ...over });
const FAMILIES: Partial<EngineConfig>[] = [
  { cylinders: 2 }, { cylinders: 3 }, { cylinders: 4 }, { cylinders: 5 }, { cylinders: 6 },
  { cylinders: 2, layout: 'boxer' }, { cylinders: 4, layout: 'boxer' }, { cylinders: 6, layout: 'boxer' },
  { cylinders: 2, layout: 'v' }, { cylinders: 4, layout: 'v' }, { cylinders: 6, layout: 'v' }, { cylinders: 8, layout: 'v' }, { cylinders: 8, layout: 'cross_plane' },
  { cylinders: 10, layout: 'v' }, { cylinders: 12, layout: 'v' }, { cylinders: 3, layout: 'v' }, { cylinders: 5, layout: 'boxer' },
];

const wrap360 = (deg: number) => ((deg % 360) + 360) % 360;

// ── La mécanique, sans navigateur ──

test.describe('mécanique', () => {
  test('la position du piston suit la formule de la bielle et de la manivelle, et la bielle garde sa longueur', () => {
    for (const family of FAMILIES) {
      const layout = engineLayout(make({ ...family, displacementCc: 3000, boreMm: 86 }));
      const pose = new EnginePose(layout);
      for (let crankDeg = 0; crankDeg < 720; crankDeg += 13) {
        pose.solve(crankDeg);
        for (const cylinder of layout.cylinders) {
          const i = cylinder.index;
          // La formule écrite ici, en toutes lettres, pas celle du code.
          const a = (cylinder.pinDeg + crankDeg - cylinder.betaDeg) * DEG;
          const r = layout.crank;
          const l = layout.rod;
          const expected = r * Math.cos(a) + Math.sqrt(l * l - r * r * Math.sin(a) * Math.sin(a));
          expect(pose.piston[i]).toBeCloseTo(expected, 8);
          // L'axe de piston reste sur l'axe du cylindre.
          const b = cylinder.betaDeg * DEG;
          expect(pose.wristX[i] * Math.cos(b) - pose.wristY[i] * Math.sin(b)).toBeCloseTo(0, 8);
          // Du maneton à l'axe : la longueur de la bielle, à toute position. Son angle dessiné y mène bien.
          const dx = pose.wristX[i] - pose.pinX[i];
          const dy = pose.wristY[i] - pose.pinY[i];
          expect(Math.hypot(dx, dy)).toBeCloseTo(l, 8);
          expect(pose.pinX[i] + l * Math.sin(pose.rodAngle[i])).toBeCloseTo(pose.wristX[i], 6);
          expect(pose.pinY[i] + l * Math.cos(pose.rodAngle[i])).toBeCloseTo(pose.wristY[i], 6);
        }
      }
    }
  });

  test('la course dessinée est celle des réglages, et le rapport de bielle celui du gaz', () => {
    const layout = engineLayout(make({ displacementCc: 2000, boreMm: 84 }));
    // Volume d'un cylindre : π/4 × alésage² × course.
    const stroke = (500 * 1000) / ((Math.PI / 4) * 84 * 84);
    expect(layout.stroke).toBeCloseTo(stroke, 6);
    expect(layout.rod / layout.stroke).toBeCloseTo(1.6, 8);
    const pose = new EnginePose(layout);
    let top = -Infinity;
    let bottom = Infinity;
    for (let deg = 0; deg < 720; deg++) {
      pose.solve(deg);
      top = Math.max(top, pose.piston[0]);
      bottom = Math.min(bottom, pose.piston[0]);
    }
    expect(top - bottom).toBeCloseTo(stroke, 2);
    // Alésage et cylindrée changent vraiment le dessin.
    expect(engineLayout(make({ displacementCc: 2000, boreMm: 90 })).stroke).toBeLessThan(layout.stroke);
    expect(engineLayout(make({ displacementCc: 3000, boreMm: 84 })).stroke).toBeGreaterThan(layout.stroke);
    // Des réglages impossibles à dessiner sont bornés, et la mécanique le sait.
    const extreme = engineLayout(make({ cylinders: 2, displacementCc: 7000, boreMm: 60 }));
    expect(extreme.strokeClamped).toBe(true);
    expect(extreme.stroke).toBeLessThanOrEqual(60 * 1.8);
  });

  test('les manetons sont à l\'angle qu\'exige chaque architecture', () => {
    const angles = (over: Partial<EngineConfig>) => engineLayout(make(over)).throwAngles;
    expect(angles({ cylinders: 2 })).toEqual([0, 0]);
    expect(angles({ cylinders: 3 })).toEqual([0, 240, 120]);
    expect(angles({ cylinders: 4 })).toEqual([0, 180, 180, 0]);
    expect(angles({ cylinders: 5 })).toEqual([0, 216, 144, 72, 288]);
    expect(angles({ cylinders: 6 })).toEqual([0, 240, 120, 120, 240, 0]);
    // V8 : à plat comme deux quatre-en-ligne, croisé avec ses manetons à 90°.
    expect(angles({ cylinders: 8, layout: 'v' })).toEqual([0, 180, 180, 0]);
    expect(angles({ cylinders: 8, layout: 'cross_plane' })).toEqual([0, 90, 270, 180]);
    // V12 à 60° : le vilebrequin d'un six en ligne.
    expect(angles({ cylinders: 12, layout: 'v' })).toEqual([0, 240, 120, 120, 240, 0]);
    // Manetons décalés : 18° sur le V10 à 90°, 60° sur le V6 à 60°.
    expect(engineLayout(make({ cylinders: 10, layout: 'v' })).splitDeg).toBe(18);
    expect(engineLayout(make({ cylinders: 10, layout: 'v' })).halfDeg).toBe(45);
    expect(engineLayout(make({ cylinders: 6, layout: 'v' })).splitDeg).toBe(60);
    expect(engineLayout(make({ cylinders: 6, layout: 'v' })).halfDeg).toBe(30);
    expect(engineLayout(make({ cylinders: 12, layout: 'v' })).splitDeg).toBe(0);
    expect(engineLayout(make({ cylinders: 8, layout: 'cross_plane' })).splitDeg).toBe(0);
  });

  test('sur un boxer, deux pistons opposés s\'écartent ensemble, et à plat', () => {
    for (const cylinders of [2, 4, 6]) {
      const layout = engineLayout(make({ cylinders, layout: 'boxer' }));
      expect(layout.halfDeg).toBe(90);
      const pose = new EnginePose(layout);
      for (let row = 0; row < layout.rows; row++) {
        const a = layout.byBank[0][row];
        const b = layout.byBank[1][row];
        expect(a.betaDeg).toBe(-90);
        expect(b.betaDeg).toBe(90);
        // Manetons opposés : les deux pistons sont au même point de leur course, en sens contraire.
        expect(wrap360(a.pinDeg - b.pinDeg)).toBeCloseTo(180, 6);
        pose.solve(77);
        expect(pose.piston[a.index]).toBeCloseTo(pose.piston[b.index], 6);
        expect(pose.wristX[a.index]).toBeCloseTo(-pose.wristX[b.index], 6);
      }
    }
  });

  test('sur un V à manetons communs, les deux bielles d\'une manivelle sont sur le même maneton', () => {
    for (const over of [{ cylinders: 8, layout: 'cross_plane' }, { cylinders: 8, layout: 'v' }, { cylinders: 12, layout: 'v' }] as Partial<EngineConfig>[]) {
      const layout = engineLayout(make(over));
      const pose = new EnginePose(layout);
      pose.solve(123);
      expect(layout.throws.length).toBe(layout.count / 2);
      for (let row = 0; row < layout.rows; row++) {
        const a = layout.byBank[0][row];
        const b = layout.byBank[1][row];
        expect(a.pinDeg).toBeCloseTo(b.pinDeg, 6);
        expect(pose.pinX[a.index]).toBeCloseTo(pose.pinX[b.index], 6);
      }
    }
  });

  test('chaque cylindre passe au point mort haut à son rang dans l\'ordre d\'allumage', () => {
    for (const family of FAMILIES) {
      const layout = engineLayout(make(family));
      const order = firingOrder(layout.count);
      expect(layout.order).toEqual(order);
      const pose = new EnginePose(layout);
      order.forEach((number, slot) => {
        const firing = (slot * 720) / layout.count;
        const i = number - 1;
        pose.solve(firing);
        expect(pose.theta[i]).toBeCloseTo(0, 6);
        // Le piston y est au plus haut : le maneton est sur l'axe du cylindre.
        expect(pose.piston[i]).toBeCloseTo(layout.crank + layout.rod, 6);
      });
    }
  });

  test('l\'allumage précède le point mort haut, puis viennent l\'échappement, le croisement et l\'admission', () => {
    for (const rpm of [800, 3000, 7000]) {
      for (const diesel of [false, true]) {
        const advance = ignitionAdvance(diesel, rpm);
        expect(advance).toBeGreaterThan(0);
        expect(advance).toBeLessThan(90);
        expect(sparkThetaDeg(diesel, rpm)).toBeCloseTo(720 - advance, 8);
      }
    }
    expect(0).toBeLessThan(EVO);
    expect(EVO).toBeLessThan(180);
    expect(IVO).toBeLessThan(360);
    expect(EVC).toBeGreaterThan(360);
    expect(IVO).toBeLessThan(EVC);
    expect(IVC).toBeGreaterThan(540);
    expect(IVC).toBeLessThan(720);
    // Le son a ses propres repères, dans `engine_sound.cpp` : le dessin doit rester dans leur voisinage.
    const sound = readFileSync(new URL('../../core/src/engine_sound.cpp', import.meta.url), 'utf-8');
    const constant = (name: string) => Number(new RegExp(`constexpr float ${name} = ([\\d.]+)f / 720`).exec(sound)![1]);
    expect(Math.abs(constant('kEvo') - EVO)).toBeLessThanOrEqual(10);
    expect(Math.abs(constant('kEvc') - EVC)).toBeLessThanOrEqual(10);
    expect(Math.abs(constant('kIvo') - IVO)).toBeLessThanOrEqual(10);
    expect(Math.abs(constant('kIvc') - IVC)).toBeLessThanOrEqual(10);
  });

  test('l\'arbre à cames tourne à demi-vitesse, et son bossage appuie exactement de la levée de la soupape', () => {
    for (const camshaft of ['dohc', 'sohc'] as const) {
      for (const family of FAMILIES) {
        const layout = engineLayout(make({ ...family, camshaft }));
        const pose = new EnginePose(layout);
        const { valve } = layout;
        pose.solve(0);
        const before = pose.camDeg;
        pose.solve(100);
        expect(pose.camDeg - before).toBeCloseTo(50, 8);
        for (let crankDeg = 0; crankDeg < 720; crankDeg += 17) {
          pose.solve(crankDeg);
          for (const cylinder of layout.cylinders) {
            for (const exhaust of [false, true]) {
              const side = exhaust ? -cylinder.intakeSide : cylinder.intakeSide;
              const open = exhaust ? EVO : IVO;
              const close = exhaust ? EVC : IVC;
              const nose = lobeNoseDeg(cylinder, valve.followerDeg, side, open, close);
              // Le rayon de la came, dans la direction de ce qui appuie dessus, après `camDeg` de rotation.
              const towardsFollower = cylinder.betaDeg + side * valve.followerDeg + 180 - pose.camDeg;
              const radius = lobeRadius(valve.camBase, valve.camLift, towardsFollower - nose, open, close);
              const lift = exhaust ? pose.liftEx[cylinder.index] : pose.liftIn[cylinder.index];
              expect(radius).toBeCloseTo(valve.camBase + valve.camLift * lift, 6);
            }
          }
        }
      }
    }
  });

  test('un simple arbre : un arbre et une roue par rangée, deux soupapes par cylindre, et la chaîne qui passe par elle', () => {
    for (const family of FAMILIES) {
      const twin = engineLayout(make({ ...family, camshaft: 'dohc' }));
      const single = engineLayout(make({ ...family, camshaft: 'sohc' }));
      expect(twin.valve.sohc).toBe(false);
      expect(twin.valve.perCylinder).toBe(4);
      expect(single.valve.sohc).toBe(true);
      expect(single.valve.perCylinder).toBe(2);
      expect(single.valve.slots.filter((slot) => slot.exhaust).length).toBe(1);
      expect(single.valve.camX).toBe(0);
      // Choisir la distribution change les pièces : la vue doit se reconstruire.
      expect(layoutKey(make({ ...family, camshaft: 'sohc' }))).not.toBe(layoutKey(make({ ...family, camshaft: 'dohc' })));
      for (let bank = 0; bank < single.banks; bank++) {
        const circles = chainCircles(single, bank);
        // Le vilebrequin et une seule came, sur l'axe de la rangée ; deux cames sur le double arbre.
        expect(circles.length).toBe(2);
        expect(chainCircles(twin, bank).length).toBe(3);
        const beta = single.banks === 1 ? 0 : bank === 0 ? -single.halfDeg : single.halfDeg;
        expect(Math.hypot(circles[1][0], circles[1][1])).toBeCloseTo(single.valve.camY, 6);
        expect(Math.atan2(circles[1][0], circles[1][1]) / DEG).toBeCloseTo(beta, 6);
        const path = chainPath(circles, (single.sprocket.crankRadius * 2 * Math.PI) / single.sprocket.crankTeeth);
        expect(path.links % 2).toBe(0);
        // La chaîne fait moins de chemin qu'autour de deux cames.
        const longer = chainPath(chainCircles(twin, bank), (twin.sprocket.crankRadius * 2 * Math.PI) / twin.sprocket.crankTeeth);
        expect(path.length).toBeLessThan(longer.length);
      }
      // La culasse d'un simple arbre est plus basse : il n'y a plus de came au-dessus de chaque soupape.
      expect(single.valve.coverTop).toBeLessThan(twin.valve.coverTop);
    }
  });

  test('un culbuteur reste posé sur sa queue de soupape et sur sa came, à tout angle, en ligne, en V et à plat', () => {
    for (const family of [{ cylinders: 4 }, { cylinders: 6, layout: 'v' }, { cylinders: 8, layout: 'cross_plane' }, { cylinders: 4, layout: 'boxer' }, { cylinders: 6, layout: 'boxer' }] as Partial<EngineConfig>[]) {
      for (const boreMm of [70, 84, 100]) {
        const layout = engineLayout(make({ ...family, camshaft: 'sohc', boreMm, displacementCc: 2000 * (family.cylinders ?? 4) / 4 }));
        const pose = new EnginePose(layout);
        const { valve } = layout;
        const rocker = valve.rocker!;
        expect(rocker.ratio).toBeGreaterThan(1.2);
        expect(rocker.ratio).toBeLessThan(1.7);
        expect(valve.camLift * rocker.ratio).toBeCloseTo(valve.lift, 6);
        const tilt = valve.tiltDeg * DEG;
        let swing = 0;
        for (let crankDeg = 0; crankDeg < 720; crankDeg += 7) {
          pose.solve(crankDeg);
          for (const cylinder of layout.cylinders) {
            for (const exhaust of [false, true]) {
              const lift = (exhaust ? pose.liftEx[cylinder.index] : pose.liftIn[cylinder.index]) * valve.lift;
              const turned = rockerAngle(valve, lift);
              expect(exhaust ? pose.rockerEx[cylinder.index] : pose.rockerIn[cylinder.index]).toBeCloseTo(turned, 9);
              swing = Math.max(swing, turned);
              // Le bout du culbuteur, vu du côté +1, par rapport à la queue qui coulisse sur son axe.
              const tip = (rocker.tipDeg * DEG) - turned;
              const tipX = rocker.pivotX + rocker.tipArm * Math.cos(tip);
              const tipY = rocker.pivotY + rocker.tipArm * Math.sin(tip);
              const seatX = valve.x;
              const seatY = layout.deck + valve.seatRise;
              const along = (tipX - seatX) * Math.sin(tilt) + (tipY - seatY) * Math.cos(tilt);
              const across = (tipX - seatX) * Math.cos(tilt) - (tipY - seatY) * Math.sin(tilt);
              expect(along).toBeCloseTo(valve.stem - lift, 6);
              // Il glisse à peine sur le bout de la queue.
              expect(Math.abs(across)).toBeLessThan(boreMm * 0.03);
              // Le patin : à moins de 2 % de l'alésage de la surface de la came, à cette levée.
              const pad = rocker.padDeg * DEG - turned;
              const padX = rocker.pivotX + rocker.padArm * Math.cos(pad);
              const padY = rocker.pivotY + rocker.padArm * Math.sin(pad);
              const wanted = valve.camBase + (valve.camLift * lift) / valve.lift;
              expect(Math.abs(Math.hypot(padX - valve.camX, padY - valve.camY) - wanted)).toBeLessThan(boreMm * 0.02);
            }
          }
        }
        // Une vingtaine de degrés à pleine levée : il bascule, et dans le bon sens.
        expect(swing).toBeGreaterThan(15 * DEG);
        expect(swing).toBeLessThan(25 * DEG);
      }
    }
  });

  test('les soupapes d\'un simple arbre sont plus grandes, et moins de section au total : le même calcul que le C++', () => {
    const source = readFileSync(new URL('../../core/src/valvetrain.cpp', import.meta.url), 'utf-8');
    const constant = (name: string) => Number(new RegExp(`constexpr double ${name} = ([\\d.]+);`).exec(source)![1]);
    expect(VALVE_EDGE_MM).toBe(constant('kEdgeMm'));
    expect(VALVE_BRIDGE_TWO_MM).toBe(constant('kBridgeTwoValvesMm'));
    expect(VALVE_BRIDGE_FOUR_MM).toBe(constant('kBridgeFourValvesMm'));
    expect(VALVE_EXHAUST_SHARE).toBe(constant('kExhaustShare'));
    expect(VALVE_INTAKE_REACH).toBe(constant('kIntakeReach'));
    expect(VALVE_EXHAUST_REACH).toBe(constant('kExhaustReach'));
    expect(VALVE_LIFT_PER_BORE).toBe(constant('kLiftPerBore'));
    expect(VALVE_THROAT_PER_HEAD).toBe(constant('kThroatPerHead'));
    expect(VALVE_DISCHARGE).toBe(constant('kDischarge'));
    for (const bore of [60, 75, 86, 100, 110]) {
      const two = valveSizes('sohc', bore);
      const four = valveSizes('dohc', bore);
      expect(two.intakeMm).toBeGreaterThan(four.intakeMm);
      expect(two.intakeAreaMm2).toBeLessThan(four.intakeAreaMm2);
      expect(two.exhaustAreaMm2).toBeLessThan(four.exhaustAreaMm2);
      const share = sohcSectionShare(bore);
      expect(share.intake).toBeGreaterThan(0.6);
      expect(share.intake).toBeLessThan(0.8);
    }
    // 86 mm d'alésage : les soupapes de vrais moteurs (C20NE, 41,8/36,5 ; C20XE, 32/29, supposés de mémoire) à 10 % près.
    expect(valveSizes('sohc', 86).intakeMm).toBeCloseTo(41.5, 0);
    expect(valveSizes('dohc', 86).intakeMm).toBeCloseTo(32.9, 0);
    // Elles sont dessinées à cette taille.
    expect(engineLayout(make({ camshaft: 'sohc', boreMm: 86 })).valve.radiusIn).toBeCloseTo(valveSizes('sohc', 86).intakeMm / 2, 6);
  });

  test('moins de section, moins de gaz en haut : le gaz à l\'écran sent les deux soupapes, sans toucher au double arbre', () => {
    const run = (camshaft: 'dohc' | 'sohc', rpm: number) => {
      const config = make({ camshaft, displacementCc: 2000, boreMm: 84 });
      const net = new GasNetwork();
      net.build(config, firingOrder(4));
      const inputs: GasInputs = { rpm, load: 1, boost: 0, release: 0, burning: true, heat: 0.5 };
      const seconds = 120 / rpm / 240;
      const turns = (rpm / 120) * seconds;
      let cycle = 0;
      let mass = 0;
      let samples = 0;
      let pushed = 0;
      let pushes = 0;
      for (let step = 0; step < 240 * 14; step++) {
        net.advance(seconds, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
        if (step >= 240 * 8) {
          // Le gaz enfermé au moment où l'admission se ferme : ce que le cylindre a réussi à avaler.
          if (net.cylLiftIn[0] < 1e-6 && net.cylTheta[0] > IVC && net.cylTheta[0] < IVC + 20) {
            mass += net.cylM[0];
            samples++;
          }
          // Et ce contre quoi le piston pousse en remontant : la pression moyenne pendant l'échappement.
          if (net.cylTheta[0] > 240 && net.cylTheta[0] < 340) {
            pushed += net.cylP[0];
            pushes++;
          }
        }
      }
      return { mass: mass / Math.max(samples, 1), pressure: pushed / Math.max(pushes, 1) };
    };
    const twoHigh = run('sohc', 7000);
    const fourHigh = run('dohc', 7000);
    expect(twoHigh.mass).toBeLessThan(fourHigh.mass * 0.98);
    expect(twoHigh.pressure).toBeGreaterThan(fourHigh.pressure);
    // À bas régime, le gaz a le temps de passer : les deux se remplissent presque autant.
    const twoLow = run('sohc', 1500);
    const fourLow = run('dohc', 1500);
    expect(Math.abs(twoLow.mass / fourLow.mass - 1)).toBeLessThan(Math.abs(twoHigh.mass / fourHigh.mass - 1));
    // Le choix figure dans la clé du réseau : sans cela, rien ne se reconstruit.
    expect(gasKey(make({ camshaft: 'sohc' }))).not.toBe(gasKey(make({ camshaft: 'dohc' })));
  });

  test('la levée dessinée est celle du gaz, et le gaz allume dans l\'ordre dessiné', () => {
    for (const over of [{ cylinders: 4 }, { cylinders: 8, layout: 'cross_plane' }, { cylinders: 12, layout: 'v' }] as Partial<EngineConfig>[]) {
      const config = make({ ...over, displacementCc: 3000 });
      const layout = engineLayout(config);
      const net = new GasNetwork();
      net.build(config, firingOrder(config.cylinders));
      const inputs: GasInputs = { rpm: 3000, load: 0.5, boost: 0, release: 0, burning: true, heat: 0.3 };
      const pose = new EnginePose(layout);
      const seen: number[] = [];
      const seconds = 2 / (3000 / 60) / 400;
      const turns = (3000 / 120) * seconds;
      // Au premier pas, tous les cylindres déjà ouverts se déclarent ensemble : on regarde après.
      net.advance(seconds, 0, turns, inputs);
      let previous = net.blowdowns;
      let cycle = turns;
      let worstGap = 0;
      for (let step = 0; step < 1000; step++) {
        net.advance(seconds, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
        if (net.blowdowns !== previous) {
          previous = net.blowdowns;
          seen.push(net.lastBlowdownCylinder + 1);
        }
        // Le dernier angle du gaz dans ce pas, et ce que la pose en déduit. Un pas de gaz vaut 1,8° : à ce pas près, et au repli des 720° près.
        pose.solve((cycle - turns + 1) * 720);
        for (const cylinder of layout.cylinders) {
          worstGap = Math.max(worstGap, Math.abs(((pose.theta[cylinder.index] - net.cylTheta[cylinder.index] + 360) % 720 + 720) % 720 - 360));
        }
      }
      expect(worstGap).toBeLessThan(2.5);
      // Les ouvertures d'échappement, à partir d'un point quelconque du cycle, suivent l'ordre d'allumage.
      const order = layout.order;
      expect(seen.length).toBeGreaterThan(order.length);
      const start = order.indexOf(seen[0]);
      seen.forEach((number, k) => expect(number).toBe(order[(start + k) % order.length]));
      for (const cylinder of layout.cylinders) {
        const theta = net.cylTheta[cylinder.index];
        expect(valveLift(theta, EVO, EVC)).toBeCloseTo(net.cylLiftEx[cylinder.index], 1);
        expect(valveLift(theta, IVO, IVC)).toBeCloseTo(net.cylLiftIn[cylinder.index], 1);
      }
    }
  });

  const clock = (over: Partial<PoseClock> = {}): PoseClock => ({ cycle: 0, turns: 0, turbineAngle: 0, rpm: 3000, load: 0.5, heat: 0.5, reduced: false, burning: true, ...over });
  const telemetry = (over: Partial<PoseTelemetry> = {}): PoseTelemetry => ({ cut: 0, cranking: 0, stalled: 0, rpm: 3000, ...over });

  /** Fait tourner la pose sur `cycles` cycles et note l'ordre des étincelles. */
  function run(layout: ReturnType<typeof engineLayout>, c: Partial<PoseClock>, t: Partial<PoseTelemetry>, cycles = 2, step = 0.004) {
    const pose = new EnginePose(layout);
    const sparks: number[] = [];
    let count = 0;
    for (let k = 0; k < cycles / step; k++) {
      const cycle = (k * step) % 1;
      pose.update(clock({ ...c, cycle, turns: k * step }), telemetry(t), null, false, 0.016);
      if (pose.sparkCount !== count) {
        count = pose.sparkCount;
        sparks.push(pose.lastSparkCylinder + 1);
      }
    }
    return { pose, sparks };
  }

  test('les étincelles suivent l\'ordre d\'allumage', () => {
    for (const family of FAMILIES) {
      const layout = engineLayout(make(family));
      const { sparks } = run(layout, {}, {});
      expect(sparks.length).toBe(2 * layout.count);
      const start = layout.order.indexOf(sparks[0]);
      sparks.forEach((number, k) => expect(number).toBe(layout.order[(start + k) % layout.count]));
    }
  });

  test('une image lente ne fait pas manquer d\'étincelle', () => {
    // À 6000 tr/min, une image à 60 Hz parcourt une cinquantaine de degrés au ralenti.
    const layout = engineLayout(make({ cylinders: 12, layout: 'v' }));
    const { sparks } = run(layout, {}, {}, 2, 48 / 720);
    // 29 intervalles de 48°, 1392° en tout, une étincelle tous les 60° : 23, et dans l'ordre.
    expect(sparks.length).toBe(23);
    const start = layout.order.indexOf(sparks[0]);
    sparks.forEach((number, k) => expect(number).toBe(layout.order[(start + k) % layout.count]));
  });

  test('pas d\'étincelle pendant une coupure, moteur arrêté ni animation réduite', () => {
    const layout = engineLayout(make({ cylinders: 6 }));
    expect(run(layout, {}, { cut: 1 }).pose.sparkCount).toBe(0);
    expect(run(layout, { burning: false }, {}).pose.sparkCount).toBe(0);
    expect(run(layout, { rpm: 0 }, { rpm: 0 }).pose.sparkCount).toBe(0);
    expect(run(layout, { reduced: true }, {}).pose.sparkCount).toBe(0);
    expect(run(layout, { burning: false, rpm: 250 }, { cranking: 1, rpm: 250 }).pose.sparkCount).toBeGreaterThan(0);
    expect(firingAllowed(clock(), telemetry({ cut: 1 }))).toBe(false);
    // La coupure éteint aussi ce qui brillait encore.
    const pose = run(layout, {}, {}).pose;
    pose.spark.fill(1);
    pose.update(clock(), telemetry({ cut: 1 }), null, false, 0.016);
    expect(Math.max(...pose.spark)).toBe(0);
  });

  test('pas de flamme sans combustion : le gaz doit avoir allumé', () => {
    const layout = engineLayout(make({}));
    const pose = new EnginePose(layout);
    const gas = { cylinders: 4, cylT: [900, 900, 900, 900], cylM: [1, 1, 1, 1], cylY: [0, 0, 0, 0], cylBurn: [0.5, 0.5, 0.5, 0.5], cylFired: [1, 1, 1, 1], cylExhaust: [0, 0, 0, 0], cylIntake: [0, 0, 0, 0] };
    pose.update(clock(), telemetry(), gas, true, 0.016);
    expect(Math.max(...pose.flame)).toBeCloseTo(0.5, 6);
    pose.update(clock(), telemetry({ cut: 1 }), gas, true, 0.016);
    expect(Math.max(...pose.flame)).toBe(0);
    pose.update(clock(), telemetry(), { ...gas, cylFired: [0, 0, 0, 0] }, true, 0.016);
    expect(Math.max(...pose.flame)).toBe(0);
    pose.update(clock(), telemetry(), gas, false, 0.016);
    expect(Math.max(...pose.flame)).toBe(0);
  });

  test('un diesel n\'a pas d\'étincelle mais injecte dans l\'ordre, et chauffe ses bougies à froid', () => {
    const layout = engineLayout(make({ fuel: 'diesel' }));
    const { pose, sparks } = run(layout, {}, {});
    expect(sparks).toEqual([]);
    expect(pose.sparkCount).toBe(0);
    expect(pose.sprayCount).toBe(2 * layout.count);
    // Bougie de préchauffage : seulement sur un diesel, au démarreur ou à froid.
    expect(glowWanted(true, true, 250, 0, 0)).toBe(true);
    expect(glowWanted(true, false, 800, 0.1, 5)).toBe(true);
    expect(glowWanted(true, false, 800, 0.1, 40)).toBe(false);
    expect(glowWanted(true, false, 3000, 0.6, 5)).toBe(false);
    expect(glowWanted(true, false, 0, 0, 0)).toBe(false);
    expect(glowWanted(false, true, 250, 0, 0)).toBe(false);
    const petrol = new EnginePose(engineLayout(make({})));
    const diesel = new EnginePose(layout);
    for (let k = 0; k < 200; k++) {
      petrol.update(clock({ rpm: 250, burning: false }), telemetry({ cranking: 1, rpm: 250 }), null, false, 0.016);
      diesel.update(clock({ rpm: 250, burning: false }), telemetry({ cranking: 1, rpm: 250 }), null, false, 0.016);
    }
    expect(petrol.glow).toBe(0);
    expect(diesel.glow).toBeGreaterThan(0.9);
  });

  test('l\'état du moteur : arrêt, démarreur, coupure, calage', () => {
    expect(engineState(telemetry({ rpm: 0 }))).toBe('stopped');
    expect(engineState(telemetry({ cranking: 1, rpm: 200 }))).toBe('cranking');
    expect(engineState(telemetry({ cut: 1 }))).toBe('cut');
    expect(engineState(telemetry({ stalled: 1, rpm: 0 }))).toBe('stalled');
    expect(engineState(telemetry())).toBe('running');
  });

  test('le pignon du démarreur engrène la couronne, avec le bon rapport et en sens inverse', () => {
    const layout = engineLayout(make({ displacementCc: 3000 }));
    const { ring, pinion } = layout;
    expect(ring.teeth).toBe(starterTeeth(4, 3000).ring);
    // Le C++ : 105 + 9 par litre, borné, et un pignon de 9 ou 10 dents.
    expect(starterTeeth(4, 2000)).toEqual({ ring: 123, pinion: 10 });
    expect(starterTeeth(8, 5000)).toEqual({ ring: 150, pinion: 9 });
    expect(Math.hypot(pinion.x, pinion.y)).toBeCloseTo(ring.radius + pinion.radius, 6);
    const pose = new EnginePose(layout);
    pose.solve(0);
    const first = pose.pinionAngle;
    pose.solve(10);
    // Les deux tournent en sens inverse : le vilebrequin va dans le sens horaire, donc le pignon dans l'autre.
    expect(pose.pinionAngle - first).toBeCloseTo((10 * DEG * ring.teeth) / pinion.teeth, 8);
    // À chaque angle, une dent de couronne au contact tombe dans un creux du pignon.
    for (let deg = 0; deg < 90; deg += 7) {
      pose.solve(deg);
      const ringTeeth = ((pinion.angle + deg * DEG) * ring.teeth) / (Math.PI * 2);
      const pinionTeeth = ((pinion.angle + Math.PI - pose.pinionAngle) * pinion.teeth) / (Math.PI * 2);
      const sum = ringTeeth + pinionTeeth;
      expect(Math.abs(sum - Math.round(sum - 0.5) - 0.5)).toBeLessThan(1e-6);
    }
  });

  test('la chaîne a la longueur d\'une courroie autour de deux roues, et tourne dans le sens du vilebrequin', () => {
    const R = 60;
    const r = 25;
    const d = 300;
    const path = chainPath([[0, 0, r], [0, d, R]], 8);
    // La formule de la courroie ouverte : les deux brins tangents, et ce que chaque roue enveloppe.
    const skew = Math.asin((R - r) / d);
    const length = 2 * Math.sqrt(d * d - (R - r) * (R - r)) + Math.PI * (R + r) + 2 * (R - r) * skew;
    expect(path.length).toBeCloseTo(length, 0);
    // Le vilebrequin tourne dans le sens horaire : à droite, la chaîne descend.
    let seen = 0;
    for (let k = 0; k < path.x.length; k++) {
      if (path.x[k] > r * 0.5 && Math.abs(path.y[k]) < 2) {
        expect(Math.sin(path.angle[k])).toBeLessThan(0);
        seen++;
      }
    }
    expect(seen).toBeGreaterThan(0);
    // Trois pignons égaux : le tour du triangle, plus un cercle entier.
    const three = chainPath([[0, 0, 10], [100, 0, 10], [0, 100, 10]], 8);
    expect(three.length).toBeCloseTo(200 + Math.SQRT2 * 100 + 2 * Math.PI * 10, 0);
  });

  test('la chaîne fait le tour de ses pignons, et les dents des pignons donnent la demi-vitesse', () => {
    for (const family of FAMILIES) {
      const layout = engineLayout(make(family));
      expect(layout.sprocket.camTeeth).toBe(layout.sprocket.crankTeeth * 2);
      expect(layout.sprocket.camRadius).toBeCloseTo(layout.sprocket.crankRadius * 2, 8);
      for (let bank = 0; bank < layout.banks; bank++) {
        const circles = chainCircles(layout, bank);
        const path = chainPath(circles, (layout.sprocket.crankRadius * 2 * Math.PI) / layout.sprocket.crankTeeth);
        expect(path.links % 2).toBe(0);
        // Chaque point est sur la bande : posé sur un pignon, ou entre deux, jamais à l'intérieur d'une roue.
        let touching = 0;
        let deepest = Infinity;
        for (let k = 0; k < path.x.length; k++) {
          for (const c of circles) {
            const d = Math.hypot(path.x[k] - c[0], path.y[k] - c[1]) - c[2];
            if (d < deepest) deepest = d;
            if (Math.abs(d) < 0.5) touching++;
          }
        }
        expect(deepest).toBeGreaterThan(-0.5);
        expect(touching).toBeGreaterThan(path.x.length / 8);
        // Elle fait le tour dans le sens horaire, comme le vilebrequin : elle descend tout à droite, remonte tout à gauche.
        let right = 0;
        let left = 0;
        for (let k = 0; k < path.x.length; k++) {
          if (path.x[k] > path.x[right]) right = k;
          if (path.x[k] < path.x[left]) left = k;
        }
        expect(Math.sin(path.angle[right])).toBeLessThan(0);
        expect(Math.sin(path.angle[left])).toBeGreaterThan(0);
      }
    }
  });
});

// ── Le navigateur ──

const engineHandle = () => (document.querySelector('[data-testid=engine-view]') as any).__engine;

async function open(page: Page, hash = '') {
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

const carried = (page: Page, testId: string) => page.getByTestId(testId).getAttribute('aria-pressed');

test.describe('la vue', () => {
  test('la 3D par défaut : elle dessine des pièces, sans erreur', async ({ page }) => {
    const errors = await open(page);
    expect(await carried(page, 'engine-mode-3d')).toBe('true');
    await ready3d(page);
    const lit = await page.evaluate(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const stage = d.stage();
      stage.render();
      const gl = stage.renderer.getContext();
      const width = gl.drawingBufferWidth;
      const height = gl.drawingBufferHeight;
      const pixels = new Uint8Array(width * height * 4);
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      let count = 0;
      for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 0) count++;
      return count / (width * height);
    });
    expect(lit).toBeGreaterThan(0.15);
    expect(errors).toEqual([]);
  });

  test('sans WebGL2, la vue reste en 2D et le dit', async ({ page }) => {
    await page.addInitScript(() => {
      const original = HTMLCanvasElement.prototype.getContext;
      (HTMLCanvasElement.prototype as any).getContext = function (this: HTMLCanvasElement, type: string, ...rest: unknown[]) {
        return type === 'webgl2' ? null : (original as any).call(this, type, ...rest);
      };
    });
    const errors = await open(page);
    await expect(page.getByTestId('engine-unavailable')).toBeVisible();
    expect(await carried(page, 'engine-mode-2d')).toBe('true');
    await expect(page.getByTestId('engine-mode-3d')).toBeDisabled();
    await expect.poll(() => page.getByTestId('engine-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
      const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
      let painted = 0;
      for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
      return painted;
    })).toBeGreaterThan(5000);
    expect(errors).toEqual([]);
  });

  test('three.js ne se charge qu\'à la demande : injoignable, la page marche et le moteur reste en 2D', async ({ page }) => {
    await page.route(/three[^/]*\.js/, (route) => route.abort());
    await open(page);
    await expect(page.getByTestId('engine-unavailable')).toBeVisible({ timeout: 15_000 });
    // Il n'est pas dans le paquet principal : celui-ci serait deux fois plus lourd.
    const main = await page.evaluate(() => performance.getEntriesByType('resource').map((entry) => entry as PerformanceResourceTiming).find((entry) => /index-[^/]*\.js/.test(entry.name))?.decodedBodySize ?? 0);
    expect(main).toBeLessThan(800_000);
    expect(await carried(page, 'engine-mode-2d')).toBe('true');
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
  });

  test('passer de 3D en 2D et retour ne laisse rien derrière', async ({ page }) => {
    const errors = await open(page);
    await ready3d(page);
    await page.getByTestId('engine-mode-2d').click();
    await expect(page.getByTestId('engine-view').locator('canvas')).toHaveCount(1);
    await page.getByTestId('engine-mode-3d').click();
    await ready3d(page);
    await page.getByTestId('engine-mode-2d').click();
    await page.reload();
    // Le choix du visiteur reste.
    await expect(page.getByTestId('engine-mode-2d')).toHaveAttribute('aria-pressed', 'true');
    expect(errors).toEqual([]);
  });

  test('une perte de contexte : la 2D prend le relais, puis la 3D revient', async ({ page }) => {
    const errors = await open(page);
    await ready3d(page);
    await page.evaluate(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const gl = d.stage().renderer.getContext();
      (window as any).__lose = gl.getExtension('WEBGL_lose_context');
      (window as any).__lose.loseContext();
    });
    await page.waitForFunction(() => !(document.querySelector('[data-testid=engine-view]') as any).__engine.live3d());
    const before = await page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.frames);
    await expect.poll(() => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.frames)).toBeGreaterThan(before + 3);
    // Le visiteur voit l'écorché pendant ce temps.
    expect(await page.getByTestId('engine-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
      const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
      let painted = 0;
      for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
      return painted;
    })).toBeGreaterThan(5000);
    await page.evaluate(() => (window as any).__lose.restoreContext());
    await page.waitForFunction(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.live3d());
    const drawn = await page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.drawn);
    await expect.poll(() => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.drawn)).toBeGreaterThan(drawn + 3);
    expect(errors.filter((text) => !/CONTEXT_LOST|context.*lost/i.test(text))).toEqual([]);
  });

  test('aucun objet GPU de plus d\'une image à l\'autre, ni après des changements de moteur', async ({ page }) => {
    await page.addInitScript(() => {
      const counts: Record<string, number> = {};
      (window as any).__gl = counts;
      for (const name of ['createBuffer', 'createTexture', 'createVertexArray', 'createProgram', 'createShader', 'createFramebuffer', 'createRenderbuffer', 'deleteBuffer', 'deleteTexture', 'deleteVertexArray', 'deleteProgram']) {
        const proto = WebGL2RenderingContext.prototype as any;
        const original = proto[name];
        counts[name] = 0;
        proto[name] = function (...args: unknown[]) {
          counts[name]++;
          return original.apply(this, args);
        };
      }
    });
    const errors = await open(page, '#m=12.v.6496.94.8900.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1');
    await ready3d(page);
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
    const frames = () => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.drawn);
    const start = await frames();
    await expect.poll(frames, { timeout: 20_000 }).toBeGreaterThan(start + 10);
    const snapshot = () => page.evaluate(() => {
      const stage = (document.querySelector('[data-testid=engine-view]') as any).__engine.stage();
      return { gl: { ...(window as any).__gl }, geometries: stage.renderer.info.memory.geometries, textures: stage.renderer.info.memory.textures, programs: stage.renderer.info.programs.length };
    });
    const first = await snapshot();
    const mid = await frames();
    await expect.poll(frames, { timeout: 30_000 }).toBeGreaterThan(mid + 30);
    expect(await snapshot()).toEqual(first);

    // Changer de moteur et revenir : le même compte de géométries, rien qui s'accumule.
    const rebuild = (cylinders: number) => page.evaluate((count) => {
      const button = [...document.querySelectorAll('fieldset')].find((set) => /Cylindres/.test(set.querySelector('legend')?.textContent ?? ''))!.querySelectorAll('button');
      [...button].find((item) => item.textContent!.trim() === String(count))!.click();
    }, cylinders);
    const memoryAfter = async (cylinders: number) => {
      await rebuild(cylinders);
      await page.waitForFunction((count) => (document.querySelector('[data-testid=engine-view]') as any).__engine.layout()?.count === count, cylinders);
      const reached = await frames();
      await expect.poll(frames, { timeout: 20_000 }).toBeGreaterThan(reached + 2);
      return (await snapshot()).geometries;
    };
    const twelve = await memoryAfter(12);
    await memoryAfter(8);
    await memoryAfter(6);
    await memoryAfter(8);
    expect(await memoryAfter(12)).toBe(twelve);

    // Le simple arbre change les pièces : aller et retour ne laissent rien non plus.
    const camshaft = (label: string, single: boolean) => page.evaluate(({ label }) => {
      const set = [...document.querySelectorAll('fieldset')].find((item) => /Distribution/.test(item.querySelector('legend')?.textContent ?? ''))!;
      [...set.querySelectorAll('button')].find((item) => item.textContent!.trim() === label)!.click();
    }, { label }).then(() => page.waitForFunction((want) => (document.querySelector('[data-testid=engine-view]') as any).__engine.scene()?.d?.rockers !== null === want, single));
    const settled = async () => {
      const reached = await frames();
      await expect.poll(frames, { timeout: 20_000 }).toBeGreaterThan(reached + 2);
      return (await snapshot()).geometries;
    };
    await camshaft('Simple arbre', true);
    const single = await settled();
    await camshaft('Double arbre', false);
    expect(await settled()).toBe(twelve);
    await camshaft('Simple arbre', true);
    expect(await settled()).toBe(single);
    expect(errors).toEqual([]);
  });

  test('un simple arbre dans la vue : un arbre par rangée, deux soupapes et des culbuteurs, en 3D et en 2D', async ({ page }) => {
    const shape = () => page.evaluate(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const scene = d.scene();
      return { sohc: d.layout().valve.sohc, banks: d.layout().banks, count: d.layout().count, cams: scene.cams.length, valvesIn: scene.d.valvesIn.count, valvesEx: scene.d.valvesEx.count, rockers: scene.d.rockers?.count ?? 0, buckets: scene.d.buckets?.count ?? 0, chains: scene.chains.length };
    });
    for (const [hash, banks, count] of [
      ['#m=4.inline.2000.86.6500.stock.natural.petrol.0.off.dump_valve~compact.1200.15.front.1_natural_standard_single_medium_roots_off_400_sohc_stock', 1, 4],
      ['#m=8.cross_plane.5000.96.6000.sport.natural.petrol.0.off.dump_valve~coupe.1600.18.rear.1_natural_standard_dual_medium_roots_off_400_sohc_stock', 2, 8],
      ['#m=4.boxer.2000.92.6500.stock.natural.petrol.0.off.dump_valve~coupe.1300.17.rear.1_natural_standard_single_medium_roots_off_400_sohc_stock', 2, 4],
    ] as const) {
      // Sans quitter la page, un nouveau `#` ne recharge rien.
      await page.goto('about:blank');
      const errors = await open(page, hash);
      await ready3d(page);
      expect(await shape()).toEqual({ sohc: true, banks, count, cams: banks, valvesIn: count, valvesEx: count, rockers: count * 2, buckets: 0, chains: banks });
      // Ça tourne, et l'arbre avec : un culbuteur bascule.
      await page.getByTestId('start').click();
      await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
      await expect.poll(() => page.evaluate(() => {
        const pose = (document.querySelector('[data-testid=engine-view]') as any).__engine.pose();
        return Math.max(...pose.rockerIn, ...pose.rockerEx);
      }), { timeout: 15_000 }).toBeGreaterThan(0.01);
      await page.getByTestId('engine-mode-2d').click();
      await expect.poll(() => page.getByTestId('engine-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
        const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
        let painted = 0;
        for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
        return painted;
      })).toBeGreaterThan(5000);
      await page.getByTestId('engine-mode-3d').click();
      expect(errors).toEqual([]);
    }
  });

  test('le choix du simple arbre : la fiche, le résumé du bouton et le lien de partage suivent', async ({ page }) => {
    const errors = await open(page, '#m=4.inline.2000.86.6500.stock.natural.petrol.0.off.dump_valve~compact.1200.15.front.1');
    const hp = () => page.evaluate(() => {
      const value = document.querySelector('[data-testid=open-engine-summary]')!.textContent!;
      return Number(/(\d+) ch/.exec(value)![1]);
    });
    const twin = await hp();
    expect(await page.getByTestId('open-engine-summary').textContent()).not.toContain('Simple arbre');
    await ouvrir(page, 'engine');
    await page.getByRole('group', { name: 'Distribution' }).getByRole('button', { name: 'Simple arbre' }).click();
    await expect(page.getByTestId('open-engine-summary')).toContainText('Simple arbre');
    await expect(page.getByTestId('camshaft-help')).toContainText('deux soupapes');
    await expect.poll(hp).toBeLessThan(twin * 0.95);
    await page.waitForFunction(() => /_sohc_/.test(location.hash));
    expect(errors).toEqual([]);
  });

  test('la vue suit en direct cylindres, cylindrée, alésage et carburant', async ({ page }) => {
    const errors = await open(page);
    await ready3d(page);
    const shape = () => page.evaluate(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const layout = d.layout();
      const scene = d.scene();
      return { count: layout.count, bore: layout.bore, stroke: layout.stroke, fuel: layout.fuel, pistons: scene.d?.pistons.count, valves: scene.d?.valvesIn.count, glow: scene.d?.glowTips?.count ?? 0 };
    });
    const before = await shape();
    expect(before.pistons).toBe(before.count);
    expect(before.valves).toBe(before.count * 2);
    const choose = (legend: RegExp, label: string) => page.evaluate(({ source, label }) => {
      const set = [...document.querySelectorAll('fieldset')].find((item) => new RegExp(source).test(item.querySelector('legend')?.textContent ?? ''))!;
      [...set.querySelectorAll('button')].find((item) => item.textContent!.trim() === label)!.click();
    }, { source: legend.source, label });
    await choose(/Cylindres/, '6');
    await page.waitForFunction(() => {
      const scene = (document.querySelector('[data-testid=engine-view]') as any).__engine.scene();
      return scene?.d?.pistons.count === 6;
    }, null, { timeout: 15_000 });
    await choose(/Carburant/, 'Diesel');
    await page.waitForFunction(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.scene()?.d?.glowTips?.count === 6, null, { timeout: 15_000 });
    await ouvrir(page, 'engine');
    await page.getByRole('slider', { name: 'Alésage' }).fill('92');
    await page.waitForFunction(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.layout().bore === 92, null, { timeout: 15_000 });
    await expect(page.getByTestId('engine-view').locator('..')).toContainText('92 mm');
    expect(errors).toEqual([]);
  });

  test('une étincelle sur l\'essence, jamais sur le diesel, et le pignon en prise au démarreur', async ({ page }) => {
    // Le compte se fait à chaque image du navigateur : une étincelle dure quelques images.
    const watch = () => page.evaluate(() => new Promise<{ sparks: number; sprays: number; cuts: number; sparkInCut: number; crankFrames: number; engaged: number; glow: number; flames: number }>((resolve) => {
      const out = { sparks: 0, sprays: 0, cuts: 0, sparkInCut: 0, crankFrames: 0, engaged: 0, glow: 0, flames: 0 };
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const until = performance.now() + 3500;
      const loop = () => {
        const t = (window as any).__revforge.telemetry();
        const pose = d.pose();
        out.sparks += d.sparksDrawn;
        out.sprays += d.spraysDrawn;
        out.flames += d.flamesDrawn;
        if (t.cut) {
          out.cuts++;
          out.sparkInCut += d.sparksDrawn;
        }
        if (t.cranking) {
          out.crankFrames++;
          if (pose.starter > 0.5) out.engaged++;
        }
        out.glow = Math.max(out.glow, pose.glow);
        if (performance.now() < until) requestAnimationFrame(loop);
        else resolve(out);
      };
      loop();
    }));
    const errors = await open(page);
    await ready3d(page);
    await page.getByTestId('start').click();
    const petrol = await watch();
    expect(petrol.crankFrames).toBeGreaterThan(0);
    expect(petrol.engaged).toBeGreaterThan(0);
    expect(petrol.sparks).toBeGreaterThan(0);
    expect(petrol.flames).toBeGreaterThan(0);
    expect(petrol.glow).toBe(0);
    // Le rupteur : pied au plancher, la coupure revient sans cesse.
    await page.keyboard.down('Space');
    const limiter = await page.evaluate(() => new Promise<{ cuts: number; sparkInCut: number }>((resolve) => {
      const out = { cuts: 0, sparkInCut: 0 };
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const until = performance.now() + 6000;
      const loop = () => {
        const t = (window as any).__revforge.telemetry();
        if (t.cut) {
          out.cuts++;
          out.sparkInCut += d.pose().spark.reduce((sum: number, level: number) => sum + (level > 0 ? 1 : 0), 0);
        }
        if (performance.now() < until) requestAnimationFrame(loop);
        else resolve(out);
      };
      loop();
    }));
    await page.keyboard.up('Space');
    expect(limiter.cuts).toBeGreaterThan(0);
    expect(limiter.sparkInCut).toBe(0);
    expect(errors).toEqual([]);
  });

  test('sur un diesel : injection et préchauffage, aucune bougie d\'allumage', async ({ page }) => {
    const errors = await open(page, '#m=4.inline.2000.85.4500.stock.turbo.diesel.0.off.dump_valve~compact.1350.18.front.1');
    await ready3d(page);
    await page.getByTestId('start').click();
    const seen = await page.evaluate(() => new Promise<{ sparks: number; sprays: number; glow: number }>((resolve) => {
      const out = { sparks: 0, sprays: 0, glow: 0 };
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      const until = performance.now() + 4000;
      const loop = () => {
        out.sparks += d.pose().sparkCount;
        out.sprays = Math.max(out.sprays, d.pose().sprayCount);
        out.glow = Math.max(out.glow, d.pose().glow);
        if (performance.now() < until) requestAnimationFrame(loop);
        else resolve(out);
      };
      loop();
    }));
    expect(seen.sparks).toBe(0);
    expect(seen.sprays).toBeGreaterThan(0);
    expect(seen.glow).toBeGreaterThan(0.2);
    expect(errors).toEqual([]);
  });

  test('moteur coupé, rien ne bouge ; animation réduite, tout reste dessiné et le bouton relance en direct', async ({ page }) => {
    const errors = await open(page);
    await ready3d(page);
    const crank = () => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.pose().crankDeg);
    const a = await crank();
    await page.waitForTimeout(300);
    expect(await crank()).toBe(a);
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
    await expect.poll(crank).not.toBe(await crank());
    // Le bouton d'animations agit tout de suite, sans recharger.
    await ouvrir(page, 'display');
    await page.getByTestId('animations').click();
    await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
    const frames = () => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.frames);
    const f0 = await frames();
    await expect.poll(frames).toBeGreaterThan(f0 + 2);
    const frozen = await crank();
    const f1 = await frames();
    await expect.poll(frames).toBeGreaterThan(f1 + 5);
    expect(await crank()).toBe(frozen);
    const drawnFrozen = await page.evaluate(() => {
      const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
      return { pistons: d.scene().d.pistons.count, drawn: d.drawn };
    });
    expect(drawnFrozen.pistons).toBe(4);
    await page.getByTestId('animations').click();
    await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
    await expect.poll(crank).not.toBe(frozen);
    // Le gaz repart aussi : les flammes reviennent sans recharger la page.
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.motion.gas.active)).toBe(true);
    expect(errors).toEqual([]);
  });

  test('le système demande moins d\'animation : la vue dessine quand même tout, et la 2D aussi', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    const errors = await open(page);
    await ready3d(page);
    await page.getByTestId('engine-mode-2d').click();
    await expect.poll(() => page.getByTestId('engine-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
      const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
      let painted = 0;
      for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
      return painted;
    })).toBeGreaterThan(5000);
    expect(errors).toEqual([]);
  });

  test('tourner à la souris et au clavier, revenir, sans bloquer le défilement au doigt', async ({ page }) => {
    const errors = await open(page);
    await ready3d(page);
    const solid = page.getByTestId('engine-view').locator('canvas.solid');
    expect(await solid.evaluate((canvas: HTMLCanvasElement) => getComputedStyle(canvas).touchAction)).toMatch(/pan-y/);
    const angle = () => page.evaluate(() => {
      const stage = (document.querySelector('[data-testid=engine-view]') as any).__engine.stage();
      const { x, y, z } = stage.camera.position;
      return [x, y, z].map((value: number) => Math.round(value * 10) / 10);
    });
    const home = await angle();
    const box = await solid.boundingBox();
    await page.mouse.move(box!.x + box!.width / 2, box!.y + box!.height / 2);
    await page.mouse.down();
    await page.mouse.move(box!.x + box!.width / 2 + 80, box!.y + box!.height / 2 + 30, { steps: 4 });
    await page.mouse.up();
    const turned = await angle();
    expect(turned).not.toEqual(home);
    await page.getByTestId('engine-reset').click();
    expect(await angle()).toEqual(home);
    await solid.focus();
    await page.keyboard.press('ArrowLeft');
    expect(await angle()).not.toEqual(home);
    await page.keyboard.press('0');
    expect(await angle()).toEqual(home);
    // La molette reste à la page.
    const before = await page.evaluate(() => window.scrollY);
    await page.mouse.move(box!.x + 20, box!.y + 20);
    await page.mouse.wheel(0, 200);
    await expect.poll(() => page.evaluate(() => window.scrollY)).toBeGreaterThan(before);
    expect(await angle()).toEqual(home);
    expect(errors).toEqual([]);
  });

  test('les noms des pièces passent par la traduction, sur téléphone sans défilement horizontal', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 800 });
    const errors = await open(page);
    await page.getByTestId('engine-names').click();
    await page.getByTestId('engine-mode-2d').click();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1)).toBe(true);
    expect(errors).toEqual([]);
  });

  test('le moteur électrique a sa vue, en 3D comme en 2D', async ({ page }) => {
    const errors = await open(page, '#m=electric~compact.1350.18.front.1');
    await ready3d(page);
    expect(await page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.layout())).toBeNull();
    await page.getByTestId('engine-mode-2d').click();
    await expect(page.getByTestId('engine-state')).toHaveCount(0);
    await page.getByTestId('engine-mode-3d').click();
    await ready3d(page);
    expect(errors).toEqual([]);
  });
});
