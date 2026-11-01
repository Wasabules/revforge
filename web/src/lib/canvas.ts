/** Ce que partagent les vues animées : netteté, couleurs du thème, suivi de la télémétrie. */
import { currentHeat, heatColors, parseHex, wallCelsius, wallColor } from './heat';

export interface Fitted {
  ctx: CanvasRenderingContext2D;
  width: number;
  height: number;
}

/** Ajuste le canvas à son affichage ; on dessine ensuite en pixels CSS. */
export function fit(canvas: HTMLCanvasElement): Fitted {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const w = Math.round(width * ratio);
  const h = Math.round(height * ratio);
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  const ctx = canvas.getContext('2d')!;
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);
  return { ctx, width, height };
}

export interface Theme {
  text: string;
  dim: string;
  line: string;
  lineStrong: string;
  surface: string;
  raised: string;
  bg: string;
  /** La chaleur du moment, lisible sur fond sombre : change dans le même objet, deux fois par seconde au plus. */
  accent: string;
  danger: string;
  cool: string;
  metal: string;
}

let themeCache: Theme | null = null;
let themeKey = '';
let themeReadAt = -Infinity;

/**
 * Les couleurs du thème. Lire les styles à chaque image alloue et coûte : on ne les relit que
 * deux fois par seconde, et l'objet ne change que si une couleur a changé. La chaleur ne compte pas : elle change
 * souvent, et ce qui se déduit des couleurs fixes (tables du gaz, matériaux) n'a pas à se recalculer pour elle.
 */
export function readTheme(element: Element): Theme {
  const now = performance.now();
  if (themeCache && now - themeReadAt < 500) return themeCache;
  themeReadAt = now;
  const styles = getComputedStyle(element);
  const get = (name: string) => styles.getPropertyValue(name).trim();
  const theme: Theme = {
    text: get('--text'),
    dim: get('--text-dim'),
    line: get('--line'),
    lineStrong: get('--line-strong'),
    surface: get('--surface'),
    raised: get('--surface-raised'),
    bg: get('--bg'),
    accent: currentHeat().text,
    danger: get('--danger'),
    cool: get('--cool'),
    metal: get('--metal'),
  };
  const key = Object.values({ ...theme, accent: '' }).join();
  if (key !== themeKey || !themeCache) {
    themeKey = key;
    themeCache = theme;
  }
  themeCache.accent = theme.accent;
  return themeCache;
}

/**
 * Boucle d'animation, à l'arrêt quand la vue est hors écran ou l'onglet caché.
 * `measured` faux : une jauge n'a pas besoin de sa taille, et la lire forcerait une mise en page à chaque image.
 */
export function animate(element: Element, draw: (seconds: number, now: number) => void, measured = true): () => void {
  let frame = 0;
  let last = performance.now();
  // Rien avant la première réponse de l'observateur : une vue cachée au montage (volet fermé, vue en pause) n'a pas de taille.
  let visible = false;
  // Ce qui passe sous la barre collée en haut de la page n'est plus visible non plus.
  const bar = document.querySelector<HTMLElement>('[data-sticky-bar]')?.offsetHeight ?? 0;
  const observer = new IntersectionObserver((entries) => {
    visible = entries[entries.length - 1].isIntersecting;
  }, { rootMargin: `-${bar}px 0px 0px 0px` });
  observer.observe(element);
  const loop = (now: number) => {
    frame = requestAnimationFrame(loop);
    // L'observateur répond une image en retard : un élément qu'on vient de cacher n'a déjà plus de taille.
    if (!visible || (measured && element.clientWidth === 0)) {
      last = now;
      return;
    }
    const seconds = Math.min((now - last) / 1000, 0.1);
    last = now;
    draw(seconds, now);
  };
  frame = requestAnimationFrame(loop);
  return () => {
    cancelAnimationFrame(frame);
    observer.disconnect();
  };
}

/** Mélange deux couleurs hexadécimales (#rrggbb). */
export function mix(a: string, b: string, t: number): string {
  const parse = (hex: string) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
  const [ar, ag, ab] = parse(a);
  const [br, bg, bb] = parse(b);
  const f = Math.max(0, Math.min(1, t));
  return `rgb(${Math.round(ar + (br - ar) * f)}, ${Math.round(ag + (bg - ag) * f)}, ${Math.round(ab + (bb - ab) * f)})`;
}

/** Couleur d'un tube : acier à froid, puis l'échelle du thème quand il rougit. `metal` en hexadécimal. */
export function pipeColor(theme: Theme, heat: number): string {
  const [r, g, b] = wallColor(parseHex(theme.metal), heat);
  return `rgb(${Math.round(r)}, ${Math.round(g)}, ${Math.round(b)})`;
}

/** Une pièce qui chauffe avec la ligne : sa teinte au repos, puis la couleur de la paroi. */
export function heatedPart(base: string, heat: number): string {
  return mix(base, heatColors(wallCelsius(heat)).fill, Math.min(1, heat * 1.6));
}

/** Couleur d'un gaz qui brûle, 0 à 1 : de la cerise sombre au jaune pâle, sur l'échelle du thème. */
export function gasColor(heat: number): string {
  return heatColors(300 + 700 * heat).fill;
}
