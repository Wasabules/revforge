import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import { advanceRace, BEARING_CLEARANCE, bearingTouches, CAGE_RATIO, clampShare, clutchDimensions, CUSHION_TRAVEL, damperWindup, DAMPER_LIMIT, dctBearingTravel, discDiameterFor, dualMassWindup, DUAL_MASS_LIMIT, emptyFrame, emptyTravel, FORK_SLAVE_R, FORK_STUD_R, fullBearingTravel, MASTER_BORE, activeClutch, pedalFromShare, PEDAL_RATIO, PEDAL_TRAVEL, readFrame, SLAVE_BORE, travelFromPedal, type ClutchClock, type ClutchSizing, type RaceState } from '../src/lib/clutchMechanism';
import { clutchLayout, diaphragmX, groupAngle, groupOffset, LIFT_EXAGGERATION, sectionParts } from '../src/lib/clutchSection';
import { revolve } from '../src/lib/clutchScene3d';
import { Motion, SLOW_MOTION } from '../src/lib/motion';
import type { EngineConfig } from '../src/lib/config';
import type { Telemetry } from '../src/lib/engine';

const SIZING: ClutchSizing = { peakTorque: 200, cylinders: 4, displacementCc: 2000, boxFlywheel: 'single', boxKind: 'synchro' };
const single = clutchDimensions(SIZING);
const layout = clutchLayout(single);
const atPedal = (pedal: number) => travelFromPedal(single, pedal, emptyTravel());
const CONFIG = { cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol', multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front', boxKind: 'synchro' } as EngineConfig;
/** Un lien de partage écrit à la main : moteur, voiture, boîte. */
const ENGINE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve';
const CAR = 'compact.1350.18.front.1';
const boxLink = (kind: string, flywheel: string) => `#m=${ENGINE}~${CAR}~${kind},0,x,0,x,0,0,0,${flywheel},`;

test.describe('la chaîne de commande, de la pédale au plateau', () => {
  test('pédale relâchée : tout est au repos, et la butée attend à sa garde', () => {
    const t = atPedal(0);
    expect(t).toMatchObject({ master: 0, slave: 0, bearing: 0, finger: 0, lift: 0, gap: 0, clamp: 1, contact: false });
    expect(layout.noseX - (diaphragmX(layout, t, 0, single.contactR) + layout.sheet / 2)).toBeCloseTo(BEARING_CLEARANCE, 5);
  });

  test('chaque maillon suit le précédent : tige, récepteur, fourchette, butée', () => {
    const t = atPedal(1);
    expect(t.master).toBeCloseTo(PEDAL_TRAVEL / PEDAL_RATIO, 5);
    // Un liquide incompressible : à volume égal, le plus gros piston bouge moins.
    expect(t.slave).toBeCloseTo((t.master * MASTER_BORE * MASTER_BORE) / (SLAVE_BORE * SLAVE_BORE), 5);
    expect(t.slave).toBeLessThan(t.master);
    // La fourchette est un levier sur sa rotule : la butée, entre la rotule et le récepteur, avance moins que le récepteur.
    expect(t.bearing).toBeCloseTo((t.slave * -FORK_STUD_R) / (FORK_SLAVE_R - FORK_STUD_R), 5);
    expect(t.bearing).toBeLessThan(t.slave);
    expect(t.bearing).toBeCloseTo(fullBearingTravel(single), 5);
  });

  test('des valeurs de vraie voiture : butée 8 à 10 mm, plateau 1,5 à 2,5 mm', () => {
    const t = atPedal(1);
    expect(t.bearing).toBeGreaterThan(8);
    expect(t.bearing).toBeLessThan(10);
    expect(t.lift).toBeGreaterThan(1.5);
    expect(t.lift).toBeLessThan(2.5);
    expect(single.leverRatio).toBeGreaterThan(3);
    expect(single.leverRatio).toBeLessThan(5);
    // Il reste un vrai jeu autour du disque, une fois les segments détendus.
    expect(t.gap).toBeGreaterThan(0.5);
  });

  test('le diaphragme est un levier : les doigts vont vers le moteur, le plateau s\'en écarte', () => {
    const rest = atPedal(0);
    for (const pedal of [0.3, 0.6, 1]) {
      const t = atPedal(pedal);
      const lift = t.lift * LIFT_EXAGGERATION;
      expect(diaphragmX(layout, t, lift, single.contactR)).toBeLessThan(diaphragmX(layout, rest, 0, single.contactR));
      expect(diaphragmX(layout, t, lift, single.fulcrumR)).toBeGreaterThan(diaphragmX(layout, rest, 0, single.fulcrumR));
      // Le jonc, lui, ne bouge pas : c'est le pivot.
      expect(diaphragmX(layout, t, lift, single.pivotR)).toBeCloseTo(layout.pivotX, 5);
    }
    // La flexion mise à part, le recul vaut l'enfoncement divisé par le rapport de levier.
    const t = atPedal(1);
    expect(t.lift).toBeCloseTo((t.finger - t.flex) / single.leverRatio, 5);
    expect(t.finger - t.flex).toBeLessThan(t.finger);
  });

  test('dans l\'ordre : la garde se ferme, les segments se détendent, puis le jeu s\'ouvre', () => {
    let touched = -1;
    let released = -1;
    let open = -1;
    let previous = emptyTravel();
    for (let i = 0; i <= 100; i++) {
      const t = atPedal(i / 100);
      if (t.contact && touched < 0) touched = i;
      if (t.clamp === 0 && released < 0) released = i;
      if (t.gap > 0 && open < 0) open = i;
      expect(t.bearing).toBeGreaterThanOrEqual(previous.bearing);
      expect(t.lift).toBeGreaterThanOrEqual(previous.lift);
      expect(t.clamp).toBeLessThanOrEqual(previous.clamp);
      // Tant que la butée ne touche pas, rien d'autre ne bouge.
      if (!t.contact) expect(t.lift).toBe(0);
      previous = { ...t };
    }
    expect(touched).toBeGreaterThan(0);
    expect(released).toBeGreaterThan(touched);
    expect(open).toBeGreaterThanOrEqual(released);
    expect(open).toBeLessThan(100);
  });

  test('la butée ne traverse jamais les doigts, et les rejoint exactement', () => {
    for (let i = 0; i <= 50; i++) {
      const t = atPedal(i / 50);
      const nose = layout.noseX + groupOffset('carrier', { ...emptyFrame(), travel: t });
      const back = diaphragmX(layout, t, t.lift * LIFT_EXAGGERATION, single.contactR) + layout.sheet / 2;
      if (t.contact) expect(nose - back).toBeCloseTo(0, 5);
      else expect(nose - back).toBeGreaterThan(0);
    }
  });

  test('sur le dessin, chaque pièce part du bon côté', () => {
    const frame = { ...emptyFrame(), travel: atPedal(1) };
    expect(groupOffset('carrier', frame)).toBeLessThan(0);
    expect(groupOffset('race', frame)).toBe(groupOffset('carrier', frame));
    expect(groupOffset('plate', frame)).toBeGreaterThan(0);
    expect(groupOffset('plate', frame)).toBeCloseTo(frame.travel.lift * LIFT_EXAGGERATION, 5);
    // Le disque flotte entre les deux : il ne frotte ni d'un côté ni de l'autre.
    expect(groupOffset('liningFront', frame)).toBeGreaterThan(0);
    expect(groupOffset('liningRear', frame)).toBeLessThan(groupOffset('plate', frame));
    // Le moyeu coulisse sur les cannelures : l'arbre, lui, reste dans ses paliers.
    expect(groupOffset('hub', frame)).toBeGreaterThan(0);
    expect(groupOffset('shaft', frame)).toBe(0);
    expect(groupAngle('shaft', { ...frame, hubAngle: 1.5 })).toBe(1.5);
    // Le volant et le couvercle ne reculent pas.
    expect(groupOffset('crank', frame)).toBe(0);
    expect(groupOffset('mass', frame)).toBe(0);
  });

  test('la pédale déduite d\'une part de couple redonne cette part, et situe le contact', () => {
    for (const share of [0.9, 0.7, 0.5, 0.3, 0.1, 0.04]) {
      const t = atPedal(pedalFromShare(single, share));
      expect(t.clamp).toBeCloseTo(share, 3);
      expect(t.contact).toBe(true);
      expect(bearingTouches(share)).toBe(true);
    }
    // Part entière : la butée est dégagée, comme le dit aussi la frontière commune.
    expect(atPedal(pedalFromShare(single, 1)).contact).toBe(false);
    expect(bearingTouches(1)).toBe(false);
    expect(pedalFromShare(single, 1)).toBe(0);
    // Part nulle : la pédale va au plancher, et le jeu s'ouvre.
    expect(pedalFromShare(single, 0)).toBeCloseTo(1, 5);
    expect(atPedal(pedalFromShare(single, 0)).gap).toBeGreaterThan(0.5);
    // Serrage : du tout au rien sur la seule course des segments.
    expect(clampShare(0)).toBe(1);
    expect(clampShare(CUSHION_TRAVEL)).toBe(0);
    expect(clampShare(CUSHION_TRAVEL / 2)).toBeGreaterThan(0);
  });
});

test.describe('la butée : ce pour quoi la pièce existe', () => {
  const run = (state: RaceState, contact: boolean, rpm: number, seconds: number, frozen = false) => {
    for (let t = 0; t < seconds; t += 1 / 60) advanceRace(state, contact, rpm, 1 / 60, SLOW_MOTION, frozen);
  };

  test('dégagée, sa bague est à l\'arrêt, quel que soit le régime', () => {
    const race: RaceState = { rpm: 3000, angle: 1, cageAngle: 1 };
    run(race, false, 3000, 20);
    expect(race.rpm).toBe(0);
    const angle = race.angle;
    run(race, false, 3000, 1);
    expect(race.angle).toBe(angle);
  });

  test('au contact, elle rejoint exactement le volant, sans y sauter d\'un coup', () => {
    const race: RaceState = { rpm: 0, angle: 0, cageAngle: 0 };
    advanceRace(race, true, 3000, 1 / 60, SLOW_MOTION, false);
    expect(race.rpm).toBeGreaterThan(0);
    expect(race.rpm).toBeLessThan(300);
    run(race, true, 3000, 12);
    expect(race.rpm).toBeCloseTo(3000, 0);
  });

  test('sa cage va un peu moins vite que la moitié : la cinématique d\'un roulement', () => {
    expect(CAGE_RATIO).toBeGreaterThan(0.4);
    expect(CAGE_RATIO).toBeLessThan(0.5);
    const race: RaceState = { rpm: 600, angle: 0, cageAngle: 0 };
    advanceRace(race, true, 600, 0.01, SLOW_MOTION, false);
    expect(race.cageAngle / race.angle).toBeCloseTo(CAGE_RATIO, 5);
  });

  test('l\'autre bague reste fixe : le chariot coulisse sans jamais tourner', () => {
    const frame = { ...emptyFrame(), travel: atPedal(1), raceAngle: 2, cageAngle: 0.8, crankAngle: 1 };
    expect(groupAngle('carrier', frame)).toBe(0);
    expect(groupAngle('race', frame)).toBe(2);
    expect(groupAngle('cage', frame)).toBe(0.8);
  });

  test('l\'horloge commune l\'arrête ou l\'emporte selon la télémétrie, et le gel la fige', () => {
    const motion = new Motion();
    motion.reduced = false;
    const config = { ...CONFIG };
    const telemetry = { rpm: 3000, clutch: 1, clutchInputRpm: 3000, gear: 1 } as unknown as Telemetry;
    let now = 1000;
    const step = (seconds: number) => {
      for (let t = 0; t < seconds; t += 1 / 30) {
        now += 1000 / 30;
        motion.update(now, telemetry, config);
      }
    };
    step(3);
    expect(motion.releaseRace.rpm).toBe(0);
    telemetry.clutch = 0.4;
    step(8);
    expect(motion.releaseRace.rpm).toBeCloseTo(3000, -1);
    const angle = motion.releaseRace.angle;
    step(0.2);
    expect(motion.releaseRace.angle).not.toBe(angle);
    motion.reduced = true;
    const frozen = motion.releaseRace.angle;
    step(0.5);
    expect(motion.releaseRace.angle).toBe(frozen);
  });
});

test.describe('disque, ressorts, bi-masse', () => {
  const clock = (over: Partial<ClutchClock>): ClutchClock => ({ clutchShare: 1, clutchTorque: 0, flywheelAngle: 0, discAngle: 0, clutchK1: 0, clutchK2: 0, boxInputTotal: 0, releaseRace: { rpm: 0, angle: 0, cageAngle: 0 }, ...over });

  test('le disque tourne avec le volant une fois serré, et librement une fois ouvert', () => {
    const motion = new Motion();
    motion.reduced = false;
    const config = { ...CONFIG };
    const telemetry = { rpm: 2400, clutch: 1, clutchInputRpm: 2400, gear: 2 } as unknown as Telemetry;
    let now = 1000;
    const advance = (frames: number) => {
      for (let i = 0; i < frames; i++) {
        now += 1000 / 60;
        motion.update(now, telemetry, config);
      }
    };
    const unwrap = (angle: number) => (angle + Math.PI * 2) % (Math.PI * 2);
    advance(60);
    const offset = unwrap(motion.flywheelAngle - motion.discAngle);
    advance(40);
    expect(unwrap(motion.flywheelAngle - motion.discAngle)).toBeCloseTo(offset, 3);
    // Pédale au plancher : le disque ralentit jusqu'à l'arrêt, le volant continue.
    telemetry.clutch = 0;
    telemetry.clutchInputRpm = 0;
    advance(60);
    const disc = motion.discAngle;
    const flywheel = motion.flywheelAngle;
    advance(10);
    expect(motion.discAngle).toBeCloseTo(disc, 3);
    expect(motion.flywheelAngle).not.toBeCloseTo(flywheel, 3);
  });

  test('les ressorts du disque suivent le couple, dans son sens, jusqu\'à leur butée', () => {
    expect(damperWindup(0, 200)).toBe(0);
    expect(damperWindup(100, 200)).toBeGreaterThan(0);
    expect(damperWindup(200, 200)).toBeCloseTo(2 * damperWindup(100, 200), 5);
    expect(damperWindup(-100, 200)).toBeCloseTo(-damperWindup(100, 200), 5);
    expect(damperWindup(1e4, 200)).toBeCloseTo(DAMPER_LIMIT, 5);
    const frame = readFrame(single, 200, clock({ clutchTorque: 150, discAngle: 0.5 }), 0, 1, emptyFrame());
    expect(frame.damperWindup).toBeCloseTo(damperWindup(150, 200), 5);
    expect(frame.liningAngle).toBeCloseTo(0.5 + frame.damperWindup, 5);
    expect(frame.hubAngle).toBe(0.5);
  });

  test('sur un bi-masse, le disque est rigide et ce sont les ressorts en arc qui travaillent', () => {
    const dual = clutchDimensions({ ...SIZING, boxFlywheel: 'dual_mass' });
    expect(dual.dampers).toBe(0);
    expect(dual.dualMass).toBe(true);
    const frame = readFrame(dual, 200, clock({ clutchTorque: 150, flywheelAngle: 1, discAngle: 0.5 }), 0, 1, emptyFrame());
    expect(frame.damperWindup).toBe(0);
    expect(frame.liningAngle).toBe(0.5);
    expect(frame.dualMassWindup).toBeCloseTo(dualMassWindup(150, 200), 5);
    expect(frame.dualMassWindup).toBeGreaterThan(damperWindup(150, 200));
    // La masse primaire devance la secondaire : c'est elle qui entraîne.
    expect(frame.crankAngle).toBeGreaterThan(frame.massAngle);
    expect(dualMassWindup(1e4, 200)).toBeCloseTo(DUAL_MASS_LIMIT, 5);
    // Le plateau et le couvercle tournent avec la secondaire, pas avec le vilebrequin.
    expect(groupAngle('plate', frame)).toBe(frame.massAngle);
    expect(groupAngle('crank', frame)).toBe(frame.crankAngle);
  });

  test('sur un volant simple, ressorts du disque, pas de bi-masse', () => {
    const frame = readFrame(single, 200, clock({ clutchTorque: 150, flywheelAngle: 1 }), 0, 1, emptyFrame());
    expect(single.dampers).toBeGreaterThan(3);
    expect(frame.dualMassWindup).toBe(0);
    expect(frame.massAngle).toBe(frame.crankAngle);
  });

  test('le disque est dimensionné par le couple : 200 mm pour 150 Nm, davantage au-dessus', () => {
    expect(discDiameterFor(150)).toBeGreaterThan(190);
    expect(discDiameterFor(150)).toBeLessThan(215);
    expect(discDiameterFor(250)).toBeGreaterThan(225);
    expect(discDiameterFor(250)).toBeLessThan(250);
    for (const torque of [60, 120, 200, 300, 450]) {
      expect(discDiameterFor(torque + 20)).toBeGreaterThanOrEqual(discDiameterFor(torque));
      const dims = clutchDimensions({ ...SIZING, peakTorque: torque });
      // Le serrage et le rayon tiennent ce que le C++ demande : 1,6 fois le couple, sur deux faces.
      expect((2 * 0.27 * dims.clampLoad * dims.meanFrictionR) / 1000).toBeCloseTo(1.6 * torque, 0);
      expect(dims.flywheelR).toBeGreaterThan(dims.liningOuterR);
      expect(dims.pivotR).toBeLessThan(dims.fulcrumR);
      expect(dims.contactR).toBeLessThan(dims.pivotR);
    }
  });

  test('le volant dépend du moteur : un trois-cylindres est plus épais qu\'un huit', () => {
    const three = clutchDimensions({ ...SIZING, cylinders: 3, displacementCc: 1500 });
    const eight = clutchDimensions({ ...SIZING, cylinders: 8, displacementCc: 4000 });
    expect(three.flywheelThickness).toBeGreaterThan(eight.flywheelThickness);
    expect(clutchDimensions({ ...SIZING, peakTorque: 450, cylinders: 8 }).crankBolts).toBeGreaterThan(single.crankBolts);
  });

  test('double embrayage : normalement ouvert, un seul serre selon la parité du rapport', () => {
    const dims = clutchDimensions({ ...SIZING, boxKind: 'dual_clutch' });
    expect(dims.kind).toBe('dual_clutch');
    expect(dims.dualMass).toBe(true);
    expect(activeClutch(1)).toBe(1);
    expect(activeClutch(2)).toBe(2);
    expect(activeClutch(3)).toBe(1);
    expect(activeClutch(-1)).toBe(2);
    expect(activeClutch(0)).toBe(0);
    // Serrer, c'est pousser : au repos, plus rien ne pousse.
    expect(dctBearingTravel(0)).toBe(0);
    expect(dctBearingTravel(1)).toBeGreaterThan(dctBearingTravel(0.5));
    const open = readFrame(dims, 200, clock({ clutchK1: 0, clutchK2: 1 }), 2, 1, emptyFrame());
    expect(open.k1Open).toBe(1);
    expect(open.k2Open).toBe(0);
    expect(groupOffset('k2Plate', open)).toBe(0);
    expect(groupOffset('k1Plate', open)).toBeLessThan(0);
    const l = clutchLayout(dims);
    expect(l.dct).not.toBeNull();
    const parts = sectionParts(l);
    for (const id of ['liningFrontK1', 'liningFrontK2', 'k1Plate', 'k2Plate', 'centre']) expect(parts.some((part) => part.id === id)).toBe(true);
  });
});

test.describe('double embrayage sur l\'horloge commune', () => {
  test('le couple passe de K1 à K2 quand le rapport change, et les butées restent en appui', () => {
    const motion = new Motion();
    motion.reduced = false;
    const config = { ...CONFIG, boxKind: 'dual_clutch' } as EngineConfig;
    const telemetry = { rpm: 3000, clutch: 1, clutchInputRpm: 3000, gear: 1 } as unknown as Telemetry;
    let now = 1000;
    const step = (seconds: number) => {
      for (let t = 0; t < seconds; t += 1 / 60) {
        now += 1000 / 60;
        motion.update(now, telemetry, config);
      }
    };
    step(1);
    expect(motion.clutchK1).toBeGreaterThan(0.95);
    expect(motion.clutchK2).toBeLessThan(0.05);
    telemetry.gear = 2;
    step(0.03);
    // Le relais : pendant un instant, les deux portent une part du couple.
    expect(motion.clutchK1).toBeGreaterThan(0.2);
    expect(motion.clutchK2).toBeGreaterThan(0.2);
    step(1);
    expect(motion.clutchK1).toBeLessThan(0.05);
    expect(motion.clutchK2).toBeGreaterThan(0.95);
    // Part entière, et pourtant la bague tourne : sur un double embrayage, serrer c'est pousser.
    step(10);
    expect(motion.releaseRace.rpm).toBeCloseTo(3000, -1);
  });
});

test.describe('les profils partagés', () => {
  for (const [name, sizing] of Object.entries({ simple: SIZING, 'bi-masse': { ...SIZING, boxFlywheel: 'dual_mass' as const }, double: { ...SIZING, boxKind: 'dual_clutch' } })) {
    test(`${name} : toutes les pièces sont des contours fermés, finis et d'un seul tenant`, () => {
      const l = clutchLayout(clutchDimensions(sizing));
      const parts = sectionParts(l);
      expect(parts.length).toBeGreaterThan(15);
      for (const part of parts) {
        expect(part.points.length % 2).toBe(0);
        let area = 0;
        const n = part.points.length / 2;
        for (let i = 0; i < n; i++) {
          const j = (i + 1) % n;
          expect(Number.isFinite(part.points[i * 2])).toBe(true);
          expect(part.points[i * 2 + 1]).toBeGreaterThanOrEqual(0);
          area += part.points[i * 2] * part.points[j * 2 + 1] - part.points[j * 2] * part.points[i * 2 + 1];
        }
        expect(Math.abs(area), part.id).toBeGreaterThan(0.1);
      }
    });
  }

  test('un profil tourné est un solide fermé, faces vers l\'extérieur', () => {
    // Le théorème de la divergence : la somme des tétraèdres donne le volume, positif si toutes les faces regardent dehors.
    const volumeOf = (points: number[]) => {
      const mesh = revolve(points, 0, Math.PI * 2, 128);
      let sum = 0;
      for (let i = 0; i < mesh.indices.length; i += 3) {
        const [a, b, c] = [mesh.indices[i] * 3, mesh.indices[i + 1] * 3, mesh.indices[i + 2] * 3];
        const p = mesh.positions;
        sum += (p[a] * (p[b + 1] * p[c + 2] - p[b + 2] * p[c + 1]) - p[a + 1] * (p[b] * p[c + 2] - p[b + 2] * p[c]) + p[a + 2] * (p[b] * p[c + 1] - p[b + 1] * p[c])) / 6;
      }
      return sum;
    };
    const expected = Math.PI * (20 * 20 - 10 * 10) * 5;
    const ring = [0, 10, 0, 20, 5, 20, 5, 10];
    expect(volumeOf(ring)).toBeCloseTo(expected, -1);
    // L'autre sens de parcours donne le même solide : c'est `revolve` qui oriente.
    expect(volumeOf([...ring].reverse().flatMap((_, i, list) => (i % 2 === 0 ? [list[i + 1], list[i]] : [])))).toBeCloseTo(expected, -1);
  });
});

/* eslint-disable @typescript-eslint/no-explicit-any */
const painted = (page: Page) => page.getByTestId('clutch-view').locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
  const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
  let count = 0;
  for (let i = 3; i < data.length; i += 4) if (data[i] > 0) count++;
  return count;
});

/** Le mode affiché par la vue : 3d seulement si la scène dessine vraiment. */
const shownMode = (page: Page) => page.getByTestId('clutch-view').getAttribute('data-mode');

async function open(page: Page, hash = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  // Chaque moteur de ce fichier se charge par son lien ; un `goto` sur la même page sans cela ne ferait que changer l'ancre.
  await page.goto('about:blank');
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await voir(page, 'clutch');
  await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
  return errors;
}

const webgl2Works = (page: Page) => page.evaluate(() => !!document.createElement('canvas').getContext('webgl2'));

async function start(page: Page) {
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm)).toBeGreaterThan(500);
}

test.describe('la vue sur la page', () => {
  test('la 3D par défaut si WebGL2 répond, et le basculement en 2D garde l\'état et les chiffres', async ({ page }) => {
    const errors = await open(page);
    test.skip(!(await webgl2Works(page)), 'pas de WebGL2 sur ce navigateur');
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    await expect(page.getByTestId('clutch-mode-3d')).toHaveAttribute('aria-pressed', 'true');
    await expect(page.getByTestId('clutch-reset')).toBeVisible();
    await page.getByTestId('clutch-mode-2d').click();
    await expect.poll(() => shownMode(page)).toBe('2d');
    await expect(page.getByTestId('clutch-mode-2d')).toHaveAttribute('aria-pressed', 'true');
    await expect(page.getByTestId('clutch-reset')).toHaveCount(0);
    expect(await painted(page)).toBeGreaterThan(20000);
    await expect(page.getByTestId('clutch-state')).toBeVisible();
    await page.getByTestId('clutch-mode-3d').click();
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    expect(errors).toEqual([]);
  });

  test('la 3D dessine vraiment l\'embrayage, sous le calque des étiquettes', async ({ page }) => {
    await open(page);
    test.skip(!(await webgl2Works(page)), 'pas de WebGL2 sur ce navigateur');
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    // La scène se construit à la première image, une fois la vue à l'écran.
    await expect.poll(() => page.evaluate(() => (window as any).__clutchView.triangles()), { timeout: 20000 }).toBeGreaterThan(5000);
    const triangles = await page.evaluate(() => (window as any).__clutchView.triangles());
    expect(triangles).toBeLessThan(120000);
    await page.waitForTimeout(800);
    await page.locator('[data-testid=clutch-view] canvas.flat').evaluate((c: HTMLCanvasElement) => (c.style.visibility = 'hidden'));
    const shot = await page.getByTestId('clutch-view').screenshot({ timeout: 60000 });
    const result = await page.evaluate(async (base64) => {
      const image = new Image();
      image.src = `data:image/png;base64,${base64}`;
      await image.decode();
      const c = document.createElement('canvas');
      c.width = image.width;
      c.height = image.height;
      const g = c.getContext('2d')!;
      g.drawImage(image, 0, 0);
      const { data } = g.getImageData(0, 0, c.width, c.height);
      const colors = new Set<number>();
      let warm = 0;
      let lit = 0;
      for (let i = 0; i < data.length; i += 4) {
        const r = data[i], g2 = data[i + 1], b = data[i + 2];
        colors.add((r >> 3) << 10 | (g2 >> 3) << 5 | (b >> 3));
        if (Math.max(r, g2, b) > 60) lit++;
        // Une garniture : le brun du disque.
        if (r > b + 25 && r > 70) warm++;
      }
      return { colors: colors.size, warm, lit };
    }, shot.toString('base64'));
    expect(result.colors).toBeGreaterThan(40);
    expect(result.lit).toBeGreaterThan(3000);
    expect(result.warm).toBeGreaterThan(20);
  });

  test('une image coûte peu au fil principal', async ({ page }) => {
    await open(page);
    await start(page);
    await voir(page, 'clutch');
    await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
    await page.waitForTimeout(3000);
    // Le budget est de 2 ms sur une machine calme ; la marge couvre un rendu logiciel et une machine chargée.
    expect(await page.evaluate(() => (window as any).__revforge.motion.clutchCostMs)).toBeLessThan(12);
  });

  test('sans WebGL2 : la coupe en 2D, le bouton 3D désactivé, et aucune erreur', async ({ page }) => {
    await page.addInitScript(() => {
      const original = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (this: HTMLCanvasElement, kind: string, ...rest: unknown[]) {
        if (kind === 'webgl2' || kind === 'webgl') return null;
        return (original as any).call(this, kind, ...rest);
      } as typeof original;
    });
    const errors = await open(page);
    await expect(page.getByTestId('clutch-mode-3d')).toBeDisabled();
    await expect(page.getByTestId('clutch-mode-2d')).toHaveAttribute('aria-pressed', 'true');
    await expect(page.getByTestId('clutch-no-3d')).toBeVisible();
    expect(await shownMode(page)).toBe('2d');
    await expect.poll(() => painted(page)).toBeGreaterThan(20000);
    await expect(page.getByTestId('clutch-reset')).toHaveCount(0);
    expect(errors).toEqual([]);
  });

  test('perte du contexte : la coupe 2D prend le relais, et la 3D revient', async ({ page }) => {
    const errors = await open(page);
    test.skip(!(await webgl2Works(page)), 'pas de WebGL2 sur ce navigateur');
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    const before = await painted(page);
    await page.evaluate(() => {
      const gl = (document.querySelector('[data-testid=clutch-view] canvas.gl') as HTMLCanvasElement).getContext('webgl2')!;
      (window as any).__lose = gl.getExtension('WEBGL_lose_context');
      (window as any).__lose.loseContext();
    });
    await expect.poll(() => shownMode(page)).toBe('2d');
    // Le canvas 2D dessine maintenant toute la coupe, bien plus que la seule loupe de la 3D.
    await expect.poll(() => painted(page)).toBeGreaterThan(Math.max(before, 20000));
    await page.evaluate(() => (window as any).__lose.restoreContext());
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    expect(errors.filter((e) => !/context|WebGL/i.test(e))).toEqual([]);
  });

  test('on tourne la vue à la souris et au clavier, on la recentre, et les flèches ne passent pas de rapport', async ({ page }) => {
    await open(page);
    test.skip(!(await webgl2Works(page)), 'pas de WebGL2 sur ce navigateur');
    await expect.poll(() => shownMode(page), { timeout: 20000 }).toBe('3d');
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await voir(page, 'clutch');
    await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
    const gl = page.locator('[data-testid=clutch-view] canvas.gl');
    const camera = () => page.evaluate(() => (window as any).__clutchView.cameraKey());
    const home = await camera();
    await gl.focus();
    await page.keyboard.press('ArrowRight');
    await expect.poll(camera).not.toBe(home);
    await page.waitForTimeout(200);
    expect(await page.evaluate(() => (window as any).__revforge.telemetry().gear)).toBe(0);
    await page.getByTestId('clutch-reset').click();
    await expect.poll(camera).toBe(home);
    await page.getByTestId('clutch-focus').click();
    await expect(page.getByTestId('clutch-focus')).toHaveAttribute('aria-pressed', 'true');
    await expect.poll(camera).not.toBe(home);
    await page.getByTestId('clutch-reset').click();
    await expect.poll(camera).toBe(home);
    const box = (await gl.boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 90, box.y + box.height / 2 + 30, { steps: 4 });
    await page.mouse.up();
    await expect.poll(camera).not.toBe(home);
    await page.getByTestId('clutch-reset').click();
    await expect.poll(camera).toBe(home);
    // Au doigt, la verticale reste à la page.
    expect(await gl.evaluate((c) => getComputedStyle(c).touchAction)).toContain('pan-y');
  });

  test('pédale au plancher, la butée touche et sa bague suit le volant ; pédale levée, elle se dégage et s\'arrête', async ({ page }) => {
    await open(page);
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await voir(page, 'clutch');
    await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
    const state = () => page.evaluate(() => {
      const r = (window as any).__revforge;
      return { race: r.motion.releaseRace.rpm, rpm: r.motion.rpm, clutch: r.telemetry().clutch };
    });
    await expect.poll(() => state().then((s) => s.clutch), { timeout: 10000 }).toBeGreaterThan(0.99);
    await expect.poll(() => state().then((s) => s.race), { timeout: 25000 }).toBe(0);
    await expect(page.getByTestId('clutch-bearing')).toHaveText(/Dégagée/);
    await expect(page.getByTestId('clutch-lift')).toHaveText('0,0 mm');
    await page.evaluate(() => (window as any).__revforge.engine.setControls(0, 0, 1));
    await expect(page.getByTestId('clutch-bearing')).toHaveText(/contact/i);
    await expect(page.getByTestId('clutch-lift')).toHaveText(/^[12],\d mm$/);
    await expect.poll(async () => {
      const s = await state();
      return Math.abs(s.race - s.rpm);
    }, { timeout: 25000 }).toBeLessThan(60);
    await page.evaluate(() => (window as any).__revforge.engine.setControls(0, 0, 0));
    await expect.poll(() => state().then((s) => s.race), { timeout: 25000 }).toBe(0);
  });

  test('animations coupées : plus rien ne bouge, mais tout est dessiné, et un clic relance', async ({ page }) => {
    await open(page);
    await start(page);
    await page.getByTestId('clutch-mode-2d').click();
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await page.evaluate(() => (window as any).__revforge.engine.setControls(0, 0, 1));
    await voir(page, 'clutch');
    await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.motion.releaseRace.rpm), { timeout: 25000 }).toBeGreaterThan(200);
    const anglesAt = () => page.evaluate(() => {
      const m = (window as any).__revforge.motion;
      return [m.flywheelAngle, m.discAngle, m.releaseRace.angle, m.releaseRace.cageAngle].join();
    });
    const a = await anglesAt();
    await expect.poll(anglesAt).not.toBe(a);
    // La butée touche dès le début de la course : la pédale, elle, finit encore la sienne. Ce mouvement-là vient de la commande, pas de l'animation.
    await page.waitForTimeout(800);
    await ouvrir(page, 'display');
    await page.getByTestId('animations').click();
    await page.waitForTimeout(150);
    // La coupe seulement : le chiffre de la loupe, lui, continue de dire le régime.
    const pixels = () => page.getByTestId('clutch-view').locator('canvas').first().evaluate((c: HTMLCanvasElement) => {
      const { data } = c.getContext('2d')!.getImageData(0, 0, Math.floor(c.width * 0.6), c.height);
      let sum = 0;
      for (let i = 0; i < data.length; i += 41) sum = (sum * 31 + data[i]) | 0;
      return sum;
    });
    const frozen = await anglesAt();
    const first = await pixels();
    await page.waitForTimeout(400);
    expect(await anglesAt()).toBe(frozen);
    expect(await pixels()).toBe(first);
    expect(await painted(page)).toBeGreaterThan(20000);
    await page.getByTestId('animations').click();
    await expect.poll(anglesAt).not.toBe(frozen);
  });

  test('bi-masse et double embrayage se dessinent, en 2D comme en 3D, sans erreur', async ({ page }) => {
    for (const dual of [false, true]) {
      const errors = await open(page, boxLink(dual ? 'dual_clutch' : 'synchro', 'dual_mass'));
      for (const mode of ['2d', '3d']) {
        const button = page.getByTestId(`clutch-mode-${mode}`);
        if (await button.isDisabled()) continue;
        await button.click();
        await page.waitForTimeout(600);
        expect(await painted(page)).toBeGreaterThan(mode === '2d' ? 20000 : 2000);
      }
      if (dual) await expect(page.getByText(/Double embrayage sec/)).toBeVisible();
      expect(errors).toEqual([]);
    }
  });

  test('convertisseur et électrique : un message honnête, pas de disque inventé ni de boutons', async ({ page }) => {
    for (const hash of [boxLink('converter', 'single'), `#m=electric~${CAR}`]) {
      await open(page, hash);
      await expect(page.getByTestId('clutch-mode-3d')).toHaveCount(0);
      await expect(page.getByTestId('clutch-state')).toHaveCount(0);
      expect(await shownMode(page)).toBe('2d');
      await expect.poll(() => painted(page)).toBeGreaterThan(50);
      expect(await painted(page)).toBeLessThan(20000);
    }
  });

  test('à la largeur d\'un téléphone, rien ne déborde et les noms des pièces attendent qu\'on les demande', async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 760 });
    await open(page);
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(overflow).toBeLessThanOrEqual(0);
    await expect(page.getByTestId('clutch-labels')).toHaveAttribute('aria-pressed', 'false');
    await page.getByTestId('clutch-labels').click();
    await expect(page.getByTestId('clutch-labels')).toHaveAttribute('aria-pressed', 'true');
    await expect.poll(() => painted(page)).toBeGreaterThan(500);
  });

  test('tout se traduit, canvas compris : pas de clé brute en anglais', async ({ page }) => {
    await page.goto('./?test&lang=en');
    await voir(page, 'clutch');
    await page.getByTestId('clutch-view').scrollIntoViewIfNeeded();
    await expect(page.getByTestId('clutch-labels')).toHaveText('Part names');
    await expect(page.getByTestId('clutch-bearing')).toHaveText(/Clear|Touching/);
  });
});
