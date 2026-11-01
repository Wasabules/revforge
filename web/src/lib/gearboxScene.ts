/**
 * La boîte en trois.js : chaque pièce de `buildParts` devient un maillage, chaque corps une matrice qu'on écrit à
 * chaque image, sans allouer. Une reconstruction libère tout ce qu'elle a créé, sauf les matériaux de la scène.
 */
import type * as ThreeNamespace from 'three';
import type { Theme } from './canvas';
import { DMF_MAX_DEG, type GearboxLayout, type GearboxMotion } from './gearboxModel';
import { buildParts, type MaterialKey, type Part } from './gearboxGeometry';
import { disposeObject, parseColor, toGeometry, type SharedMaterials, type Stage3D } from './render3d';

/** Les roulements : la cage tourne à un peu moins de la moitié de la vitesse de l'arbre. */
const CAGE_SPEED = 0.42;

interface PlacedPart {
  part: Part;
  mesh: ThreeNamespace.Mesh;
}

/** Ce qui ne bouge pas selon le thème mais doit se voir : la roue en prise, la présélectionnée, l'huile, le danger. */
type AnyMaterial = ThreeNamespace.MeshStandardMaterial | ThreeNamespace.MeshLambertMaterial;

interface OwnMaterials {
  engaged: AnyMaterial;
  preselected: AnyMaterial;
  danger: AnyMaterial;
  hot: AnyMaterial;
  oil: AnyMaterial;
  bronze: AnyMaterial;
  /** Le carter du train : coupé par un plan fixe, pour voir planétaires et satellites. */
  cutForged: AnyMaterial;
  cutSteel: AnyMaterial;
  cutAlloy: AnyMaterial;
  cutCasting: AnyMaterial;
  /** En rendu logiciel, à la place des matériaux partagés de la scène. */
  softSteel: AnyMaterial | null;
  softForged: AnyMaterial | null;
  softCasting: AnyMaterial | null;
  softAlloy: AnyMaterial | null;
  softGhost: AnyMaterial | null;
}

export function makeOwnMaterials(stage: Stage3D): OwnMaterials {
  const { THREE, software } = stage;
  // Rendu logiciel : le métal réfléchissant coûte cher à chaque pixel. Un éclairage simple suffit pour lire la mécanique.
  const surface = (color: number, metalness: number, roughness: number, extra: ThreeNamespace.MeshStandardMaterialParameters = {}): AnyMaterial =>
    software ? new THREE.MeshLambertMaterial({ color, ...(extra as ThreeNamespace.MeshLambertMaterialParameters) }) : new THREE.MeshStandardMaterial({ color, metalness, roughness, ...extra });
  const metal = (color: number, roughness = 0.42) => surface(color, 0.8, roughness);
  // Ce qui reste du côté z < 2 mm est dessiné : on regarde la moitié de pièce restante, de l'intérieur.
  const plane = new THREE.Plane(new THREE.Vector3(0, 0, -1), 2);
  const cut = (color: number, roughness: number) => surface(color, 0.45, roughness, { clippingPlanes: [plane], side: THREE.DoubleSide });
  return {
    engaged: metal(0xd77a35),
    preselected: metal(0x4f8fc4),
    danger: metal(0xd23b2f),
    hot: metal(0xf0b84c, 0.3),
    oil: surface(0xc9892a, 0, 0.25, { transparent: true, opacity: 0.13, depthWrite: false, side: THREE.DoubleSide }),
    bronze: metal(0xc69a52, 0.35),
    cutForged: cut(0x8a9299, 0.45),
    cutSteel: cut(0xb9c0c7, 0.35),
    cutAlloy: cut(0xc7a06a, 0.42),
    cutCasting: cut(0x6c7176, 0.7),
    softSteel: software ? metal(0xb4bac1) : null,
    softForged: software ? metal(0x7d858d) : null,
    softCasting: software ? metal(0x777c80) : null,
    softAlloy: software ? metal(0xc9ccce) : null,
    softGhost: software ? surface(0x8c98a6, 0.1, 0.35, { transparent: true, opacity: 0.12, depthWrite: false, side: THREE.DoubleSide }) : null,
  };
}

export function disposeOwnMaterials(own: OwnMaterials): void {
  for (const material of Object.values(own)) material?.dispose();
}

export class GearboxScene {
  readonly group: ThreeNamespace.Group;
  private readonly placed: PlacedPart[] = [];
  private readonly wheelMeshes: (PlacedPart | null)[] = [];
  readonly triangles: number;
  readonly drawCalls: number;
  private readonly springCoils: ThreeNamespace.InstancedMesh | null = null;
  private readonly springMatrix: ThreeNamespace.Matrix4 | null = null;
  private readonly materials: Record<MaterialKey, ThreeNamespace.Material>;
  private lastAccent = '';
  private readonly missingTooth: number | undefined;

  constructor(private readonly stage: Stage3D, private readonly layout: GearboxLayout, private readonly own: OwnMaterials, options: { missingTooth?: number } = {}) {
    const { THREE } = stage;
    this.missingTooth = options.missingTooth;
    this.group = new THREE.Group();
    this.group.matrixAutoUpdate = false;
    const shared = stage.materials;
    this.materials = {
      steel: own.softSteel ?? shared.steel,
      forged: own.softForged ?? shared.forged,
      casting: own.softCasting ?? shared.casting,
      alloy: own.softAlloy ?? shared.alloy,
      rubber: shared.rubber,
      brass: own.bronze,
      ghost: own.softGhost ?? shared.ghost,
      oil: own.oil,
      hub: own.softAlloy ?? shared.alloy,
      cutForged: own.cutForged,
      cutSteel: own.cutSteel,
      cutAlloy: own.cutAlloy,
      cutCasting: own.cutCasting,
    };
    stage.renderer.localClippingEnabled = layout.planets.length > 0;
    let triangles = 0;
    for (const part of buildParts(layout, options)) {
      if (part.data.indices.length === 0) continue;
      const geometry = toGeometry(THREE, part.data);
      const mesh = new THREE.Mesh(geometry, this.materials[part.material]);
      mesh.matrixAutoUpdate = false;
      mesh.frustumCulled = false;
      if (part.material === 'ghost' || part.material === 'oil') mesh.renderOrder = 2;
      this.group.add(mesh);
      const placed = { part, mesh };
      this.placed.push(placed);
      if (part.kind === 'wheel' && part.wheel !== undefined) this.wheelMeshes[part.wheel] = placed;
      triangles += part.data.indices.length / 3;
    }
    if (layout.flywheel === 'dual_mass' && layout.form !== 'converter') {
      // Les ressorts en arc : une spire de tore, répétée. Écrire leurs matrices coûte moins que redessiner un ressort.
      const geom = layout.flywheelGeom;
      const R = geom.radius;
      const coil = new THREE.TorusGeometry(R * 0.085, Math.max(R * 0.012, 0.7), 6, 12);
      const count = 2 * SPRING_COILS;
      const instanced = new THREE.InstancedMesh(coil, own.softSteel ?? shared.steel, count);
      instanced.frustumCulled = false;
      instanced.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
      this.group.add(instanced);
      this.springCoils = instanced;
      this.springMatrix = new THREE.Matrix4();
      triangles += coil.index ? (coil.index.count / 3) * count : 0;
    }
    this.triangles = triangles;
    this.drawCalls = this.placed.length + (this.springCoils ? 1 : 0);
    stage.scene.add(this.group);
    this.placeStatic();
  }

  /** Le centre de ce qu'il y a à voir, et son rayon, pour la caméra. */
  frame(aspect = 2, yaw = 0.62, pitch = 0.34): { x: number; y: number; z: number; radius: number } {
    const { layout } = this;
    const b = layout.bounds;
    let x0 = b.x0;
    let x1 = b.x1;
    let y0 = b.y0;
    let y1 = b.y1;
    let z0 = b.z0;
    let z1 = b.z1;
    const g = layout.flywheelGeom;
    if (g.radius > 0) {
      x0 = Math.min(x0, g.x - g.width / 2);
      y1 = Math.max(y1, g.y + g.radius);
      y0 = Math.min(y0, g.y - g.radius);
      z0 = Math.min(z0, -g.radius);
      z1 = Math.max(z1, g.radius);
    }
    if (layout.final) {
      const ring = layout.wheels[layout.final.ring];
      const r = ring.radius + ring.moduleN * 2;
      if (layout.final.kind === 'bevel') {
        x1 = Math.max(x1, layout.final.x + r);
        y0 = Math.min(y0, layout.final.y - r);
        y1 = Math.max(y1, layout.final.y + r);
      } else {
        y0 = Math.min(y0, layout.final.y - r);
        z0 = Math.min(z0, layout.final.z - r);
      }
    }
    // Ce qui se voit de cet angle : le cadre étant large, c'est souvent la hauteur qui décide.
    const horizontal = (x1 - x0) * Math.abs(Math.cos(yaw)) + (z1 - z0) * Math.abs(Math.sin(yaw));
    const vertical = (y1 - y0) * Math.abs(Math.cos(pitch)) + ((x1 - x0) * Math.abs(Math.sin(yaw)) + (z1 - z0) * Math.abs(Math.cos(yaw))) * Math.abs(Math.sin(pitch));
    // La caméra recule selon le plus petit de ses deux champs : on exprime tout dans celui-là.
    const fit = aspect >= 1 ? Math.max(vertical, horizontal / aspect) : Math.max(horizontal, vertical * aspect);
    const radius = 0.5 * fit * (aspect >= 1 ? 0.94 : 1.12);
    return { x: (x0 + x1) / 2, y: (y0 + y1) / 2, z: (z0 + z1) / 2, radius };
  }

  private placeStatic(): void {
    for (const { part, mesh } of this.placed) {
      if (part.kind === 'static' || part.kind === 'casing') {
        mesh.matrix.identity();
        mesh.matrixWorldNeedsUpdate = true;
      }
    }
    this.group.matrix.identity();
  }

  /** Teinte la peinture d'accent quand le thème change : la paire en prise garde la couleur du site. */
  setTheme(theme: Theme): void {
    if (theme.accent === this.lastAccent) return;
    this.lastAccent = theme.accent;
    const { THREE } = this.stage;
    const [r, g, b] = parseColor(theme.accent);
    this.own.engaged.color.setRGB(r, g, b, THREE.SRGBColorSpace);
    const [cr, cg, cb] = parseColor(theme.cool);
    this.own.preselected.color.setRGB(cr, cg, cb, THREE.SRGBColorSpace);
    const [dr, dg, db] = parseColor(theme.danger);
    this.own.danger.color.setRGB(dr, dg, db, THREE.SRGBColorSpace);
  }

  /** À chaque image : la position de chaque corps. Aucune allocation. */
  update(kin: GearboxMotion, flags: { wear: number; broken: boolean; grinding: boolean; syncing: boolean; casing: boolean }): void {
    const { layout, placed, own } = this;
    const angles = kin.angles;
    this.carrierAngle = angles[layout.output] ?? 0;
    for (let i = 0; i < placed.length; i++) {
      const { part, mesh } = placed[i];
      switch (part.kind) {
        case 'wheel': {
          const wheel = layout.wheels[part.wheel!];
          const body = layout.bodies[wheel.body];
          let x = 0;
          if (wheel.role === 'reverseIdler' && layout.reverse) x = (1 - kin.reverseIn) * layout.reverse.slide;
          this.place(mesh, body.id, angles[body.id], x);
          this.tint(mesh, part, wheel.gear, kin, flags);
          break;
        }
        case 'body':
          this.place(mesh, part.body, angles[part.body], 0);
          break;
        case 'balls':
          this.place(mesh, part.body, angles[part.body] * CAGE_SPEED, 0);
          break;
        case 'sleeve': {
          const selector = layout.selectors[part.selector!];
          const offset = (kin.pos[part.selector!] + (part.selector === kin.syncSelector ? kin.shake : 0)) * selector.travel;
          this.place(mesh, selector.shaftBody, angles[part.body], offset);
          mesh.material = flags.grinding && part.selector === kin.syncSelector ? own.danger : kin.clunk > 0.3 && Math.abs(kin.pos[part.selector!]) > 0.85 ? own.hot : this.materials[part.material];
          break;
        }
        case 'ring': {
          const selector = layout.selectors[part.selector!];
          const pos = kin.pos[part.selector!];
          // La bague est poussée par le baladeur, et s'arrête sur le cône.
          const pushed = Math.sign(pos) === part.side ? Math.min(Math.abs(pos), 0.55) : 0;
          this.place(mesh, selector.shaftBody, angles[part.body], part.side! * pushed * selector.travel * 0.7);
          mesh.material = part.selector === kin.syncSelector && kin.syncGapRpm !== 0 && Math.abs(pos) > 0.4 && Math.sign(pos) === part.side ? own.hot : this.materials[part.material];
          break;
        }
        case 'fork': {
          const selector = layout.selectors[part.selector!];
          mesh.matrix.identity();
          mesh.matrix.elements[12] = selector.x + kin.pos[part.selector!] * selector.travel;
          mesh.matrixWorldNeedsUpdate = true;
          break;
        }
        case 'idlerFork':
          mesh.matrix.identity();
          mesh.matrix.elements[12] = (1 - kin.reverseIn) * (layout.reverse?.slide ?? 0);
          mesh.matrixWorldNeedsUpdate = true;
          break;
        case 'drum': {
          const drum = layout.drum!;
          this.setMatrix(mesh, kin.drum * drum.step, 0, drum.y, drum.z);
          break;
        }
        case 'pawl':
          mesh.matrix.identity();
          mesh.matrix.elements[13] = -kin.pawl * (layout.drum?.radius ?? 0) * 0.3;
          mesh.matrixWorldNeedsUpdate = true;
          break;
        case 'casing':
          mesh.visible = flags.casing;
          break;
        case 'static':
          break;
      }
    }
    this.updateSprings(kin);
  }

  private tint(mesh: ThreeNamespace.Mesh, part: Part, gear: number, kin: GearboxMotion, flags: { wear: number; broken: boolean }): void {
    const { own } = this;
    if (gear !== 0 && gear === kin.engaged) mesh.material = flags.wear > 0.6 ? own.danger : own.engaged;
    else if (gear !== 0 && gear === kin.preselected) mesh.material = own.preselected;
    else if (flags.broken && part.wheel === this.missingTooth) mesh.material = own.danger;
    else mesh.material = this.materials[part.material];
  }

  private place(mesh: ThreeNamespace.Mesh, bodyId: number, angle: number, x: number): void {
    const body = this.layout.bodies[bodyId];
    if (body.orbit) {
      const carrier = this.carrierAngle;
      const a = carrier + body.orbit.angle;
      this.setMatrix(mesh, angle, x, body.y + body.orbit.radius * Math.cos(a), body.z + body.orbit.radius * Math.sin(a));
      return;
    }
    if (body.axis === 'z') {
      // Le pont conique : l'axe local x devient z ; y et z locaux deviennent x et y.
      const c = Math.cos(angle);
      const s = Math.sin(angle);
      const e = mesh.matrix.elements;
      e[0] = 0; e[1] = 0; e[2] = 1; e[3] = 0;
      e[4] = c; e[5] = s; e[6] = 0; e[7] = 0;
      e[8] = -s; e[9] = c; e[10] = 0; e[11] = 0;
      e[12] = body.x; e[13] = body.y; e[14] = body.z; e[15] = 1;
      mesh.matrixWorldNeedsUpdate = true;
      return;
    }
    this.setMatrix(mesh, angle, x, body.y, body.z);
  }

  private setMatrix(mesh: ThreeNamespace.Mesh, angle: number, x: number, y: number, z: number): void {
    const c = Math.cos(angle);
    const s = Math.sin(angle);
    const e = mesh.matrix.elements;
    e[0] = 1; e[1] = 0; e[2] = 0; e[3] = 0;
    e[4] = 0; e[5] = c; e[6] = s; e[7] = 0;
    e[8] = 0; e[9] = -s; e[10] = c; e[11] = 0;
    e[12] = x; e[13] = y; e[14] = z; e[15] = 1;
    mesh.matrixWorldNeedsUpdate = true;
  }

  private carrierAngle = 0;

  private updateSprings(kin: GearboxMotion): void {
    const coils = this.springCoils;
    const m = this.springMatrix;
    if (!coils || !m) return;
    const { layout } = this;
    const geom = layout.flywheelGeom;
    const primary = kin.angles[layout.flywheelBody];
    const windup = (kin.windup * DMF_MAX_DEG * Math.PI) / 180;
    const R = geom.radius * 0.72;
    let index = 0;
    for (let spring = 0; spring < 2; spring++) {
      const start = primary + spring * Math.PI - SPRING_ARC / 2;
      // La patte de la masse secondaire pousse l'autre bout : l'arc se raccourcit, les spires se serrent.
      const arc = SPRING_ARC - Math.abs(windup);
      for (let k = 0; k < SPRING_COILS; k++) {
        const a = start + ((k + 0.5) / SPRING_COILS) * arc;
        // Le plan de la spire contient le rayon et l'axe du volant : le tore (plan xy) est tourné pour regarder le long de l'arc.
        const c = Math.cos(a);
        const s = Math.sin(a);
        const e = m.elements;
        e[0] = 1; e[1] = 0; e[2] = 0; e[3] = 0;
        e[4] = 0; e[5] = c; e[6] = s; e[7] = 0;
        e[8] = 0; e[9] = -s; e[10] = c; e[11] = 0;
        e[12] = geom.x + geom.width * 0.1; e[13] = geom.y + R * c; e[14] = R * s; e[15] = 1;
        coils.setMatrixAt(index++, m);
      }
    }
    coils.instanceMatrix.needsUpdate = true;
  }

  dispose(): void {
    this.stage.scene.remove(this.group);
    const keep: SharedMaterials = { ...this.stage.materials, ...(this.own as unknown as Partial<SharedMaterials>) };
    disposeObject(this.group, keep);
    this.placed.length = 0;
  }
}

const SPRING_COILS = 11;
const SPRING_ARC = 2.8;

