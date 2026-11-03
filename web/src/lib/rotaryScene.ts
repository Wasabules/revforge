/**
 * Le rotatif en 3D : carter en épitrochoïde, rotors triangulaires, arbre excentrique, pignon fixe et couronne, générés depuis
 * `RotaryLayout`, en millimètres, et mus par `RotaryPose`. Vue de face : x à droite, y en haut, z vers l'avant.
 * Ni piston, ni bielle, ni came : le gaz se lit dans les chambres. Rien n'alloue dans `update`.
 */
import type * as T from 'three';
import type { Theme } from './canvas';
import type { Motion } from './motion';
import { disposeObject, m34, type Mat34, MeshBuilder, type Stage3D, toGeometry } from './render3d';
import { EXHAUST_PORT_U, EXHAUST_PORT_WIDTH_U, INTAKE_PORT_U, type RotaryLayout, type RotaryPose } from './rotary';

type P2 = [number, number];
type P3 = [number, number, number];

const DEG = Math.PI / 180;

/** T(x, y, z) · Rx(90°) : l'axe Y d'un tour devient l'axe Z du moteur, vers l'avant. */
function onZ(x: number, y: number, z: number): Mat34 {
  const m = m34.rotationX(m34.create(), Math.PI / 2);
  m[3] = x;
  m[7] = y;
  m[11] = z;
  return m;
}

const tr = (x: number, y: number, z: number) => m34.translation(m34.create(), x, y, z);

/** Un cercle en n points, dans le sens trigonométrique. */
function circle(radius: number, points: number): P2[] {
  const out: P2[] = [];
  for (let k = 0; k < points; k++) {
    const a = (k / points) * Math.PI * 2;
    out.push([Math.cos(a) * radius, Math.sin(a) * radius]);
  }
  return out;
}

/** Une denture intérieure en n points, sens trigonométrique, une dent toutes les n/teeth positions. */
function internalTeeth(teeth: number, pitch: number, depth: number, points: number): P2[] {
  const perTooth = points / teeth;
  const out: P2[] = [];
  for (let k = 0; k < points; k++) {
    const a = (k / points) * Math.PI * 2;
    const inTooth = (k % perTooth) / perTooth;
    const radius = inTooth < 0.25 || inTooth >= 0.75 ? pitch - depth : pitch + depth;
    out.push([Math.cos(a) * radius, Math.sin(a) * radius]);
  }
  return out;
}

export interface RotaryPartLabel {
  key: string;
  x: number;
  y: number;
  z: number;
}

export class RotaryScene {
  readonly root: T.Group;
  readonly labels: RotaryPartLabel[] = [];
  center: P3 = [0, 0, 0];
  radius = 240;
  /** Pour les tests : ce qui brille à cette image. */
  drawnSparks = 0;
  drawnFlames = 0;
  private readonly stage: Stage3D;
  private readonly shaft: T.Group;
  private readonly rotors: T.Group[] = [];
  private readonly pinion: T.Mesh;
  private readonly sparks: T.Mesh[] = [];
  private readonly chambers: { mesh: T.Mesh; material: T.MeshBasicMaterial; positions: Float32Array; attribute: T.BufferAttribute }[] = [];
  private readonly layout: RotaryLayout;
  private readonly rgb = [0, 0, 0];

  constructor(stage: Stage3D, layout: RotaryLayout) {
    this.stage = stage;
    this.layout = layout;
    const { THREE, materials } = stage;
    const geo = (builder: MeshBuilder) => toGeometry(THREE, builder.build());
    this.root = new THREE.Group();
    stage.scene.add(this.root);
    const { radius: r, eccentricity: e, width: b } = layout;

    // ── Ce qui tourne avec l'arbre : tourillons, excentriques, poulie, volant ──
    this.shaft = new THREE.Group();
    this.root.add(this.shaft);
    const shaft = new MeshBuilder();
    const front = layout.frontZ + b * 0.35;
    const rear = layout.ring.z;
    shaft.transform(onZ(0, 0, (front + rear) / 2)).cylinder(layout.mainRadius, front - rear, 28);
    for (const slot of layout.slots) {
      // À l'arbre A, le centre du rotor est à A − firingDeg : l'excentrique est décalé d'autant, une fois pour toutes.
      const a = -slot.firingDeg * DEG;
      shaft.transform(onZ(e * Math.sin(a), e * Math.cos(a), slot.z)).cylinder(layout.lobeRadius, b * 0.76, 36);
    }
    this.shaft.add(new THREE.Mesh(geo(shaft), materials.steel));
    // Une poulie étroite : elle laisse voir le pignon fixe et la couronne, derrière elle.
    const pulley = new MeshBuilder().transform(onZ(0, 0, layout.frontZ + b * 0.24)).cylinder(layout.mainRadius * 1.25, b * 0.16, 36);
    this.shaft.add(new THREE.Mesh(geo(pulley), materials.alloy));
    const ring = layout.ring;
    const flywheel = new MeshBuilder();
    flywheel.transform(onZ(0, 0, ring.z)).cylinder(ring.radius - 4, ring.width, 48);
    flywheel.transform(tr(0, 0, ring.z)).gear(ring.teeth, ring.radius - 2.2, ring.radius + 2.2, ring.width, ring.radius - 6);
    this.shaft.add(new THREE.Mesh(geo(flywheel), materials.forged));

    // ── Les rotors ──
    const points = 120;
    const counter = [...layout.rotor].reverse() as P2[];
    const body = new MeshBuilder();
    // La couronne n'occupe que le devant : derrière, le rotor porte son excentrique sur un palier.
    const gearDepth = b * 0.26;
    const holeBack = layout.lobeRadius * 1.02;
    body.transform(tr(0, 0, -gearDepth / 2)).extrudeRing(counter, circle(holeBack, points), b - gearDepth - 1, 25);
    const gearRoot = layout.rotorGearRadius;
    body.transform(tr(0, 0, b / 2 - gearDepth / 2 - 0.5)).extrudeRing(counter, internalTeeth(layout.rotorTeeth, gearRoot, e * 0.13, points), gearDepth, 25);
    // Le creux de chaque face : c'est lui, et non le flanc, qui donne le taux de compression.
    const pockets = new MeshBuilder();
    const seals = new MeshBuilder();
    for (let k = 0; k < 3; k++) {
      const a = (120 * k * Math.PI) / 180;
      const m = m34.rotationZ(m34.create(), -a);
      const pocket = m34.multiply(m34.create(), m, tr(0, layout.flankRise - r * 0.012, 0));
      pockets.transform(pocket).box(r * 0.5, r * 0.028, b * 0.5);
      const apex = ((60 + 120 * k) * Math.PI) / 180;
      const place = m34.multiply(m34.create(), m34.rotationZ(m34.create(), -apex), tr(0, r - 1, 0));
      seals.transform(place).box(2.4, 5, b - 1);
    }
    const bodyGeometry = geo(body);
    const pocketGeometry = geo(pockets);
    const sealGeometry = geo(seals);
    for (const slot of layout.slots) {
      const group = new THREE.Group();
      group.position.z = slot.z;
      group.add(new THREE.Mesh(bodyGeometry, materials.alloy), new THREE.Mesh(pocketGeometry, materials.rubber), new THREE.Mesh(sealGeometry, materials.accent));
      this.rotors.push(group);
      this.root.add(group);
    }

    // ── Ce qui ne bouge pas : carter de verre, ses arêtes, pignons fixes, lumières, bougies ──
    const housing = new MeshBuilder();
    const edges = new MeshBuilder();
    const fixed = new MeshBuilder();
    const brass = new MeshBuilder();
    const exhaustPipes = new MeshBuilder();
    const intakePipes = new MeshBuilder();
    const inner: P2[] = [...layout.housing].reverse() as P2[];
    const margin = r * 0.24;
    const outer: P2[] = inner.map(([x, y]) => {
      const length = Math.hypot(x, y) || 1;
      return [x + (x / length) * margin, y + (y / length) * margin];
    });
    const housingPath = (z: number): P3[] => layout.housing.map(([x, y]) => [x, y, z] as P3);
    const across = (u: number) => [r * Math.sin(u * DEG) - e * Math.sin(3 * u * DEG), r * Math.cos(u * DEG) - e * Math.cos(3 * u * DEG)];
    for (const slot of layout.slots) {
      housing.transform(tr(0, 0, slot.z)).extrudeRing(outer, inner, b, 25);
      // Les flasques, de chaque côté : du verre plein, qu'on traverse du regard.
      housing.transform(tr(0, 0, slot.z + (b + layout.plate) / 2)).extrude(outer, layout.plate * 0.9, 25);
      edges.transform(null);
      edges.tube(housingPath(slot.z + b / 2), 1.1, 6, true);
      edges.tube(housingPath(slot.z - b / 2), 1.1, 6, true);
      // Le pignon fixe, vissé au flasque avant, autour de l'arbre.
      fixed.transform(tr(0, 0, slot.z + b / 2 - gearDepth / 2)).gear(layout.fixedTeeth, layout.fixedRadius - e * 0.13, layout.fixedRadius + e * 0.13, gearDepth, layout.mainRadius * 1.04);
      // La lumière d'échappement, dans le carter, puis son tube ; l'admission par le flasque.
      const ex = across(EXHAUST_PORT_U + EXHAUST_PORT_WIDTH_U / 2);
      const exLength = Math.hypot(ex[0], ex[1]) || 1;
      const exOut = [ex[0] / exLength, ex[1] / exLength];
      exhaustPipes.transform(null).tube([[ex[0], ex[1], slot.z], [ex[0] + exOut[0] * margin * 2.2, ex[1] + exOut[1] * margin * 2.2, slot.z], [ex[0] + exOut[0] * margin * 3.4, ex[1] + exOut[1] * margin * 3.8, slot.z]], b * 0.2, 14);
      const inp = across(INTAKE_PORT_U);
      const inLength = Math.hypot(inp[0], inp[1]) || 1;
      const inOut = [inp[0] / inLength, inp[1] / inLength];
      intakePipes.transform(null).tube([[inp[0] * 0.98, inp[1] * 0.98, slot.z], [inp[0] + inOut[0] * margin * 2.2, inp[1] + inOut[1] * margin * 2.2, slot.z], [inp[0] + inOut[0] * margin * 3.2, inp[1] + inOut[1] * margin * 3.6, slot.z]], b * 0.22, 14);
      // Deux bougies en haut, sur la taille du carter.
      const sparkShape = new MeshBuilder();
      for (const side of [-1, 1]) {
        brass.transform(tr(side * r * 0.14, r - e + margin * 0.55, slot.z)).cylinder(r * 0.035, margin * 1.1, 10);
        sparkShape.transform(tr(side * r * 0.14, r - e - 1, slot.z)).sphere(r * 0.04, 10, 6);
      }
      const spark = new THREE.Mesh(geo(sparkShape), new THREE.MeshBasicMaterial({ color: 0xffd080, transparent: true, depthWrite: false, toneMapped: false }));
      spark.visible = false;
      this.sparks.push(spark);
      this.root.add(spark);
    }
    // Ce qui se branche sur les lumières, en schéma : volute et compresseur, carburateurs, filtre.
    const across2 = (u: number) => {
      const p = across(u);
      const length = Math.hypot(p[0], p[1]) || 1;
      return { p, out: [p[0] / length, p[1] / length] };
    };
    const inlet = across2(INTAKE_PORT_U);
    const outlet = across2(EXHAUST_PORT_U + EXHAUST_PORT_WIDTH_U / 2);
    const inletAt = (d: number, z: number): P3 => [inlet.p[0] + inlet.out[0] * d, inlet.p[1] + inlet.out[1] * d, z];
    const outletAt = (d: number, z: number): P3 => [outlet.p[0] + outlet.out[0] * d, outlet.p[1] + outlet.out[1] * d, z];
    const midZ = (layout.slots[0].z + layout.slots[layout.slots.length - 1].z) / 2;
    const lean = m34.rotationZ(m34.create(), Math.atan2(inlet.out[0], inlet.out[1]));
    /** Une pièce tournée autour de la direction de la lumière d'admission, à cette distance. */
    const onInlet = (d: number, z: number): Mat34 => {
      const at = inletAt(d, z);
      return m34.multiply(m34.create(), tr(at[0], at[1], at[2]), lean);
    };
    const ends = new MeshBuilder();
    const mouthD = margin * 3.4;
    const venturi: P2[] = [[r * 0.11, 0], [r * 0.08, r * 0.14], [r * 0.14, r * 0.3], [r * 0.12, r * 0.3], [r * 0.05, r * 0.14], [r * 0.09, 0]];
    let fitting = mouthD;
    if (layout.carb === 'multi') {
      for (const slot of layout.slots) ends.transform(onInlet(mouthD, slot.z)).lathe(venturi, 22, 50);
      fitting = mouthD + r * 0.3;
    } else if (layout.carb === 'single' || layout.charger) {
      // Les conduits se rejoignent sur une pipe, et le carburateur ou le compresseur se pose dessus.
      const zs = layout.slots.map((slot) => slot.z);
      if (zs.length > 1) ends.transform(null).tube([inletAt(mouthD, zs[0]), inletAt(mouthD, zs[zs.length - 1])], b * 0.22, 12);
      if (layout.carb === 'single') ends.transform(onInlet(mouthD, midZ)).lathe(venturi, 22, 50);
      fitting = mouthD + r * 0.3;
      if (layout.charger) {
        ends.transform(onInlet(mouthD + r * 0.42, midZ)).box(r * 0.46, r * 0.34, Math.max(b * 1.1, r * 0.5));
        fitting = mouthD + r * 0.6;
      }
    }
    const carbShape = new THREE.Mesh(geo(ends), materials.alloy);
    this.root.add(carbShape);
    const filters = new MeshBuilder();
    if (!layout.turbo && layout.airFilter !== 'stock') {
      const place = onInlet(fitting, midZ);
      if (layout.airFilter === 'cone') filters.transform(place).lathe([[r * 0.11, 0], [r * 0.2, r * 0.05], [r * 0.14, r * 0.34], [0, r * 0.36]], 22, 50);
      else if (layout.airFilter === 'stacks') filters.transform(place).lathe([[r * 0.08, 0], [r * 0.1, r * 0.08], [r * 0.18, r * 0.14], [r * 0.2, r * 0.14]], 22, 60);
      else filters.transform(place).lathe([[0, 0], [r * 0.2, 0], [r * 0.2, r * 0.1], [0, r * 0.1]], 22, 40);
      const filterMesh = new THREE.Mesh(geo(filters), layout.airFilter === 'stacks' ? materials.alloy : stage.surface({ color: 0xb5382a, metalness: 0, roughness: 0.85 }));
      this.root.add(filterMesh);
    }
    const turboParts = new MeshBuilder();
    if (layout.turbo) {
      // Une volute par rotor n'aurait pas de sens : les tubes se rejoignent, et un seul turbo les reçoit.
      const ventD = margin * 3.8;
      const zs = layout.slots.map((slot) => slot.z);
      if (zs.length > 1) exhaustPipes.transform(null).tube([outletAt(ventD, zs[0]), outletAt(ventD, zs[zs.length - 1])], b * 0.2, 12);
      const volute = outletAt(ventD + r * 0.28, midZ);
      exhaustPipes.transform(onZ(volute[0], volute[1], volute[2])).lathe([[r * 0.06, -r * 0.1], [r * 0.24, -r * 0.09], [r * 0.28, -r * 0.01], [r * 0.25, r * 0.08], [r * 0.08, r * 0.1]], 22, 60);
      turboParts.transform(onZ(volute[0], volute[1], volute[2] + r * 0.28)).lathe([[r * 0.06, -r * 0.08], [r * 0.24, -r * 0.05], [r * 0.26, r * 0.02], [r * 0.18, r * 0.08], [r * 0.07, r * 0.09]], 22, 60);
      turboParts.transform(null).tube([[volute[0], volute[1] - r * 0.1, volute[2] + r * 0.3], [volute[0] * 0.3, volute[1] - r * 0.5, volute[2] + r * 0.3], inletAt(mouthD, midZ)], r * 0.06, 10);
      this.labels.push({ key: 'engine.part.turbine', x: volute[0], y: volute[1], z: volute[2] }, { key: 'engine.part.compressor', x: volute[0], y: volute[1], z: volute[2] + r * 0.3 });
      // Le second turbo d'un 13B-REW : sous le premier, sur le même collecteur, et son compresseur sur la même admission.
      if (layout.turbos === 2) {
        const below = outletAt(ventD + r * 0.28, midZ);
        below[1] -= r * 0.62;
        exhaustPipes.transform(onZ(below[0], below[1], below[2])).lathe([[r * 0.06, -r * 0.1], [r * 0.24, -r * 0.09], [r * 0.28, -r * 0.01], [r * 0.25, r * 0.08], [r * 0.08, r * 0.1]], 22, 60);
        exhaustPipes.transform(null).tube([[volute[0], volute[1] - r * 0.2, volute[2]], [below[0], below[1] + r * 0.2, below[2]]], r * 0.08, 10);
        turboParts.transform(onZ(below[0], below[1], below[2] + r * 0.28)).lathe([[r * 0.06, -r * 0.08], [r * 0.24, -r * 0.05], [r * 0.26, r * 0.02], [r * 0.18, r * 0.08], [r * 0.07, r * 0.09]], 22, 60);
        turboParts.transform(null).tube([[below[0], below[1] - r * 0.1, below[2] + r * 0.3], [below[0] * 0.3, below[1] - r * 0.3, below[2] + r * 0.3], inletAt(mouthD, midZ)], r * 0.06, 10);
        this.labels.push({ key: 'engine.part.turbine2', x: below[0], y: below[1], z: below[2] });
      }
    }
    if (layout.charger) {
      const at = inletAt(mouthD + r * 0.42, midZ);
      this.labels.push({ key: 'engine.part.compressor', x: at[0], y: at[1], z: at[2] });
    }
    if (layout.carb) {
      const at = inletAt(mouthD + r * 0.2, layout.carb === 'multi' ? layout.slots[0].z : midZ);
      this.labels.push({ key: 'engine.part.carburetor', x: at[0], y: at[1], z: at[2] });
    }
    if (turboParts.vertexCount > 0) this.root.add(new THREE.Mesh(geo(turboParts), materials.alloy));

    const shell = new THREE.Mesh(geo(housing), materials.ghost);
    shell.renderOrder = 2;
    this.root.add(shell);
    this.root.add(new THREE.Mesh(geo(edges), materials.steel));
    this.root.add(new THREE.Mesh(geo(fixed), materials.forged));
    this.root.add(new THREE.Mesh(geo(brass), materials.brass));
    this.root.add(new THREE.Mesh(geo(exhaustPipes), materials.forged));
    this.root.add(new THREE.Mesh(geo(intakePipes), materials.alloy));

    const pinionShape = new MeshBuilder().gear(layout.pinion.teeth, layout.pinion.radius - 2.2, layout.pinion.radius + 2.2, ring.width);
    this.pinion = new THREE.Mesh(geo(pinionShape), materials.forged);
    this.pinion.position.set(layout.pinion.x, layout.pinion.y, ring.z);
    this.root.add(this.pinion);

    // ── Le gaz : une lame par chambre, redessinée à chaque image, entre le flanc et le carter ──
    const strip = 25;
    const vertices = (strip + 1) * 2;
    for (let c = 0; c < layout.chambers; c++) {
      const positions = new Float32Array(vertices * 3);
      const geometry = new THREE.BufferGeometry();
      const attribute = new THREE.BufferAttribute(positions, 3);
      attribute.setUsage(THREE.DynamicDrawUsage);
      geometry.setAttribute('position', attribute);
      const indices: number[] = [];
      for (let k = 0; k < strip; k++) {
        const a = k * 2;
        indices.push(a, a + 1, a + 2, a + 1, a + 3, a + 2);
      }
      geometry.setIndex(indices);
      const material = new THREE.MeshBasicMaterial({ color: 0x808080, transparent: true, opacity: 0.55, depthWrite: false, side: THREE.DoubleSide, toneMapped: false });
      const mesh = new THREE.Mesh(geometry, material);
      mesh.frustumCulled = false;
      this.chambers.push({ mesh, material, positions, attribute });
      this.root.add(mesh);
    }

    const span = layout.frontZ + b * 0.4 - (ring.z - ring.width);
    this.center = [0, -r * 0.05, (layout.frontZ + b * 0.4 + ring.z - ring.width) / 2];
    const fitted = layout.turbo || layout.charger || layout.carb !== null || layout.airFilter !== 'stock';
    this.radius = Math.max(r + e + margin * (fitted ? 5 : 3), span / 2 + r * 0.45, ring.radius * 1.1) * 1.08;
    const firstZ = layout.slots[0].z;
    this.labels.push(
      { key: 'engine.part.rotor', x: r * 0.55, y: 0, z: firstZ },
      { key: 'engine.part.housing', x: r + e + margin * 0.5, y: r * 0.25, z: firstZ },
      { key: 'engine.part.eccentric', x: 0, y: 0, z: front - b * 0.05 },
      { key: 'engine.part.fixedGear', x: layout.fixedRadius, y: 0, z: firstZ + b / 2 },
      { key: 'engine.part.rotorGear', x: -layout.rotorGearRadius, y: 0, z: firstZ + b / 2 },
      { key: 'engine.part.apex', x: 0, y: -(r - e), z: firstZ },
      { key: 'engine.part.plug', x: r * 0.14, y: r - e + margin, z: firstZ },
      { key: 'engine.part.exhaustPort', x: across(EXHAUST_PORT_U)[0] * 1.2, y: across(EXHAUST_PORT_U)[1] * 1.2, z: firstZ },
      { key: 'engine.part.intakePort', x: across(INTAKE_PORT_U)[0] * 1.2, y: across(INTAKE_PORT_U)[1] * 1.2, z: firstZ },
      { key: 'engine.part.flywheel', x: 0, y: ring.radius, z: ring.z },
    );
  }

  update(pose: RotaryPose, motion: Motion, theme: Theme): void {
    const layout = this.layout;
    const { radius: r, eccentricity: e, width: b } = layout;
    this.shaft.rotation.z = -pose.shaftDeg * DEG;
    const painter = motion.painter;
    let sparks = 0;
    let flames = 0;
    const perFlank = layout.rotor.length / 3;
    for (let i = 0; i < layout.rotors; i++) {
      const group = this.rotors[i];
      group.position.x = pose.rotorX[i];
      group.position.y = pose.rotorY[i];
      group.rotation.z = -pose.rotorDeg[i] * DEG;
      const beta = pose.rotorDeg[i] * DEG;
      const cos = Math.cos(beta);
      const sin = Math.sin(beta);
      const spark = this.sparks[i];
      spark.visible = pose.spark[i] > 0.02;
      if (spark.visible) {
        (spark.material as T.MeshBasicMaterial).opacity = pose.spark[i];
        sparks++;
      }
      const z = layout.slots[i].z;
      for (let k = 0; k < 3; k++) {
        const c = i * 3 + k;
        const chamber = this.chambers[c];
        const middle = pose.rotorDeg[i] - 120 * k;
        const flank = (3 - k) % 3;
        const positions = chamber.positions;
        const strip = 25;
        for (let s = 0; s <= strip; s++) {
          const share = s / strip;
          // Du sommet d'arrière au sommet d'avant, sur le carter et sur le flanc en même temps.
          const u = (middle - 60 + 120 * share) * DEG;
          const flankIndex = Math.min(Math.floor(share * perFlank), perFlank - 1);
          const p = layout.rotor[flank * perFlank + flankIndex];
          const at = s * 6;
          positions[at] = r * Math.sin(u) - e * Math.sin(3 * u);
          positions[at + 1] = r * Math.cos(u) - e * Math.cos(3 * u);
          positions[at + 2] = z + b / 2 - 1;
          positions[at + 3] = pose.rotorX[i] + p[0] * cos + p[1] * sin;
          positions[at + 4] = pose.rotorY[i] - p[0] * sin + p[1] * cos;
          positions[at + 5] = z + b / 2 - 1;
        }
        chamber.attribute.needsUpdate = true;
        const live = pose.gasLive;
        chamber.mesh.visible = live;
        if (live) {
          const temperature = pose.gasT[c];
          painter.gasRgb(theme, temperature < 800 ? temperature : 800 + (temperature - 800) * 0.33, pose.gasBurnt[c], this.rgb);
          if (pose.flame[c] > 0) {
            const hot = 1 - pose.flame[c];
            chamber.material.color.setRGB(this.rgb[0] / 255 + (1 - this.rgb[0] / 255) * hot, this.rgb[1] / 255 + (0.62 - this.rgb[1] / 255) * hot, this.rgb[2] / 255 * (1 - hot), this.stage.THREE.SRGBColorSpace);
            chamber.material.opacity = 0.9;
            flames++;
          } else {
            chamber.material.color.setRGB(this.rgb[0] / 255, this.rgb[1] / 255, this.rgb[2] / 255, this.stage.THREE.SRGBColorSpace);
            chamber.material.opacity = 0.55;
          }
        }
      }
    }
    this.drawnSparks = sparks;
    this.drawnFlames = flames;
    this.pinion.rotation.z = pose.pinionAngle;
    this.pinion.position.z = layout.ring.z + layout.ring.width * (1.6 - 1.6 * pose.starter);
  }

  dispose(): void {
    this.stage.scene.remove(this.root);
    disposeObject(this.root, this.stage.materials);
  }
}
