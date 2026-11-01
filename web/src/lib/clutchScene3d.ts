/**
 * Le modèle 3D de l'embrayage, sur la scène partagée de `render3d.ts`. Les profils viennent de `clutchSection.ts` :
 * la 3D tourne ce que la 2D coupe. Un quart est retiré dans le monde, pas sur la pièce : les pièces tournent, la découpe reste,
 * et une face intérieure vue par la découpe se peint comme une coupe hachurée.
 *
 * `update` n'alloue rien : pas de chaîne, pas de tableau, pas de closure.
 */
import type * as ThreeNamespace from 'three';
import { BEARING_BALL, BEARING_PITCH_R, FORK_SLAVE_R, FORK_STUD_R, type ClutchFrame } from './clutchMechanism';
import { bellProfile, circlePoints, dctLeverX, diaphragmX, groupAngle, groupOffset, sectionParts, type ClutchLayout, type PartGroup, type PartMaterial } from './clutchSection';
import { MeshBuilder, m34, toGeometry, triangulate, type MeshData, type Stage3D } from './render3d';
import type { ClutchLive } from './clutchDraw2d';

const TAU = Math.PI * 2;
type Three = Stage3D['THREE'];
type Material = ThreeNamespace.MeshStandardMaterial | ThreeNamespace.MeshLambertMaterial;

/** Y d'un `MeshBuilder` devient l'axe de l'embrayage, X. */
const yToX = m34.rotationZ(m34.create(), -Math.PI / 2);
/** Z d'une roue dentée `gear` devient X. */
const zToX = m34.rotationY(m34.create(), Math.PI / 2);

/** Un contour (x, r) tourné de `from` à `to` : plein, fermé à ses deux bouts si ce n'est pas un tour complet. */
export function revolve(points: readonly number[], from: number, to: number, segments: number): MeshData {
  // Contour dans le sens trigonométrique en (r, x) : les faces extérieures regardent dehors.
  let area = 0;
  const n = points.length / 2;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    area += points[i * 2 + 1] * points[j * 2] - points[j * 2 + 1] * points[i * 2];
  }
  const ordered: number[] = [];
  for (let i = 0; i < n; i++) {
    const k = area >= 0 ? i : n - 1 - i;
    ordered.push(points[k * 2], points[k * 2 + 1]);
  }
  const positions: number[] = [];
  const normals: number[] = [];
  const indices: number[] = [];
  for (let e = 0; e < n; e++) {
    const f = (e + 1) % n;
    const x0 = ordered[e * 2], r0 = ordered[e * 2 + 1], x1 = ordered[f * 2], r1 = ordered[f * 2 + 1];
    const len = Math.hypot(x1 - x0, r1 - r0) || 1;
    // Une arête qui monte (x croissant) en r : sa normale regarde vers l'extérieur.
    const nr = (x1 - x0) / len;
    const nx = -(r1 - r0) / len;
    const base = positions.length / 3;
    for (let s = 0; s <= segments; s++) {
      const a = from + ((to - from) * s) / segments;
      const c = Math.cos(a);
      const si = Math.sin(a);
      positions.push(x0, r0 * c, r0 * si, x1, r1 * c, r1 * si);
      normals.push(nx, nr * c, nr * si, nx, nr * c, nr * si);
    }
    for (let s = 0; s < segments; s++) {
      const a = base + s * 2;
      indices.push(a, a + 2, a + 3, a, a + 3, a + 1);
    }
  }
  if (to - from < TAU - 1e-6) {
    const outline: [number, number][] = [];
    for (let i = 0; i < n; i++) outline.push([ordered[i * 2 + 1], ordered[i * 2]]);
    const tris = triangulate(outline);
    for (const [angle, flip] of [[from, false], [to, true]] as const) {
      const c = Math.cos(angle);
      const si = Math.sin(angle);
      const base = positions.length / 3;
      const nn = flip ? 1 : -1;
      for (const [r, x] of outline) {
        positions.push(x, r * c, r * si);
        normals.push(0, -si * nn, c * nn);
      }
      for (let t = 0; t < tris.length; t += 3) {
        if (flip) indices.push(base + tris[t], base + tris[t + 2], base + tris[t + 1]);
        else indices.push(base + tris[t], base + tris[t + 1], base + tris[t + 2]);
      }
    }
  }
  return { positions, normals, indices };
}

/** Remet chaque triangle dans le sens que demandent les normales : la découpe distingue l'intérieur de l'extérieur par là. */
function orientFaces(positions: readonly number[], normals: readonly number[], indices: number[]): number[] {
  for (let t = 0; t < indices.length; t += 3) {
    const a = indices[t] * 3, b = indices[t + 1] * 3, c = indices[t + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nx * normals[a] + ny * normals[a + 1] + nz * normals[a + 2] < 0) {
      indices[t + 1] = c / 3;
      indices[t + 2] = b / 3;
    }
  }
  return indices;
}

const rectPoints = (x0: number, x1: number, r0: number, r1: number) => [x0, r0, x0, r1, x1, r1, x1, r0];

/** Couleurs d'une matière : ce que voit la lumière, et le ton de la coupe. */
const LOOK: Record<PartMaterial, { base: keyof Stage3D['materials']; color?: number; roughness?: number; metalness?: number; cap: number }> = {
  cast: { base: 'forged', color: 0x8b9096, roughness: 0.62, metalness: 0.8, cap: 0x4e5358 },
  steel: { base: 'steel', cap: 0xa6adb4 },
  sheet: { base: 'forged', color: 0x9aa2a9, roughness: 0.45, metalness: 0.75, cap: 0x7f8990 },
  friction: { base: 'casting', color: 0x6d5238, roughness: 0.95, metalness: 0, cap: 0xa07444 },
  ring: { base: 'forged', color: 0x6c7379, roughness: 0.42, cap: 0x3e4449 },
  race: { base: 'steel', color: 0xdce2e8, roughness: 0.18, cap: 0xeef1f4 },
  carrier: { base: 'forged', color: 0x4d5258, roughness: 0.55, metalness: 0.3, cap: 0x2d3136 },
  brass: { base: 'brass', cap: 0xc69a52 },
  shaft: { base: 'steel', color: 0xaab2ba, roughness: 0.28, cap: 0x9aa3ab },
  housing: { base: 'casting', color: 0x777c80, cap: 0x63686c },
  crank: { base: 'forged', color: 0x6f767c, cap: 0x585e63 },
  rubber: { base: 'rubber', cap: 0x1c1d1f },
  lever: { base: 'steel', color: 0x6d8fb3, roughness: 0.34, cap: 0x4f83bd },
};

/** Une pièce vue de l'intérieur par la découpe : une coupe hachurée, sans éclairage. */
function hatchInterior(material: Material, cap: number, THREE: Three): void {
  material.onBeforeCompile = (shader) => {
    shader.uniforms.capColor = { value: new THREE.Color().setHex(cap, THREE.SRGBColorSpace) };
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', '#include <common>\nuniform vec3 capColor;')
      .replace('#include <dithering_fragment>', `#include <dithering_fragment>
  if (!gl_FrontFacing) {
    float hatch = mod(gl_FragCoord.x + gl_FragCoord.y, 6.0) < 1.5 ? 0.82 : 1.0;
    gl_FragColor = vec4(sqrt(capColor) * hatch, 1.0);
  }`);
  };
  material.customProgramCacheKey = () => 'clutch-cut';
}

/** Longueur du ressort de rappel au repos, avant que la fourchette ne l'étire. */
const RETURN_SPRING = 30;

/** Un disque de moindre détail sur un rendu logiciel : la moitié des segments, le même dessin. */
const SEGMENTS = 96;

export class ClutchScene {
  private readonly stage: Stage3D;
  private readonly layout: ClutchLayout;
  readonly root: ThreeNamespace.Group;
  private readonly nodes = new Map<PartGroup, ThreeNamespace.Object3D>();
  private readonly nodeList: { group: PartGroup; node: ThreeNamespace.Object3D }[] = [];
  private readonly materials = new Map<PartMaterial, Material>();
  private readonly geometries: ThreeNamespace.BufferGeometry[] = [];
  private readonly owned: ThreeNamespace.Material[] = [];
  private readonly clip: ThreeNamespace.Plane[];
  private readonly frictionMaterial: Material;
  private readonly plateMaterial: Material;
  private readonly massMaterial: Material;
  private readonly raceMark: Material;
  private readonly hotColor: ThreeNamespace.Color;
  private readonly matrix: ThreeNamespace.Matrix4;
  private readonly quaternion: ThreeNamespace.Quaternion;
  private readonly position: ThreeNamespace.Vector3;
  private readonly scale3: ThreeNamespace.Vector3;
  private readonly axisX: ThreeNamespace.Vector3;

  private dampers: ThreeNamespace.InstancedMesh | null = null;
  private damperNode: ThreeNamespace.Object3D | null = null;
  private readonly arcNodes: ThreeNamespace.Object3D[] = [];
  private arcUpdate: ((span: number) => void) | null = null;
  private diaphragmUpdate: ((frame: ClutchFrame) => void) | null = null;
  private strapUpdate: ((lift: number) => void) | null = null;
  private forkNode: ThreeNamespace.Object3D | null = null;
  private pedalNode: ThreeNamespace.Object3D | null = null;
  private slavePiston: ThreeNamespace.Object3D | null = null;
  private slaveFluid: ThreeNamespace.Object3D | null = null;
  private masterPiston: ThreeNamespace.Object3D | null = null;
  private masterFluid: ThreeNamespace.Object3D | null = null;
  private slaveRod: ThreeNamespace.Object3D | null = null;
  private masterRod: ThreeNamespace.Object3D | null = null;
  private returnSpring: ThreeNamespace.Object3D | null = null;
  private beads: ThreeNamespace.InstancedMesh | null = null;
  private sparks: ThreeNamespace.InstancedMesh | null = null;
  private readonly hosePath: number[] = [];
  private sparkSeed = 7;
  /** Nombre de triangles, pour la mesure de coût. */
  triangles = 0;

  constructor(stage: Stage3D, layout: ClutchLayout) {
    this.stage = stage;
    this.layout = layout;
    const THREE = stage.THREE;
    this.matrix = new THREE.Matrix4();
    this.quaternion = new THREE.Quaternion();
    this.position = new THREE.Vector3();
    this.scale3 = new THREE.Vector3(1, 1, 1);
    this.axisX = new THREE.Vector3(1, 0, 0);
    this.hotColor = new THREE.Color();
    this.root = new THREE.Group();
    stage.scene.add(this.root);
    stage.renderer.localClippingEnabled = true;
    // Le quart retiré : au-dessus de l'axe, vers la caméra. Le plan z = 0, c'est la coupe de la vue 2D.
    this.clip = [new THREE.Plane(new THREE.Vector3(0, -1, 0), 0), new THREE.Plane(new THREE.Vector3(0, 0, -1), 0)];
    for (const key of Object.keys(LOOK) as PartMaterial[]) this.materials.set(key, this.makeMaterial(key));
    this.frictionMaterial = this.materials.get('friction')!;
    this.plateMaterial = this.makeMaterial('cast');
    this.massMaterial = this.makeMaterial('cast');
    this.raceMark = this.stage.software ? new THREE.MeshLambertMaterial({ color: this.stage.materials.accent.color.getHex() }) : this.stage.materials.accent.clone();
    this.raceMark.clippingPlanes = this.clip;
    this.raceMark.clipIntersection = true;
    this.owned.push(this.raceMark);
    this.build();
    this.frameWhole();
  }

  /** On regarde le plan de coupe presque de face, un peu du côté de la boîte et par-dessus : assez pour voir les doigts et la butée. */
  frameWhole(): void {
    const l = this.layout;
    // Sur un écran étroit, c'est la longueur de l'ensemble qui décide, pas sa hauteur.
    const element = this.stage.renderer.domElement;
    const narrow = element.clientWidth < 1.2 * element.clientHeight;
    this.stage.frameOn((l.xMin + l.xMax) / 2 - 8, 46, 0, narrow ? (l.xMax - l.xMin) * 0.56 : Math.max(l.dims.flywheelR + 24, 165), { yaw: 0.38, pitch: 0.2 });
    this.stage.resetView();
  }

  /** La butée de près, par la découpe : races, billes, cage, et les doigts qu'elle pousse. */
  frameBearing(): void {
    const l = this.layout;
    const x = l.dct ? l.dct.k1NoseX + 8 : l.noseX + 8;
    this.stage.frameOn(x + 12, 28, 0, 44, { yaw: 0.34, pitch: 0.16 });
    this.stage.resetView();
  }

  /** Sur un rendu logiciel, chaque pixel coûte : un éclairage simple, sans reflets, garde la vue fluide et lisible. */
  private basic(key: PartMaterial): Material {
    const THREE = this.stage.THREE;
    const look = LOOK[key];
    const base = this.stage.materials[look.base];
    if (this.stage.software) return new THREE.MeshLambertMaterial({ color: look.color ?? base.color.getHex() });
    const material = (base as ThreeNamespace.MeshStandardMaterial).clone();
    if (look.color !== undefined) material.color.setHex(look.color);
    if (look.roughness !== undefined) material.roughness = look.roughness;
    if (look.metalness !== undefined) material.metalness = look.metalness;
    return material;
  }

  private makeMaterial(key: PartMaterial): Material {
    const THREE = this.stage.THREE;
    const look = LOOK[key];
    const material = this.basic(key);
    material.side = THREE.DoubleSide;
    material.clippingPlanes = this.clip;
    material.clipIntersection = true;
    hatchInterior(material, look.cap, THREE);
    this.owned.push(material);
    return material;
  }

  private unclipped(key: PartMaterial): Material {
    const material = this.basic(key);
    this.owned.push(material);
    return material;
  }

  private node(group: PartGroup): ThreeNamespace.Object3D {
    let node = this.nodes.get(group);
    if (!node) {
      node = new this.stage.THREE.Group();
      this.root.add(node);
      this.nodes.set(group, node);
      this.nodeList.push({ group, node });
    }
    return node;
  }

  private addMesh(parent: ThreeNamespace.Object3D, data: MeshData, material: ThreeNamespace.Material): ThreeNamespace.Mesh {
    const geometry = toGeometry(this.stage.THREE, data);
    this.geometries.push(geometry);
    this.triangles += data.indices.length / 3;
    const mesh = new this.stage.THREE.Mesh(geometry, material);
    mesh.frustumCulled = false;
    parent.add(mesh);
    return mesh;
  }

  /** Mêmes pièces répétées autour de l'axe : un seul maillage. */
  private addInstances(parent: ThreeNamespace.Object3D, data: MeshData, material: ThreeNamespace.Material, count: number, place: (i: number, matrix: ThreeNamespace.Matrix4) => void): ThreeNamespace.InstancedMesh {
    const geometry = toGeometry(this.stage.THREE, data);
    this.geometries.push(geometry);
    this.triangles += (data.indices.length / 3) * count;
    const mesh = new this.stage.THREE.InstancedMesh(geometry, material, count);
    mesh.frustumCulled = false;
    const matrix = new this.stage.THREE.Matrix4();
    for (let i = 0; i < count; i++) {
      place(i, matrix);
      mesh.setMatrixAt(i, matrix);
    }
    mesh.instanceMatrix.needsUpdate = true;
    parent.add(mesh);
    return mesh;
  }

  /** Une matrice : pose à (x, r, angle), l'axe local X restant celui de l'embrayage. */
  private polar(matrix: ThreeNamespace.Matrix4, x: number, r: number, angle: number): ThreeNamespace.Matrix4 {
    this.quaternion.setFromAxisAngle(this.axisX, angle);
    this.position.set(x, r * Math.cos(angle), r * Math.sin(angle));
    this.scale3.set(1, 1, 1);
    return matrix.compose(this.position, this.quaternion, this.scale3);
  }

  private build(): void {
    const l = this.layout;
    const d = l.dims;
    const skip = new Set(['boxWall', 'cover', 'pilotNeedles', 'liningFront', 'liningRear', 'retainerFront', 'retainerRear', 'hubFlange', 'cage', 'cageRear', 'liningFrontK1', 'liningRearK1', 'liningFrontK2', 'liningRearK2', 'retainerFrontK1', 'retainerRearK1', 'retainerFrontK2', 'retainerRearK2', 'hubFlangeK1', 'hubFlangeK2']);
    const builders = new Map<string, { builder: MeshBuilder; group: PartGroup; material: PartMaterial }>();
    const take = (group: PartGroup, material: PartMaterial) => {
      const key = `${group}/${material}`;
      let entry = builders.get(key);
      if (!entry) {
        entry = { builder: new MeshBuilder(), group, material };
        builders.set(key, entry);
      }
      return entry.builder;
    };
    for (const part of sectionParts(l)) {
      if (skip.has(part.id)) continue;
      take(part.group, part.material).add(revolve(part.points, 0, TAU, part.points.length > 16 ? 64 : SEGMENTS));
    }

    this.buildCover(take);
    if (l.dct) {
      this.buildDisc(take, 'k1Lining', 'k1Lining', 'k1Hub', l.dct.k1DiscX);
      this.buildDisc(take, 'k2Lining', 'k2Lining', 'k2Hub', l.dct.k2DiscX);
    } else {
      this.buildDisc(take, 'liningFront', 'liningRear', 'hub', 0);
    }
    this.buildRingGear(take);
    this.buildSplines(take);
    this.buildPlateLugs(take);

    for (const entry of builders.values()) {
      const material = this.pickMaterial(entry.group, entry.material);
      this.addMesh(this.node(entry.group), entry.builder.build(), material);
    }
    this.buildBolts();
    this.buildBearingFeatures();
    this.buildPilotNeedles();
    this.buildBell();
    if (l.dct) this.buildDctLevers();
    else {
      this.buildDiaphragm();
      this.buildStraps();
      this.buildPivotRings();
      this.buildFork();
      this.buildCommand();
    }
    if (d.dualMass) this.buildArcSprings();
    if (d.dampers > 0) this.buildDampers();
    this.buildSparks();
  }

  private pickMaterial(group: PartGroup, material: PartMaterial): Material {
    if (material === 'cast' && (group === 'plate' || group === 'k1Plate' || group === 'k2Plate')) return this.plateMaterial;
    if (material === 'cast') return this.massMaterial;
    return this.materials.get(material)!;
  }

  /** Le couvercle : son fond, et une paroi percée de trois fenêtres où passent les pattes du plateau. */
  private buildCover(take: (group: PartGroup, material: PartMaterial) => MeshBuilder): void {
    const l = this.layout;
    if (l.dct) {
      const cover = sectionParts(l).find((part) => part.id === 'cover')!;
      take('mass', 'sheet').add(revolve(cover.points, 0, TAU, SEGMENTS));
      return;
    }
    const b = take('mass', 'sheet');
    b.add(revolve([l.coverIn, l.coverOpening, l.coverIn, l.wallIn - 2, l.coverIn - 2, l.wallIn, l.coverIn - 2, l.wallOut, l.coverOut - 3, l.wallOut, l.coverOut, l.wallOut - 3, l.coverOut, l.coverOpening], 0, TAU, SEGMENTS));
    const wall = [0, l.flangeR, 3, l.flangeR, 3, l.wallOut, l.coverIn - 2, l.wallOut, l.coverIn - 2, l.wallIn, 0, l.wallIn];
    const windows = 3;
    const half = 0.26;
    for (let i = 0; i < windows; i++) {
      const a = (i / windows) * TAU;
      b.add(revolve(wall, a + half, a + TAU / windows - half, 26));
    }
  }

  /** Le disque : garnitures rainurées et rivetées, segments, plaques de guidage et voile, ou simple voile sur un bi-masse. */
  private buildDisc(take: (group: PartGroup, material: PartMaterial) => MeshBuilder, front: PartGroup, rear: PartGroup, hub: PartGroup, x0: number): void {
    const l = this.layout;
    const d = l.dims;
    const li = d.liningInnerR;
    const lo = d.liningOuterR;
    const t = l.liningThickness;
    const w = l.discWidth;
    const sectors = 16;
    const groove = 0.012;
    const f = take(front, 'friction');
    const r = take(rear, 'friction');
    for (let i = 0; i < sectors; i++) {
      const a = (i / sectors) * TAU + groove;
      const b = ((i + 1) / sectors) * TAU - groove;
      f.add(revolve(rectPoints(x0, x0 + t, li, lo), a, b, 8));
      r.add(revolve(rectPoints(x0 + w - t, x0 + w, li, lo), a, b, 8));
    }
    // Les segments, entre les deux garnitures : des pétales d'acier ondulés.
    const seg = take(front, 'sheet');
    const petals = 12;
    for (let i = 0; i < petals; i++) {
      const a = (i / petals) * TAU + 0.03;
      const b = ((i + 1) / petals) * TAU - 0.03;
      seg.add(revolve(rectPoints(x0 + t + 0.05, x0 + t + 0.45, li - 6, lo - 2), a, b, 4));
    }
    const sheet = take(front, 'sheet');
    const hR = l.hubR;
    if (d.dampers > 0) {
      // Plaques de guidage percées, voile du moyeu percé aussi : les ressorts logent dans les fenêtres.
      const n = d.dampers;
      const span = (TAU / n) * 0.62;
      const inR = l.springR - l.springD / 2 - 1;
      const outR = l.springR + l.springD / 2 + 1;
      const plate = (px0: number, px1: number, b2: MeshBuilder, r1: number, phase: number) => {
        b2.add(revolve(rectPoints(px0, px1, hR + 1, inR), 0, TAU, 48));
        b2.add(revolve(rectPoints(px0, px1, outR, r1), 0, TAU, 72));
        for (let i = 0; i < n; i++) {
          const c = phase + (i / n) * TAU;
          b2.add(revolve(rectPoints(px0, px1, inR, outR), c + span / 2, c + TAU / n - span / 2, 8));
        }
      };
      plate(x0 - 1.5, x0 + 0.2, sheet, li - 6, 0);
      plate(x0 + w - 0.2, x0 + w + 1.5, sheet, li - 6, 0);
      // Le pied de la plaque avant monte jusqu'aux segments.
      sheet.add(revolve([x0 - 1.5, li - 6.5, x0 - 1.5, li - 5.5, x0 + t + 0.45, li - 5.5, x0 + t + 0.45, li - 6.5], 0, TAU, 72));
      const flange = take(hub, 'steel');
      const fx0 = x0 + 2.2;
      const fx1 = x0 + 5.2;
      flange.add(revolve(rectPoints(fx0, fx1, hR - 2, inR), 0, TAU, 48));
      flange.add(revolve(rectPoints(fx0, fx1, outR, outR + 4), 0, TAU, 72));
      for (let i = 0; i < n; i++) {
        const c = (i / n) * TAU;
        flange.add(revolve(rectPoints(fx0, fx1, inR, outR), c + span / 2, c + TAU / n - span / 2, 8));
      }
    } else {
      // Sur un bi-masse, un simple voile : plus rien à amortir ici.
      take(hub, 'sheet').add(revolve([x0 + 2.8, hR - 2, x0 + 2.8, li - 6, x0 + t + 0.05, li - 6, x0 + t + 0.05, li - 2, x0 + t + 0.45, li - 2, x0 + t + 0.45, li - 6.5, x0 + 4.4, hR - 2], 0, TAU, 72));
    }
  }

  private buildRingGear(take: (group: PartGroup, material: PartMaterial) => MeshBuilder): void {
    const l = this.layout;
    const d = l.dims;
    const width = 12;
    const x = (d.dualMass ? -l.T + 7 : -l.T + 6);
    const matrix = m34.multiply(m34.create(), m34.translation(m34.create(), x, 0, 0), zToX);
    take('crank', 'ring').transform(matrix).gear(d.ringTeeth, d.flywheelR - 3, d.flywheelR, width, d.flywheelR - 7).transform(null);
  }

  /** Les cannelures de l'arbre, du nez jusqu'au-delà de ce que le moyeu parcourt. */
  private buildSplines(take: (group: PartGroup, material: PartMaterial) => MeshBuilder): void {
    const l = this.layout;
    const d = l.dims;
    const from = -10;
    const to = l.guideX + 2;
    const matrix = m34.multiply(m34.create(), m34.translation(m34.create(), (from + to) / 2, 0, 0), zToX);
    const group: PartGroup = l.dct ? 'k1Shaft' : 'shaft';
    take(group, 'shaft').transform(matrix).gear(d.splines, d.shaftR - 0.5, d.shaftR + 0.55, to - from).transform(null);
  }

  /** Les trois pattes du plateau, qui traversent la paroi du couvercle pour les languettes. */
  private buildPlateLugs(take: (group: PartGroup, material: PartMaterial) => MeshBuilder): void {
    const l = this.layout;
    if (l.dct) return;
    const d = l.dims;
    const b = take('plate', 'cast');
    for (let i = 0; i < 3; i++) {
      const a = (i / 3) * TAU;
      b.add(revolve(rectPoints(l.plateX + 8, l.plateX + 12.5, d.liningOuterR + 1, l.wallOut + 2), a - 0.2, a + 0.2, 6));
    }
  }

  private buildBolts(): void {
    const l = this.layout;
    const d = l.dims;
    const THREE = this.stage.THREE;
    const steel = this.materials.get('steel')!;
    const hex = new MeshBuilder().transform(yToX).cylinder(6, 5, 6).build();
    const crankX = d.dualMass ? -l.T + 6.5 : -4.5;
    this.addInstances(this.node('crank'), hex, steel, d.crankBolts, (i, m) => this.polar(m, crankX, l.boltR, (i / d.crankBolts) * TAU));
    const coverHex = new MeshBuilder().transform(yToX).cylinder(4.5, 3.5, 6).build();
    const boltR = l.flangeR - 3;
    this.addInstances(this.node('mass'), coverHex, steel, d.coverBolts, (i, m) => this.polar(m, 5, boltR, ((i + 0.5) / d.coverBolts) * TAU));
    // Rivets des garnitures : deux rangées, à fleur de chaque face. Un rivet, une pastille sombre.
    const dark = new THREE.MeshBasicMaterial({ color: 0x17181a, clippingPlanes: this.clip, clipIntersection: true });
    this.owned.push(dark);
    const cap = new MeshBuilder().transform(yToX).cylinder(2, 0.12, 10).build();
    const sectors = 16;
    const count = sectors * 2;
    const face = (group: PartGroup, x: number, ) => this.addInstances(this.node(group), cap, dark, count, (i, m) => {
      const ring = i % 2 === 0 ? d.liningInnerR + 7 : d.liningOuterR - 9;
      this.polar(m, x, ring, (((i >> 1) + 0.5) / sectors) * TAU);
    });
    if (l.dct) {
      face('k1Lining', l.dct.k1DiscX - 0.02);
      face('k1Lining', l.dct.k1DiscX + l.discWidth + 0.02);
      face('k2Lining', l.dct.k2DiscX - 0.02);
      face('k2Lining', l.dct.k2DiscX + l.discWidth + 0.02);
    } else {
      face('liningFront', -0.02);
      face('liningRear', l.discWidth + 0.02);
      // Les rivets du diaphragme, dans le fond du couvercle : ils portent les joncs.
      const rivet = new MeshBuilder().transform(yToX).cylinder(2.4, 1.4, 10).build();
      this.addInstances(this.node('mass'), rivet, steel, 9, (i, m) => this.polar(m, l.coverOut + 0.7, d.pivotR, ((i + 0.5) / 9) * TAU));
    }
  }

  private buildBearingFeatures(): void {
    const l = this.layout;
    const raceBalls = (nose: number, radial: number, cage: PartGroup, race: PartGroup, carrier: PartGroup) => {
      const pitch = BEARING_PITCH_R + radial;
      const balls = radial === 0 ? 11 : 17;
      const ball = new MeshBuilder().sphere(BEARING_BALL / 2 - 0.05, 14, 9).build();
      const steel = this.materials.get('race')!;
      const node = this.node(cage);
      this.addInstances(node, ball, steel, balls, (i, m) => this.polar(m, nose + 12, pitch, (i / balls) * TAU));
      // Cage : deux flasques, reliés entre les billes.
      const brass = this.materials.get('brass')!;
      const flanks = new MeshBuilder();
      flanks.add(revolve(rectPoints(nose + 6.4, nose + 7.8, pitch - 1.6, pitch + 1.6), 0, TAU, 48));
      flanks.add(revolve(rectPoints(nose + 16.2, nose + 17.6, pitch - 1.6, pitch + 1.6), 0, TAU, 48));
      this.addMesh(node, flanks.build(), brass);
      const bar = new MeshBuilder().box(9, 2.2, 2.4).build();
      this.addInstances(node, bar, brass, balls, (i, m) => this.polar(m, nose + 12, pitch, ((i + 0.5) / balls) * TAU));
      // Trois repères peints sur le nez de la bague tournante, pour lire sa rotation.
      const inner = pitch - BEARING_BALL / 2;
      const marks = new MeshBuilder();
      for (let i = 0; i < 3; i++) {
        const a = (i / 3) * TAU;
        marks.add(revolve(rectPoints(nose - 0.04, nose + 0.6, inner - 6, inner - 1.6), a - 0.16, a + 0.16, 4));
      }
      this.addMesh(this.node(race), marks.build(), this.raceMark);
      // Plots d'appui de la fourchette, sur le chariot.
      const lug = new MeshBuilder().box(5, 12, 10).build();
      const carrierMaterial = this.materials.get('carrier')!;
      const padX = nose + 24;
      this.addInstances(this.node(carrier), lug, carrierMaterial, 2, (i, m) => {
        const z = (i === 0 ? 1 : -1) * (pitch + 15);
        m.makeTranslation(padX - 2.5, 0, z);
      });
    };
    if (l.dct) {
      raceBalls(l.dct.k1NoseX, 0, 'k1Cage', 'k1Race', 'k1Carrier');
      raceBalls(l.dct.k2NoseX, 28, 'k2Cage', 'k2Race', 'k2Carrier');
    } else raceBalls(l.noseX, 0, 'cage', 'race', 'carrier');
  }

  private buildPilotNeedles(): void {
    const l = this.layout;
    const needle = new MeshBuilder().transform(yToX).cylinder(1.3, 8, 6).build();
    this.addInstances(this.node('crank'), needle, this.materials.get('steel')!, 14, (i, m) => this.polar(m, -l.T - 7, 11.1, (i / 14) * TAU));
  }

  /** Carter en verre, ouvert là où la fourchette sort. */
  private buildBell(): void {
    const l = this.layout;
    const ghost = this.stage.materials.ghost.clone();
    ghost.clippingPlanes = this.clip;
    ghost.clipIntersection = true;
    this.owned.push(ghost);
    const builder = new MeshBuilder();
    // Le dessus reste ouvert : la fourchette et le récepteur y passent.
    const gap = 0.3;
    builder.add(revolve(bellProfile(l), gap, TAU - gap, 80));
    // Le voile avant de la boîte : en verre lui aussi, sauf le bossage du roulement, sinon il cache tout le mécanisme.
    const wall = l.wallX;
    builder.add(revolve(rectPoints(wall, wall + 4, l.dims.shaftR + 30, 112), 0, TAU, 80));
    const mesh = this.addMesh(this.root, builder.build(), ghost);
    mesh.renderOrder = 10;
    this.addMesh(this.root, revolve(rectPoints(wall, wall + 14, l.dims.shaftR + 16, l.dims.shaftR + 30), 0, TAU, 64), this.materials.get('housing')!);
  }

  /** Le diaphragme : une couronne, ses doigts et leurs fentes. Le maillage se recalcule à chaque image selon le rayon. */
  private buildDiaphragm(): void {
    const l = this.layout;
    const d = l.dims;
    const THREE = this.stage.THREE;
    const N = d.fingers;
    const sheet = l.sheet;
    const positions: number[] = [];
    const normals: number[] = [];
    const radii: number[] = [];
    const sides: number[] = [];
    const indices: number[] = [];
    const vertex = (r: number, a: number, side: number, ny: number, nz: number, nx: number) => {
      positions.push(0, r * Math.cos(a), r * Math.sin(a));
      normals.push(nx, ny, nz);
      radii.push(r);
      sides.push(side);
      return radii.length - 1;
    };
    const faceGrid = (r0: number, r1: number, a0: number, a1: number, stepsR: number, stepsA: number) => {
      for (const side of [1, -1]) {
        const first = radii.length;
        for (let i = 0; i <= stepsR; i++) {
          const r = r0 + ((r1 - r0) * i) / stepsR;
          for (let j = 0; j <= stepsA; j++) vertex(r, a0 + ((a1 - a0) * j) / stepsA, (side * sheet) / 2, 0, 0, side);
        }
        const stride = stepsA + 1;
        for (let i = 0; i < stepsR; i++) {
          for (let j = 0; j < stepsA; j++) {
            const q = first + i * stride + j;
            if (side > 0) indices.push(q, q + 1, q + stride + 1, q, q + stride + 1, q + stride);
            else indices.push(q, q + stride + 1, q + 1, q, q + stride, q + stride + 1);
          }
        }
      }
    };
    const ringR0 = d.pivotR - 10;
    const ringR1 = d.fulcrumR + 3.5;
    const annulus = N * 3;
    faceGrid(ringR0, ringR1, 0, TAU, 4, annulus);
    // Les bords de la couronne.
    for (const r of [ringR1]) {
      const first = radii.length;
      for (let j = 0; j <= annulus; j++) {
        const a = (j / annulus) * TAU;
        vertex(r, a, sheet / 2, Math.cos(a), Math.sin(a), 0);
        vertex(r, a, -sheet / 2, Math.cos(a), Math.sin(a), 0);
      }
      for (let j = 0; j < annulus; j++) {
        const q = first + j * 2;
        indices.push(q, q + 1, q + 3, q, q + 3, q + 2);
      }
    }
    const steps = 7;
    for (let f = 0; f < N; f++) {
      const c = (f / N) * TAU;
      const half = TAU / N / 2;
      const widthAt = (r: number) => half - (r > d.pivotR - 14 ? 2.6 : 1.5) / r;
      // Chaque doigt : ses deux faces, ses deux flancs, son bout.
      for (const side of [1, -1]) {
        const first = radii.length;
        for (let i = 0; i <= steps; i++) {
          const r = d.contactR - 2 + ((ringR0 + 1 - d.contactR + 2) * i) / steps;
          const w = widthAt(r);
          vertex(r, c - w, (side * sheet) / 2, 0, 0, side);
          vertex(r, c + w, (side * sheet) / 2, 0, 0, side);
        }
        for (let i = 0; i < steps; i++) {
          const q = first + i * 2;
          if (side > 0) indices.push(q, q + 1, q + 3, q, q + 3, q + 2);
          else indices.push(q, q + 3, q + 1, q, q + 2, q + 3);
        }
      }
      for (const flank of [-1, 1]) {
        const first = radii.length;
        const ny = -Math.sin(c) * flank;
        const nz = Math.cos(c) * flank;
        for (let i = 0; i <= steps; i++) {
          const r = d.contactR - 2 + ((ringR0 + 1 - d.contactR + 2) * i) / steps;
          const a = c + flank * widthAt(r);
          vertex(r, a, sheet / 2, ny, nz, 0);
          vertex(r, a, -sheet / 2, ny, nz, 0);
        }
        for (let i = 0; i < steps; i++) {
          const q = first + i * 2;
          if (flank > 0) indices.push(q, q + 1, q + 3, q, q + 3, q + 2);
          else indices.push(q, q + 3, q + 1, q, q + 2, q + 3);
        }
      }
      const tipR = d.contactR - 2;
      const w = widthAt(tipR);
      const first = radii.length;
      const ny = -Math.cos(c);
      const nz = -Math.sin(c);
      vertex(tipR, c - w, sheet / 2, ny, nz, 0);
      vertex(tipR, c + w, sheet / 2, ny, nz, 0);
      vertex(tipR, c + w, -sheet / 2, ny, nz, 0);
      vertex(tipR, c - w, -sheet / 2, ny, nz, 0);
      indices.push(first, first + 3, first + 2, first, first + 2, first + 1);
    }
    const geometry = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(Float32Array.from(positions), 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    geometry.setAttribute('position', posAttr);
    geometry.setAttribute('normal', new THREE.BufferAttribute(Float32Array.from(normals), 3));
    // Un feuillet : pour orienter ses faces, on l'ouvre le temps du calcul, comme il sera à la position de repos.
    const shaped = positions.slice();
    for (let i = 0; i < radii.length; i++) shaped[i * 3] = sides[i];
    geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(orientFaces(shaped, normals, indices)), 1));
    this.geometries.push(geometry);
    this.triangles += indices.length / 3;
    const mesh = new THREE.Mesh(geometry, this.materials.get('lever')!);
    mesh.frustumCulled = false;
    this.node('mass').add(mesh);
    const count = radii.length;
    const r = Float32Array.from(radii);
    const s = Float32Array.from(sides);
    const array = posAttr.array as Float32Array;
    this.diaphragmUpdate = (frame) => {
      const lift = groupOffset('plate', frame);
      for (let i = 0; i < count; i++) array[i * 3] = diaphragmX(l, frame.travel, lift, r[i]) + s[i];
      posAttr.needsUpdate = true;
    };
  }

  /** Les languettes : trois paquets de lames, du couvercle à une patte du plateau. */
  private buildStraps(): void {
    const l = this.layout;
    const THREE = this.stage.THREE;
    const positions: number[] = [];
    const normals: number[] = [];
    const follow: number[] = [];
    const indices: number[] = [];
    const leaves = 3;
    const R = (l.wallOut + l.flangeR) / 2 - 1;
    for (let i = 0; i < 3; i++) {
      const plateAngle = (i / 3) * TAU;
      const coverAngle = plateAngle + 0.48;
      for (let leaf = 0; leaf < leaves; leaf++) {
        const offset = leaf * 0.9;
        const r0 = R + offset;
        const px = l.plateX + 10;
        const cx = 3.5;
        const halfW = 6;
        // Une lame plate : ses deux bouts, coupés selon l'axe et la tangente.
        const ends = [[px, r0 * Math.cos(plateAngle), r0 * Math.sin(plateAngle), 1], [cx, r0 * Math.cos(coverAngle), r0 * Math.sin(coverAngle), 0]];
        const base = positions.length / 3;
        for (const [x, y, z, f] of ends) {
          const ny = y / r0;
          const nz = z / r0;
          for (const [dx, dr] of [[-halfW, -0.35], [-halfW, 0.35], [halfW, 0.35], [halfW, -0.35]]) {
            positions.push(x + dx * 0.25, y + ny * dr, z + nz * dr);
            normals.push(0, ny, nz);
            follow.push(f);
          }
        }
        const q = (a: number, b: number, c: number, e: number) => indices.push(base + a, base + b, base + c, base + a, base + c, base + e);
        q(0, 1, 5, 4);
        q(1, 2, 6, 5);
        q(2, 3, 7, 6);
        q(3, 0, 4, 7);
        q(3, 2, 1, 0);
        q(4, 5, 6, 7);
      }
    }
    const geometry = new THREE.BufferGeometry();
    const attribute = new THREE.BufferAttribute(Float32Array.from(positions), 3);
    attribute.setUsage(THREE.DynamicDrawUsage);
    geometry.setAttribute('position', attribute);
    geometry.setAttribute('normal', new THREE.BufferAttribute(Float32Array.from(normals), 3));
    geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(orientFaces(positions, normals, indices)), 1));
    this.geometries.push(geometry);
    this.triangles += indices.length / 3;
    const mesh = new THREE.Mesh(geometry, this.materials.get('sheet')!);
    mesh.frustumCulled = false;
    this.node('mass').add(mesh);
    const base = Float32Array.from(positions);
    const f = Float32Array.from(follow);
    const array = attribute.array as Float32Array;
    this.strapUpdate = (lift) => {
      for (let i = 0; i < f.length; i++) array[i * 3] = base[i * 3] + f[i] * lift;
      attribute.needsUpdate = true;
    };
  }

  /** Deux joncs de chaque côté du diaphragme : il bascule entre eux, sans rien d'autre. */
  private buildPivotRings(): void {
    const l = this.layout;
    const d = l.dims;
    const rings = new MeshBuilder();
    rings.add(revolve(circlePoints(l.pivotX - l.sheet / 2 - 1.3, d.pivotR, 1.3, 8), 0, TAU, 72));
    rings.add(revolve(circlePoints(l.pivotX + l.sheet / 2 + 1.3, d.pivotR, 1.3, 8), 0, TAU, 72));
    this.addMesh(this.node('mass'), rings.build(), this.materials.get('steel')!);
  }

  /** La fourchette : forgée, deux branches autour du chariot, une rotule en bas, le récepteur en haut. */
  private buildFork(): void {
    const l = this.layout;
    const THREE = this.stage.THREE;
    const h = FORK_SLAVE_R - FORK_STUD_R;
    const mid = -FORK_STUD_R;
    const prong = BEARING_PITCH_R + 15;
    const b = new MeshBuilder();
    const arm = (sign: number) => {
      b.tube([[0, 6, 0], [0, mid - 46, 0], [0, mid - 40, sign * prong * 0.55], [0, mid - 18, sign * prong], [0, mid + 18, sign * prong], [0, mid + 40, sign * prong * 0.55], [0, mid + 46, 0], [0, h + 4, 0]], 4.6, 9);
    };
    arm(1);
    arm(-1);
    for (const sign of [1, -1]) {
      const m = m34.translation(m34.create(), -3.6, mid, sign * prong);
      b.transform(m).box(2.6, 14, 12).transform(null);
    }
    // Découpée comme le reste : sa branche avant ne cache plus la butée.
    const material = this.materials.get('ring')!;
    const node = new THREE.Group();
    node.position.set(l.forkX, FORK_STUD_R, 0);
    this.root.add(node);
    this.addMesh(node, b.build(), material);
    this.forkNode = node;
    // La rotule et son pied, vissés dans le carter.
    const stud = new MeshBuilder();
    stud.transform(m34.translation(m34.create(), 0, 0, 0)).sphere(6, 16, 10);
    stud.transform(m34.translation(m34.create(), 0, -24, 0)).cylinder(4, 42, 12).transform(null);
    const studNode = new THREE.Group();
    studNode.position.set(l.forkX, FORK_STUD_R, 0);
    this.root.add(studNode);
    this.addMesh(studNode, stud.build(), this.unclipped('steel'));
  }

  /** Récepteur, durite, maître-cylindre, bocal et pédale. Le verre laisse voir le liquide qui avance. */
  private buildCommand(): void {
    const l = this.layout;
    const THREE = this.stage.THREE;
    const fluid = new THREE.MeshLambertMaterial({ color: 0xc99a3c, emissive: 0x4a3510 });
    this.owned.push(fluid);
    const glass = this.stage.materials.ghost.clone();
    glass.opacity = 0.2;
    this.owned.push(glass);
    const steel = this.unclipped('steel');
    const rubber = this.unclipped('rubber');

    const cylinderAlong = (radius: number, length: number) => new MeshBuilder().transform(m34.multiply(m34.create(), m34.translation(m34.create(), length / 2, 0, 0), yToX)).cylinder(radius, length, 20).build();

    const place = (object: ThreeNamespace.Object3D, x: number, y: number) => {
      object.position.set(x, y, 0);
      this.root.add(object);
      return object;
    };
    const slaveLen = l.slaveToX - l.slaveFromX;
    const body = this.addMesh(this.root, cylinderAlong(11, slaveLen), glass);
    body.position.set(l.slaveFromX, l.slaveR, 0);
    body.renderOrder = 11;
    // Le piston, la colonne de liquide derrière lui, et la tige qui pousse la fourchette.
    this.slavePiston = place(new THREE.Group(), l.slaveToX - 12, l.slaveR);
    this.addMesh(this.slavePiston, cylinderAlong(8.8, 5), steel);
    this.slaveFluid = this.addMesh(this.root, cylinderAlong(8.3, 1), fluid);
    this.slaveFluid.position.set(l.slaveToX - 7, l.slaveR, 0);
    this.slaveRod = this.addMesh(this.root, cylinderAlong(2.2, 1), steel);
    this.slaveRod.position.set(l.forkX, l.slaveR, 0);

    const masterLen = l.masterToX - l.masterFromX;
    const master = this.addMesh(this.root, cylinderAlong(9, masterLen), glass);
    master.position.set(l.masterFromX, l.masterR, 0);
    master.renderOrder = 11;
    this.masterPiston = place(new THREE.Group(), l.masterToX - 10, l.masterR);
    this.addMesh(this.masterPiston, cylinderAlong(7, 5), steel);
    this.masterFluid = this.addMesh(this.root, cylinderAlong(6.5, 1), fluid);
    this.masterFluid.position.set(l.masterFromX + 1.5, l.masterR, 0);
    this.masterRod = this.addMesh(this.root, cylinderAlong(2, 1), steel);
    this.masterRod.position.set(l.masterToX, l.masterR, 0);

    // Le bocal, avec son niveau.
    const tank = this.addMesh(this.root, new MeshBuilder().box(24, 24, 18).build(), glass);
    tank.position.set(l.masterFromX + 20, l.masterR + 25, 0);
    tank.renderOrder = 11;
    const level = this.addMesh(this.root, new MeshBuilder().box(22, 12, 16).build(), fluid);
    level.position.set(l.masterFromX + 20, l.masterR + 20, 0);

    // La durite : de l'un à l'autre, et de petits repères qui s'y déplacent avec le volume chassé.
    const elbow = l.slaveToX + 13;
    this.addMesh(this.root, new MeshBuilder().tube([[l.slaveToX, l.slaveR, 0], [elbow - 3, l.slaveR, 0], [elbow, l.slaveR + 3, 0], [elbow, l.masterR - 3, 0], [elbow + 3, l.masterR, 0], [l.masterFromX, l.masterR, 0]], 2.2, 8).build(), rubber);
    this.hosePath.push(l.slaveToX, l.slaveR, elbow, l.slaveR, elbow, l.masterR, l.masterFromX, l.masterR);
    this.beads = this.addInstances(this.root, new MeshBuilder().sphere(2.6, 8, 6).build(), fluid, 5, (i, m) => m.makeTranslation(l.slaveToX + i * 4, l.slaveR, 0));

    // La pédale : un axe, un levier, un patin.
    const pedal = new MeshBuilder();
    pedal.tube([[0, 0, 0], [0, -130, 0]], 3.2, 10);
    pedal.transform(m34.translation(m34.create(), 0, -135, 0)).box(9, 14, 30).transform(null);
    this.pedalNode = place(new THREE.Group(), l.pedalX, l.pedalR);
    this.addMesh(this.pedalNode, pedal.build(), this.unclipped('lever'));

    // Le ressort de rappel, tendu entre le haut de la fourchette et une patte du carter.
    const coils: [number, number, number][] = [];
    for (let i = 0; i <= 70; i++) {
      const a = (i / 70) * TAU * 7;
      coils.push([(RETURN_SPRING * i) / 70, 2.4 * Math.cos(a), 2.4 * Math.sin(a)]);
    }
    const spring = new MeshBuilder();
    spring.tube(coils, 0.3, 5);
    this.returnSpring = this.addMesh(this.root, spring.build(), steel);
    this.returnSpring.position.set(l.forkX, 114, 0);
  }

  /** Les ressorts de torsion : le moyeu et les garnitures les serrent entre leurs fenêtres. */
  private buildDampers(): void {
    const l = this.layout;
    const d = l.dims;
    const n = d.dampers;
    const span = (TAU / n) * 0.62;
    const length = l.springR * span;
    const coils: [number, number, number][] = [];
    const turns = 6;
    const samples = turns * 10;
    const radius = l.springD / 2 - 1.2;
    for (let i = 0; i <= samples; i++) {
      const a = (i / samples) * TAU * turns;
      coils.push([radius * Math.sin(a), radius * Math.cos(a), length * (i / samples - 0.5)]);
    }
    const data = new MeshBuilder().tube(coils, 1.15, 6).build();
    const material = this.materials.get('steel')!;
    const node = new this.stage.THREE.Group();
    this.root.add(node);
    this.damperNode = node;
    this.dampers = this.addInstances(node, data, material, n, (i, m) => this.polar(m, 3.7, l.springR, (i / n) * TAU));
  }

  /** Les ressorts en arc d'un bi-masse : un seul maillage partagé, recourbé selon l'angle qui reste entre les masses. */
  private buildArcSprings(): void {
    const l = this.layout;
    const THREE = this.stage.THREE;
    const turns = 16;
    const samples = turns * 8;
    const rho = l.arcD / 2 - 2.2;
    // Le fil se calcule d'abord droit, à sa vraie longueur ; on le recourbe ensuite, sommet par sommet.
    const length = l.arcR * Math.PI * 0.9;
    const path: [number, number, number][] = [];
    for (let i = 0; i <= samples; i++) {
      const a = (i / samples) * TAU * turns;
      path.push([rho * Math.cos(a), rho * Math.sin(a), (length * i) / samples]);
    }
    const straight = new MeshBuilder().tube(path, 1.6, 5).build();
    const count = straight.positions.length / 3;
    const positions = new Float32Array(count * 3);
    const normals = new Float32Array(count * 3);
    const geometry = new THREE.BufferGeometry();
    const posAttr = new THREE.BufferAttribute(positions, 3);
    const normalAttr = new THREE.BufferAttribute(normals, 3);
    posAttr.setUsage(THREE.DynamicDrawUsage);
    normalAttr.setUsage(THREE.DynamicDrawUsage);
    geometry.setAttribute('position', posAttr);
    geometry.setAttribute('normal', normalAttr);
    geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(straight.indices), 1));
    this.geometries.push(geometry);
    this.triangles += (straight.indices.length / 3) * 2;
    const sp = Float32Array.from(straight.positions);
    const sn = Float32Array.from(straight.normals!);
    const arcR = l.arcR;
    const arcX = l.arcX;
    this.arcUpdate = (span) => {
      for (let i = 0; i < count; i++) {
        const a = sp[i * 3];
        const b = sp[i * 3 + 1];
        const u = sp[i * 3 + 2] / length;
        // Le long de l'arc : cos/sin tirés une fois par sommet, pour deux ressorts.
        const theta = span * (u - 0.5);
        const c = Math.cos(theta);
        const s = Math.sin(theta);
        const R = arcR + a;
        positions[i * 3] = arcX + b;
        positions[i * 3 + 1] = R * c;
        positions[i * 3 + 2] = R * s;
        const nx = sn[i * 3];
        const ny = sn[i * 3 + 1];
        const nz = sn[i * 3 + 2];
        normals[i * 3] = ny;
        normals[i * 3 + 1] = nx * c - nz * s;
        normals[i * 3 + 2] = nx * s + nz * c;
      }
      posAttr.needsUpdate = true;
      normalAttr.needsUpdate = true;
    };
    const material = this.materials.get('steel')!;
    for (let i = 0; i < 2; i++) {
      const node = new THREE.Group();
      this.root.add(node);
      const mesh = new THREE.Mesh(geometry, material);
      mesh.frustumCulled = false;
      node.add(mesh);
      this.arcNodes.push(node);
    }
    // Les bras du voile et les butées de la primaire, entre les ressorts.
    const arm = new MeshBuilder().box(2.4, l.arcR - 34, 14).build();
    this.addInstances(this.node('mass'), arm, this.materials.get('steel')!, 2, (i, m) => {
      const a = Math.PI / 2 + i * Math.PI;
      this.polar(m, l.arcX, 34 + (l.arcR - 34) / 2, a);
    });
    const stop = new MeshBuilder().box(26, 22, 6).build();
    this.addInstances(this.node('crank'), stop, this.materials.get('sheet')!, 2, (i, m) => {
      const a = Math.PI / 2 + i * Math.PI;
      this.polar(m, l.arcX, l.arcR, a);
    });
  }

  /** Les leviers d'un double embrayage : la même tôle que le diaphragme, en plus simple. */
  private buildDctLevers(): void {
    const l = this.layout;
    const c = l.dct!;
    const THREE = this.stage.THREE;
    const updates: ((frame: ClutchFrame) => void)[] = [];
    for (const k1 of [true, false]) {
      const tipR = k1 ? c.k1LeverTipR : c.k2LeverTipR;
      const pivotR = k1 ? c.tieRodR : c.k2LeverPivotR;
      const N = 12;
      const positions: number[] = [];
      const normals: number[] = [];
      const radii: number[] = [];
      const sides: number[] = [];
      const indices: number[] = [];
      for (let f = 0; f < N; f++) {
        const a0 = (f / N) * TAU + 0.03;
        const a1 = ((f + 1) / N) * TAU - 0.03;
        for (const side of [1.2, -1.2]) {
          const first = radii.length;
          for (const [r, a] of [[tipR, a0], [tipR, a1], [pivotR, a1], [pivotR, a0]] as const) {
            positions.push(0, r * Math.cos(a), r * Math.sin(a));
            normals.push(side > 0 ? 1 : -1, 0, 0);
            radii.push(r);
            sides.push(side);
          }
          if (side > 0) indices.push(first, first + 1, first + 2, first, first + 2, first + 3);
          else indices.push(first, first + 2, first + 1, first, first + 3, first + 2);
        }
      }
      const geometry = new THREE.BufferGeometry();
      const attribute = new THREE.BufferAttribute(Float32Array.from(positions), 3);
      attribute.setUsage(THREE.DynamicDrawUsage);
      geometry.setAttribute('position', attribute);
      geometry.setAttribute('normal', new THREE.BufferAttribute(Float32Array.from(normals), 3));
      const shaped = positions.slice();
      for (let i = 0; i < radii.length; i++) shaped[i * 3] = sides[i];
      geometry.setIndex(new THREE.BufferAttribute(Uint32Array.from(orientFaces(shaped, normals, indices)), 1));
      this.geometries.push(geometry);
      this.triangles += indices.length / 3;
      const mesh = new THREE.Mesh(geometry, this.materials.get('lever')!);
      mesh.frustumCulled = false;
      this.node('mass').add(mesh);
      const count = radii.length;
      const r = Float32Array.from(radii);
      const s = Float32Array.from(sides);
      const array = attribute.array as Float32Array;
      updates.push((frame) => {
        for (let i = 0; i < count; i++) array[i * 3] = dctLeverX(c, frame, k1, r[i]) + s[i];
        attribute.needsUpdate = true;
      });
    }
    const [one, two] = updates;
    this.diaphragmUpdate = (frame) => {
      one(frame);
      two(frame);
    };
  }

  private buildSparks(): void {
    const THREE = this.stage.THREE;
    const material = new THREE.MeshBasicMaterial({ color: 0xffd9a0, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false });
    this.owned.push(material);
    const data = new MeshBuilder().box(1.6, 1.6, 1.6).build();
    this.sparks = this.addInstances(this.root, data, material, 16, (_, m) => m.makeScale(0, 0, 0));
    this.sparks.renderOrder = 12;
  }

  private nextSpark(): number {
    this.sparkSeed = (this.sparkSeed * 1664525 + 1013904223) | 0;
    return (this.sparkSeed >>> 16) / 65536;
  }

  /** Une image : positions, angles et chaleur. Ne touche pas au rendu. */
  update(frame: ClutchFrame, live: ClutchLive): void {
    const l = this.layout;
    const list = this.nodeList;
    for (let i = 0; i < list.length; i++) {
      const entry = list[i];
      entry.node.position.x = groupOffset(entry.group, frame);
      entry.node.rotation.x = groupAngle(entry.group, frame);
    }
    this.diaphragmUpdate?.(frame);
    this.strapUpdate?.(groupOffset('plate', frame));

    if (this.damperNode && this.dampers) {
      const n = l.dims.dampers;
      this.damperNode.rotation.x = (frame.liningAngle + frame.hubAngle) / 2;
      this.damperNode.position.x = groupOffset('hub', frame);
      const span = (TAU / n) * 0.62;
      const squeeze = (span - Math.abs(frame.damperWindup)) / span;
      for (let i = 0; i < n; i++) {
        const a = (i / n) * TAU;
        this.quaternion.setFromAxisAngle(this.axisX, a);
        this.position.set(3.7, l.springR * Math.cos(a), l.springR * Math.sin(a));
        this.scale3.set(1, 1, squeeze);
        this.matrix.compose(this.position, this.quaternion, this.scale3);
        this.dampers.setMatrixAt(i, this.matrix);
      }
      this.dampers.instanceMatrix.needsUpdate = true;
    }

    if (this.arcUpdate) {
      const span = Math.PI * 0.9 - Math.abs(frame.dualMassWindup);
      this.arcUpdate(span);
      const mid = frame.crankAngle - frame.dualMassWindup / 2;
      this.arcNodes[0].rotation.x = mid;
      this.arcNodes[1].rotation.x = mid + Math.PI;
    }

    this.updateCommand(frame);
    this.updateHeat(live, frame);
  }

  private updateCommand(frame: ClutchFrame): void {
    const l = this.layout;
    const t = frame.travel;
    if (this.forkNode) this.forkNode.rotation.z = t.forkAngle;
    if (this.slavePiston && this.slaveFluid && this.slaveRod) {
      const piston = l.slaveToX - 12 - t.slave;
      this.slavePiston.position.x = piston;
      this.slaveFluid.position.x = piston + 5;
      this.slaveFluid.scale.x = l.slaveToX - piston - 7;
      const rodFrom = l.forkX - (l.slaveR - FORK_STUD_R) * t.forkAngle;
      this.slaveRod.position.x = rodFrom;
      this.slaveRod.scale.x = piston - rodFrom;
    }
    let pedalAngle = 0;
    if (this.masterPiston && this.masterFluid && this.masterRod && this.pedalNode) {
      const piston = l.masterToX - 10 - t.master;
      this.masterPiston.position.x = piston;
      this.masterFluid.scale.x = piston - l.masterFromX - 1.5;
      const rest = (12 * Math.PI) / 180;
      pedalAngle = Math.asin(Math.max(-1, Math.min(1, Math.sin(rest) - t.master / 52)));
      this.pedalNode.rotation.z = pedalAngle;
      const rodX = l.pedalX + 52 * Math.sin(pedalAngle);
      const rodY = l.pedalR - 52 * Math.cos(pedalAngle);
      const from = piston + 5;
      const dx = rodX - from;
      const dy = rodY - l.masterR;
      this.masterRod.position.x = from;
      this.masterRod.rotation.z = Math.atan2(dy, dx);
      this.masterRod.scale.x = Math.hypot(dx, dy);
    }
    if (this.returnSpring) {
      const from = l.forkX - (114 - FORK_STUD_R) * t.forkAngle + 5;
      this.returnSpring.position.x = from;
      this.returnSpring.scale.x = (l.forkX + 34 - from) / RETURN_SPRING;
    }
    if (this.beads) this.placeBeads(t.master);
  }

  /** Chaque bille de liquide avance sur la durite du volume chassé par le maître-cylindre, et revient avec lui. */
  private placeBeads(master: number): void {
    const path = this.hosePath;
    const beads = this.beads!;
    let total = 0;
    for (let i = 0; i + 3 < path.length; i += 2) total += Math.abs(path[i + 2] - path[i]) + Math.abs(path[i + 3] - path[i + 1]);
    for (let b = 0; b < 5; b++) {
      let along = ((b / 5) * total + total - (master / 30) * (total / 5)) % total;
      let x = path[0];
      let y = path[1];
      for (let i = 0; i + 3 < path.length; i += 2) {
        const seg = Math.abs(path[i + 2] - path[i]) + Math.abs(path[i + 3] - path[i + 1]);
        if (along <= seg) {
          const f = seg > 0 ? along / seg : 0;
          x = path[i] + (path[i + 2] - path[i]) * f;
          y = path[i + 1] + (path[i + 3] - path[i + 1]) * f;
          break;
        }
        along -= seg;
      }
      this.matrix.makeTranslation(x, y, 0);
      beads.setMatrixAt(b, this.matrix);
    }
    beads.instanceMatrix.needsUpdate = true;
  }

  /** Friction, plateau et volant prennent la température calculée. La couleur vient du même barème qu'en 2D. */
  private updateHeat(live: ClutchLive, frame: ClutchFrame): void {
    const heat = live.heat;
    const c = this.frictionMaterial.color;
    if (heat < 150) c.setRGB(0.165, 0.09, 0.043);
    else if (heat < 300) {
      const f = (heat - 150) / 150;
      c.setRGB(0.165 + 0.2 * f, 0.09 - 0.05 * f, 0.043 - 0.03 * f);
    } else {
      const f = Math.min(1, (heat - 300) / 150);
      c.setRGB(0.365 + 0.63 * f, 0.04 + 0.2 * f, 0.013 + 0.02 * f);
    }
    const glow = heat > 120 ? Math.min(1, (heat - 120) / 400) : 0;
    this.hotColor.setRGB(glow, glow * 0.25, glow * 0.04);
    this.frictionMaterial.emissive.copy(this.hotColor);
    this.frictionMaterial.emissiveIntensity = glow * 1.4;
    this.plateMaterial.emissive.copy(this.hotColor);
    this.plateMaterial.emissiveIntensity = glow * 0.7;
    this.massMaterial.emissive.copy(this.hotColor);
    this.massMaterial.emissiveIntensity = glow * 0.5;
    this.raceMark.emissive.setRGB(0.8, 0.35, 0.1);
    this.raceMark.emissiveIntensity = frame.travel.contact || this.layout.dct ? 0.8 : 0;

    const sparks = this.sparks;
    if (sparks) {
      const d = this.layout.dims;
      const count = live.slipKw > 1.5 ? Math.min(16, Math.floor(live.slipKw * 1.2)) : 0;
      if (live.frozen) this.sparkSeed = 7;
      const x0 = groupOffset('liningFront', frame);
      const x1 = groupOffset('liningRear', frame) + this.layout.discWidth;
      for (let i = 0; i < 16; i++) {
        if (i < count) {
          const a = this.nextSpark() * TAU;
          const r = d.liningInnerR + this.nextSpark() * (d.liningOuterR - d.liningInnerR);
          this.matrix.makeTranslation(i % 2 === 0 ? x0 : x1, r * Math.cos(a), r * Math.sin(a));
        } else this.matrix.makeScale(0, 0, 0);
        sparks.setMatrixAt(i, this.matrix);
      }
      sparks.instanceMatrix.needsUpdate = true;
      sparks.visible = count > 0;
    }
  }

  dispose(): void {
    this.stage.scene.remove(this.root);
    for (const geometry of this.geometries) geometry.dispose();
    for (const material of this.owned) material.dispose();
    for (const mesh of [this.dampers, this.beads, this.sparks]) mesh?.dispose();
  }
}
