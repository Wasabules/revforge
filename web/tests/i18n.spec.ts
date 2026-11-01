import { readFileSync } from 'node:fs';
import { expect, test, type Page } from '@playwright/test';
import { ouvrir, voir } from './aide';
import { en } from '../src/lib/locales/en';
import { fr, frPresets } from '../src/lib/locales/fr';

/* eslint-disable @typescript-eslint/no-explicit-any */

// Un visiteur neuf dont le navigateur est en français : c'est lui que le choix de l'anglais par défaut concerne.
test.use({ storageState: { cookies: [], origins: [] }, locale: 'fr-FR' });

const PRESETS: { id: string; name: string; description: string }[] = JSON.parse(readFileSync(new URL('../../presets/presets.json', import.meta.url), 'utf-8'));

// ── Ce qui trahit du français : accents, élisions, ponctuation espacée, virgule décimale, unités, mots courants ──
const FRENCH_WORDS = [
  'le', 'la', 'les', 'des', 'du', 'de', 'une', 'un', 'et', 'ou', 'au', 'aux', 'avec', 'sans', 'dans', 'pour', 'sur', 'par', 'est', 'ne', 'pas', 'ce', 'cette', 'en',
  'moteur', 'boite', 'vitesse', 'vitesses', 'puissance', 'couple', 'embrayage', 'rapport', 'rapports', 'frein', 'pont', 'volant', 'habitacle', 'dents', 'denture',
  'cylindres', 'reglage', 'sortie', 'sorties', 'ligne', 'niveau', 'tours', 'voir', 'valeurs', 'ralenti', 'rupteur', 'chaque', 'carburant', 'essence', 'masse', 'jantes',
  'silencieux', 'catalyseur', 'graphiques', 'spectre', 'confort', 'citadine', 'compacte', 'traction', 'route', 'maintenant', 'figer', 'marche', 'arbre', 'garnitures',
  'plateau', 'disque', 'fourchette', 'filtre', 'papillon', 'compresseur', 'vitres', 'fermees', 'ouvertes', 'derriere', 'devant', 'manette', 'touche', 'espace', 'pouces',
];
const FRENCH = [
  /[àâäçéèêëîïôöùûüÿœæ]/i,
  /\b[cdjlnqs]'[a-z]/i,
  /\s[:;!?»]|«/,
  /\d,\d{1,2}(?!\d)/,
  /\btr\/min\b|\bcm³|\bch\b|\bch\/t\b/,
  new RegExp(`\\b(${FRENCH_WORDS.join('|')})\\b`, 'i'),
];
/** Une clé ou un paramètre oublié dans le texte affiché. */
const RAW_KEY = /\{\w+\}|\b[a-zA-Z]+\.[a-z_A-Z]+\.[a-z_A-Z0-9.]+\b/;
const isFrench = (text: string) => FRENCH.some((pattern) => pattern.test(text));

/** Tout le texte de la page, même replié ou dessiné : nœuds de texte, attributs lus par les aides techniques, canvas. */
const pageText = (page: Page) => page.evaluate(() => {
  const out: string[] = [];
  // Les boutons du sélecteur se nomment chacun dans leur langue, et le disent par `lang`.
  const skipped = (element: Element | null) => !element || element.closest('[data-testid="lang-en"], [data-testid="lang-fr"], style, script');
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const text = node.textContent!.trim();
    if (text && !skipped(node.parentElement)) out.push(text);
  }
  for (const element of document.body.querySelectorAll('*')) {
    if (skipped(element)) continue;
    for (const name of ['aria-label', 'title', 'alt', 'placeholder']) {
      const value = element.getAttribute(name);
      if (value) out.push(value);
    }
  }
  out.push(document.title, document.querySelector('meta[name="description"]')!.getAttribute('content')!);
  out.push(...(window as any).__drawn);
  return [...new Set(out)];
});

async function open(page: Page, query = '') {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  await page.addInitScript(() => {
    // Ce que les canvas écrivent : le seul moyen de lire leur texte.
    const drawn = new Set<string>();
    (window as any).__drawn = drawn;
    const fillText = CanvasRenderingContext2D.prototype.fillText;
    CanvasRenderingContext2D.prototype.fillText = function patched(this: CanvasRenderingContext2D, text: string, x: number, y: number, maxWidth?: number) {
      drawn.add(String(text));
      return maxWidth === undefined ? fillText.call(this, text, x, y) : fillText.call(this, text, x, y, maxWidth);
    };
  });
  await page.goto(`./?test${query}`);
  await expect(page.getByTestId('start')).toBeEnabled();
  return errors;
}

async function startEngine(page: Page) {
  await page.getByTestId('start').click();
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetry().rpm), { timeout: 15_000 }).toBeGreaterThan(500);
}

/** Vide ce qu'ont écrit les canvas, puis attend qu'ils aient redessiné : ce qui s'y trouve ensuite est le texte actuel. */
async function freshDrawing(page: Page) {
  await page.evaluate(() => (window as any).__drawn.clear());
  await page.evaluate(() => new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve()))));
  await page.waitForTimeout(100);
  return page.evaluate(() => [...(window as any).__drawn] as string[]);
}

test.describe('les dictionnaires', () => {
  // Rien à voir avec le navigateur : une seule passe suffit.
  test.skip(({ browserName }) => browserName !== 'chromium');

  test('le dictionnaire anglais et les préréglages ne contiennent pas de français', () => {
    const texts = [...Object.values(en), ...PRESETS.flatMap((preset) => [preset.name, preset.description])];
    expect(texts.filter(isFrench)).toEqual([]);
  });

  test('les deux langues demandent les mêmes paramètres', () => {
    const holes = (text: string) => [...text.matchAll(/\{(\w+)\}/g)].map((match) => match[1]).sort();
    for (const key of Object.keys(en) as (keyof typeof en)[]) expect(holes(fr[key]), key).toEqual(holes(en[key]));
  });

  test('chaque préréglage a son nom et sa description en français', () => {
    expect(Object.keys(frPresets).sort()).toEqual(PRESETS.map((preset) => preset.id).sort());
    for (const preset of PRESETS) {
      expect(frPresets[preset.id].name, preset.id).toBeTruthy();
      expect(frPresets[preset.id].description, preset.id).toBeTruthy();
    }
  });
});

test('anglais par défaut, stockage vide et navigateur en français', async ({ page }) => {
  const errors = await open(page);
  expect(await page.evaluate(() => navigator.language)).toBe('fr-FR');
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await expect(page.getByTestId('start')).toHaveText('Start the engine');
  await expect(page.locator('h1 + p')).toHaveText(en['header.tagline']);
  await expect(page.locator('meta[name="description"]')).toHaveAttribute('content', en['meta.description']);
  await expect(page.getByTestId('lang-en')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.getByTestId('lang-fr')).toHaveAttribute('aria-pressed', 'false');
  // Être passé par la page ne vaut pas un choix.
  expect(await page.evaluate(() => localStorage.getItem('revforge.lang'))).toBeNull();
  expect(errors).toEqual([]);
});

test("le sélecteur change le texte, l'attribut lang, les nombres et les canvas, sans recharger", async ({ page }) => {
  const errors = await open(page);
  await page.evaluate(() => ((window as any).__sameDocument = true));
  const summary = page.getByTestId('summary');
  await expect(summary).toContainText('2.0 L');
  await expect(summary).toContainText('cc per cylinder');
  await expect(page.getByTestId('box-table').locator('caption')).toContainText('7,200 rpm');
  await expect.poll(() => freshDrawing(page)).toContain('Muffler');

  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(page.locator('meta[name="description"]')).toHaveAttribute('content', fr['meta.description']);
  await expect(page.getByTestId('lang-fr')).toHaveAttribute('aria-pressed', 'true');
  await expect(summary).toContainText('2,0 L');
  await expect(summary).toContainText('cm³ par cylindre');
  // Le séparateur de milliers français : une espace fine insécable.
  await expect(page.getByTestId('box-table').locator('caption')).toContainText('7\u202f200 tr/min');
  await expect(page.getByTestId('preset').locator('option', { hasText: 'Compacte 2.0 turbo' })).toHaveCount(1);
  const french = await freshDrawing(page);
  expect(french).toContain('Silencieux');
  expect(french).not.toContain('Muffler');

  await page.getByTestId('lang-en').click();
  await expect(page.getByTestId('start')).toHaveText('Start the engine');
  await expect(summary).toContainText('2.0 L');
  const english = await freshDrawing(page);
  expect(english).toContain('Muffler');
  expect(english).not.toContain('Silencieux');

  expect(await page.evaluate(() => (window as any).__sameDocument)).toBe(true);
  expect(errors).toEqual([]);
});

test('le choix survit au rechargement', async ({ page }) => {
  await open(page);
  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  expect(await page.evaluate(() => localStorage.getItem('revforge.lang'))).toBe('fr');
  await page.reload();
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  await ouvrir(page, 'display');
  await page.getByTestId('lang-en').click();
  await page.reload();
  await expect(page.getByTestId('start')).toHaveText('Start the engine');
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
});

test('?lang= impose la langue sans devenir un choix', async ({ page }) => {
  await open(page, '&lang=fr');
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(page.locator('html')).toHaveAttribute('lang', 'fr');
  expect(await page.evaluate(() => localStorage.getItem('revforge.lang'))).toBeNull();
  await page.goto('./?test');
  await expect(page.getByTestId('start')).toHaveText('Start the engine');

  // Une valeur inconnue ne change rien.
  await page.goto('./?test&lang=de');
  await expect(page.getByTestId('start')).toHaveText('Start the engine');

  // Elle l'emporte sur un choix enregistré.
  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await page.goto('./?test&lang=en');
  await expect(page.getByTestId('start')).toHaveText('Start the engine');

  // Choisir au sélecteur l'emporte sur l'adresse, et l'adresse ne doit plus le contredire au rechargement.
  await page.goto('./?test&lang=fr');
  await ouvrir(page, 'display');
  await page.getByTestId('lang-en').click();
  expect(page.url()).not.toContain('lang=');
  await page.reload();
  await expect(page.getByTestId('start')).toHaveText('Start the engine');
});

test('la page fonctionne sans localStorage', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(window, 'localStorage', { get() { throw new Error('stockage refusé'); } });
  });
  const errors = await open(page);
  await expect(page.getByTestId('start')).toHaveText('Start the engine');
  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await expect(page.getByTestId('start')).toHaveText('Démarrer le moteur');
  expect(errors).toEqual([]);
});

test('changer de langue ne touche pas au son', async ({ page }) => {
  const errors = await open(page);
  await startEngine(page);
  await page.evaluate(() => {
    // Tout ce qui part vers le worklet ou recalcule la fiche passe par ces deux fonctions.
    const engine = (window as any).__revforge.engine;
    const sent: string[] = [];
    (window as any).__sent = sent;
    const send = engine.send.bind(engine);
    engine.send = (command: { type: string }) => {
      sent.push(command.type);
      send(command);
    };
    const spec = engine.spec.bind(engine);
    engine.spec = (config: unknown) => {
      sent.push('spec');
      return spec(config);
    };
  });
  const before = await page.evaluate(() => {
    const r = (window as any).__revforge;
    (window as any).__context = r.engine.context;
    return { written: r.telemetryLog.written, state: r.engine.context.state };
  });
  expect(before.state).toBe('running');

  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await expect(page.getByTestId('start')).toHaveText('Couper le contact');

  // La télémétrie continue d'arriver, le moteur tourne encore, le son sort encore.
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.telemetryLog.written)).toBeGreaterThan(before.written + 15);
  const after = await page.evaluate(() => {
    const r = (window as any).__revforge;
    return { rpm: r.telemetry().rpm, state: r.engine.context.state, sameContext: r.engine.context === (window as any).__context, sent: (window as any).__sent };
  });
  expect(after.rpm).toBeGreaterThan(500);
  expect(after.state).toBe('running');
  expect(after.sameContext).toBe(true);
  expect(after.sent).toEqual([]);
  await expect.poll(() => page.evaluate(() => (window as any).__revforge.engine.level())).toBeGreaterThan(0);
  expect(errors).toEqual([]);
});

test("toute la page en anglais : plus aucun français, panneaux ouverts et réglages parcourus", async ({ page }) => {
  test.setTimeout(240_000);
  await page.setViewportSize({ width: 1400, height: 4000 });
  const errors = await open(page);
  const seen = new Set<string>();
  const scan = async (wait = 150) => {
    await page.waitForTimeout(wait);
    for (const text of await pageText(page)) seen.add(text);
  };
  const press = (name: string) => page.getByRole('button', { name, exact: true }).first().click();

  await scan();

  // Les graphiques : tout ouvert, tout coché, valeurs comprises.
  await page.getByTestId('charts-toggle').click();
  for (const box of await page.getByTestId('charts-body').locator('.filters input').all()) await box.check();
  await scan();

  await startEngine(page);
  await page.getByTestId('charts-body').locator('details').first().evaluate((node: HTMLDetailsElement) => (node.open = true));
  await page.getByTestId('charts-freeze').click();
  await scan(400);
  await page.getByTestId('charts-freeze').click();
  // L'infobulle, au survol d'une courbe et du spectrogramme.
  await page.getByTestId('chart-rpm').locator('canvas').hover();
  await scan();
  await page.getByTestId('chart-spectrogram').locator('canvas').hover();
  await scan();

  // Les modes de conduite, boîte manuelle et embrayage au pied compris.
  await ouvrir(page, 'listener');
  await scan();
  await ouvrir(page, 'engine');
  await scan();
  await ouvrir(page, 'exhaust');
  await scan();
  await ouvrir(page, 'car');
  await scan();
  await ouvrir(page, 'share');
  await scan();
  await ouvrir(page, 'display');
  await scan();
  await press(en['mode.drive']);
  await scan();
  await press(en['gearboxMode.manual']);
  await press(en['clutchMode.foot']);
  await page.keyboard.down('Space');
  await scan(600);
  await page.keyboard.up('Space');
  await press(en['clutchMode.assist']);
  await press(en['mode.lap']);
  await scan();
  await press(en['mode.neutral']);

  // Toutes les positions d'écoute : les vitres n'apparaissent que dans l'habitacle.
  await ouvrir(page, 'listener');
  for (const key of ['listener.pos.front', 'listener.pos.side', 'listener.pos.far', 'listener.pos.pipe', 'listener.pos.bay', 'listener.pos.cabin'] as const) {
    await press(en[key]);
    await scan(250);
  }
  await press(en['listener.windows.open']);
  await scan();

  // La boîte : chaque type, chaque famille, chaque réglage imposé.
  await ouvrir(page, 'gearbox');
  for (const key of ['dog', 'sequential', 'dual_clutch', 'converter', 'synchro'] as const) {
    await press(en[`boxKind.${key}`]);
    await scan();
  }
  for (const key of ['short', 'close', 'long', 'custom', 'matched'] as const) {
    await press(en[`boxFamily.${key}`]);
    await scan();
  }
  await press(en['box.final.chosen']);
  await press(en['box.module.chosen']);
  await page.getByRole('button', { name: en['box.limit.chosen'], exact: true }).last().click();
  await press(en['boxTeeth.spur']);
  await press(en['boxFlywheel.dual_mass']);
  await scan();
  for (const option of await page.getByTestId('box-model').locator('option').all()) {
    await page.getByTestId('box-model').selectOption({ value: (await option.getAttribute('value')) ?? '' });
    await scan();
  }

  // Chaque préréglage : turbo, diesel, électrique, avec leurs textes propres ; puis un moteur modifié.
  for (const preset of PRESETS) {
    await ouvrir(page, 'share');
    await page.getByTestId('preset').selectOption(preset.id);
    await scan();
  }
  await page.getByTestId('preset').selectOption('v8_cross_plane');
  await ouvrir(page, 'engine');
  await press(en['aspiration.turbo']);
  await press(en['fuel.diesel']);
  await scan();

  const texts = [...seen];
  expect(texts.length).toBeGreaterThan(300);
  expect(texts.filter(isFrench)).toEqual([]);
  expect(texts.filter((text) => RAW_KEY.test(text))).toEqual([]);
  expect(errors).toEqual([]);
});

test('un préréglage se choisit en anglais, et la physique ne dépend pas de la langue', async ({ page }) => {
  const errors = await open(page);
  await ouvrir(page, 'share');
  const select = page.getByTestId('preset');
  expect(await select.locator('option').allTextContents()).toEqual(PRESETS.map((preset) => preset.name));
  await select.selectOption({ label: 'Cross-plane V8' });
  await expect(select).toHaveValue('v8_cross_plane');
  await expect(page.locator('.preset small')).toHaveText('Big, deep, rough.');
  await expect(page.getByTestId('summary')).toContainText('8 cylinders');
  const hp = await page.getByTestId('spec-hp').textContent();
  expect(hp).toMatch(/^\d+ hp$/);

  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await expect(select.locator('option:checked')).toHaveText(frPresets.v8_cross_plane.name);
  await expect(page.locator('.preset small')).toHaveText(frPresets.v8_cross_plane.description);
  await expect(page.getByTestId('spec-hp')).toHaveText(hp!.replace('hp', 'ch'));
  await expect(select).toHaveValue('v8_cross_plane');
  expect(errors).toEqual([]);
});

test('le lien de partage ne porte pas la langue', async ({ page, context }) => {
  // Le presse-papiers n'est pas lisible partout : on regarde ce qui lui est confié.
  await context.addInitScript(() => {
    (window as any).__copied = [];
    (window as any).__refuse = false;
    Object.defineProperty(navigator, 'clipboard', {
      value: {
        writeText: async (text: string) => {
          if ((window as any).__refuse) throw new Error('refusé');
          (window as any).__copied.push(text);
        },
      },
    });
  });
  const errors = await open(page, '&lang=en');
  await ouvrir(page, 'share');
  await page.getByTestId('preset').selectOption('hot_hatch_turbo');
  await page.getByTestId('share').click();
  await expect(page.locator('p[role="status"]')).toHaveText(en['notice.copied']);
  const copied: string = await page.evaluate(() => (window as any).__copied[0]);
  expect(copied).toContain('#m=');
  expect(copied).not.toContain('lang');
  const hash = new URL(copied).hash;

  // Le refus du presse-papiers renvoie à la barre d'adresse : elle non plus ne doit pas porter la langue.
  await page.evaluate(() => ((window as any).__refuse = true));
  await page.getByTestId('share').click();
  await expect(page.locator('p[role="status"]')).toHaveText(en['notice.copyRefused']);
  expect(page.url()).not.toContain('lang');

  // Le lien s'ouvre en anglais chez un visiteur neuf, avec le même moteur.
  const other = await context.newPage();
  await other.goto(copied);
  await expect(other.getByTestId('start')).toHaveText('Start the engine');
  await expect(other.getByTestId('preset')).toHaveValue('hot_hatch_turbo');

  // Créé en français, il ne change pas d'une lettre, et chacun le lit dans sa langue.
  await ouvrir(other, 'display');
  await other.getByTestId('lang-fr').click();
  await ouvrir(other, 'share');
  await other.getByTestId('share').click();
  const copiedFrench: string = await other.evaluate(() => (window as any).__copied[0]);
  expect(new URL(copiedFrench).hash).toBe(hash);
  expect(copiedFrench).not.toContain('lang');
  await other.goto('about:blank');
  await other.goto(copied);
  await expect(other.getByTestId('start')).toHaveText('Démarrer le moteur');
  await expect(other.getByTestId('preset')).toHaveValue('hot_hatch_turbo');
  expect(errors).toEqual([]);
});

test('le tableau des graphiques suit la langue', async ({ page }) => {
  await open(page);
  await startEngine(page);
  await page.getByTestId('charts-toggle').click();
  await page.getByTestId('charts-body').locator('details summary').first().click();
  const firstCell = page.getByTestId('charts-table').locator('tbody tr td').first();
  await expect(firstCell).toHaveText(/^−\d+\.\d s$/);
  await expect(page.getByTestId('charts-table').locator('thead')).toContainText('Engine (rpm)');
  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await expect(firstCell).toHaveText(/^−\d+,\d s$/);
  await expect(page.getByTestId('charts-table').locator('thead')).toContainText('Moteur (tr/min)');
});

test('le fichier WAV exporté se nomme dans la langue choisie', async ({ page }) => {
  await open(page);
  await ouvrir(page, 'share');
  const [english] = await Promise.all([page.waitForEvent('download'), page.getByTestId('export-blips').click()]);
  expect(english.suggestedFilename()).toBe('revforge-four_cyl_sport-blips.wav');
  await ouvrir(page, 'display');
  await page.getByTestId('lang-fr').click();
  await ouvrir(page, 'share');
  const [french] = await Promise.all([page.waitForEvent('download'), page.getByTestId('export-blips').click()]);
  expect(french.suggestedFilename()).toBe('revforge-four_cyl_sport-coups-de-gaz.wav');
});

test('à la largeur d\'un téléphone, le texte anglais ne déborde pas', async ({ page }) => {
  await page.setViewportSize({ width: 360, height: 780 });
  const errors = await open(page);
  await page.getByTestId('charts-toggle').click();
  for (const box of await page.getByTestId('charts-body').locator('.filters input').all()) await box.check();
  await ouvrir(page, 'display');
  await expect(page.getByTestId('lang-fr')).toBeVisible();
  const overflow = await page.evaluate(() => {
    const limit = document.documentElement.clientWidth;
    const wide = [...document.querySelectorAll('button, legend, label, h1, h2, h3, p, dd, dt')].filter((element) => element.getBoundingClientRect().right > limit + 1);
    return { page: document.documentElement.scrollWidth - limit, wide: wide.map((element) => element.textContent!.trim().slice(0, 50)) };
  });
  expect(overflow.page).toBeLessThanOrEqual(0);
  expect(overflow.wide).toEqual([]);
  expect(errors).toEqual([]);
});
