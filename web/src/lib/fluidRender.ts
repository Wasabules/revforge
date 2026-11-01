/**
 * Ce que le gaz devient à l'écran : couleurs précalculées (aucune chaîne fabriquée à chaque image),
 * images des grilles 2D, distorsion par la chaleur. Le calcul, lui, est dans `fluid.ts`.
 */
import type { Theme } from './canvas';
import type { Chamber, GasFlow } from './fluid';
import { CELL_INLET, CELL_WALL, type FluidField } from './fluidField';
import { AMBIENT_C, heatRgb, HEAT_MIN_C } from './heat';

const T_LEVELS = 64;
const Y_LEVELS = 8;
const T_FROM = 293;
const T_TO = 1350;
const AIR_TEMPS = 16;
const AIR_DENSITIES = 6;

/** Le gaz à l'ambiante se fond presque dans le tube : le thème, lui, ne descend pas sous sa couleur de métal froid. */
const COLD_GAS: [number, number, number] = [30, 26, 22];

/** Du tube froid au gaz le plus chaud : la même échelle que le thème, à température égale. */
function thermal(kelvin: number): [number, number, number] {
  const celsius = kelvin - 273.15;
  const hot = heatRgb(celsius);
  if (celsius >= HEAT_MIN_C) return hot;
  const f = Math.max(0, (celsius - AMBIENT_C) / (HEAT_MIN_C - AMBIENT_C));
  return [COLD_GAS[0] + (hot[0] - COLD_GAS[0]) * f, COLD_GAS[1] + (hot[1] - COLD_GAS[1]) * f, COLD_GAS[2] + (hot[2] - COLD_GAS[2]) * f];
}

/** De l'air, pas du gaz brûlé : teinte froide, même chauffé par les parois. */
function airColor(kelvin: number): [number, number, number] {
  const f = Math.max(0, Math.min(1, (kelvin - 293) / 500));
  return [38 + 96 * f, 56 + 86 * f, 82 + 78 * f];
}

function parseHex(hex: string): [number, number, number] {
  const value = parseInt(hex.replace('#', ''), 16);
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

function luminance(rgb: [number, number, number]): number {
  return (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255;
}

export class GasPainter {
  private gas: GasFlow;
  private theme: Theme | null = null;
  /** Couleur d'une cellule de tube selon sa température et sa part de gaz brûlé. */
  private tubeStyles: string[] = [];
  private airStyles: string[] = [];
  private airFor: Theme | null = null;
  private tubeR = new Uint8ClampedArray(T_LEVELS * Y_LEVELS);
  private tubeG = new Uint8ClampedArray(T_LEVELS * Y_LEVELS);
  private tubeB = new Uint8ClampedArray(T_LEVELS * Y_LEVELS);
  private heatR = new Uint8ClampedArray(T_LEVELS);
  private heatG = new Uint8ClampedArray(T_LEVELS);
  private heatB = new Uint8ClampedArray(T_LEVELS);
  private hazeR = 200;
  private hazeG = 190;
  private hazeB = 180;
  private sootR = 40;
  private sootG = 38;
  private sootB = 36;
  private dark = true;

  private plumeCanvas: HTMLCanvasElement | null = null;
  private plumeContext: CanvasRenderingContext2D | null = null;
  private plumeImage: ImageData | null = null;
  private plumeFrame = -1;
  /** Le panache s'efface vers le haut et vers le large : jamais de bord net là où la grille s'arrête. */
  private plumeFade: Float32Array | null = null;

  private chamberCanvas: HTMLCanvasElement | null = null;
  private chamberContext: CanvasRenderingContext2D | null = null;
  private chamberImages: ImageData[] = [];
  private chamberFrame = -1;
  private chamberOwner: Chamber[] | null = null;
  private chamberTops: number[] = [];

  private scratch: HTMLCanvasElement | null = null;
  private scratchContext: CanvasRenderingContext2D | null = null;

  constructor(gas: GasFlow) {
    this.gas = gas;
  }

  private adopt(theme: Theme): void {
    if (theme === this.theme) return;
    this.theme = theme;
    const surface = theme.surface.startsWith('#') ? parseHex(theme.surface) : ([22, 26, 33] as [number, number, number]);
    this.dark = luminance(surface) < 0.45;
    if (this.dark) {
      this.hazeR = 216;
      this.hazeG = 208;
      this.hazeB = 198;
      this.sootR = 92;
      this.sootG = 88;
      this.sootB = 84;
    } else {
      this.hazeR = 118;
      this.hazeG = 118;
      this.hazeB = 122;
      this.sootR = 34;
      this.sootG = 32;
      this.sootB = 30;
    }
    this.tubeStyles = new Array(T_LEVELS * Y_LEVELS);
    for (let y = 0; y < Y_LEVELS; y++) {
      const share = y / (Y_LEVELS - 1);
      for (let t = 0; t < T_LEVELS; t++) {
        const kelvin = T_FROM + ((T_TO - T_FROM) * t) / (T_LEVELS - 1);
        const hot = thermal(kelvin);
        const air = airColor(kelvin);
        const r = air[0] + (hot[0] - air[0]) * share;
        const g = air[1] + (hot[1] - air[1]) * share;
        const b = air[2] + (hot[2] - air[2]) * share;
        const at = y * T_LEVELS + t;
        this.tubeR[at] = r;
        this.tubeG[at] = g;
        this.tubeB[at] = b;
        this.tubeStyles[at] = `rgb(${Math.round(r)}, ${Math.round(g)}, ${Math.round(b)})`;
      }
    }
    for (let t = 0; t < T_LEVELS; t++) {
      const hot = thermal(T_FROM + ((T_TO - T_FROM) * t) / (T_LEVELS - 1));
      this.heatR[t] = hot[0];
      this.heatG[t] = hot[1];
      this.heatB[t] = hot[2];
    }
  }

  /**
   * L'air de l'admission : sa teinte dit sa température (de l'air frais à 170 °C), sa force dit sa densité.
   * Sur la chaîne d'un turbo, l'écart entre tube chaud et tube froid se lit d'un coup d'œil.
   */
  airStyle(theme: Theme, kelvin: number, density: number): string {
    this.adopt(theme);
    if (this.airFor !== theme) {
      this.airFor = theme;
      this.airStyles = [];
      const cool = parseHex(theme.cool);
      const ground = parseHex(theme.bg);
      for (let d = 0; d < AIR_DENSITIES; d++) {
        for (let t = 0; t < AIR_TEMPS; t++) {
          const share = t / (AIR_TEMPS - 1);
          const strength = 0.3 + (0.7 * d) / (AIR_DENSITIES - 1);
          const r = Math.round(ground[0] + (cool[0] + (232 - cool[0]) * share - ground[0]) * strength);
          const g = Math.round(ground[1] + (cool[1] + (106 - cool[1]) * share - ground[1]) * strength);
          const b = Math.round(ground[2] + (cool[2] + (32 - cool[2]) * share - ground[2]) * strength);
          this.airStyles.push(`rgb(${r}, ${g}, ${b})`);
        }
      }
    }
    let t = ((kelvin - 293) / 150) * (AIR_TEMPS - 1);
    t = t < 0 ? 0 : t > AIR_TEMPS - 1 ? AIR_TEMPS - 1 : t | 0;
    let d = (density / 2.8) * (AIR_DENSITIES - 1);
    d = d < 0 ? 0 : d > AIR_DENSITIES - 1 ? AIR_DENSITIES - 1 : d | 0;
    return this.airStyles[d * AIR_TEMPS + t];
  }

  /** Couleur d'un gaz à cette température (K) et cette part de gaz brûlé (0 à 1). */
  gasStyle(theme: Theme, kelvin: number, burnt: number): string {
    this.adopt(theme);
    let t = ((kelvin - T_FROM) / (T_TO - T_FROM)) * (T_LEVELS - 1);
    t = t < 0 ? 0 : t > T_LEVELS - 1 ? T_LEVELS - 1 : t | 0;
    let y = burnt * (Y_LEVELS - 1) + 0.5;
    y = y < 0 ? 0 : y > Y_LEVELS - 1 ? Y_LEVELS - 1 : y | 0;
    return this.tubeStyles[y * T_LEVELS + t];
  }

  /** La même couleur en nombres (0 à 255), pour la 3D : `out[0..2]`. */
  gasRgb(theme: Theme, kelvin: number, burnt: number, out: { [i: number]: number }): void {
    this.adopt(theme);
    let t = ((kelvin - T_FROM) / (T_TO - T_FROM)) * (T_LEVELS - 1);
    t = t < 0 ? 0 : t > T_LEVELS - 1 ? T_LEVELS - 1 : t | 0;
    let y = burnt * (Y_LEVELS - 1) + 0.5;
    y = y < 0 ? 0 : y > Y_LEVELS - 1 ? Y_LEVELS - 1 : y | 0;
    const at = y * T_LEVELS + t;
    out[0] = this.tubeR[at];
    out[1] = this.tubeG[at];
    out[2] = this.tubeB[at];
  }

  /** L'image du panache, une seule fois par image même si deux vues la demandent. */
  private refreshPlume(): CanvasImageSource | null {
    const field = this.gas.plume;
    if (!this.plumeCanvas) {
      this.plumeCanvas = document.createElement('canvas');
      this.plumeCanvas.width = field.nx;
      this.plumeCanvas.height = field.ny;
      this.plumeContext = this.plumeCanvas.getContext('2d');
      this.plumeImage = this.plumeContext?.createImageData(field.nx, field.ny) ?? null;
    }
    const image = this.plumeImage;
    const context = this.plumeContext;
    if (!image || !context) return null;
    const frame = this.gas.net.time;
    if (frame !== this.plumeFrame) {
      this.plumeFrame = frame;
      this.fillPlume(field, image.data);
      context.putImageData(image, 0, 0);
    }
    return this.plumeCanvas;
  }

  private fillPlume(field: FluidField, data: Uint8ClampedArray): void {
    const { nx, ny, temp, smoke, soot, flame, kind } = field;
    const { heatR, heatG, heatB, hazeR, hazeG, hazeB, sootR, sootG, sootB } = this;
    if (!this.plumeFade) {
      this.plumeFade = new Float32Array(nx * ny);
      const margin = 10;
      for (let j = 0; j < ny; j++) {
        for (let i = 0; i < nx; i++) {
          const top = Math.min(1, (ny - 1 - j) / margin);
          const far = Math.min(1, (nx - 1 - i) / margin);
          this.plumeFade[i + j * nx] = top * top * (3 - 2 * top) * far * far * (3 - 2 * far);
        }
      }
    }
    const fade = this.plumeFade;
    const useSoot = field.sootActive && soot !== null;
    const useFlame = field.flameActive && flame !== null;
    // Ligne du bas de l'image = haut du champ : le sol du calcul est en bas à l'écran.
    for (let j = 0; j < ny; j++) {
      let out = (ny - 1 - j) * nx * 4;
      for (let i = 0, c = j * nx; i < nx; i++, c++, out += 4) {
        const type = kind[c];
        if (type === CELL_WALL || type === CELL_INLET) {
          data[out + 3] = 0;
          continue;
        }
        const heat = temp[c];
        const s = smoke[c];
        if (s < 0.004 && heat < 8 && !useSoot && !useFlame) {
          data[out + 3] = 0;
          continue;
        }
        let t = (heat / (T_TO - T_FROM)) * (T_LEVELS - 1);
        t = t < 0 ? 0 : t > T_LEVELS - 1 ? T_LEVELS - 1 : t | 0;
        // Brume : du gaz visible ; lueur : un gaz encore assez chaud pour rougir.
        let alpha = s * 2.8;
        if (alpha > 0.92) alpha = 0.92;
        const glow = heat > 160 ? (heat - 160) / 480 : 0;
        const tint = glow > 1 ? 1 : glow;
        let r = hazeR + (heatR[t] - hazeR) * tint;
        let g = hazeG + (heatG[t] - hazeG) * tint;
        let b = hazeB + (heatB[t] - hazeB) * tint;
        if (tint > 0.01) {
          const lit = tint * 0.5;
          if (lit > alpha) alpha = lit;
        } else if (heat > 30) {
          // Un gaz chaud sans fumée : à peine une teinte, la distorsion fait le reste.
          const faint = heat / 2400;
          if (faint > alpha) alpha = faint;
        }
        if (useSoot) {
          const k = soot![c] * 1.8;
          if (k > 0.004) {
            const share = k > 1 ? 1 : k;
            r += (sootR - r) * share;
            g += (sootG - g) * share;
            b += (sootB - b) * share;
            const a = share * 0.9;
            if (a > alpha) alpha = a;
          }
        }
        if (useFlame) {
          const f = flame![c] * 2.2;
          if (f > 0.01) {
            const share = f > 1 ? 1 : f;
            // Le cœur de la flamme tire vers le jaune pâle, ses bords vers l'orange.
            const fr = 255;
            const fg = 130 + 110 * share;
            const fb = 30 + 150 * share * share;
            r += (fr - r) * share;
            g += (fg - g) * share;
            b += (fb - b) * share;
            if (share > alpha) alpha = share;
          }
        }
        data[out] = r;
        data[out + 1] = g;
        data[out + 2] = b;
        data[out + 3] = alpha * fade[c] * 255;
      }
    }
  }

  /**
   * Dessine le panache : la sortie du tube tombe au point (`exitX`, `exitY`), à `scale` pixels par cellule.
   * `direction` : 1 vers la droite, -1 vers la gauche.
   */
  drawPlume(ctx: CanvasRenderingContext2D, theme: Theme, exitX: number, exitY: number, scale: number, direction: 1 | -1, pipeCells: number, exitRow: number): void {
    this.adopt(theme);
    const source = this.refreshPlume();
    if (!source) return;
    const field = this.gas.plume;
    const w = field.nx * scale;
    const h = field.ny * scale;
    const top = exitY - (field.ny - 0.5 - exitRow) * scale;
    const left = -pipeCells * scale;
    ctx.save();
    ctx.translate(exitX, top);
    ctx.scale(direction, 1);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'low';
    ctx.drawImage(source, left, 0, w, h);
    ctx.restore();
  }

  /**
   * Distorsion par la chaleur : une zone chaude dévie la lumière selon le gradient de température, comme
   * en strioscopie. On recopie ce qui est déjà dessiné derrière le panache, par petits carreaux décalés.
   */
  shimmer(ctx: CanvasRenderingContext2D, canvas: HTMLCanvasElement, ratio: number, exitX: number, exitY: number, scale: number, direction: 1 | -1, pipeCells: number, exitRow: number, strength: number): void {
    const field = this.gas.plume;
    const { nx, ny, temp } = field;
    // Des carreaux assez grands : une dizaine de pixels suffit à l'œil, et chacun coûte une copie.
    const box = Math.max(3, Math.round(10 / Math.max(scale, 0.5)));
    const size = box * scale;
    if (size < 2) return;
    let budget = 45;
    const widthPx = nx * scale;
    const heightPx = ny * scale;
    const top = exitY - (ny - 0.5 - exitRow) * scale;
    const left = direction > 0 ? exitX - pipeCells * scale : exitX + pipeCells * scale - widthPx;
    const x0 = Math.max(0, Math.floor(left * ratio));
    const y0 = Math.max(0, Math.floor(top * ratio));
    const x1 = Math.min(canvas.width, Math.ceil((left + widthPx) * ratio));
    const y1 = Math.min(canvas.height, Math.ceil((top + heightPx) * ratio));
    const sw = x1 - x0;
    const sh = y1 - y0;
    if (sw <= 0 || sh <= 0) return;
    if (!this.scratch) {
      this.scratch = document.createElement('canvas');
      this.scratchContext = this.scratch.getContext('2d');
    }
    const scratch = this.scratch;
    const context = this.scratchContext;
    if (!context) return;
    if (scratch.width < sw || scratch.height < sh) {
      scratch.width = Math.max(scratch.width, sw);
      scratch.height = Math.max(scratch.height, sh);
    }
    context.globalCompositeOperation = 'copy';
    context.drawImage(canvas, x0, y0, sw, sh, 0, 0, sw, sh);
    context.globalCompositeOperation = 'source-over';
    const sizeDevice = size * ratio;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    for (let j = 1; j + box + 1 < ny; j += box) {
      for (let i = 1; i + box + 1 < nx; i += box) {
        const c = i + (box >> 1) + (j + (box >> 1)) * nx;
        const hot = temp[c];
        if (hot < 40) continue;
        // Gradient de température : la déviation lui est proportionnelle.
        const reach = box >> 1;
        const gx = (temp[c + reach] - temp[c - reach]) / reach;
        const gy = (temp[c + reach * nx] - temp[c - reach * nx]) / reach;
        let dx = direction * gx * strength * ratio;
        let dy = -gy * strength * ratio;
        if (dx * dx + dy * dy < 0.04) continue;
        const limit = 3.5 * ratio;
        dx = dx > limit ? limit : dx < -limit ? -limit : dx;
        dy = dy > limit ? limit : dy < -limit ? -limit : dy;
        const px = direction > 0 ? left + i * scale : left + widthPx - (i + box) * scale;
        const py = top + (ny - 1 - j - box) * scale;
        const dxPx = Math.round(px * ratio);
        const dyPx = Math.round(py * ratio);
        const sx = dxPx - x0;
        const sy = dyPx - y0;
        if (sx < 0 || sy < 0 || sx + sizeDevice > sw || sy + sizeDevice > sh) continue;
        ctx.drawImage(scratch, sx, sy, sizeDevice, sizeDevice, dxPx + dx, dyPx + dy, sizeDevice, sizeDevice);
        if (--budget <= 0) {
          ctx.restore();
          return;
        }
      }
    }
    ctx.restore();
  }

  /** Prépare les images des volumes : température en teinte, vitesse en éclat. */
  private refreshChambers(theme: Theme): void {
    this.adopt(theme);
    const gas = this.gas;
    const chambers = gas.chambers;
    if (chambers !== this.chamberOwner) {
      this.chamberOwner = chambers;
      let width = 1;
      let height = 0;
      this.chamberTops = [];
      for (const chamber of chambers) {
        width = Math.max(width, chamber.field.nx);
        this.chamberTops.push(height);
        height += chamber.field.ny;
      }
      if (!this.chamberCanvas) {
        this.chamberCanvas = document.createElement('canvas');
      }
      this.chamberCanvas.width = width;
      this.chamberCanvas.height = Math.max(height, 1);
      this.chamberContext = this.chamberCanvas.getContext('2d');
      this.chamberImages = chambers.map((chamber) => this.chamberContext!.createImageData(chamber.field.nx, chamber.field.ny));
      this.chamberFrame = -1;
    }
    const frame = gas.net.time;
    if (frame === this.chamberFrame || !this.chamberContext) return;
    this.chamberFrame = frame;
    for (let index = 0; index < chambers.length; index++) {
      const { field } = chambers[index];
      const { nx, ny, temp, smoke, u, v, kind } = field;
      const data = this.chamberImages[index].data;
      const reference = 22;
      for (let j = 0; j < ny; j++) {
        let out = (ny - 1 - j) * nx * 4;
        for (let i = 0, c = j * nx; i < nx; i++, c++, out += 4) {
          if (kind[c] === CELL_WALL) {
            data[out + 3] = 0;
            continue;
          }
          let t = (temp[c] / (T_TO - T_FROM)) * (T_LEVELS - 1);
          t = t < 0 ? 0 : t > T_LEVELS - 1 ? T_LEVELS - 1 : t | 0;
          let y = smoke[c] * (Y_LEVELS - 1) + 0.5;
          y = y < 0 ? 0 : y > Y_LEVELS - 1 ? Y_LEVELS - 1 : y | 0;
          const at = y * T_LEVELS + t;
          const speed = Math.abs(u[c]) + Math.abs(v[c]);
          const lift = speed >= reference ? 1 : speed / reference;
          // Le jet ressort, le gaz qui tourne lentement reste sombre : c'est là qu'on lit la recirculation.
          const k = 0.5 + 0.5 * lift;
          data[out] = this.tubeR[at] * k + 60 * lift * lift;
          data[out + 1] = this.tubeG[at] * k + 50 * lift * lift;
          data[out + 2] = this.tubeB[at] * k + 40 * lift * lift;
          data[out + 3] = 255;
        }
      }
      this.chamberContext.putImageData(this.chamberImages[index], 0, this.chamberTops[index]);
    }
  }

  drawChamber(ctx: CanvasRenderingContext2D, theme: Theme, index: number, x: number, y: number, w: number, h: number): void {
    this.refreshChambers(theme);
    const canvas = this.chamberCanvas;
    if (!canvas) return;
    const { field } = this.gas.chambers[index];
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'low';
    ctx.drawImage(canvas, 0, this.chamberTops[index], field.nx, field.ny, x, y, w, h);
  }
}
