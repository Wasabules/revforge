import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { BoxGear, BoxSpec } from '../src/lib/engine';
import { DMF_MAX_DEG, DMF_NM_PER_DEG, GearboxMotion, RATTLE_PITCH_SHARE, RING_CONTACT, buildGearbox, meshError, newFrame, planetError, trackAt, type BoxFrame, type GearboxLayout } from '../src/lib/gearboxModel';
import { buildParts, measuredHelix, toothOutline, countTeeth, wheelTeethMesh } from '../src/lib/gearboxGeometry';

/* eslint-disable @typescript-eslint/no-explicit-any */

// ── Des boîtes telles que les construit le C++ : nombres de dents et rapports repris de `designGearbox` ──

function gearOf(gear: number, driver: number, driven: number, direct = false): BoxGear {
  return { gear, driver, driven, ratio: direct ? 1 : driven / driver, overall: 0, contact: 1.5, topKmh: 100, meshHz: 100, direct };
}

const baseBox = (over: Partial<BoxSpec>): BoxSpec => ({
  kind: 0,
  gears: [],
  finalDrive: 4,
  finalPinion: 17,
  finalRing: 65,
  inputDriver: 0,
  inputDriven: 0,
  reverseOverall: 3.5,
  limit: 300,
  moduleMm: 2.5,
  helix: 28,
  center: 70,
  inertia: 0.01,
  efficiency: 0.95,
  threeShafts: false,
  hypoid: false,
  faceMm: 19,
  dogTeeth: 36,
  coneRadiusMm: 31.5,
  reverseTeeth: [13, 22, 45],
  ...over,
});

const SYNCHRO5 = baseBox({ gears: [gearOf(1, 11, 38), gearOf(2, 18, 31), gearOf(3, 22, 27), gearOf(4, 25, 24), gearOf(5, 28, 21)] });
const DOG6 = baseBox({ kind: 1, helix: 0, moduleMm: 3.2, faceMm: 15, dogTeeth: 6, gears: [gearOf(1, 12, 32), gearOf(2, 16, 28), gearOf(3, 19, 25), gearOf(4, 21, 23), gearOf(5, 23, 21), gearOf(6, 25, 19)] });
const SEQ6 = { ...DOG6, kind: 2 };
const DCT7 = baseBox({ kind: 3, gears: [gearOf(1, 11, 41), gearOf(2, 15, 36), gearOf(3, 19, 32), gearOf(4, 22, 29), gearOf(5, 25, 27), gearOf(6, 27, 26), gearOf(7, 28, 24)] });
const DCT8 = baseBox({ kind: 3, gears: [...DCT7.gears, gearOf(8, 30, 22)] });
const LONG6 = baseBox({ threeShafts: true, hypoid: true, finalPinion: 11, finalRing: 41, inputDriver: 20, inputDriven: 29, gears: [gearOf(1, 12, 35), gearOf(2, 17, 30), gearOf(3, 22, 26), gearOf(4, 0, 0, true), gearOf(5, 28, 20), gearOf(6, 31, 17)] });
LONG6.gears[3] = { ...LONG6.gears[3], driver: 1, driven: 1 };
const LONG_DCT = { ...LONG6, kind: 3 };
const CONVERTER8 = baseBox({ kind: 4, gears: Array.from({ length: 8 }, (_, i) => gearOf(i + 1, 12 + i * 3, 40 - i * 2)) });
const REDUCER = baseBox({ kind: 5, helix: 22, inputDriver: 31, inputDriven: 70, finalPinion: 23, finalRing: 85, reverseTeeth: [0, 0, 0], gears: [gearOf(1, 31, 70)] });

const ALL: [string, BoxSpec][] = [['synchro 5', SYNCHRO5], ['crabots 6', DOG6], ['séquentielle 6', SEQ6], ['double embrayage 7', DCT7], ['double embrayage 8', DCT8], ['longitudinale 6', LONG6], ['double embrayage longitudinale', LONG_DCT], ['convertisseur 8', CONVERTER8], ['réducteur', REDUCER]];

const build = (box: BoxSpec, flywheel: 'single' | 'dual_mass' = 'single', drive: 'front' | 'rear' | 'all' = 'front') => buildGearbox(box, { flywheel, drive, starterRing: 132 });

/** Fait tourner la boîte image par image, à ce régime et dans cet état. */
function spin(kin: GearboxMotion, frame: BoxFrame, frames: number, inputRpm: number, outputRpm: number, seconds = 1 / 60): void {
  frame.inputRpm = inputRpm;
  frame.outputRpm = outputRpm;
  for (let i = 0; i < frames; i++) {
    frame.inputTotal += ((inputRpm / 60 / 12) * 2 * Math.PI) * seconds;
    frame.outputTotal += ((outputRpm / 60 / 12) * 2 * Math.PI) * seconds;
    frame.flywheelTotal += ((inputRpm / 60 / 12) * 2 * Math.PI) * seconds;
    kin.update(frame, seconds);
  }
}

/** Le nombre de sommets de dents d'un vrai maillage : les points au rayon de tête sur une face, regroupés par dent. */
function teethInMesh(positions: ArrayLike<number>): number {
  let rMax = 0;
  let xMin = Infinity;
  for (let v = 0; v < positions.length; v += 3) {
    rMax = Math.max(rMax, Math.hypot(positions[v + 1], positions[v + 2]));
    xMin = Math.min(xMin, positions[v]);
  }
  const angles: number[] = [];
  for (let v = 0; v < positions.length; v += 3) {
    if (Math.abs(positions[v] - xMin) > 1e-3) continue;
    if (Math.abs(Math.hypot(positions[v + 1], positions[v + 2]) - rMax) > 1e-3) continue;
    angles.push(Math.atan2(positions[v + 2], positions[v + 1]));
  }
  angles.sort((a, b) => a - b);
  // Deux sommets de tête par dent, chacun répété : on compte les trous entre plateaux.
  const unique = angles.filter((a, i) => i === 0 || a - angles[i - 1] > 1e-4);
  return unique.length / 2;
}

test.describe('la mécanique, sans navigateur', () => {
  test('chaque paire dessine le nombre de dents de la fiche, pas un de plus', () => {
    for (const [name, box] of ALL) {
      const layout = build(box);
      for (const pair of layout.pairs) {
        if (pair.direct) continue;
        const g = box.gears[pair.gear - 1];
        if (box.kind === 3 && g.direct) continue;
        const a = layout.wheels[pair.driver];
        const b = layout.wheels[pair.driven];
        expect(a.teeth, `${name}, pignon du ${pair.gear}`).toBe(g.driver);
        expect(b.teeth, `${name}, roue du ${pair.gear}`).toBe(g.driven);
        expect(countTeeth(toothOutline(a.teeth, a.moduleT, a.moduleN, a.helix)), `${name}, contour`).toBe(g.driver);
        // Sur le maillage lui-même : c'est lui qui part vers la carte graphique.
        expect(teethInMesh(wheelTeethMesh(a).positions), `${name}, maillage du pignon du ${pair.gear}`).toBe(g.driver);
        expect(teethInMesh(wheelTeethMesh(b).positions), `${name}, maillage de la roue du ${pair.gear}`).toBe(g.driven);
      }
      if (layout.final) {
        expect(layout.wheels[layout.final.pinion].teeth).toBe(box.finalPinion);
        expect(layout.wheels[layout.final.ring].teeth).toBe(box.finalRing);
      }
    }
  });

  test('le rapport lu sur les dents dessinées est le rapport configuré, et les rayons font l\'entraxe', () => {
    for (const [name, box] of ALL) {
      if (box.kind === 4) continue;
      const layout = build(box);
      for (const pair of layout.pairs) {
        const g = box.gears[pair.gear - 1];
        const expected = g.direct ? 1 : box.threeShafts ? (g.driven / g.driver) * (box.inputDriven / box.inputDriver) : g.driven / g.driver;
        expect(pair.drawnRatio, `${name}, rapport ${pair.gear}`).toBeCloseTo(expected, 6);
        if (pair.direct) continue;
        const a = layout.wheels[pair.driver];
        const b = layout.wheels[pair.driven];
        if (box.kind !== 5) expect(a.radius + b.radius, `${name}, entraxe ${pair.gear}`).toBeCloseTo(box.center, 6);
        // Le pas apparent est le même sur les deux roues, sinon elles ne peuvent pas engrener.
        expect(a.moduleT).toBeCloseTo(b.moduleT, 6);
      }
    }
  });

  test('changer le module change le nombre de dents et leur taille, à entraxe constant', () => {
    const coarse = build(baseBox({ moduleMm: 3.5, gears: [gearOf(1, 8, 30)] }));
    const fine = build(baseBox({ moduleMm: 1.5, gears: [gearOf(1, 18, 70)] }));
    const c = coarse.wheels[coarse.pairs[0].driven];
    const f = fine.wheels[fine.pairs[0].driven];
    expect(c.moduleN).toBeGreaterThan(f.moduleN * 2);
    expect(c.radius + coarse.wheels[coarse.pairs[0].driver].radius).toBeCloseTo(f.radius + fine.wheels[fine.pairs[0].driver].radius, 6);
    // La hauteur d'une dent suit le module : 2,25 fois, tête et pied.
    const depth = (wheel: typeof c) => {
      const radii = toothOutline(wheel.teeth, wheel.moduleT, wheel.moduleN, wheel.helix).map((p) => p.r);
      return Math.max(...radii) - Math.min(...radii);
    };
    expect(depth(c)).toBeCloseTo(2.25 * c.moduleN, 6);
    expect(depth(f)).toBeCloseTo(2.25 * f.moduleN, 6);
    expect(depth(c)).toBeGreaterThan(depth(f) * 2);
  });

  test('l\'hélice : nulle en denture droite, au bon angle sinon, et de sens opposés dans une paire', () => {
    const straight = build(DOG6);
    for (const wheel of straight.wheels) {
      if (wheel.bevel) continue;
      expect(Math.abs(wheel.helix), wheel.role).toBe(0);
      const mesh = wheelTeethMesh(wheel);
      expect(Math.abs(measuredHelix(mesh, wheel.radius, wheel.width))).toBeLessThan(1e-3);
    }
    for (const degrees of [12, 28, 40]) {
      const helical = build({ ...SYNCHRO5, helix: degrees });
      for (const pair of helical.pairs) {
        const a = helical.wheels[pair.driver];
        const b = helical.wheels[pair.driven];
        expect(Math.abs(a.helix)).toBeCloseTo((degrees * Math.PI) / 180, 6);
        expect(a.helix).toBeCloseTo(-b.helix, 9);
        // Mesurée sur les sommets du maillage : c'est l'hélice que voit l'œil.
        const measured = (measuredHelix(wheelTeethMesh(a), a.radius, a.width) * 180) / Math.PI;
        expect(measured).toBeCloseTo(degrees, 1);
        expect((measuredHelix(wheelTeethMesh(b), b.radius, b.width) * 180) / Math.PI).toBeCloseTo(-degrees, 1);
      }
    }
  });

  test('les dents d\'une paire restent en prise à toute vitesse et à tout rapport', () => {
    for (const [name, box] of ALL) {
      const layout = build(box);
      const kin = new GearboxMotion(layout);
      const frame = newFrame();
      for (const gear of [0, 1, box.gears.length]) {
        frame.gear = gear;
        frame.meshed = gear !== 0;
        for (const rpm of [800, 3333, 9000, 17500]) {
          const g = box.gears[Math.max(gear - 1, 0)];
          const ratio = g.direct ? 1 : (g.driven / g.driver) * (box.threeShafts ? box.inputDriven / Math.max(box.inputDriver, 1) : 1);
          spin(kin, frame, 40, rpm, gear === 0 ? 120 : rpm / ratio, 1 / 30);
          for (const mesh of layout.meshes) {
            const idler = layout.reverse && (mesh.a === layout.reverse.idler || mesh.b === layout.reverse.idler);
            if (idler) continue;
            expect(meshError(layout, mesh, kin.angles), `${name}, rapport ${gear}, ${rpm} tr/min, ${layout.wheels[mesh.a].role} sur ${layout.wheels[mesh.b].role}`).toBeLessThan(1e-6);
          }
          expect(planetError(layout, kin.angles), `${name}, trains`).toBeLessThan(1e-6);
        }
      }
    }
  });

  test('marche arrière : l\'intermédiaire entre en prise, le train reste engrené, et la sortie tourne à l\'envers', () => {
    for (const box of [SYNCHRO5, LONG6]) {
      const layout = build(box);
      const reverse = layout.reverse!;
      const kin = new GearboxMotion(layout);
      const frame = newFrame();
      frame.gear = 1;
      frame.meshed = true;
      const ratio = box.gears[0].driven / box.gears[0].driver * (box.threeShafts ? box.inputDriven / box.inputDriver : 1);
      spin(kin, frame, 60, 3000, 3000 / ratio);
      const hub = layout.hubBody;
      const before = kin.angles[hub];
      spin(kin, frame, 10, 3000, 3000 / ratio);
      const forward = Math.sign(kin.angles[hub] - before);
      expect(kin.reverseIn).toBe(0);

      frame.gear = -1;
      spin(kin, frame, 120, 1500, -1500 / 3.45);
      expect(kin.reverseIn).toBe(1);
      const start = kin.angles[hub];
      spin(kin, frame, 10, 1500, -1500 / 3.45);
      expect(Math.sign(kin.angles[hub] - start)).toBe(-forward);
      const meshes = layout.meshes.filter((m) => m.a === reverse.idler || m.b === reverse.idler);
      expect(meshes.length).toBe(2);
      for (const mesh of meshes) expect(meshError(layout, mesh, kin.angles)).toBeLessThan(1e-6);
    }
  });

  test('prise constante : toutes les roues tournent, chacune à son rapport, et seule celle engagée suit la sortie', () => {
    const layout = build(SYNCHRO5);
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    frame.gear = 2;
    frame.meshed = true;
    const g2 = SYNCHRO5.gears[1];
    const rpm = 3000;
    const output = (rpm * g2.driver) / g2.driven;
    spin(kin, frame, 60, rpm, output);
    const before = Float64Array.from(kin.angles);
    spin(kin, frame, 30, rpm, output);
    const turned = (body: number) => kin.angles[body] - before[body];
    const inputTurn = turned(layout.input);
    const outputTurn = turned(layout.hubBody);
    expect(Math.abs(inputTurn)).toBeGreaterThan(0.5);
    for (const pair of layout.pairs) {
      const wheel = layout.wheels[pair.driven];
      const g = SYNCHRO5.gears[pair.gear - 1];
      // La roue folle tourne à son propre rapport, en sens inverse du pignon.
      expect(turned(wheel.body) / inputTurn, `rapport ${pair.gear}`).toBeCloseTo(-g.driver / g.driven, 6);
      const followsOutput = Math.abs(turned(wheel.body) - outputTurn) < 1e-6;
      expect(followsOutput, `rapport ${pair.gear}`).toBe(pair.gear === 2);
    }
    expect(kin.engaged).toBe(2);
    expect(kin.rpm[layout.hubBody]).toBeCloseTo((-rpm * g2.driver) / g2.driven, 6);

    // Au point mort, plus aucune roue n'entraîne la sortie : elle va à la vitesse de la voiture, le primaire à la sienne.
    frame.gear = 0;
    frame.meshed = false;
    spin(kin, frame, 60, 1000, 500);
    expect(kin.engaged).toBe(0);
    const n = Float64Array.from(kin.angles);
    spin(kin, frame, 30, 1000, 500);
    expect(kin.angles[layout.input] - n[layout.input]).toBeCloseTo(((1000 / 60 / 12) * 2 * Math.PI * 30) / 60, 6);
    expect(kin.angles[layout.hubBody] - n[layout.hubBody]).toBeCloseTo((-(500 / 60 / 12) * 2 * Math.PI * 30) / 60, 6);
  });

  test('un passage : le baladeur sort, s\'arrête sur le cône, puis s\'engage ; la sortie n\'est liée qu\'à la fin', () => {
    const layout = build(SYNCHRO5);
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    const sel2 = layout.selectors.findIndex((s) => s.left?.gear === 2 || s.right?.gear === 2);
    const side2 = layout.selectors[sel2].left?.gear === 2 ? -1 : 1;
    const sel3 = layout.selectors.findIndex((s) => s.left?.gear === 3 || s.right?.gear === 3);
    const side3 = layout.selectors[sel3].left?.gear === 3 ? -1 : 1;
    expect(sel2).not.toBe(sel3);

    frame.gear = 2;
    frame.meshed = true;
    spin(kin, frame, 40, 4000, 2000);
    expect(kin.pos[sel2] * side2).toBeCloseTo(1, 3);
    expect(kin.engaged).toBe(2);

    frame.step = 2;
    frame.pending = 3;
    spin(kin, frame, 30, 4000, 2000);
    expect(Math.abs(kin.pos[sel2])).toBeLessThan(0.01);
    frame.gear = 3;
    frame.meshed = false;
    frame.step = 3;
    spin(kin, frame, 20, 3500, 2000);
    expect(kin.engaged).toBe(0);
    frame.step = 4;
    frame.coneSlip = 40;
    spin(kin, frame, 30, 3000, 2000);
    // Bloqué sur la bague tant que le cône n'a pas égalisé.
    expect(kin.pos[sel3] * side3).toBeCloseTo(RING_CONTACT, 3);
    expect(kin.engaged).toBe(0);
    expect(kin.syncSelector).toBe(sel3);
    expect(Math.abs(kin.syncGapRpm)).toBeGreaterThan(50);
    frame.step = 5;
    frame.coneSlip = 0;
    spin(kin, frame, 30, 2500, 2000);
    expect(kin.engaged).toBe(0);
    frame.meshed = true;
    frame.step = 6;
    spin(kin, frame, 30, 2500, 2000);
    expect(kin.pos[sel3] * side3).toBeCloseTo(1, 3);
    expect(kin.engaged).toBe(3);

    // Un passage plus rapide que la télémétrie : le modèle dit « en prise », mais tant que le baladeur n'y est pas, rien n'est lié.
    frame.gear = 4;
    frame.step = 0;
    frame.pending = 0;
    spin(kin, frame, 1, 2000, 1500);
    expect(kin.engaged).toBe(0);
    spin(kin, frame, 90, 2000, 1500);
    expect(kin.engaged).toBe(4);
  });

  test('le claquement secoue les roues folles dans leur jeu, jamais celle en prise, et se tait quand le modèle se tait', () => {
    const layout = build(SYNCHRO5);
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    frame.gear = 2;
    frame.meshed = true;
    spin(kin, frame, 40, 0, 0);
    const still = Float64Array.from(kin.angles);
    frame.loose0 = 1;
    frame.loose1 = 1;
    frame.loose2 = -1;
    frame.rattle = 8;
    kin.update(frame, 1 / 60);
    const wheelOf = (gear: number) => layout.wheels[layout.pairs.find((p) => p.gear === gear)!.driven];
    const w1 = wheelOf(1);
    const w2 = wheelOf(2);
    const w3 = wheelOf(3);
    expect(kin.angles[w1.body] - still[w1.body]).toBeCloseTo((RATTLE_PITCH_SHARE * 2 * Math.PI) / w1.teeth, 9);
    expect(kin.angles[w3.body] - still[w3.body]).toBeCloseTo((-RATTLE_PITCH_SHARE * 2 * Math.PI) / w3.teeth, 9);
    expect(kin.angles[w2.body]).toBe(still[w2.body]);
    // Ce va-et-vient reste dans le jeu dessiné : les dents ne se traversent pas.
    expect(RATTLE_PITCH_SHARE).toBeLessThan((1 - 2 * 0.44) / 2);
    frame.loose0 = frame.loose1 = frame.loose2 = 0;
    frame.rattle = 0;
    kin.update(frame, 1 / 60);
    expect(kin.angles[w1.body]).toBe(still[w1.body]);
  });

  test('double embrayage : impairs et pairs sur deux arbres, l\'un dans l\'autre, et le rapport suivant déjà engagé', () => {
    const layout = build(DCT7, 'dual_mass');
    expect(layout.inputOuter).toBeGreaterThanOrEqual(0);
    const outer = layout.shafts.find((s) => s.role === 'inputOuter')!;
    const inner = layout.shafts.find((s) => s.role === 'input')!;
    expect(outer.bore).toBeGreaterThanOrEqual(inner.radius);
    expect(inner.x1).toBeGreaterThan(outer.x1);
    for (const pair of layout.pairs) {
      const driver = layout.wheels[pair.driver];
      expect(driver.body, `rapport ${pair.gear}`).toBe(pair.gear % 2 === 0 ? layout.inputOuter : layout.input);
    }
    // Chaque arbre a ses baladeurs : pairs d'un côté, impairs de l'autre.
    for (const selector of layout.selectors) {
      const parities = [selector.left?.gear, selector.right?.gear].filter((g): g is number => g !== undefined).map((g) => g % 2);
      expect(new Set(parities).size).toBe(1);
    }
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    frame.gear = 3;
    frame.meshed = true;
    frame.throttle = 0.6;
    const rpm = 3000;
    const output = (rpm * 19) / 32;
    spin(kin, frame, 90, rpm, output);
    expect(kin.engaged).toBe(3);
    expect(kin.preselected).toBe(4);
    const sel4 = layout.selectors.findIndex((s) => s.left?.gear === 4 || s.right?.gear === 4);
    expect(Math.abs(kin.pos[sel4])).toBeCloseTo(1, 3);
    // L'arbre creux tourne, entraîné par la sortie à travers le 4 : à sa vitesse, pas à celle du moteur.
    expect(Math.abs(kin.rpm[layout.inputOuter])).toBeCloseTo((output * 29) / 22, 3);
    expect(Math.abs(kin.rpm[layout.inputOuter])).toBeLessThan(rpm);
    // Levier du relais : le rapport suivant reprend le couple, et l'arbre plein ralentit.
    frame.gear = 4;
    frame.step = 7;
    spin(kin, frame, 6, (output * 29) / 22, output);
    expect(kin.clutchMix).toBeGreaterThan(0);
    frame.step = 0;
    spin(kin, frame, 60, (output * 29) / 22, output);
    expect(kin.engaged).toBe(4);
    expect(kin.clutchMix).toBe(1);
  });

  test('volant : un seul disque en simple masse, deux masses et des ressorts comprimés par le couple en double masse', () => {
    const single = build(SYNCHRO5, 'single');
    const dual = build(SYNCHRO5, 'dual_mass');
    const names = (layout: GearboxLayout) => buildParts(layout).map((p) => p.name);
    expect(names(single)).not.toContain('masse-secondaire');
    expect(names(dual)).toContain('masse-secondaire');

    const torque = (layout: GearboxLayout, nm: number) => {
      const kin = new GearboxMotion(layout);
      const frame = newFrame();
      frame.clutchTorque = nm;
      spin(kin, frame, 120, 1000, 0);
      return kin;
    };
    const none = torque(single, 300);
    expect(none.windup).toBe(0);
    expect(none.angles[single.flywheelSecondaryBody]).toBe(none.angles[single.flywheelBody]);
    const low = torque(dual, 100);
    const high = torque(dual, 300);
    expect(high.windup).toBeGreaterThan(low.windup * 2);
    expect(low.windup).toBeCloseTo(100 / DMF_NM_PER_DEG / DMF_MAX_DEG, 3);
    expect(high.angles[dual.flywheelBody] - high.angles[dual.flywheelSecondaryBody]).toBeCloseTo((high.windup * DMF_MAX_DEG * Math.PI) / 180, 6);
    // Une électrique n'a pas de volant.
    expect(build(REDUCER).flywheel).toBe('none');
    expect(names(build(REDUCER))).not.toContain('volant');
  });

  test('chaque type a ses pièces : bagues, crabots, barillet, trains, deux étages ; jamais de fausse boîte à arbres sur une automatique', () => {
    const names = (box: BoxSpec) => buildParts(build(box)).map((p) => p.name);
    expect(names(SYNCHRO5).some((n) => n.startsWith('bague-'))).toBe(true);
    expect(names(DOG6).some((n) => n.startsWith('bague-'))).toBe(false);
    expect(names(DOG6).some((n) => n.startsWith('baladeur-'))).toBe(true);
    expect(names(DOG6)).not.toContain('barillet');
    expect(names(SEQ6)).toContain('barillet');
    expect(names(SEQ6)).toContain('cliquet');
    expect(names(SEQ6).some((n) => n.startsWith('fourchette-'))).toBe(true);

    const auto = build(CONVERTER8);
    expect(auto.selectors.length).toBe(0);
    expect(auto.lay).toBe(-1);
    expect(auto.pairs.length).toBe(0);
    expect(auto.reverse).toBeNull();
    expect(auto.planets.length).toBe(4);
    expect(names(CONVERTER8)).toContain('turbine');
    expect(names(CONVERTER8)).toContain('pompe');
    // La condition de montage : le satellite tient entre planétaire et couronne.
    for (const set of auto.planets) {
      const zs = auto.wheels[set.sun].teeth;
      const zr = auto.wheels[set.annulus].teeth;
      const zp = auto.wheels[set.planets[0]].teeth;
      expect(zr).toBe(zs + 2 * zp);
      expect((zs + zr) % set.planets.length).toBe(0);
    }

    const ev = build(REDUCER);
    expect(ev.selectors.length).toBe(0);
    expect(ev.pairs.length).toBe(1);
    expect(ev.final?.kind).toBe('helical');
    expect(ev.wheels.map((w) => w.teeth)).toEqual([31, 70, 23, 85]);
  });

  test('le pont suit la voiture : couronne parallèle en traction, couple conique à 90° en propulsion', () => {
    const front = build(SYNCHRO5);
    expect(front.final?.kind).toBe('helical');
    expect(front.bodies[front.final!.ringBody].axis).toBe('x');
    const rear = build(LONG6, 'single', 'rear');
    expect(rear.final?.kind).toBe('bevel');
    expect(rear.bodies[rear.final!.ringBody].axis).toBe('z');
    expect(rear.lay).toBeGreaterThanOrEqual(0);
    // Trois arbres : primaire et sortie sur le même axe, et la prise directe lie les deux.
    expect(rear.bodies[rear.input].y).toBe(rear.bodies[rear.output].y);
    const direct = rear.selectors.find((s) => s.left?.gear === 4 || s.right?.gear === 4)!;
    expect((direct.left?.gear === 4 ? direct.left : direct.right)!.body).toBe(rear.input);
    const kin = new GearboxMotion(rear);
    const frame = newFrame();
    frame.gear = 4;
    frame.meshed = true;
    spin(kin, frame, 60, 3000, 3000);
    expect(kin.engaged).toBe(4);
    const a = Float64Array.from(kin.angles);
    spin(kin, frame, 20, 3000, 3000);
    expect(kin.angles[rear.output] - a[rear.output]).toBeCloseTo(kin.angles[rear.input] - a[rear.input], 6);
    // La couronne, elle, va au rapport du pont.
    const ring = rear.final!.ringBody;
    expect(Math.abs((kin.angles[ring] - a[ring]) / (kin.angles[rear.output] - a[rear.output]))).toBeCloseTo(LONG6.finalPinion / LONG6.finalRing, 6);
  });

  test('séquentielle : le barillet mène les fourchettes, sans jamais engager deux rapports à la fois', () => {
    const layout = build(SEQ6);
    const drum = layout.drum!;
    const selector = layout.selectors[0];
    expect(trackAt(selector, drum, drum.neutral)).toBe(0);
    for (let gear = 1; gear <= 6; gear++) {
      const position = drum.neutral + gear;
      const engaged = layout.selectors.filter((s) => Math.abs(trackAt(s, drum, position)) === 1);
      expect(engaged.length, `rapport ${gear}`).toBe(1);
      expect(engaged[0].left?.gear === gear || engaged[0].right?.gear === gear).toBe(true);
      // À mi-chemin vers le suivant, jamais deux baladeurs à fond.
      const between = layout.selectors.filter((s) => Math.abs(trackAt(s, drum, position + 0.5)) === 1);
      expect(between.length).toBe(0);
    }
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    frame.gear = 3;
    frame.meshed = true;
    spin(kin, frame, 90, 3000, 1500);
    expect(kin.drum).toBeCloseTo(drum.neutral + 3, 3);
    expect(kin.engaged).toBe(3);
    const s3 = layout.selectors.findIndex((s) => s.left?.gear === 3 || s.right?.gear === 3);
    expect(Math.abs(kin.pos[s3])).toBeCloseTo(1, 3);
  });

  test('moins d\'animation : tout est dessiné, rien ne tourne, et les baladeurs vont directement à leur place', () => {
    const layout = build(SYNCHRO5);
    const kin = new GearboxMotion(layout);
    const frame = newFrame();
    frame.frozen = true;
    frame.gear = 2;
    frame.meshed = true;
    // L'horloge partagée s'arrête : les totaux restent constants.
    kin.update(frame, 1 / 60);
    kin.update(frame, 1 / 60);
    const sel = layout.selectors.findIndex((s) => s.left?.gear === 2 || s.right?.gear === 2);
    expect(Math.abs(kin.pos[sel])).toBe(1);
    expect(kin.angles[layout.input]).toBe(0);
    expect(buildParts(layout).length).toBeGreaterThan(10);
  });
});

// ── Dans le navigateur ──

async function open(page: Page, hash = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text());
  });
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().box.gears.length)).toBeGreaterThan(0);
  await voir(page, 'gearbox');
  return errors;
}

const panel = (page: Page) => page.getByTestId('gearbox-panel');
/** Hors écran, la vue ne se reconstruit pas : elle attend d'être vue. */
const choose = async (page: Page, name: string) => {
  await ouvrir(page, 'gearbox');
  await panel(page).getByRole('button', { name, exact: true }).click();
  await voir(page, 'gearbox');
  await page.getByTestId('gearbox-view').scrollIntoViewIfNeeded();
};
const view = (page: Page) => page.getByTestId('gearbox-view');
const memory = (page: Page) => page.evaluate(() => {
  const box = (window as any).__revforgeBox;
  const info = box.stage?.renderer.info.memory;
  return { geometries: info?.geometries ?? 0, textures: info?.textures ?? 0, triangles: box.triangles, wheels: box.layout.wheels.length };
});
const on3d = (page: Page) => expect.poll(() => page.evaluate(() => !!(window as any).__revforgeBox?.scene), { timeout: 20000 }).toBe(true);

test.describe('la vue, sur la page', () => {
  test('les dents de la fiche sont celles de la vue, et un réglage reconstruit la boîte sans fuite', async ({ page }) => {
    const errors = await open(page);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    const has3d = await page.evaluate(() => !document.querySelector<HTMLButtonElement>('[data-testid="gearbox-mode-3d"]')!.disabled);
    test.skip(!has3d, 'pas de WebGL2 dans ce navigateur : le repli a son propre test');
    await on3d(page);
    const check = () => page.evaluate(() => {
      const spec = (window as any).__revforge.spec().box;
      const { layout } = (window as any).__revforgeBox;
      return spec.gears.filter((g: any) => !g.direct).every((g: any) => {
        const pair = layout.pairs.find((p: any) => p.gear === g.gear);
        return !!pair && layout.wheels[pair.driver].teeth === g.driver && layout.wheels[pair.driven].teeth === g.driven;
      });
    });
    await expect.poll(check).toBe(true);
    const first = await memory(page);

    // Un tour des réglages : type, denture, nombre de rapports, volant.
    await choose(page, 'Droite');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.layout.helixDeg)).toBe(0);
    await choose(page, 'Hélicoïdale');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.layout.helixDeg)).toBeGreaterThan(0);
    await choose(page, 'Bi-masse');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.layout.flywheel)).toBe('dual_mass');
    await choose(page, 'Simple');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.layout.flywheel)).toBe('single');
    const before = (await memory(page)).geometries;
    for (let i = 0; i < 6; i++) {
      await choose(page, i % 2 === 0 ? 'Double embrayage' : 'À synchros');
      await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.layout.form)).toBe(i % 2 === 0 ? 'dual_clutch' : 'synchro');
    }
    await expect.poll(() => check()).toBe(true);
    // Le renderer ne compte qu'après un rendu : on laisse passer une image.
    await page.waitForTimeout(500);
    const after = await memory(page);
    expect(after.geometries).toBeLessThanOrEqual(before + 2);
    expect(after.geometries).toBeGreaterThan(5);
    expect(first.wheels).toBeGreaterThan(5);
    expect(errors).toEqual([]);
  });

  test('sans WebGL2, la vue reste en coupe, dessinée, et dit pourquoi', async ({ page }) => {
    await page.addInitScript(() => {
      const original = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (this: HTMLCanvasElement, type: string, ...rest: any[]) {
        if (type === 'webgl2' || type === 'webgl' || type === 'experimental-webgl') return null;
        return (original as any).call(this, type, ...rest);
      } as any;
    });
    const errors = await open(page);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    await expect(view(page)).toHaveAttribute('data-mode', '2d');
    await expect(page.getByTestId('gearbox-mode-3d')).toBeDisabled();
    await expect(page.getByTestId('gearbox-no3d')).toBeVisible();
    await expect.poll(() => painted(page)).toBeGreaterThan(5000);
    expect(errors).toEqual([]);
  });

  test('un contexte perdu ne casse rien : on repasse en coupe, puis la 3D revient', async ({ page, browserName }) => {
    const errors = await open(page);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    const has3d = await page.evaluate(() => !document.querySelector<HTMLButtonElement>('[data-testid="gearbox-mode-3d"]')!.disabled);
    test.skip(!has3d || browserName === 'firefox', 'WEBGL_lose_context : testé sur Chromium');
    await on3d(page);
    await page.evaluate(() => {
      const box = (window as any).__revforgeBox;
      const gl = box.stage.renderer.getContext() as WebGL2RenderingContext;
      (window as any).__loseExt = gl.getExtension('WEBGL_lose_context');
      (window as any).__loseExt.loseContext();
    });
    await expect(view(page)).toHaveAttribute('data-lost', 'true');
    await expect.poll(() => painted(page)).toBeGreaterThan(5000);
    await page.evaluate(() => (window as any).__loseExt.restoreContext());
    await expect(view(page)).toHaveAttribute('data-lost', 'false');
    await page.waitForTimeout(300);
    expect(await page.evaluate(() => (window as any).__revforgeBox.stage.lost)).toBe(false);
    expect(errors.filter((e) => !/context|WebGL/i.test(e))).toEqual([]);
  });

  test('le bouton passe de la 3D à la coupe et rend son contexte ; le clavier atteint les commandes', async ({ page }) => {
    const errors = await open(page);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    const has3d = await page.evaluate(() => !document.querySelector<HTMLButtonElement>('[data-testid="gearbox-mode-3d"]')!.disabled);
    test.skip(!has3d, 'pas de WebGL2');
    await on3d(page);
    await expect(view(page)).toHaveAttribute('data-mode', '3d');
    await page.getByTestId('gearbox-mode-2d').click();
    await expect(view(page)).toHaveAttribute('data-mode', '2d');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.stage)).toBeNull();
    await expect.poll(() => painted(page)).toBeGreaterThan(5000);
    await page.getByTestId('gearbox-mode-3d').focus();
    await page.keyboard.press('Enter');
    await expect(view(page)).toHaveAttribute('data-mode', '3d');
    await on3d(page);
    expect(errors).toEqual([]);
  });

  test('en conduisant : la sortie suit la roue engagée, et les autres continuent à leur rapport', async ({ page }) => {
    const errors = await open(page);
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15000 }).toBeGreaterThan(500);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    await page.keyboard.press('ArrowRight');
    await page.keyboard.down('Space');
    await expect.poll(() => page.evaluate(() => (window as any).__revforgeBox.kin.engaged), { timeout: 8000 }).toBe(1);
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().outputRpm), { timeout: 8000 }).toBeGreaterThan(300);
    const state = await page.evaluate(() => {
      const { kin, layout } = (window as any).__revforgeBox;
      const speeds = layout.pairs.filter((p: any) => !p.direct).map((p: any) => ({ gear: p.gear, rpm: Math.abs(kin.rpm[layout.wheels[p.driven].body]) }));
      return { speeds, hub: Math.abs(kin.rpm[layout.hubBody]) };
    });
    await page.keyboard.up('Space');
    const first = state.speeds.find((item) => item.gear === 1)!;
    expect(first.rpm).toBeCloseTo(state.hub, 3);
    for (const other of state.speeds) {
      if (other.gear === 1) continue;
      expect(other.rpm, `la roue du ${other.gear} va plus vite que celle de première`).toBeGreaterThan(first.rpm * 1.1);
    }
    expect(errors).toEqual([]);
  });

  test('couper les animations fige les pièces en direct, sans rien effacer', async ({ page }) => {
    const errors = await open(page);
    await page.getByTestId('start').click();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15000 }).toBeGreaterThan(500);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    const angle = () => page.evaluate(() => { const { kin, layout } = (window as any).__revforgeBox; return kin.angles[layout.input]; });
    const moving = await angle();
    await expect.poll(angle).not.toBe(moving);
    // Sans défiler : hors écran, la boucle s'arrête d'elle-même et le test ne prouverait rien.
    const toggle = () => page.evaluate(() => (document.querySelector('[data-testid="animations"]') as HTMLButtonElement).click());
    await toggle();
    await page.waitForTimeout(200);
    const frozen = await angle();
    await page.waitForTimeout(300);
    expect(await angle()).toBe(frozen);
    await expect.poll(() => painted(page)).toBeGreaterThan(50);
    await toggle();
    await expect.poll(angle).not.toBe(frozen);
    expect(errors).toEqual([]);
  });

  test('téléphone : pas de défilement horizontal, et un doigt qui glisse verticalement garde la page', async ({ browser, browserName }) => {
    test.skip(browserName === 'firefox', 'émulation de téléphone : Chromium');
    const context = await browser.newContext({ viewport: { width: 360, height: 780 }, hasTouch: true, isMobile: true, baseURL: `http://127.0.0.1:${process.env.REVFORGE_PW_PORT ?? '4173'}/`, storageState: { cookies: [], origins: [{ origin: `http://127.0.0.1:${process.env.REVFORGE_PW_PORT ?? '4173'}`, localStorage: [{ name: 'revforge.lang', value: 'fr' }] }] } });
    const page = await context.newPage();
    await open(page);
    await voir(page, 'gearbox');
    await view(page).scrollIntoViewIfNeeded();
    const widths = await page.evaluate(() => ({ doc: document.documentElement.scrollWidth, win: window.innerWidth, touch: getComputedStyle(document.querySelector('[data-testid="gearbox-view"] .gl')!).touchAction }));
    expect(widths.doc).toBeLessThanOrEqual(widths.win);
    expect(widths.touch).toContain('pan-y');
    await context.close();
  });
});

function painted(page: Page) {
  return page.evaluate(() => {
    const canvas = document.querySelector('[data-testid="gearbox-view"] canvas') as HTMLCanvasElement;
    const data = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height).data;
    let filled = 0;
    for (let i = 3; i < data.length; i += 4) if (data[i] > 0) filled++;
    return filled;
  });
}
