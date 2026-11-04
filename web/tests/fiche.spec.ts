import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';

/**
 * La fiche technique ne fige pas la page. Sur le thread de la page, elle coûte de 20 à 120 ms, et son 0 à 100 de 0,1 à 0,6 s :
 * calculés à chaque mouvement d'un curseur, ils retenaient chaque mouvement d'autant (jusqu'à 0,9 s d'écran figé d'un coup sur un
 * V8 qui patine). La fiche attend que le curseur s'arrête, et le 0 à 100 se calcule dans un worker.
 */

/** Trente mouvements de curseur, un par image : ce que retient chaque mouvement, et la plus longue tâche de la page entre-temps. */
async function glisser(page: Page, label: string, from: number, to: number) {
  return page.evaluate(
    async ({ label, from, to }) => {
      const tag = [...document.querySelectorAll('label')].find((el) => el.textContent?.trim() === label && (el as HTMLElement).offsetParent !== null) as HTMLLabelElement;
      const slider = document.getElementById(tag.htmlFor) as HTMLInputElement;
      const tasks: number[] = [];
      const observer = new PerformanceObserver((list) => list.getEntries().forEach((entry) => tasks.push(entry.duration)));
      observer.observe({ type: 'longtask', buffered: false });
      const held: number[] = [];
      const steps = 30;
      for (let i = 0; i <= steps; i++) {
        const start = performance.now();
        slider.value = String(from + ((to - from) * i) / steps);
        slider.dispatchEvent(new Event('input', { bubbles: true }));
        // La page se met à jour au bout de la file des microtâches : ce qui s'y calcule compte aussi.
        await Promise.resolve();
        held.push(performance.now() - start);
        await new Promise((resolve) => setTimeout(resolve, 16));
      }
      // Le curseur s'arrête : le temps que les tâches en cours finissent, puis on le relâche.
      await new Promise((resolve) => setTimeout(resolve, 300));
      observer.disconnect();
      slider.dispatchEvent(new Event('change', { bubbles: true }));
      return { held: Math.max(...held), longest: Math.max(0, ...tasks), pending: document.querySelector('[data-testid="spec-100"]')?.textContent };
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
  // Le son reçoit chaque mouvement sans attendre : rien de lourd dans le geste lui-même.
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
