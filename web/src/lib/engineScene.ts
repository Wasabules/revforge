/**
 * Le moteur en 3D : toutes les pièces sont générées depuis `EngineLayout`, en millimètres, et bougent depuis `EnginePose`.
 * Les pièces qui se répètent sont instanciées, ce qui est fixe est fusionné : une scène de V12 tient en quelques dizaines
 * de dessins. Vue de face : x à droite, y en haut, z vers l'avant. Rien n'alloue dans `update`.
 */
import type * as T from 'three';
import type { Theme } from './canvas';
import { EVC, EVO, IVC, IVO } from './fluidGas';
import { bankToFront, chainCircles, chainPath, type ChainPath, collectorPosition, DEG, type Cylinder, type EngineLayout, type EnginePose, lobeNoseDeg, lobeRadius, plenumPosition, stackOf, rockerOutline, hasOpenStacks } from './engineLayout';
import { chargerUnit, lobedOutline } from './superchargerLayout';
import { turboChain } from './turboChain';
import type { Motion } from './motion';
import { disposeObject, m34, type Mat34, MeshBuilder, parseColor, type Stage3D, toGeometry } from './render3d';

type P2 = [number, number];
type P3 = [number, number, number];

/** Finesse des maillages : pleine sur un GPU, moitié sur un rendu logiciel, où chaque triangle se paie cher. */
let detail = 1;
const seg = (full: number, least = 6) => Math.max(least, Math.round(full * detail));

/** T(x, y, z) · Rx(90°) : l'axe Y d'un tour devient l'axe Z du moteur, vers l'avant. */
function onZ(x: number, y: number, z: number): Mat34 {
  const m = m34.rotationX(m34.create(), Math.PI / 2);
  m[3] = x;
  m[7] = y;
  m[11] = z;
  return m;
}

function chainM(...items: Mat34[]): Mat34 {
  const out = m34.create();
  m34.identity(out);
  for (const item of items) m34.multiply(out, out, item);
  return out;
}

const tr = (x: number, y: number, z: number) => m34.translation(m34.create(), x, y, z);
const rz = (a: number) => m34.rotationZ(m34.create(), a);
const ry = (a: number) => m34.rotationY(m34.create(), a);
const rx = (a: number) => m34.rotationX(m34.create(), a);
const bankM = (betaDeg: number) => rz(-betaDeg * DEG);

/** Un cercle, dans le sens trigonométrique. */
function circle(radius: number, points: number, from = 0, to = Math.PI * 2, cx = 0, cy = 0): P2[] {
  const out: P2[] = [];
  const closed = Math.abs(to - from - Math.PI * 2) < 1e-6;
  const count = closed ? points : points + 1;
  for (let k = 0; k < count; k++) {
    const a = from + ((to - from) * k) / points;
    out.push([cx + Math.cos(a) * radius, cy + Math.sin(a) * radius]);
  }
  return out;
}

/** Un flasque vu de face, le maneton vers +y : tête autour du maneton, taille, puis le contrepoids. */
function webOutline(layout: EngineLayout): P2[] {
  const { crank: r, pinRadius, mainRadius, bore } = layout;
  const headR = pinRadius + bore * 0.06;
  const waist = mainRadius * 0.92;
  // Le contrepoids s'arrête avant de toucher une jupe de piston au point mort bas.
  const clear = layout.rod - r - layout.pistonBottom - bore * 0.04;
  const weightR = Math.min(Math.max(r * 1.22, mainRadius * 1.5), clear);
  const out: P2[] = [];
  for (let k = 0; k <= 10; k++) {
    const a = (k / 10) * Math.PI;
    out.push([Math.cos(a) * headR, r + Math.sin(a) * headR]);
  }
  out.push([-waist, mainRadius * 0.12]);
  const spread = 68 * DEG;
  for (let k = 0; k <= 12; k++) {
    const a = -Math.PI / 2 - spread + (2 * spread * k) / 12;
    out.push([Math.cos(a) * weightR, Math.sin(a) * weightR]);
  }
  out.push([waist, mainRadius * 0.12]);
  return out;
}

function flyingWebOutline(a: P2, b: P2, radius: number): P2[] {
  const angle = Math.atan2(b[1] - a[1], b[0] - a[0]);
  const out: P2[] = [];
  for (let k = 0; k <= 6; k++) {
    const u = angle + Math.PI / 2 + (Math.PI * k) / 6;
    out.push([a[0] + Math.cos(u) * radius, a[1] + Math.sin(u) * radius]);
  }
  for (let k = 0; k <= 6; k++) {
    const u = angle - Math.PI / 2 + (Math.PI * k) / 6;
    out.push([b[0] + Math.cos(u) * radius, b[1] + Math.sin(u) * radius]);
  }
  return out;
}

/** Vilebrequin : tourillons, manetons, puis flasques et contrepoids. Les angles viennent de l'ordre d'allumage. */
function buildCrank(layout: EngineLayout): { journals: MeshBuilder; webs: MeshBuilder } {
  const journals = new MeshBuilder();
  const webs = new MeshBuilder();
  const { crank: r, mainRadius, pinRadius, webWidth, bore } = layout;
  const outline = webOutline(layout);
  for (const main of layout.mains) {
    journals.transform(onZ(0, 0, main.z)).cylinder(mainRadius, main.width + webWidth * 0.3, seg(20));
  }
  for (const item of layout.throws) {
    const positions: P2[] = item.pins.map((pin) => [r * Math.sin(pin.deg * DEG), r * Math.cos(pin.deg * DEG)]);
    item.pins.forEach((pin, k) => {
      journals.transform(onZ(positions[k][0], positions[k][1], pin.z)).cylinder(pinRadius, pin.width + webWidth * 0.3, seg(18));
    });
    const front = item.pins[0];
    const rear = item.pins[item.pins.length - 1];
    webs.transform(chainM(tr(0, 0, item.zFront - webWidth / 2), rz(-front.deg * DEG))).extrude(outline, webWidth, 40);
    webs.transform(chainM(tr(0, 0, item.zRear + webWidth / 2), rz(-rear.deg * DEG))).extrude(outline, webWidth, 40);
    // Deux manetons décalés : un flasque volant les relie, comme sur un V6 à 60° ou un V10.
    if (item.pins.length === 2) {
      const z = (item.pins[0].z + item.pins[1].z) / 2;
      const width = Math.abs(item.pins[0].z - item.pins[1].z) - item.pins[0].width;
      webs.transform(tr(0, 0, z)).extrude(flyingWebOutline(positions[0], positions[1], pinRadius * 1.08), Math.max(width, bore * 0.03), 40);
    }
  }
  // Le nez, côté distribution, et la bride du volant.
  const snoutEnd = layout.pulleyZ + bore * 0.1;
  journals.transform(onZ(0, 0, (layout.frontZ + snoutEnd) / 2)).cylinder(bore * 0.17, snoutEnd - layout.frontZ, 16);
  journals.transform(onZ(0, 0, (layout.rearZ + layout.ring.z) / 2)).cylinder(mainRadius * 1.25, layout.rearZ - layout.ring.z, 20);
  return { journals, webs };
}

/** Volant et couronne dentée. Le double volant a sa seconde masse, à part, et des ressorts en arc. */
function buildFlywheel(layout: EngineLayout, builder: MeshBuilder, secondary: MeshBuilder | null): void {
  const { ring, bore } = layout;
  const module = (ring.radius * 2) / ring.teeth;
  const root = ring.radius - module * 0.62;
  const tip = ring.radius + module * 0.62;
  const zFace = ring.z - ring.width / 2;
  const hub = bore * 0.42;
  // Le disque.
  const thick = bore * 0.32;
  builder.transform(onZ(0, 0, 0));
  builder.transform(chainM(tr(0, 0, zFace - thick / 2 + ring.width), rx(Math.PI / 2))).lathe([
    [0, -thick / 2],
    [hub, -thick / 2],
    [hub * 1.5, -thick * 0.12],
    [root - bore * 0.08, -thick * 0.12],
    [root - bore * 0.04, -thick / 2],
    [root, -thick / 2],
    [root, thick / 2],
    [hub * 1.5, thick / 2],
    [hub, thick / 2 + bore * 0.04],
    [0, thick / 2 + bore * 0.04],
  ], 48);
  builder.transform(tr(0, 0, ring.z)).gear(ring.teeth, root, tip, ring.width, root - bore * 0.08);
  // Les vis, autour du moyeu.
  for (let k = 0; k < 8; k++) {
    const a = (k / 8) * Math.PI * 2;
    builder.transform(onZ(Math.cos(a) * hub * 0.72, Math.sin(a) * hub * 0.72, ring.z + thick / 2 + bore * 0.02)).cylinder(bore * 0.035, bore * 0.04, 6);
  }
  if (!secondary) return;
  // Double volant : ressorts en arc entre les deux masses, qui encaissent les à-coups.
  const springR = root * 0.74;
  const wire = bore * 0.018;
  const coilR = bore * 0.075;
  for (let arc = 0; arc < 2; arc++) {
    const path: P3[] = [];
    const start = arc * Math.PI + 0.25;
    const sweep = Math.PI - 0.5;
    const coils = 16;
    const samples = coils * 7;
    for (let k = 0; k <= samples; k++) {
      const u = k / samples;
      const along = start + sweep * u;
      const turn = u * coils * Math.PI * 2;
      const radial = springR + Math.cos(turn) * coilR;
      path.push([Math.cos(along) * radial, Math.sin(along) * radial, zFace - thick - bore * 0.08 + Math.sin(turn) * coilR]);
    }
    builder.transform(null).tube(path, wire, 5);
  }
  // La seconde masse, derrière : c'est elle qui porte l'embrayage.
  const z2 = zFace - thick - bore * 0.26;
  secondary.transform(chainM(tr(0, 0, z2), rx(Math.PI / 2))).lathe([
    [bore * 0.2, -bore * 0.11],
    [root * 0.95, -bore * 0.11],
    [root * 0.97, bore * 0.11],
    [hub, bore * 0.11],
    [bore * 0.2, bore * 0.14],
  ], 40);
  for (let k = 0; k < 6; k++) {
    const a = (k / 6) * Math.PI * 2;
    secondary.transform(onZ(Math.cos(a) * root * 0.86, Math.sin(a) * root * 0.86, z2 - bore * 0.12)).cylinder(bore * 0.03, bore * 0.03, 6);
  }
}

/** Un piston à jupe courte : tête et gorges au tour, jupes de poussée seulement, bossages visibles. */
function buildPiston(layout: EngineLayout): { body: MeshBuilder; rings: MeshBuilder; pin: MeshBuilder } {
  const { bore, pistonTop: top, pistonBottom: bottom } = layout;
  const R = bore / 2 - 0.25;
  const groove = bore * 0.02;
  const land = (y: number, height: number): P2[] => [[R, y - height / 2], [R - groove, y - height / 2], [R - groove, y + height / 2], [R, y + height / 2]];
  const ringY = [top - bore * 0.075, top - bore * 0.135, top - bore * 0.2];
  const ringH = [bore * 0.022, bore * 0.022, bore * 0.04];
  const under = bore * 0.03;
  const profile: P2[] = [[0, under], [R * 0.86, under], [R, under + bore * 0.03]];
  for (let k = ringY.length - 1; k >= 0; k--) profile.push(...land(ringY[k], ringH[k]));
  if (layout.fuel === 'diesel') {
    // Le bol du diesel : le jet de l'injecteur y brûle.
    profile.push([R, top], [bore * 0.3, top], [bore * 0.27, top - bore * 0.14], [bore * 0.12, top - bore * 0.16], [0, top - bore * 0.08]);
  } else {
    profile.push([R, top - bore * 0.01], [R * 0.86, top], [0, top]);
  }
  const body = new MeshBuilder();
  body.lathe(profile, seg(28, 14));
  // Les jupes : deux arcs, du côté où la bielle pousse le piston contre le cylindre.
  const outer = circle(R, 12, -52 * DEG, 52 * DEG);
  const inner = circle(R - bore * 0.035, 12, 52 * DEG, -52 * DEG);
  const skirt: P2[] = [...outer, ...inner];
  const height = bottom + bore * 0.08;
  for (const side of [0, Math.PI]) {
    body.transform(chainM(ry(side), tr(0, under - height / 2, 0), rx(-Math.PI / 2))).extrude(skirt, height, 30);
  }
  // Les bossages de l'axe, dans le prolongement de l'axe du piston.
  for (const side of [-1, 1]) {
    body.transform(onZ(0, 0, side * bore * 0.25)).cylinder(bore * 0.15, bore * 0.12, 14);
    body.transform(tr(0, bore * 0.08, side * bore * 0.25)).box(bore * 0.24, bore * 0.16, bore * 0.12);
  }
  const rings = new MeshBuilder();
  ringY.forEach((y, k) => {
    rings.lathe([[R - groove, y - ringH[k] / 2], [R + 0.15, y - ringH[k] / 2], [R + 0.15, y + ringH[k] / 2], [R - groove, y + ringH[k] / 2]], seg(28, 14));
  });
  const pin = new MeshBuilder();
  pin.transform(onZ(0, 0, 0)).lathe([[bore * 0.055, -bore * 0.34], [bore * 0.1, -bore * 0.34], [bore * 0.1, bore * 0.34], [bore * 0.055, bore * 0.34]], 14);
  return { body, rings, pin };
}

/** Une bielle en I, grosse tête à l'origine, petite tête en +y. */
function buildRod(layout: EngineLayout): MeshBuilder {
  const { rod, bore, pinRadius, rodWidth } = layout;
  const b = new MeshBuilder();
  const bigOuter = pinRadius + bore * 0.07;
  const smallOuter = bore * 0.145;
  const smallInner = bore * 0.1;
  b.extrudeRing(circle(bigOuter, 24), circle(pinRadius, 24), rodWidth, 40);
  b.transform(tr(0, rod, 0)).extrudeRing(circle(smallOuter, 18), circle(smallInner, 18), rodWidth * 0.72, 40);
  b.transform(null);
  const yFrom = bigOuter * 0.85;
  const yTo = rod - smallOuter * 0.85;
  // Le voile, mince, et les deux semelles du I.
  b.extrude([[bore * 0.13, yFrom], [bore * 0.075, yTo], [-bore * 0.075, yTo], [-bore * 0.13, yFrom]], rodWidth * 0.26, 5);
  for (const side of [-1, 1]) {
    const inner0 = bore * 0.115;
    const inner1 = bore * 0.062;
    const outer0 = bore * 0.17;
    const outer1 = bore * 0.095;
    const outline: P2[] = side > 0 ? [[inner0, yFrom], [outer0, yFrom], [outer1, yTo], [inner1, yTo]] : [[-outer0, yFrom], [-inner0, yFrom], [-inner1, yTo], [-outer1, yTo]];
    b.extrude(outline, rodWidth * 0.82, 5);
  }
  // Les deux vis du chapeau.
  for (const side of [-1, 1]) {
    b.transform(tr(side * (pinRadius + bore * 0.035), -pinRadius * 0.2, 0)).cylinder(bore * 0.022, pinRadius * 1.6, 6);
    b.transform(tr(side * (pinRadius + bore * 0.035), -pinRadius * 1.05, 0)).cylinder(bore * 0.04, bore * 0.035, 6);
  }
  b.transform(null);
  return b;
}

function buildValve(layout: EngineLayout, radius: number): MeshBuilder {
  const { stem, stemRadius } = layout.valve;
  const b = new MeshBuilder();
  const face = layout.bore * 0.02;
  // Tête en tulipe, portée à 45°, puis la queue.
  b.lathe([
    [0, 0],
    [radius - face, 0],
    [radius, face],
    [radius * 0.8, face * 2.2],
    [stemRadius * 2.2, layout.bore * 0.09],
    [stemRadius, layout.bore * 0.17],
    [stemRadius, stem],
    [0, stem],
  ], seg(16, 8), 25);
  return b;
}

/** Un ressort de soupape, y de 0 à `height` : l'instance le comprime. */
function buildSpring(layout: EngineLayout, height: number): MeshBuilder {
  const radius = layout.bore * 0.085;
  const wire = layout.bore * 0.013;
  const coils = 5.5;
  const samples = Math.ceil(coils * seg(12, 5));
  const path: P3[] = [];
  for (let k = 0; k <= samples; k++) {
    const u = k / samples;
    const a = u * coils * Math.PI * 2;
    path.push([Math.cos(a) * radius, u * height, Math.sin(a) * radius]);
  }
  return new MeshBuilder().tube(path, wire, detail < 1 ? 3 : 5);
}

function buildBucket(layout: EngineLayout): MeshBuilder {
  const r = layout.bore * 0.105;
  const h = layout.bore * 0.1;
  // Une coupelle renversée : la came appuie sur son fond, la soupape dessous.
  return new MeshBuilder().lathe([[r * 0.92, -h], [r, -h], [r, 0], [0, 0]], seg(16, 8));
}

function buildRetainer(layout: EngineLayout): MeshBuilder {
  const b = layout.bore;
  return new MeshBuilder().lathe([[b * 0.03, -b * 0.022], [b * 0.095, 0], [b * 0.095, b * 0.012], [b * 0.028, b * 0.012]], seg(12));
}

/** Le contour d'un bossage, dans le repère de la vue de face. */
function lobeOutline(layout: EngineLayout, cylinder: Cylinder, side: number, exhaust: boolean): P2[] {
  const { valve } = layout;
  const open = exhaust ? EVO : IVO;
  const close = exhaust ? EVC : IVC;
  const nose = lobeNoseDeg(cylinder, valve.followerDeg, side, open, close);
  const outline: P2[] = [];
  const points = seg(44, 24);
  for (let k = 0; k < points; k++) {
    // Sens trigonométrique : l'angle horaire décroît.
    const a = -(k / points) * 360;
    const radius = lobeRadius(valve.camBase, valve.camLift, a - nose, open, close);
    outline.push([Math.sin(a * DEG) * radius, Math.cos(a * DEG) * radius]);
  }
  return outline;
}

/**
 * Un arbre à cames : un bossage par soupape, calé pour ce cylindre, et son pignon. Axe sur Z, centré sur l'origine.
 * `exhaust` nul : le seul arbre d'un simple arbre, qui porte l'admission et l'échappement.
 */
function buildCam(layout: EngineLayout, bank: number, side: number, exhaust: boolean | null): { shaft: MeshBuilder; sprocket: MeshBuilder } {
  const { valve, bore, sprocket } = layout;
  const cylinders = layout.byBank[bank];
  const shaft = new MeshBuilder();
  const lobeWidth = valve.sohc ? bore * 0.13 : bore * 0.15;
  for (const cylinder of cylinders) {
    if (exhaust === null) {
      // Chaque bossage en face de son culbuteur : l'admission d'un côté, l'échappement de l'autre.
      for (const lobeSide of [-1, 1]) {
        const lobe = lobeOutline(layout, cylinder, lobeSide, lobeSide !== cylinder.intakeSide);
        shaft.transform(tr(0, 0, cylinder.z + lobeSide * valve.zOffset)).extrude(lobe, lobeWidth, 35);
      }
      // Le palier, de l'autre côté de la bougie.
      shaft.transform(onZ(0, 0, cylinder.z - bore * 0.32)).cylinder(valve.camBase * 0.95, bore * 0.14, 16);
      continue;
    }
    const outline = lobeOutline(layout, cylinder, side, exhaust);
    for (const offset of [-valve.zOffset, valve.zOffset]) {
      shaft.transform(tr(0, 0, cylinder.z + offset)).extrude(outline, lobeWidth, 35);
    }
    // Le palier, entre les deux bossages du cylindre.
    shaft.transform(onZ(0, 0, cylinder.z)).cylinder(valve.camBase * 0.95, bore * 0.16, 16);
  }
  const zFront = layout.sprocket.z[bank] ?? layout.sprocket.z[0];
  const zRear = cylinders[cylinders.length - 1].z - bore * 0.42;
  shaft.transform(onZ(0, 0, (zFront + zRear) / 2)).cylinder(valve.camBase * 0.62, zFront - zRear, 12);
  const module = (sprocket.camRadius * 2) / sprocket.camTeeth;
  const wheel = new MeshBuilder();
  wheel.transform(tr(0, 0, zFront)).gear(sprocket.camTeeth, sprocket.camRadius - module * 0.55, sprocket.camRadius + module * 0.55, bore * 0.09, valve.camBase * 0.5);
  return { shaft, sprocket: wheel };
}

/** Un culbuteur : axe à l'origine, soupape fermée, le bras vers +x. Le demi-tour de l'instance l'envoie de l'autre côté. */
function buildRocker(layout: EngineLayout): MeshBuilder {
  const rocker = layout.valve.rocker!;
  const b = new MeshBuilder();
  b.extrude(rockerOutline(rocker, layout.bore), rocker.width, 40);
  return b;
}

function buildChainLink(layout: EngineLayout, pitch: number): MeshBuilder {
  const b = new MeshBuilder();
  const height = layout.bore * 0.045;
  const gap = layout.bore * 0.04;
  // Rendu logiciel : un maillon plein, il y en a des centaines.
  if (detail < 1) return b.box(pitch * 1.1, height, gap * 2.2);
  const r = height / 2;
  const half = pitch * 0.62;
  const outline: P2[] = [];
  for (let k = 0; k <= 4; k++) {
    const a = -Math.PI / 2 + (k / 4) * Math.PI;
    outline.push([half - r + Math.cos(a) * r, Math.sin(a) * r]);
  }
  for (let k = 0; k <= 4; k++) {
    const a = Math.PI / 2 + (k / 4) * Math.PI;
    outline.push([-half + r + Math.cos(a) * r, Math.sin(a) * r]);
  }
  for (const side of [-1, 1]) b.transform(tr(0, 0, side * gap)).extrude(outline, layout.bore * 0.012, 50);
  b.transform(onZ(pitch / 2, 0, 0)).cylinder(r * 0.7, gap * 2, 6);
  return b;
}

/** Un tube qui part d'un point dans une direction, et arrive sur un autre : assez pour un collecteur. */
function bendPath(a: P3, da: P3, b: P3, db: P3, steps = 8): P3[] {
  const path: P3[] = [];
  const length = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  for (let k = 0; k <= steps; k++) {
    const u = k / steps;
    const v = 1 - u;
    const h00 = v * v * (1 + 2 * u);
    const h10 = u * v * v;
    const h01 = u * u * (3 - 2 * u);
    const h11 = -u * u * v;
    const point: P3 = [0, 0, 0];
    for (let i = 0; i < 3; i++) point[i] = h00 * a[i] + h10 * da[i] * length + h01 * b[i] + h11 * db[i] * length;
    path.push(point);
  }
  return path;
}

function bankPoint(betaDeg: number, x: number, y: number, z: number): P3 {
  const p = [0, 0];
  bankToFront(p, betaDeg, x, y);
  return [p[0], p[1], z];
}

/** Le rayon d'un cône à ce point de sa longueur : large à sa base, un peu moins à son bout. */
function coneRadiusAt(along: number, radius: number, length: number): number {
  return radius * (1 - (0.3 * along) / length);
}

/** Le profil d'un cône de filtre, à faire tourner : sa base sur le conduit, son flanc, sa calotte. */
function coneProfile(start: number, radius: number, length: number, neck: number): P2[] {
  const end = coneRadiusAt(length, radius, length);
  return [[neck, 0], [neck * 1.1, start * 0.7], [radius, start], [end, start + length], [end * 0.7, start + length * 1.03], [0, start + length * 1.05]];
}

function bankDir(betaDeg: number, x: number, y: number): P3 {
  const p = [0, 0];
  bankToFront(p, betaDeg, x, y);
  return [p[0], p[1], 0];
}

export interface PartLabel {
  key: string;
  x: number;
  y: number;
  z: number;
}

interface Dynamic {
  pistons: T.InstancedMesh;
  rings: T.InstancedMesh;
  pins: T.InstancedMesh;
  rods: T.InstancedMesh;
  valvesIn: T.InstancedMesh;
  valvesEx: T.InstancedMesh;
  springs: T.InstancedMesh;
  retainers: T.InstancedMesh;
  /** Double arbre : un poussoir sous chaque came. Simple arbre : un culbuteur par soupape. */
  buckets: T.InstancedMesh | null;
  rockers: T.InstancedMesh | null;
  chain: T.InstancedMesh;
  gas: T.InstancedMesh;
  flame: T.InstancedMesh;
  spark: T.InstancedMesh;
  spray: T.InstancedMesh;
  jets: T.InstancedMesh;
  portsIn: T.InstancedMesh;
  portsEx: T.InstancedMesh;
  glowTips: T.InstancedMesh | null;
  pinion: T.Mesh;
  butterflies: T.Group[];
  accessory: T.InstancedMesh;
}

/** Une scène de moteur thermique. `dispose` rend tout ce qu'elle a pris au GPU. */
export class EngineScene {
  readonly root: T.Group;
  readonly labels: PartLabel[] = [];
  /** Combien d'étincelles, de flammes et de jets sont dessinés à cette image : pour les tests. */
  drawnSparks = 0;
  drawnFlames = 0;
  drawnSprays = 0;
  center: P3 = [0, 0, 0];
  radius = 1;

  private readonly stage: Stage3D;
  private readonly layout: EngineLayout;
  private readonly d: Dynamic;
  private readonly crankGroup: T.Group;
  private readonly secondary: T.Mesh | null;
  private readonly cams: T.Object3D[] = [];
  private readonly turboWheels: T.Object3D[] = [];
  /** Ce qui tourne avec l'arbre du compresseur mécanique, et à quelle part de sa vitesse. */
  private readonly chargerParts: { mesh: T.Object3D; ratio: number }[] = [];
  /** Ce qui bouge selon l'état réel des soupapes, à sa position de repos. */
  private readonly wastegateRods: { mesh: T.Object3D; rest: number; travel: number }[] = [];
  private readonly blowoffPistons: { mesh: T.Object3D; rest: number; travel: number }[] = [];
  private readonly labelSpots: { maf?: P3; blowoff?: P3; coldPipe?: P3; carb?: P3 } = {};
  private coolerGeometry: T.BufferGeometry | null = null;
  private hotPipeMaterial: T.MeshStandardMaterial | null = null;
  private coldPipeMaterial: T.MeshStandardMaterial | null = null;
  private readonly exhaustMaterial: T.MeshStandardMaterial;
  private readonly edges: T.LineSegments[] = [];
  private readonly edgeMaterial: T.LineBasicMaterial;
  private readonly chains: ChainPath[];
  private readonly chainZ: number[];
  /** Matrices de base : cylindre (rangée puis z), soupape à l'assise, ressort posé. */
  private readonly cylinderBase: T.Matrix4[] = [];
  private readonly valveBase: T.Matrix4[] = [];
  private readonly rockerBase: T.Matrix4[] = [];
  private readonly sparkAt: Float64Array;
  private readonly accessoryAt: { x: number; y: number; z: number; ratio: number; scale: number }[] = [];
  private readonly M: T.Matrix4;
  private readonly N: T.Matrix4;
  private readonly C: T.Color;
  private readonly rgb = new Float64Array(3);
  private readonly springBase: number;
  private readonly springHeight: number;
  private readonly retainerY: number;
  private readonly bucketY: number;
  private readonly chamberTop: number;
  private lastTheme: Theme | null = null;
  private coolR = 0.4;
  private coolG = 0.7;
  private coolB = 1;

  constructor(stage: Stage3D, layout: EngineLayout) {
    this.stage = stage;
    this.layout = layout;
    detail = stage.software ? 0.5 : 1;
    const { THREE, materials } = stage;
    const { bore, deck, valve } = layout;
    this.M = new THREE.Matrix4();
    this.N = new THREE.Matrix4();
    this.C = new THREE.Color();
    this.root = new THREE.Group();
    stage.scene.add(this.root);

    const geo = (builder: MeshBuilder) => toGeometry(THREE, builder.build());
    const add = (geometry: T.BufferGeometry, material: T.Material, cast = true) => {
      const mesh = new THREE.Mesh(geometry, material);
      mesh.matrixAutoUpdate = false;
      mesh.castShadow = cast;
      this.root.add(mesh);
      return mesh;
    };
    const instanced = (geometry: T.BufferGeometry, material: T.Material, capacity: number, cast = true) => {
      const mesh = new THREE.InstancedMesh(geometry, material, capacity);
      mesh.frustumCulled = false;
      mesh.castShadow = cast;
      this.root.add(mesh);
      return mesh;
    };
    const glowMaterial = () => new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false, side: THREE.DoubleSide });

    // ── Ce qui tourne avec le vilebrequin ──
    const crankGroup = new THREE.Group();
    this.crankGroup = crankGroup;
    this.root.add(crankGroup);
    const crank = buildCrank(layout);
    const addCrank = (builder: MeshBuilder, material: T.Material) => {
      const mesh = new THREE.Mesh(geo(builder), material);
      mesh.matrixAutoUpdate = false;
      mesh.castShadow = true;
      crankGroup.add(mesh);
    };
    addCrank(crank.journals, materials.steel);
    addCrank(crank.webs, materials.forged);
    const flywheel = new MeshBuilder();
    const secondaryBuilder = layout.dualMass ? new MeshBuilder() : null;
    buildFlywheel(layout, flywheel, secondaryBuilder);
    // Le pignon de chaîne et le damper, solidaires du nez.
    const crankModule = (layout.sprocket.crankRadius * 2) / layout.sprocket.crankTeeth;
    for (const z of layout.sprocket.z) {
      flywheel.transform(tr(0, 0, z)).gear(layout.sprocket.crankTeeth, layout.sprocket.crankRadius - crankModule * 0.55, layout.sprocket.crankRadius + crankModule * 0.55, bore * 0.09, bore * 0.14);
    }
    const pulleyR = bore * 0.72;
    flywheel.transform(onZ(0, 0, layout.pulleyZ)).lathe([
      [bore * 0.14, -bore * 0.13],
      [pulleyR * 0.5, -bore * 0.13],
      [pulleyR * 0.56, -bore * 0.05],
      [pulleyR * 0.86, -bore * 0.05],
      [pulleyR * 0.86, -bore * 0.12],
      [pulleyR, -bore * 0.13],
      [pulleyR, bore * 0.13],
      [pulleyR * 0.86, bore * 0.12],
      [pulleyR * 0.86, bore * 0.04],
      [pulleyR * 0.56, bore * 0.04],
      [pulleyR * 0.5, bore * 0.13],
      [bore * 0.14, bore * 0.16],
    ], 40);
    flywheel.transform(null);
    addCrank(flywheel, materials.forged);
    // Le joint de caoutchouc du damper, entre le moyeu et sa masse.
    const damper = new MeshBuilder();
    damper.transform(onZ(0, 0, layout.pulleyZ + bore * 0.02)).lathe([[pulleyR * 0.83, -bore * 0.05], [pulleyR * 0.87, -bore * 0.05], [pulleyR * 0.87, bore * 0.05], [pulleyR * 0.83, bore * 0.05]], 40);
    addCrank(damper, materials.rubber);
    this.secondary = null;
    if (secondaryBuilder) {
      const mesh = new THREE.Mesh(geo(secondaryBuilder), materials.alloy);
      mesh.matrixAutoUpdate = false;
      mesh.castShadow = true;
      this.root.add(mesh);
      this.secondary = mesh;
    }

    // ── L'attelage mobile ──
    const count = layout.count;
    const piston = buildPiston(layout);
    const pistons = instanced(geo(piston.body), materials.alloy, count);
    const rings = instanced(geo(piston.rings), materials.forged, count, false);
    const pins = instanced(geo(piston.pin), materials.steel, count, false);
    const rods = instanced(geo(buildRod(layout)), materials.forged, count);
    pistons.name = 'pistons';
    rods.name = 'rods';

    // ── Les soupapes et leur commande ──
    const springBase = valve.stem * 0.55;
    const retainerY = valve.stem - bore * 0.08;
    const springHeight = retainerY - springBase;
    this.springBase = springBase;
    this.springHeight = springHeight;
    this.retainerY = retainerY;
    this.bucketY = valve.stem + bore * 0.05;
    const valves = count * valve.perCylinder;
    const valvesIn = instanced(geo(buildValve(layout, valve.radiusIn)), materials.steel, valves / 2);
    const valvesEx = instanced(geo(buildValve(layout, valve.radiusEx)), materials.forged, valves / 2);
    const springs = instanced(geo(buildSpring(layout, springHeight)), materials.steel, valves, false);
    const retainers = instanced(geo(buildRetainer(layout)), materials.forged, valves, false);
    const buckets = valve.sohc ? null : instanced(geo(buildBucket(layout)), materials.alloy, valves, false);
    const rockers = valve.sohc ? instanced(geo(buildRocker(layout)), materials.forged, valves) : null;

    const THREE_rz = new THREE.Matrix4();
    const tilted = new THREE.Matrix4();
    for (const cylinder of layout.cylinders) {
      const base = new THREE.Matrix4().makeRotationZ(-cylinder.betaDeg * DEG);
      base.multiply(new THREE.Matrix4().makeTranslation(0, 0, cylinder.z));
      this.cylinderBase.push(base);
      for (const slot of valve.slots) {
        const side = slot.exhaust ? -cylinder.intakeSide : cylinder.intakeSide;
        const dz = slot.zSign * side * valve.zOffset;
        const m = base.clone();
        m.multiply(tilted.makeTranslation(side * valve.x, deck + valve.seatRise, dz));
        m.multiply(THREE_rz.makeRotationZ(-side * valve.tiltDeg * DEG));
        this.valveBase.push(m);
        if (valve.rocker) {
          // Le culbuteur est dessiné vers +x : un demi-tour autour de l'axe du cylindre le met de l'autre côté, sans miroir.
          const r = base.clone();
          r.multiply(tilted.makeTranslation(side * valve.rocker.pivotX, valve.rocker.pivotY, dz));
          if (side < 0) r.multiply(THREE_rz.makeRotationY(Math.PI));
          this.rockerBase.push(r);
        }
      }
    }

    // ── Les cames ──
    for (let bank = 0; bank < layout.banks; bank++) {
      const beta = layout.banks === 1 ? 0 : bank === 0 ? -layout.halfDeg : layout.halfDeg;
      const intakeSide = layout.byBank[bank][0].intakeSide;
      for (const exhaust of valve.sohc ? [null] : [false, true]) {
        const side = exhaust ? -intakeSide : intakeSide;
        const built = buildCam(layout, bank, side, exhaust);
        const group = new THREE.Group();
        const at = bankPoint(beta, side * valve.camX, valve.camY, 0);
        group.position.set(at[0], at[1], 0);
        const shaft = new THREE.Mesh(geo(built.shaft), materials.steel);
        shaft.matrixAutoUpdate = false;
        shaft.castShadow = true;
        const wheel = new THREE.Mesh(geo(built.sprocket), materials.forged);
        wheel.matrixAutoUpdate = false;
        group.add(shaft, wheel);
        this.root.add(group);
        this.cams.push(group);
      }
    }

    // ── Les chaînes ──
    this.chains = [];
    this.chainZ = [];
    const linkPitch = (layout.sprocket.crankRadius * 2 * Math.PI) / layout.sprocket.crankTeeth;
    let links = 0;
    for (let bank = 0; bank < layout.banks; bank++) {
      const path = chainPath(chainCircles(layout, bank), linkPitch);
      this.chains.push(path);
      this.chainZ.push(layout.sprocket.z[bank] ?? layout.sprocket.z[0]);
      links += path.links;
    }
    const chain = instanced(geo(buildChainLink(layout, this.chains[0].pitch)), materials.steel, links, false);

    // ── Le gaz et le feu ──
    const unitColumn = new MeshBuilder().lathe([[0, 0], [1, 0], [1, 1], [0, 1]], 24);
    const gasMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.42, depthWrite: false, toneMapped: false });
    const gas = instanced(geo(unitColumn), gasMaterial, count, false);
    const dome: P2[] = [];
    for (let k = 0; k <= 8; k++) {
      const a = (k / 8) * (Math.PI / 2);
      dome.push([Math.sin(a), -Math.cos(a)]);
    }
    const flame = instanced(geo(new MeshBuilder().lathe(dome, 18, 80)), glowMaterial(), count, false);
    // L'étincelle : deux éclats croisés, et le point chaud entre les électrodes.
    const sparkShape = new MeshBuilder();
    sparkShape.sphere(0.45, 8, 6);
    for (const angle of [0, Math.PI / 2, Math.PI / 4, -Math.PI / 4]) {
      sparkShape.transform(rz(angle)).extrude([[-1.6, 0], [0, -0.16], [1.6, 0], [0, 0.16]], 0.05, 5);
    }
    const spark = instanced(geo(sparkShape), glowMaterial(), count, false);
    const diesel = layout.fuel === 'diesel';
    // Le carburant : six trous pour un injecteur diesel central, un jet pour l'injection directe d'essence.
    const sprayShape = new MeshBuilder();
    if (diesel) {
      for (let k = 0; k < 6; k++) {
        const a = (k / 6) * Math.PI * 2;
        const length = bore * 0.4;
        sprayShape.tube([[0, 0, 0], [Math.cos(a) * length * 0.55, -length * 0.3, Math.sin(a) * length * 0.55], [Math.cos(a) * length, -length * 0.5, Math.sin(a) * length]], [bore * 0.004, bore * 0.018, bore * 0.04], 6);
      }
    } else {
      const length = bore * 0.62;
      sprayShape.tube([[0, 0, 0], [-length * 0.5, -length * 0.3, 0], [-length, -length * 0.6, 0]], [bore * 0.006, bore * 0.05, bore * 0.1], 8);
    }
    const spray = instanced(geo(sprayShape), glowMaterial(), count, false);
    // Les jets aux soupapes : un cône qui descend, à l'échelle du débit.
    const jetShape = new MeshBuilder().lathe([[0, -1], [0.55, -0.15], [0.42, 0]], 10, 80);
    const jets = instanced(geo(jetShape), glowMaterial(), valves, false);

    // ── Les conduits : une fourche par cylindre, vers le côté de la culasse ──
    const portsIn = instanced(geo(this.buildPort(valve.radiusIn * 0.68)), stage.surface({ metalness: 0.15, roughness: 0.55 }), count, false);
    const portsEx = instanced(geo(this.buildPort(valve.radiusEx * 0.68)), stage.surface({ metalness: 0.15, roughness: 0.55 }), count, false);

    // ── Ce qui reste à sa place ──
    const heads = new MeshBuilder();
    const covers = new MeshBuilder();
    const dense = new MeshBuilder();
    const castings = new MeshBuilder();
    const alloys = new MeshBuilder();
    const blacks = new MeshBuilder();
    const steels = new MeshBuilder();
    const brasses = new MeshBuilder();
    const porcelain = new MeshBuilder();
    const headers = new MeshBuilder();
    const glowBase = new MeshBuilder();

    const blockFront = layout.frontZ + bore * 0.06;
    const blockRear = layout.rearZ - bore * 0.04;
    const blockLength = blockFront - blockRear;
    const blockZ = (blockFront + blockRear) / 2;
    const swing = Math.max(layout.crank * 1.25 + bore * 0.1, layout.mainRadius * 1.6);
    const blockHalf = bore * 0.66;
    const fp = [0, 0];
    const fromBank = (beta: number, x: number, y: number): P2 => {
      bankToFront(fp, beta, x, y);
      return [fp[0], fp[1]];
    };
    let outline: P2[];
    let panWidth: number;
    let panTop: number;
    if (layout.banks === 1) {
      const flange = Math.max(swing + bore * 0.05, blockHalf);
      panWidth = flange;
      panTop = -swing * 0.7;
      outline = [[-flange, panTop], [flange, panTop], [flange, deck * 0.08], [blockHalf, deck * 0.42], [blockHalf, deck], [-blockHalf, deck], [-blockHalf, deck * 0.42], [-flange, deck * 0.08]];
    } else if (layout.flat) {
      const cc = Math.max(swing, blockHalf);
      panWidth = cc * 0.8;
      panTop = -cc;
      outline = [[-deck, -blockHalf], [-cc, -blockHalf], [-cc * 0.8, -cc], [cc * 0.8, -cc], [cc, -blockHalf], [deck, -blockHalf], [deck, blockHalf], [cc, blockHalf], [cc * 0.8, cc], [-cc * 0.8, cc], [-cc, blockHalf], [-deck, blockHalf]];
    } else {
      const flange = Math.max(swing + bore * 0.05, blockHalf);
      panWidth = flange;
      panTop = -swing * 0.72;
      const half = layout.halfDeg;
      const outerR = fromBank(half, blockHalf, deck);
      const innerR = fromBank(half, -blockHalf, deck);
      const innerL = fromBank(-half, blockHalf, deck);
      const outerL = fromBank(-half, -blockHalf, deck);
      const valleyY = fromBank(half, -blockHalf, blockHalf / Math.tan(half * DEG))[1];
      outline = [[-flange, panTop], [flange, panTop], [flange, swing * 0.3], outerR, innerR, [0, valleyY], innerL, outerL, [-flange, swing * 0.3]];
    }
    heads.transform(tr(0, 0, blockZ)).extrude(outline, blockLength, 8);

    // Le carter d'huile, et l'huile dedans.
    const panDepth = Math.max(layout.crank * 1.25 + bore * 0.55, bore * 1.1);
    const panOutline: P2[] = [[-panWidth, panTop], [-panWidth, panTop - bore * 0.2], [-panWidth * 0.86, panTop - panDepth], [panWidth * 0.86, panTop - panDepth], [panWidth, panTop - bore * 0.2], [panWidth, panTop]];
    panOutline.reverse();
    if (!layout.flat) covers.transform(tr(0, 0, blockZ)).extrude(panOutline, blockLength, 8);
    else covers.transform(tr(0, 0, blockZ)).extrude([[-panWidth, panTop], [panWidth, panTop], [panWidth * 0.86, panTop - bore * 0.55], [-panWidth * 0.86, panTop - bore * 0.55]].reverse() as P2[], blockLength, 8);
    const oilTop = layout.flat ? panTop - bore * 0.3 : panTop - panDepth * 0.62;
    const oilBottom = layout.flat ? panTop - bore * 0.54 : panTop - panDepth + bore * 0.01;

    // Paliers : chapeau et vis, autour de chaque tourillon.
    for (const main of layout.mains) {
      const cap = bore * 0.1 + layout.mainRadius;
      castings.transform(tr(0, 0, main.z)).extrudeRing(circle(cap, 20, Math.PI, Math.PI * 2).concat([[cap * 1.25, 0], [-cap * 1.25, 0]]), circle(layout.mainRadius * 1.04, 20, Math.PI, Math.PI * 2).concat([[layout.mainRadius * 1.04, 0.01], [-layout.mainRadius * 1.04, 0.01]]), main.width * 0.86, 30);
      for (const side of [-1, 1]) {
        steels.transform(tr(side * cap * 1.02, -cap * 0.72, main.z)).cylinder(bore * 0.042, bore * 0.11, 6);
      }
    }

    const halfDeg = layout.halfDeg;
    const plugTipY = deck + valve.roofRise - bore * 0.03;
    this.chamberTop = deck + valve.roofRise * 0.55;
    this.sparkAt = new Float64Array(count * 3);
    let yMax = deck;
    let xMax = bore;
    const extent = (p: P3) => {
      yMax = Math.max(yMax, p[1]);
      xMax = Math.max(xMax, Math.abs(p[0]));
    };

    for (let bank = 0; bank < layout.banks; bank++) {
      const beta = layout.banks === 1 ? 0 : bank === 0 ? -halfDeg : halfDeg;
      const cylinders = layout.byBank[bank];
      const zFrontBank = cylinders[0].z + bore * 0.62;
      const zRearBank = cylinders[cylinders.length - 1].z - bore * 0.62;
      const length = zFrontBank - zRearBank;
      const zMid = (zFrontBank + zRearBank) / 2;
      const base = bankM(beta);
      const W = bore * 1.04;
      const headTop = valve.headTop;
      // La culasse, puis le couvre-culasse, jusqu'au-dessus des cames.
      heads.transform(chainM(base, tr(0, deck, zMid))).extrude([[-W, 0], [W, 0], [W, bore * 0.85], [W * 0.92, headTop - deck], [-W * 0.92, headTop - deck], [-W, bore * 0.85]], length, 8);
      const coverW = valve.coverW;
      const coverBottom = headTop - deck;
      const coverTop = valve.coverTop - deck;
      covers.transform(chainM(base, tr(0, deck, zMid))).extrude([[-coverW, coverBottom], [coverW, coverBottom], [coverW, coverTop - bore * 0.14], [coverW - bore * 0.18, coverTop], [-coverW + bore * 0.18, coverTop], [-coverW, coverTop - bore * 0.14]], length, 8);
      if (valve.rocker) {
        // Les deux axes de culbuteurs, de bout en bout de la culasse.
        for (const axle of [-1, 1]) {
          steels.transform(chainM(base, onZ(axle * valve.rocker.pivotX, valve.rocker.pivotY, zMid))).cylinder(bore * 0.04, length - bore * 0.1, 12);
        }
      }

      for (const cylinder of cylinders) {
        const z = cylinder.z;
        const side = cylinder.intakeSide;
        const i = cylinder.index;
        // La chemise, tube ouvert de la culasse au carter. Son haut est soulevé en un collet bien lisible.
        const liner = deck - layout.stroke - layout.pistonTop - layout.pistonBottom - bore * 0.02;
        heads.transform(chainM(base, tr(0, 0, z))).lathe([[bore / 2 + bore * 0.015, Math.max(liner, swing * 0.8)], [bore / 2 + bore * 0.015, deck]], 32);
        // Le toit de la chambre, en bâtière.
        const roofOutline: P2[] = [[-bore / 2, 0], [bore / 2, 0], [0, valve.roofRise]];
        dense.transform(chainM(base, tr(0, deck, z))).extrude(roofOutline, bore * 0.94, 5);

        const point = bankPoint(beta, 0, plugTipY, z + valve.plugZ);
        this.sparkAt[i * 3] = point[0];
        this.sparkAt[i * 3 + 1] = point[1];
        this.sparkAt[i * 3 + 2] = point[2];
        const local = chainM(base, tr(0, 0, z));
        // Sur un simple arbre, l'arbre passe au milieu : bougie ou injecteur entrent à côté de lui, penchés vers l'échappement.
        const plugFrame = chainM(local, tr(0, plugTipY, valve.plugZ), rz(side * valve.plugTiltDeg * DEG));

        if (diesel) {
          // Injecteur au centre, sa rampe commune, et la bougie de préchauffage à côté de lui, inclinée.
          steels.transform(chainM(plugFrame, tr(0, bore * 0.02, 0))).lathe([
            [0, -bore * 0.04], [bore * 0.03, -bore * 0.03], [bore * 0.045, bore * 0.2], [bore * 0.075, bore * 0.5], [bore * 0.075, bore * 1.2], [bore * 0.05, bore * 1.26], [0, bore * 1.26],
          ], 12);
          blacks.transform(chainM(plugFrame, tr(0, bore * 1.34, 0))).box(bore * 0.12, bore * 0.16, bore * 0.2);
          const glow = valve.sohc
            ? chainM(local, tr(side * bore * 0.12, plugTipY, valve.plugZ), rz(-side * valve.plugTiltDeg * DEG))
            : chainM(local, tr(0, plugTipY, bore * 0.18), rz(-side * 0.0), rx(0.26));
          glowBase.transform(glow).lathe([[bore * 0.018, bore * 0.04], [bore * 0.038, bore * 0.5], [bore * 0.038, bore * 0.92], [bore * 0.06, bore * 0.92], [bore * 0.06, bore * 1.02], [0, bore * 1.02]], 10);
          blacks.transform(chainM(glow, tr(0, bore * 1.09, 0))).cylinder(bore * 0.035, bore * 0.12, 8);
        } else {
          // Bougie : électrode, culot fileté, six-pans, isolant, et la bobine crayon par-dessus.
          steels.transform(plugFrame).lathe([
            [bore * 0.03, bore * 0.04], [bore * 0.075, bore * 0.04], [bore * 0.075, bore * 0.27], [bore * 0.105, bore * 0.27], [bore * 0.105, bore * 0.37], [bore * 0.06, bore * 0.37],
          ], 6, 5);
          brasses.transform(plugFrame).lathe([[0, -bore * 0.012], [bore * 0.012, -bore * 0.012], [bore * 0.012, bore * 0.06], [0, bore * 0.06]], 6);
          brasses.transform(chainM(plugFrame, tr(bore * 0.026, bore * 0.01, 0))).box(bore * 0.012, bore * 0.06, bore * 0.018);
          porcelain.transform(plugFrame).lathe([[bore * 0.02, bore * 0.05], [bore * 0.055, bore * 0.37], [bore * 0.05, bore * 0.72], [bore * 0.028, bore * 0.72]], 10);
          blacks.transform(plugFrame).lathe([[bore * 0.058, bore * 0.5], [bore * 0.08, bore * 0.74], [bore * 0.08, bore * 1.3], [bore * 0.11, bore * 1.32], [bore * 0.11, bore * 1.56], [0, bore * 1.56]], 12);
          blacks.transform(chainM(plugFrame, tr(side * bore * 0.12, bore * 1.46, 0))).box(bore * 0.2, bore * 0.14, bore * 0.2);
          // L'injecteur d'essence, sous le conduit d'admission, plonge dans la chambre.
          const injector = chainM(local, tr(side * bore * 0.42, deck + bore * 0.06, 0), rz(-side * 1.05));
          steels.transform(chainM(injector, tr(0, 0, 0))).lathe([[bore * 0.015, 0], [bore * 0.03, bore * 0.08], [bore * 0.04, bore * 0.48], [0, bore * 0.5]], 8);
          blacks.transform(chainM(injector, tr(0, bore * 0.56, 0))).cylinder(bore * 0.05, bore * 0.14, 8);
        }

        // Admission, vers le plénum ; échappement, vers son collecteur.
        const portY = deck + bore * 0.62;
        extent(bankPoint(beta, side * W, portY, z));
        extent(bankPoint(beta, -side * W, portY, z));
      }
    }

    // ── L'admission : un plénum, un conduit par cylindre, le papillon devant ──
    const runnerR = bore * 0.15;
    const portY = deck + bore * 0.62;
    const W = bore * 1.04;
    const plenum = plenumPosition(layout);
    const plenumX = plenum.x;
    const plenumY = plenum.y;
    const plenumR = plenum.radius;
    const zFrontAll = Math.max(...layout.cylinders.map((c) => c.z)) + bore * 0.5;
    const zRearAll = Math.min(...layout.cylinders.map((c) => c.z)) - bore * 0.5;
    const butterflies: T.Group[] = [];
    const carb = layout.carb;
    // Des cornets sur un moteur sans suralimentation ni carburateur : plus de plénum, un tube et un papillon par cylindre.
    const openStacks = hasOpenStacks(layout);
    const throttleZ = zFrontAll + bore * 0.5;
    const throttleR = bore * 0.2;
    // L'élément filtrant, là où il se voit : le coton huilé d'un cône, le cadre d'un panneau.
    const elements = new MeshBuilder();
    // Le volet d'un corps, au bout de son arbre : `tilted`, son disque regarde l'axe X ; sinon, l'axe Y.
    const carbFlap = (x: number, y: number, z: number, radius: number, tilted: boolean) => {
      const pivot = new THREE.Group();
      pivot.position.set(x, y, z);
      const disc = new THREE.Mesh(geo(new MeshBuilder().cylinder(radius, bore * 0.012, 18)), materials.steel);
      if (tilted) disc.rotation.z = Math.PI / 2;
      disc.updateMatrix();
      disc.matrixAutoUpdate = false;
      pivot.add(disc);
      this.root.add(pivot);
      butterflies.push(pivot);
    };
    // Un corps de carburateur le long de Y : son col, ses parois, et le cornet. Ouvert en haut : le venturi se voit.
    const carbBody = (matrix: Mat34, throat: number, height: number) => {
      const outer = throat * 1.45;
      alloys.transform(matrix).lathe([
        [outer, 0],
        [outer, height * 0.62],
        [outer * 1.02, height * 0.66],
        [outer * 1.3, height],
        [outer * 1.18, height],
        [throat * 1.2, height * 0.68],
        [throat, height * 0.42],
        [throat * 1.18, height * 0.1],
        [throat * 1.18, 0],
      ], 22, 50);
    };
    if (openStacks) {
      const flapMesh = geo(new MeshBuilder().cylinder(runnerR * 0.94, bore * 0.012, 14));
      for (const cylinder of layout.cylinders) {
        const stack = stackOf(layout, cylinder);
        const path: P3[] = [];
        const radii: number[] = [];
        for (let k = 0; k <= 10; k++) {
          const share = k / 10;
          const flare = Math.max(share - 0.72, 0) / 0.28;
          path.push([stack.from[0] + (stack.to[0] - stack.from[0]) * share, stack.from[1] + (stack.to[1] - stack.from[1]) * share, cylinder.z]);
          radii.push(stack.radius * (1 + 0.85 * flare * flare));
        }
        alloys.transform(null).tube(path, radii, 14);
        extent(path[10]);
        // Le papillon, tout près de la culasse : son axe dans le plan du tube, sa levée autour de lui.
        const axis = new THREE.Group();
        const at = path[3];
        axis.position.set(at[0], at[1], at[2]);
        axis.rotation.order = 'ZYX';
        axis.rotation.set(0, Math.PI / 2, Math.atan2(stack.to[1] - stack.from[1], stack.to[0] - stack.from[0]));
        const lever = new THREE.Group();
        const flat = new THREE.Mesh(flapMesh, materials.steel);
        flat.rotation.x = Math.PI / 2;
        flat.updateMatrix();
        flat.matrixAutoUpdate = false;
        lever.add(flat);
        axis.add(lever);
        this.root.add(axis);
        butterflies.push(lever);
      }
    } else if (carb?.kind === 'multi') {
      // Un corps par cylindre : un conduit court et droit, à la place du plénum. De côté en ligne, debout entre les rangées d'un V.
      const throat = (carb.throatM * 1000) / 2;
      const height = bore * 1.0;
      for (const cylinder of layout.cylinders) {
        const from = bankPoint(cylinder.betaDeg, cylinder.intakeSide * W, portY, cylinder.z);
        const dir = bankDir(cylinder.betaDeg, cylinder.intakeSide, 0.1);
        const sideways = layout.banks === 1;
        const base: P3 = sideways ? [-bore * 1.65, portY, cylinder.z] : [from[0] * (layout.flat ? 1 : 0.72), plenumY - bore * 0.3, cylinder.z];
        const axis: P3 = sideways ? [-1, 0, 0] : [0, 1, 0];
        alloys.transform(null).tube(bendPath(from, dir, base, axis), Math.min(runnerR, throat * 1.2), 9);
        const matrix = sideways ? chainM(tr(base[0], base[1], base[2]), rz(Math.PI / 2)) : tr(base[0], base[1], base[2]);
        carbBody(matrix, throat, height);
        // Des cônes individuels, un par corps. Soufflé, l'air est déjà passé par le filtre du compresseur.
        if (layout.airFilter === 'cone' && !layout.turbo && !layout.charger) {
          const mouth = sideways ? chainM(tr(base[0] - height, base[1], base[2]), rz(Math.PI / 2)) : tr(base[0], base[1] + height, base[2]);
          elements.transform(mouth).lathe(coneProfile(bore * 0.08, throat * 2.1, bore * 0.6, throat * 1.3), 22, 50);
          extent([base[0] + axis[0] * (height + bore * 0.7), base[1] + axis[1] * (height + bore * 0.7), base[2]]);
        }
        carbFlap(base[0] + axis[0] * height * 0.16, base[1] + axis[1] * height * 0.16, base[2], throat * 1.08, sideways);
        extent([base[0] + axis[0] * height, base[1] + axis[1] * height, base[2]]);
        extent([base[0] - throat * 1.9, base[1], base[2]]);
      }
      const first = layout.cylinders[0];
      this.labelSpots.carb = layout.banks === 1 ? [-bore * 2.6, portY + bore * 0.5, first.z] : [plenumX, plenumY + bore * 0.9, first.z];
    } else {
      // Sur une pipe commune, le tube reste, en plus fin : c'est la pipe qui se partage entre les cylindres.
      const logR = carb ? Math.min(plenumR, ((carb.pipeM * 1000) / 2) * 0.9) : plenumR;
      alloys.transform(onZ(plenumX, plenumY, (zFrontAll + zRearAll) / 2)).lathe([[0, -(zFrontAll - zRearAll) / 2 - logR * 0.5], [logR, -(zFrontAll - zRearAll) / 2], [logR, (zFrontAll - zRearAll) / 2], [0, (zFrontAll - zRearAll) / 2 + logR * 0.5]], 18, 60);
      extent([plenumX + plenumR, plenumY + plenumR, 0]);
      for (const cylinder of layout.cylinders) {
        const from = bankPoint(cylinder.betaDeg, cylinder.intakeSide * W, portY, cylinder.z);
        const dir = bankDir(cylinder.betaDeg, cylinder.intakeSide, 0.1);
        const target: P3 = [plenumX, plenumY, cylinder.z];
        alloys.transform(null).tube(bendPath(from, dir, target, layout.banks === 1 ? [-1, 0, 0] : [from[0] > 0 ? -0.2 : 0.2, 1, 0]), carb ? Math.min(runnerR, logR * 0.8) : runnerR, 9);
      }
      if (carb) {
        // Le carburateur, debout au milieu de sa pipe : son col, sa cuve, et son filtre plat autour du cornet.
        const midZ = (zFrontAll + zRearAll) / 2;
        const throat = (carb.throatM * 1000) / 2;
        const height = bore * 1.05;
        const baseY = plenumY + logR * 0.8;
        carbBody(tr(plenumX, baseY, midZ), throat, height);
        carbFlap(plenumX, baseY + height * 0.16, midZ, throat * 1.08, false);
        // La cuve de niveau constant, sur le côté.
        brasses.transform(tr(plenumX, baseY + height * 0.3, midZ + throat * 2.1)).lathe([[0, -height * 0.26], [throat * 0.72, -height * 0.26], [throat * 0.8, -height * 0.18], [throat * 0.8, height * 0.2], [0, height * 0.26]], 16, 50);
        // Le filtre, un anneau : il laisse voir le venturi. Un cône le remplace, des cornets laissent la bouche nue.
        // Soufflé, le carburateur n'en a pas : c'est le compresseur qui a le sien.
        const filterR = Math.max(throat * 3, bore * 0.9);
        const filterY = baseY + height * 1.02;
        if (!layout.turbo && !layout.charger) {
          if (layout.airFilter === 'cone') {
            elements.transform(tr(plenumX, filterY, midZ)).lathe(coneProfile(bore * 0.2, bore * 0.56, bore * 1.5, throat * 1.3), 28, 50);
            extent([plenumX, filterY + bore * 1.8, midZ]);
          } else if (layout.airFilter !== 'stacks') {
            (layout.airFilter === 'panel' ? elements : blacks).transform(tr(plenumX, filterY, midZ)).lathe([[throat * 1.7, 0], [filterR, 0], [filterR, bore * 0.26], [throat * 1.7, bore * 0.26]], 28, 40);
          }
        }
        extent([plenumX + filterR, filterY + bore * 0.26, midZ]);
        extent([plenumX - filterR, filterY, midZ]);
        this.labelSpots.carb = [plenumX, filterY + bore * 0.4, midZ];
      } else {
        // Le corps de papillon : tube court et, dedans, le volet.
        alloys.transform(onZ(plenumX, plenumY, throttleZ)).lathe([[throttleR, -bore * 0.32], [throttleR * 1.2, -bore * 0.2], [throttleR * 1.2, bore * 0.2], [throttleR, bore * 0.32]], 18);
        const butterfly = new THREE.Group();
        butterfly.position.set(plenumX, plenumY, throttleZ);
        const flap = new THREE.Mesh(geo(new MeshBuilder().cylinder(throttleR * 0.96, bore * 0.012, 18)), materials.steel);
        flap.rotation.x = Math.PI / 2;
        flap.updateMatrix();
        flap.matrixAutoUpdate = false;
        butterfly.add(flap);
        this.root.add(butterfly);
        butterflies.push(butterfly);
      }
    }

    // Devant le papillon d'un moteur sans turbo : sa boîte à air, ou son cône à l'air libre.
    if (!layout.turbo && !layout.charger && !carb && !openStacks) {
      const mouthZ = throttleZ + bore * 0.32;
      if (layout.airFilter === 'cone') {
        const radius = bore * 0.56;
        const length = bore * 1.9;
        blacks.transform(onZ(plenumX, plenumY, mouthZ)).lathe([[throttleR * 1.08, 0], [throttleR * 1.08, bore * 0.18], [radius * 0.5, bore * 0.32]], 18);
        elements.transform(onZ(plenumX, plenumY, mouthZ)).lathe(coneProfile(bore * 0.3, radius, length, throttleR), 28, 50);
        for (let k = 1; k < 6; k++) {
          const along = (length * k) / 6;
          const r = coneRadiusAt(along, radius, length);
          steels.transform(onZ(plenumX, plenumY, mouthZ + bore * 0.3 + along)).lathe([[r - bore * 0.012, -bore * 0.012], [r + bore * 0.014, -bore * 0.012], [r + bore * 0.014, bore * 0.012], [r - bore * 0.012, bore * 0.012]], 28);
        }
        steels.transform(onZ(plenumX, plenumY, mouthZ + bore * 0.14)).lathe([[throttleR * 1.1, -bore * 0.04], [throttleR * 1.2, -bore * 0.04], [throttleR * 1.2, bore * 0.04], [throttleR * 1.1, bore * 0.04]], 18);
        extent([plenumX + radius, plenumY + radius, 0]);
      } else {
        const hx = bore * 0.74;
        const hy = bore * 0.34;
        const hz = bore * 0.46;
        const boxZ = mouthZ + bore * 0.4 + hz;
        blacks.transform(onZ(plenumX, plenumY, mouthZ + bore * 0.2)).cylinder(throttleR * 1.05, bore * 0.46, 16);
        blacks.transform(tr(plenumX, plenumY, boxZ)).box(2 * hx, 2 * hy, 2 * hz);
        // Le joint entre les deux demi-boîtes : un filet noir sur le papier, le cadre de l'élément sur un panneau sport.
        (layout.airFilter === 'panel' ? elements : blacks).transform(tr(plenumX, plenumY, boxZ)).box(2 * hx * 1.05, bore * 0.05, 2 * hz * 1.05);
        // La prise d'air : elle va chercher l'air dehors.
        blacks.transform(onZ(plenumX + hx * 0.55, plenumY, boxZ + hz + bore * 0.26)).cylinder(bore * 0.13, bore * 0.52, 14);
        extent([plenumX + hx, plenumY + hy, 0]);
      }
    }

    // ── L'échappement : un conduit par cylindre, un collecteur par rangée, puis la turbine ──
    for (let bank = 0; bank < layout.banks; bank++) {
      const cylinders = layout.byBank[bank];
      const beta = layout.banks === 1 ? 0 : bank === 0 ? -halfDeg : halfDeg;
      const side = -cylinders[0].intakeSide;
      const collector = collectorPosition(layout, bank);
      const collectorPoint: P3 = [collector.x, collector.y, 0];
      const zRear = cylinders[cylinders.length - 1].z - bore * 0.45;
      const zFront = cylinders[0].z + bore * 0.2;
      for (const cylinder of cylinders) {
        const from = bankPoint(beta, side * W, portY, cylinder.z);
        const dir = bankDir(beta, side, 0.1);
        const to: P3 = [collectorPoint[0], collectorPoint[1], cylinder.z - bore * 0.3];
        headers.transform(null).tube(bendPath(from, dir, to, [0, -0.3, -1]), bore * 0.13, 10);
      }
      headers.transform(onZ(collectorPoint[0], collectorPoint[1], (zFront + zRear) / 2)).lathe([[0, -(zFront - zRear) / 2], [bore * 0.17, -(zFront - zRear) / 2], [bore * 0.17, (zFront - zRear) / 2], [0, (zFront - zRear) / 2]], 12);
      extent(collectorPoint);
      if (!layout.turbo) headers.transform(null).tube(bendPath([collectorPoint[0], collectorPoint[1], zRear + bore * 0.05], [0, 0, -1], [collectorPoint[0] * 0.95, Math.min(collectorPoint[1], 0) - bore * 1.2, zRear - bore * 1.2], [0, -0.1, -1]), bore * 0.17, 10);
    }

    // Un turbo par rangée, et tout le chemin de l'air : voir `turboChain`. Volute et carter de compresseur en verre,
    // l'arbre et ses deux roues qui tournent, l'échangeur devant le moteur, le tube froid jusqu'au papillon.
    const airChain = layout.turbo ? turboChain(layout) : null;
    const hotPipes = new MeshBuilder();
    const coldPipes = new MeshBuilder();
    if (airChain) {
      const s = layout.turboScale;
      for (const unit of airChain.units) {
        const R = unit.turbineHousing;
        const Rc = unit.compressorHousing;
        // La volute de la turbine, et celle du compresseur : carters à section qui s'enroule, sans autre détail.
        const volute: P2[] = [[bore * 0.12, -bore * 0.22], [R * 0.86, -bore * 0.2], [R, -bore * 0.04], [R * 0.92, bore * 0.16], [bore * 0.18, bore * 0.22]];
        headers.transform(onZ(unit.x, unit.y, unit.z)).lathe(volute, 22, 60);
        covers.transform(onZ(unit.x, unit.y, unit.compressorZ)).lathe([[bore * 0.14, -bore * 0.3], [Rc * 0.95, -bore * 0.14], [Rc, bore * 0.04], [Rc * 0.7, bore * 0.2], [bore * 0.22, bore * 0.26]], 22, 60);
        castings.transform(onZ(unit.x, unit.y, unit.z + bore * 0.45)).lathe([[bore * 0.17, -bore * 0.22], [bore * 0.26, -bore * 0.18], [bore * 0.26, bore * 0.18], [bore * 0.17, bore * 0.22]], 14);
        // Sortie de la turbine, vers l'arrière de la voiture.
        headers.transform(null).tube(bendPath([unit.x, unit.y, unit.z - bore * 0.2], [0, 0, -1], [unit.x, unit.y - bore * 0.9, unit.z - bore * 1.5], [0, -0.1, -1]), bore * 0.17, 10);
        extent([unit.x + R, unit.y, 0]);

        // La boîte à air : un bloc à arêtes, sa cartouche, et le conduit souple jusqu'à l'œil du compresseur.
        const [ax, ay, az] = unit.airbox;
        const [ahx, ahy, ahz] = unit.airboxSize;
        // Selon le filtre : la boîte, le cône à l'air libre, ou une simple bouche arrondie sur le conduit du compresseur.
        if (layout.airFilter === 'cone') {
          elements.transform(tr(ax, ay - ahy, az - ahz * 0.3)).lathe(coneProfile(bore * 0.1, bore * 0.46, 2 * ahy + bore * 0.7, bore * 0.13), 26, 50);
        } else if (layout.airFilter === 'stacks') {
          alloys.transform(tr(ax, ay - ahy, az - ahz * 0.3)).lathe([[bore * 0.13, -bore * 0.05], [bore * 0.15, bore * 0.2], [bore * 0.22, bore * 0.34], [bore * 0.36, bore * 0.42]], 22, 60);
        } else {
          blacks.transform(tr(ax, ay, az)).box(2 * ahx, 2 * ahy, 2 * ahz);
          (layout.airFilter === 'panel' ? elements : blacks).transform(tr(ax, ay + ahy + bore * 0.03, az)).box(2 * ahx * 1.05, bore * 0.05, 2 * ahz * 1.05);
        }
        const eye: P3 = [unit.x, unit.y, unit.compressorZ + bore * 0.3];
        const duct = bendPath([ax, ay - ahy, az - ahz * 0.3], [0, -1, 0], eye, [0, 0, -1], 14);
        blacks.transform(null).tube(duct, bore * 0.13, 10);
        // Le débitmètre, à la sortie de la boîte : un boîtier sur le conduit. Une bouche nue n'en a pas.
        const at = duct[Math.floor(duct.length * 0.2)];
        if (layout.airFilter !== 'stacks') {
          steels.transform(tr(at[0], at[1], at[2])).box(bore * 0.34, bore * 0.2, bore * 0.26);
          this.labelSpots.maf ??= at;
        }

        // Le tube chaud : de la volute à l'échangeur, teinté par l'air qui y passe.
        hotPipes.transform(null).tube(unit.hotRun.length > 0 ? unit.hotRun : bendPath(unit.hotFrom, [unit.side * 0.2, -1, 0], unit.hotTo, [-unit.side, 0, 0], 14), bore * 0.13, 10);
        // Déporté, le tube d'échappement jusqu'à la volute : il rougit avec le reste du collecteur.
        if (unit.feedRun.length > 0) headers.transform(null).tube(unit.feedRun, bore * 0.17, 10);

        // La wastegate : l'actionneur en dehors du carter, sa tige qui pousse un levier selon l'ouverture réelle.
        const [wx, wy, wz] = unit.actuator;
        castings.transform(onZ(wx, wy, wz)).lathe([[0, -bore * 0.12], [bore * 0.15, -bore * 0.12], [bore * 0.15, bore * 0.1], [0, bore * 0.1]], 14);
        const rod = new MeshBuilder();
        rod.transform(onZ(0, 0, 0)).cylinder(bore * 0.018, bore * 0.5, 6);
        rod.transform(tr(0, 0, -bore * 0.28)).box(bore * 0.1, bore * 0.04, bore * 0.05);
        const rodMesh = new THREE.Mesh(geo(rod), materials.steel);
        rodMesh.position.set(wx, wy, wz - bore * 0.32);
        rodMesh.matrixAutoUpdate = true;
        this.root.add(rodMesh);
        this.wastegateRods.push({ mesh: rodMesh, rest: wz - bore * 0.32, travel: bore * 0.14 });

        // L'arbre et ses deux roues, un seul bloc qui tourne.
        const shaft = new MeshBuilder();
        shaft.transform(onZ(0, 0, bore * 0.45)).cylinder(bore * 0.035, bore * 1.15, 8);
        const wheel = (z: number, radius: number, compressor: boolean) => {
          shaft.transform(onZ(0, 0, z)).lathe([[0, -bore * 0.14], [bore * 0.05, -bore * 0.14], [radius * 0.55, bore * 0.02], [radius * 0.55, bore * 0.09], [0, bore * 0.09]], 12);
          const blades = compressor ? 11 : 9;
          for (let k = 0; k < blades; k++) {
            const a = (k / blades) * Math.PI * 2;
            const blade: P2[] = [[bore * 0.06, -bore * 0.13], [radius, bore * 0.02], [radius, bore * 0.085], [bore * 0.06, bore * 0.08]];
            shaft.transform(chainM(tr(0, 0, z), rz(a), rx(Math.PI / 2), ry(0.55))).extrude(blade, bore * 0.01, 5);
          }
        };
        wheel(0, unit.turbineWheel, false);
        wheel(unit.compressorZ - unit.z, unit.compressorWheel, true);
        const mesh = new THREE.Mesh(geo(shaft), materials.steel);
        mesh.position.set(unit.x, unit.y, unit.z);
        this.root.add(mesh);
        this.turboWheels.push(mesh);
      }

      // L'échangeur : boîtes d'extrémité, faisceau et ailettes, devant le moteur.
      const [cx, cy, cz] = airChain.cooler;
      const [chx, chy, chz] = airChain.coolerSize;
      const core = new MeshBuilder();
      core.transform(tr(cx, cy, cz)).box(2 * chx * 0.86, 2 * chy, 2 * chz * 0.75);
      const fins = Math.round(16 * s);
      for (let k = 0; k < fins; k++) {
        const fx = cx - chx * 0.86 + (2 * chx * 0.86 * (k + 0.5)) / fins;
        core.transform(tr(fx, cy, cz + chz * 0.78)).box(bore * 0.018, 2 * chy * 0.96, bore * 0.02);
      }
      for (const end of [-1, 1]) {
        alloys.transform(tr(cx + end * chx * 0.93, cy, cz)).box(2 * chx * 0.14, 2 * chy * 1.06, 2 * chz * 1.05);
      }
      alloys.transform(null);
      this.coolerGeometry = geo(core);

      // Le tube froid : de l'échangeur au papillon. La soupape de décharge est à mi-chemin, là où un vrai moteur la monte.
      const coldPath = bendPath(airChain.coldFrom, airChain.coldFromDir, airChain.coldTo, [0, 0, -1], 18);
      coldPipes.transform(null).tube(coldPath, bore * 0.13, 10);
      const valveAt = coldPath[Math.floor(coldPath.length * 0.62)];
      if (layout.releaseValve) {
        const body = new MeshBuilder();
        castings.transform(onZ(valveAt[0], valveAt[1], valveAt[2] + bore * 0.26)).lathe([[bore * 0.02, -bore * 0.16], [bore * 0.15, -bore * 0.12], [bore * 0.15, bore * 0.14], [0, bore * 0.16]], 14);
        body.transform(onZ(0, 0, 0)).cylinder(bore * 0.1, bore * 0.04, 12);
        const piston = new THREE.Mesh(geo(body), materials.steel);
        const rest = valveAt[2] + bore * 0.4;
        piston.position.set(valveAt[0], valveAt[1], rest);
        this.root.add(piston);
        this.blowoffPistons.push({ mesh: piston, rest, travel: bore * 0.14 });
      }
      this.labelSpots.blowoff = valveAt;
      this.labelSpots.coldPipe = coldPath[Math.floor(coldPath.length * 0.35)];
      extent([airChain.cooler[0] + chx, airChain.cooler[1], 0]);
    } else {
      void hotPipes;
    }

    // ── Le démarreur, contre la couronne ──
    const { pinion, ring } = layout;
    const starterFront = ring.z + bore * 1.75;
    const starterR = bore * 0.4;
    castings.transform(onZ(pinion.x * (1 + starterR * 0.05 / ring.radius), pinion.y, (ring.z + bore * 0.34 + starterFront) / 2)).lathe([[0, -(starterFront - ring.z - bore * 0.34) / 2], [starterR * 0.8, -(starterFront - ring.z - bore * 0.34) / 2], [starterR, -(starterFront - ring.z - bore * 0.34) / 2 + bore * 0.06], [starterR, (starterFront - ring.z - bore * 0.34) / 2 - bore * 0.06], [starterR * 0.8, (starterFront - ring.z - bore * 0.34) / 2], [0, (starterFront - ring.z - bore * 0.34) / 2]], 18);
    castings.transform(onZ(pinion.x - starterR * 1.05, pinion.y + starterR * 0.9, ring.z + bore * 0.75)).cylinder(starterR * 0.42, bore * 0.72, 12);
    steels.transform(onZ(pinion.x, pinion.y, ring.z + bore * 0.26)).cylinder(pinion.radius * 0.55, bore * 0.32, 8);
    const pinionGeometry = geo(new MeshBuilder().gear(pinion.teeth, pinion.radius - (ring.radius * 2) / ring.teeth * 0.6, pinion.radius + (ring.radius * 2) / ring.teeth * 0.6, ring.width * 1.1, pinion.radius * 0.3));
    const pinionMesh = new THREE.Mesh(pinionGeometry, materials.steel);
    pinionMesh.matrixAutoUpdate = false;
    this.root.add(pinionMesh);

    // ── L'alternateur et la pompe à eau, sur la courroie ──
    const pulleyCenter = pulleyR;
    const altX = (layout.banks === 2 ? -1 : -1) * bore * (layout.flat ? 0.9 : 1.25);
    const altY = layout.flat ? bore * 1.45 : layout.crank + layout.rod * 0.55;
    const pumpX = bore * (layout.flat ? 1.0 : 1.1);
    const pumpY = layout.flat ? bore * 1.1 : layout.crank + layout.rod * 0.32;
    const altR = bore * 0.2;
    const pumpR = bore * 0.36;
    const beltZ = layout.pulleyZ;
    const beltPath = chainPath([[0, 0, pulleyCenter], [altX, altY, altR], [pumpX, pumpY, pumpR]], bore * 0.2);
    const beltOuter: P2[] = [];
    const beltInner: P2[] = [];
    const stepBelt = 2;
    for (let k = beltPath.x.length - 1; k >= 0; k -= stepBelt) {
      const nx = Math.sin(beltPath.angle[k]);
      const ny = -Math.cos(beltPath.angle[k]);
      // Le tour va dans le sens horaire : la normale extérieure est à gauche, on le parcourt à l'envers.
      beltOuter.push([beltPath.x[k] - nx * bore * 0.012, beltPath.y[k] - ny * bore * 0.012]);
      beltInner.push([beltPath.x[k] + nx * bore * 0.02, beltPath.y[k] + ny * bore * 0.02]);
    }
    blacks.transform(tr(0, 0, beltZ)).extrudeRing(beltOuter, beltInner, bore * 0.19, 35);
    // Alternateur : corps ailetté derrière sa poulie.
    const altBodyZ = beltZ - bore * 0.42;
    const altBodyR = bore * 0.38;
    castings.transform(onZ(altX, altY, altBodyZ)).lathe([[0, -bore * 0.3], [altBodyR * 0.9, -bore * 0.3], [altBodyR, -bore * 0.24], [altBodyR, bore * 0.24], [altBodyR * 0.8, bore * 0.3], [0, bore * 0.3]], 20);
    castings.transform(onZ(pumpX, pumpY, beltZ - bore * 0.24)).cylinder(bore * 0.2, bore * 0.32, 12);
    const accessoryShape = new MeshBuilder();
    accessoryShape.transform(onZ(0, 0, 0)).lathe([[0, -bore * 0.1], [0.75, -bore * 0.1], [0.75, -bore * 0.09], [1, -bore * 0.095], [1, bore * 0.095], [0.75, bore * 0.09], [0, bore * 0.09]], 20, 40);
    // Les rayons de la poulie : ils montrent la rotation.
    for (let k = 0; k < 3; k++) accessoryShape.transform(chainM(rz((k / 3) * Math.PI * 2), tr(0.42, 0, bore * 0.095))).box(0.6, 0.18, bore * 0.02);
    accessoryShape.transform(null);
    const accessory = instanced(geo(accessoryShape), materials.forged, 2, false);
    this.accessoryAt.push({ x: altX, y: altY, z: beltZ, ratio: pulleyCenter / altR, scale: altR }, { x: pumpX, y: pumpY, z: beltZ, ratio: pulleyCenter / pumpR, scale: pumpR });

    // ── Le compresseur mécanique, sur l'admission, et sa courroie ──
    const charger = layout.charger ? chargerUnit(layout, layout.charger) : null;
    if (charger) {
      const addSpinning = (builder: MeshBuilder, x: number, y: number, z: number, ratio: number) => {
        const mesh = new THREE.Mesh(geo(builder), materials.steel);
        mesh.position.set(x, y, z);
        this.root.add(mesh);
        this.chargerParts.push({ mesh, ratio });
      };
      if (charger.volumetric) {
        // Le carter en verre, pour voir les rotors, et ses paliers en bout.
        covers.transform(tr(charger.x, charger.y, charger.z)).box(2 * charger.halfX, 2 * charger.halfY, 2 * charger.halfZ);
        alloys.transform(tr(charger.x, charger.y, charger.z + charger.halfZ + bore * 0.05)).box(2 * charger.halfX * 0.92, 2 * charger.halfY * 0.92, bore * 0.1);
        alloys.transform(tr(charger.x, charger.y, charger.z - charger.halfZ - bore * 0.04)).box(2 * charger.halfX * 0.92, 2 * charger.halfY * 0.92, bore * 0.08);
        // Les deux rotors : des tranches tournées, pour l'hélice.
        const rotor = (lobes: number, sign: number) => {
          const built = new MeshBuilder();
          const outline = lobedOutline(lobes, charger.rotorRadius * (lobes === 3 ? 1 : 0.92));
          const slices = 12;
          const length = 2 * charger.halfZ * 0.96;
          for (let k = 0; k < slices; k++) {
            built.transform(chainM(tr(0, 0, -length / 2 + ((k + 0.5) * length) / slices), rz(sign * charger.twist * ((k + 0.5) / slices)))).extrude(outline, length / slices + 0.2, 25);
          }
          built.transform(onZ(0, 0, 0)).cylinder(bore * 0.04, length + bore * 0.3, 8);
          return built;
        };
        addSpinning(rotor(charger.lobesA, 1), charger.x - charger.rotorSpacing, charger.y, charger.z, 1);
        addSpinning(rotor(charger.lobesB, -1), charger.x + charger.rotorSpacing, charger.y, charger.z, -charger.lobesA / charger.lobesB);
        // L'arbre, jusqu'à sa poulie devant le moteur.
        const shaft = new MeshBuilder();
        shaft.transform(onZ(0, 0, (charger.beltZ - charger.z - charger.halfZ) / 2)).cylinder(bore * 0.05, charger.beltZ - charger.z - charger.halfZ, 10);
        addSpinning(shaft, charger.pulleyX, charger.pulleyY, charger.z + charger.halfZ, 1);
        // L'air comprimé descend directement dans le collecteur.
        alloys.transform(tr(charger.x, (charger.y - charger.halfY + plenumY) / 2, charger.z)).box(2 * charger.halfX * 0.6, Math.max(charger.y - charger.halfY - plenumY, bore * 0.1), 2 * charger.halfZ * 0.7);
      } else {
        // La volute en verre, et la roue qui suit l'arbre.
        const R = charger.halfX;
        covers.transform(onZ(charger.x, charger.y, charger.z)).lathe([[bore * 0.18, -bore * 0.3], [R * 0.95, -bore * 0.14], [R, bore * 0.04], [R * 0.7, bore * 0.2], [bore * 0.22, bore * 0.26]], 24, 60);
        const wheel = new MeshBuilder();
        const r = charger.wheelRadius;
        wheel.transform(onZ(0, 0, 0)).lathe([[0, -bore * 0.14], [bore * 0.06, -bore * 0.14], [r * 0.55, bore * 0.02], [r * 0.55, bore * 0.09], [0, bore * 0.09]], 14);
        for (let k = 0; k < 11; k++) {
          const a = (k / 11) * Math.PI * 2;
          wheel.transform(chainM(tr(0, 0, 0), rz(a), rx(Math.PI / 2), ry(0.55))).extrude([[bore * 0.06, -bore * 0.13], [r, bore * 0.02], [r, bore * 0.085], [bore * 0.06, bore * 0.08]], bore * 0.012, 5);
        }
        wheel.transform(onZ(0, 0, (charger.beltZ - charger.z) / 2)).cylinder(bore * 0.045, charger.beltZ - charger.z, 8);
        addSpinning(wheel, charger.x, charger.y, charger.z, 1);
        // De la volute au papillon : l'air chauffé.
        alloys.transform(null).tube(bendPath(charger.outFrom, [0, 1, 0], charger.outTo, [0, 0, -1], 16), bore * 0.13, 10);
      }
      // La boîte à air, et son conduit jusqu'à l'œil du compresseur.
      const [ax, ay, az] = charger.airbox;
      const [ahx, ahy, ahz] = charger.airboxSize;
      // Selon le filtre, comme sur un turbo : la boîte, le cône à l'air libre, ou une bouche arrondie.
      if (layout.airFilter === 'cone') {
        elements.transform(tr(ax, ay - ahy, az)).lathe(coneProfile(bore * 0.1, bore * 0.46, 2 * ahy + bore * 0.7, bore * 0.13), 26, 50);
      } else if (layout.airFilter === 'stacks') {
        alloys.transform(tr(ax, ay - ahy, az)).lathe([[bore * 0.13, -bore * 0.05], [bore * 0.15, bore * 0.2], [bore * 0.22, bore * 0.34], [bore * 0.36, bore * 0.42]], 22, 60);
      } else {
        blacks.transform(tr(ax, ay, az)).box(2 * ahx, 2 * ahy, 2 * ahz);
        (layout.airFilter === 'panel' ? elements : blacks).transform(tr(ax, ay + ahy + bore * 0.03, az)).box(2 * ahx * 1.05, bore * 0.05, 2 * ahz * 1.05);
      }
      const eye: P3 = charger.volumetric ? [charger.x, charger.y + charger.halfY, az] : [charger.x, charger.y + bore * 0.2, charger.z + bore * 0.1];
      blacks.transform(null).tube(bendPath([ax, ay - ahy, az], [0, -1, 0], eye, charger.volumetric ? [0, -1, 0] : [0, -1, 0], 10), bore * 0.13, 10);
      // Sa poulie et sa courroie, devant celle des accessoires, sur la poulie du vilebrequin.
      const pulley = new MeshBuilder();
      const pr = charger.pulleyRadius;
      pulley.transform(onZ(0, 0, 0)).lathe([[0, -bore * 0.1], [pr * 0.75, -bore * 0.1], [pr * 0.75, -bore * 0.09], [pr, -bore * 0.095], [pr, bore * 0.095], [pr * 0.75, bore * 0.09], [0, bore * 0.09]], 22, 40);
      for (let k = 0; k < 3; k++) pulley.transform(chainM(rz((k / 3) * Math.PI * 2), tr(pr * 0.42, 0, bore * 0.095))).box(pr * 0.6, pr * 0.18, bore * 0.02);
      addSpinning(pulley, charger.pulleyX, charger.pulleyY, charger.beltZ, 1);
      const chargerBelt = chainPath([[0, 0, charger.crankPulley], [charger.pulleyX, charger.pulleyY, pr]], bore * 0.2);
      const chargerOuter: P2[] = [];
      const chargerInner: P2[] = [];
      for (let k = chargerBelt.x.length - 1; k >= 0; k -= 2) {
        const nx = Math.sin(chargerBelt.angle[k]);
        const ny = -Math.cos(chargerBelt.angle[k]);
        chargerOuter.push([chargerBelt.x[k] - nx * bore * 0.012, chargerBelt.y[k] - ny * bore * 0.012]);
        chargerInner.push([chargerBelt.x[k] + nx * bore * 0.02, chargerBelt.y[k] + ny * bore * 0.02]);
      }
      blacks.transform(tr(0, 0, charger.beltZ)).extrudeRing(chargerOuter, chargerInner, bore * 0.17, 35);
      steels.transform(onZ(0, 0, charger.beltZ)).cylinder(charger.crankPulley * 0.97, bore * 0.18, 26);
      extent([charger.x, charger.yMax, 0]);
      extent([charger.xMax, charger.y, 0]);
    }
    extent([altX - altBodyR, altY + altBodyR, 0]);
    extent([pumpX + pumpR, pumpY + pumpR, 0]);

    // Le carter de distribution, en verre, autour de chaque chaîne. Pour la rampe commune du diesel : au-dessus des injecteurs.
    for (let bank = 0; bank < layout.banks; bank++) {
      const path = this.chains[bank];
      const margin = bore * 0.14;
      const cover: P2[] = [];
      const every = Math.max(1, Math.floor(path.x.length / 90));
      for (let k = path.x.length - every; k >= 0; k -= every) {
        const nx = Math.sin(path.angle[k]);
        const ny = -Math.cos(path.angle[k]);
        cover.push([path.x[k] - nx * margin, path.y[k] - ny * margin]);
      }
      covers.transform(tr(0, 0, this.chainZ[bank])).extrude(cover, bore * 0.2, 25);
    }
    if (diesel) {
      for (let bank = 0; bank < layout.banks; bank++) {
        const beta = layout.banks === 1 ? 0 : bank === 0 ? -halfDeg : halfDeg;
        const cylinders = layout.byBank[bank];
        // Le haut de l'injecteur : droit sur l'axe, ou penché vers l'échappement.
        const lean = valve.plugTiltDeg * DEG;
        const out = -cylinders[0].intakeSide;
        const injectorX = out * Math.sin(lean) * bore * 1.42;
        const injectorY = valve.sohc ? plugTipY + Math.cos(lean) * bore * 1.42 : deck + bore * 1.42;
        const y = valve.sohc ? injectorY + bore * 0.34 : deck + bore * 1.86;
        const railSide = injectorX + bore * 0.24 * out;
        const a = bankPoint(beta, railSide, y, cylinders[0].z + bore * 0.4 + valve.plugZ);
        const b = bankPoint(beta, railSide, y, cylinders[cylinders.length - 1].z - bore * 0.4 + valve.plugZ);
        steels.transform(null).tube([a, b], bore * 0.045, 10);
        for (const cylinder of cylinders) {
          const from = bankPoint(beta, railSide, y, cylinder.z + valve.plugZ);
          const to = bankPoint(beta, injectorX, injectorY, cylinder.z + valve.plugZ);
          steels.transform(null).tube(bendPath(from, bankDir(beta, -Math.sign(railSide), 0), to, bankDir(beta, 0, -1), 5), bore * 0.016, 6);
        }
        extent(a);
      }
    }

    // ── La mise en scène : chaque matériau, une seule fois ──
    const denseMaterial = materials.ghost.clone();
    denseMaterial.opacity = 0.28;
    const oilMaterial = stage.surface({ color: 0x5a3a0c, metalness: 0.1, roughness: 0.3, transparent: true, opacity: 0.5, depthWrite: false });
    const exhaustMaterial = stage.surface({ color: 0x7c7168, metalness: 0.8, roughness: 0.5, emissive: 0xff3a10, emissiveIntensity: 0 });
    this.exhaustMaterial = exhaustMaterial;
    const whiteMaterial = stage.surface({ color: 0xe9e6dd, metalness: 0, roughness: 0.35 });
    const glowSteel = stage.surface({ color: 0x9a9fa5, metalness: 0.9, roughness: 0.4 });

    add(geo(castings), materials.casting);
    add(geo(alloys), materials.alloy);
    add(geo(blacks), materials.rubber);
    add(geo(steels), materials.steel);
    if (elements.vertexCount > 0) add(geo(elements), stage.surface({ color: 0xb5382a, metalness: 0, roughness: 0.85 }), false);
    if (brasses.vertexCount > 0) add(geo(brasses), materials.brass, false);
    if (porcelain.vertexCount > 0) add(geo(porcelain), whiteMaterial, false);
    add(geo(headers), exhaustMaterial);
    if (airChain) {
      // L'air se lit à leur teinte : c'est lui qui chauffe et refroidit le tube.
      this.hotPipeMaterial = stage.surface({ color: 0xc8c4bc, metalness: 0.55, roughness: 0.45 });
      this.coldPipeMaterial = stage.surface({ color: 0xc8c4bc, metalness: 0.55, roughness: 0.45 });
      add(geo(hotPipes), this.hotPipeMaterial);
      add(geo(coldPipes), this.coldPipeMaterial);
      if (this.coolerGeometry) add(this.coolerGeometry, materials.alloy);
    }
    let glowTips: T.InstancedMesh | null = null;
    if (diesel) {
      add(geo(glowBase), glowSteel, false);
      glowTips = instanced(geo(new MeshBuilder().lathe([[0, 0], [bore * 0.024, bore * 0.02], [bore * 0.024, bore * 0.22], [0, bore * 0.24]], 10)), new THREE.MeshBasicMaterial({ color: 0xffffff, toneMapped: false }), count, false);
    }
    const oil = new MeshBuilder();
    oil.transform(tr(0, (oilTop + oilBottom) / 2, blockZ)).box(panWidth * 1.66, Math.abs(oilTop - oilBottom), blockLength * 0.96);
    const oilMesh = add(geo(oil), oilMaterial, false);
    oilMesh.renderOrder = 1;
    // Le verre se dessine en dernier, du plus dense au plus clair.
    const denseMesh = add(geo(dense), denseMaterial, false);
    denseMesh.renderOrder = 2;
    const headGeometry = geo(heads);
    const coverGeometry = geo(covers);
    // Le verre est ce qui coûte le plus de pixels : un rendu logiciel n'en garde que les arêtes, nettes et presque gratuites.
    if (!stage.software) {
      const headMesh = add(headGeometry, materials.ghost, false);
      headMesh.renderOrder = 3;
      const coverMesh = add(coverGeometry, materials.ghost, false);
      coverMesh.renderOrder = 4;
    }
    // Les arêtes des carters : elles tracent le moteur sans le cacher.
    this.edgeMaterial = new THREE.LineBasicMaterial({ color: 0x9aa4ae, transparent: true, opacity: 0.35, depthWrite: false });
    for (const geometry of [headGeometry, coverGeometry]) {
      const lines = new THREE.LineSegments(new THREE.EdgesGeometry(geometry, 35), this.edgeMaterial);
      lines.matrixAutoUpdate = false;
      lines.renderOrder = 5;
      this.root.add(lines);
      this.edges.push(lines);
      // Sans verre, les arêtes sont seules à garder cette géométrie : elle n'ira jamais au GPU, mais reste à libérer.
      if (stage.software) geometry.dispose();
    }

    this.d = { pistons, rings, pins, rods, valvesIn, valvesEx, springs, retainers, buckets, rockers, chain, gas, flame, spark, spray, jets, portsIn, portsEx, glowTips, pinion: pinionMesh, butterflies, accessory };

    // Les ombres, sur un vrai GPU seulement : les pièces s'en reçoivent les unes des autres.
    this.root.traverse((node) => {
      const mesh = node as T.Mesh;
      if (mesh.isMesh && mesh.castShadow) mesh.receiveShadow = true;
    });

    // ── Le cadre : la boîte réelle de ce qui est dessiné, pour que rien ne sorte de l'image et que le moteur la remplisse ──
    const bounds = new THREE.Box3();
    const part = new THREE.Box3();
    this.root.updateMatrixWorld(true);
    this.root.traverse((node) => {
      const shape = node as T.Mesh;
      if ((!shape.isMesh && !(node as T.LineSegments).isLineSegments) || (node as T.InstancedMesh).isInstancedMesh) return;
      if (!shape.geometry.boundingBox) shape.geometry.computeBoundingBox();
      part.copy(shape.geometry.boundingBox!).applyMatrix4(shape.matrixWorld);
      bounds.union(part);
    });
    // Les pièces en mouvement ne dépassent jamais de leur carter, sauf le haut d'un piston au point mort : c'est dans la culasse.
    const size = new THREE.Vector3();
    bounds.getCenter(size);
    this.center = [size.x, size.y, size.z];
    this.radius = bounds.getSize(size).length() * 0.5 * 0.8;

    // ── Les étiquettes : un point d'accroche par pièce, sur le premier cylindre ──
    const first = layout.byBank[0][0];
    const fb = first.betaDeg;
    const labelAt = (key: string, p: P3) => this.labels.push({ key, x: p[0], y: p[1], z: p[2] });
    labelAt('engine.part.crank', [0, -layout.crank * 0.9, layout.mains[layout.mains.length >> 1].z]);
    labelAt('engine.part.flywheel', [ring.radius * 0.7, ring.radius * 0.7, ring.z]);
    labelAt('engine.part.piston', bankPoint(fb, 0, deck - layout.stroke * 0.6, first.z));
    labelAt('engine.part.rod', bankPoint(fb, 0, layout.rod * 0.55, first.z));
    labelAt('engine.part.cam', [...bankPoint(fb, first.intakeSide * valve.camX, valve.camY, first.z - bore * 0.1)]);
    labelAt('engine.part.valve', bankPoint(fb, -first.intakeSide * valve.x, deck + bore * 0.55, first.z + valve.zOffset));
    if (valve.rocker) labelAt('engine.part.rocker', bankPoint(fb, first.intakeSide * valve.rocker.pivotX, valve.rocker.pivotY + bore * 0.1, first.z + first.intakeSide * valve.zOffset));
    labelAt('engine.part.chain', bankPoint(fb, valve.sohc ? layout.sprocket.camRadius * 0.8 : valve.camX, valve.camY * 0.6, layout.sprocket.z[0]));
    labelAt(diesel ? 'engine.part.injector' : 'engine.part.coil', bankPoint(fb, 0, deck + bore * 1.4, first.z));
    if (diesel) labelAt('engine.part.glow', bankPoint(fb, 0, deck + bore * 0.9, first.z + bore * 0.3));
    else labelAt('engine.part.plug', bankPoint(fb, 0, deck + bore * 0.45, first.z));
    labelAt('engine.part.pan', [panWidth * 0.5, panTop - panDepth * 0.6, blockZ]);
    labelAt('engine.part.starter', [pinion.x, pinion.y, ring.z + bore * 0.9]);
    labelAt('engine.part.exhaust', [xMax * 0.9, deck * 0.6, blockZ]);
    if (this.labelSpots.carb) labelAt('engine.part.carburetor', this.labelSpots.carb);
    if (airChain) {
      const unit = airChain.units[0];
      labelAt('engine.part.turbine', [unit.x, unit.y + unit.turbineHousing, unit.z]);
      labelAt('engine.part.compressor', [unit.x, unit.y + unit.compressorHousing, unit.compressorZ]);
      labelAt('engine.part.shaft', [unit.x, unit.y, (unit.z + unit.compressorZ) / 2]);
      labelAt(layout.airFilter === 'cone' ? 'engine.part.cone' : layout.airFilter === 'stacks' ? 'engine.part.stacks' : 'engine.part.airbox', [unit.airbox[0], unit.airbox[1] + unit.airboxSize[1], unit.airbox[2]]);
      if (this.labelSpots.maf) labelAt('engine.part.maf', this.labelSpots.maf);
      labelAt('engine.part.chargeHot', unit.hotRun.length > 0 ? unit.hotRun[Math.floor(unit.hotRun.length / 2)] : [(unit.hotFrom[0] + unit.hotTo[0]) / 2, (unit.hotFrom[1] + unit.hotTo[1]) / 2, (unit.hotFrom[2] + unit.hotTo[2]) / 2]);
      if (unit.feedRun.length > 0) labelAt('engine.part.turboPipe', unit.feedRun[Math.floor(unit.feedRun.length / 2)]);
      labelAt('engine.part.intercooler', [airChain.cooler[0], airChain.cooler[1], airChain.cooler[2] + airChain.coolerSize[2]]);
      if (this.labelSpots.coldPipe) labelAt('engine.part.chargeCold', this.labelSpots.coldPipe);
      if (layout.releaseValve && this.labelSpots.blowoff) labelAt('engine.part.blowoff', this.labelSpots.blowoff);
      labelAt('engine.part.wastegate', unit.actuator);
    }
    if (layout.charger) {
      const unit = chargerUnit(layout, layout.charger);
      labelAt('engine.part.compressor', [unit.x, unit.y + unit.halfY, unit.z]);
      labelAt('engine.part.belt', [unit.pulleyX / 2, unit.pulleyY / 2, unit.beltZ]);
      labelAt('engine.part.airbox', [unit.airbox[0], unit.airbox[1] + unit.airboxSize[1], unit.airbox[2]]);
    }
  }

  /** Une fourche : deux conduits qui descendent aux soupapes, et se rejoignent vers le côté +x de la culasse. */
  private buildPort(radius: number): MeshBuilder {
    const { valve, bore } = this.layout;
    const b = new MeshBuilder();
    const W = bore * 1.04;
    const rise = valve.seatRise;
    const portY = bore * 0.62;
    const tiltDir: P3 = [Math.sin(valve.tiltDeg * DEG), Math.cos(valve.tiltDeg * DEG), 0];
    for (const dz of valve.sohc ? [valve.zOffset] : [valve.zOffset, -valve.zOffset]) {
      const seat: P3 = [valve.x, rise + radius * 0.3, dz];
      b.tube(bendPath([W, portY, 0], [-1, -0.05, 0], [seat[0] + tiltDir[0] * bore * 0.1, seat[1] + tiltDir[1] * bore * 0.1, seat[2]], [-tiltDir[0], -tiltDir[1], 0], 8), radius, 9);
    }
    return b;
  }

  /** Une image : la pose donne les pièces, le gaz les couleurs. Rien n'alloue. */
  update(pose: EnginePose, motion: Motion, theme: Theme): void {
    const layout = this.layout;
    const { d, M, N, C, rgb, cylinderBase, valveBase } = this;
    const { bore, valve } = layout;
    this.adopt(theme);
    const crankRad = pose.crankDeg * DEG;
    this.crankGroup.rotation.z = -crankRad;
    if (this.secondary) {
      // La seconde masse suit la première, avec l'angle que le couple donne aux ressorts.
      const torque = Math.max(-1, Math.min(1, motion.clutchTorque / 400));
      M.makeRotationZ(-crankRad + torque * 0.22);
      this.secondary.matrix.copy(M);
    }
    const camRad = pose.camDeg * DEG;
    for (const cam of this.cams) cam.rotation.z = -camRad;

    const painter = motion.painter;
    const net = motion.gas.net;
    const chamberTop = this.chamberTop;
    const viewGas = pose.gasLive;
    let sparks = 0;
    let flames = 0;
    let sprays = 0;
    let jets = 0;
    let glows = 0;
    const R = bore / 2 - 0.5;
    for (let i = 0; i < layout.count; i++) {
      const cylinder = layout.cylinders[i];
      const base = cylinderBase[i];
      const s = pose.piston[i];
      // Piston, segments et axe glissent sur l'axe du cylindre.
      M.copy(base).multiply(N.makeTranslation(0, s, 0));
      d.pistons.setMatrixAt(i, M);
      d.rings.setMatrixAt(i, M);
      d.pins.setMatrixAt(i, M);
      // La bielle va du maneton à l'axe.
      M.makeRotationZ(-pose.rodAngle[i]);
      M.setPosition(pose.pinX[i], pose.pinY[i], cylinder.z);
      d.rods.setMatrixAt(i, M);

      // La colonne de gaz, de la tête du piston au toit, et sa couleur calculée.
      const crown = s + layout.pistonTop;
      const height = Math.max(chamberTop - crown, 0.5);
      M.copy(base).multiply(N.makeTranslation(0, crown, 0));
      M.multiply(N.makeScale(R, height, R));
      d.gas.setMatrixAt(i, M);
      const burning = pose.flame[i] > 0;
      if (viewGas) {
        const kelvin = pose.gasT[i];
        const shown = kelvin < 800 ? kelvin : 800 + (kelvin - 800) * 0.33;
        painter.gasRgb(theme, shown, burning ? 0 : pose.gasBurnt[i], rgb);
        C.setRGB(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255, this.stage.THREE.SRGBColorSpace);
      } else {
        C.setRGB(0.28, 0.34, 0.42, this.stage.THREE.SRGBColorSpace);
      }
      d.gas.setColorAt(i, C);

      // Les conduits : couleur du gaz qu'ils contiennent.
      this.placePort(M, N, cylinder, base, 1);
      d.portsIn.setMatrixAt(i, M);
      C.setRGB(this.coolR, this.coolG, this.coolB, this.stage.THREE.SRGBColorSpace);
      d.portsIn.setColorAt(i, C);
      this.placePort(M, N, cylinder, base, -1);
      d.portsEx.setMatrixAt(i, M);
      if (viewGas && net.n > 0 && i < net.cylinders) {
        const cell = net.runnerStart(i);
        painter.gasRgb(theme, net.temp[cell], net.y[cell], rgb);
        C.setRGB(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255, this.stage.THREE.SRGBColorSpace);
      } else {
        C.setRGB(0.42, 0.4, 0.38, this.stage.THREE.SRGBColorSpace);
      }
      d.portsEx.setColorAt(i, C);

      const sx = this.sparkAt[i * 3];
      const sy = this.sparkAt[i * 3 + 1];
      const sz = this.sparkAt[i * 3 + 2];

      if (pose.spark[i] > 0) {
        const size = bore * (0.05 + 0.08 * pose.spark[i]);
        M.makeScale(size, size, size);
        M.setPosition(sx, sy, sz);
        d.spark.setMatrixAt(sparks, M);
        C.setRGB(0.75 + 0.25 * pose.spark[i], 0.85, 1);
        d.spark.setColorAt(sparks, C);
        sparks++;
      }

      if (burning) {
        // La flamme part de la bougie : la masse brûlée est calculée (Wiebe), la forme du front est une approximation.
        const radius = bore * 0.08 + bore * 0.72 * Math.sqrt(pose.flame[i]);
        const inChamber = Math.min(radius, Math.max(height + (chamberTop - crown) * 0, 1));
        M.copy(base).multiply(N.makeTranslation(0, chamberTop, 0));
        M.multiply(N.makeScale(Math.min(radius, R), inChamber, Math.min(radius, R)));
        d.flame.setMatrixAt(flames, M);
        const heat = 1 - pose.flame[i];
        C.setRGB(1, 0.5 + 0.38 * heat, 0.14 + 0.5 * heat);
        d.flame.setColorAt(flames, C);
        flames++;
      }

      if (pose.spray[i] > 0) {
        const level = pose.spray[i];
        const scale = 0.5 + 0.5 * (1 - level);
        M.copy(base);
        if (layout.fuel === 'diesel') {
          M.multiply(N.makeTranslation(0, chamberTop, 0));
        } else {
          // Le jet part de l'injecteur, du côté de l'admission, vers le milieu de la chambre.
          if (cylinder.intakeSide < 0) M.multiply(N.makeRotationY(Math.PI));
          M.multiply(N.makeTranslation(bore * 0.42, layout.deck + bore * 0.03, 0));
        }
        M.multiply(N.makeScale(scale, scale, scale));
        d.spray.setMatrixAt(sprays, M);
        C.setRGB(0.55 * level, 0.72 * level, 0.9 * level);
        d.spray.setColorAt(sprays, C);
        sprays++;
      }

      if (d.glowTips) {
        // Du rouge sombre à l'orange vif : la bougie suit le courant, lentement.
        if (valve.sohc) {
          const lean = valve.plugTiltDeg * DEG;
          const back = bore * 0.17;
          const plugSide = cylinder.intakeSide;
          M.copy(base).multiply(N.makeTranslation(plugSide * (bore * 0.12 - Math.sin(lean) * back), layout.deck + valve.roofRise - bore * 0.03 - Math.cos(lean) * back, valve.plugZ));
          M.multiply(N.makeRotationZ(-plugSide * lean));
        } else {
          M.copy(base).multiply(N.makeTranslation(0, layout.deck + valve.roofRise - bore * 0.2, bore * 0.18));
          M.multiply(N.makeRotationX(0.26));
        }
        d.glowTips.setMatrixAt(glows, M);
        const g = pose.glow;
        C.setRGB(0.1 + 0.9 * g, 0.08 + 0.5 * g * g, 0.08 + 0.16 * g * g * g);
        d.glowTips.setColorAt(glows, C);
        glows++;
      }

      // Les soupapes de ce cylindre, les admissions puis les échappements : même ordre que la construction.
      const perCylinder = valve.perCylinder;
      const perKind = perCylinder >> 1;
      const k0 = i * perCylinder;
      for (let v = 0; v < perCylinder; v++) {
        const exhaust = v >= perKind;
        const lift = (exhaust ? pose.liftEx[i] : pose.liftIn[i]) * valve.lift;
        const k = k0 + v;
        const valveIndex = i * perKind + (v % perKind);
        M.copy(valveBase[k]).multiply(N.makeTranslation(0, -lift, 0));
        if (exhaust) d.valvesEx.setMatrixAt(valveIndex, M);
        else d.valvesIn.setMatrixAt(valveIndex, M);
        // Le ressort reste posé sur la culasse, et se comprime de la levée.
        M.copy(valveBase[k]).multiply(N.makeTranslation(0, this.springBase, 0));
        M.multiply(N.makeScale(1, (this.springHeight - lift) / this.springHeight, 1));
        d.springs.setMatrixAt(k, M);
        M.copy(valveBase[k]).multiply(N.makeTranslation(0, this.retainerY - lift, 0));
        d.retainers.setMatrixAt(k, M);
        if (d.buckets) {
          M.copy(valveBase[k]).multiply(N.makeTranslation(0, this.bucketY - lift, 0));
          d.buckets.setMatrixAt(k, M);
        }
        if (d.rockers) {
          // Le culbuteur bascule autour de son axe : côté soupape il descend, côté came il monte.
          M.copy(this.rockerBase[k]).multiply(N.makeRotationZ(-(exhaust ? pose.rockerEx[i] : pose.rockerIn[i])));
          d.rockers.setMatrixAt(k, M);
        }
        // Les jets : le débit calculé à chaque soupape.
        const flow = exhaust ? pose.jetEx[i] : pose.jetIn[i];
        const amount = flow < 0 ? -flow : flow;
        if (viewGas && amount > 0.02) {
          const length = Math.min(amount * (exhaust ? 1.4 : 1.8), 1) * height * 0.9;
          const radius = bore * (exhaust ? 0.11 : 0.13);
          M.copy(valveBase[k]).multiply(N.makeTranslation(0, -lift * 0.9, 0));
          M.multiply(N.makeScale(radius, length, radius));
          d.jets.setMatrixAt(jets, M);
          if (exhaust && flow > 0) C.setRGB(0.55, 0.3, 0.14);
          else C.setRGB(this.coolR * 0.5, this.coolG * 0.5, this.coolB * 0.5);
          d.jets.setColorAt(jets, C);
          jets++;
        }
      }
    }
    d.spark.count = sparks;
    d.flame.count = flames;
    d.spray.count = sprays;
    d.jets.count = jets;
    this.drawnSparks = sparks;
    this.drawnFlames = flames;
    this.drawnSprays = sprays;
    d.pistons.instanceMatrix.needsUpdate = true;
    d.rings.instanceMatrix.needsUpdate = true;
    d.pins.instanceMatrix.needsUpdate = true;
    d.rods.instanceMatrix.needsUpdate = true;
    d.valvesIn.instanceMatrix.needsUpdate = true;
    d.valvesEx.instanceMatrix.needsUpdate = true;
    d.springs.instanceMatrix.needsUpdate = true;
    d.retainers.instanceMatrix.needsUpdate = true;
    if (d.buckets) d.buckets.instanceMatrix.needsUpdate = true;
    if (d.rockers) d.rockers.instanceMatrix.needsUpdate = true;
    d.gas.instanceMatrix.needsUpdate = true;
    d.portsIn.instanceMatrix.needsUpdate = true;
    d.portsEx.instanceMatrix.needsUpdate = true;
    if (d.gas.instanceColor) d.gas.instanceColor.needsUpdate = true;
    if (d.portsIn.instanceColor) d.portsIn.instanceColor.needsUpdate = true;
    if (d.portsEx.instanceColor) d.portsEx.instanceColor.needsUpdate = true;
    if (sparks > 0) {
      d.spark.instanceMatrix.needsUpdate = true;
      if (d.spark.instanceColor) d.spark.instanceColor.needsUpdate = true;
    }
    if (flames > 0) {
      d.flame.instanceMatrix.needsUpdate = true;
      if (d.flame.instanceColor) d.flame.instanceColor.needsUpdate = true;
    }
    if (sprays > 0) {
      d.spray.instanceMatrix.needsUpdate = true;
      if (d.spray.instanceColor) d.spray.instanceColor.needsUpdate = true;
    }
    if (jets > 0) {
      d.jets.instanceMatrix.needsUpdate = true;
      if (d.jets.instanceColor) d.jets.instanceColor.needsUpdate = true;
    }
    if (d.glowTips) {
      d.glowTips.instanceMatrix.needsUpdate = true;
      if (d.glowTips.instanceColor) d.glowTips.instanceColor.needsUpdate = true;
    }

    // Les chaînes : la vitesse de leur pignon, sans jamais glisser.
    let link = 0;
    const travel = pose.chainTurns * Math.PI * 2 * layout.sprocket.crankRadius;
    for (let c = 0; c < this.chains.length; c++) {
      const path = this.chains[c];
      const z = this.chainZ[c];
      const samples = path.x.length;
      const zero = ((travel % path.length) + path.length) % path.length;
      for (let k = 0; k < path.links; k++) {
        const at = ((k * path.pitch + zero) / path.step) % samples | 0;
        M.makeRotationZ(path.angle[at]);
        M.setPosition(path.x[at], path.y[at], z);
        d.chain.setMatrixAt(link++, M);
      }
    }
    d.chain.instanceMatrix.needsUpdate = true;

    // Le démarreur : le pignon avance jusqu'à la couronne, et ne tourne qu'avec elle.
    const pinion = layout.pinion;
    M.makeRotationZ(pose.pinionAngle);
    M.setPosition(pinion.x, pinion.y, layout.ring.z + bore * 0.22 * (1 - pose.starter));
    d.pinion.matrix.copy(M);
    d.pinion.matrixWorldNeedsUpdate = true;

    // Le papillon : calculé par le gaz quand il tourne.
    const open = viewGas ? net.throttleShare : motion.load;
    const angle = 0.14 + open * 1.32;
    // Sur un corps par cylindre, l'arbre du volet est parallèle au vilebrequin.
    const spindleZ = layout.carb?.kind === 'multi';
    for (let b = 0; b < d.butterflies.length; b++) {
      if (spindleZ) d.butterflies[b].rotation.z = angle;
      else d.butterflies[b].rotation.x = angle;
    }

    for (let a = 0; a < this.accessoryAt.length; a++) {
      const item = this.accessoryAt[a];
      M.makeRotationZ(-((pose.chainTurns * item.ratio) % 1) * Math.PI * 2);
      N.makeScale(item.scale, item.scale, 1);
      M.multiply(N);
      M.setPosition(item.x, item.y, item.z);
      d.accessory.setMatrixAt(a, M);
    }
    d.accessory.instanceMatrix.needsUpdate = true;

    for (let t = 0; t < this.turboWheels.length; t++) this.turboWheels[t].rotation.z = motion.turbineAngle;
    for (let t = 0; t < this.chargerParts.length; t++) this.chargerParts[t].mesh.rotation.z = motion.chargerAngle * this.chargerParts[t].ratio;
    for (let t = 0; t < this.wastegateRods.length; t++) {
      const rod = this.wastegateRods[t];
      rod.mesh.position.z = rod.rest - rod.travel * pose.wastegate;
    }
    for (let t = 0; t < this.blowoffPistons.length; t++) {
      const piston = this.blowoffPistons[t];
      piston.mesh.position.z = piston.rest + piston.travel * pose.blowoff;
    }
    if (this.hotPipeMaterial && this.coldPipeMaterial) {
      this.tintAir(this.hotPipeMaterial, pose.compressorOutK);
      this.tintAir(this.coldPipeMaterial, pose.coolerOutK);
    }

    // Le collecteur rougit avec la ligne. Une matcap n'a pas de lumière propre : c'est sa teinte qui chauffe.
    const hot = motion.heat * motion.heat;
    if (this.stage.software) this.exhaustMaterial.color.setRGB(0.2 + 0.55 * hot, 0.16 + 0.03 * hot, 0.14 - 0.08 * hot);
    else this.exhaustMaterial.emissiveIntensity = hot * 0.85;
  }

  /** Un tube prend la teinte de l'air qu'il contient : aluminium froid, orangé à 170 °C. */
  private tintAir(material: T.MeshStandardMaterial, kelvin: number): void {
    const hot = Math.max(0, Math.min(1, (kelvin - 300) / 140));
    material.color.setRGB(0.72 + 0.2 * hot, 0.72 - 0.3 * hot, 0.72 - 0.56 * hot);
  }

  private placePort(M: T.Matrix4, N: T.Matrix4, cylinder: Cylinder, base: T.Matrix4, which: number): void {
    M.copy(base);
    // La fourche est dessinée du côté +x : un demi-tour autour de l'axe la met de l'autre côté, sans miroir.
    const side = which > 0 ? cylinder.intakeSide : -cylinder.intakeSide;
    if (side < 0) M.multiply(N.makeRotationY(Math.PI));
    M.multiply(N.makeTranslation(0, this.layout.deck, 0));
  }

  private adopt(theme: Theme): void {
    if (theme === this.lastTheme) return;
    this.lastTheme = theme;
    const cool = parseColor(theme.cool);
    this.coolR = cool[0];
    this.coolG = cool[1];
    this.coolB = cool[2];
    const line = parseColor(theme.lineStrong);
    this.edgeMaterial.color.setRGB(line[0], line[1], line[2], this.stage.THREE.SRGBColorSpace);
  }

  dispose(): void {
    this.stage.scene.remove(this.root);
    disposeObject(this.root, this.stage.materials);
  }
}

/** Le moteur électrique en 3D : stator bobiné, rotor à aimants, carter en verre. Le rotor suit `motion.rotorAngle`. */
export class ElectricScene {
  readonly root: T.Group;
  readonly labels: PartLabel[] = [];
  center: P3 = [0, 0, 0];
  radius = 210;
  private readonly stage: Stage3D;
  private readonly rotor: T.Mesh;
  private readonly coils: T.InstancedMesh;
  private readonly C: T.Color;
  private readonly M: T.Matrix4;
  private lastTheme: Theme | null = null;
  private lit: [number, number, number] = [0.4, 0.7, 1];

  constructor(stage: Stage3D) {
    this.stage = stage;
    const { THREE, materials } = stage;
    this.C = new THREE.Color();
    this.M = new THREE.Matrix4();
    this.root = new THREE.Group();
    stage.scene.add(this.root);
    const geo = (builder: MeshBuilder) => toGeometry(THREE, builder.build());
    const length = 150;
    const statorOuter = circle(135, 48);
    const statorInner = circle(92, 48);
    const stator = new THREE.Mesh(geo(new MeshBuilder().extrudeRing(statorOuter, statorInner, length, 40)), materials.casting);
    this.root.add(stator);
    const housing = new MeshBuilder();
    housing.extrudeRing(circle(150, 48), circle(142, 48), length + 50, 40);
    const shell = new THREE.Mesh(geo(housing), materials.ghost);
    shell.renderOrder = 2;
    this.root.add(shell);
    const coilPath: P3[] = [];
    const span = 60;
    for (let k = 0; k <= 20; k++) {
      const a = (k / 20) * Math.PI * 2;
      coilPath.push([Math.cos(a) * 12, Math.sin(a) * span, 0]);
    }
    const coil = new MeshBuilder().transform(rx(Math.PI / 2)).tube(coilPath, 8, 8, true);
    const coils = new THREE.InstancedMesh(geo(coil), stage.surface({ color: 0xffffff, metalness: 0.9, roughness: 0.35 }), 12);
    coils.frustumCulled = false;
    this.coils = coils;
    for (let k = 0; k < 12; k++) {
      this.M.makeRotationZ((k / 12) * Math.PI * 2);
      this.M.multiply(new THREE.Matrix4().makeTranslation(104, 0, 0));
      coils.setMatrixAt(k, this.M);
      coils.setColorAt(k, this.C.setRGB(0.72, 0.4, 0.18));
    }
    this.root.add(coils);
    const rotor = new MeshBuilder();
    rotor.transform(rx(Math.PI / 2)).lathe([[22, -length / 2], [84, -length / 2], [84, length / 2], [22, length / 2]], 36);
    for (let k = 0; k < 8; k++) {
      const a = (k / 8) * Math.PI * 2;
      rotor.transform(chainM(rz(a), tr(79, 0, 0))).box(11, 46, length * 0.94);
    }
    this.rotor = new THREE.Mesh(geo(rotor), materials.forged);
    this.root.add(this.rotor);
    const shaft = new MeshBuilder().transform(rx(Math.PI / 2)).lathe([[0, -length * 0.9], [22, -length * 0.9], [22, length * 0.9], [0, length * 0.9]], 24);
    this.root.add(new THREE.Mesh(geo(shaft), materials.steel));
    this.labels.push({ key: 'engine.part.stator', x: 120, y: 80, z: 40 }, { key: 'engine.part.rotor', x: 0, y: 70, z: 75 });
  }

  update(motion: Motion, theme: Theme): void {
    if (theme !== this.lastTheme) {
      this.lastTheme = theme;
      this.lit = parseColor(theme.cool);
    }
    const rotor = motion.rotorAngle;
    this.rotor.rotation.z = rotor;
    const { C, coils } = this;
    // Le champ tourne avec le rotor : trois bobines sur douze travaillent à chaque instant.
    const step = Math.floor((rotor * 12) / (Math.PI * 2));
    for (let k = 0; k < 12; k++) {
      const on = (step + k) % 3 === 0 && motion.load > 0.02;
      if (on) C.setRGB(this.lit[0], this.lit[1], this.lit[2], this.stage.THREE.SRGBColorSpace);
      else C.setRGB(0.72, 0.4, 0.18);
      coils.setColorAt(k, C);
    }
    if (coils.instanceColor) coils.instanceColor.needsUpdate = true;
  }

  dispose(): void {
    this.stage.scene.remove(this.root);
    disposeObject(this.root, this.stage.materials);
  }
}
