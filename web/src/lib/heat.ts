/**
 * La couleur de la chaleur : le thème du site suit la température de la ligne d'échappement.
 *
 * Métal froid, cerise sombre, orange, puis jaune pâle : le rayonnement d'un métal qui chauffe. Tout ce fichier, sauf la
 * dernière partie, est fait de fonctions pures : la même échelle sert au thème, aux tubes et au gaz.
 *
 * La source est `Motion.heat`, la température des parois, plutôt que celle du gaz au collecteur :
 * - elle avance en temps réel, alors que le gaz suit le ralenti choisi et ne calcule que si une vue ou une jauge le demande ;
 * - elle a l'inertie d'une pièce d'acier, donc elle ne bat pas à chaque explosion ;
 * - elle retombe au froid moteur coupé, et reste nulle sur une voiture électrique, qui n'a pas de ligne.
 */

export type Rgb = readonly [number, number, number];

/** Température ambiante (°C), et échauffement des parois quand `Motion.heat` vaut 1 (K) : le modèle du gaz lit les mêmes. */
export const AMBIENT_C = 20;
export const WALL_RISE_K = 720;

/** Les paliers de la maquette. En dessous du premier, le métal ne rayonne plus : il reste à sa couleur froide. */
export const HEAT_STOPS: readonly (readonly [number, Rgb])[] = [
  [80, [74, 62, 54]],
  [350, [122, 30, 16]],
  [550, [196, 52, 16]],
  [750, [255, 106, 26]],
  [900, [255, 170, 66]],
  [1000, [255, 226, 170]],
];
export const HEAT_MIN_C = HEAT_STOPS[0][0];
export const HEAT_MAX_C = HEAT_STOPS[HEAT_STOPS.length - 1][0];

/** Les fonds sur lesquels se pose le texte, du plus sombre au plus clair. */
export const GROUND = '#0c0a09';
export const SURFACE = '#15110e';
export const RAISED = '#1f1a16';

/** Deux encres seulement : ces deux-là gardent 4,5:1 sur toute l'échelle, même au pire point où elles se valent. */
export const INK_DARK = '#050302';
export const INK_LIGHT = '#ffffff';

/** Sans ligne d'échappement, pas de chaleur à montrer : une couleur froide, fixe, qui ne prétend rien. */
export const NO_EXHAUST: HeatColors = { fill: '#35566b', text: '#a3c9df', ink: INK_LIGHT };

const clampC = (celsius: number) => (celsius < HEAT_MIN_C ? HEAT_MIN_C : celsius > HEAT_MAX_C ? HEAT_MAX_C : celsius);

/** La température d'une paroi (°C) d'après `Motion.heat`, 0 à 1 : le calcul de `GasNetwork.setWalls`. */
export const wallCelsius = (heat: number): number => AMBIENT_C + WALL_RISE_K * heat;

/** Couleur du métal à cette température, sans arrondi. */
export function heatRgb(celsius: number): [number, number, number] {
  const t = clampC(celsius);
  for (let i = 1; i < HEAT_STOPS.length; i++) {
    const [to, b] = HEAT_STOPS[i];
    if (t <= to) {
      const [from, a] = HEAT_STOPS[i - 1];
      const f = (t - from) / (to - from);
      return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
    }
  }
  const last = HEAT_STOPS[HEAT_STOPS.length - 1][1];
  return [last[0], last[1], last[2]];
}

export function toHex(rgb: Rgb): string {
  const part = (value: number) => Math.round(value).toString(16).padStart(2, '0');
  return `#${part(rgb[0])}${part(rgb[1])}${part(rgb[2])}`;
}

export function parseHex(hex: string): [number, number, number] {
  const value = parseInt(hex.slice(1), 16);
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

/** Luminance relative, au sens du WCAG. */
export function luminance(rgb: Rgb): number {
  const channel = (value: number) => {
    const c = value / 255;
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * channel(rgb[0]) + 0.7152 * channel(rgb[1]) + 0.0722 * channel(rgb[2]);
}

export function contrast(a: Rgb, b: Rgb): number {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

/** Vers quoi s'éclaircit le texte : un gris chaud tant que le métal est froid, un orangé clair dès qu'il rougit. */
const LIFT_COLD: Rgb = [239, 233, 223];
const LIFT_HOT: Rgb = [255, 186, 130];
/** Un peu au-dessus de 4,5:1 : les arrondis du calcul ne doivent pas faire passer dessous. */
const TEXT_CONTRAST = 4.6;
const RAISED_RGB = parseHex(RAISED);

const blend = (a: Rgb, b: Rgb, f: number): [number, number, number] => [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];

/** La version lisible de cette couleur sur tous les fonds de la page : le fond le plus clair décide. */
export function heatTextRgb(celsius: number): [number, number, number] {
  const fill = heatRgb(celsius);
  if (contrast(fill, RAISED_RGB) >= TEXT_CONTRAST) return fill;
  const warm = Math.min(1, Math.max(0, (clampC(celsius) - HEAT_MIN_C) / 270));
  const target = blend(LIFT_COLD, LIFT_HOT, warm);
  let low = 0;
  let high = 1;
  for (let i = 0; i < 12; i++) {
    const f = (low + high) / 2;
    if (contrast(blend(fill, target, f), RAISED_RGB) >= TEXT_CONTRAST) high = f;
    else low = f;
  }
  return blend(fill, target, high);
}

/** L'encre la plus lisible sur une surface remplie de cette couleur. */
export function inkOn(fill: Rgb): string {
  return contrast(fill, parseHex(INK_LIGHT)) >= contrast(fill, parseHex(INK_DARK)) ? INK_LIGHT : INK_DARK;
}

export interface HeatColors {
  /** La couleur du métal : ce qui se remplit. */
  fill: string;
  /** Sa version lisible sur fond sombre : le texte, les traits fins. */
  text: string;
  /** L'encre à poser sur `fill`. */
  ink: string;
}

/** Un palier de couleur tous les 10 °C : sous cet écart, l'œil ne voit rien, et rien ne se fabrique ensuite. */
export const HEAT_STEP_C = 10;
const LEVELS = Math.round((HEAT_MAX_C - HEAT_MIN_C) / HEAT_STEP_C) + 1;
const TABLE: HeatColors[] = Array.from({ length: LEVELS }, (_, i) => {
  const celsius = HEAT_MIN_C + i * HEAT_STEP_C;
  // L'encre se choisit sur la couleur telle qu'elle est écrite : c'est celle-là qui s'affiche.
  const fill = toHex(heatRgb(celsius));
  return { fill, text: toHex(heatTextRgb(celsius)), ink: inkOn(parseHex(fill)) };
});

const levelOf = (celsius: number) => Math.round((clampC(celsius) - HEAT_MIN_C) / HEAT_STEP_C);

/** Les trois couleurs à cette température, sans rien allouer : à appeler aussi souvent qu'on veut. */
export const heatColors = (celsius: number): HeatColors => TABLE[levelOf(celsius)];

/** Couleur d'un tube : sa teinte de métal à froid, puis l'échelle commune dès qu'il rougit. */
export function wallColor(metal: Rgb, heat: number): [number, number, number] {
  const celsius = wallCelsius(heat);
  const red = HEAT_STOPS[1][0];
  if (celsius >= red) return heatRgb(celsius);
  return blend(metal, heatRgb(red), Math.max(0, (celsius - AMBIENT_C) / (red - AMBIENT_C)));
}

// ── Ce qui suit écrit dans la page : le reste du fichier n'en dépend pas. ──

/** Combien de fois par seconde on regarde la température, et d'écart avant de réécrire. */
const CHECK_MS = 125;
/** Moins d'animation : la couleur dit toujours l'état, par paliers francs et rares, sans donner l'impression de bouger. */
const CHECK_REDUCED_MS = 500;
const QUANTUM_REDUCED_C = 50;

const targets = new Set<HTMLElement | SVGElement>();
let shown: HeatColors = TABLE[0];
let shownC = HEAT_MIN_C;
let noExhaust = false;
let checkAt = 0;

function paint(element: HTMLElement | SVGElement): void {
  element.style.setProperty('--heat', shown.fill);
  element.style.setProperty('--heat-text', shown.text);
  element.style.setProperty('--on-heat', shown.ink);
}

function show(colors: HeatColors): void {
  shown = colors;
  for (const element of targets) paint(element);
}

/**
 * Ce qui suit la chaleur. Changer une propriété sur la racine recalculerait le style de toute la page, et sur un élément,
 * celui de tout ce qu'il contient : seuls les traits et les zones qui s'en servent la reçoivent. Un volet fermé ne coûte rien.
 */
export function heated(element: HTMLElement | SVGElement): { destroy(): void } {
  targets.add(element);
  paint(element);
  return { destroy: () => void targets.delete(element) };
}

/** La couleur que montre la page en ce moment : les canvas la lisent ici, sans passer par les styles. */
export const currentHeat = (): HeatColors => shown;

/**
 * À appeler à chaque image, avec la température de la ligne : `null` s'il n'y en a pas.
 * Ne touche la page que si l'écart se voit, et l'écart à franchir évite de battre entre deux paliers.
 */
export function updateHeat(now: number, celsius: number | null, reduced: boolean): void {
  if (now < checkAt) return;
  checkAt = now + (reduced ? CHECK_REDUCED_MS : CHECK_MS);
  if (celsius === null) {
    if (noExhaust) return;
    noExhaust = true;
    show(NO_EXHAUST);
    return;
  }
  const wanted = clampC(celsius);
  const quantum = reduced ? QUANTUM_REDUCED_C : HEAT_STEP_C;
  if (!noExhaust && Math.abs(wanted - shownC) < quantum) return;
  noExhaust = false;
  shownC = HEAT_MIN_C + levelOf(wanted) * HEAT_STEP_C;
  show(TABLE[levelOf(wanted)]);
}
