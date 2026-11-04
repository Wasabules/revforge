import { expect, type Page } from '@playwright/test';

/**
 * Les réglages vivent dans des volets, et une seule vue est montée à la fois : ces aides y mènent.
 * Elles ne remplacent aucune vérification, elles ne font qu'ouvrir ce que le visiteur ouvrirait.
 */
export type PanelId = 'engine' | 'exhaust' | 'gearbox' | 'car' | 'listener' | 'share' | 'display';
export type ViewId = 'engine' | 'clutch' | 'gearbox' | 'gas';

/** Ouvre ce volet s'il ne l'est pas déjà ; un seul est ouvert à la fois. */
export async function ouvrir(page: Page, id: PanelId) {
  const box = page.getByTestId(`panel-${id}`);
  if (!(await box.isVisible())) await page.getByTestId(`open-${id}`).click();
  await expect(box).toBeVisible();
}

/** Affiche cette vue : montée à la première demande, en pause ensuite. */
export async function voir(page: Page, id: ViewId) {
  await page.getByTestId(`view-${id}`).click();
}

/**
 * L'attente d'un chiffre que calcule le worker de la fiche (`calcWorker.ts`) : le 0 à 100 et la fiche acoustique. Il fait un
 * calcul à la fois et ne garde que la dernière demande de chacun : seul, un réglage y attend au plus le calcul en cours, son
 * 0 à 100 puis sa fiche acoustique, environ 2,4 s. Une passe complète, deux navigateurs à la fois, en a pris plus de 5. Ce
 * n'est que le temps d'attente : ce que le test exige du chiffre ne change pas.
 */
export const CALCUL_A_PART = { timeout: 15_000 };
