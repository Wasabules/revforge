import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/* eslint-disable @typescript-eslint/no-explicit-any */
/**
 * Glisser un curseur ne fige ni la page ni le son. Sur le thread de la page, la fiche coûte de 20 à 120 ms, et son 0 à 100 de 0,1
 * à 0,6 s : calculés à chaque mouvement d'un curseur, ils retenaient chaque mouvement d'autant (jusqu'à 0,9 s d'écran figé d'un coup
 * sur un V8 qui patine). Refaire la 3D du moteur en coûte de 50 à 180. Sur le thread du son, chaque réglage reconstruisait trois
 * fois le modèle de conduite, jusqu'à 28 ms chaque fois, pour 2,7 ms par bloc. Tout cela attend que le curseur s'arrête, et le
 * 0 à 100 se calcule dans un worker.
 */

/**
 * Trente mouvements de curseur, un par image : ce que retient chaque mouvement, la plus longue tâche de la page pendant les
 * mouvements, puis jusqu'au relâchement, et la course que dessine la vue du moteur avant et juste après les mouvements.
 */
async function glisser(page: Page, label: string, from: number, to: number) {
  return page.evaluate(
    async ({ label, from, to }) => {
      const tag = [...document.querySelectorAll('label')].find((el) => el.textContent?.trim() === label && (el as HTMLElement).offsetParent !== null) as HTMLLabelElement;
      const slider = document.getElementById(tag.htmlFor) as HTMLInputElement;
      const view = (document.querySelector('[data-testid=engine-view]') as any)?.__engine;
      const strokeBefore = view?.layout()?.stroke;
      const tasks: number[] = [];
      const observer = new PerformanceObserver((list) => list.getEntries().forEach((entry) => tasks.push(entry.duration)));
      observer.observe({ type: 'longtask', buffered: false });
      const held: number[] = [];
      const steps = 30;
      const started = performance.now();
      for (let i = 0; i <= steps; i++) {
        const start = performance.now();
        slider.value = String(from + ((to - from) * i) / steps);
        slider.dispatchEvent(new Event('input', { bubbles: true }));
        // La page se met à jour au bout de la file des microtâches : ce qui s'y calcule compte aussi.
        await Promise.resolve();
        held.push(performance.now() - start);
        await new Promise((resolve) => setTimeout(resolve, 16));
      }
      const moving = { ms: performance.now() - started, longest: Math.max(0, ...tasks), stroke: view?.layout()?.stroke };
      // Le curseur s'arrête : le temps que les tâches en cours finissent, puis on le relâche.
      await new Promise((resolve) => setTimeout(resolve, 300));
      observer.disconnect();
      slider.dispatchEvent(new Event('change', { bubbles: true }));
      return { held: Math.max(...held), moving, longest: Math.max(0, ...tasks), strokeBefore, pending: document.querySelector('[data-testid="spec-100"]')?.textContent };
    },
    { label, from, to },
  );
}

test('glisser la masse ne fige pas la page : la fiche attend le curseur, le 0 à 100 se calcule à part', async ({ page }) => {
  test.setTimeout(60_000);
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  // Un V8 propulsion qui patine : son 0 à 100 est celui de l'essayeur, jusqu'à neuf départs.
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('v8_cross_plane');
  const sprint = page.getByTestId('spec-100');
  await expect(sprint).not.toHaveText('…');
  const before = Number((await sprint.textContent())!.replace(/[^0-9,]/g, '').replace(',', '.'));
  expect(before).toBeGreaterThan(3);

  await ouvrir(page, 'car');
  const result = await glisser(page, 'Masse', 1600, 2400);
  // Rien de lourd dans le geste lui-même.
  expect(result.held).toBeLessThan(50);
  // Aucune image retenue un dixième de seconde pendant le glissé (avant : de 0,6 à 0,9 s d'un coup).
  expect(result.longest).toBeLessThan(150);
  // Pendant le glissé, le 0 à 100 n'est pas celui d'un autre réglage : il attend.
  expect(result.pending).toBe('…');

  // Relâché : la fiche suit la voiture plus lourde, et le 0 à 100 arrive.
  await expect(sprint).not.toHaveText('…');
  const after = Number((await sprint.textContent())!.replace(/[^0-9,]/g, '').replace(',', '.'));
  expect(after).toBeGreaterThan(before);
  const mass = await page.evaluate(() => (window as unknown as { __revforge: { config(): { massKg: number } } }).__revforge.config().massKg);
  expect(mass).toBe(2400);
});

test('glisser la cylindrée ne charge pas le thread du son : le moteur y est reconstruit une fois, à l\'arrêt du curseur', async ({ page }) => {
  test.setTimeout(60_000);
  // Le worklet chronomètre chaque message qu'il reçoit et le dit à la page : c'est le temps pris au thread du son. Playwright ne
  // voit pas le chargement d'un worklet : on lui passe une copie chronométrée.
  await page.addInitScript(() => {
    const original = AudioWorklet.prototype.addModule;
    AudioWorklet.prototype.addModule = async function (url: string | URL, options?: WorkletOptions) {
      const source = await (await fetch(url)).text();
      const timed = source.replace(
        'this.port.onmessage = ({ data }) => this.onCommand(data);',
        "this.port.onmessage = ({ data }) => { const t0 = Date.now(); this.onCommand(data); this.port.postMessage({ type: 'cost', command: data.type, ms: Date.now() - t0, cc: data.args?.[2] }); };",
      );
      if (timed === source) throw new Error('worklet : rien à chronométrer');
      return original.call(this, URL.createObjectURL(new Blob([timed], { type: 'text/javascript' })), options);
    };
  });
  // Un 2 L turbo dont le calculateur régule la pression : chaque reconstruction refait son banc d'étalonnage.
  await page.goto('./?test#m=4.inline.1984.83.6500.sport.turbo.petrol.0.off.dump_valve~compact.1400.18.all.1.3~~bc=electronic');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 10_000 }).toBeGreaterThan(500);
  await page.evaluate(() => {
    const port = (window as any).__revforge.engine.node.port as MessagePort;
    const costs: { command: string; ms: number; cc?: number }[] = [];
    (window as any).__costs = costs;
    port.addEventListener('message', ({ data }) => data.type === 'cost' && costs.push(data));
  });
  // Le démarrage a bien été chronométré : sans cela, ce test ne verrait rien.
  await page.evaluate(() => (window as any).__revforge.engine.setConfig({ ...(window as any).__revforge.config(), massKg: 1401 }));
  await expect.poll(() => page.evaluate(() => (window as any).__costs.length)).toBeGreaterThan(0);
  await page.evaluate(() => (window as any).__revforge.engine.setConfig((window as any).__revforge.config()));
  await page.waitForTimeout(300);
  await page.evaluate(() => ((window as any).__costs.length = 0));

  await ouvrir(page, 'engine');
  await glisser(page, 'Cylindrée', 2000, 2600);
  await page.waitForTimeout(500);
  const costs: { command: string; ms: number; cc?: number }[] = await page.evaluate(() => (window as any).__costs);
  const rebuilds = costs.filter((cost) => ['car', 'gearbox', 'config', 'electric'].includes(cost.command));
  const spent = rebuilds.reduce((sum, cost) => sum + cost.ms, 0);
  console.log(`thread du son pendant le glissé : ${rebuilds.length} reconstructions, ${spent} ms`);
  // Une seule, à l'arrêt du curseur, du seul moteur : ni la voiture ni la boîte n'ont changé (avant : 96, 2,7 s pour 0,5 s de geste).
  expect(rebuilds.map((cost) => cost.command)).toEqual(['config']);
  expect(rebuilds[0].cc).toBe(2600);
  expect(spent).toBeLessThan(200);
  expect(await page.evaluate(() => (window as any).__revforge.config().displacementCc)).toBe(2600);
});

test('glisser la cylindrée, vue 3D du moteur à l\'écran, ne fige pas la page : ses pièces attendent l\'arrêt du curseur', async ({ page }) => {
  test.setTimeout(60_000);
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await page.getByTestId('engine-view').scrollIntoViewIfNeeded();
  await page.waitForFunction(() => {
    const d = (document.querySelector('[data-testid=engine-view]') as any)?.__engine;
    return d && d.live3d() && d.scene() && d.drawn > 0;
  }, null, { timeout: 30_000 });
  await page.evaluate(() => ((window as any).__sceneBefore = (document.querySelector('[data-testid=engine-view]') as any).__engine.scene()));

  await ouvrir(page, 'engine');
  const from = await page.evaluate(() => (window as any).__revforge.config().displacementCc);
  const result = await glisser(page, 'Cylindrée', from, from + 600);
  console.log(`vue 3D : ${Math.round(result.moving.ms)} ms pour trente mouvements, tâche la plus longue ${Math.round(result.moving.longest)} ms`);
  expect(result.held).toBeLessThan(50);
  // Pendant les mouvements, la 3D garde ses pièces, et aucune image n'est retenue (avant : refaite à chaque mouvement, douze tâches
  // jusqu'à 184 ms, 2,5 s de geste pour 0,5 s). La durée du geste n'est qu'affichée : un autre test en parallèle l'allonge sans
  // qu'aucune tâche de la page ne soit longue.
  expect(result.moving.stroke).toBe(result.strokeBefore);
  expect(result.moving.longest).toBeLessThan(150);

  // Arrêté puis relâché : la course est celle de la nouvelle cylindrée, et la scène a été refaite.
  await page.waitForFunction((stroke) => {
    const d = (document.querySelector('[data-testid=engine-view]') as any).__engine;
    return d.layout().stroke > stroke * 1.2 && d.scene() !== (window as any).__sceneBefore;
  }, result.strokeBefore, { timeout: 15_000 });
});
