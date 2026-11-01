/**
 * La langue du site : anglais par défaut, français au choix. Tout le texte passe par `t`, et les nombres par `whole`
 * et `fixed`. Lire l'une de ces fonctions dans un gabarit ou un `$derived` s'y abonne : changer de langue met à jour
 * le texte sans recharger la page. Les canvas, eux, relisent à chaque image. Rien de tout cela ne touche au son.
 */
import { en, type MessageKey } from './locales/en';
import { fr, frPresets } from './locales/fr';

export type Lang = 'en' | 'fr';
export type { MessageKey };

const STORAGE_KEY = 'revforge.lang';
const MESSAGES: Record<Lang, Record<MessageKey, string>> = { en, fr };
const LOCALES: Record<Lang, string> = { en: 'en-US', fr: 'fr-FR' };

const isLang = (value: string | null | undefined): value is Lang => value === 'en' || value === 'fr';

/** Le stockage peut être refusé : navigation privée, données bloquées. La page s'en passe. */
function storedLang(): string | null {
  try {
    return localStorage.getItem(STORAGE_KEY);
  } catch {
    return null;
  }
}

function initialLang(): Lang {
  const asked = new URLSearchParams(location.search).get('lang')?.toLowerCase();
  if (isLang(asked)) return asked;
  const stored = storedLang();
  // Jamais la langue du navigateur : sans choix du visiteur, c'est l'anglais.
  return isLang(stored) ? stored : 'en';
}

let current = $state<Lang>(initialLang());

export const lang = (): Lang => current;

export function t(key: MessageKey, params?: Record<string, string | number>): string {
  const text = MESSAGES[current][key];
  if (!params) return text;
  return text.replace(/\{(\w+)\}/g, (hole, name: string) => (name in params ? String(params[name]) : hole));
}

/** La vitesse des animations, en toutes lettres. */
export const speedLabel = (slow: number): string => (slow === 1 ? t('common.realTime') : t('common.slowMotion', { factor: slow }));

/** Un entier arrondi, avec le séparateur de milliers de la langue. */
export const whole = (value: number): string => Math.round(value).toLocaleString(LOCALES[current]);

/** Un nombre à décimales fixes, sans séparateur de milliers : virgule en français, point en anglais. */
export function fixed(value: number, digits: number): string {
  const text = value.toFixed(digits);
  return current === 'fr' ? text.replace('.', ',') : text;
}

/** Le nom et la description d'un préréglage : `english` vient de `presets/presets.json`. */
export function presetText(id: string, field: 'name' | 'description', english: string): string {
  return current === 'fr' ? (frPresets[id]?.[field] ?? english) : english;
}

function applyToDocument() {
  document.documentElement.lang = current;
  document.title = t('meta.title');
  document.querySelector('meta[name="description"]')?.setAttribute('content', t('meta.description'));
}

/** Le choix du visiteur : il l'emporte désormais, et l'adresse ne doit plus imposer l'ancienne langue au rechargement. */
export function setLang(next: Lang) {
  current = next;
  try {
    localStorage.setItem(STORAGE_KEY, next);
  } catch {
    // Le choix vaudra pour cette page seulement.
  }
  const url = new URL(location.href);
  if (url.searchParams.has('lang')) {
    url.searchParams.delete('lang');
    history.replaceState(null, '', url);
  }
  applyToDocument();
}

applyToDocument();
