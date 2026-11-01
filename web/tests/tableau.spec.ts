import { expect, test, type Locator, type Page } from '@playwright/test';
import { ouvrir, voir, type PanelId } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */

const TURBO = '#m=4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3';
const PANELS: PanelId[] = ['engine', 'exhaust', 'gearbox', 'car', 'listener', 'share', 'display'];

const telemetry = (page: Page) => page.evaluate(() => (window as any).__revforge.telemetry());
/** Ce qu'une jauge affiche : le signe moins typographique et la virgule française redeviennent un nombre. */
const shown = async (page: Page, testId: string) => {
  const text = (await page.getByTestId(testId).textContent())!;
  return Number(text.replace(/[^\d,.\-−+]/g, '').replace('−', '-').replace(',', '.'));
};
/** Le modèle du gaz, moyenné sur quelques images : c'est ce que les jauges disent lire. */
const gasModel = (page: Page) => page.evaluate(async () => {
  const net = (window as any).__revforge.motion.gas.net;
  let plenum = 0;
  let collector = 0;
  let join = 0;
  let exit = 0;
  const frames = 60;
  for (let i = 0; i < frames; i++) {
    await new Promise((resolve) => requestAnimationFrame(resolve));
    plenum += net.zP[net.zonePlenum];
    join += net.zP[net.zoneJoin];
    collector += net.zT[net.zoneJoin];
    exit += net.exitT;
  }
  return { plenum: plenum / frames / 1e5 - 1.01325, join: join / frames / 1e5 - 1.01325, collector: collector / frames - 273.15, exit: exit / frames - 273.15 };
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
  await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
  return errors;
}

const atIdle = async (page: Page) => {
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await expect.poll(() => telemetry(page).then((t) => t.cranking === 0 && t.rpm < 1300 && t.rpm > 300), { timeout: 10_000 }).toBe(true);
};

test.describe('les volets de réglage', () => {
  test('chaque bouton ouvre son volet, Échap le ferme et rend le focus au bouton', async ({ page }) => {
    const errors = await open(page);
    for (const id of PANELS) {
      const button = page.getByTestId(`open-${id}`);
      const box = page.getByTestId(`panel-${id}`);
      await expect(box).toBeHidden();
      await expect(button).toHaveAttribute('aria-expanded', 'false');
      await button.click();
      await expect(box).toBeVisible();
      await expect(button).toHaveAttribute('aria-expanded', 'true');
      // Un vrai dialogue, nommé par son titre.
      await expect(page.getByRole('dialog', { name: (await box.locator('h2').textContent())! })).toBeVisible();
      // Le focus est entré.
      expect(await box.evaluate((node) => node.contains(document.activeElement))).toBe(true);
      await page.keyboard.press('Escape');
      await expect(box).toBeHidden();
      await expect(button).toBeFocused();
      await expect(button).toHaveAttribute('aria-expanded', 'false');
    }
    expect(errors).toEqual([]);
  });

  test('un volet à la fois ; le bouton fermer et le bouton d\'ouverture le ferment aussi', async ({ page }) => {
    await open(page);
    await page.getByTestId('open-engine').click();
    await page.getByTestId('open-car').click();
    await expect(page.getByTestId('panel-engine')).toBeHidden();
    await expect(page.getByTestId('panel-car')).toBeVisible();
    expect(await page.getByTestId('panel-car').evaluate((node) => node.contains(document.activeElement))).toBe(true);
    await page.getByTestId('panel-car-close').click();
    await expect(page.getByTestId('panel-car')).toBeHidden();
    await expect(page.getByTestId('open-car')).toBeFocused();
    await page.getByTestId('open-car').click();
    await page.getByTestId('open-car').click();
    await expect(page.getByTestId('panel-car')).toBeHidden();
  });

  test('Échap ferme même si le focus est sorti du volet', async ({ page }) => {
    await start(page);
    await ouvrir(page, 'engine');
    await page.getByRole('button', { name: 'Accélérateur' }).focus();
    await page.keyboard.press('Escape');
    await expect(page.getByTestId('panel-engine')).toBeHidden();
    await expect(page.getByTestId('open-engine')).toBeFocused();
  });

  test('ce qui est réglé se lit sur les boutons, volet fermé', async ({ page }) => {
    await open(page, TURBO);
    await expect(page.getByTestId('open-engine-summary')).toHaveText(/2,0 L · 4 cyl · \d+ ch/);
    await expect(page.getByTestId('open-exhaust-summary')).toContainText('Sport');
    await expect(page.getByTestId('open-gearbox-summary')).toContainText('rapports');
    await expect(page.getByTestId('open-car-summary')).toContainText('1400 kg');
    await expect(page.getByTestId('open-listener-summary')).toHaveText('Derrière');
    await expect(page.getByTestId('open-display-summary')).toContainText('Français');
    await ouvrir(page, 'engine');
    await page.getByRole('button', { name: '6', exact: true }).click();
    await expect(page.getByTestId('open-engine-summary')).toContainText('6 cyl');
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption('electric');
    await expect(page.getByTestId('open-engine-summary')).toContainText('Électrique');
    await expect(page.getByTestId('open-share-summary')).toHaveText('Électrique');
  });

  test('un réglage s\'applique volet ouvert : le moteur continue de tourner, le son et les jauges suivent', async ({ page }) => {
    const errors = await start(page);
    await ouvrir(page, 'engine');
    const before = await page.evaluate(() => {
      const r = (window as any).__revforge;
      return { written: r.telemetryLog.written, hp: r.spec().peakHp, redline: r.spec().redline, state: r.engine.context.state };
    });
    expect(before.state).toBe('running');
    // Tout ce qui part vers le son passe par là.
    await page.evaluate(() => {
      const engine = (window as any).__revforge.engine;
      const sent: string[] = [];
      (window as any).__sent = sent;
      const send = engine.send.bind(engine);
      engine.send = (command: { type: string }) => {
        sent.push(command.type);
        send(command);
      };
    });
    await page.getByRole('button', { name: 'Turbo', exact: true }).click();
    expect(await page.evaluate(() => (window as any).__sent)).toContain('config');
    await page.getByRole('slider', { name: 'Cylindrée' }).fill('4000');
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.spec().peakHp)).toBeGreaterThan(before.hp * 1.5);
    await expect(page.getByTestId('panel-engine')).toBeVisible();
    // La télémétrie arrive toujours, moteur en marche, et le son sort.
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetryLog.written)).toBeGreaterThan(before.written + 15);
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(300);
    await expect.poll(() => page.evaluate(() => (window as any).__revforge.engine.level())).toBeGreaterThan(0.001);
    await expect(page.getByTestId('open-engine-summary')).toContainText('4,0 L');
    expect(errors).toEqual([]);
  });

  test('l\'accélérateur au clavier marche volet ouvert, sauf en tapant dans un champ', async ({ page }) => {
    await start(page, TURBO);
    await atIdle(page);
    await ouvrir(page, 'car');
    // Volet ouvert, le focus dans son cadre : Espace reste la pédale.
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(2500);
    await page.keyboard.up('Space');
    await atIdle(page);
    await ouvrir(page, 'car');
    // Sur un curseur, les flèches déplacent le curseur et n'accélèrent pas.
    const slider = page.getByRole('slider', { name: 'Masse' });
    await slider.focus();
    const value = await slider.inputValue();
    await page.keyboard.down('ArrowUp');
    await page.waitForTimeout(500);
    expect((await telemetry(page)).rpm).toBeLessThan(1500);
    await page.keyboard.up('ArrowUp');
    expect(await slider.inputValue()).not.toBe(value);
    // Dans la liste des préréglages non plus.
    await ouvrir(page, 'share');
    await page.getByTestId('preset').focus();
    await page.keyboard.down('ArrowUp');
    await page.waitForTimeout(300);
    expect((await telemetry(page)).throttle).toBeLessThan(0.3);
    await page.keyboard.up('ArrowUp');
  });

  test('le volet laisse voir le compte-tours, la voiture et les pédales', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await open(page);
    const overlap = (a: { x: number; y: number; width: number; height: number }, b: { x: number; y: number; width: number; height: number }) =>
      a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
    for (const id of PANELS) {
      await ouvrir(page, id);
      const box = (await page.getByTestId(`panel-${id}`).boundingBox())!;
      for (const seen of [page.getByTestId('gauge-rpm'), page.getByTestId('car-stage'), page.getByRole('button', { name: 'Accélérateur' }), page.getByTestId('start')]) {
        expect(overlap(box, (await seen.boundingBox())!)).toBe(false);
      }
    }
  });

  test('assez large, le tableau se resserre et les jauges restent visibles à côté du volet', async ({ page }) => {
    await page.setViewportSize({ width: 1920, height: 1080 });
    await open(page);
    await ouvrir(page, 'engine');
    const box = (await page.getByTestId('panel-engine').boundingBox())!;
    const gauge = (await page.getByTestId('gauge-manifold').boundingBox())!;
    expect(gauge.x + gauge.width).toBeLessThanOrEqual(box.x);
  });

  test('la note sur la vitesse reste à côté du réglage de vitesse', async ({ page }) => {
    await open(page);
    await ouvrir(page, 'display');
    const box = page.getByTestId('panel-display');
    await expect(box.getByTestId('speed-help')).toHaveCount(0);
    await box.getByRole('button', { name: 'Temps réel' }).click();
    await expect(box.getByTestId('speed-help')).toContainText('tr/min');
  });

  test('langue, animations et vitesse se règlent depuis l\'affichage', async ({ page }) => {
    await open(page);
    await ouvrir(page, 'display');
    await page.getByTestId('animations').click();
    await expect(page.getByTestId('open-display-summary')).toContainText('sans animation');
    await page.getByTestId('animations').click();
    await page.getByRole('button', { name: '1/4' }).click();
    await expect(page.getByTestId('open-display-summary')).toContainText('1/4');
    await page.getByTestId('lang-en').click();
    await expect(page.getByTestId('start')).toHaveText('Start the engine');
    await expect(page.getByTestId('open-display-summary')).toContainText('English');
  });
});

test.describe('les jauges lisent la simulation', () => {
  test('le compte-tours suit le régime, et sa zone rouge suit le réglage', async ({ page }) => {
    await open(page);
    const needle = () => page.getByTestId('gauge-rpm').locator('line.needle').evaluate((node: SVGLineElement) => node.transform.baseVal.getItem(0).angle);
    const before = await needle();
    await expect(page.getByTestId('tach-redline')).toContainText('7\u202f200');
    await page.getByTestId('open-engine').click();
    await expect(page.getByTestId('gauge-rpm')).toHaveAttribute('aria-valuemax', '8000');
    const zone = () => page.getByTestId('gauge-rpm').locator('path.zone').getAttribute('d');
    const zoneBefore = await zone();
    await page.getByRole('slider', { name: 'Régime maximal' }).fill('8000');
    await expect(page.getByTestId('tach-redline')).toContainText('8\u202f000');
    // Une graduation de plus au-delà du régime maximal : la zone rouge reste visible.
    await expect(page.getByTestId('gauge-rpm')).toHaveAttribute('aria-valuemax', '9000');
    expect(await zone()).not.toBe(zoneBefore);
    await page.keyboard.press('Escape');
    await page.getByTestId('start').click();
    await expect(page.getByTestId('start')).toHaveText('Couper le contact');
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(4000);
    // En pleine montée, le régime prend mille tours en un dixième de seconde : la comparaison se fait une fois au rupteur.
    await page.waitForTimeout(1500);
    const pair = await page.evaluate(async () => {
      await new Promise((resolve) => setTimeout(resolve, 150));
      const text = document.querySelector('[data-testid=rpm]')!.textContent!;
      return { text: Number(text.replace(/\D/g, '')), rpm: (window as any).__revforge.telemetry().rpm };
    });
    expect(Math.abs(pair.text - pair.rpm)).toBeLessThan(800);
    expect(await needle()).not.toBe(before);
    await page.keyboard.up('Space');
  });

  test('le collecteur est en dépression au ralenti, comme le dit le modèle du gaz', async ({ page }) => {
    await start(page);
    await atIdle(page);
    await page.waitForTimeout(1200);
    const gauge = await shown(page, 'gauge-manifold-value');
    expect(gauge).toBeLessThan(-0.3);
    expect(gauge).toBeGreaterThan(-1);
    // Le ralenti accéléré du démarrage retombe encore : l'écart se lit une fois posé.
    await expect.poll(async () => {
      const model = (await gasModel(page)).plenum;
      return Math.abs((await shown(page, 'gauge-manifold-value')) - model);
    }, { timeout: 15_000 }).toBeLessThan(0.1);
    await expect(page.getByTestId('gauge-manifold-value')).toHaveText(/^−0,\d\d$/);
  });

  test('sur un turbo à fond, le collecteur passe au-dessus de l\'atmosphère', async ({ page }) => {
    await start(page, TURBO);
    await atIdle(page);
    await page.waitForTimeout(800);
    expect(await shown(page, 'gauge-manifold-value')).toBeLessThan(-0.3);
    await page.keyboard.down('Space');
    await expect.poll(() => shown(page, 'gauge-manifold-value'), { timeout: 10_000 }).toBeGreaterThan(0.5);
    const gauge = await shown(page, 'gauge-manifold-value');
    expect(Math.abs(gauge - (await gasModel(page)).plenum)).toBeLessThan(0.3);
    // L'échelle et le repère viennent de la pression réglée.
    await expect(page.getByTestId('gauge-manifold')).toHaveAttribute('aria-valuemax', '1.5');
    await expect(page.getByTestId('gauge-manifold-mark')).toHaveCount(1);
    await page.keyboard.up('Space');
    await expect.poll(() => shown(page, 'gauge-manifold-value'), { timeout: 8000 }).toBeLessThan(0);
  });

  test('sans turbo, le collecteur ne passe jamais vraiment au-dessus de l\'atmosphère ; à l\'arrêt il y est', async ({ page }) => {
    await open(page);
    expect(await shown(page, 'gauge-manifold-value')).toBe(0);
    await page.getByTestId('start').click();
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(500);
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await page.waitForTimeout(2500);
    const full = await shown(page, 'gauge-manifold-value');
    expect(full).toBeLessThan(0.1);
    expect(full).toBeGreaterThan(-0.3);
    await page.keyboard.up('Space');
    await expect(page.getByTestId('gauge-manifold')).toHaveAttribute('aria-valuemax', '0.5');
    await expect(page.getByTestId('gauge-manifold-mark')).toHaveCount(0);
    await page.getByTestId('start').click();
    await expect.poll(() => shown(page, 'gauge-manifold-value'), { timeout: 5000 }).toBe(0);
  });

  test('températures et contre-pression sont celles du modèle du gaz, et montent sous charge', async ({ page }) => {
    await start(page);
    await atIdle(page);
    await page.waitForTimeout(1000);
    const cold = await shown(page, 'gauge-collector-value');
    await page.keyboard.down('Space');
    await expect.poll(() => shown(page, 'gauge-collector-value'), { timeout: 15_000 }).toBeGreaterThan(cold + 80);
    await page.waitForTimeout(1000);
    const model = await gasModel(page);
    const collector = await shown(page, 'gauge-collector-value');
    const back = await shown(page, 'gauge-back-value');
    const tailpipe = await shown(page, 'gauge-tailpipe');
    await page.keyboard.up('Space');
    expect(Math.abs(collector - model.collector)).toBeLessThan(80);
    expect(Math.abs(tailpipe - model.exit)).toBeLessThan(80);
    expect(Math.abs(back - model.join)).toBeLessThan(0.15);
    expect(back).toBeGreaterThan(0.03);
  });

  test('le niveau en dB(A) et ses repères viennent de la télémétrie', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 1100 });
    await start(page);
    await atIdle(page);
    await page.waitForTimeout(600);
    const quiet = await shown(page, 'meter-level-value');
    const level = (await telemetry(page)).levelDbA;
    expect(Math.abs(quiet - level)).toBeLessThan(6);
    const before = await page.getByTestId('level-landmark').textContent();
    expect(before).toContain('Comme ');
    await page.keyboard.down('Space');
    await expect.poll(() => shown(page, 'meter-level-value'), { timeout: 8000 }).toBeGreaterThan(quiet + 15);
    await expect.poll(() => page.getByTestId('level-landmark').textContent()).not.toBe(before);
    await page.keyboard.up('Space');
  });

  test('un seuil dangereux change la couleur et le dit en toutes lettres', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 1100 });
    await start(page, '#m=8.cross_plane.5000.96.6000.straight.natural.petrol.0.off.dump_valve~coupe.1700.19.rear.1');
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect(page.getByTestId('level-danger')).toBeVisible({ timeout: 10_000 });
    await expect(page.getByTestId('meter-level').locator('rect.fill')).toHaveClass(/danger|warn/);
    await page.keyboard.up('Space');
  });

  test('vitesse, accélération et pédale lisent la conduite', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 1320 });
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => shown(page, 'gauge-speed-value'), { timeout: 15_000 }).toBeGreaterThan(20);
    await expect.poll(() => shown(page, 'meter-throttle-value')).toBe(100);
    await expect.poll(() => shown(page, 'meter-accel-value'), { timeout: 5000 }).toBeGreaterThan(0.05);
    const speed = await shown(page, 'gauge-speed-value');
    expect(Math.abs(speed - (await telemetry(page)).speed)).toBeLessThan(25);
    await page.keyboard.up('Space');
    await page.keyboard.down('b');
    await expect.poll(() => shown(page, 'meter-accel-value'), { timeout: 5000 }).toBeLessThan(-0.05);
    await page.keyboard.up('b');
    // L'échelle vient de la vitesse de pointe calculée.
    const top = await page.evaluate(() => (window as any).__revforge.spec().topSpeed);
    const max = Number(await page.getByTestId('gauge-speed').getAttribute('aria-valuemax'));
    expect(max).toBeGreaterThanOrEqual(top);
    expect(max).toBeLessThanOrEqual(top * 1.35);
  });

  test('l\'embrayage et la transmission lisent la télémétrie', async ({ page }) => {
    await start(page);
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await page.getByTestId('dynamics').scrollIntoViewIfNeeded();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.press('ArrowRight');
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.outputRpm), { timeout: 10_000 }).toBeGreaterThan(200);
    const read = () => page.evaluate(async () => {
      await new Promise((resolve) => setTimeout(resolve, 150));
      const number = (id: string) => Number(document.querySelector(`[data-testid=${id}]`)!.textContent!.replace(/\D/g, ''));
      const t = (window as any).__revforge.telemetry();
      return { input: number('stat-input'), output: number('stat-output'), wheels: number('stat-wheels'), t, surface: (window as any).__revforge.motion.clutchSurface, temp: number('meter-clutch-c-value') };
    });
    const r = await read();
    await page.keyboard.up('Space');
    expect(r.output).toBeGreaterThan(50);
    expect(Math.abs(r.input - r.t.clutchInputRpm)).toBeLessThan(1000);
    expect(Math.abs(r.output - r.t.outputRpm)).toBeLessThan(500);
    expect(Math.abs(r.temp - r.surface)).toBeLessThan(20);
    await expect(page.getByTestId('stat-peak')).toHaveText(/dBFS|—/);
  });

  test('courbes de puissance et de couple : le point vivant et sa valeur', async ({ page }) => {
    await start(page);
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(async () => Number((await page.getByTestId('curve-live-torque').textContent())!.replace(/\D/g, '')), { timeout: 5000 }).toBeGreaterThan(40);
    await page.keyboard.up('Space');
  });

  test('tendances : la dernière valeur est celle de l\'historique, et le survol remonte le temps', async ({ page }) => {
    await start(page);
    await page.getByTestId('trends').scrollIntoViewIfNeeded();
    await page.waitForTimeout(1500);
    const value = await shown(page, 'trend-rpm-value');
    expect(Math.abs(value - (await telemetry(page)).rpm)).toBeLessThan(400);
    const paint = () => page.getByTestId('trend-rpm').evaluate((canvas: HTMLCanvasElement) => {
      const { data } = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height);
      let count = 0;
      for (let i = 3; i < data.length; i += 4) if (data[i] > 0) count++;
      return count;
    });
    await expect.poll(paint).toBeGreaterThan(50);
    const box = (await page.getByTestId('trend-rpm').boundingBox())!;
    await page.mouse.move(box.x + box.width * 0.5, box.y + box.height / 2);
    await expect(page.getByTestId('trends').locator('.foot')).toContainText('il y a');
    await page.mouse.move(0, 0);
    await expect(page.getByTestId('trends').locator('.foot')).not.toContainText('il y a');
  });

  test('le tableau de toutes les valeurs dit d\'où vient chacune, seulement s\'il est ouvert', async ({ page }) => {
    await start(page);
    await expect(page.getByTestId('readings-table')).toHaveCount(0);
    await page.getByTestId('readings-toggle').click();
    const table = page.getByTestId('readings-table');
    await expect(table).toContainText('tr/min');
    await expect(table).toContainText('Modèle du gaz');
    await expect(table).toContainText('Modèle de conduite');
    const first = () => table.locator('tbody tr').first().locator('td').first().textContent();
    const before = await first();
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(first, { timeout: 5000 }).not.toBe(before);
    await page.keyboard.up('Space');
  });

  test('électrique : ni collecteur ni gaz d\'échappement, et rien ne casse', async ({ page }) => {
    const errors = await open(page);
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption('electric');
    await expect(page.getByTestId('gauge-manifold')).toHaveCount(0);
    await expect(page.getByTestId('gauge-collector')).toHaveCount(0);
    await expect(page.getByTestId('gauge-back')).toHaveCount(0);
    await expect(page.getByTestId('gauge-power')).toBeVisible();
    await page.getByTestId('start').click();
    await expect.poll(() => telemetry(page).then((t) => t.rpm >= 0)).toBe(true);
    await page.waitForTimeout(500);
    expect(errors).toEqual([]);
  });

  test('un diesel n\'a pas de dépression', async ({ page }) => {
    await start(page, '#m=4.inline.1984.83.4500.stock.natural.diesel.0.off.dump_valve~compact.1400.18.front.1');
    await atIdle(page);
    await page.waitForTimeout(1000);
    expect(await shown(page, 'gauge-manifold-value')).toBeGreaterThan(-0.15);
  });
});

test.describe('animations coupées', () => {
  test('les jauges restent vraies et vivantes, le gaz affiché ne calcule rien', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    const errors = await start(page, TURBO);
    await atIdle(page);
    await page.waitForTimeout(800);
    expect(await shown(page, 'gauge-manifold-value')).toBeLessThan(-0.3);
    const frozen = await page.evaluate(() => {
      const gas = (window as any).__revforge.motion.gas;
      return { active: gas.active, time: gas.net.time };
    });
    expect(frozen.active).toBe(false);
    await page.keyboard.down('Space');
    await expect.poll(() => shown(page, 'gauge-manifold-value'), { timeout: 10_000 }).toBeGreaterThan(0.5);
    await expect.poll(() => shown(page, 'gauge-collector-value'), { timeout: 10_000 }).toBeGreaterThan(120);
    await expect.poll(() => shown(page, 'rpm')).toBeGreaterThan(2500);
    await page.keyboard.up('Space');
    const after = await page.evaluate(() => (window as any).__revforge.motion.gas.net.time);
    expect(after).toBe(frozen.time);
    expect(errors).toEqual([]);
  });
});

test.describe('ce qui ne se voit pas ne coûte rien', () => {
  test('une seule vue 3D montée au départ ; une vue cachée se met en pause', async ({ page }) => {
    await start(page);
    await expect(page.getByTestId('engine-view')).toHaveCount(1);
    await expect(page.getByTestId('clutch-view')).toHaveCount(0);
    await expect(page.getByTestId('gearbox-view')).toHaveCount(0);
    await expect(page.getByTestId('exhaust-view')).toHaveCount(0);
    const frames = () => page.evaluate(() => (document.querySelector('[data-testid=engine-view]') as any).__engine.frames);
    await expect.poll(frames).toBeGreaterThan(0);
    await voir(page, 'gas');
    await expect(page.getByTestId('engine-view')).toBeHidden();
    await page.waitForTimeout(150);
    const paused = await frames();
    await page.waitForTimeout(500);
    expect(await frames()).toBe(paused);
    await voir(page, 'engine');
    await expect.poll(frames).toBeGreaterThan(paused);
  });

  test('une vue cachée ne suit plus la télémétrie', async ({ page }) => {
    await start(page);
    await voir(page, 'clutch');
    const figures = page.getByTestId('clutch-state').locator('xpath=ancestor::dl');
    await expect(figures).toHaveCount(1);
    await voir(page, 'engine');
    await figures.evaluate((node) => {
      (window as any).__changes = 0;
      new MutationObserver(() => (window as any).__changes++).observe(node, { characterData: true, childList: true, subtree: true });
    });
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.down('Space');
    await expect.poll(() => telemetry(page).then((t) => t.rpm)).toBeGreaterThan(2500);
    await page.waitForTimeout(400);
    await page.keyboard.up('Space');
    expect(await page.evaluate(() => (window as any).__changes)).toBe(0);
  });

  test('une valeur qui ne change pas n\'est pas réécrite : moteur coupé, les jauges ne touchent plus au DOM', async ({ page }) => {
    await open(page);
    await page.waitForTimeout(500);
    const writes = await page.evaluate(async () => {
      let count = 0;
      const observer = new MutationObserver((records) => (count += records.length));
      // Tout sauf les valeurs lues par les aides techniques, rafraîchies une fois par seconde.
      const filter = ['transform', 'stroke-dasharray', 'stroke-dashoffset', 'x', 'width', 'class'];
      for (const selector of ['.rail', '.cockpit', '.analysis']) {
        observer.observe(document.querySelector(selector)!, { attributes: true, attributeFilter: filter, characterData: true, childList: true, subtree: true });
      }
      await new Promise((resolve) => setTimeout(resolve, 1000));
      observer.disconnect();
      return count;
    });
    expect(writes).toBe(0);
  });

  test('l\'annonce vocale n\'est pas inondée : les valeurs se relisent, sans zone annoncée', async ({ page }) => {
    await start(page);
    const updates = await page.evaluate(async () => {
      let changes = 0;
      const gauge = document.querySelector('[data-testid=gauge-rpm]')!;
      const observer = new MutationObserver((records) => (changes += records.length));
      observer.observe(gauge, { attributes: true, attributeFilter: ['aria-valuetext'] });
      await new Promise((resolve) => setTimeout(resolve, 2100));
      observer.disconnect();
      return changes;
    });
    expect(updates).toBeLessThanOrEqual(4);
    await expect(page.getByTestId('gauge-rpm')).toHaveAttribute('role', 'meter');
    await expect(page.getByTestId('gauge-rpm')).toHaveAttribute('aria-valuetext', /\d+ tr\/min/);
    await expect(page.locator('.rail [aria-live="polite"], .rail [aria-live="assertive"], .cockpit [aria-live="polite"]')).toHaveCount(0);
  });

  test('les chiffres des jauges suivent la langue, même sans changer de valeur', async ({ page }) => {
    await open(page);
    // Moteur coupé, le chiffre ne bouge pas : c'est la langue seule qui doit le réécrire.
    await expect(page.getByTestId('gauge-manifold-value')).toHaveText('0,00');
    await ouvrir(page, 'display');
    await page.getByTestId('lang-en').click();
    await expect(page.getByTestId('gauge-manifold-value')).toHaveText('0.00');
  });
});

test.describe('toutes les largeurs', () => {
  for (const [width, height] of [[360, 740], [390, 844], [768, 1024], [1280, 720], [1440, 900], [1920, 1080], [2560, 1440]]) {
    test(`aucun défilement horizontal à ${width} px, volets ouverts compris`, async ({ page }) => {
      await page.setViewportSize({ width, height });
      await start(page);
      const overflow = () => page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      expect(await overflow()).toBeLessThanOrEqual(0);
      for (const id of PANELS) {
        await ouvrir(page, id);
        expect(await overflow()).toBeLessThanOrEqual(0);
        const box = (await page.getByTestId(`panel-${id}`).boundingBox())!;
        expect(box.x).toBeGreaterThanOrEqual(0);
        expect(box.x + box.width).toBeLessThanOrEqual(width);
        expect(box.y + box.height).toBeLessThanOrEqual(height);
      }
    });
  }

  test('sur un téléphone, rien ne couvre le démarrage ni la pédale, quel que soit le volet', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await start(page);
    const onTop = (locator: Locator) => locator.evaluate((node) => {
      const box = node.getBoundingClientRect();
      const top = document.elementFromPoint(box.left + box.width / 2, box.top + box.height / 2);
      return !!top && (node === top || node.contains(top));
    });
    const pedal = page.getByRole('button', { name: 'Accélérateur' });
    const stop = page.getByTestId('start');
    expect(await onTop(pedal)).toBe(true);
    for (const id of PANELS) {
      await ouvrir(page, id);
      expect(await onTop(pedal), id).toBe(true);
      expect(await onTop(stop), id).toBe(true);
      const box = (await page.getByTestId(`panel-${id}`).boundingBox())!;
      const dock = (await pedal.boundingBox())!;
      expect(box.y + box.height, id).toBeLessThanOrEqual(dock.y);
      await page.keyboard.press('Escape');
    }
    // En manuelle aussi, avec trois pédales et le levier.
    await page.getByRole('button', { name: 'Conduite', exact: true }).click();
    await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
    await ouvrir(page, 'engine');
    for (const name of ['Accélérateur', 'Frein', 'Embrayage']) expect(await onTop(page.locator(`button.pedal[aria-label="${name}"]`)), name).toBe(true);
    await expect(page.getByTestId('shift-up')).toBeInViewport();
    const box = (await page.getByTestId('panel-engine').boundingBox())!;
    expect(box.y + box.height).toBeLessThanOrEqual((await page.getByTestId('shift-up').boundingBox())!.y);
  });

  test('sur un téléphone, le compte-tours et la voiture restent en haut, volet ouvert', async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await start(page);
    await ouvrir(page, 'engine');
    const box = (await page.getByTestId('panel-engine').boundingBox())!;
    const dial = (await page.getByTestId('gauge-rpm').boundingBox())!;
    expect(box.height).toBeGreaterThan(280);
    expect(dial.y).toBeLessThan(box.y);
  });

  test('à 1280 × 720, le démarrage et la pédale sont visibles sans défiler', async ({ page }) => {
    await page.setViewportSize({ width: 1280, height: 720 });
    await open(page);
    await expect(page.getByRole('button', { name: 'Accélérateur' })).toBeInViewport({ ratio: 1 });
    await expect(page.getByTestId('start')).toBeInViewport({ ratio: 1 });
    await expect(page.getByTestId('car-stage')).toBeInViewport({ ratio: 1 });
  });
});
