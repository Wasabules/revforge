/**
 * Un écoulement à deux dimensions, sur une petite grille : méthode « stable fluids » de Stam
 * (advection semi-lagrangienne, projection par la pression), avec le confinement de tourbillons de
 * Fedkiw, Stam et Jensen pour rendre les enroulements que la grille gomme.
 * Sert au panache derrière la voiture et à la recirculation dans les volumes de la ligne.
 * Rien n'alloue pendant le calcul. Les vitesses sont en m/s, le temps en secondes physiques.
 */

export const CELL_FLUID = 0;
export const CELL_WALL = 1;
/** Air libre : pression fixée, l'écoulement y entre ou en sort à son gré. */
export const CELL_OPEN = 2;
/** Vitesse et traceurs imposés de l'extérieur. */
export const CELL_INLET = 3;

const OMEGA = 1.65;
const NOISE_SIZE = 64;

export class FluidField {
  readonly nx: number;
  readonly ny: number;
  /** Taille d'une cellule (m). */
  readonly h: number;
  readonly count: number;
  u: Float32Array;
  v: Float32Array;
  private u0: Float32Array;
  private v0: Float32Array;
  /** Pression, gardée d'une image à l'autre : le calcul repart de la bonne solution. */
  pressure: Float32Array;
  private divergence: Float32Array;
  private curl: Float32Array;
  /** Écart de température à l'air ambiant (K). */
  temp: Float32Array;
  private temp0: Float32Array;
  /** Gaz visible : brume, vapeur (`smoke`) ; suies (`soot`) ; flamme (`flame`). */
  smoke: Float32Array;
  private smoke0: Float32Array;
  soot: Float32Array | null = null;
  private soot0: Float32Array | null = null;
  flame: Float32Array | null = null;
  private flame0: Float32Array | null = null;
  kind: Uint8Array;
  /** Nid d'abeille : le gaz n'y avance que dans l'axe. */
  straightener: Uint8Array | null = null;
  private backX: Float32Array;
  private backY: Float32Array;
  /** Cellules à résoudre, en damier : celles du milieu, vite, et celles qui touchent un bord, avec plus de soin. */
  private redInner = new Int32Array(0);
  private blackInner = new Int32Array(0);
  private redEdge = new Int32Array(0);
  private blackEdge = new Int32Array(0);
  private invNeighbours: Float32Array;
  private neighbours: Uint8Array;
  /** Les traceurs facultatifs ne coûtent rien tant qu'ils sont vides. */
  sootActive = false;
  flameActive = false;

  /** Itérations de pression par pas. */
  iterations = 14;
  /** Vent uniforme (m/s) : l'air que traverse une voiture qui roule. */
  windX = 0;
  /** Gravité appliquée à l'écart de température (m/s²). */
  gravity = 0;
  /** Force du confinement de tourbillons, sans dimension. */
  confinement = 0;
  /** Temps de refroidissement, de dissipation de la brume et de la flamme (s). */
  coolTau = 0.4;
  smokeTau = 0.8;
  flameTau = 0.012;
  /** Perte de vitesse (s) : en vrai, le jet s'étale dans la troisième dimension, ce qu'une grille plane ignore. */
  dragTau = 0;
  /** Température ambiante (K). */
  ambient = 293;
  /**
   * Agitation à petite échelle, proportionnelle à la vitesse locale. Une grille plane garde des tourbillons
   * trop réguliers, qu'un vrai jet en trois dimensions casse vite : c'est un modèle, pas une résolution.
   */
  turbulence = 0;
  private noiseU: Float32Array | null = null;
  private noiseV: Float32Array | null = null;
  private noisePhase = 0;

  constructor(nx: number, ny: number, h: number, withSoot = false, withFlame = false) {
    const n = nx * ny;
    this.nx = nx;
    this.ny = ny;
    this.h = h;
    this.count = n;
    this.u = new Float32Array(n);
    this.v = new Float32Array(n);
    this.u0 = new Float32Array(n);
    this.v0 = new Float32Array(n);
    this.pressure = new Float32Array(n);
    this.divergence = new Float32Array(n);
    this.curl = new Float32Array(n);
    this.temp = new Float32Array(n);
    this.temp0 = new Float32Array(n);
    this.smoke = new Float32Array(n);
    this.smoke0 = new Float32Array(n);
    if (withSoot) {
      this.soot = new Float32Array(n);
      this.soot0 = new Float32Array(n);
    }
    if (withFlame) {
      this.flame = new Float32Array(n);
      this.flame0 = new Float32Array(n);
    }
    this.kind = new Uint8Array(n);
    this.backX = new Float32Array(n);
    this.backY = new Float32Array(n);
    this.invNeighbours = new Float32Array(n);
    this.neighbours = new Uint8Array(n);
  }

  /** À appeler une fois les types de cellules posés. */
  finalize(): void {
    const { nx, ny, kind, neighbours, invNeighbours } = this;
    const redInner: number[] = [];
    const blackInner: number[] = [];
    const redEdge: number[] = [];
    const blackEdge: number[] = [];
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const c = i + j * nx;
        if (kind[c] !== CELL_FLUID) continue;
        // Les parois et les entrées ne comptent pas : la pression n'y a pas de gradient normal.
        let mask = 0;
        let count = 0;
        if (i + 1 < nx && kind[c + 1] !== CELL_WALL && kind[c + 1] !== CELL_INLET) { mask |= 1; count++; }
        if (i > 0 && kind[c - 1] !== CELL_WALL && kind[c - 1] !== CELL_INLET) { mask |= 2; count++; }
        if (j + 1 < ny && kind[c + nx] !== CELL_WALL && kind[c + nx] !== CELL_INLET) { mask |= 4; count++; }
        if (j > 0 && kind[c - nx] !== CELL_WALL && kind[c - nx] !== CELL_INLET) { mask |= 8; count++; }
        neighbours[c] = mask;
        invNeighbours[c] = count > 0 ? 1 / count : 0;
        const even = (i + j) % 2 === 0;
        if (mask === 15) (even ? redInner : blackInner).push(c);
        else (even ? redEdge : blackEdge).push(c);
      }
    }
    this.redInner = Int32Array.from(redInner);
    this.blackInner = Int32Array.from(blackInner);
    this.redEdge = Int32Array.from(redEdge);
    this.blackEdge = Int32Array.from(blackEdge);
  }

  clear(): void {
    this.u.fill(0);
    this.v.fill(0);
    this.pressure.fill(0);
    this.temp.fill(0);
    this.smoke.fill(0);
    this.soot?.fill(0);
    this.flame?.fill(0);
  }

  /** Un pas de calcul. Les entrées (`CELL_INLET`) doivent avoir été posées juste avant. */
  step(dt: number): void {
    this.computeBacktrace(dt);
    this.advectMain(Math.exp(-dt / this.coolTau), Math.exp(-dt / this.smokeTau));
    if (this.sootActive && this.soot && this.soot0) {
      const swap = this.soot;
      this.advect(swap, this.soot0, Math.exp(-dt / (this.smokeTau * 1.4)));
      this.soot = this.soot0;
      this.soot0 = swap;
    }
    if (this.flameActive && this.flame && this.flame0) {
      const swap = this.flame;
      this.advect(swap, this.flame0, Math.exp(-dt / this.flameTau));
      this.flame = this.flame0;
      this.flame0 = swap;
    }
    this.applyForces(dt);
    this.project();
    this.applyBoundaries();
  }

  /** Remonte le courant : d'où vient le gaz qui arrive dans chaque cellule ? */
  private computeBacktrace(dt: number): void {
    const { nx, ny, u, v, backX, backY, kind } = this;
    const k = dt / this.h;
    const wind = this.windX;
    const maxX = nx - 1.001;
    const maxY = ny - 1.001;
    for (let j = 0, c = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++, c++) {
        if (kind[c] === CELL_WALL) continue;
        // Euler vers l'arrière, puis un second pas avec la vitesse lue là-bas : plus juste, à peine plus cher.
        const x1 = i - 0.5 * k * (u[c] + wind);
        const y1 = j - 0.5 * k * v[c];
        const xm = x1 < 0 ? 0 : x1 > maxX ? maxX : x1;
        const ym = y1 < 0 ? 0 : y1 > maxY ? maxY : y1;
        const ix = xm | 0;
        const iy = ym | 0;
        const fx = xm - ix;
        const fy = ym - iy;
        const at = ix + iy * nx;
        const gx = 1 - fx;
        const gy = 1 - fy;
        const um = gy * (gx * u[at] + fx * u[at + 1]) + fy * (gx * u[at + nx] + fx * u[at + nx + 1]);
        const vm = gy * (gx * v[at] + fx * v[at + 1]) + fy * (gx * v[at + nx] + fx * v[at + nx + 1]);
        const x2 = i - k * (um + wind);
        const y2 = j - k * vm;
        backX[c] = x2 < 0 ? 0 : x2 > maxX ? maxX : x2;
        backY[c] = y2 < 0 ? 0 : y2 > maxY ? maxY : y2;
      }
    }
  }

  /** Vitesse, température et brume ensemble : mêmes poids d'interpolation, bien moins cher qu'un champ après l'autre. */
  private advectMain(keepTemp: number, keepSmoke: number): void {
    const { nx, count, backX, backY, kind, u, v, temp, smoke, u0, v0, temp0, smoke0 } = this;
    for (let c = 0; c < count; c++) {
      const type = kind[c];
      if (type !== CELL_FLUID && type !== CELL_OPEN) {
        u0[c] = u[c];
        v0[c] = v[c];
        temp0[c] = temp[c];
        smoke0[c] = smoke[c];
        continue;
      }
      const x = backX[c];
      const y = backY[c];
      const ix = x | 0;
      const iy = y | 0;
      const fx = x - ix;
      const fy = y - iy;
      const a = ix + iy * nx;
      const b = a + 1;
      const d = a + nx;
      const e = d + 1;
      const w00 = (1 - fx) * (1 - fy);
      const w10 = fx * (1 - fy);
      const w01 = (1 - fx) * fy;
      const w11 = fx * fy;
      u0[c] = w00 * u[a] + w10 * u[b] + w01 * u[d] + w11 * u[e];
      v0[c] = w00 * v[a] + w10 * v[b] + w01 * v[d] + w11 * v[e];
      temp0[c] = keepTemp * (w00 * temp[a] + w10 * temp[b] + w01 * temp[d] + w11 * temp[e]);
      smoke0[c] = keepSmoke * (w00 * smoke[a] + w10 * smoke[b] + w01 * smoke[d] + w11 * smoke[e]);
    }
    this.u = u0;
    this.u0 = u;
    this.v = v0;
    this.v0 = v;
    this.temp = temp0;
    this.temp0 = temp;
    this.smoke = smoke0;
    this.smoke0 = smoke;
  }

  private advect(source: Float32Array, target: Float32Array, keep: number): void {
    const { nx, count, backX, backY, kind } = this;
    for (let c = 0; c < count; c++) {
      if (kind[c] !== CELL_FLUID && kind[c] !== CELL_OPEN) {
        target[c] = source[c];
        continue;
      }
      const x = backX[c];
      const y = backY[c];
      const ix = x | 0;
      const iy = y | 0;
      const fx = x - ix;
      const fy = y - iy;
      const at = ix + iy * nx;
      const gx = 1 - fx;
      target[c] = keep * ((1 - fy) * (gx * source[at] + fx * source[at + 1]) + fy * (gx * source[at + nx] + fx * source[at + nx + 1]));
    }
  }

  private applyForces(dt: number): void {
    const { count, u, v, temp, kind, straightener } = this;
    const lift = this.gravity * dt;
    const keep = this.dragTau > 0 ? Math.exp(-dt / this.dragTau) : 1;
    if (lift !== 0 || keep !== 1) {
      const ambient = this.ambient;
      for (let c = 0; c < count; c++) {
        if (kind[c] !== CELL_FLUID) continue;
        u[c] *= keep;
        v[c] = v[c] * keep + (lift * temp[c]) / (ambient + temp[c]);
      }
    }
    if (straightener) {
      for (let c = 0; c < count; c++) if (straightener[c] === 1) v[c] = 0;
    }
    if (this.confinement > 0) this.applyConfinement(dt);
    if (this.turbulence > 0) this.applyTurbulence();
  }

  private applyTurbulence(): void {
    const { nx, ny, u, v, kind } = this;
    if (!this.noiseU || !this.noiseV) {
      // Un bruit à divergence nulle (le rotationnel d'un potentiel aléatoire lissé), tiré une fois pour toutes.
      const size = NOISE_SIZE;
      const potential = new Float32Array(size * size);
      let state = 0x51ed27;
      for (let i = 0; i < potential.length; i++) {
        state = (state * 1664525 + 1013904223) | 0;
        potential[i] = (state >>> 16) / 65536 - 0.5;
      }
      const smooth = new Float32Array(size * size);
      const at = (x: number, y: number) => ((x + size) % size) + ((y + size) % size) * size;
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          let sum = 0;
          for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) sum += potential[at(x + dx, y + dy)];
          smooth[at(x, y)] = sum / 25;
        }
      }
      this.noiseU = new Float32Array(size * size);
      this.noiseV = new Float32Array(size * size);
      let top = 1e-6;
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const nu = smooth[at(x, y + 1)] - smooth[at(x, y - 1)];
          const nv = smooth[at(x - 1, y)] - smooth[at(x + 1, y)];
          this.noiseU[at(x, y)] = nu;
          this.noiseV[at(x, y)] = nv;
          top = Math.max(top, Math.abs(nu), Math.abs(nv));
        }
      }
      for (let i = 0; i < size * size; i++) {
        this.noiseU[i] /= top;
        this.noiseV[i] /= top;
      }
    }
    const noiseU = this.noiseU;
    const noiseV = this.noiseV;
    const mask = NOISE_SIZE - 1;
    this.noisePhase = (this.noisePhase + 3) & mask;
    const shift = this.noisePhase;
    const k = this.turbulence;
    for (let j = 1; j < ny - 1; j++) {
      const row = ((j + shift * 2) & mask) * NOISE_SIZE;
      for (let i = 1; i < nx - 1; i++) {
        const c = i + j * nx;
        if (kind[c] !== CELL_FLUID) continue;
        const uu = u[c];
        const vv = v[c];
        const speed = (uu < 0 ? -uu : uu) + (vv < 0 ? -vv : vv);
        if (speed < 1) continue;
        const n = row + ((i - shift) & mask);
        u[c] = uu + k * speed * noiseU[n];
        v[c] = vv + k * speed * noiseV[n];
      }
    }
  }

  /** Redonne à chaque tourbillon l'énergie que la grille lui a retirée, sans en créer. */
  private applyConfinement(dt: number): void {
    const { nx, ny, u, v, curl, kind } = this;
    for (let j = 1; j < ny - 1; j++) {
      for (let i = 1; i < nx - 1; i++) {
        const c = i + j * nx;
        curl[c] = 0.5 * (v[c + 1] - v[c - 1] - u[c + nx] + u[c - nx]);
      }
    }
    const strength = this.confinement * dt / this.h;
    for (let j = 2; j < ny - 2; j++) {
      for (let i = 2; i < nx - 2; i++) {
        const c = i + j * nx;
        if (kind[c] !== CELL_FLUID) continue;
        const left = curl[c - 1];
        const right = curl[c + 1];
        const down = curl[c - nx];
        const up = curl[c + nx];
        const gx = 0.5 * ((right < 0 ? -right : right) - (left < 0 ? -left : left));
        const gy = 0.5 * ((up < 0 ? -up : up) - (down < 0 ? -down : down));
        const length = Math.sqrt(gx * gx + gy * gy) + 1e-3;
        const w = curl[c] * strength;
        u[c] += (w * gy) / length;
        v[c] -= (w * gx) / length;
      }
    }
  }

  /** Rend l'écoulement sans divergence : red-black avec surrelaxation, à partir de la pression précédente. */
  private project(): void {
    const { redInner, blackInner, redEdge, blackEdge } = this;
    this.computeDivergence(redInner);
    this.computeDivergence(blackInner);
    this.computeDivergence(redEdge);
    this.computeDivergence(blackEdge);
    for (let iter = 0; iter < this.iterations; iter++) {
      this.relaxInner(redInner);
      this.relaxEdge(redEdge);
      this.relaxInner(blackInner);
      this.relaxEdge(blackEdge);
    }
    this.subtractGradient(redInner);
    this.subtractGradient(blackInner);
    this.subtractGradient(redEdge);
    this.subtractGradient(blackEdge);
  }

  private computeDivergence(list: Int32Array): void {
    const { nx, u, v, divergence } = this;
    for (let k = 0; k < list.length; k++) {
      const c = list[k];
      divergence[c] = 0.5 * (u[c + 1] - u[c - 1] + v[c + nx] - v[c - nx]);
    }
  }

  private relaxInner(list: Int32Array): void {
    const { nx, pressure, divergence } = this;
    for (let k = 0; k < list.length; k++) {
      const c = list[k];
      const here = pressure[c];
      pressure[c] = here + OMEGA * (0.25 * (pressure[c + 1] + pressure[c - 1] + pressure[c + nx] + pressure[c - nx] - divergence[c]) - here);
    }
  }

  private relaxEdge(list: Int32Array): void {
    const { nx, pressure, divergence, neighbours, invNeighbours } = this;
    for (let k = 0; k < list.length; k++) {
      const c = list[k];
      const mask = neighbours[c];
      let sum = 0;
      if (mask & 1) sum += pressure[c + 1];
      if (mask & 2) sum += pressure[c - 1];
      if (mask & 4) sum += pressure[c + nx];
      if (mask & 8) sum += pressure[c - nx];
      pressure[c] += OMEGA * ((sum - divergence[c]) * invNeighbours[c] - pressure[c]);
    }
  }

  private subtractGradient(list: Int32Array): void {
    const { nx, u, v, pressure, neighbours } = this;
    for (let k = 0; k < list.length; k++) {
      const c = list[k];
      const here = pressure[c];
      const mask = neighbours[c];
      // Vers une paroi, le gradient normal est nul.
      const east = mask & 1 ? pressure[c + 1] : here;
      const west = mask & 2 ? pressure[c - 1] : here;
      const north = mask & 4 ? pressure[c + nx] : here;
      const south = mask & 8 ? pressure[c - nx] : here;
      u[c] -= 0.5 * (east - west);
      v[c] -= 0.5 * (north - south);
    }
  }

  /** Air libre : ce qui sort garde sa vitesse, ce qui rentre arrive sans traceur. */
  private applyBoundaries(): void {
    const { nx, ny, u, v, temp, smoke, kind } = this;
    for (let j = 0; j < ny; j++) {
      for (let i = 0; i < nx; i++) {
        const c = i + j * nx;
        if (kind[c] === CELL_WALL) {
          u[c] = 0;
          v[c] = 0;
          temp[c] = 0;
          smoke[c] = 0;
        } else if (kind[c] === CELL_OPEN) {
          // Sur un bord ouvert, le voisin intérieur donne la vitesse.
          const inner = i === 0 ? c + 1 : i === nx - 1 ? c - 1 : j === 0 ? c + nx : j === ny - 1 ? c - nx : c;
          u[c] = u[inner];
          v[c] = v[inner];
        }
      }
    }
  }

  /** Plus grande vitesse présente (m/s). */
  maxSpeed(): number {
    const { count, u, v } = this;
    let top = 0;
    for (let c = 0; c < count; c++) {
      const s = u[c] * u[c] + v[c] * v[c];
      if (s > top) top = s;
    }
    return Math.sqrt(top);
  }
}
