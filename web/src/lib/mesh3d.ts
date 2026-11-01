/**
 * Des maillages générés par le code, en tableaux simples : sans WebGL ni three.js, donc testables sans navigateur.
 * `MeshBuilder` assemble lathe, box, cylinder, sphere, extrude, extrudeRing, gear, tube ; `build()` donne les
 * tableaux, que `toGeometry` de `render3d.ts` change en géométrie three.js.
 *
 * Conventions : matrices `m34` en lignes, 3 × 4 (`Float32Array(12)`) ; repère direct, Y vers le haut ; faces
 * avant dans le sens trigonométrique, vues de l'extérieur. Rien ici n'est prévu pour tourner à chaque image.
 */

// ── Matrices 3 × 4, par lignes : rien n'alloue ──

export type Mat34 = Float32Array;

export const m34 = {
  create(): Mat34 {
    const m = new Float32Array(12);
    m[0] = m[5] = m[10] = 1;
    return m;
  },
  identity(out: Mat34): Mat34 {
    out.fill(0);
    out[0] = out[5] = out[10] = 1;
    return out;
  },
  translation(out: Mat34, x: number, y: number, z: number): Mat34 {
    m34.identity(out);
    out[3] = x;
    out[7] = y;
    out[11] = z;
    return out;
  },
  rotationX(out: Mat34, a: number): Mat34 {
    const c = Math.cos(a);
    const s = Math.sin(a);
    m34.identity(out);
    out[5] = c;
    out[6] = -s;
    out[9] = s;
    out[10] = c;
    return out;
  },
  rotationY(out: Mat34, a: number): Mat34 {
    const c = Math.cos(a);
    const s = Math.sin(a);
    m34.identity(out);
    out[0] = c;
    out[2] = s;
    out[8] = -s;
    out[10] = c;
    return out;
  },
  rotationZ(out: Mat34, a: number): Mat34 {
    const c = Math.cos(a);
    const s = Math.sin(a);
    m34.identity(out);
    out[0] = c;
    out[1] = -s;
    out[4] = s;
    out[5] = c;
    return out;
  },
  scaling(out: Mat34, x: number, y: number, z: number): Mat34 {
    out.fill(0);
    out[0] = x;
    out[5] = y;
    out[10] = z;
    return out;
  },
  /** out = a × b : b s'applique d'abord. `out` peut être `a` ou `b`. */
  multiply(out: Mat34, a: Mat34, b: Mat34): Mat34 {
    const a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
    const a4 = a[4], a5 = a[5], a6 = a[6], a7 = a[7];
    const a8 = a[8], a9 = a[9], a10 = a[10], a11 = a[11];
    const b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3];
    const b4 = b[4], b5 = b[5], b6 = b[6], b7 = b[7];
    const b8 = b[8], b9 = b[9], b10 = b[10], b11 = b[11];
    out[0] = a0 * b0 + a1 * b4 + a2 * b8;
    out[1] = a0 * b1 + a1 * b5 + a2 * b9;
    out[2] = a0 * b2 + a1 * b6 + a2 * b10;
    out[3] = a0 * b3 + a1 * b7 + a2 * b11 + a3;
    out[4] = a4 * b0 + a5 * b4 + a6 * b8;
    out[5] = a4 * b1 + a5 * b5 + a6 * b9;
    out[6] = a4 * b2 + a5 * b6 + a6 * b10;
    out[7] = a4 * b3 + a5 * b7 + a6 * b11 + a7;
    out[8] = a8 * b0 + a9 * b4 + a10 * b8;
    out[9] = a8 * b1 + a9 * b5 + a10 * b9;
    out[10] = a8 * b2 + a9 * b6 + a10 * b10;
    out[11] = a8 * b3 + a9 * b7 + a10 * b11 + a11;
    return out;
  },
  /** Applique à (x, y, z) ; le résultat va dans `out[0..2]`. */
  apply(out: ArrayLike<number> & { [i: number]: number }, m: Mat34, x: number, y: number, z: number): void {
    const px = m[0] * x + m[1] * y + m[2] * z + m[3];
    const py = m[4] * x + m[5] * y + m[6] * z + m[7];
    const pz = m[8] * x + m[9] * y + m[10] * z + m[11];
    out[0] = px;
    out[1] = py;
    out[2] = pz;
  },
};

// ── Maillages ──

/** Un maillage en tableaux simples. Sans `normals`, elles sont calculées à plat, face par face. */
export interface MeshData {
  positions: ArrayLike<number>;
  normals?: ArrayLike<number>;
  indices: ArrayLike<number>;
}

type P2 = readonly [number, number];
type P3 = readonly [number, number, number];

const TAU = Math.PI * 2;

/** Assemble un maillage pièce par pièce. Sert à construire une scène, pas à chaque image : il alloue. */
export class MeshBuilder {
  private p: number[] = [];
  private n: number[] = [];
  private i: number[] = [];
  /** Transformation appliquée à ce qu'on ajoute ensuite. */
  private matrix: Mat34 | null = null;

  /** Tout ce qui est ajouté ensuite passe par cette matrice (ou plus rien, avec `null`). */
  transform(matrix: Mat34 | null): this {
    this.matrix = matrix;
    return this;
  }

  get vertexCount(): number {
    return this.p.length / 3;
  }

  get triangleCount(): number {
    return this.i.length / 3;
  }

  private vertex(x: number, y: number, z: number, nx: number, ny: number, nz: number): number {
    const m = this.matrix;
    if (m) {
      const px = m[0] * x + m[1] * y + m[2] * z + m[3];
      const py = m[4] * x + m[5] * y + m[6] * z + m[7];
      const pz = m[8] * x + m[9] * y + m[10] * z + m[11];
      // La cotransposée : juste aussi pour une échelle différente selon l'axe.
      const a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
      const det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
      const sign = det < 0 ? -1 : 1;
      let qx = ((e * k - f * h) * nx + (f * g - d * k) * ny + (d * h - e * g) * nz) * sign;
      let qy = ((c * h - b * k) * nx + (a * k - c * g) * ny + (b * g - a * h) * nz) * sign;
      let qz = ((b * f - c * e) * nx + (c * d - a * f) * ny + (a * e - b * d) * nz) * sign;
      const len = Math.hypot(qx, qy, qz) || 1;
      qx /= len;
      qy /= len;
      qz /= len;
      this.p.push(px, py, pz);
      this.n.push(qx, qy, qz);
    } else {
      this.p.push(x, y, z);
      this.n.push(nx, ny, nz);
    }
    return this.p.length / 3 - 1;
  }

  /** Ajoute des tableaux bruts. */
  add(data: MeshData): this {
    const normals = data.normals ?? flatNormals(data.positions, data.indices);
    const base = this.vertexCount;
    for (let v = 0; v < data.positions.length / 3; v++) {
      this.vertex(data.positions[v * 3], data.positions[v * 3 + 1], data.positions[v * 3 + 2], normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
    }
    for (let k = 0; k < data.indices.length; k++) this.i.push(data.indices[k] + base);
    return this;
  }

  private quad(a: number, b: number, c: number, d: number): void {
    this.i.push(a, b, c, a, c, d);
  }

  /** Une boîte centrée sur l'origine. */
  box(w: number, h: number, d: number): this {
    const x = w / 2, y = h / 2, z = d / 2;
    const faces: [P3, P3, P3, P3, P3][] = [
      [[1, 0, 0], [x, -y, z], [x, -y, -z], [x, y, -z], [x, y, z]],
      [[-1, 0, 0], [-x, -y, -z], [-x, -y, z], [-x, y, z], [-x, y, -z]],
      [[0, 1, 0], [-x, y, z], [x, y, z], [x, y, -z], [-x, y, -z]],
      [[0, -1, 0], [-x, -y, -z], [x, -y, -z], [x, -y, z], [-x, -y, z]],
      [[0, 0, 1], [-x, -y, z], [x, -y, z], [x, y, z], [-x, y, z]],
      [[0, 0, -1], [x, -y, -z], [-x, -y, -z], [-x, y, -z], [x, y, -z]],
    ];
    for (const [nn, a, b, c, e] of faces) {
      const v0 = this.vertex(a[0], a[1], a[2], nn[0], nn[1], nn[2]);
      const v1 = this.vertex(b[0], b[1], b[2], nn[0], nn[1], nn[2]);
      const v2 = this.vertex(c[0], c[1], c[2], nn[0], nn[1], nn[2]);
      const v3 = this.vertex(e[0], e[1], e[2], nn[0], nn[1], nn[2]);
      this.quad(v0, v1, v2, v3);
    }
    return this;
  }

  /**
   * Une surface de révolution autour de l'axe Y. `profile` : points [rayon, y], de bas en haut pour
   * que les normales regardent vers l'extérieur. Une arête vive se garde au-delà de `creaseDeg`.
   */
  lathe(profile: readonly P2[], segments = 24, creaseDeg = 35): this {
    const cosCrease = Math.cos((creaseDeg * Math.PI) / 180);
    const edges = profile.length - 1;
    const dirs: [number, number][] = [];
    for (let e = 0; e < edges; e++) {
      const dr = profile[e + 1][0] - profile[e][0];
      const dy = profile[e + 1][1] - profile[e][1];
      const len = Math.hypot(dr, dy) || 1;
      // La normale d'une arête qui monte regarde vers l'extérieur.
      dirs.push([dy / len, -dr / len]);
    }
    for (let e = 0; e < edges; e++) {
      // Chaque arête a son anneau de départ et d'arrivée ; on lisse quand l'angle est faible.
      const n0 = e > 0 && dirs[e][0] * dirs[e - 1][0] + dirs[e][1] * dirs[e - 1][1] > cosCrease ? blend2(dirs[e - 1], dirs[e]) : dirs[e];
      const n1 = e + 1 < edges && dirs[e][0] * dirs[e + 1][0] + dirs[e][1] * dirs[e + 1][1] > cosCrease ? blend2(dirs[e], dirs[e + 1]) : dirs[e];
      const ringA = this.ring(profile[e][0], profile[e][1], n0, segments);
      const ringB = this.ring(profile[e + 1][0], profile[e + 1][1], n1, segments);
      for (let s = 0; s < segments; s++) {
        this.quad(ringA + s, ringA + s + 1, ringB + s + 1, ringB + s);
      }
    }
    return this;
  }

  private ring(radius: number, y: number, normal: readonly number[], segments: number): number {
    const first = this.vertexCount;
    for (let s = 0; s <= segments; s++) {
      // Le sens de parcours donne des faces avant vers l'extérieur, vues d'un repère direct.
      const a = -(s / segments) * TAU;
      const c = Math.cos(a);
      const si = Math.sin(a);
      this.vertex(radius * c, y, radius * si, normal[0] * c, normal[1], normal[0] * si);
    }
    return first;
  }

  /** Un cylindre plein d'axe Y, centré sur l'origine. */
  cylinder(radius: number, height: number, segments = 16): this {
    const y = height / 2;
    return this.lathe([[0, -y], [radius, -y], [radius, y], [0, y]], segments);
  }

  sphere(radius: number, segments = 16, rings = 10): this {
    const profile: [number, number][] = [];
    for (let r = 0; r <= rings; r++) {
      const a = -Math.PI / 2 + (r / rings) * Math.PI;
      profile.push([Math.cos(a) * radius, Math.sin(a) * radius]);
    }
    return this.lathe(profile, segments, 90);
  }

  /**
   * Un contour plat (x, y) extrudé selon Z, centré sur z = 0. `smoothDeg` : les arêtes plus douces que
   * cet angle sont lissées. Le contour peut être concave, sans trou ; il doit être parcouru dans le sens trigonométrique.
   */
  extrude(outline: readonly P2[], depth: number, smoothDeg = 30): this {
    const z = depth / 2;
    const n = outline.length;
    const edgeNormals: [number, number][] = [];
    for (let k = 0; k < n; k++) {
      const a = outline[k];
      const b = outline[(k + 1) % n];
      const dx = b[0] - a[0];
      const dy = b[1] - a[1];
      const len = Math.hypot(dx, dy) || 1;
      edgeNormals.push([dy / len, -dx / len]);
    }
    const cosSmooth = Math.cos((smoothDeg * Math.PI) / 180);
    for (let k = 0; k < n; k++) {
      const a = outline[k];
      const b = outline[(k + 1) % n];
      const ne = edgeNormals[k];
      const prev = edgeNormals[(k + n - 1) % n];
      const next = edgeNormals[(k + 1) % n];
      const na = ne[0] * prev[0] + ne[1] * prev[1] > cosSmooth ? blend2(prev, ne) : ne;
      const nb = ne[0] * next[0] + ne[1] * next[1] > cosSmooth ? blend2(ne, next) : ne;
      const v0 = this.vertex(a[0], a[1], z, na[0], na[1], 0);
      const v1 = this.vertex(a[0], a[1], -z, na[0], na[1], 0);
      const v2 = this.vertex(b[0], b[1], -z, nb[0], nb[1], 0);
      const v3 = this.vertex(b[0], b[1], z, nb[0], nb[1], 0);
      this.quad(v0, v1, v2, v3);
    }
    const tris = triangulate(outline);
    const front = this.vertexCount;
    for (const p of outline) this.vertex(p[0], p[1], z, 0, 0, 1);
    const back = this.vertexCount;
    for (const p of outline) this.vertex(p[0], p[1], -z, 0, 0, -1);
    for (let t = 0; t < tris.length; t += 3) {
      this.i.push(front + tris[t], front + tris[t + 1], front + tris[t + 2]);
      this.i.push(back + tris[t], back + tris[t + 2], back + tris[t + 1]);
    }
    return this;
  }

  /** Une couronne plate d'axe Z : deux contours de même nombre de points, parcourus dans le sens trigonométrique. */
  extrudeRing(outer: readonly P2[], inner: readonly P2[], depth: number, smoothDeg = 30): this {
    const z = depth / 2;
    const n = outer.length;
    if (inner.length !== n) throw new Error('extrudeRing : contours de tailles différentes');
    const side = (loop: readonly P2[], sign: number) => {
      const normals: [number, number][] = [];
      for (let k = 0; k < n; k++) {
        const a = loop[k];
        const b = loop[(k + 1) % n];
        const dx = b[0] - a[0];
        const dy = b[1] - a[1];
        const len = Math.hypot(dx, dy) || 1;
        normals.push([(sign * dy) / len, (-sign * dx) / len]);
      }
      const cosSmooth = Math.cos((smoothDeg * Math.PI) / 180);
      for (let k = 0; k < n; k++) {
        const a = loop[k];
        const b = loop[(k + 1) % n];
        const ne = normals[k];
        const prev = normals[(k + n - 1) % n];
        const next = normals[(k + 1) % n];
        const na = ne[0] * prev[0] + ne[1] * prev[1] > cosSmooth ? blend2(prev, ne) : ne;
        const nb = ne[0] * next[0] + ne[1] * next[1] > cosSmooth ? blend2(ne, next) : ne;
        const v0 = this.vertex(a[0], a[1], z, na[0], na[1], 0);
        const v1 = this.vertex(a[0], a[1], -z, na[0], na[1], 0);
        const v2 = this.vertex(b[0], b[1], -z, nb[0], nb[1], 0);
        const v3 = this.vertex(b[0], b[1], z, nb[0], nb[1], 0);
        if (sign > 0) this.quad(v0, v1, v2, v3);
        else this.quad(v3, v2, v1, v0);
      }
    };
    side(outer, 1);
    side(inner, -1);
    const f0 = this.vertexCount;
    for (let k = 0; k < n; k++) {
      this.vertex(outer[k][0], outer[k][1], z, 0, 0, 1);
      this.vertex(inner[k][0], inner[k][1], z, 0, 0, 1);
    }
    const b0 = this.vertexCount;
    for (let k = 0; k < n; k++) {
      this.vertex(outer[k][0], outer[k][1], -z, 0, 0, -1);
      this.vertex(inner[k][0], inner[k][1], -z, 0, 0, -1);
    }
    for (let k = 0; k < n; k++) {
      const j = (k + 1) % n;
      this.quad(f0 + k * 2, f0 + j * 2, f0 + j * 2 + 1, f0 + k * 2 + 1);
      this.quad(b0 + j * 2, b0 + k * 2, b0 + k * 2 + 1, b0 + j * 2 + 1);
    }
    return this;
  }

  /** Une roue dentée d'axe Z, à `teeth` dents et moyeu percé : dents trapézoïdales, à l'angle zéro une dent sur +X. */
  gear(teeth: number, rootRadius: number, tipRadius: number, width: number, boreRadius = 0): this {
    const outer: [number, number][] = [];
    const pitch = TAU / teeth;
    for (let k = 0; k < teeth; k++) {
      const a = k * pitch;
      outer.push(polar(rootRadius, a - pitch * 0.32), polar(tipRadius, a - pitch * 0.14), polar(tipRadius, a + pitch * 0.14), polar(rootRadius, a + pitch * 0.32));
    }
    if (boreRadius <= 0) return this.extrude(outer, width, 5);
    const inner: [number, number][] = [];
    for (let k = 0; k < outer.length; k++) {
      inner.push(polar(boreRadius, (k / outer.length) * TAU - pitch * 0.32));
    }
    return this.extrudeRing(outer, inner, width, 5);
  }

  /** Un tube le long d'un chemin (rayon fixe ou un par point). `closed` : le chemin se referme. */
  tube(path: readonly P3[], radius: number | readonly number[], sides = 10, closed = false): this {
    const count = path.length;
    const radiusAt = (k: number) => (typeof radius === 'number' ? radius : radius[k]);
    const frames = transportFrames(path, closed);
    const first = this.vertexCount;
    for (let k = 0; k < count; k++) {
      const [nx, ny, nz, bx, by, bz] = frames[k];
      const r = radiusAt(k);
      for (let s = 0; s <= sides; s++) {
        const a = (s / sides) * TAU;
        const c = Math.cos(a);
        const si = Math.sin(a);
        const ox = nx * c + bx * si;
        const oy = ny * c + by * si;
        const oz = nz * c + bz * si;
        this.vertex(path[k][0] + ox * r, path[k][1] + oy * r, path[k][2] + oz * r, ox, oy, oz);
      }
    }
    const stride = sides + 1;
    const rows = closed ? count : count - 1;
    for (let k = 0; k < rows; k++) {
      const j = (k + 1) % count;
      for (let s = 0; s < sides; s++) {
        this.quad(first + k * stride + s, first + k * stride + s + 1, first + j * stride + s + 1, first + j * stride + s);
      }
    }
    if (!closed) {
      this.tubeCap(path, frames, radiusAt, 0, sides, -1);
      this.tubeCap(path, frames, radiusAt, count - 1, sides, 1);
    }
    return this;
  }

  private tubeCap(path: readonly P3[], frames: number[][], radiusAt: (k: number) => number, k: number, sides: number, sign: number): void {
    const [nx, ny, nz, bx, by, bz] = frames[k];
    const tx = (ny * bz - nz * by) * sign;
    const ty = (nz * bx - nx * bz) * sign;
    const tz = (nx * by - ny * bx) * sign;
    const center = this.vertex(path[k][0], path[k][1], path[k][2], tx, ty, tz);
    const r = radiusAt(k);
    const first = this.vertexCount;
    for (let s = 0; s <= sides; s++) {
      const a = (s / sides) * TAU;
      const c = Math.cos(a);
      const si = Math.sin(a);
      this.vertex(path[k][0] + (nx * c + bx * si) * r, path[k][1] + (ny * c + by * si) * r, path[k][2] + (nz * c + bz * si) * r, tx, ty, tz);
    }
    for (let s = 0; s < sides; s++) {
      if (sign > 0) this.i.push(center, first + s, first + s + 1);
      else this.i.push(center, first + s + 1, first + s);
    }
  }

  build(): MeshData {
    return { positions: Float32Array.from(this.p), normals: Float32Array.from(this.n), indices: Uint32Array.from(this.i) };
  }
}

function blend2(a: readonly number[], b: readonly number[]): [number, number] {
  const x = a[0] + b[0];
  const y = a[1] + b[1];
  const len = Math.hypot(x, y) || 1;
  return [x / len, y / len];
}

function polar(r: number, a: number): [number, number] {
  return [Math.cos(a) * r, Math.sin(a) * r];
}

/** Des repères qui ne vrillent pas le long du chemin : normale puis binormale, pour chaque point. */
function transportFrames(path: readonly P3[], closed: boolean): number[][] {
  const count = path.length;
  const frames: number[][] = [];
  let nx = 0, ny = 0, nz = 0;
  for (let k = 0; k < count; k++) {
    const before = path[closed ? (k + count - 1) % count : Math.max(k - 1, 0)];
    const after = path[closed ? (k + 1) % count : Math.min(k + 1, count - 1)];
    let tx = after[0] - before[0];
    let ty = after[1] - before[1];
    let tz = after[2] - before[2];
    const len = Math.hypot(tx, ty, tz) || 1;
    tx /= len;
    ty /= len;
    tz /= len;
    if (k === 0) {
      // Le départ : n'importe quelle perpendiculaire.
      if (Math.abs(ty) < 0.9) {
        nx = -tz;
        ny = 0;
        nz = tx;
      } else {
        nx = 0;
        ny = tz;
        nz = -ty;
      }
    }
    // On retire de la normale précédente ce qui suit la tangente.
    const dot = nx * tx + ny * ty + nz * tz;
    nx -= dot * tx;
    ny -= dot * ty;
    nz -= dot * tz;
    const nl = Math.hypot(nx, ny, nz) || 1;
    nx /= nl;
    ny /= nl;
    nz /= nl;
    frames.push([nx, ny, nz, ty * nz - tz * ny, tz * nx - tx * nz, tx * ny - ty * nx]);
  }
  return frames;
}

/** Découpe un contour simple en triangles, par oreilles. */
export function triangulate(outline: readonly P2[]): number[] {
  const n = outline.length;
  const out: number[] = [];
  const left: number[] = [];
  for (let k = 0; k < n; k++) left.push(k);
  const cross = (a: P2, b: P2, c: P2) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  let guard = n * n + 8;
  while (left.length > 3 && guard-- > 0) {
    let cut = false;
    for (let k = 0; k < left.length; k++) {
      const ia = left[(k + left.length - 1) % left.length];
      const ib = left[k];
      const ic = left[(k + 1) % left.length];
      const a = outline[ia];
      const b = outline[ib];
      const c = outline[ic];
      if (cross(a, b, c) <= 1e-12) continue;
      let inside = false;
      for (const j of left) {
        if (j === ia || j === ib || j === ic) continue;
        const p = outline[j];
        if (cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      out.push(ia, ib, ic);
      left.splice(k, 1);
      cut = true;
      break;
    }
    // Un contour dégénéré : on retire un point plutôt que de boucler.
    if (!cut) left.splice(0, 1);
  }
  if (left.length === 3) out.push(left[0], left[1], left[2]);
  return out;
}

function flatNormals(positions: ArrayLike<number>, indices: ArrayLike<number>): Float32Array {
  const normals = new Float32Array(positions.length);
  for (let t = 0; t < indices.length; t += 3) {
    const a = indices[t] * 3;
    const b = indices[t + 1] * 3;
    const c = indices[t + 2] * 3;
    const ux = positions[b] - positions[a];
    const uy = positions[b + 1] - positions[a + 1];
    const uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a];
    const vy = positions[c + 1] - positions[a + 1];
    const vz = positions[c + 2] - positions[a + 2];
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    for (const v of [a, b, c]) {
      normals[v] += nx;
      normals[v + 1] += ny;
      normals[v + 2] += nz;
    }
  }
  for (let v = 0; v < normals.length; v += 3) {
    const len = Math.hypot(normals[v], normals[v + 1], normals[v + 2]) || 1;
    normals[v] /= len;
    normals[v + 1] /= len;
    normals[v + 2] /= len;
  }
  return normals;
}

// ── Couleurs ──

/** '#rrggbb' ou 'rgb(r, g, b)' vers [r, g, b] en 0..1. Une couleur illisible donne du gris. */
export function parseColor(text: string): [number, number, number] {
  const value = text.trim();
  if (value[0] === '#' && value.length >= 7) {
    return [parseInt(value.slice(1, 3), 16) / 255, parseInt(value.slice(3, 5), 16) / 255, parseInt(value.slice(5, 7), 16) / 255];
  }
  const match = /rgba?\(\s*([\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)/.exec(value);
  if (match) return [Number(match[1]) / 255, Number(match[2]) / 255, Number(match[3]) / 255];
  return [0.5, 0.5, 0.5];
}
