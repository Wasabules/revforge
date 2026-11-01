/**
 * Les pièces de la boîte en tableaux de sommets : dentures en développante, avec leur hélice, moyeux, baladeurs,
 * fourchettes, roulements, volant. Sans WebGL ni three.js, donc testable sans navigateur.
 *
 * Une roue est décrite autour de son axe +x : (y, z) = (r cos a, r sin a), sa dent 0 à l'angle de sa phase.
 */
import { MeshBuilder, m34, type Mat34, type MeshData } from './mesh3d';
import { trackAt, type Bearing, type GearboxLayout, type Selector, type Shaft, type Wheel } from './gearboxModel';

const TAU = Math.PI * 2;
const PRESSURE_ANGLE = (20 * Math.PI) / 180;
/** Part du pas occupée par une dent au primitif. Le reste est le jeu : assez pour qu'une roue qui claque ne traverse rien. */
export const TOOTH_SHARE = 0.44;

export type MaterialKey = 'steel' | 'forged' | 'casting' | 'alloy' | 'rubber' | 'brass' | 'ghost' | 'oil' | 'hub' | 'cutForged' | 'cutSteel' | 'cutAlloy' | 'cutCasting';

export type PartKind = 'wheel' | 'body' | 'sleeve' | 'ring' | 'fork' | 'balls' | 'static' | 'drum' | 'pawl' | 'idlerFork' | 'casing';

export interface Part {
  kind: PartKind;
  name: string;
  /** Le corps qui l'entraîne, -1 s'il ne tourne pas. */
  body: number;
  material: MaterialKey;
  data: MeshData;
  wheel?: number;
  selector?: number;
  side?: -1 | 1;
}

/** Un point du contour d'une roue, en coordonnées polaires. */
export interface OutlinePoint {
  a: number;
  r: number;
}

const involute = (alpha: number) => Math.tan(alpha) - alpha;

/** Le contour plan d'une roue, profil en développante : 8 points par dent. */
export function toothOutline(teeth: number, moduleT: number, moduleN: number, helix: number, options: { internal?: boolean; missing?: number } = {}): OutlinePoint[] {
  const pitchRadius = (teeth * moduleT) / 2;
  const addendum = moduleN;
  const dedendum = 1.25 * moduleN;
  const internal = !!options.internal;
  const alphaT = Math.atan(Math.tan(PRESSURE_ANGLE) / Math.max(Math.cos(helix), 0.3));
  const base = pitchRadius * Math.cos(alphaT);
  const tipR = internal ? pitchRadius - addendum : pitchRadius + addendum;
  const rootR = internal ? pitchRadius + dedendum : pitchRadius - dedendum;
  const pitch = TAU / teeth;
  const halfPitch = (pitch * TOOTH_SHARE) / 2;
  const halfAt = (r: number) => {
    if (internal) {
      // Une couronne intérieure : la dent s'affine vers son sommet, vers l'axe.
      return halfPitch + ((r - pitchRadius) / Math.max(dedendum, 1e-6)) * pitch * 0.09;
    }
    const clamped = Math.max(r, base);
    const alpha = Math.acos(Math.min(base / clamped, 1));
    return Math.max(halfPitch + involute(alphaT) - involute(alpha), pitch * 0.03);
  };
  const flankStart = internal ? rootR : Math.max(rootR, base);
  const points: OutlinePoint[] = [];
  for (let k = 0; k < teeth; k++) {
    const centre = k * pitch;
    if (k === options.missing) {
      points.push({ a: centre - pitch * 0.4, r: rootR }, { a: centre + pitch * 0.4, r: rootR });
      continue;
    }
    const hRoot = Math.min(halfAt(flankStart) + pitch * 0.05, pitch * 0.48);
    const hStart = halfAt(flankStart);
    const hMid = halfAt(pitchRadius);
    const hTip = halfAt(tipR);
    points.push(
      { a: centre - hRoot, r: rootR },
      { a: centre - hStart, r: flankStart },
      { a: centre - hMid, r: pitchRadius },
      { a: centre - hTip, r: tipR },
      { a: centre + hTip, r: tipR },
      { a: centre + hMid, r: pitchRadius },
      { a: centre + hStart, r: flankStart },
      { a: centre + hRoot, r: rootR },
    );
  }
  return points;
}

/** Combien de sommets de dents dans un contour : une dent, c'est un plateau au rayon maximal (minimal à l'intérieur). */
export function countTeeth(outline: readonly OutlinePoint[], internal = false): number {
  const extreme = outline.reduce((best, p) => (internal ? Math.min(best, p.r) : Math.max(best, p.r)), internal ? Infinity : 0);
  let count = 0;
  for (let k = 0; k < outline.length; k++) {
    const here = Math.abs(outline[k].r - extreme) < 1e-6;
    const next = Math.abs(outline[(k + 1) % outline.length].r - extreme) < 1e-6;
    if (here && next) count++;
  }
  return count;
}

/** Empile des sommets à plat, pour avoir des arêtes vives sans partager de normales. */
class Flat {
  p: number[] = [];
  n: number[] = [];
  i: number[] = [];

  quad(a: number[], b: number[], c: number[], d: number[]): void {
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
    let nx = uy * vz - uz * vy;
    let ny = uz * vx - ux * vz;
    let nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len;
    ny /= len;
    nz /= len;
    const base = this.p.length / 3;
    for (const q of [a, b, c, d]) {
      this.p.push(q[0], q[1], q[2]);
      this.n.push(nx, ny, nz);
    }
    this.i.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }

  data(): MeshData {
    return { positions: Float32Array.from(this.p), normals: Float32Array.from(this.n), indices: Uint32Array.from(this.i) };
  }
}

/**
 * Un contour extrudé le long de x, de `x0` à `x1`, tordu de `twist` radians d'un bout à l'autre, fermé par deux
 * couronnes planes jusqu'au rayon `rim`. Les dents sont vraies, et la torsion aussi : c'est l'hélice.
 */
export function extrudeOutline(outline: readonly OutlinePoint[], x0: number, x1: number, twist: number, rim: number, phase = 0): MeshData {
  const flat = new Flat();
  const n = outline.length;
  const at = (k: number, x: number, turn: number, radius?: number): number[] => {
    const a = outline[k % n].a + phase + turn;
    const r = radius ?? outline[k % n].r;
    return [x, r * Math.cos(a), r * Math.sin(a)];
  };
  const t0 = -twist / 2;
  const t1 = twist / 2;
  for (let k = 0; k < n; k++) {
    flat.quad(at(k, x0, t0), at(k + 1, x0, t0), at(k + 1, x1, t1), at(k, x1, t1));
    flat.quad(at(k, x1, t1), at(k + 1, x1, t1), at(k + 1, x1, t1, rim), at(k, x1, t1, rim));
    flat.quad(at(k + 1, x0, t0), at(k, x0, t0), at(k, x0, t0, rim), at(k + 1, x0, t0, rim));
  }
  return flat.data();
}

/** La torsion d'une hélice sur une largeur : l'arc parcouru au primitif est largeur × tan β. */
export const helixTwist = (helix: number, width: number, pitchRadius: number): number => (width * Math.tan(helix)) / pitchRadius;

/** Mesure l'angle d'hélice sur un maillage : le premier flanc relie les deux faces, sa torsion donne l'hélice. Pour les tests. */
export function measuredHelix(data: MeshData, pitchRadius: number, width: number): number {
  const p = data.positions;
  // Quatre sommets par quadrilatère : le premier et le quatrième sont le même point du contour, sur chaque face.
  const a0 = Math.atan2(p[2], p[1]);
  const a1 = Math.atan2(p[11], p[10]);
  const twist = Math.atan2(Math.sin(a1 - a0), Math.cos(a1 - a0));
  return Math.atan((twist * pitchRadius) / width);
}

/** La denture d'une roue seule : contour, largeur, hélice, phase. */
export function wheelTeethMesh(wheel: Wheel, missing?: number): MeshData {
  const outline = toothOutline(wheel.teeth, wheel.moduleT, wheel.moduleN, wheel.helix, { internal: wheel.internal, missing });
  const x0 = wheel.x - wheel.width / 2;
  const x1 = wheel.x + wheel.width / 2;
  const twist = helixTwist(wheel.helix, wheel.width, wheel.radius);
  const rim = wheel.internal ? wheel.radius + 1.25 * wheel.moduleN + wheel.moduleN * 1.1 : wheel.radius - 1.25 * wheel.moduleN - wheel.moduleN * 0.4;
  return extrudeOutline(outline, x0, x1, twist, rim, wheel.phase);
}

/** Un couple conique : le contour posé sur le cône primitif, du dos vers le sommet, avec sa spirale. */
export function bevelMesh(wheel: Wheel): MeshData {
  const bevel = wheel.bevel!;
  const outline = toothOutline(wheel.teeth, wheel.moduleT, wheel.moduleN, 0);
  const cone = bevel.cone;
  const coneDistance = wheel.radius / Math.sin(cone);
  const share = bevel.width / coneDistance;
  const axial = wheel.radius / Math.tan(cone);
  const flat = new Flat();
  const n = outline.length;
  const place = (k: number, s: number, override?: number): number[] => {
    const point = outline[k % n];
    const twist = Math.tan(wheel.helix) * (1 - s) * (coneDistance / wheel.radius);
    const a = point.a + wheel.phase + twist;
    const d = (override ?? point.r) - wheel.radius;
    const radial = s * (wheel.radius + d * Math.cos(cone));
    const along = s * (axial - d * Math.sin(cone));
    return [bevel.apex + bevel.dir * along, radial * Math.cos(a), radial * Math.sin(a)];
  };
  const inner = 1 - share;
  const rim = wheel.radius - 1.6 * wheel.moduleN;
  for (let k = 0; k < n; k++) {
    if (bevel.dir > 0) {
      flat.quad(place(k, inner), place(k + 1, inner), place(k + 1, 1), place(k, 1));
      flat.quad(place(k, 1), place(k + 1, 1), place(k + 1, 1, rim), place(k, 1, rim));
      flat.quad(place(k + 1, inner), place(k, inner), place(k, inner, rim), place(k + 1, inner, rim));
    } else {
      flat.quad(place(k, 1), place(k + 1, 1), place(k + 1, inner), place(k, inner));
      flat.quad(place(k, inner), place(k + 1, inner), place(k + 1, inner, rim), place(k, inner, rim));
      flat.quad(place(k + 1, 1), place(k, 1), place(k, 1, rim), place(k + 1, 1, rim));
    }
  }
  return flat.data();
}

// ── Des révolutions autour de x ──

/** Passe de l'axe Y de `lathe` à l'axe X des roues, centré en `x` et à la distance (y, z) de l'axe. */
const aroundX = (x: number, y = 0, z = 0): Mat34 => {
  const m = m34.create();
  m[0] = 0; m[1] = 1; m[2] = 0; m[3] = x;
  m[4] = -1; m[5] = 0; m[6] = 0; m[7] = y;
  m[8] = 0; m[9] = 0; m[10] = 1; m[11] = z;
  return m;
};

/** Un anneau plein, de `inner` à `outer`, sur la largeur `width` autour de `x`. */
function ring(b: MeshBuilder, x: number, inner: number, outer: number, width: number, segments = 24, y = 0, z = 0): void {
  const w = width / 2;
  b.transform(aroundX(x, y, z));
  b.lathe(inner > 0 ? [[inner, -w], [outer, -w], [outer, w], [inner, w], [inner, -w]] : [[0, -w], [outer, -w], [outer, w], [0, w]], segments, 10);
  b.transform(null);
}

function lathePath(b: MeshBuilder, x: number, profile: readonly (readonly [number, number])[], segments = 24, crease = 25): void {
  b.transform(aroundX(x));
  b.lathe(profile, segments, crease);
  b.transform(null);
}

/** Moyeu et voile d'une roue : plein sur une petite, évidé sur une grande. Un alésage si elle est folle. */
function wheelBody(b: MeshBuilder, wheel: Wheel, bore: number): void {
  const w = wheel.width / 2;
  const rim = wheel.radius - 1.25 * wheel.moduleN - wheel.moduleN * 0.4;
  if (rim <= bore + 3) {
    ring(b, wheel.x, Math.max(bore, 0.1), rim + 0.2, wheel.width, 24);
    return;
  }
  const hubR = Math.min(bore + (rim - bore) * 0.34, rim - 1);
  const web = w * 0.55;
  if (rim - hubR < 6) {
    ring(b, wheel.x, Math.max(bore, 0.1), rim + 0.2, wheel.width, 24);
    return;
  }
  lathePath(b, wheel.x, [
    [Math.max(bore, 0.1), -w], [hubR, -w], [hubR + 1, -web], [rim - 2, -web], [rim - 1, -w], [rim + 0.2, -w],
    [rim + 0.2, w], [rim - 1, w], [rim - 2, web], [hubR + 1, web], [hubR, w], [Math.max(bore, 0.1), w], [Math.max(bore, 0.1), -w],
  ], 28, 20);
}

/** La couronne d'engagement ou les crabots qui dépassent d'une roue, côté baladeur. */
function dogFace(b: MeshBuilder, layout: GearboxLayout, selector: Selector, wheelSide: number, wheelX: number, wheelWidth: number): void {
  const dogs = layout.form !== 'synchro' && layout.form !== 'dual_clutch';
  const start = wheelX + (wheelSide * wheelWidth) / 2;
  const width = selector.dogWidth;
  const mid = start + (wheelSide * width) / 2;
  const outer = selector.hubRadius * 0.96;
  const inner = selector.hubRadius * 0.78;
  const x0 = mid - width / 2;
  const x1 = mid + width / 2;
  if (dogs) {
    // Six larges crabots, bien espacés : c'est ce qu'on voit quand on ouvre une boîte de course.
    const count = Math.max(layout.dogTeeth, 3);
    b.add(extrudeOutline(squareTeeth(count, inner, outer, 0.32), x0, x1, 0, inner * 0.98));
    return;
  }
  // La couronne d'engagement, puis le cône sur lequel frotte la bague.
  b.add(extrudeOutline(squareTeeth(Math.max(layout.dogTeeth, 8), inner * 1.14, outer, 0.5), x0, x1, 0, inner * 1.02));
  const coneStart = mid + wheelSide * (width / 2);
  const coneEnd = coneStart + wheelSide * Math.max(width * 0.6, 2);
  const coneA = inner * 1.02;
  const coneB = inner * 0.92;
  const profile: [number, number][] = wheelSide > 0 ? [[coneA * 0.7, 0], [coneA, 0], [coneB, coneEnd - coneStart], [coneA * 0.7, coneEnd - coneStart]] : [[coneA * 0.7, coneEnd - coneStart], [coneB, coneEnd - coneStart], [coneA, 0], [coneA * 0.7, 0]];
  lathePath(b, coneStart, profile, 24);
}

/** Des dents droites à flancs parallèles : couronne d'engagement, crabots, cliquet. */
function squareTeeth(count: number, root: number, tip: number, share: number): OutlinePoint[] {
  const out: OutlinePoint[] = [];
  const pitch = TAU / count;
  const half = (pitch * share) / 2;
  for (let k = 0; k < count; k++) {
    const a = k * pitch;
    out.push({ a: a - half, r: root }, { a: a - half, r: tip }, { a: a + half, r: tip }, { a: a + half, r: root });
  }
  return out;
}

/** Une rangée de billes, entre les deux bagues d'un roulement. */
function ballsOf(b: MeshBuilder, bearing: Bearing, count = 9): void {
  const race = (bearing.inner + bearing.outer) / 2;
  const radius = Math.min((bearing.outer - bearing.inner) * 0.3, bearing.width * 0.38);
  for (let k = 0; k < count; k++) {
    const a = (k / count) * TAU;
    b.transform(m34.translation(m34.create(), bearing.x, race * Math.cos(a), race * Math.sin(a)));
    b.sphere(radius, 8, 5);
  }
  b.transform(null);
}

/** Tout ce qu'il faut pour dessiner cette boîte. Rien n'est gardé ici : c'est l'appelant qui en fait des géométries. */
export function buildParts(layout: GearboxLayout, options: { missingTooth?: number } = {}): Part[] {
  const parts: Part[] = [];
  const { wheels, selectors, shafts } = layout;
  if (wheels.length === 0) return parts;
  const hubBodyShaft = shafts.find((s) => s.body === layout.hubBody);
  const hubShaftR = hubBodyShaft?.radius ?? 10;

  // Les roues : une pièce chacune, pour pouvoir colorer celle qui est en prise.
  for (const wheel of wheels) {
    const b = new MeshBuilder();
    const missing = options.missingTooth === wheel.id ? 0 : undefined;
    if (wheel.bevel) {
      b.add(bevelMesh(wheel));
      bevelBody(b, wheel);
    } else {
      b.add(wheelTeethMesh(wheel, missing));
      if (wheel.internal) {
        const outer = wheel.radius + 1.25 * wheel.moduleN + wheel.moduleN * 1.1;
        ring(b, wheel.x, outer - 0.3, outer + wheel.moduleN * 0.9, wheel.width, 36);
      } else {
        const shaft = shafts.find((s) => s.body === wheel.body);
        const bore = wheel.free ? hubShaftR + 0.8 : shaft ? shaft.radius * 0.9 : wheel.role === 'finalRing' ? wheel.radius * 0.42 : wheel.role === 'reverseIdler' ? 4 : wheel.role === 'planet' ? 3 : 0;
        wheelBody(b, wheel, bore);
      }
    }
    // Côté baladeur, une roue folle porte sa couronne d'engagement.
    for (const selector of selectors) {
      for (const [side, sign] of [[selector.left, 1], [selector.right, -1]] as const) {
        if (side && side.wheel === wheel.id) dogFace(b, layout, selector, sign, wheel.x, wheel.width);
      }
    }
    parts.push({ kind: 'wheel', name: `${wheel.role}-${wheel.gear}-${wheel.id}`, body: wheel.body, material: wheel.role === 'annulus' ? 'cutForged' : wheel.free || wheel.role === 'finalRing' ? 'forged' : 'steel', data: b.build(), wheel: wheel.id });
  }

  // Les arbres, épaulés, et ce qui leur est fixé.
  for (const shaft of shafts) {
    const b = new MeshBuilder();
    shaftGeometry(b, shaft);
    // Le moyeu, claveté sur l'arbre qui porte les baladeurs.
    if (shaft.body === layout.hubBody) {
      for (const selector of selectors) ring(b, selector.x, shaft.radius - 0.2, selector.hubRadius, selector.hubWidth * 0.82, 28);
    }
    // La couronne d'engagement de la prise directe, au dos du pignon.
    if (shaft.role === 'input' || shaft.role === 'inputOuter') {
      for (const selector of selectors) {
        const direct = selector.left && selector.left.wheel === -1 ? selector.left : null;
        if (direct) {
          const inner = selector.hubRadius * 0.78;
          b.add(extrudeOutline(squareTeeth(Math.max(layout.dogTeeth, 6), inner, selector.hubRadius * 0.96, 0.5), direct.x - selector.dogWidth / 2, direct.x + selector.dogWidth / 2, 0, shaft.radius));
        }
      }
    }
    parts.push({ kind: 'body', name: `arbre-${shaft.role}`, body: shaft.body, material: shaft.role === 'input' || shaft.role === 'inputOuter' ? 'steel' : shaft.role === 'lay' ? 'alloy' : 'steel', data: b.build() });
  }

  // Les baladeurs.
  for (const selector of selectors) {
    const b = new MeshBuilder();
    const w = selector.hubWidth / 2;
    const groove = Math.max(selector.hubWidth * 0.22, 1.6);
    const r = selector.sleeveRadius;
    lathePath(b, selector.x, [
      [selector.hubRadius, -w], [r - 1, -w], [r, -w + 1], [r, -groove], [r * 0.93, -groove * 0.8], [r * 0.93, groove * 0.8], [r, groove], [r, w - 1], [r - 1, w], [selector.hubRadius, w], [selector.hubRadius, -w],
    ], 32, 12);
    const dogs = layout.form !== 'synchro' && layout.form !== 'dual_clutch';
    if (dogs) {
      // Des crabots de chaque côté du baladeur, qui viennent entre ceux de la roue.
      const count = Math.max(layout.dogTeeth, 3);
      const inner = selector.hubRadius * 0.78;
      const outer = selector.hubRadius * 0.96;
      const offset = Math.PI / count;
      if (selector.left) b.add(extrudeOutline(squareTeeth(count, inner, outer, 0.32), selector.x - w - selector.dogWidth * 0.8, selector.x - w, 0, inner * 0.98, offset));
      if (selector.right) b.add(extrudeOutline(squareTeeth(count, inner, outer, 0.32), selector.x + w, selector.x + w + selector.dogWidth * 0.8, 0, inner * 0.98, offset));
    }
    parts.push({ kind: 'sleeve', name: `baladeur-${selector.id}`, body: selector.sleeveBody, material: 'alloy', data: b.build(), selector: selector.id });

    if (layout.form === 'synchro' || layout.form === 'dual_clutch') {
      // La bague de synchro : elle frotte sur le cône de la roue, et ses dents bloquent le baladeur tant que les vitesses diffèrent.
      for (const [side, sign] of [[selector.left, -1], [selector.right, 1]] as const) {
        if (!side || side.wheel < 0) continue;
        const rb = new MeshBuilder();
        const inner = selector.hubRadius * 0.8;
        const rx = selector.x + sign * (w + selector.dogWidth * 0.1);
        const thick = Math.max(selector.dogWidth * 0.7, 1.8);
        ring(rb, rx, inner, selector.hubRadius * 0.92, thick, 28);
        rb.add(extrudeOutline(squareTeeth(Math.max(Math.round(layout.dogTeeth / 2), 8), selector.hubRadius * 0.92, selector.hubRadius * 0.98, 0.42), rx - thick / 2, rx + thick / 2, 0, selector.hubRadius * 0.92));
        parts.push({ kind: 'ring', name: `bague-${selector.id}-${sign}`, body: selector.sleeveBody, material: 'brass', data: rb.build(), selector: selector.id, side: sign });
      }
    }

    // La fourchette : deux bras dans la gorge du baladeur, un tronc vers le rail ou le barillet.
    parts.push({ kind: 'fork', name: `fourchette-${selector.id}`, body: -1, material: 'alloy', data: forkGeometry(selector, layout), selector: selector.id });
  }

  // Le rail, ou le barillet.
  const stat = new MeshBuilder();
  if (selectors[0] && !layout.drum) {
    const railLow = Math.min(...selectors.map((s) => s.x)) - 10;
    const railHigh = Math.max(...selectors.map((s) => s.x)) + 10;
    ring(stat, (railLow + railHigh) / 2, 0, Math.max(layout.center * 0.05, 2.4), railHigh - railLow, 12, selectors[0].railY, selectors[0].railZ);
  }
  if (layout.reverse) {
    const idler = wheels[layout.reverse.idler];
    // L'axe de l'intermédiaire.
    ring(stat, idler.x + layout.reverse.slide / 2, 0, 3, idler.width + layout.reverse.slide + 6, 12, idler.y, idler.z);
    parts.push({ kind: 'idlerFork', name: 'fourchette-ar', body: -1, material: 'alloy', data: idlerForkGeometry(layout), side: 1 });
  }
  parts.push({ kind: 'static', name: 'rail', body: -1, material: 'steel', data: stat.build() });

  if (layout.drum) drumParts(parts, layout);

  // Les roulements : bagues fixes, billes qui tournent à moitié moins vite.
  const races = new MeshBuilder();
  for (const bearing of layout.bearings) {
    const span = bearing.outer - bearing.inner;
    ring(races, bearing.x, bearing.outer - span * 0.22, bearing.outer, bearing.width, 24, bearing.y, bearing.z);
    ring(races, bearing.x, bearing.inner, bearing.inner + span * 0.22, bearing.width, 20, bearing.y, bearing.z);
  }
  parts.push({ kind: 'static', name: 'bagues', body: -1, material: 'steel', data: races.build() });
  layout.bearings.forEach((bearing, index) => {
    const b = new MeshBuilder();
    ballsOf(b, { ...bearing, y: 0, z: 0 });
    parts.push({ kind: 'balls', name: `billes-${index}`, body: bearing.body, material: 'steel', data: b.build() });
  });

  // Le différentiel, fixé à la couronne, et les arbres de roue qui en sortent.
  if (layout.final) finalParts(parts, layout);

  if (layout.transfer) {
    const transfer = layout.transfer;
    const shaft = new MeshBuilder();
    ring(shaft, (transfer.x0 + transfer.x1) / 2, 0, transfer.radius, transfer.x1 - transfer.x0, 14);
    parts.push({ kind: 'body', name: 'transfert', body: transfer.body, material: 'steel', data: shaft.build() });
    const box = new MeshBuilder();
    const cx = transfer.x1 - 4;
    box.transform(m34.translation(m34.create(), cx, (transfer.y + (layout.bodies[layout.output]?.y ?? 0)) / 2, transfer.z / 2));
    box.box(22, Math.abs(transfer.y - (layout.bodies[layout.output]?.y ?? 0)) + 30, Math.abs(transfer.z) + 30);
    box.transform(null);
    parts.push({ kind: 'casing', name: 'boite-transfert', body: -1, material: 'ghost', data: box.build() });
  }
  inputParts(parts, layout);
  casingParts(parts, layout);
  if (layout.planets.length > 0) planetParts(parts, layout);
  return parts;
}

function bevelBody(b: MeshBuilder, wheel: Wheel): void {
  const bevel = wheel.bevel!;
  const cone = bevel.cone;
  const axial = wheel.radius / Math.tan(cone);
  const rim = wheel.radius - 1.6 * wheel.moduleN;
  if (wheel.role === 'finalRing') {
    // La couronne : un voile qui la relie au boîtier du différentiel. Il s'arrête sous les dents, en suivant leur pied.
    const coneDistance = wheel.radius / Math.sin(cone);
    const share = 1 - bevel.width / coneDistance;
    const rootAlong = axial + 1.6 * wheel.moduleN * Math.sin(cone);
    const outer = bevel.apex + bevel.dir * rootAlong;
    const inner = bevel.apex + bevel.dir * rootAlong * share;
    const low = Math.min(outer, inner);
    const high = Math.max(outer, inner);
    const back = low - wheel.moduleN * 2;
    const hole = wheel.radius * 0.46;
    lathePath(b, 0, [[hole, back], [rim * 1.01, back], [rim, low], [rim * share, high], [hole, high], [hole, back]], 40, 15);
  } else {
    // Le pignon d'attaque : sa queue, jusqu'à l'arbre.
    const x = bevel.apex - axial;
    const back = x - bevel.width * 0.7;
    const front = bevel.apex - (axial * (1 - bevel.width / (wheel.radius / Math.sin(cone)))) + 1;
    lathePath(b, 0, [[0.1, back], [rim * 0.72, back], [rim * 0.92, x], [rim * 0.6, front], [0.1, front]], 20, 20);
  }
}

function shaftGeometry(b: MeshBuilder, shaft: Shaft): void {
  const r = shaft.radius;
  const length = shaft.x1 - shaft.x0;
  const mid = (shaft.x0 + shaft.x1) / 2;
  if (shaft.bore > 0) {
    lathePath(b, mid, [[shaft.bore, -length / 2], [r, -length / 2], [r, length / 2], [shaft.bore, length / 2], [shaft.bore, -length / 2]], 20, 10);
    return;
  }
  // Un léger chanfrein aux deux bouts ; des cannelures au bout du primaire, là où prend le disque.
  lathePath(b, mid, [[0.1, -length / 2], [r * 0.85, -length / 2], [r, -length / 2 + r * 0.15], [r, length / 2 - r * 0.15], [r * 0.85, length / 2], [0.1, length / 2]], 18, 25);
  if (shaft.role === 'input' || shaft.role === 'inputOuter') {
    const splines = squareTeeth(12, r * 0.98, r * 1.08, 0.5);
    b.add(extrudeOutline(splines, shaft.x0 + 1, shaft.x0 + Math.min(14, length * 0.2), 0, r * 0.9));
  }
}

function forkGeometry(selector: Selector, layout: GearboxLayout): MeshData {
  const b = new MeshBuilder();
  // Dans le repère du baladeur, x = 0 au centre de sa course ; y, z absolus.
  const rFork = selector.sleeveRadius * 0.965;
  const thick = Math.max(layout.center * 0.028, 1.5);
  const toRail = Math.atan2(selector.railZ - selector.z, selector.railY - selector.y);
  const path: [number, number, number][] = [];
  const spread = 1.35;
  for (let k = 0; k <= 16; k++) {
    const a = toRail - spread + (k / 16) * spread * 2;
    path.push([0, selector.y + rFork * Math.cos(a), selector.z + rFork * Math.sin(a)]);
  }
  b.tube(path, thick, 8);
  const drumRadius = layout.drum ? layout.drum.radius * 1.05 : 0;
  const armLength = Math.hypot(selector.railY - selector.y, selector.railZ - selector.z) - rFork - drumRadius;
  const ax = selector.y + rFork * Math.cos(toRail);
  const az = selector.z + rFork * Math.sin(toRail);
  const steps: [number, number, number][] = [[0, ax, az], [0, ax + armLength * Math.cos(toRail), az + armLength * Math.sin(toRail)]];
  b.tube(steps, thick * 1.3, 8);
  if (!layout.drum) {
    // Le coulisseau sur le rail.
    ring(b, 0, 0, Math.max(layout.center * 0.075, 3.4), Math.max(selector.hubWidth * 1.1, 8), 12, selector.railY, selector.railZ);
  } else {
    // Le doigt qui suit la rainure.
    const py = selector.railY - Math.cos(toRail) * layout.drum.radius * -1.06;
    const pz = selector.railZ - Math.sin(toRail) * layout.drum.radius * -1.06;
    b.tube([[0, py, pz], [0, py - Math.cos(toRail) * layout.drum.radius * 0.32, pz - Math.sin(toRail) * layout.drum.radius * 0.32]], thick * 1.15, 8);
  }
  return b.build();
}

function idlerForkGeometry(layout: GearboxLayout): MeshData {
  const reverse = layout.reverse!;
  const idler = layout.wheels[reverse.idler];
  const b = new MeshBuilder();
  const railY = reverse.fork.railY;
  const railZ = reverse.fork.railZ;
  const thick = Math.max(layout.center * 0.024, 1.3);
  const towards = Math.atan2(railZ - idler.z, railY - idler.y);
  const r = idler.radius + idler.moduleN + 1.5;
  const sx = idler.x + idler.width / 2 + thick;
  const path: [number, number, number][] = [];
  for (let k = 0; k <= 10; k++) {
    const a = towards - 0.9 + (k / 10) * 1.8;
    path.push([sx, idler.y + r * Math.cos(a), idler.z + r * Math.sin(a)]);
  }
  b.tube(path, thick, 6);
  b.tube([[sx, idler.y + r * Math.cos(towards), idler.z + r * Math.sin(towards)], [sx, railY, railZ]], thick * 1.2, 6);
  return b.build();
}

/** Le barillet : un cylindre dont les rainures mènent les fourchettes, et son cliquet. */
function drumParts(parts: Part[], layout: GearboxLayout): void {
  const drum = layout.drum!;
  const b = new MeshBuilder();
  const length = drum.x1 - drum.x0;
  ring(b, (drum.x0 + drum.x1) / 2, 0, drum.radius, length, 24);
  const span = drum.positions.length - 1;
  for (const selector of layout.selectors) {
    const towards = Math.atan2(selector.z - drum.z, selector.y - drum.y);
    const path: [number, number, number][] = [];
    for (let k = 0; k <= span * 6; k++) {
      const position = k / 6;
      const a = towards - position * drum.step;
      const x = selector.x + trackAt(selector, drum, position) * selector.travel;
      path.push([x, drum.radius * 1.01 * Math.cos(a), drum.radius * 1.01 * Math.sin(a)]);
    }
    b.tube(path, Math.max(drum.radius * 0.1, 0.8), 5);
  }
  // Le cliquet : une roue à rochet, une dent par position, au bout du barillet.
  const teeth = drum.positions.length + 2;
  const outline: OutlinePoint[] = [];
  const pitch = TAU / teeth;
  for (let k = 0; k < teeth; k++) {
    outline.push({ a: k * pitch, r: drum.radius * 1.12 }, { a: k * pitch + pitch * 0.15, r: drum.radius * 1.55 }, { a: k * pitch + pitch * 0.3, r: drum.radius * 1.55 });
  }
  b.add(extrudeOutline(outline, drum.x1 + 1, drum.x1 + 1 + Math.max(drum.radius * 0.5, 3), 0, drum.radius));
  parts.push({ kind: 'drum', name: 'barillet', body: -1, material: 'alloy', data: b.build() });
  const pawl = new MeshBuilder();
  pawl.transform(m34.translation(m34.create(), drum.x1 + 1 + Math.max(drum.radius * 0.25, 1.5), drum.y + drum.radius * 2.4, drum.z));
  pawl.box(Math.max(drum.radius * 0.45, 2.5), drum.radius * 1.9, Math.max(drum.radius * 0.5, 2.5));
  pawl.transform(null);
  parts.push({ kind: 'pawl', name: 'cliquet', body: -1, material: 'brass', data: pawl.build() });
}

function finalParts(parts: Part[], layout: GearboxLayout): void {
  const final = layout.final!;
  const b = new MeshBuilder();
  // Pour un couple conique, ce repère local tourne autour de son x, qui est le z du monde.
  const cx = final.kind === 'bevel' ? 0 : final.x;
  const r = final.caseRadius;
  const w = final.caseWidth / 2;
  lathePath(b, cx, [[0.1, -w], [r * 0.34, -w], [r * 0.5, -w * 0.72], [r, -w * 0.35], [r, w * 0.35], [r * 0.5, w * 0.72], [r * 0.34, w], [0.1, w]], 32, 25);
  // Les arbres de roue.
  const stub = Math.max(layout.center * 0.65, 40);
  const stubR = Math.max(r * 0.24, 6);
  ring(b, cx - w - stub / 2, 0, stubR, stub, 16);
  ring(b, cx + w + stub / 2, 0, stubR, stub, 16);
  parts.push({ kind: 'body', name: 'differentiel', body: final.ringBody, material: 'casting', data: b.build() });
}

/** Ce qui est en amont de l'arbre primaire : volant, double volant, embrayages, convertisseur, moteur électrique. */
function inputParts(parts: Part[], layout: GearboxLayout): void {
  const geom = layout.flywheelGeom;
  const y = geom.y;
  if (layout.form === 'reducer') {
    const rotor = new MeshBuilder();
    const r = geom.radius * 0.52;
    const w = geom.width / 2;
    lathePath(rotor, geom.x, [[0.1, -w * 0.82], [r, -w * 0.82], [r, w * 0.82], [0.1, w * 0.82]], 32, 25);
    for (let k = -3; k <= 3; k++) ring(rotor, geom.x + ((k * w) / 3.5), r - 0.6, r + 0.5, w * 0.1, 32);
    parts.push({ kind: 'body', name: 'rotor', body: layout.input, material: 'steel', data: moved(rotor.build(), 0, 0, 0) });
    const stator = new MeshBuilder();
    ring(stator, geom.x, r * 1.1, geom.radius * 0.88, geom.width * 0.92, 40, y, 0);
    const windings = new MeshBuilder();
    ring(windings, geom.x - w * 0.98, r * 1.14, geom.radius * 0.8, w * 0.14, 40, y, 0);
    ring(windings, geom.x + w * 0.98, r * 1.14, geom.radius * 0.8, w * 0.14, 40, y, 0);
    parts.push({ kind: 'casing', name: 'stator', body: -1, material: 'ghost', data: stator.build() });
    parts.push({ kind: 'static', name: 'bobinage', body: -1, material: 'brass', data: windings.build() });
    return;
  }
  if (layout.form === 'converter') {
    converterParts(parts, layout);
    return;
  }
  // Le volant : plaque, couronne de démarreur aux vraies dents, boulons.
  const primary = new MeshBuilder();
  const fw = geom.radius;
  const w = geom.width / 2;
  const x = geom.x;
  const dual = layout.flywheel === 'dual_mass';
  if (dual) {
    // Double masse : un canal ouvert côté boîte, où vivent les ressorts en arc.
    lathePath(primary, x, [[fw * 0.1, -w], [fw, -w], [fw, w], [fw * 0.93, w], [fw * 0.9, -w * 0.15], [fw * 0.58, -w * 0.3], [fw * 0.1, -w * 0.3], [fw * 0.1, -w]], 44, 20);
  } else {
    lathePath(primary, x, [[fw * 0.1, -w], [fw, -w], [fw, w], [fw * 0.34, w], [fw * 0.3, w * 0.2], [fw * 0.1, w * 0.2], [fw * 0.1, -w]], 44, 20);
  }
  if (geom.ringTeeth > 0) {
    const m = (2 * fw * 1.02) / geom.ringTeeth;
    const outline = toothOutline(geom.ringTeeth, m, m, 0);
    primary.add(extrudeOutline(outline, x - w, x - w + Math.max(geom.width * 0.32, 5), 0, fw - 0.5));
  }
  // Les boulons du vilebrequin.
  for (let k = 0; k < 8; k++) {
    const a = (k / 8) * TAU;
    ring(primary, x - w - 0.5, 0, Math.max(fw * 0.04, 2), 2.2, 8, fw * 0.17 * Math.cos(a), fw * 0.17 * Math.sin(a));
  }
  parts.push({ kind: 'body', name: 'volant', body: layout.flywheelBody, material: 'forged', data: primary.build() });

  if (dual) {
    // La masse secondaire : le plateau de friction, et les pattes qui appuient sur les ressorts.
    const s = new MeshBuilder();
    const inner = fw * 0.5;
    lathePath(s, x + w * 0.4, [[fw * 0.12, -w * 0.5], [inner, -w * 0.5], [inner, w * 0.5], [fw * 0.12, w * 0.5], [fw * 0.12, -w * 0.5]], 36, 15);
    for (let k = 0; k < 2; k++) {
      const a = (k / 2) * TAU;
      const c = fw * 0.72;
      const boxer = m34.create();
      m34.multiply(boxer, m34.translation(m34.create(), x + w * 0.1, c * Math.cos(a), c * Math.sin(a)), m34.rotationX(m34.create(), a));
      s.transform(boxer);
      s.box(w * 0.6, fw * 0.4, fw * 0.06);
      s.transform(null);
    }
    parts.push({ kind: 'body', name: 'masse-secondaire', body: layout.flywheelSecondaryBody, material: 'steel', data: s.build() });
  }
  // L'embrayage est dessiné dans sa propre vue : ici, seulement sa place.
  const clutch = new MeshBuilder();
  const clutchR = fw * 0.88;
  if (layout.form === 'dual_clutch') {
    ring(clutch, layout.clutchX - 6, fw * 0.2, clutchR, 8, 36, y, 0);
    ring(clutch, layout.clutchX + 6, fw * 0.2, clutchR * 0.82, 8, 36, y, 0);
  } else {
    ring(clutch, layout.clutchX, fw * 0.2, clutchR, 10, 36, y, 0);
  }
  parts.push({ kind: 'casing', name: 'embrayage', body: -1, material: 'ghost', data: clutch.build() });
}

/** Une géométrie déplacée, utile quand elle a été construite ailleurs. */
function moved(data: MeshData, x: number, y: number, z: number): MeshData {
  if (x === 0 && y === 0 && z === 0) return data;
  const positions = Float32Array.from(data.positions);
  for (let v = 0; v < positions.length; v += 3) {
    positions[v] += x;
    positions[v + 1] += y;
    positions[v + 2] += z;
  }
  return { positions, normals: data.normals, indices: data.indices };
}

function bladeRing(b: MeshBuilder, x: number, inner: number, outer: number, width: number, count: number, tilt: number): void {
  for (let k = 0; k < count; k++) {
    const a = (k / count) * TAU;
    const m = m34.create();
    const r = (inner + outer) / 2;
    m34.multiply(m, m34.translation(m34.create(), x, r * Math.cos(a), r * Math.sin(a)), m34.rotationX(m34.create(), a));
    m34.multiply(m, m, m34.rotationY(m34.create(), tilt));
    b.transform(m);
    b.box(width, outer - inner, 0.9);
    b.transform(null);
  }
}

function converterParts(parts: Part[], layout: GearboxLayout): void {
  const geom = layout.flywheelGeom;
  const y = geom.y;
  const R = layout.center * 1.02;
  const x = -46;
  const w = R * 0.42;
  // La plaque d'entraînement, et le double volant si la boîte en a un : c'est le même plateau.
  const plate = new MeshBuilder();
  const px = x - w - 6;
  lathePath(plate, px, [[R * 0.1, -1.5], [R * 1.14, -1.5], [R * 1.14, 1.5], [R * 0.1, 1.5], [R * 0.1, -1.5]], 44, 10);
  if (geom.ringTeeth > 0) {
    const m = (2 * R * 1.14) / geom.ringTeeth;
    plate.add(extrudeOutline(toothOutline(geom.ringTeeth, m, m, 0), px - 3, px + 3, 0, R * 1.12));
  }
  parts.push({ kind: 'body', name: 'plaque', body: layout.flywheelBody, material: 'forged', data: plate.build() });
  // La pompe, solidaire du moteur.
  const pump = new MeshBuilder();
  bladeRing(pump, x - w * 0.42, R * 0.46, R * 0.92, w * 0.62, 26, 0.32);
  lathePath(pump, x - w * 0.42, [[R * 0.26, -w * 0.46], [R * 0.86, -w * 0.46], [R * 0.95, -w * 0.14], [R * 0.95, -w * 0.05], [R * 0.9, -w * 0.05], [R * 0.82, -w * 0.33], [R * 0.26, -w * 0.33], [R * 0.26, -w * 0.46]], 40, 25);
  parts.push({ kind: 'body', name: 'pompe', body: layout.flywheelBody, material: 'cutSteel', data: pump.build() });
  // La turbine, sur l'arbre primaire.
  const turbine = new MeshBuilder();
  bladeRing(turbine, x + w * 0.42, R * 0.46, R * 0.92, w * 0.62, 24, -0.32);
  lathePath(turbine, x + w * 0.42, [[R * 0.2, w * 0.33], [R * 0.82, w * 0.33], [R * 0.9, w * 0.05], [R * 0.95, w * 0.05], [R * 0.95, w * 0.14], [R * 0.86, w * 0.46], [R * 0.2, w * 0.46], [R * 0.2, w * 0.33]], 40, 25);
  ring(turbine, x + w * 0.46, layout.center * 0.13, R * 0.22, w * 0.3, 20);
  // Le disque de pontage, sur la turbine.
  ring(turbine, x + w + 2, R * 0.3, R * 0.93, 3, 40);
  parts.push({ kind: 'body', name: 'turbine', body: layout.input, material: 'cutAlloy', data: turbine.build() });
  // Le réacteur, fixe sur sa roue libre.
  const stator = new MeshBuilder();
  bladeRing(stator, x, R * 0.3, R * 0.46, w * 0.28, 14, 0.75);
  ring(stator, x, R * 0.2, R * 0.3, w * 0.28, 24);
  parts.push({ kind: 'static', name: 'reacteur', body: -1, material: 'brass', data: moved(stator.build(), 0, y, 0) });
  // Le carter du convertisseur, à travers lequel on voit.
  const shell = new MeshBuilder();
  lathePath(shell, x, [[R * 0.2, -w - 1], [R * 0.9, -w - 1], [R * 1.05, -w * 0.5], [R * 1.05, w * 0.5], [R * 0.9, w + 4], [R * 0.2, w + 4], [R * 0.2, -w - 1]], 48, 30);
  parts.push({ kind: 'casing', name: 'coque', body: -1, material: 'ghost', data: moved(shell.build(), 0, y, 0) });
}

function planetParts(parts: Part[], layout: GearboxLayout): void {
  // Le porte-satellites, solidaire de la sortie : deux flasques et les axes.
  const output = layout.shafts.find((s) => s.body === layout.output);
  if (!output) return;
  const carrier = new MeshBuilder();
  const packs = new MeshBuilder();
  for (const set of layout.planets) {
    const w = set.width / 2 + 1.6;
    const r = set.carrierRadius;
    const planetR = layout.wheels[set.planets[0]].radius;
    ring(carrier, set.x + w, output.radius * 0.5, r + planetR * 0.3, 2, 28);
    for (let k = 0; k < set.planets.length; k++) {
      const a = (k / set.planets.length) * TAU;
      ring(carrier, set.x, 0, Math.max(planetR * 0.28, 1.6), set.width + 3, 8, r * Math.cos(a), r * Math.sin(a));
    }
    // Les embrayages multidisques entre deux trains : leurs disques se voient dans le carter.
    const annulus = layout.wheels[set.annulus];
    const packR = annulus.radius + annulus.moduleN * 2.2;
    for (let k = 0; k < 4; k++) ring(packs, set.x + set.width / 2 + 3 + k * 1.8, packR * 0.68, packR, 1.1, 32, set.y, 0);
  }
  parts.push({ kind: 'body', name: 'porte-satellites', body: layout.output, material: 'cutSteel', data: carrier.build() });
  parts.push({ kind: 'static', name: 'embrayages', body: -1, material: 'cutCasting', data: packs.build() });
}

/** Le carter : une coque translucide, l'huile au fond, et un dôme autour du pont. */
function casingParts(parts: Part[], layout: GearboxLayout): void {
  const { x0, x1, y0, y1, z0, z1 } = layout.bounds;
  const shell = new MeshBuilder();
  const length = x1 - x0;
  const midY = (y0 + y1) / 2;
  const midZ = (z0 + z1) / 2;
  const hy = (y1 - y0) / 2;
  const hz = (z1 - z0) / 2;
  const corner = Math.min(hy, hz) * 0.55;
  // Un coffre à angles arrondis : contour (y, z) extrudé le long de x, et ses deux faces de bout.
  const outline: OutlinePoint[] = [];
  const cornerPoints: [number, number, number][] = [[hy - corner, hz - corner, 0], [-(hy - corner), hz - corner, Math.PI / 2], [-(hy - corner), -(hz - corner), Math.PI], [hy - corner, -(hz - corner), Math.PI * 1.5]];
  for (const [cy, cz, start] of cornerPoints) {
    for (let k = 0; k <= 5; k++) {
      const a = start + (k / 5) * (Math.PI / 2);
      const py = midY + cy + corner * Math.cos(a);
      const pz = midZ + cz + corner * Math.sin(a);
      outline.push({ a: Math.atan2(pz, py), r: Math.hypot(pz, py) });
    }
  }
  shell.add(walls(outline, x0, x1));
  parts.push({ kind: 'casing', name: 'carter', body: -1, material: 'ghost', data: shell.build() });
  // L'huile : un volume plat au fond du carter, où les roues du bas trempent.
  const oilHeight = layout.oilY - y0;
  if (oilHeight > 0) {
    const oil = new MeshBuilder();
    oil.transform(m34.translation(m34.create(), (x0 + x1) / 2, y0 + oilHeight / 2 + 1, midZ));
    oil.box(length - 4, oilHeight, hz * 2 - corner * 0.6);
    oil.transform(null);
    parts.push({ kind: 'casing', name: 'huile', body: -1, material: 'oil', data: oil.build() });
  }
  if (layout.final) {
    const final = layout.final;
    const wheel = layout.wheels[final.ring];
    const r = wheel.radius + wheel.moduleN * 3;
    const dome = new MeshBuilder();
    if (final.kind === 'bevel') {
      // L'axe de la couronne suit z : le dôme la contient, de ce côté du pont.
      const m = m34.create();
      m[0] = 0; m[1] = 0; m[2] = 1; m[3] = final.x;
      m[4] = 1; m[5] = 0; m[6] = 0; m[7] = final.y;
      m[8] = 0; m[9] = 1; m[10] = 0; m[11] = 0;
      dome.transform(m);
      dome.add(latheData(aroundZero(r, wheel.width * 3)));
      dome.transform(null);
    } else {
      dome.transform(m34.translation(m34.create(), final.x, final.y, final.z));
      dome.add(latheData(aroundZero(r, final.caseWidth * 0.8)));
      dome.transform(null);
    }
    parts.push({ kind: 'casing', name: 'carter-pont', body: -1, material: 'ghost', data: dome.build() });
  }
}

function aroundZero(radius: number, width: number): [number, number][] {
  const w = width / 2;
  return [[radius * 0.3, -w], [radius * 0.8, -w], [radius, -w * 0.5], [radius, w * 0.5], [radius * 0.8, w], [radius * 0.3, w]];
}

function latheData(profile: [number, number][]): MeshData {
  const b = new MeshBuilder();
  b.transform(aroundX(0));
  b.lathe(profile, 44, 30);
  b.transform(null);
  return b.build();
}

/** Les parois d'un coffre, sans dents : un contour fermé, deux plaques de bout. */
function walls(outline: readonly OutlinePoint[], x0: number, x1: number): MeshData {
  const flat = new Flat();
  const n = outline.length;
  const at = (k: number, x: number): number[] => {
    const p = outline[k % n];
    return [x, p.r * Math.cos(p.a), p.r * Math.sin(p.a)];
  };
  let cy = 0;
  let cz = 0;
  for (const p of outline) {
    cy += p.r * Math.cos(p.a);
    cz += p.r * Math.sin(p.a);
  }
  cy /= n;
  cz /= n;
  for (let k = 0; k < n; k++) {
    flat.quad(at(k, x0), at(k + 1, x0), at(k + 1, x1), at(k, x1));
    flat.quad(at(k, x1), at(k + 1, x1), [x1, cy, cz], [x1, cy, cz]);
    flat.quad(at(k + 1, x0), at(k, x0), [x0, cy, cz], [x0, cy, cz]);
  }
  return flat.data();
}

