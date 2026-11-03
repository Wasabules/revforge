import { expect, test, type Page } from '@playwright/test';
import { layoutKey } from '../src/lib/engineLayout';
import { gasKey } from '../src/lib/fluid';
import { rotaryKey } from '../src/lib/rotary';
import type { EngineConfig } from '../src/lib/config';

/** Là où les réglages se rencontrent : voir « Quand les réglages se rencontrent » dans docs/modele.md. */
const BASE = '4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve~compact.1350.18.front.1';

async function open(page: Page, hash = BASE) {
  await page.goto('about:blank');
  await page.goto(`./?test#m=${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
}

test.describe('le lien de partage', () => {
  test('toutes les valeurs des nouveaux réglages font l’aller-retour, et le dernier champ reste à sa place', async ({ page }) => {
    await open(page);
    const failures = await page.evaluate(() => {
      const { encodeConfig, decodeConfig, config } = (window as any).__revforge;
      const start = config();
      const wrong: string[] = [];
      for (const superchargerKind of ['roots', 'twin_screw', 'centrifugal']) {
        for (const fuelSystem of ['injection', 'single', 'multi']) {
          for (const airFilter of ['stock', 'panel', 'cone', 'stacks']) {
            for (const antiLag of ['off', 'retard', 'air', 'cut']) {
              for (const camshaft of ['dohc', 'sohc']) {
                for (const turboPipeMm of [150, 400, 3000]) {
                  for (const aspiration of ['natural', 'turbo', 'supercharger']) {
                    for (const boostBar of [0.7, 1.3]) {
                      const next = { ...start, superchargerKind, fuelSystem, airFilter, antiLag, camshaft, turboPipeMm, aspiration, boostBar };
                      const back = decodeConfig(encodeConfig(next));
                      for (const key of ['superchargerKind', 'fuelSystem', 'airFilter', 'antiLag', 'camshaft', 'turboPipeMm', 'aspiration', 'boostBar', 'turboSize', 'routing', 'insulation', 'placement']) {
                        if (back?.[key] !== next[key]) wrong.push(`${encodeConfig(next)} : ${key} ${back?.[key]} au lieu de ${next[key]}`);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      return wrong;
    });
    expect(failures.slice(0, 5)).toEqual([]);
  });

  test('les anciens liens se lisent : sans ces champs, ou avec ceux du socle seuls', async ({ page }) => {
    await open(page);
    const read = (hash: string) => page.evaluate((text) => (window as any).__revforge.decodeConfig(text), hash);

    // D'avant la voiture.
    const bare = await read('4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve');
    expect(bare.aspiration).toBe('turbo');
    expect(bare.fuelSystem).toBe('injection');
    expect(bare.superchargerKind).toBe('roots');
    expect(bare.airFilter).toBe('stock');

    // D'avant les réglages : voiture, emplacement, isolation, sorties, taille du turbo.
    const older = await read('4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~coupe.1400.18.all.1.3_mid_light_dual_large');
    expect(older.placement).toBe('mid');
    expect(older.turboSize).toBe('large');
    expect(older.superchargerKind).toBe('roots');
    expect(older.antiLag).toBe('off');
    expect(older.turboPipeMm).toBe(400);
    expect(older.camshaft).toBe('dohc');
    expect(older.airFilter).toBe('stock');
    expect(older.fuelSystem).toBe('injection');

    // Les champs du socle seuls : pas de carburateur à la fin, et `twin_screw` ne décale rien.
    const socle = await read('4.inline.2000.84.6800.stock.supercharger.petrol.0.off.dump_valve~compact.1350.18.front.0.8_front_standard_single_medium_twin_screw_retard_2500_sohc_cone');
    expect(socle.superchargerKind).toBe('twin_screw');
    expect(socle.antiLag).toBe('retard');
    expect(socle.turboPipeMm).toBe(2500);
    expect(socle.camshaft).toBe('sohc');
    expect(socle.airFilter).toBe('cone');
    expect(socle.fuelSystem).toBe('injection');
    expect(socle.boostBar).toBe(0.8);

    // Et avec lui, le dernier champ : c'est lui que `twin_screw` décalait.
    const full = await read('4.inline.2000.84.6800.stock.supercharger.petrol.0.off.dump_valve~compact.1350.18.front.0.8_front_standard_single_medium_twin_screw_off_400_dohc_stacks_multi');
    expect(full.airFilter).toBe('stacks');
    expect(full.fuelSystem).toBe('multi');

    // La boîte, après le second `~`, reste indépendante.
    const boxed = await read('4.inline.2000.84.6800.stock.supercharger.petrol.0.off.dump_valve~compact.1350.18.front.0.8_front_standard_single_medium_twin_screw_off_400_dohc_stock_single~dog,6,x,0,x,0,0,0,x,');
    expect(boxed.fuelSystem).toBe('single');
    expect(boxed.boxKind).toBe('dog');

    // Un rotatif reste cohérent, quoi que dise le lien : essence et nombre de rotors.
    const rotary = await read('8.rotary.1308.84.9000.stock.natural.diesel.0.light.dump_valve~coupe.1300.18.rear.1');
    expect(rotary.fuel).toBe('petrol');
    expect(rotary.cylinders).toBe(2);
  });
});

test.describe('les clés de reconstruction', () => {
  const engine: EngineConfig = {
    cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
    multiplier: 0, pops: 'off', turboRelease: 'dump_valve', turboSize: 'medium', body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
    placement: 'front', insulation: 'standard', routing: 'single', boostBar: 1, electric: false, boxFlywheel: 'solid',
    superchargerKind: 'roots', antiLag: 'off', turboPipeMm: 400, camshaft: 'dohc', airFilter: 'stock', fuelSystem: 'injection',
  } as EngineConfig;

  /** Tout ce qui change les pièces d'une vue ou le chemin du gaz. */
  const CHANGES: [string, Partial<EngineConfig>][] = [
    ['filtre', { airFilter: 'cone' }],
    ['cornets', { airFilter: 'stacks' }],
    ['carburateur', { fuelSystem: 'single' }],
    ['corps multiples', { fuelSystem: 'multi' }],
    ['arbre', { camshaft: 'sohc' }],
    ['compresseur', { aspiration: 'supercharger' }],
    ['type de compresseur', { superchargerKind: 'centrifugal' }],
    ['tube', { turboPipeMm: 3000 }],
    ['architecture', { layout: 'v' }],
  ];

  for (const [name, change] of CHANGES) {
    test(`${name} : la vue et le réseau de gaz se reconstruisent`, () => {
      const next = { ...engine, ...change };
      expect(layoutKey(next)).not.toBe(layoutKey(engine));
      expect(gasKey(next)).not.toBe(gasKey(engine));
    });
  }

  test('l’air sous le capot change avec l’emplacement du moteur : le gaz le suit', () => {
    expect(gasKey({ ...engine, placement: 'mid' })).not.toBe(gasKey(engine));
  });

  test('sur un rotatif aussi', () => {
    const rotary = { ...engine, layout: 'rotary', cylinders: 2, displacementCc: 1308 } as EngineConfig;
    for (const change of [{ airFilter: 'cone' }, { fuelSystem: 'multi' }, { aspiration: 'turbo' }, { aspiration: 'supercharger' }, { superchargerKind: 'centrifugal' }] as Partial<EngineConfig>[]) {
      expect(rotaryKey({ ...rotary, ...change })).not.toBe(rotaryKey(rotary));
      expect(gasKey({ ...rotary, ...change })).not.toBe(gasKey(rotary));
    }
  });

  test('à chaud : changer de filtre reconstruit le réseau que lisent les jauges', async ({ page }) => {
    await open(page);
    await page.getByTestId('view-gas').click();
    const inlet = () => page.evaluate(() => (window as any).__revforge.motion.gas.net.inletK);
    await expect.poll(inlet).toBeCloseTo(293, 0);
    await page.getByTestId('open-engine').click();
    await page.getByTestId('panel-engine').getByRole('button', { name: 'Cône' }).click();
    await expect.poll(inlet).toBeGreaterThan(300);
  });
});
