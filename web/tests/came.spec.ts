import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import { camLiftShare, camProfileOf, camShift, highCamProfileOf, makeCamProfile, stockCamProfile } from '../src/lib/cam';
import { engineLayout, EnginePose, valveLift } from '../src/lib/engineLayout';
import { EVC, EVO, GasNetwork, IVC, IVO, type GasInputs } from '../src/lib/fluidGas';
import { ouvrir } from './aide';

// La came, sans navigateur : son calage, recopié de cam.cpp, et ce qu'en font le dessin et le gaz à l'écran.

const base = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 86, redlineRpm: 7200, exhaust: 'sport', aspiration: 'natural', fuel: 'petrol', body: 'compact',
  boostBar: 1, turboRelease: 'dump_valve', turboSize: 'medium', turboPipeMm: 400, camshaft: 'dohc', fuelSystem: 'injection', airFilter: 'stock', placement: 'front',
  superchargerKind: 'roots', antiLag: 'off', boxFlywheel: 'single', cycle: 'four_stroke', camDurationDeg: 0, camLiftMm: 0, lobeSeparationDeg: 0, valveTiming: 'fixed',
} as const;
type Cam = { camDurationDeg?: number; camLiftMm?: number; lobeSeparationDeg?: number; valveTiming?: 'fixed' | 'phased' | 'two_step' | 'phased_two_step' };
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const make = (cam: Cam = {}): any => ({ ...base, ...cam });

/** Le C++ compilé en WebAssembly, chargé dans Node comme le fait `check_wasm.mjs`. */
function wasm() {
  const module = new WebAssembly.Module(readFileSync(new URL('../public/revforge.wasm', import.meta.url)));
  const { exports } = new WebAssembly.Instance(module, {});
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const rf = exports as any;
  rf._initialize?.();
  rf.rf_init(44100);
  return rf;
}

const VVT = { fixed: 0, phased: 1, two_step: 2, phased_two_step: 3 };

/** Le calage du C++ pour cette came (`rf_cam_spec`), à 1 mm : ouverture et fermeture de l'admission, puis de l'échappement. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
function native(rf: any, cam: Cam) {
  // Les 26 réglages du socle, dans l'ordre de `rf_set_tuning` : la came est aux places 14 à 17.
  const tuning = [20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, cam.camDurationDeg ?? 0, cam.camLiftMm ?? 0, cam.lobeSeparationDeg ?? 0, VVT[cam.valveTiming ?? 'fixed'], 0, 0, 0, 0, 0, 0, 0, 0];
  rf.rf_set_tuning(...tuning);
  rf.rf_set_config(4, 0, 2000, 86, 7200, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 400, 0, 0, 0);
  const at = (k: number) => rf.rf_cam_spec(k);
  return { ivo: at(0), ivc: at(1), evo: at(2), evc: at(3), lift: at(4), high: { ivo: at(13), ivc: at(14), evo: at(15), evc: at(16) }, switchRpm: at(8) };
}

test.describe('la came, sans navigateur', () => {
  test('au profil d\'origine, le gaz, le dessin et le son gardent exactement leur calage', () => {
    const stock = stockCamProfile(make());
    expect([stock.evo, stock.evc, stock.ivo, stock.ivc]).toEqual([130, 370, 350, 590]);
    // Ce sont les kEvo… d'engine_sound.cpp, que le son garde tels quels.
    const sound = readFileSync(new URL('../../core/src/engine_sound.cpp', import.meta.url), 'utf-8');
    const constant = (name: string) => Number(new RegExp(`constexpr float ${name} = ([\\d.]+)f / 720`).exec(sound)![1]);
    expect([constant('kEvo'), constant('kEvc'), constant('kIvo'), constant('kIvc')]).toEqual([130, 370, 350, 590]);
    const shift = camShift(make());
    expect([shift.evo, shift.evc, shift.ivo, shift.ivc, shift.lift]).toEqual([0, 0, 0, 0, 1]);
    const layout = engineLayout(make());
    expect(layout.timing).toEqual({ evo: EVO, evc: EVC, ivo: IVO, ivc: IVC });
  });

  test('le calage du site est celui du C++, profil par profil', () => {
    const rf = wasm();
    const cams: Cam[] = [{}, { camDurationDeg: 290, camLiftMm: 12.5, lobeSeparationDeg: 106 }, { camDurationDeg: 200, camLiftMm: 7 }, { lobeSeparationDeg: 118 },
      { valveTiming: 'two_step' }, { camDurationDeg: 300, camLiftMm: 5, valveTiming: 'phased_two_step' }];
    for (const cam of cams) {
      const c = native(rf, cam);
      const low = camProfileOf(make(cam));
      const high = highCamProfileOf(make(cam));
      // En float d'un côté, en double de l'autre : au millième de degré.
      expect(low.ivo).toBeCloseTo(c.ivo, 2);
      expect(low.ivc).toBeCloseTo(c.ivc, 2);
      expect(low.evo).toBeCloseTo(c.evo, 2);
      expect(low.evc).toBeCloseTo(c.evc, 2);
      expect(low.liftMm).toBeCloseTo(c.lift, 3);
      expect(high.ivo).toBeCloseTo(c.high.ivo, 2);
      expect(high.evc).toBeCloseTo(c.high.evc, 2);
    }
    // Deux profils : le C++ calcule le régime de bascule, là où les courbes se croisent.
    expect(native(rf, { valveTiming: 'two_step' }).switchRpm).toBeGreaterThan(3000);
    expect(native(rf, { valveTiming: 'two_step' }).switchRpm).toBeLessThan(7200);
  });

  test('la loi de levée : 1 mm aux points du calage, la levée au nez', () => {
    const cam = makeCamProfile(270, 11, 108, 108);
    const lift = (deg: number, intake: boolean) => cam.liftMm * camLiftShare((deg - (intake ? 360 + cam.intakeCenterDeg : 360 - cam.exhaustCenterDeg)) / cam.halfSpanDeg);
    expect(lift(cam.ivo, true)).toBeCloseTo(1, 6);
    expect(lift(cam.ivc, true)).toBeCloseTo(1, 6);
    expect(lift(cam.evo, false)).toBeCloseTo(1, 6);
    expect(lift(360 + 108, true)).toBeCloseTo(11, 6);
    expect(cam.ivc - cam.ivo).toBeCloseTo(270, 6);
  });

  test('le gaz et le dessin suivent la came : plus longue, plus haute, plus de croisement', () => {
    const race = make({ camDurationDeg: 300, camLiftMm: 13, lobeSeparationDeg: 104 });
    const layout = engineLayout(race);
    const stock = engineLayout(make());
    // 60° de plus, 6° plus serrée : l'admission ouvre 36° plus tôt et ferme 24° plus tard.
    expect(layout.timing.ivo).toBeCloseTo(IVO - 36, 6);
    expect(layout.timing.ivc).toBeCloseTo(IVC + 24, 6);
    expect(layout.timing.evo).toBeCloseTo(EVO - 24, 6);
    expect(layout.timing.evc).toBeCloseTo(EVC + 36, 6);
    // Le bossage : plus haut de ce que la levée gagne.
    expect(layout.valve.lift / stock.valve.lift).toBeCloseTo(13 / (0.12 * 86), 6);
    expect(layout.key).not.toBe(stock.key);
    // La pose lit ce calage : la soupape d'admission est ouverte au nouveau point d'ouverture, fermée juste avant.
    const pose = new EnginePose(layout);
    const cylinder = layout.cylinders[0];
    pose.solve(cylinder.firingDeg + layout.timing.ivo + 5);
    expect(pose.liftIn[cylinder.index]).toBeGreaterThan(0);
    pose.solve(cylinder.firingDeg + layout.timing.ivo - 5);
    expect(pose.liftIn[cylinder.index]).toBe(0);
    expect(valveLift(layout.timing.ivo + 1, layout.timing.ivo, layout.timing.ivc)).toBeGreaterThan(0);
    // Le gaz à l'écran : les soupapes s'ouvrent au même calage que le dessin.
    const net = new GasNetwork();
    net.build(race, layout.order);
    const inputs: GasInputs = { rpm: 3000, load: 0.6, boost: 0, release: 0, burning: true, heat: 0.5 };
    const seconds = 2 / (3000 / 60) / 400;
    const turns = (3000 / 120) * seconds;
    let cycle = 0;
    let open = 0;
    let shut = 0;
    for (let step = 0; step < 800; step++) {
      net.advance(seconds, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
      for (const c of layout.cylinders) {
        const theta = net.cylTheta[c.index];
        if (theta > layout.timing.ivo + 1 && theta < layout.timing.ivc - 1) {
          expect(net.cylLiftIn[c.index]).toBeGreaterThan(0);
          open++;
        } else if (theta < layout.timing.ivo - 1 || theta > layout.timing.ivc + 1) {
          expect(net.cylLiftIn[c.index]).toBe(0);
          shut++;
        }
      }
    }
    expect(open).toBeGreaterThan(100);
    expect(shut).toBeGreaterThan(100);
  });
});

test.describe('la came, à l\'écran', () => {
  test('la section de distribution donne le calage et ce que la came calcule', async ({ page }) => {
    await page.goto('/');
    await ouvrir(page, 'engine');
    const section = page.getByTestId('panel-engine').getByTestId('section-valvetrain');
    await expect(section.getByTestId('cam-timing')).toContainText('10°');
    await expect(section.getByTestId('cam-timing')).toContainText('50°');
    await expect(section.getByTestId('cam-figures')).toContainText('20°');
    await expect(section.getByTestId('cam-switch')).toHaveCount(0);
    // Deux profils : le régime de bascule, calculé.
    await section.getByTestId('valve-timing').getByRole('button', { name: 'Deux profils', exact: true }).click();
    await expect(section.getByTestId('cam-switch')).toContainText('tr/min');
  });
});
