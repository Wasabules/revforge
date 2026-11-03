/**
 * Le rotatif en écorché 2D : le premier rotor vu de face, dans son carter, et une coupe le long de l'arbre. Même `RotaryLayout`,
 * même `RotaryPose` que la 3D. On dessine en millimètres, y vers le haut. Les formes fixes sont calculées une fois ; dessiner une image n'alloue rien.
 */
import type { Theme } from './canvas';
import type { GasPainter } from './fluidRender';
import { EXHAUST_PORT_U, EXHAUST_PORT_WIDTH_U, INTAKE_PORT_U, type RotaryLayout, type RotaryPose } from './rotary';

const DEG = Math.PI / 180;

function polyPath(points: readonly (readonly [number, number])[]): Path2D {
  const path = new Path2D();
  points.forEach((p, k) => (k === 0 ? path.moveTo(p[0], p[1]) : path.lineTo(p[0], p[1])));
  path.closePath();
  return path;
}

/** Les textes à poser sur le dessin : déjà traduits par la vue. */
export interface RotaryLabels {
  front: string;
  side: string;
  parts: boolean;
  rotor: string;
  housing: string;
  eccentric: string;
  fixedGear: string;
  rotorGear: string;
  apex: string;
  exhaustPort: string;
  intakePort: string;
  plug: string;
  flywheel: string;
  ring: string;
}

function gearPath(teeth: number, pitch: number, depth: number): Path2D {
  const points: [number, number][] = [];
  for (let k = 0; k < teeth; k++) {
    const a = (k / teeth) * Math.PI * 2;
    const step = (Math.PI * 2) / teeth;
    for (const [share, radius] of [[-0.3, pitch - depth], [-0.12, pitch + depth], [0.12, pitch + depth], [0.3, pitch - depth]] as const) {
      points.push([radius * Math.sin(a + share * step), radius * Math.cos(a + share * step)]);
    }
  }
  return polyPath(points);
}

export class Rotary2D {
  private readonly housing: Path2D;
  private readonly housingOuter: Path2D;
  private readonly rotor: Path2D;
  private readonly fixedGear: Path2D;
  private readonly rotorGear: Path2D;
  private readonly extent: number;
  private readonly portEx: Path2D;
  private readonly portIn: Path2D;

  constructor(private readonly layout: RotaryLayout) {
    const margin = layout.radius * 0.24;
    this.housing = polyPath(layout.housing);
    // Le carter : la trace du sommet, poussée vers l'extérieur.
    this.housingOuter = polyPath(layout.housing.map(([x, y]) => {
      const length = Math.hypot(x, y) || 1;
      return [x + (x / length) * margin, y + (y / length) * margin] as [number, number];
    }));
    this.rotor = polyPath(layout.rotor);
    const toothDepth = layout.eccentricity * 0.13;
    this.fixedGear = gearPath(layout.fixedTeeth, layout.fixedRadius, toothDepth);
    this.rotorGear = gearPath(layout.rotorTeeth, layout.rotorGearRadius, toothDepth);
    this.extent = layout.radius + layout.eccentricity + margin + layout.radius * 0.28;
    const { radius: r, eccentricity: e } = layout;
    const port = (u: number, width: number, length: number) => {
      const a = u * DEG;
      const b = (u + width) * DEG;
      const point = (angle: number) => [r * Math.sin(angle) - e * Math.sin(3 * angle), r * Math.cos(angle) - e * Math.cos(3 * angle)] as [number, number];
      const inner0 = point(a);
      const inner1 = point(b);
      const out = (p: [number, number]) => [p[0] * (1 + length), p[1] * (1 + length)] as [number, number];
      return polyPath([inner0, inner1, out(inner1), out(inner0)]);
    };
    this.portEx = port(EXHAUST_PORT_U, EXHAUST_PORT_WIDTH_U, 0.46);
    this.portIn = port(INTAKE_PORT_U - 6, 12, 0.46);
  }

  draw(ctx: CanvasRenderingContext2D, width: number, height: number, pose: RotaryPose, painter: GasPainter, theme: Theme, labels: RotaryLabels): void {
    const wide = width >= 620;
    const gap = 8;
    const titleH = 16;
    const frontW = wide ? width * 0.56 - gap : width;
    const frontH = wide ? height - titleH : (height - titleH * 2 - gap) * 0.64;
    const sideX = wide ? frontW + gap * 2 : 0;
    const sideY = wide ? titleH : titleH + frontH + gap + titleH;
    const sideW = wide ? width - sideX : width;
    const sideH = height - sideY;
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'alphabetic';
    ctx.textAlign = 'left';
    ctx.fillStyle = theme.dim;
    ctx.fillText(labels.front, 2, titleH - 5);
    ctx.fillText(labels.side, sideX + 2, sideY - 5);
    this.drawFront(ctx, 0, titleH, frontW, frontH, pose, painter, theme, labels);
    this.drawSide(ctx, sideX, sideY, sideW, sideH, pose, theme, labels);
  }

  private drawFront(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, pose: RotaryPose, painter: GasPainter, theme: Theme, labels: RotaryLabels): void {
    const layout = this.layout;
    const { radius: r, eccentricity: e } = layout;
    const scale = Math.min(w / (2 * this.extent), h / (2 * (r - e + r * 0.34 + r * 0.36)));
    const px = 1 / scale;
    ctx.save();
    ctx.translate(x + w / 2, y + h / 2);
    ctx.scale(scale, -scale);

    // Carter, lumières, fond des chambres.
    ctx.fillStyle = theme.raised;
    ctx.fill(this.housingOuter);
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.2 * px;
    ctx.stroke(this.housingOuter);
    ctx.fillStyle = theme.surface;
    ctx.fill(this.housing);
    this.drawPort(ctx, this.portEx, pose, 0, theme, px, true);
    this.drawPort(ctx, this.portIn, pose, 0, theme, px, false);

    // Le gaz de chaque chambre : entre le carter et le flanc.
    const rotor = 0;
    const beta = pose.rotorDeg[rotor] * DEG;
    const cos = Math.cos(beta);
    const sin = Math.sin(beta);
    const cx = pose.rotorX[rotor];
    const cy = pose.rotorY[rotor];
    const flankPoints = layout.rotor;
    const perFlank = flankPoints.length / 3;
    for (let k = 0; k < 3; k++) {
      const c = k;
      const middle = pose.rotorDeg[rotor] - 120 * k;
      ctx.beginPath();
      const steps = 24;
      for (let s = 0; s <= steps; s++) {
        const u = (middle + 60 - (120 * s) / steps) * DEG;
        const hx = r * Math.sin(u) - e * Math.sin(3 * u);
        const hy = r * Math.cos(u) - e * Math.cos(3 * u);
        if (s === 0) ctx.moveTo(hx, hy);
        else ctx.lineTo(hx, hy);
      }
      // La face k, dans le repère du rotor : milieu à −120·k degrés, soit le flanc (3 − k) mod 3 du contour.
      const flank = (3 - k) % 3;
      for (let j = 0; j < perFlank; j++) {
        const p = flankPoints[flank * perFlank + j];
        ctx.lineTo(cx + p[0] * cos + p[1] * sin, cy - p[0] * sin + p[1] * cos);
      }
      ctx.closePath();
      if (pose.gasLive) {
        const temperature = pose.gasT[c];
        ctx.fillStyle = painter.gasStyle(theme, temperature < 800 ? temperature : 800 + (temperature - 800) * 0.33, pose.gasBurnt[c]);
      } else {
        ctx.fillStyle = theme.bg;
      }
      ctx.fill();
      if (pose.flame[c] > 0) {
        ctx.globalAlpha = 0.45 + 0.45 * (1 - pose.flame[c]);
        ctx.fillStyle = theme.accent;
        ctx.fill();
        ctx.globalAlpha = 1;
      }
    }

    // Les bougies, en haut : deux, de part et d'autre de la taille du carter.
    const top = r - e;
    for (const side of [-1, 1]) {
      const plugX = side * r * 0.13;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 3 * px;
      ctx.beginPath();
      ctx.moveTo(plugX, top + r * 0.02);
      ctx.lineTo(plugX, top + r * 0.2);
      ctx.stroke();
      if (pose.spark[rotor] > 0.02) {
        ctx.globalAlpha = pose.spark[rotor];
        ctx.fillStyle = theme.accent;
        ctx.beginPath();
        ctx.arc(plugX, top - r * 0.005, r * 0.05, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
    }

    // Le rotor : à A/3, autour de son centre, qui tourne à A. Le sens des aiguilles, y vers le haut : un angle négatif.
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(-beta);
    ctx.fillStyle = theme.metal;
    ctx.globalAlpha = 0.92;
    ctx.fill(this.rotor);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.2 * px;
    ctx.stroke(this.rotor);
    // Le creux de chaque face, et le segment d'apex à chaque sommet.
    ctx.fillStyle = theme.raised;
    for (let k = 0; k < 3; k++) {
      ctx.save();
      ctx.rotate(120 * k * DEG);
      ctx.fillRect(-r * 0.22, layout.flankRise - r * 0.045, r * 0.44, r * 0.04);
      ctx.restore();
    }
    ctx.fillStyle = theme.accent;
    for (let k = 0; k < 3; k++) {
      const a = (60 + 120 * k) * DEG;
      ctx.fillRect(r * Math.sin(a) - r * 0.02, r * Math.cos(a) - r * 0.02, r * 0.04, r * 0.04);
    }
    // La couronne, solidaire du rotor.
    ctx.fillStyle = theme.surface;
    ctx.fill(this.rotorGear);
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 0.8 * px;
    ctx.stroke(this.rotorGear);
    ctx.restore();

    // Le pignon fixe : il ne tourne jamais. La couronne roule dessus, à 3 pour 2.
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 0.8 * px;
    ctx.fill(this.fixedGear);
    ctx.stroke(this.fixedGear);

    // L'excentrique, puis l'arbre : ils tournent ensemble, à A.
    ctx.fillStyle = theme.metal;
    ctx.beginPath();
    ctx.arc(cx, cy, layout.lobeRadius * 0.86, 0, Math.PI * 2);
    ctx.globalAlpha = 0.65;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.stroke();
    ctx.fillStyle = theme.text;
    ctx.beginPath();
    ctx.arc(0, 0, layout.mainRadius * 0.8, 0, Math.PI * 2);
    ctx.fill();
    // Un trait du centre de l'arbre au centre du rotor : l'excentricité qui tourne.
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 2 * px;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(cx, cy);
    ctx.stroke();
    ctx.restore();

    if (labels.parts) {
      const X = (mm: number) => x + w / 2 + mm * scale;
      const Y = (mm: number) => y + h / 2 - mm * scale;
      ctx.fillStyle = theme.dim;
      ctx.textAlign = 'center';
      ctx.fillText(labels.plug, X(0), Y(top + r * 0.3));
      ctx.fillText(labels.eccentric, X(0), Y(-layout.rotorGearRadius - r * 0.12));
      ctx.fillText(labels.housing, X(this.extent * 0.72), Y(r * 0.64));
      ctx.fillText(labels.exhaustPort, X(r * 0.72), Y(-r * 1.0));
      ctx.fillText(labels.intakePort, X(-r * 0.72), Y(-r * 1.0));
      ctx.textAlign = 'left';
    }
  }

  private drawPort(ctx: CanvasRenderingContext2D, path: Path2D, pose: RotaryPose, rotor: number, theme: Theme, px: number, exhaust: boolean): void {
    let flow = 0;
    for (let k = 0; k < 3; k++) {
      const c = rotor * 3 + k;
      flow += Math.abs(exhaust ? pose.jetEx[c] : pose.jetIn[c]);
    }
    ctx.fillStyle = theme.surface;
    ctx.fill(path);
    if (flow > 0.002) {
      ctx.fillStyle = exhaust ? theme.accent : theme.cool;
      ctx.globalAlpha = Math.min(0.2 + flow * 0.9, 0.9);
      ctx.fill(path);
      ctx.globalAlpha = 1;
    }
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1 * px;
    ctx.stroke(path);
  }

  private drawSide(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, pose: RotaryPose, theme: Theme, labels: RotaryLabels): void {
    const layout = this.layout;
    const { radius: r, eccentricity: e, width: b } = layout;
    const front = layout.frontZ + b * 0.3;
    const rear = layout.ring.z - b * 0.1;
    const span = front - rear;
    const tall = 2 * (layout.ring.radius + r * 0.02);
    const scale = Math.min((w - 8) / span, (h - 8) / tall);
    const px = 1 / scale;
    ctx.save();
    ctx.translate(x + w / 2, y + h / 2);
    ctx.scale(scale, -scale);
    // z vers l'avant, à droite de la coupe.
    ctx.translate(-(front + rear) / 2, 0);

    // Le volant et sa couronne, derrière, et le démarreur, en prise seulement quand il travaille.
    const ring = layout.ring;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1 * px;
    ctx.fillRect(ring.z - ring.width / 2, -ring.radius, ring.width, 2 * ring.radius);
    ctx.strokeRect(ring.z - ring.width / 2, -ring.radius, ring.width, 2 * ring.radius);
    const pinionZ = ring.z + ring.width * (1.6 - 1.6 * pose.starter);
    ctx.fillStyle = theme.metal;
    ctx.fillRect(pinionZ - ring.width / 2, layout.pinion.y - layout.pinion.radius, ring.width, 2 * layout.pinion.radius);
    // L'arbre, tout du long, puis une poulie à l'avant.
    ctx.fillStyle = theme.metal;
    ctx.fillRect(rear, -layout.mainRadius, front - rear, 2 * layout.mainRadius);
    ctx.fillStyle = theme.raised;
    ctx.fillRect(layout.frontZ + b * 0.12, -r * 0.42, b * 0.14, r * 0.84);
    ctx.strokeRect(layout.frontZ + b * 0.12, -r * 0.42, b * 0.14, r * 0.84);

    for (let i = 0; i < layout.rotors; i++) {
      const slot = layout.slots[i];
      const z0 = slot.z - b / 2;
      // Le carter : plus haut que le rotor, de la largeur du rotor ; flasques de chaque côté.
      const major = r + e;
      ctx.fillStyle = theme.raised;
      ctx.fillRect(z0 - layout.plate / 2, -major - r * 0.1, b + layout.plate, 2 * (major + r * 0.1));
      ctx.strokeStyle = theme.lineStrong;
      ctx.strokeRect(z0 - layout.plate / 2, -major - r * 0.1, b + layout.plate, 2 * (major + r * 0.1));
      ctx.fillStyle = theme.surface;
      ctx.fillRect(z0, -major, b, 2 * major);
      // Le rotor : il monte et descend avec son excentrique. Vu de côté, sa hauteur suit l'angle de ses sommets.
      const beta = pose.rotorDeg[i] * DEG;
      let top = -Infinity;
      let bottom = Infinity;
      for (let k = 0; k < 3; k++) {
        const height = r * Math.cos(beta + (60 + 120 * k) * DEG);
        if (height > top) top = height;
        if (height < bottom) bottom = height;
      }
      const cy = pose.rotorY[i];
      ctx.fillStyle = theme.metal;
      ctx.globalAlpha = 0.9;
      ctx.fillRect(z0 + 1, cy + bottom, b - 2, top - bottom);
      ctx.globalAlpha = 1;
      ctx.strokeRect(z0 + 1, cy + bottom, b - 2, top - bottom);
      // L'excentrique, dans le rotor.
      ctx.fillStyle = theme.text;
      ctx.globalAlpha = 0.8;
      ctx.fillRect(z0 + b * 0.1, cy - layout.lobeRadius, b * 0.8, 2 * layout.lobeRadius);
      ctx.globalAlpha = 1;
      // Le pignon fixe contre le flasque avant, la couronne en face de lui.
      ctx.fillStyle = theme.accent;
      ctx.fillRect(z0 + b * 0.88, -layout.fixedRadius, b * 0.14, 2 * layout.fixedRadius);
      if (pose.spark[i] > 0.02) {
        ctx.globalAlpha = pose.spark[i];
        ctx.fillStyle = theme.accent;
        ctx.beginPath();
        ctx.arc(slot.z, r - e, r * 0.06, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
    }
    ctx.restore();

    if (labels.parts) {
      const X = (mm: number) => x + w / 2 + (mm - (front + rear) / 2) * scale;
      const Y = (mm: number) => y + h / 2 - mm * scale;
      ctx.fillStyle = theme.dim;
      ctx.textAlign = 'center';
      ctx.fillText(labels.flywheel, X(ring.z), Y(ring.radius) - 3);
      ctx.fillText(labels.fixedGear, X(layout.slots[0].z), Y(-(r + e) - r * 0.22));
      ctx.textAlign = 'left';
    }
  }
}
