import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir } from './aide';
import { digitColumns } from '../src/lib/digits';
import { GasFlow } from '../src/lib/fluid';
import { GasPainter } from '../src/lib/fluidRender';
import { contrast, heated, heatColors, heatRgb, heatTextRgb, HEAT_MAX_C, HEAT_MIN_C, HEAT_STOPS, inkOn, INK_DARK, INK_LIGHT, luminance, NO_EXHAUST, parseHex, RAISED, SURFACE, GROUND, updateHeat, wallCelsius } from '../src/lib/heat';
import type { Theme } from '../src/lib/canvas';

/** Le thème suit la température de la ligne : ce qui compte est écrit dans le fichier `heat.ts`, on le vérifie ici et dans la page. */

/** La maquette, recopiée ici : modifier l'échelle du site doit se voir. */
const MOCKUP: [number, [number, number, number]][] = [[80, [74, 62, 54]], [350, [122, 30, 16]], [550, [196, 52, 16]], [750, [255, 106, 26]], [900, [255, 170, 66]], [1000, [255, 226, 170]]];
const COLD = '#4a3e36';
/** Les couleurs telles qu'elles sont écrites dans la feuille de style. */
const css = readFileSync(new URL('../src/app.css', import.meta.url), 'utf8');
const token = (name: string) => css.match(new RegExp(`${name}:\\s*(#[0-9a-f]{6})`))![1];
const WIDTHS = [[360, 740], [390, 844], [1280, 720], [1440, 900], [1920, 1080], [2560, 1440]] as const;

/** Toute l'échelle, degré par degré. */
const everyDegree = Array.from({ length: HEAT_MAX_C - HEAT_MIN_C + 1 }, (_, i) => HEAT_MIN_C + i);

async function open(page: Page) {
  await page.goto('/?test');
  await expect(page.getByTestId('start')).toBeEnabled({ timeout: 30_000 });
}

/** La couleur que porte réellement la barre du haut. */
const barHeat = (page: Page) => page.locator('[data-sticky-bar]').evaluate((node) => getComputedStyle(node).getPropertyValue('--heat').trim());
const red = (hex: string) => parseHex(hex)[0];

async function startEngine(page: Page, turning = 500) {
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(turning);
  // Espace active encore un bouton sélectionné : la pédale, c'est sans focus.
  await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
}

/** À poser avant de charger la page : compte chaque écriture de la chaleur, même celles qui ne changeraient rien. */
async function spyOnWrites(page: Page) {
  await page.addInitScript(() => {
    const original = CSSStyleDeclaration.prototype.setProperty;
    const counts = new WeakMap<CSSStyleDeclaration, number>();
    CSSStyleDeclaration.prototype.setProperty = function (this: CSSStyleDeclaration, name: string, ...rest: [string | null, string?]) {
      if (name === '--heat') counts.set(this, (counts.get(this) ?? 0) + 1);
      return original.call(this, name, ...rest);
    };
    (window as any).__heatWrites = (element: HTMLElement) => counts.get(element.style) ?? 0;
  });
}

/** Combien de fois la chaleur a été écrite sur la barre du haut pendant ce temps. */
async function countWrites(page: Page, ms: number) {
  return page.evaluate(async (ms) => {
    const bar = document.querySelector<HTMLElement>('[data-sticky-bar]')!;
    const before = (window as any).__heatWrites(bar);
    await new Promise((resolve) => setTimeout(resolve, ms));
    return (window as any).__heatWrites(bar) - before;
  }, ms);
}

test.describe('la couleur de la chaleur', () => {
  test('passe par les paliers de la maquette', () => {
    for (const [celsius, rgb] of MOCKUP) expect(heatRgb(celsius), `${celsius} °C`).toEqual(rgb);
    expect(HEAT_STOPS).toHaveLength(MOCKUP.length);
    // Hors de l'échelle, elle s'arrête à ses bouts.
    expect(heatRgb(20)).toEqual(heatRgb(80));
    expect(heatRgb(1500)).toEqual(heatRgb(1000));
  });

  test('ne fait que se réchauffer', () => {
    let last = heatRgb(HEAT_MIN_C);
    for (const celsius of everyDegree.slice(1)) {
      const now = heatRgb(celsius);
      // Le rouge monte toujours ; le vert baisse d'abord, le temps que le gris devienne cerise, mais rouge et vert ensemble montent.
      expect(now[0], `${celsius} °C`).toBeGreaterThanOrEqual(last[0]);
      expect(now[0] + now[1], `${celsius} °C`).toBeGreaterThanOrEqual(last[0] + last[1]);
      // Dès la cerise, chaque degré éclaire davantage.
      if (celsius > 350) expect(luminance(now), `${celsius} °C`).toBeGreaterThan(luminance(last));
      last = now;
    }
  });

  test('le texte reste à 4,5:1 sur tous les fonds, sur toute l\'échelle', () => {
    expect([GROUND, SURFACE, RAISED]).toEqual([token('--bg'), token('--surface'), token('--surface-raised')]);
    for (const ground of [GROUND, SURFACE, RAISED]) {
      for (const celsius of everyDegree) {
        expect(contrast(heatTextRgb(celsius), parseHex(ground)), `${celsius} °C sur ${ground}`).toBeGreaterThanOrEqual(4.5);
        // Ce qui est réellement écrit dans la page, arrondi à la couleur près.
        expect(contrast(parseHex(heatColors(celsius).text), parseHex(ground)), `${celsius} °C sur ${ground}`).toBeGreaterThanOrEqual(4.5);
      }
      expect(contrast(parseHex(NO_EXHAUST.text), parseHex(ground))).toBeGreaterThanOrEqual(4.5);
    }
  });

  test('l\'encre posée sur la chaleur reste à 4,5:1 sur toute l\'échelle', () => {
    for (const celsius of everyDegree) {
      expect(contrast(heatRgb(celsius), parseHex(inkOn(heatRgb(celsius)))), `${celsius} °C`).toBeGreaterThanOrEqual(4.5);
      const { fill, ink } = heatColors(celsius);
      expect(contrast(parseHex(fill), parseHex(ink)), `${celsius} °C`).toBeGreaterThanOrEqual(4.5);
    }
    // Claire sur le métal sombre, sombre sur l'orange vif.
    expect(heatColors(80).ink).toBe(INK_LIGHT);
    expect(heatColors(900).ink).toBe(INK_DARK);
    expect(contrast(parseHex(NO_EXHAUST.fill), parseHex(NO_EXHAUST.ink))).toBeGreaterThanOrEqual(4.5);
  });

  test('le texte discret reste à 4,5:1 sur tous les fonds', () => {
    // Le bouton survolé, le plus clair, porte encore du texte discret.
    for (const ground of [token('--bg'), token('--surface'), token('--surface-raised'), token('--surface-high')]) {
      for (const name of ['--text', '--text-dim', '--text-quiet']) expect(contrast(parseHex(token(name)), parseHex(ground)), `${name} sur ${ground}`).toBeGreaterThanOrEqual(4.5);
    }
    // Et le contrôle choisi, plein et neutre.
    expect(contrast(parseHex(token('--on-control')), parseHex(token('--control')))).toBeGreaterThanOrEqual(4.5);
  });

  test('la température d\'une paroi est celle que le gaz lit', () => {
    expect(wallCelsius(0)).toBe(20);
    expect(wallCelsius(1)).toBe(740);
  });

  test('le gaz et le thème disent la même couleur à température égale', () => {
    const theme: Theme = { text: '#efe9df', dim: '#c9c0b3', line: '#2c2620', lineStrong: '#4a4136', surface: '#15110e', raised: '#1f1a16', bg: '#0c0a09', accent: '#ff6a1a', danger: '#ef5350', cool: '#5aa9e6', metal: '#8a8075' };
    const painter = new GasPainter(new GasFlow());
    const rgb = [0, 0, 0];
    for (const celsius of [100, 200, 350, 550, 750, 900, 1000]) {
      painter.gasRgb(theme, celsius + 273.15, 1, rgb);
      const wanted = heatRgb(celsius);
      // Le gaz a 64 niveaux sur 1057 K : à un niveau près.
      for (let c = 0; c < 3; c++) expect(Math.abs(rgb[c] - wanted[c]), `${celsius} °C, canal ${c}`).toBeLessThan(10);
    }
  });

  test('chaque chiffre garde sa colonne, quel qu\'il soit', () => {
    // Big Shoulders n'a pas de chiffres tabulaires : un 1 et un 8 occupent la même place.
    expect(digitColumns('1111', 100, 40)).toBe(digitColumns('8888', 100, 40));
    const columns = digitColumns('12', 100, 40).split(' ').map(Number);
    expect((columns[0] + columns[1]) / 2).toBeCloseTo(100, 0);
    expect(digitColumns('1,25', 0, 40).split(' ')).toHaveLength(4);
  });
});

test.describe('le rythme d\'écriture', () => {
  /** Un faux élément : `updateHeat` n'a besoin que de son style, et l'horloge est celle qu'on lui donne. */
  function fakeTarget() {
    let writes = 0;
    const element = { style: { setProperty: (name: string) => { if (name === '--heat') writes++; } } } as unknown as HTMLElement;
    const stop = heated(element);
    return { count: () => writes, reset: () => (writes = 0), stop: stop.destroy };
  }
  // Une horloge qui ne revient jamais en arrière d'un test à l'autre : le module garde sa prochaine lecture.
  let clock = 1e6;

  test('huit fois par seconde au plus, même si la température saute à chaque image', () => {
    const target = fakeTarget();
    target.reset();
    for (let frame = 0; frame < 60; frame++) updateHeat((clock += 1000 / 60), 100 + frame * 15, false);
    target.stop();
    expect(target.count()).toBeGreaterThan(3);
    expect(target.count()).toBeLessThanOrEqual(9);
  });

  test('deux fois par seconde au plus quand on demande moins d\'animation', () => {
    const target = fakeTarget();
    target.reset();
    clock += 1000;
    for (let frame = 0; frame < 60; frame++) updateHeat((clock += 1000 / 60), 100 + frame * 15, true);
    target.stop();
    expect(target.count()).toBeLessThanOrEqual(3);
  });

  test('une température qui tremble ne fait rien changer', () => {
    clock += 1000;
    updateHeat((clock += 1000), 500, false);
    const target = fakeTarget();
    target.reset();
    for (let frame = 0; frame < 120; frame++) updateHeat((clock += 1000 / 60), 500 + (frame % 2 ? 4 : -4), false);
    target.stop();
    expect(target.count()).toBe(0);
  });
});

test.describe('le thème dans la page', () => {
  test('moteur coupé : la couleur froide, et aucune écriture', async ({ page }) => {
    await spyOnWrites(page);
    await open(page);
    expect(await barHeat(page)).toBe(COLD);
    expect(await page.locator('h1 span').evaluate((node) => getComputedStyle(node).color)).toBe(`rgb(${parseHex(heatColors(HEAT_MIN_C).text).join(', ')})`);
    expect(await countWrites(page, 1500)).toBe(0);
  });

  test('pied au plancher, la page chauffe ; moteur coupé, elle refroidit', async ({ page }) => {
    await open(page);
    await startEngine(page);
    await page.keyboard.down('Space');
    await expect.poll(async () => red(await barHeat(page)), { timeout: 20_000, intervals: [500] }).toBeGreaterThan(200);
    const hot = await barHeat(page);
    await page.keyboard.up('Space');
    await page.getByTestId('start').click();
    await expect.poll(async () => red(await barHeat(page)), { timeout: 25_000, intervals: [500] }).toBeLessThan(red(hot) - 60);
    // La plaque du volet ouvert suit la même chaleur.
    await page.getByTestId('open-display').click();
    // Lues d'un coup : la couleur continue de baisser.
    const { plate, bar } = await page.getByTestId('open-display').evaluate((node) => ({
      plate: getComputedStyle(node, '::before').backgroundColor,
      bar: getComputedStyle(document.querySelector('[data-sticky-bar]')!).getPropertyValue('--heat').trim(),
    }));
    expect(plate).toBe(`rgb(${parseHex(bar).join(', ')})`);
  });

  test('l\'arc du compte-tours et les chiffres suivent la chaleur', async ({ page }) => {
    await open(page);
    await startEngine(page);
    const arc = () => page.getByTestId('gauge-rpm').locator('path.value').evaluate((node) => getComputedStyle(node).stroke);
    const cold = await arc();
    await page.keyboard.down('Space');
    await expect.poll(arc, { timeout: 15_000, intervals: [500] }).not.toBe(cold);
    await page.keyboard.up('Space');
  });

  test('la couleur ne dépend pas de la vue affichée, ni de ce qui est à l\'écran', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await open(page);
    await startEngine(page);
    // Plus aucune jauge ni vue à l'écran, plus rien qui lise la simulation : seule reste la barre du haut.
    await page.addStyleTag({ content: 'main, .analysis { display: none !important; }' });
    await page.keyboard.down('Space');
    await expect.poll(async () => red(await barHeat(page)), { timeout: 20_000, intervals: [500] }).toBeGreaterThan(160);
    await page.keyboard.up('Space');
    await expect(page.getByTestId('view-gas')).toHaveAttribute('aria-pressed', 'false');
  });

  test('la page n\'est réécrite que quelques fois par seconde', async ({ page }) => {
    await spyOnWrites(page);
    await open(page);
    await startEngine(page);
    await page.keyboard.down('Space');
    await page.waitForTimeout(300);
    // C'est ici que la couleur change le plus vite.
    const writes = await countWrites(page, 3000);
    await page.keyboard.up('Space');
    expect(writes).toBeGreaterThan(3);
    expect(writes).toBeLessThanOrEqual(3 * 10);
  });

  test('moins d\'animation : la couleur dit l\'état, sans transition et par paliers rares', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await spyOnWrites(page);
    await open(page);
    expect(await page.evaluate(() => (window as any).__revforge.motion.reduced)).toBe(true);
    await startEngine(page);
    await page.keyboard.down('Space');
    const writes = await countWrites(page, 3000);
    expect(writes).toBeLessThanOrEqual(3 * 2 + 1);
    await expect.poll(async () => red(await barHeat(page)), { timeout: 20_000, intervals: [500] }).toBeGreaterThan(200);
    await page.keyboard.up('Space');
    for (const selector of ['[data-sticky-bar]', '.heat-bar', '[data-testid=open-engine]', 'h1 span']) {
      const duration = await page.locator(selector).evaluate((node) => getComputedStyle(node).transitionDuration);
      expect(duration, selector).toBe('0s');
    }
    expect(await page.getByTestId('open-engine').evaluate((node) => getComputedStyle(node, '::before').transitionDuration)).toBe('0s');
  });

  test('couper les animations laisse la couleur suivre le moteur', async ({ page }) => {
    await open(page);
    await ouvrir(page, 'display');
    await page.getByTestId('animations').click();
    await page.getByTestId('panel-display-close').click();
    await startEngine(page);
    await page.keyboard.down('Space');
    await expect.poll(async () => red(await barHeat(page)), { timeout: 20_000, intervals: [500] }).toBeGreaterThan(200);
    await page.keyboard.up('Space');
  });

  test('une voiture électrique n\'a pas de chaleur à montrer : une couleur froide et fixe', async ({ page }) => {
    await open(page);
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption('electric');
    await page.getByTestId('panel-share-close').click();
    await expect.poll(() => barHeat(page)).toBe(NO_EXHAUST.fill);
    // Un moteur électrique ne tourne pas au ralenti.
    await startEngine(page, -1);
    await page.keyboard.down('Space');
    await page.waitForTimeout(3000);
    expect(await barHeat(page)).toBe(NO_EXHAUST.fill);
    await page.keyboard.up('Space');
    // Retour à un moteur thermique : l'échelle reprend.
    await page.getByTestId('open-share').click();
    await page.getByTestId('preset').selectOption('four_cyl_sport');
    await expect.poll(() => barHeat(page)).not.toBe(NO_EXHAUST.fill);
  });

  test('rien ne dépasse en largeur', async ({ page }) => {
    for (const [width, height] of WIDTHS) {
      await page.setViewportSize({ width, height });
      await open(page);
      for (const panel of [null, 'engine'] as const) {
        if (panel) await ouvrir(page, panel);
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
        expect(overflow, `${width} px, volet ${panel}`).toBeLessThanOrEqual(0);
      }
    }
  });

  test('les plaques du haut font au moins 44 px, et aucune ne déborde', async ({ page }) => {
    for (const [width, height] of WIDTHS) {
      await page.setViewportSize({ width, height });
      await open(page);
      for (const id of ['engine', 'exhaust', 'gearbox', 'car', 'listener', 'share', 'display']) {
        const plate = page.getByTestId(`open-${id}`);
        const box = (await plate.boundingBox())!;
        expect(box.height, `${id} à ${width} px`).toBeGreaterThanOrEqual(44);
        // Le nom tient dans sa plaque.
        const fits = await plate.locator('.name').evaluate((node) => node.scrollWidth <= node.clientWidth + 1);
        expect(fits, `${id} à ${width} px`).toBe(true);
      }
    }
  });

  test('le focus se voit autour d\'une plaque, malgré sa découpe', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await open(page);
    const plate = page.getByTestId('open-engine');
    const box = (await plate.boundingBox())!;
    // Une bande juste à gauche de la plaque, là où seul le contour peut dessiner.
    const strip = { x: box.x - 4, y: box.y + box.height / 2 - 10, width: 3, height: 20 };
    const before = await page.screenshot({ clip: strip });
    // Au clavier depuis le haut de la page : la première plaque est le premier arrêt.
    await page.keyboard.press('Tab');
    await expect(plate).toBeFocused();
    expect(await plate.evaluate((node) => node.matches(':focus-visible'))).toBe(true);
    const style = await plate.evaluate((node) => {
      const css = getComputedStyle(node);
      return { width: parseFloat(css.outlineWidth), kind: css.outlineStyle, clip: css.clipPath };
    });
    expect(style.width).toBeGreaterThanOrEqual(2);
    expect(style.kind).toBe('solid');
    expect(style.clip).toBe('none');
    expect((await page.screenshot({ clip: strip })).equals(before)).toBe(false);
  });

  test('aucune carte n\'a de bordure : les zones se distinguent par leur ton', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await open(page);
    await ouvrir(page, 'engine');
    const noBorder = async (name: string, locator: ReturnType<Page['locator']>) => {
      const borders = await locator.evaluate((node) => {
        const css = getComputedStyle(node);
        return [css.borderTopWidth, css.borderRightWidth, css.borderBottomWidth, css.borderLeftWidth];
      });
      expect(borders, name).toEqual(['0px', '0px', '0px', '0px']);
    };
    await noBorder('poste de conduite', page.locator('section.cockpit'));
    await noBorder('voiture', page.locator('section.stage'));
    await noBorder('vues', page.locator('section.views'));
    await noBorder('jauges', page.locator('aside.rail section').first());
    await noBorder('analyse', page.locator('.analysis section').first());
    await noBorder('volet', page.getByTestId('panel-engine'));
    await noBorder('plaque', page.getByTestId('open-engine'));
    await noBorder('démarrage', page.getByTestId('start'));
    await noBorder('pédale', page.getByRole('button', { name: 'Accélérateur' }));
    await noBorder('choix', page.getByTestId('panel-engine').locator('fieldset button').first());
    await noBorder('onglet', page.getByTestId('view-engine'));
    await noBorder('graphiques', page.getByTestId('charts-toggle').locator('xpath=..'));
    // Et le ton distingue bien le fond, la carte et la commande.
    const background = (locator: ReturnType<Page['locator']>) => locator.evaluate((node) => getComputedStyle(node).backgroundColor);
    expect(await page.evaluate(() => getComputedStyle(document.body).backgroundColor)).toBe('rgb(12, 10, 9)');
    expect(await background(page.locator('section.cockpit'))).toBe('rgb(21, 17, 14)');
    expect(await page.getByTestId('open-exhaust').evaluate((node) => getComputedStyle(node, '::before').backgroundColor)).toBe('rgb(31, 26, 22)');
  });

  test('les polices viennent du site : aucun service extérieur', async ({ page, baseURL }) => {
    const requests: string[] = [];
    page.on('request', (request) => requests.push(request.url()));
    await open(page);
    await page.evaluate(() => document.fonts.ready);
    const origin = new URL(baseURL!).origin;
    const outside = requests.filter((url) => url.startsWith('http') && !url.startsWith(origin));
    expect(outside).toEqual([]);
    expect(requests.some((url) => /fonts\.(googleapis|gstatic)|bunny\.net|typekit|fontshare/.test(url))).toBe(false);
    const fonts = requests.filter((url) => /\.woff2?(\?|$)/.test(url));
    expect(fonts.length).toBeGreaterThanOrEqual(3);
    expect(fonts.every((url) => /latin-(400|600|700|800)-normal/.test(url) && !/latin-ext/.test(url))).toBe(true);
    // Et elles servent bien.
    const faces = await page.evaluate(() => [...document.fonts].filter((face) => face.status === 'loaded').map((face) => `${face.family.replace(/["']/g, '')} ${face.weight}`));
    for (const face of ['Big Shoulders Display 700', 'Big Shoulders Display 800', 'Inter Tight 400', 'Inter Tight 600']) expect(faces).toContain(face);
    expect(await page.getByTestId('open-engine').locator('.name').evaluate((node) => getComputedStyle(node).fontFamily)).toContain('Big Shoulders Display');
    expect(await page.locator('body').evaluate((node) => getComputedStyle(node).fontFamily)).toContain('Inter Tight');
  });

  test('le grand chiffre du régime ne saute pas : une colonne par chiffre', async ({ page }) => {
    await open(page);
    await startEngine(page);
    await expect.poll(() => page.getByTestId('rpm').textContent()).not.toBe('0');
    const columns = await page.getByTestId('rpm').evaluate((node) => {
      const text = node.textContent ?? '';
      return { count: text.length, x: (node.getAttribute('x') ?? '').split(' ').length };
    });
    expect(columns.x).toBe(columns.count);
    // Les petites valeurs qui changent vite restent en Inter Tight, à chiffres tabulaires.
    const live = await page.getByTestId('live-hp').evaluate((node) => {
      const css = getComputedStyle(node);
      return { family: css.fontFamily, numeric: css.fontVariantNumeric };
    });
    expect(live.family).toContain('Inter Tight');
    expect(live.numeric).toBe('tabular-nums');
  });

  test('la zone rouge reste marquée et les étiquettes lisibles sur l\'arc épais', async ({ page }) => {
    await open(page);
    const gauge = page.getByTestId('gauge-rpm');
    await expect(gauge.locator('path.zone')).toHaveCount(1);
    expect(await gauge.locator('path.zone').evaluate((node) => getComputedStyle(node).stroke)).toBe('rgb(239, 83, 80)');
    await expect(page.getByTestId('tach-redline')).toBeVisible();
    await expect(gauge.locator('text.unit')).toBeVisible();
    const size = await gauge.locator('text.scale').first().evaluate((node) => parseFloat(getComputedStyle(node).fontSize));
    expect(size).toBeGreaterThanOrEqual(13);
  });
});
