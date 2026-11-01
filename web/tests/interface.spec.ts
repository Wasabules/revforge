import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import type { EngineConfig } from '../src/lib/config';
import { CELL_INLET, CELL_OPEN, CELL_WALL, FluidField } from '../src/lib/fluidField';
import { GasFlow } from '../src/lib/fluid';
import { GasNetwork, P_ATM, type GasInputs } from '../src/lib/fluidGas';

/* eslint-disable @typescript-eslint/no-explicit-any */
const rf = (page: Page) => page.evaluate(() => {
  const r = (window as any).__revforge;
  const t = r.telemetry();
  const g = r.motion.gas;
  return {
    rpm: t.rpm, gear: t.gear, clutch: t.clutch, input: t.clutchInputRpm, slipKw: t.clutchSlipKw, torque: t.clutchTorque, speed: t.speed, level: t.levelDbA,
    time: g.net.time, blowdowns: g.net.blowdowns, burnt: g.net.burntBlowdowns, active: g.active, cost: g.costMs, turns: r.motion.turns, surface: r.motion.clutchSurface,
    draws: r.telemetryLog.draws, written: r.telemetryLog.written,
  };
});

async function open(page: Page, hash = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  return errors;
}

async function start(page: Page, hash = '') {
  const errors = await open(page, hash);
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => rf(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

const painted = (page: Page, testId: string) => page.getByTestId(testId).locator('canvas').first().evaluate((canvas: HTMLCanvasElement) => {
  const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
  let count = 0;
  for (let i = 3; i < data.length; i += 4) if (data[i] > 0) count++;
  return count;
});

// ── Le calcul, sans navigateur ──

const BASE: EngineConfig = {
  cylinders: 4, layout: 'inline', displacementCc: 2000, boreMm: 84, redlineRpm: 6800, exhaust: 'stock', aspiration: 'natural', fuel: 'petrol',
  multiplier: 0, pops: 'off', turboRelease: 'dump_valve', boostBar: 1, electric: false, body: 'compact', massKg: 1350, rimInch: 18, drive: 'front',
};
const ORDER4 = [1, 3, 4, 2];
const ORDER8 = [1, 8, 4, 3, 6, 5, 7, 2];
const RUNNING: GasInputs = { rpm: 3000, load: 0.8, boost: 0, release: 0, burning: true, heat: 0.6 };
const FRAME = 1 / 60 / 12;

function drive(net: GasNetwork, inputs: GasInputs, seconds: number, each?: (time: number) => void) {
  let cycle = 0;
  for (let t = 0; t < seconds; t += FRAME) {
    const turns = (inputs.rpm / 120) * FRAME;
    net.advance(FRAME, cycle, turns, inputs);
    cycle = (cycle + turns) % 1;
    each?.(t);
  }
}

test.describe('le calcul du gaz', () => {
  // Rien à voir avec le navigateur : une seule passe suffit.
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('une onde de pression va à la vitesse du son', () => {
    const net = new GasNetwork();
    net.build({ ...BASE, exhaust: 'straight' }, ORDER4);
    const rest: GasInputs = { rpm: 0, load: 0, boost: 0, release: 0, burning: false, heat: 0 };
    const a = net.lineFirst + 15;
    const b = a + 25;
    for (let k = -2; k <= 2; k++) net.ene[a + k] *= 1.03;
    let seenAt = -1;
    let peak = 0;
    for (let i = 0; i < 400; i++) {
      net.advance(1e-5, 0, 0, rest);
      if (net.p[b] > peak) {
        peak = net.p[b];
        seenAt = net.time;
      }
    }
    const speed = (25 * net.cell) / seenAt;
    expect(speed).toBeGreaterThan(337 * 0.93);
    expect(speed).toBeLessThan(337 * 1.07);
  });

  test('un changement de section renvoie l\'onde selon la théorie', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    const rest: GasInputs = { rpm: 0, load: 0, boost: 0, release: 0, burning: false, heat: 0 };
    // Le tube avant le résonateur, cinq fois plus large : R = (1 - 5) / (1 + 5).
    const tube = net.sections[2];
    expect(net.sections[3].area / tube.area).toBe(5);
    const probe = tube.first + Math.floor(tube.count * 0.55);
    const centre = tube.first + 8;
    // Une onde assez large pour que la diffusion du schéma ne la mange pas, qui part vers le résonateur : pression et vitesse en phase.
    for (let k = -8; k <= 8; k++) {
      const shape = 0.02 * P_ATM * Math.exp(-(k * k) / 18);
      const i = centre + k;
      const c = Math.sqrt((1.36 * net.p[i]) / net.rho[i]);
      net.ene[i] += shape / 0.36;
      net.rho[i] += shape / (c * c);
      net.mom[i] += shape / c;
    }
    // Le schéma étale l'onde sur son trajet, mais conserve son aire : c'est elle qu'on compare, pas le sommet.
    let incident = 0;
    let reflected = 0;
    for (let i = 0; i < 430; i++) {
      net.advance(1e-5, 0, 0, rest);
      const gauge = net.p[probe] - P_ATM;
      if (gauge > 0) incident += gauge;
      else reflected += gauge;
    }
    expect(incident).toBeGreaterThan(0);
    const ratio = reflected / incident;
    expect(ratio).toBeLessThan(-0.55);
    expect(ratio).toBeGreaterThan(-0.78);
  });

  test('une ligne n\'invente pas de contre-pression aux changements de section', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    drive(net, { ...RUNNING, rpm: 3000, load: 0.5 }, 0.5);
    let sum = 0;
    let n = 0;
    drive(net, { ...RUNNING, rpm: 3000, load: 0.5 }, 0.1, () => {
      sum += net.zP[net.zoneJoin];
      n++;
    });
    expect(sum / n / P_ATM).toBeLessThan(1.15);
    expect(sum / n / P_ATM).toBeGreaterThan(1.0);
  });

  test('au repos, rien ne bouge, même où la section change', () => {
    for (const config of [BASE, { ...BASE, aspiration: 'turbo' as const }, { ...BASE, cylinders: 8, layout: 'cross_plane' as const, exhaust: 'sport' as const }]) {
      const net = new GasNetwork();
      net.build(config, config.cylinders === 8 ? ORDER8 : ORDER4);
      const rest: GasInputs = { rpm: 0, load: 1, boost: 0, release: 0, burning: false, heat: 0 };
      for (let i = 0; i < 400; i++) net.advance(1e-4, 0, 0, rest);
      let top = 0;
      for (let i = 0; i < net.n; i++) top = Math.max(top, Math.abs(net.u[i]));
      expect(top).toBeLessThan(0.05);
    }
  });

  test('une bouffée par explosion, dans l\'ordre d\'allumage', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    const sequence: number[] = [];
    let last = -1;
    let cycle = 0;
    const inputs = { ...RUNNING };
    // Pas fins : chaque ouverture de soupape doit être vue.
    for (let t = 0; t < 0.2; t += 1e-4) {
      const turns = (inputs.rpm / 120) * 1e-4;
      net.advance(1e-4, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
      if (net.blowdowns !== last) {
        last = net.blowdowns;
        sequence.push(net.lastBlowdownCylinder + 1);
      }
    }
    // Cinq cycles en 0,2 s à 3000 tr/min, quatre bouffées par cycle.
    expect(net.blowdowns).toBeGreaterThanOrEqual(19);
    expect(net.blowdowns).toBeLessThanOrEqual(22);
    const tail = sequence.slice(-12);
    const start = ORDER4.indexOf(tail[0]);
    tail.forEach((cylinder, i) => expect(cylinder).toBe(ORDER4[(start + i) % 4]));
  });

  test('la pression voyage bien plus vite que la matière', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    drive(net, { ...RUNNING, rpm: 6000, load: 1 }, 0.3);
    const mid = net.sections[2].first + 5;
    let u = 0;
    let c = 0;
    const samples = 200;
    drive(net, { ...RUNNING, rpm: 6000, load: 1 }, samples * FRAME, () => {
      u += Math.abs(net.u[mid]);
      c += net.c[mid];
    });
    u /= samples;
    c /= samples;
    expect(c).toBeGreaterThan(450);
    expect(u).toBeGreaterThan(20);
    expect(u).toBeLessThan(c / 3);
  });

  test('coupure d\'injection : c\'est de l\'air pompé qui sort, plus froid', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    const on = { ...RUNNING, rpm: 5000, load: 0.9 };
    drive(net, on, 0.4);
    const hot = net.temp[net.lineFirst + 2];
    const burntBefore = net.burntBlowdowns;
    const before = net.blowdowns;
    const off = { ...on, load: 0, burning: false };
    drive(net, off, 0.4);
    expect(net.blowdowns).toBeGreaterThan(before + 30);
    // Au plus une bouffée déjà partie quand la coupure a commencé.
    expect(net.burntBlowdowns - burntBefore).toBeLessThanOrEqual(net.cylinders);
    expect(net.temp[net.lineFirst + 2]).toBeLessThan(hot - 100);
    expect(net.y[net.lineFirst + 2]).toBeLessThan(0.2);
  });

  test('un cylindre allumé dépasse 40 bar en pleine charge, et rien n\'approche ces valeurs à la coupure', () => {
    const net = new GasNetwork();
    net.build(BASE, ORDER4);
    let fired = 0;
    drive(net, { ...RUNNING, rpm: 4000, load: 1 }, 0.2, () => { fired = Math.max(fired, net.cylP[0]); });
    let cut = 0;
    drive(net, { ...RUNNING, rpm: 4000, load: 0, burning: false }, 0.2, () => { cut = Math.max(cut, net.cylP[0]); });
    expect(fired / P_ATM).toBeGreaterThan(40);
    expect(cut / P_ATM).toBeLessThan(20);
  });

  test('derrière le papillon : le vide pied levé, l\'atmosphère à fond, la pression du turbo', () => {
    const plenum = (config: EngineConfig, inputs: GasInputs) => {
      const net = new GasNetwork();
      net.build(config, ORDER4);
      drive(net, inputs, 0.4);
      let sum = 0;
      let n = 0;
      drive(net, inputs, 0.1, () => {
        sum += net.zP[net.zonePlenum];
        n++;
      });
      return sum / n / P_ATM;
    };
    expect(plenum(BASE, { ...RUNNING, rpm: 850, load: 0.15 })).toBeLessThan(0.45);
    expect(plenum(BASE, { ...RUNNING, rpm: 4000, load: 1 })).toBeGreaterThan(0.85);
    expect(plenum({ ...BASE, aspiration: 'turbo', boostBar: 1.2 }, { ...RUNNING, rpm: 4000, load: 1, boost: 0.9 })).toBeGreaterThan(1.6);
    // Pas de papillon sur un diesel : pas de vide, même pied levé.
    expect(plenum({ ...BASE, fuel: 'diesel' }, { ...RUNNING, rpm: 850, load: 0.1 })).toBeGreaterThan(0.9);
  });

  test('au lever de pied, la décharge du turbo vide le conduit ; en flutter, ça reflue par le compresseur', () => {
    const lift = (release: EngineConfig['turboRelease']) => {
      const net = new GasNetwork();
      net.build({ ...BASE, aspiration: 'turbo', turboRelease: release }, ORDER4);
      const on = { ...RUNNING, rpm: 5000, load: 1, boost: 0.9 };
      drive(net, on, 0.3);
      let vented = 0;
      let back = 0;
      drive(net, { ...on, load: 0, boost: 0.4, release: 1, burning: false }, 0.02, () => {
        vented = Math.max(vented, net.releaseFlow);
        back = Math.min(back, net.compressorFlow);
      });
      return { vented, back };
    };
    const dump = lift('dump_valve');
    expect(dump.vented).toBeGreaterThan(0.01);
    const flutter = lift('flutter');
    expect(flutter.vented).toBe(0);
    expect(flutter.back).toBeLessThan(-0.01);
  });

  test('la turbine prend de l\'énergie et lisse les bouffées', () => {
    const spread = (config: EngineConfig, inputs: GasInputs) => {
      const net = new GasNetwork();
      net.build(config, ORDER4);
      drive(net, inputs, 0.3);
      const cell = net.lineFirst + 3;
      let low = Infinity;
      let high = 0;
      let temp = 0;
      let n = 0;
      drive(net, inputs, 0.1, () => {
        low = Math.min(low, net.p[cell]);
        high = Math.max(high, net.p[cell]);
        temp += net.temp[cell];
        n++;
      });
      return { swing: high - low, temp: temp / n, power: net.turbinePower };
    };
    const inputs = { ...RUNNING, rpm: 4500, load: 1 };
    const natural = spread(BASE, inputs);
    const turbo = spread({ ...BASE, aspiration: 'turbo' }, { ...inputs, boost: 0.8 });
    expect(turbo.swing).toBeLessThan(natural.swing * 0.8);
    expect(turbo.temp).toBeLessThan(natural.temp);
    expect(turbo.power).toBeGreaterThan(500);
  });

  test('sur un V8 cross-plane, deux bouffées de suite dans une rangée, puis les rangées se rejoignent', () => {
    const net = new GasNetwork();
    net.build({ ...BASE, cylinders: 8, layout: 'cross_plane', displacementCc: 5000, boreMm: 92, exhaust: 'sport', body: 'coupe' }, ORDER8);
    expect(net.banks).toBe(2);
    const banks = ORDER8.map((cylinder) => net.bank[cylinder - 1]);
    const sameRow = banks.filter((bank, i) => bank === banks[(i + 1) % 8]).length;
    expect(sameRow).toBeGreaterThan(0);
    drive(net, { ...RUNNING, rpm: 3000 }, 0.5);
    // Du gaz brûlé dans les deux rangées, et dans la ligne commune après la jonction.
    expect(net.y[net.bankFirst[0] + 2]).toBeGreaterThan(0.5);
    expect(net.y[net.bankFirst[1] + 2]).toBeGreaterThan(0.5);
    expect(net.y[net.lineFirst + 2]).toBeGreaterThan(0.5);
  });

  test('le gaz qui recircule : dans un volume, une partie repart à contre-courant', () => {
    const gas = new GasFlow();
    gas.configure(BASE, ORDER4);
    const inputs = { ...RUNNING, rpm: 5000, load: 1 };
    let cycle = 0;
    for (let f = 0; f < 200; f++) {
      const turns = (inputs.rpm / 120) * FRAME;
      gas.wantChambers = true;
      gas.advance(FRAME, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
    }
    const chamber = gas.chambers.find((item) => item.section.kind === 'res')!;
    const { u, kind, count } = chamber.field;
    let forward = 0;
    let backward = 0;
    for (let c = 0; c < count; c++) {
      if (kind[c] !== 0) continue;
      if (u[c] > 1) forward++;
      if (u[c] < -1) backward++;
    }
    expect(forward).toBeGreaterThan(10);
    expect(backward).toBeGreaterThan(10);
  });

  test('le panache : un jet chaud s\'il y a combustion, sans fumée s\'il n\'y en a pas', () => {
    const run = (inputs: GasInputs) => {
      const gas = new GasFlow();
      gas.configure({ ...BASE, exhaust: 'straight' }, ORDER4);
      // Une ligne déjà pleine de ce que produit le moteur.
      let cycle = 0;
      for (let f = 0; f < 400; f++) {
        const turns = (inputs.rpm / 120) * FRAME;
        gas.advance(FRAME, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
      }
      const { smoke, temp, count, nx } = gas.plume;
      let smokeSum = 0;
      let reach = 0;
      for (let c = 0; c < count; c++) {
        smokeSum += smoke[c];
        if (temp[c] > 20) reach = Math.max(reach, c % nx);
      }
      return { smokeSum, reach };
    };
    const burning = run({ ...RUNNING, rpm: 4000, load: 1, heat: 0.6 });
    expect(burning.smokeSum).toBeGreaterThan(5);
    expect(burning.reach).toBeGreaterThan(30);
    const pumping = run({ ...RUNNING, rpm: 4000, load: 0.2, burning: false, heat: 0.6 });
    expect(pumping.smokeSum).toBeLessThan(burning.smokeSum * 0.02);
  });

  test('à froid, de la vapeur ; à chaud, presque rien', () => {
    const run = (heat: number) => {
      const gas = new GasFlow();
      gas.configure({ ...BASE, exhaust: 'straight' }, ORDER4, true, heat);
      const inputs = { ...RUNNING, rpm: 2500, load: 0.5, heat };
      let cycle = 0;
      for (let f = 0; f < 300; f++) {
        const turns = (inputs.rpm / 120) * FRAME;
        gas.advance(FRAME, cycle, turns, inputs);
        cycle = (cycle + turns) % 1;
      }
      let sum = 0;
      for (let c = 0; c < gas.plume.count; c++) sum += gas.plume.smoke[c];
      return sum;
    };
    expect(run(0.05)).toBeGreaterThan(run(0.7) * 2);
  });

  test('l\'écoulement 2D reste sans divergence : le jet aspire l\'air autour de lui', () => {
    const NX = 64;
    const NY = 40;
    const field = new FluidField(NX, NY, 0.02);
    for (let j = 0; j < NY; j++) {
      for (let i = 0; i < NX; i++) {
        const c = i + j * NX;
        if (j === 0 || i === 0) field.kind[c] = CELL_WALL;
        else if (i === NX - 1 || j === NY - 1) field.kind[c] = CELL_OPEN;
      }
    }
    const inlet: number[] = [];
    for (let j = 18; j <= 21; j++) for (let i = 1; i <= 4; i++) { field.kind[i + j * NX] = CELL_INLET; inlet.push(i + j * NX); }
    field.finalize();
    field.iterations = 30;
    for (let f = 0; f < 120; f++) {
      for (const c of inlet) field.u[c] = 30;
      field.step(1e-3);
    }
    let total = 0;
    let speed = 0;
    let cells = 0;
    let entrained = 0;
    for (let j = 2; j < NY - 2; j++) {
      for (let i = 8; i < NX - 2; i++) {
        const c = i + j * NX;
        total += Math.abs(0.5 * (field.u[c + 1] - field.u[c - 1] + field.v[c + NX] - field.v[c - NX]));
        speed += Math.abs(field.u[c]) + Math.abs(field.v[c]);
        cells++;
        // Loin du jet, l'air n'a de vitesse que parce que la projection l'y met.
        if (j > 30) entrained = Math.max(entrained, Math.abs(field.v[c]));
      }
    }
    expect(total / cells).toBeLessThan((speed / cells) * 0.15);
    expect(entrained).toBeGreaterThan(0.3);
  });

  test('rien n\'est alloué d\'une image à l\'autre', () => {
    const gas = new GasFlow();
    gas.configure(BASE, ORDER4);
    const inputs = { ...RUNNING, rpm: 5000, load: 1 };
    let cycle = 0;
    const step = () => {
      const turns = (inputs.rpm / 120) * FRAME;
      gas.wantChambers = true;
      gas.advance(FRAME, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
    };
    for (let f = 0; f < 600; f++) step();
    // On additionne ce que le tas gagne entre deux relevés : un ramasse-miettes ne peut que le sous-estimer.
    let grown = 0;
    let previous = process.memoryUsage().heapUsed;
    for (let f = 0; f < 3000; f++) {
      step();
      if (f % 50 === 0) {
        const now = process.memoryUsage().heapUsed;
        if (now > previous) grown += now - previous;
        previous = now;
      }
    }
    // Quelques nombres par pas de calcul, tout au plus : la version qui passait ses nombres en arguments en allouait 85 ko.
    expect(grown / 3000).toBeLessThan(4000);
  });

  test('le calcul complet tient dans une image, même sur un gros moteur', () => {
    const gas = new GasFlow();
    gas.configure({ ...BASE, cylinders: 12, layout: 'v', displacementCc: 6500, boreMm: 90, exhaust: 'sport', body: 'supercar', redlineRpm: 8500 }, [1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10]);
    const inputs = { ...RUNNING, rpm: 8000, load: 1 };
    let cycle = 0;
    for (let f = 0; f < 600; f++) {
      const turns = (inputs.rpm / 120) * FRAME;
      gas.wantChambers = true;
      gas.advance(FRAME, cycle, turns, inputs);
      cycle = (cycle + turns) % 1;
    }
    // Largement au-dessus de ce qu'on vise (4 ms) : la machine des tests peut être chargée.
    expect(gas.costMs).toBeLessThan(12);
  });
});

// ── Dans la page ──

test('le gaz reçoit une bouffée par explosion, au ralenti de Motion', async ({ page }) => {
  const errors = await start(page);
  await page.waitForTimeout(1500);
  const a = await rf(page);
  const wall = 2500;
  await page.waitForTimeout(wall);
  const b = await rf(page);
  const rpm = (a.rpm + b.rpm) / 2;
  const expected = ((rpm / 120) * 4 * (wall / 1000)) / 12;
  // Cycles et bouffées suivent le même ralenti que les pistons.
  expect((b.blowdowns - a.blowdowns) / expected).toBeGreaterThan(0.6);
  expect((b.blowdowns - a.blowdowns) / expected).toBeLessThan(1.5);
  const physical = (b.time - a.time) / (wall / 1000);
  expect(physical).toBeGreaterThan(0.5 / 12);
  expect(physical).toBeLessThan(1.5 / 12);
  expect(errors).toEqual([]);
});

test('à l\'arrêt, le gaz ne calcule rien', async ({ page }) => {
  await open(page);
  await page.waitForTimeout(500);
  const a = await rf(page);
  await page.waitForTimeout(800);
  const b = await rf(page);
  expect(b.time).toBe(a.time);
  expect(b.cost).toBe(0);
});

test('hors écran, plus aucun calcul', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 500 });
  await start(page);
  await page.waitForTimeout(500);
  await page.evaluate(() => window.scrollTo(0, document.documentElement.scrollHeight));
  await page.waitForTimeout(400);
  const a = await rf(page);
  await page.waitForTimeout(600);
  const b = await rf(page);
  expect(b.rpm).toBeGreaterThan(300);
  expect(b.time).toBe(a.time);
  await page.evaluate(() => window.scrollTo(0, 0));
  await expect.poll(() => rf(page).then((t) => t.time)).toBeGreaterThan(b.time);
});

test('au rupteur, ce qui sort n\'a pas brûlé', async ({ page }) => {
  await start(page);
  await page.keyboard.down('Space');
  await expect(page.getByText('Rupteur', { exact: true })).toBeVisible({ timeout: 5000 });
  const a = await rf(page);
  await page.waitForTimeout(1500);
  const b = await rf(page);
  await page.keyboard.up('Space');
  const fired = b.burnt - a.burnt;
  const total = b.blowdowns - a.blowdowns;
  expect(total).toBeGreaterThan(4);
  // Le rupteur coupe par rafales : une bonne part des ouvertures ne chasse que de l'air.
  expect(fired).toBeLessThan(total);
});

test('une détonation chauffe la fin de ligne, et une flamme sort', async ({ page }) => {
  await start(page, '#m=4.inline.1984.86.7200.straight.natural.petrol.0.strong.dump_valve~compact.1300.17.front.1');
  await page.waitForTimeout(800);
  const result = await page.evaluate(async () => {
    const r = (window as any).__revforge;
    const gas = r.motion.gas;
    const net = gas.net;
    const cell = net.lineFirst + net.lineCount - 8;
    const before = net.temp[cell];
    gas.pop(130);
    r.motion.flame = 1;
    let after = 0;
    let flame = 0;
    for (let i = 0; i < 12; i++) {
      await new Promise((resolve) => requestAnimationFrame(resolve));
      after = Math.max(after, net.temp[cell]);
      if (gas.plume.flame) for (const value of gas.plume.flame) flame = Math.max(flame, value);
    }
    return { before, after, flame };
  });
  expect(result.after).toBeGreaterThan(result.before + 50);
  expect(result.flame).toBeGreaterThan(0.2);
});

test('l\'embrayage à mi-course glisse, chauffe, et le disque suit sa propre vitesse', async ({ page }) => {
  const errors = await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => rf(page).then((t) => t.gear), { timeout: 3000 }).toBe(1);
  await page.evaluate(() => (window as any).__revforge.engine.setControls(0.8, 0, 0.72));
  let hottest = 0;
  let slipping = false;
  for (let i = 0; i < 40; i++) {
    await page.waitForTimeout(50);
    const t = await rf(page);
    hottest = Math.max(hottest, t.surface);
    if (t.slipKw > 0.5 && Math.abs(t.rpm - t.input) > 150) slipping = true;
  }
  expect(slipping).toBe(true);
  expect(hottest).toBeGreaterThan(61);
  await voir(page, 'clutch');
  await expect(page.getByTestId('clutch-state')).toHaveText(/patine|transmet/);
  await expect.poll(() => painted(page, 'clutch-view')).toBeGreaterThan(2000);
  expect(errors).toEqual([]);
});

test('pédale à fond, l\'embrayage est ouvert et ne transmet plus rien', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => rf(page).then((t) => t.gear), { timeout: 3000 }).toBe(1);
  await page.evaluate(() => (window as any).__revforge.engine.setControls(0.3, 0, 1));
  await expect.poll(() => rf(page).then((t) => t.clutch), { timeout: 3000 }).toBeLessThan(0.02);
  await voir(page, 'clutch');
  await expect(page.getByTestId('clutch-state')).toHaveText('Débrayé');
  const t = await rf(page);
  expect(Math.abs(t.torque)).toBeLessThan(5);
  expect(t.slipKw).toBeLessThan(0.2);
});

test('l\'horloge de l\'embrayage suit celle des pistons', async ({ page }) => {
  await start(page);
  const measured = await page.evaluate(async () => {
    const motion = (window as any).__revforge.motion;
    await new Promise((resolve) => setTimeout(resolve, 1500));
    let total = 0;
    let last = motion.flywheelAngle;
    const from = motion.turns;
    for (let i = 0; i < 40; i++) {
      await new Promise((resolve) => requestAnimationFrame(resolve));
      const now = motion.flywheelAngle;
      total += (now - last + Math.PI * 2) % (Math.PI * 2);
      last = now;
    }
    // Deux tours de volant par cycle à quatre temps.
    return { revolutions: total / (Math.PI * 2), cycles: motion.turns - from };
  });
  expect(measured.revolutions / measured.cycles).toBeCloseTo(2, 0);
});

test('graphiques fermés : aucun dessin ; ouverts : courbes, survol, gel et valeurs', async ({ page }) => {
  await start(page);
  await page.waitForTimeout(600);
  // Fermé : pas de nœud, pas de dessin, mais l'historique se remplit sans rien coûter.
  await expect(page.getByTestId('charts-body')).toHaveCount(0);
  const closed = await rf(page);
  await page.waitForTimeout(400);
  const later = await rf(page);
  expect(later.draws).toBe(0);
  expect(later.written).toBeGreaterThan(closed.written);

  await page.getByTestId('charts-toggle').click();
  await expect(page.getByTestId('chart-rpm')).toBeVisible();
  await page.getByTestId('charts-toggle').scrollIntoViewIfNeeded();
  await page.getByTestId('chart-rpm').scrollIntoViewIfNeeded();
  await expect.poll(() => rf(page).then((t) => t.draws)).toBeGreaterThan(0);
  expect(await painted(page, 'chart-rpm')).toBeGreaterThan(500);

  // Une lecture au survol : la valeur et le nom de chaque courbe.
  const box = (await page.getByTestId('chart-rpm').locator('canvas').boundingBox())!;
  await page.mouse.move(box.x + box.width - 40, box.y + box.height / 2);
  await expect(page.getByTestId('charts-tooltip')).toContainText('tr/min');
  await page.mouse.move(0, 0);

  // Choisir ce qu'on regarde.
  await page.getByLabel('Charge').check();
  await expect(page.getByTestId('chart-load')).toBeVisible();

  // Figer : plus rien ne bouge, même si le moteur continue.
  await page.getByTestId('charts-freeze').click();
  const value = async () => page.getByTestId('chart-rpm').locator('.legend strong').first().textContent();
  await page.keyboard.down('Space');
  const frozen = await value();
  await page.waitForTimeout(500);
  expect(await value()).toBe(frozen);
  await page.keyboard.up('Space');
  await page.getByTestId('charts-freeze').click();

  // Le tableau, sans passer par le survol.
  await page.getByTestId('charts-body').getByText('Voir les valeurs').first().click();
  await expect(page.getByTestId('charts-table').locator('tbody tr').first()).toBeVisible();
});

test('le spectrogramme défile et se lit, seulement quand on le demande', async ({ page }) => {
  await start(page);
  await page.getByTestId('charts-toggle').click();
  await expect(page.getByTestId('chart-spectrogram')).toHaveCount(0);
  await page.getByTestId('charts-spectrogram').check();
  await page.getByTestId('chart-spectrogram').scrollIntoViewIfNeeded();
  await page.waitForTimeout(800);
  // Une partie seulement du tracé est remplie : une colonne par instant écoulé depuis qu'il est ouvert.
  const coloured = await page.getByTestId('chart-spectrogram').locator('canvas').evaluate((canvas: HTMLCanvasElement) => {
    const { data } = canvas.getContext('2d')!.getImageData(canvas.width - 20, 0, 20, canvas.height - 30);
    const seen = new Set<number>();
    for (let i = 0; i < data.length; i += 4) seen.add((data[i] << 16) | (data[i + 1] << 8) | data[i + 2]);
    return seen.size;
  });
  expect(coloured).toBeGreaterThan(20);
});

test('la réponse de la ligne vient du moteur et ne suit pas le régime', async ({ page }) => {
  await start(page);
  await page.getByTestId('charts-toggle').click();
  await page.getByTestId('charts-line').check();
  const chart = page.getByTestId('chart-line');
  await expect(chart).toBeVisible();
  const path = () => chart.locator('path.curve').getAttribute('d');
  const before = await path();
  expect(before!.split('L').length).toBeGreaterThan(200);
  const first = await chart.getByTestId('line-order').first().getAttribute('x1');
  await page.keyboard.down('Space');
  await expect.poll(async () => Number(await chart.getByTestId('line-order').first().getAttribute('x1'))).toBeGreaterThan(Number(first) + 20);
  // Les raies défilent, la ligne ne bouge pas : c'est le principe du timbre.
  expect(await path()).toBe(before);
  await page.keyboard.up('Space');
  // Une autre ligne, une autre réponse.
  await ouvrir(page, 'exhaust');
  await page.getByRole('button', { name: 'Ligne directe' }).click();
  await expect.poll(path).not.toBe(before);
});

test('le niveau en dB(A) n\'apparaît que si la simulation le donne', async ({ page }) => {
  await start(page);
  await page.getByTestId('charts-toggle').click();
  await page.waitForTimeout(300);
  const { level } = await rf(page);
  await expect(page.getByLabel('Niveau sonore estimé')).toHaveCount(typeof level === 'number' ? 1 : 0);
});

test('animation réduite : tout reste dessiné, plus rien ne bouge', async ({ page }) => {
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await start(page);
  await page.waitForTimeout(400);
  const a = await rf(page);
  await page.waitForTimeout(500);
  const b = await rf(page);
  expect(b.active).toBe(false);
  expect(b.turns).toBe(a.turns);
  expect(b.time).toBe(a.time);
  // Le moteur s'ouvre en 3D, testée dans moteur.spec.ts : ici, son écorché 2D, figé lui aussi.
  await page.getByTestId('engine-mode-2d').click();
  await expect.poll(() => painted(page, 'engine-view')).toBeGreaterThan(500);
  expect(await painted(page, 'car-stage')).toBeGreaterThan(500);
  await voir(page, 'gas');
  await expect.poll(() => painted(page, 'exhaust-view')).toBeGreaterThan(500);
  await voir(page, 'clutch');
  await expect.poll(() => painted(page, 'clutch-view')).toBeGreaterThan(500);
});

test('animation réduite par le système : un clic la relance, et le choix reste', async ({ page }) => {
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await start(page);
  await expect(page.getByTestId('animations-note')).toBeVisible();
  await page.getByTestId('animations-note').getByRole('button').click();
  await expect(page.getByTestId('animations-note')).toBeHidden();
  const before = await rf(page);
  await expect.poll(async () => (await rf(page)).turns).toBeGreaterThan(before.turns);
  // Le gaz ne se calcule que si sa vue est à l'écran.
  await voir(page, 'gas');
  await page.getByTestId('exhaust-view').scrollIntoViewIfNeeded();
  await expect.poll(async () => (await rf(page)).active).toBe(true);
  await page.reload();
  await expect(page.getByTestId('animations')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.getByTestId('animations-note')).toBeHidden();
});

test('les animations passent au temps réel, et le choix reste', async ({ page }) => {
  await start(page);
  const rate = async () => {
    const before = await page.evaluate(() => { const m = (window as any).__revforge.motion; return { turns: m.turns, at: performance.now() }; });
    await page.waitForTimeout(1000);
    return page.evaluate((b) => { const r = (window as any).__revforge; return { cycles: (r.motion.turns - b.turns) / ((performance.now() - b.at) / 1000), expected: r.telemetry().rpm / 120 }; }, before);
  };
  const slow = await rate();
  expect(slow.cycles).toBeCloseTo(slow.expected / 12, 0);
  await ouvrir(page, 'display');
  await page.getByRole('button', { name: 'Temps réel' }).click();
  const real = await rate();
  expect(real.cycles / real.expected).toBeGreaterThan(0.7);
  expect(real.cycles / real.expected).toBeLessThan(1.3);
  await page.reload();
  expect(await page.evaluate(() => (window as any).__revforge.motion.slow)).toBe(1);
});

test('une voiture électrique n\'a ni gaz ni embrayage, et rien ne casse', async ({ page }) => {
  const errors = await open(page);
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('electric');
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await page.waitForTimeout(500);
  const t = await rf(page);
  expect(t.active).toBe(false);
  await voir(page, 'clutch');
  await expect(page.getByTestId('clutch-state')).toHaveCount(0);
  await voir(page, 'gas');
  await expect.poll(() => painted(page, 'exhaust-view')).toBeGreaterThan(50);
  expect(errors).toEqual([]);
});

test('changer de pièces en marche ne repart pas d\'une ligne froide', async ({ page }) => {
  await start(page);
  await page.waitForTimeout(800);
  await ouvrir(page, 'exhaust');
  await page.getByRole('button', { name: 'Ligne directe' }).click();
  await voir(page, 'gas');
  await page.getByTestId('exhaust-view').scrollIntoViewIfNeeded();
  // La ligne est reconstruite à l'image suivante, et d'emblée pleine de gaz brûlé.
  await expect.poll(() => page.evaluate(() => {
    const net = (window as any).__revforge.motion.gas.net;
    return net.sections.length === 2 ? net.y[net.lineFirst + Math.floor(net.lineCount / 2)] : -1;
  })).toBeGreaterThan(0.5);
});

test('thème clair : les vues reprennent les couleurs du thème, sans erreur', async ({ page }) => {
  const errors = await start(page);
  // Le contour du moteur suit le texte du thème : on compte les pixels de cette couleur précise.
  const textPixels = () => page.getByTestId('exhaust-view').locator('canvas').evaluate((canvas: HTMLCanvasElement) => {
    const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
    let count = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (data[i + 3] > 200 && Math.abs(data[i] - 0x16) < 5 && Math.abs(data[i + 1] - 0x18) < 5 && Math.abs(data[i + 2] - 0x1c) < 5) count++;
    }
    return count;
  });
  await voir(page, 'gas');
  await page.getByTestId('exhaust-view').scrollIntoViewIfNeeded();
  await page.waitForTimeout(300);
  const before = await textPixels();
  await page.addStyleTag({ content: ':root{color-scheme:light;--bg:#f4f4f1;--surface:#fcfcfb;--surface-raised:#eeeeea;--line:#e1e0d9;--line-strong:#c3c2b7;--text:#16181c;--text-dim:#5a5d63;--accent:#c95a12;--on-accent:#fff;--danger:#c63a38;--cool:#2a78d6;--metal:#6a7482;}' });
  await expect.poll(textPixels).toBeGreaterThan(before + 100);
  await page.getByTestId('charts-toggle').click();
  await page.getByTestId('charts-spectrogram').check();
  await page.getByTestId('chart-rpm').scrollIntoViewIfNeeded();
  await page.waitForTimeout(600);
  expect(await painted(page, 'chart-rpm')).toBeGreaterThan(500);
  expect(errors).toEqual([]);
});

test('sur téléphone, rien ne déborde, panneau ouvert compris', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 740 });
  await start(page);
  await page.getByTestId('charts-toggle').click();
  await page.getByTestId('charts-spectrogram').check();
  await page.getByTestId('charts-line').check();
  await page.waitForTimeout(400);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
  for (const [id, name] of [['exhaust-view', 'gas'], ['clutch-view', 'clutch']] as const) {
    await voir(page, name);
    await page.getByTestId(id).scrollIntoViewIfNeeded();
    await expect.poll(() => painted(page, id)).toBeGreaterThan(500);
  }
});
