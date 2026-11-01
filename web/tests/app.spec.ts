import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';

declare global {
  interface Window {
    __revforge: {
      engine: { level(): number };
      telemetry(): { rpm: number; speed: number; gear: number; peak: number; torque: number; power: number; levelDbA: number; listenerX: number; listenerY: number; listenerZ: number };
      spec(): { idle: number };
      motion: { turns: number; turbine: number; heat: number; turbineAngle: number; burning: boolean; pulses: { strength: number; burnt: boolean }[] };
    };
  }
}

const telemetry = (page: Page) => page.evaluate(() => window.__revforge.telemetry());
const level = (page: Page) => page.evaluate(() => window.__revforge.engine.level());

async function start(page: Page) {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

test('démarre, fait du son, monte en régime et se coupe', async ({ page }) => {
  const errors = await start(page);
  await expect.poll(() => level(page), { timeout: 5000 }).toBeGreaterThan(0.005);

  // Le ralenti de la fiche : juste après le démarreur, le moteur est encore en train de monter.
  const idle = await page.evaluate(() => window.__revforge.spec().idle);
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(idle + 2000);
  await page.keyboard.up('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeLessThan(idle + 200);

  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  expect(errors).toEqual([]);
});

test('maintenir les touches de conduite ne fait pas défiler la page', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 720 });
  await start(page);
  const scrolled = () => page.evaluate(() => window.scrollY);
  const hold = async (key: string) => {
    // Plusieurs appuis sans relâcher : ce sont les répétitions d'une touche maintenue.
    for (let i = 0; i < 12; i++) await page.keyboard.down(key);
    await page.waitForTimeout(250);
    await page.keyboard.up(key);
  };
  // Focus sur la page, comme après un clic ailleurs que sur un bouton.
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await hold('Space');
  expect(await scrolled()).toBe(0);
  await hold('ArrowUp');
  expect(await scrolled()).toBe(0);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await hold('ArrowDown');
  expect(await scrolled()).toBe(0);
  // Le bouton de démarrage a le focus : maintenir Espace ne doit pas couper le moteur.
  await page.getByTestId('start').focus();
  await hold('Space');
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
});

test('moteur coupé, Espace garde son rôle habituel', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 720 });
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.keyboard.press('Space');
  await expect.poll(() => page.evaluate(() => window.scrollY)).toBeGreaterThan(0);
});

test('la pédale à la souris fait monter le régime', async ({ page }) => {
  await start(page);
  const pedal = page.getByRole('button', { name: 'Accélérateur' });
  const box = (await pedal.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + 8);
  await page.mouse.down();
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(2500);
  await page.mouse.up();
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeLessThan(1500);
});

test('conduite : la voiture avance et passe les rapports, le frein l\'arrête', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 15000 }).toBeGreaterThanOrEqual(2);
  expect((await telemetry(page)).speed).toBeGreaterThan(10);
  await page.keyboard.up('Space');
  await page.keyboard.down('b');
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeLessThan(0.5);
  await page.keyboard.up('b');
});

test('balade : le pilote automatique démarre seul', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Balade', exact: true }).click();
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeGreaterThan(10);
});

test('tous les préréglages jouent, sans erreur ni silence', async ({ page }) => {
  const errors = await start(page);
  await ouvrir(page, 'share');
  const select = page.getByTestId('preset');
  const ids = await select.locator('option').evaluateAll((options) => options.map((option) => (option as HTMLOptionElement).value));
  expect(ids.length).toBeGreaterThanOrEqual(15);
  for (const id of ids) {
    await select.selectOption(id);
    await expect.poll(() => level(page), { message: id, timeout: 5000 }).toBeGreaterThan(0.002);
  }
  expect(errors).toEqual([]);
});

test('régler un moteur crée « Mon moteur », et le lien le restitue', async ({ page }) => {
  await start(page);
  await ouvrir(page, 'exhaust');
  await page.getByRole('button', { name: 'Ligne directe' }).click();
  await ouvrir(page, 'engine');
  await page.getByRole('button', { name: 'Turbo', exact: true }).click();
  await page.getByRole('button', { name: 'Flutter' }).click();
  await page.getByRole('button', { name: '8', exact: true }).click();
  await expect(page.getByTestId('preset')).toHaveValue('custom');
  await expect(page.getByTestId('summary')).toContainText('8 cylindres');
  const url = page.url();
  expect(url).toContain('#m=8.');
  expect(url).toContain('~');

  const other = await page.context().newPage();
  await other.goto(url);
  await expect(other.getByTestId('summary')).toContainText('8 cylindres');
  await ouvrir(other, 'engine');
  await expect(other.getByRole('button', { name: 'Flutter' })).toHaveAttribute('aria-pressed', 'true');
  await ouvrir(other, 'exhaust');
  await expect(other.getByRole('button', { name: 'Ligne directe' })).toHaveAttribute('aria-pressed', 'true');
  await other.close();
});

test('un lien invalide retombe sur un moteur par défaut', async ({ page }) => {
  await page.goto('./#m=abc.def');
  await expect(page.getByTestId('preset')).toHaveValue('four_cyl_sport');
});

test('la fiche technique suit les réglages : puissance, couple, performances', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const number = async (id: string) => Number((await page.getByTestId(id).textContent())!.replace(/[^0-9,]/g, '').replace(',', '.'));
  await expect.poll(() => number('spec-hp')).toBeGreaterThan(50);
  const before = { hp: await number('spec-hp'), torque: await number('spec-torque'), sprint: await number('spec-100') };
  await ouvrir(page, 'engine');
  await page.getByRole('slider', { name: 'Cylindrée' }).fill('4000');
  await expect.poll(() => number('spec-hp')).toBeGreaterThan(before.hp * 1.5);
  expect(await number('spec-torque')).toBeGreaterThan(before.torque * 1.5);
  expect(await number('spec-100')).toBeLessThan(before.sprint);
  await ouvrir(page, 'car');
  await page.getByRole('slider', { name: 'Masse' }).fill('2500');
  await expect.poll(() => number('spec-100')).toBeGreaterThan(5);
  await expect(page.getByTestId('power-curves')).toContainText('Couple');
});

test('la voiture, le moteur et la ligne se dessinent', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  // Le moteur s'ouvre en 3D, testée dans moteur.spec.ts : ici, son écorché 2D.
  await page.getByTestId('engine-mode-2d').click();
  for (const id of ['car-stage', 'engine-view', 'exhaust-view']) {
    if (id === 'exhaust-view') await voir(page, 'gas');
    await expect.poll(() => page.getByTestId(id).locator('canvas').evaluate((canvas: HTMLCanvasElement) => {
      const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
      let painted = 0;
      for (let i = 3; i < data.length; i += 4) if (data[i] > 0) painted++;
      return painted;
    }), { message: id }).toBeGreaterThan(500);
  }
});

test('boîte manuelle : on passe les rapports soi-même', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.getByRole('button', { name: 'Manuelle' }).click();
  await expect.poll(() => telemetry(page).then((t) => t.gear)).toBe(0);
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 3000 }).toBe(1);
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 15000 }).toBeGreaterThan(15);
  // Elle ne monte pas toute seule, même à fond.
  expect((await telemetry(page)).gear).toBe(1);
  await page.getByTestId('shift-up').click();
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 3000 }).toBe(2);
  await page.keyboard.up('Space');
});

test('le couple affiché suit le pied', async ({ page }) => {
  await start(page);
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.torque), { timeout: 3000 }).toBeGreaterThan(50);
  await page.keyboard.up('Space');
  await expect.poll(() => telemetry(page).then((t) => t.torque), { timeout: 3000 }).toBeLessThan(5);
});

test('la pression du turbo se règle, change le couple et tient dans le lien', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const torque = async () => Number((await page.getByTestId('spec-torque').textContent())!.replace(/[^0-9]/g, ''));
  await ouvrir(page, 'engine');
  await page.getByRole('button', { name: 'Turbo', exact: true }).click();
  const slider = page.getByRole('slider', { name: 'Pression du turbo' });
  await slider.fill('1');
  await expect.poll(torque).toBeGreaterThan(200);
  const low = await torque();
  await slider.fill('2.5');
  await expect.poll(torque).toBeGreaterThan(low * 1.5);
  expect(page.url()).toContain('.2.5');
  const other = await page.context().newPage();
  await other.goto(page.url());
  await ouvrir(other, 'engine');
  await expect(other.getByRole('slider', { name: 'Pression du turbo' })).toHaveValue('2.5');
  await other.close();
});

test('le rupteur s\'affiche', async ({ page }) => {
  await start(page);
  await page.keyboard.down('Space');
  await expect(page.getByText('Rupteur', { exact: true })).toBeVisible({ timeout: 5000 });
  await page.keyboard.up('Space');
});

test('les pistons tournent en proportion du vrai régime', async ({ page }) => {
  await start(page);
  // Cycles animés par seconde, comparés au régime au même moment : ralenti de 12 fois.
  const measure = () => page.evaluate(async () => {
    const before = window.__revforge.motion.turns;
    const rpmA = window.__revforge.telemetry().rpm;
    await new Promise((resolve) => setTimeout(resolve, 600));
    const rpmB = window.__revforge.telemetry().rpm;
    return { rate: (window.__revforge.motion.turns - before) / 0.6, rpm: (rpmA + rpmB) / 2 };
  });
  await page.waitForTimeout(2500);
  const idle = await measure();
  expect(idle.rate).toBeCloseTo(idle.rpm / 120 / 12, 0);
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(5000);
  const high = await measure();
  await page.keyboard.up('Space');
  expect(high.rate).toBeGreaterThan(idle.rate * 3);
  expect(high.rate / (high.rpm / 120 / 12)).toBeGreaterThan(0.75);
  expect(high.rate / (high.rpm / 120 / 12)).toBeLessThan(1.25);
});

test('au rupteur, les bouffées ne brûlent plus', async ({ page }) => {
  await start(page);
  await page.keyboard.down('Space');
  await expect(page.getByText('Rupteur', { exact: true })).toBeVisible({ timeout: 5000 });
  const cold = await page.evaluate(async () => {
    let seen = 0;
    for (let i = 0; i < 60; i++) {
      seen += window.__revforge.motion.pulses.filter((pulse) => !pulse.burnt).length;
      await new Promise((resolve) => requestAnimationFrame(resolve));
    }
    return seen;
  });
  await page.keyboard.up('Space');
  expect(cold).toBeGreaterThan(0);
});

test('la turbine prend sa vitesse sous charge et garde son élan au lever de pied', async ({ page }) => {
  await page.goto('./?test#m=4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
  await page.waitForTimeout(1500);
  const turbine = () => page.evaluate(() => window.__revforge.motion.turbine);
  const atIdle = await turbine();
  await page.keyboard.down('Space');
  await expect.poll(turbine, { timeout: 6000 }).toBeGreaterThan(atIdle + 0.3);
  const spinning = await turbine();
  await voir(page, 'gas');
  await expect(page.getByTestId('turbo-readout')).toContainText('bar');
  await page.keyboard.up('Space');
  await page.waitForTimeout(300);
  // La pression retombe tout de suite, pas la turbine : elle a de l'inertie.
  expect(await turbine()).toBeGreaterThan(spinning * 0.6);
});

test('le point de fonctionnement reste sous la courbe tant qu\'on n\'est pas à fond', async ({ page }) => {
  await start(page);
  await page.waitForTimeout(1500);
  const y = (id: string) => page.getByTestId(id).evaluate((dot) => Number(dot.getAttribute('cy')));
  const atIdle = await y('live-torque-dot');
  await page.keyboard.down('Space');
  await expect.poll(() => y('live-torque-dot')).toBeLessThan(atIdle - 15);
  await page.keyboard.up('Space');
});

test('en manuelle, repartir en sixième cale le moteur, et on le relance', async ({ page }) => {
  await start(page);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  await page.getByRole('button', { name: 'Manuelle' }).click();
  for (let gear = 1; gear <= 6; gear++) {
    await page.keyboard.press('ArrowRight');
    await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 3000 }).toBe(gear);
  }
  await page.keyboard.down('Space');
  await expect(page.getByTestId('stalled')).toBeVisible({ timeout: 6000 });
  await page.keyboard.up('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeLessThan(100);
  await page.getByTestId('restart').click();
  await expect(page.getByTestId('stalled')).toBeHidden();
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 5000 }).toBeGreaterThan(500);
});

test('la fiche indique le démarreur de ce moteur', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(page.getByTestId('starter')).toContainText('à réducteur');
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('v8_cross_plane');
  await expect(page.getByTestId('starter')).toContainText('à entraînement direct');
  await expect(page.getByTestId('starter')).toContainText('denture droite');
});

test('les anciens liens restent valides', async ({ page }) => {
  await page.goto('./#m=8.v.5000.92.7000.sport.natural.petrol.0.off.dump_valve');
  await expect(page.getByTestId('summary')).toContainText('8 cylindres');
});

test('exporte un WAV valide', async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'share');
  const [download] = await Promise.all([page.waitForEvent('download'), page.getByTestId('export-blips').click()]);
  expect(download.suggestedFilename()).toMatch(/^revforge-.*coups-de-gaz\.wav$/);
  const path = await download.path();
  const { readFileSync } = await import('node:fs');
  const bytes = readFileSync(path);
  expect(bytes.subarray(0, 4).toString()).toBe('RIFF');
  // Stéréo : deux oreilles, comme à l'écoute.
  expect(bytes.readUInt16LE(22)).toBe(2);
  expect(bytes.length).toBe(44 + 44100 * 14 * 2 * 2);
  const samples = new Int16Array(bytes.buffer, bytes.byteOffset + 44, 44100 * 14 * 2);
  const left = samples.filter((_, i) => i % 2 === 0).slice(44100, 88200);
  const right = samples.filter((_, i) => i % 2 === 1).slice(44100, 88200);
  expect(Math.max(...left)).toBeGreaterThan(300);
  expect(Math.max(...right)).toBeGreaterThan(300);
  // Pas une copie d'un canal sur l'autre : la voiture n'est pas dans l'axe de la tête.
  expect(left.some((value, i) => value !== right[i])).toBe(true);
});

test('la pédale est visible sans défiler, même sur un petit écran', async ({ page }) => {
  for (const [width, height] of [[1280, 720], [390, 844]]) {
    await page.setViewportSize({ width, height });
    await page.goto('./');
    await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
    const box = (await page.getByRole('button', { name: 'Accélérateur' }).boundingBox())!;
    expect(box.y + box.height).toBeLessThanOrEqual(height);
  }
});

test('aucun débordement horizontal sur téléphone', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 740 });
  await page.goto('./');
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});

// ── Où l'on écoute ──

/** Le niveau estimé, une fois la tête arrivée (elle glisse, à 6 m/s) et le sonomètre posé (constante de temps « fast », 125 ms). */
async function settledDb(page: Page) {
  const at = () => telemetry(page).then((t) => [t.listenerX, t.listenerY]);
  let before = await at();
  await expect
    .poll(
      async () => {
        await page.waitForTimeout(150);
        const now = await at();
        const moved = Math.hypot(now[0] - before[0], now[1] - before[1]);
        before = now;
        return moved;
      },
      { timeout: 15_000 },
    )
    .toBeLessThan(0.01);
  await page.waitForTimeout(700);
  return (await telemetry(page)).levelDbA;
}

const position = (page: Page, name: string) => page.getByRole('group', { name: "Position d'écoute" }).getByRole('button', { name, exact: true });

test("chaque position d'écoute a son niveau, du plus discret au plus fort", async ({ page }) => {
  const errors = await start(page);
  await ouvrir(page, 'listener');
  await position(page, 'Habitacle').click();
  await expect(page.getByTestId('listener-where')).toContainText("Dans l'habitacle");
  const cabin = await settledDb(page);
  await position(page, 'Derrière').click();
  const behind = await settledDb(page);
  await expect(page.getByTestId('listener-where')).toContainText('Derrière');
  await position(page, "L'oreille dans le pot").click();
  const pipe = await settledDb(page);
  await expect(page.getByTestId('listener-where')).toContainText('de la sortie');
  await position(page, 'Au loin').click();
  const far = await settledDb(page);
  await position(page, 'Sous le capot').click();
  const bay = await settledDb(page);
  await expect(page.getByTestId('listener-where')).toContainText('Sous le capot');

  expect(pipe).toBeGreaterThan(behind + 15);
  expect(far).toBeLessThan(behind);
  expect(bay).toBeGreaterThan(behind);
  expect(cabin).toBeGreaterThan(25);
  expect(cabin).toBeLessThan(70);
  expect(errors).toEqual([]);
});

test('le niveau estimé apparaît en direct et ne dépend pas du volume', async ({ page }) => {
  await start(page);
  await ouvrir(page, 'listener');
  await expect(page.getByTestId('level-dba')).toHaveText(/≈ \d+/, { timeout: 5000 });
  await expect(page.getByTestId('live-dba')).toHaveText(/^\d+ dB\(A\)$/);
  const before = await settledDb(page);
  await page.evaluate(() => window.__revforge.engine.setVolume(0.1));
  const after = await settledDb(page);
  expect(Math.abs(after - before)).toBeLessThan(3);
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.levelDbA), { timeout: 8000 }).toBeGreaterThan(before + 8);
  await page.keyboard.up('Space');
});

test('glisser sur le plan à la souris déplace la tête, sans saut', async ({ page }) => {
  await start(page);
  await ouvrir(page, 'listener');
  const map = page.getByTestId('listener-map');
  await map.scrollIntoViewIfNeeded();
  const box = (await map.boundingBox())!;
  const before = await telemetry(page);
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.1, { steps: 4 });
  await expect(page.getByTestId('listener-where')).toContainText('m');
  await page.mouse.up();
  await expect.poll(() => telemetry(page).then((t) => Math.hypot(t.listenerX - before.listenerX, t.listenerY - before.listenerY))).toBeGreaterThan(0.5);
  // Choisir une position toute prête après un glissé : la tête y retourne.
  await position(page, 'Derrière').click();
  await expect.poll(() => telemetry(page).then((t) => Math.hypot(t.listenerX - before.listenerX, t.listenerY - before.listenerY)), { timeout: 8000 }).toBeLessThan(0.1);
});

test('le plan se manipule au doigt, sans faire défiler la page', async ({ page, browserName }) => {
  test.skip(browserName !== 'chromium', 'Les événements tactiles passent par le protocole de Chromium.');
  await page.setViewportSize({ width: 390, height: 844 });
  await start(page);
  await ouvrir(page, 'listener');
  const map = page.getByTestId('listener-map');
  await map.scrollIntoViewIfNeeded();
  const canvas = map.locator('canvas');
  expect(await canvas.evaluate((node) => getComputedStyle(node).touchAction)).toBe('none');
  const box = (await canvas.boundingBox())!;
  const before = await telemetry(page);
  const scrolled = await page.evaluate(() => window.scrollY);
  const session = await page.context().newCDPSession(page);
  const touch = (type: string, x: number, y: number) =>
    session.send('Input.dispatchTouchEvent', { type: type as 'touchStart', touchPoints: type === 'touchEnd' ? [] : [{ x, y }] });
  const x = box.x + box.width / 2;
  await touch('touchStart', x, box.y + box.height * 0.3);
  for (let i = 1; i <= 5; i++) await touch('touchMove', x, box.y + box.height * (0.3 - i * 0.04));
  await touch('touchEnd', 0, 0);
  await expect.poll(() => telemetry(page).then((t) => Math.hypot(t.listenerX - before.listenerX, t.listenerY - before.listenerY))).toBeGreaterThan(0.5);
  expect(await page.evaluate(() => window.scrollY)).toBe(scrolled);
});

test("les vitres ne se proposent qu'à bord, et font entrer le dehors", async ({ page }) => {
  await start(page);
  await ouvrir(page, 'listener');
  const windows = page.getByRole('group', { name: 'Vitres' });
  await expect(windows).toHaveCount(0);
  await position(page, 'Habitacle').click();
  await expect(windows).toBeVisible();
  const closed = await settledDb(page);
  await windows.getByRole('button', { name: 'Ouvertes' }).click();
  await expect.poll(() => settledDb(page), { timeout: 8000 }).toBeGreaterThan(closed + 1);
});

test("changer d'échappement se voit sur la fiche : niveau et ce que retient la ligne", async ({ page }) => {
  await page.goto('./#m=4.inline.1984.83.6500.stock.natural.petrol.0.off.dump_valve');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const standstill = page.getByTestId('sheet-standstill');
  await expect(standstill).toHaveText(/^\d+ dB\(A\)$/, { timeout: 10_000 });
  const read = async () => Number((await standstill.textContent())!.match(/\d+/)![0]);
  const origin = await read();
  const loss = await page.getByTestId('line-loss').innerHTML();
  await ouvrir(page, 'exhaust');
  await page.getByRole('group', { name: 'Échappement' }).getByRole('button', { name: 'Ligne directe' }).click();
  await expect.poll(read).toBeGreaterThan(origin + 10);
  expect(await page.getByTestId('line-loss').innerHTML()).not.toBe(loss);
  await page.getByRole('group', { name: 'Échappement' }).getByRole('button', { name: 'Confort' }).click();
  await expect.poll(read).toBeLessThan(origin);
});

test("isolation et emplacement du moteur changent l'habitacle sur la fiche", async ({ page }) => {
  // Une ligne silencieuse : sinon, à plein gaz, la sortie masque ce que l'emplacement change (le bloc et l'admission).
  await page.goto('./#m=4.inline.1984.83.6500.quiet.natural.petrol.0.off.dump_valve');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const cabin = page.getByTestId('sheet-cabin-full');
  await expect(cabin).toHaveText(/^\d+ dB\(A\)$/, { timeout: 10_000 });
  const read = async () => Number((await cabin.textContent())!.match(/\d+/)![0]);
  const normal = await read();
  await ouvrir(page, 'car');
  await page.getByRole('group', { name: 'Insonorisation' }).getByRole('button', { name: 'Course' }).click();
  await expect.poll(read).toBeGreaterThan(normal + 8);
  await page.getByRole('group', { name: 'Insonorisation' }).getByRole('button', { name: 'Luxe' }).click();
  await expect.poll(read).toBeLessThan(normal - 3);
  const luxury = await read();
  await page.getByRole('group', { name: 'Moteur', exact: true }).getByRole('button', { name: 'Central' }).click();
  await expect.poll(read).toBeGreaterThan(luxury + 2);
});

test('le lien de partage garde emplacement, isolation et sorties, et les anciens liens restent valides', async ({ page }) => {
  await page.goto('./?test#m=8.cross_plane.5000.96.6000.sport.natural.petrol.0.light.dump_valve~coupe.1700.19.rear.1_mid_luxury_x');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const pressed = (group: string, name: string) => page.getByRole('group', { name: group, exact: true }).getByRole('button', { name, exact: true });
  await ouvrir(page, 'exhaust');
  await expect(pressed('Sorties', 'Pont en X')).toHaveAttribute('aria-pressed', 'true');
  await ouvrir(page, 'car');
  await expect(pressed('Moteur', 'Central')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed('Insonorisation', 'Luxe')).toHaveAttribute('aria-pressed', 'true');
  await pressed('Insonorisation', 'Course').click();
  const hash = await page.evaluate(() => location.hash);
  expect(hash).toContain('_mid_stripped_x');
  await page.goto('about:blank');
  await page.goto(`./?test${hash}`);
  await ouvrir(page, 'car');
  await expect(pressed('Moteur', 'Central')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed('Insonorisation', 'Course')).toHaveAttribute('aria-pressed', 'true');

  // Un lien d'avant : la voiture sans ces champs. La carrosserie dit où est le moteur.
  await page.goto('about:blank');
  await page.goto('./?test#m=6.boxer.3800.102.8000.sport.natural.petrol.0.light.dump_valve~supercar.1450.20.rear.1');
  await ouvrir(page, 'car');
  await expect(pressed('Moteur', 'Central')).toHaveAttribute('aria-pressed', 'true');
  await expect(pressed('Insonorisation', 'Normale')).toHaveAttribute('aria-pressed', 'true');
  await page.goto('about:blank');
  await page.goto('./?test#m=4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'car');
  await expect(pressed('Moteur', 'Avant')).toHaveAttribute('aria-pressed', 'true');
});

test('le plan tient sur un téléphone, sans débordement', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 740 });
  await page.goto('./');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await ouvrir(page, 'listener');
  const map = page.getByTestId('listener-map');
  await map.scrollIntoViewIfNeeded();
  const box = (await map.boundingBox())!;
  expect(box.x).toBeGreaterThanOrEqual(0);
  expect(box.x + box.width).toBeLessThanOrEqual(360);
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});

test("l'export part de la position choisie", async ({ page }) => {
  await page.goto('./#m=4.inline.1984.83.6500.sport.natural.petrol.0.off.dump_valve');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  const rmsOf = async () => {
    await ouvrir(page, 'share');
    const [download] = await Promise.all([page.waitForEvent('download'), page.getByTestId('export-blips').click()]);
    const { readFileSync } = await import('node:fs');
    const bytes = readFileSync(await download.path());
    const samples = new Int16Array(bytes.buffer, bytes.byteOffset + 44, (bytes.length - 44) / 2);
    let energy = 0;
    for (const value of samples) energy += value * value;
    return Math.sqrt(energy / samples.length);
  };
  // Le fichier est un enregistrement réel de cette position : le niveau suit.
  const behind = await rmsOf();
  await ouvrir(page, 'listener');
  await position(page, 'Habitacle').click();
  const cabin = await rmsOf();
  expect(Math.abs(20 * Math.log10(cabin / behind))).toBeGreaterThan(0.1);
});
