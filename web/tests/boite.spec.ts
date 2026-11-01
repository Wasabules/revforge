import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';

declare global {
  interface Window {
    __revforge: {
      engine: { level(): number };
      telemetry(): { rpm: number; speed: number; gear: number; peak: number; torque: number; power: number; clutchInputRpm: number; outputRpm: number; meshed: number; shiftStep: number; grinding: number; overload: number; boxBroken: number };
      spec(): { idle: number; redline: number; topSpeed: number; zeroTo100: number; peakTorque: number; box: { kind: number; limit: number; finalDrive: number; finalPinion: number; finalRing: number; reverseOverall: number; gears: { gear: number; driver: number; driven: number; ratio: number; overall: number; topKmh: number; meshHz: number; contact: number }[] } };
    };
  }
}

const spec = (page: Page) => page.evaluate(() => window.__revforge.spec());
const telemetry = (page: Page) => page.evaluate(() => window.__revforge.telemetry());
const panel = (page: Page) => page.getByTestId('gearbox-panel');

async function open(page: Page, hash = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.goto(`./?test${hash}`);
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect.poll(() => spec(page).then((s) => s.box.gears.length)).toBeGreaterThan(0);
  await ouvrir(page, 'gearbox');
  return errors;
}

async function drive(page: Page, manual: boolean) {
  await page.getByTestId('start').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 10000 }).toBeGreaterThan(500);
  await page.getByRole('button', { name: 'Conduite', exact: true }).click();
  if (manual) await page.getByRole('button', { name: 'Manuelle', exact: true }).click();
}

async function choose(page: Page, name: string) {
  await ouvrir(page, 'gearbox');
  await panel(page).getByRole('button', { name, exact: true }).click();
}

test('la fiche de la boîte vient de dents entières, et le tableau en découle', async ({ page }) => {
  const errors = await open(page);
  const s = await spec(page);
  for (const g of s.box.gears) {
    expect(Number.isInteger(g.driver) && Number.isInteger(g.driven)).toBe(true);
    expect(g.overall).toBeCloseTo(g.ratio * s.box.finalDrive, 2);
    // Vitesse au régime maximal : démultiplication et rayon de roue, rien d'autre.
    expect(g.topKmh).toBeGreaterThan(20);
  }
  expect(s.box.finalDrive).toBeCloseTo(s.box.finalRing / s.box.finalPinion, 4);
  const first = s.box.gears[0];
  await expect(page.getByTestId('box-table')).toContainText(`${first.driver}/${first.driven}`);
  await expect(page.getByTestId('box-top-1')).toContainText(`${Math.round(first.topKmh)} km/h`);
  await expect(page.getByTestId('gears-saw')).toBeAttached();
  expect(errors).toEqual([]);
});

test('une boîte choisie ne dépend plus du moteur : mêmes rapports sur deux préréglages', async ({ page }) => {
  await open(page);
  await page.getByTestId('box-model').selectOption('honda_s2000');
  const onFirst = await spec(page);
  expect(onFirst.box.gears.length).toBe(6);
  expect(onFirst.box.gears[0].ratio).toBeCloseTo(3.133, 0);
  // Sélectionner un autre moteur reprend sa boîte : c'est en réglant la cylindrée que le moteur change seul.
  const firstOverall = onFirst.box.gears.map((g) => g.overall);
  await ouvrir(page, 'engine');
  await page.getByRole('slider', { name: 'Cylindrée' }).fill('5000');
  await expect.poll(() => spec(page).then((s) => s.peakTorque)).toBeGreaterThan(onFirst.peakTorque * 1.5);
  const onSecond = await spec(page);
  expect(onSecond.box.gears.map((g) => g.overall)).toEqual(firstOverall);
  // Le couple admissible, lui non plus, n'a pas bougé : la fiche prévient.
  await ouvrir(page, 'gearbox');
  await expect(page.getByTestId('box-too-weak')).toBeVisible();
});

test('un pont trop long ou trop court change les performances calculées', async ({ page }) => {
  await open(page);
  const reference = await spec(page);
  await choose(page, 'Choisi');
  const slider = panel(page).getByRole('slider', { name: 'Rapport de pont' });
  await slider.fill('7');
  await expect.poll(() => spec(page).then((s) => s.box.finalDrive)).toBeGreaterThan(reference.box.finalDrive * 1.2);
  const short = await spec(page);
  expect(short.topSpeed).toBeLessThan(reference.topSpeed);
  await expect(page.getByTestId('box-top-1')).not.toContainText(`${Math.round(reference.box.gears[0].topKmh)} km/h`);
});

test('la denture droite supprime le recouvrement, et se retrouve dans le tableau', async ({ page }) => {
  await open(page);
  const before = await spec(page);
  await choose(page, 'Droite');
  await expect.poll(() => spec(page).then((s) => s.box.gears[0].contact)).toBeLessThan(before.box.gears[0].contact);
  await expect(page.getByTestId('box-summary')).toContainText('denture droite');
});

test('le lien de partage garde la boîte, et les anciens liens prennent celle par défaut', async ({ page }) => {
  await open(page);
  await choose(page, 'Séquentielle');
  await choose(page, 'Droite');
  await choose(page, 'Rapprochés');
  const hash = await page.evaluate(() => location.hash);
  expect(hash.split('~').length).toBe(3);
  // Une vraie ouverture : changer seulement le fragment ne relit pas le lien.
  await page.goto('about:blank');
  await page.goto(`./?test${hash}`);
  await expect.poll(() => spec(page).then((s) => s.box.kind)).toBe(2);
  await ouvrir(page, 'gearbox');
  await expect(panel(page).getByRole('button', { name: 'Droite', exact: true })).toHaveAttribute('aria-pressed', 'true');
  await expect(panel(page).getByRole('button', { name: 'Rapprochés', exact: true })).toHaveAttribute('aria-pressed', 'true');

  // Un ancien lien, sans groupe de boîte, ni même de voiture.
  await page.goto('about:blank');
  await page.goto('./?test#m=4.inline.2000.84.6800.stock.natural.petrol.0.off.dump_valve');
  await expect.poll(() => spec(page).then((s) => s.box.gears.length)).toBeGreaterThan(0);
  await ouvrir(page, 'gearbox');
  await expect(panel(page).getByRole('button', { name: 'À synchros', exact: true })).toHaveAttribute('aria-pressed', 'true');
  await expect(panel(page).getByRole('button', { name: 'Hélicoïdale', exact: true })).toHaveAttribute('aria-pressed', 'true');
});

test('sur mesure : on règle chaque rapport, et le passage en sur mesure ne saute pas', async ({ page }) => {
  await open(page);
  const before = await spec(page);
  await choose(page, 'Sur mesure');
  const after = await spec(page);
  after.box.gears.forEach((g, i) => expect(g.ratio).toBeCloseTo(before.box.gears[i].ratio, 1));
  await panel(page).getByRole('slider', { name: 'Rapport 2' }).fill('2.6');
  await expect.poll(() => spec(page).then((s) => s.box.gears[1].ratio)).toBeGreaterThan(2.45);
});

test('la vue de la boîte tourne au ralenti et décrit le rapport engagé', async ({ page }) => {
  await open(page);
  await drive(page, false);
  await voir(page, 'gearbox');
  await page.getByTestId('gearbox-view').scrollIntoViewIfNeeded();
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 8000 }).toBeGreaterThanOrEqual(1);
  await expect.poll(() => telemetry(page).then((t) => t.outputRpm), { timeout: 8000 }).toBeGreaterThan(100);
  await page.keyboard.up('Space');
  await expect(page.getByTestId('gearbox-caption')).toContainText('dents');
  await expect(page.getByTestId('gearbox-caption')).toContainText('primaire');
  await expect(page.getByTestId('gearbox-view')).toHaveAttribute('aria-label', /Vue animée de la boîte/);
  const painted = await page.evaluate(() => {
    const canvas = document.querySelector('[data-testid="gearbox-view"] canvas') as HTMLCanvasElement;
    const data = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height).data;
    let filled = 0;
    for (let i = 3; i < data.length; i += 4) if (data[i] > 0) filled++;
    return filled;
  });
  expect(painted).toBeGreaterThan(500);
});

test('marche arrière : sous le point mort, le rapport passe à R et la voiture recule', async ({ page }) => {
  await open(page);
  await drive(page, true);
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await page.keyboard.press('ArrowLeft');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 3000 }).toBe(-1);
  await expect(page.getByTestId('manual-gear')).toHaveText('R');
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.speed), { timeout: 8000 }).toBeGreaterThan(3);
  await page.keyboard.up('Space');
});

test('un V8 sur une boîte trop faible la casse : on le voit, et on la remplace', async ({ page }) => {
  await open(page);
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('v8_cross_plane');
  await choose(page, 'Choisie');
  await panel(page).getByRole('slider', { name: 'Couple admissible' }).fill('120');
  await expect(page.getByTestId('box-too-weak')).toBeVisible();
  await drive(page, false);
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await page.keyboard.down('Space');
  await expect(page.getByTestId('box-broken')).toBeVisible({ timeout: 15000 });
  await page.keyboard.up('Space');
  await page.getByTestId('box-repair').click();
  await expect(page.getByTestId('box-broken')).toHaveCount(0);
});

test('au pied, rétrograder sans débrayer gratte', async ({ page }) => {
  await open(page);
  await drive(page, true);
  await page.getByRole('button', { name: 'Au pied', exact: true }).click();
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
  await page.keyboard.press('KeyC');
  await page.keyboard.down('KeyC');
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => telemetry(page).then((t) => t.meshed), { timeout: 3000 }).toBe(1);
  await page.keyboard.up('KeyC');
  await page.keyboard.down('Space');
  await expect.poll(() => telemetry(page).then((t) => t.rpm), { timeout: 8000 }).toBeGreaterThan(3000);
  await expect.poll(() => telemetry(page).then((t) => t.clutchInputRpm), { timeout: 8000 }).toBeGreaterThan(2500);
  await page.keyboard.down('KeyC');
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => telemetry(page).then((t) => t.gear), { timeout: 3000 }).toBe(2);
  await expect.poll(() => telemetry(page).then((t) => t.meshed), { timeout: 3000 }).toBe(1);
  await page.keyboard.up('KeyC');
  await expect.poll(() => telemetry(page).then((t) => t.clutchInputRpm), { timeout: 8000 }).toBeGreaterThan(3000);
  await page.keyboard.up('Space');
  await page.keyboard.press('ArrowLeft');
  await expect.poll(() => telemetry(page).then((t) => t.grinding), { timeout: 3000 }).toBeGreaterThan(5);
  await expect(page.getByTestId('box-grinding')).toBeVisible({ timeout: 3000 });
});
