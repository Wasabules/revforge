/**
 * La vue 2D de l'embrayage : une vraie coupe, hachurée comme un plan, une vue de face où chaque secteur découvre une
 * couche, et la loupe sur la butée. Elle lit `clutchSection` et une `ClutchFrame`, comme la 3D, et n'a pas d'horloge.
 *
 * Les profils deviennent des `Path2D` une fois par moteur et par taille : une image ne fait que les remplir et les déplacer.
 */
import { mix, type Theme } from './canvas';
import { BEARING_BALL, BEARING_PITCH_R, FORK_SLAVE_R, FORK_STUD_R, type ClutchFrame } from './clutchMechanism';
import { bellProfile, dctLeverX, diaphragmX, groupOffset, LIFT_EXAGGERATION, sectionParts, type ClutchLayout, type PartGroup, type PartMaterial, type SectionPart } from './clutchSection';
import { heatColors } from './heat';
import { lang, t, whole, type MessageKey } from './i18n.svelte';
import type { Stage3D } from './render3d';

export const FLUID_COLOR = '#c99a3c';
const GLOW = heatColors(750).fill;
const RED_HOT = heatColors(550).fill;
const clamp = (value: number, low: number, high: number) => (value < low ? low : value > high ? high : value);
const TAU = Math.PI * 2;

/** Couleur d'une surface de friction selon sa température : brun, puis rouge sombre, puis orange. */
export function frictionColor(celsius: number): string {
  if (celsius < 150) return '#8a6a48';
  if (celsius < 300) return mix('#8a6a48', '#a8341c', (celsius - 150) / 150);
  return mix('#a8341c', GLOW, clamp((celsius - 300) / 150, 0, 1));
}

function steelColor(theme: Theme, celsius: number): string {
  if (celsius < 120) return theme.metal;
  return mix(theme.metal, RED_HOT, clamp((celsius - 120) / 280, 0, 1));
}

function toHex(color: string): string {
  // `mix` n'accepte que l'hexadécimal, et le thème peut donner autre chose.
  if (color.startsWith('#') && color.length === 7) return color;
  if (color.startsWith('#') && color.length === 4) return `#${color[1]}${color[1]}${color[2]}${color[2]}${color[3]}${color[3]}`;
  return '#808890';
}

/** Ce qui fait vivre une image, en plus de la mécanique. */
export interface ClutchLive {
  heat: number;
  slipKw: number;
  rpm: number;
  inputRpm: number;
  frozen: boolean;
  transmitting: boolean;
}

interface Drawn {
  part: SectionPart;
  path: Path2D;
}

interface Label {
  key: MessageKey;
  group: PartGroup;
  /** Point visé, en mm. */
  ax: number;
  ar: number;
  /** Où écrire, en mm. */
  lx: number;
  lr: number;
  align: CanvasTextAlign;
}

export interface Box {
  x: number;
  y: number;
  w: number;
  h: number;
}

const HATCH_MATERIALS: Partial<Record<PartMaterial, 0 | 1>> = { cast: 0, sheet: 1, steel: 1, ring: 0, crank: 1, carrier: 1, housing: 0, shaft: 1 };

export class ClutchPainter {
  private readonly layout: ClutchLayout;
  private readonly drawn: Drawn[] = [];
  private readonly labels: Label[];
  private readonly bearingLabels: Label[];
  private readonly bell: Path2D;
  private readonly clip: Path2D;
  private readonly breakLine: Path2D;
  private readonly fork: Path2D;
  private readonly pedal: Path2D;
  private readonly details: { path: Path2D; group: PartGroup }[] = [];
  private readonly slaveBody: Path2D;
  private readonly masterBody: Path2D;
  private readonly hose: Path2D;
  private readonly teeth: Path2D;
  private readonly splines: Path2D;
  private readonly rivets: Path2D;
  private readonly grooves: Path2D;
  private readonly fingers: Path2D;
  private readonly crankBolts: Path2D;
  private readonly coverBolts: Path2D;
  private theme: Theme | null = null;
  private colors: Record<PartMaterial, string> = {} as Record<PartMaterial, string>;
  private hatches: [CanvasPattern | null, CanvasPattern | null] = [null, null];
  private hatchScale = 0;
  private sparkSeed = 7;
  private k2Lever = '';
  private mmFont = '';
  private frictionKey = NaN;
  private frictionText = '';
  private steelKey = NaN;
  private steelTheme: Theme | null = null;
  private steelText = '';
  private raceKey = '';
  private raceText = '';
  private dualKey = NaN;
  private dualText = '';
  /** Dernière échelle de la coupe, pour les étiquettes et les essais. */
  scale = 1;

  constructor(layout: ClutchLayout) {
    this.layout = layout;
    const d = layout.dims;
    for (const part of sectionParts(layout)) this.drawn.push({ part, path: mirrored(part.points) });
    this.bell = mirrored(bellProfile(layout));

    // La coupe s'arrête sur une ligne brisée, comme sur un plan : le bas du volant n'apprend rien.
    const zig = new Path2D();
    const line = new Path2D();
    const bottom = -layout.rMin;
    zig.moveTo(layout.xMin - 4, -layout.rMax - 4);
    zig.lineTo(layout.xMax + 4, -layout.rMax - 4);
    zig.lineTo(layout.xMax + 4, bottom);
    line.moveTo(layout.xMax, bottom);
    const step = 9;
    for (let x = layout.xMax + 4, k = 0; x > layout.xMin - 4; x -= step, k++) {
      const y = bottom + (k % 2 === 0 ? 0 : 2.5);
      zig.lineTo(x, y);
      line.lineTo(x, y);
    }
    zig.lineTo(layout.xMin - 4, bottom);
    zig.closePath();
    this.clip = zig;
    this.breakLine = line;

    // La fourchette, vue de côté, autour de sa rotule. Le haut sort du carter par sa fenêtre.
    const h = FORK_SLAVE_R - FORK_STUD_R;
    const fork = new Path2D();
    fork.moveTo(-5, 7);
    fork.lineTo(-5, -h * 0.55);
    fork.lineTo(-3, -h - 5);
    fork.lineTo(5, -h - 5);
    fork.lineTo(5, -h * 0.55);
    fork.lineTo(5, 7);
    fork.closePath();
    this.fork = fork;

    const pedal = new Path2D();
    pedal.moveTo(-3, -6);
    pedal.lineTo(3, -6);
    pedal.lineTo(3, 126);
    pedal.lineTo(5, 132);
    pedal.lineTo(15, 132);
    pedal.lineTo(15, 140);
    pedal.lineTo(-13, 140);
    pedal.lineTo(-13, 132);
    pedal.lineTo(-3, 132);
    pedal.closePath();
    this.pedal = pedal;

    // Boulons et rivets vus en coupe : leur tête, et leur tige dans le trou.
    const cBolt = new Path2D();
    const T = layout.T;
    addMirroredRect(cBolt, d.dualMass ? -T + 4 : -7.5, d.dualMass ? -T + 9 : -1.5, layout.boltR - 6, layout.boltR + 6);
    addMirroredRect(cBolt, -T - 9, d.dualMass ? -T + 4 : -7.5, layout.boltR - 3.5, layout.boltR + 3.5);
    this.details.push({ path: cBolt, group: 'crank' });
    const fBolt = new Path2D();
    const r = layout.flangeR - 3;
    addMirroredRect(fBolt, 3, 6.5, r - 4.5, r + 4.5);
    addMirroredRect(fBolt, -9, 3, r - 2.8, r + 2.8);
    if (!layout.dct) {
      addMirroredRect(fBolt, layout.pivotX - 3, layout.coverOut + 1.4, d.pivotR - 1.1, d.pivotR + 1.1);
      addMirroredRect(fBolt, layout.coverOut, layout.coverOut + 1.4, d.pivotR - 2.6, d.pivotR + 2.6);
    }
    this.details.push({ path: fBolt, group: 'mass' });

    const box = layout;
    this.slaveBody = new Path2D();
    this.slaveBody.rect(box.slaveFromX, -box.slaveR - 11, box.slaveToX - box.slaveFromX, 22);
    this.slaveBody.rect(box.slaveFromX + 8, -box.slaveR + 11, 8, 8);
    this.masterBody = new Path2D();
    this.masterBody.rect(box.masterFromX, -box.masterR - 9, box.masterToX - box.masterFromX, 18);
    const elbow = box.slaveToX + 13;
    this.hose = new Path2D();
    this.hose.moveTo(box.masterFromX, -box.masterR);
    this.hose.lineTo(elbow, -box.masterR);
    this.hose.lineTo(elbow, -box.slaveR);
    this.hose.lineTo(box.slaveToX, -box.slaveR);

    // La vue de face se dessine dans une boîte de 100 unités de rayon, à l'échelle du volant.
    const unit = 100 / d.flywheelR;
    const teeth = new Path2D();
    for (let i = 0; i < d.ringTeeth; i++) {
      const a = (i / d.ringTeeth) * TAU;
      teeth.moveTo(Math.cos(a) * (d.flywheelR - 3) * unit, Math.sin(a) * (d.flywheelR - 3) * unit);
      teeth.lineTo(Math.cos(a) * d.flywheelR * unit, Math.sin(a) * d.flywheelR * unit);
    }
    this.teeth = teeth;
    const splines = new Path2D();
    const splineTip = d.shaftR * unit;
    const splineRoot = (d.shaftR - 1.6) * unit;
    for (let i = 0; i < d.splines; i++) {
      const a = (i / d.splines) * TAU;
      const b = ((i + 0.5) / d.splines) * TAU;
      splines[i === 0 ? 'moveTo' : 'lineTo'](Math.cos(a) * splineRoot, Math.sin(a) * splineRoot);
      splines.lineTo(Math.cos(a) * splineTip, Math.sin(a) * splineTip);
      splines.lineTo(Math.cos(b) * splineTip, Math.sin(b) * splineTip);
      splines.lineTo(Math.cos(b) * splineRoot, Math.sin(b) * splineRoot);
    }
    splines.closePath();
    this.splines = splines;
    const rivets = new Path2D();
    const grooves = new Path2D();
    const rings = [(d.liningInnerR + 7) * unit, (d.liningOuterR - 9) * unit];
    const count = 16;
    for (let i = 0; i < count; i++) {
      const a = (i / count) * TAU;
      grooves.moveTo(Math.cos(a) * d.liningInnerR * unit, Math.sin(a) * d.liningInnerR * unit);
      grooves.lineTo(Math.cos(a) * d.liningOuterR * unit, Math.sin(a) * d.liningOuterR * unit);
      for (const ring of rings) {
        const b = a + (TAU / count) * 0.5;
        const x = Math.cos(b) * ring;
        const y = Math.sin(b) * ring;
        rivets.moveTo(x + 2 * unit, y);
        rivets.arc(x, y, 2 * unit, 0, TAU);
      }
    }
    this.rivets = rivets;
    this.grooves = grooves;

    // Les doigts du diaphragme, de face : leur largeur diminue vers le centre, une fente les sépare.
    const fingers = new Path2D();
    const N = d.fingers;
    for (let i = 0; i < N; i++) {
      const a = (i / N) * TAU;
      const half = TAU / N / 2;
      const outer = d.pivotR * unit;
      const inner = d.contactR * unit;
      const gapOuter = 3 * unit / outer * 1.2;
      const gapInner = 1.6 * unit / inner;
      fingers.moveTo(Math.cos(a - half + gapOuter) * outer, Math.sin(a - half + gapOuter) * outer);
      fingers.lineTo(Math.cos(a - half + gapInner) * inner, Math.sin(a - half + gapInner) * inner);
      fingers.lineTo(Math.cos(a + half - gapInner) * inner, Math.sin(a + half - gapInner) * inner);
      fingers.lineTo(Math.cos(a + half - gapOuter) * outer, Math.sin(a + half - gapOuter) * outer);
      fingers.closePath();
    }
    this.fingers = fingers;

    const crankBolts = new Path2D();
    for (let i = 0; i < d.crankBolts; i++) {
      const a = (i / d.crankBolts) * TAU;
      hexagon(crankBolts, Math.cos(a) * layout.boltR * unit, Math.sin(a) * layout.boltR * unit, 6 * unit, a);
    }
    this.crankBolts = crankBolts;
    const coverBolts = new Path2D();
    for (let i = 0; i < d.coverBolts; i++) {
      const a = ((i + 0.5) / d.coverBolts) * TAU;
      hexagon(coverBolts, Math.cos(a) * (layout.flangeR - 3) * unit, Math.sin(a) * (layout.flangeR - 3) * unit, 4.5 * unit, a);
    }
    this.coverBolts = coverBolts;

    const n = layout.noseX;
    const top = 203;
    const second = 186;
    const lo = d.liningOuterR;
    const wall = layout.wallX;
    const inner = BEARING_PITCH_R - BEARING_BALL / 2;
    const outer = BEARING_PITCH_R + BEARING_BALL / 2;
    this.bearingLabels = layout.dct ? [] : [
      { key: 'clutch.race.rotating', group: 'race', ax: n + 12, ar: inner - 3, lx: n + 58, lr: inner - 3, align: 'left' },
      { key: 'clutch.race.fixed', group: 'carrier', ax: n + 14, ar: outer + 3, lx: n + 58, lr: outer + 8, align: 'left' },
      { key: 'clutch.ball', group: 'cage', ax: n + 12, ar: BEARING_PITCH_R, lx: n + 58, lr: BEARING_PITCH_R + 1, align: 'left' },
      { key: 'clutch.cage', group: 'cage', ax: n + 7, ar: BEARING_PITCH_R + 1, lx: n + 2, lr: 62, align: 'center' },
      { key: 'clutch.carrierPart', group: 'carrier', ax: n + 30, ar: layout.tubeOut + 2, lx: n + 58, lr: layout.tubeOut + 2, align: 'left' },
      { key: 'clutch.fingers', group: 'static', ax: layout.tipX - 3, ar: d.contactR + 16, lx: layout.tipX - 26, lr: 56, align: 'right' },
      { key: 'clutch.guide', group: 'static', ax: wall - 30, ar: layout.tubeOut - 1, lx: wall - 18, lr: 8, align: 'left' },
    ];
    if (layout.dct) {
      const c = layout.dct;
      this.labels = [
        { key: 'clutch.ringGear', group: 'crank', ax: -T + 6, ar: d.flywheelR - 2, lx: layout.xMin + 2, lr: top, align: 'left' },
        { key: 'clutch.dmf', group: 'crank', ax: -T / 2, ar: layout.arcR, lx: layout.xMin + 2, lr: second, align: 'left' },
        { key: 'clutch.k1', group: 'k1Lining', ax: c.k1DiscX + 3.7, ar: lo - 8, lx: c.k1DiscX - 6, lr: top, align: 'center' },
        { key: 'clutch.centre', group: 'mass', ax: c.centreX + 9, ar: lo - 2, lx: c.centreX - 6, lr: second, align: 'center' },
        { key: 'clutch.k2', group: 'k2Lining', ax: c.k2DiscX + 3.7, ar: lo - 8, lx: c.k2DiscX + 12, lr: top, align: 'center' },
        { key: 'clutch.tieRods', group: 'k1Plate', ax: c.leverPivotX - 12, ar: c.tieRodR, lx: c.leverPivotX + 26, lr: second, align: 'center' },
        { key: 'clutch.engagement', group: 'k1Carrier', ax: c.k1NoseX + 12, ar: -31, lx: c.k1NoseX + 60, lr: -60, align: 'left' },
        { key: 'clutch.shaftSolid', group: 'k1Shaft', ax: wall + 44, ar: 6, lx: wall + 30, lr: 40, align: 'left' },
        { key: 'clutch.shaftHollow', group: 'k2Shaft', ax: wall - 6, ar: c.hollowR - 1, lx: wall + 30, lr: 24, align: 'left' },
      ];
    } else {
      const dmf = d.dualMass;
      this.labels = [
        { key: 'clutch.ringGear', group: 'crank', ax: dmf ? -T + 6 : -T + 5, ar: d.flywheelR - 2, lx: -16, lr: top, align: 'right' },
        dmf
          ? { key: 'clutch.dmf', group: 'crank', ax: layout.arcX, ar: layout.arcR + 6, lx: -16, lr: second, align: 'right' }
          : { key: 'clutch.flywheel', group: 'crank', ax: -T / 2, ar: d.flywheelR - 22, lx: -16, lr: second, align: 'right' },
        { key: 'clutch.disc', group: 'liningFront', ax: 3.7, ar: lo - 4, lx: -6, lr: top, align: 'left' },
        { key: 'clutch.plate', group: 'plate', ax: layout.plateX + 8, ar: lo - 3, lx: 6, lr: second, align: 'left' },
        { key: 'clutch.cover', group: 'mass', ax: layout.coverOut - 2, ar: layout.wallOut - 1, lx: 34, lr: top, align: 'left' },
        { key: 'clutch.diaphragm', group: 'static', ax: layout.pivotX + 6, ar: d.pivotR - 32, lx: layout.forkX + 22, lr: 90, align: 'left' },
        { key: 'clutch.bearing', group: 'carrier', ax: n + 12, ar: -(BEARING_PITCH_R + 6), lx: layout.forkX + 22, lr: -54, align: 'left' },
        { key: 'clutch.guide', group: 'static', ax: wall - 8, ar: -(layout.tubeOut - 1), lx: wall + 20, lr: -24, align: 'left' },
        { key: 'clutch.shaft', group: 'shaft', ax: wall + 30, ar: -d.shaftR + 2, lx: wall + 20, lr: -38, align: 'left' },
        { key: 'clutch.stud', group: 'static', ax: layout.forkX, ar: FORK_STUD_R, lx: layout.forkX + 22, lr: -76, align: 'left' },
        { key: 'clutch.fork', group: 'static', ax: layout.forkX + 3, ar: 62, lx: layout.forkX + 22, lr: 70, align: 'left' },
        { key: 'clutch.pilot', group: 'crank', ax: -T - 7, ar: -14, lx: layout.xMin + 2, lr: -60, align: 'left' },
        { key: 'clutch.slave', group: 'static', ax: layout.slaveFromX + 22, ar: layout.slaveR + 11, lx: layout.slaveFromX + 8, lr: top - 40, align: 'center' },
        { key: 'clutch.master', group: 'static', ax: layout.masterFromX + 34, ar: layout.masterR + 9, lx: layout.masterFromX + 42, lr: top - 16, align: 'center' },
        { key: 'clutch.gearbox', group: 'static', ax: wall + 6, ar: 62, lx: wall + 16, lr: 44, align: 'left' },
        { key: 'clutch.bell', group: 'static', ax: layout.coverOut - 8, ar: layout.bellR - 1, lx: layout.xMin + 2, lr: 170, align: 'left' },
        { key: 'clutch.pedal', group: 'static', ax: layout.pedalX + 72 * PEDAL_REST_SINE, ar: layout.pedalR - 72, lx: layout.pedalX + 4, lr: 42, align: 'center' },
      ];
    }
  }

  /** Une teinte par degré : la même chaîne tant que la température n'a pas bougé d'un degré. */
  private frictionShade(celsius: number): string {
    const key = Math.round(celsius);
    if (key !== this.frictionKey) {
      this.frictionKey = key;
      this.frictionText = frictionColor(key);
    }
    return this.frictionText;
  }

  private steelShade(theme: Theme, celsius: number): string {
    const key = Math.round(celsius);
    if (key !== this.steelKey || theme !== this.steelTheme) {
      this.steelKey = key;
      this.steelTheme = theme;
      this.steelText = steelColor(theme, key);
    }
    return this.steelText;
  }

  private nextSpark(): number {
    this.sparkSeed = (this.sparkSeed * 1664525 + 1013904223) | 0;
    return (this.sparkSeed >>> 16) / 65536;
  }

  private refresh(ctx: CanvasRenderingContext2D, theme: Theme, scale: number) {
    if (theme === this.theme && scale === this.hatchScale) return;
    this.hatchScale = scale;
    if (theme !== this.theme) {
      this.theme = theme;
      const metal = toHex(theme.metal);
      const text = toHex(theme.text);
      const bg = toHex(theme.bg);
      this.colors = {
        cast: metal,
        steel: mix(metal, text, 0.35),
        sheet: mix(metal, text, 0.18),
        friction: '#8a6a48',
        ring: mix(metal, bg, 0.3),
        race: mix(metal, text, 0.6),
        carrier: mix(metal, bg, 0.55),
        brass: '#c69a52',
        shaft: mix(metal, text, 0.45),
        housing: mix(metal, bg, 0.5),
        crank: mix(metal, bg, 0.25),
        rubber: '#1c1d1f',
        lever: mix(metal, text, 0.55),
      };
      this.k2Lever = mix(metal, GLOW, 0.5);
    }
    this.hatches = [makeHatch(ctx, theme.bg, 0, scale), makeHatch(ctx, theme.bg, 1, scale)];
    this.mmFont = `${10.5 / scale}px "Inter Tight", system-ui, sans-serif`;
  }

  /** Dessine la coupe dans la boîte. */
  drawSection(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive, box: Box, labels: boolean) {
    const l = this.layout;
    const d = l.dims;
    const spanX = l.xMax - l.xMin;
    const spanR = l.rMax - l.rMin;
    const S = Math.min(box.w / spanX, box.h / spanR);
    this.scale = S;
    this.refresh(ctx, theme, S);
    const ox = box.x + (box.w - spanX * S) / 2 - l.xMin * S;
    const oy = box.y + (box.h - spanR * S) / 2 + l.rMax * S;
    const t2 = frame.travel;
    const px = 1 / S;

    ctx.save();
    ctx.translate(ox, oy);
    ctx.scale(S, S);
    ctx.save();
    ctx.clip(this.clip);

    // L'axe, en trait mixte.
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = px;
    ctx.setLineDash(AXIS_DASH);
    ctx.beginPath();
    ctx.moveTo(l.xMin, 0);
    ctx.lineTo(l.wallX + 64, 0);
    ctx.stroke();
    ctx.setLineDash(NO_DASH);

    // Le carter : on voit au travers. La boîte commence derrière son voile.
    ctx.fillStyle = theme.raised;
    ctx.globalAlpha = 0.5;
    ctx.fill(this.bell);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.1 * px;
    ctx.stroke(this.bell);
    ctx.strokeStyle = theme.line;
    ctx.beginPath();
    ctx.rect(l.wallX, -110, 62, 208);
    ctx.stroke();

    // La fourchette passe derrière l'arbre et le chariot : ce sont ses deux branches, de part et d'autre du plan de coupe.
    this.drawFork(ctx, theme, frame, px);

    const heat = live.heat;
    const friction = this.frictionShade(heat);
    const hot = this.steelShade(theme, heat);
    for (const item of this.drawn) {
      const offset = groupOffset(item.part.group, frame);
      if (offset !== 0) {
        ctx.save();
        ctx.translate(offset, 0);
      }
      const material = item.part.material;
      ctx.fillStyle = material === 'friction' ? friction : (item.part.id === 'plate' || item.part.id === 'flywheel' || item.part.id === 'dmfSecondary' || item.part.id === 'centre') ? hot : this.colors[material];
      ctx.fill(item.path);
      const hatch = HATCH_MATERIALS[material];
      if (hatch !== undefined) {
        const pattern = this.hatches[hatch];
        if (pattern) {
          ctx.fillStyle = pattern;
          ctx.fill(item.path);
        }
      }
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 0.55 * px;
      ctx.stroke(item.path);
      if (offset !== 0) ctx.restore();
    }
    for (const detail of this.details) {
      ctx.fillStyle = this.colors.steel;
      ctx.fill(detail.path);
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 0.55 * px;
      ctx.stroke(detail.path);
    }

    if (l.dct) this.drawDctLevers(ctx, theme, frame, px);
    else {
      this.drawDiscDetails(ctx, theme, frame, live);
      this.drawDiaphragm(ctx, theme, frame, px);
      this.drawStraps(ctx, theme, frame, px);
      this.drawBearingDetails(ctx, theme, frame, px, l.noseX - t2.bearing, 0);
    }
    if (d.dualMass) this.drawArcSections(ctx, theme, live);

    // La lueur là où ça frotte, proportionnelle à la température.
    if (heat > 120 && !l.dct) {
      ctx.fillStyle = GLOW;
      ctx.globalAlpha = clamp((heat - 120) / 400, 0, 0.8);
      const gap = groupOffset('hub', frame);
      const rear = groupOffset('liningRear', frame) + l.discWidth;
      const plate = groupOffset('plate', frame) + l.plateX;
      for (let side = -1; side <= 1; side += 2) {
        const y0 = side > 0 ? d.liningInnerR : -d.liningOuterR;
        ctx.fillRect(-1, y0, gap + 2, d.liningOuterR - d.liningInnerR);
        ctx.fillRect(rear - 1, y0, Math.max(plate - rear, 0) + 2, d.liningOuterR - d.liningInnerR);
      }
      ctx.globalAlpha = 1;
    }
    // Étincelles sur la face de friction, seulement si ça dissipe vraiment.
    if (live.slipKw > 1.5 && !l.dct) {
      if (live.frozen) this.sparkSeed = 7;
      ctx.fillStyle = '#ffd9a0';
      const count = Math.min(14, Math.floor(live.slipKw * 1.2));
      for (let i = 0; i < count; i++) {
        const r = d.liningInnerR + this.nextSpark() * (d.liningOuterR - d.liningInnerR);
        const y = (i % 2 === 0 ? -1 : 1) * r;
        const x = i % 3 === 0 ? 0 : groupOffset('liningRear', frame) + l.discWidth;
        ctx.fillRect(x - 0.6, y - 0.6, 1.2, 1.2);
      }
    }

    // Traits de peinture : on lit la rotation jusque sur la coupe.
    this.mark(ctx, theme.bg, -l.T / 2, d.liningOuterR + 6, frame.crankAngle, px);
    this.mark(ctx, theme.bg, l.plateX + groupOffset('plate', frame) + 8, d.liningOuterR - 8, frame.massAngle + 1.2, px);
    this.mark(ctx, theme.text, 3.7 + groupOffset('liningFront', frame), d.liningOuterR - 16, frame.liningAngle, px);
    this.mark(ctx, theme.bg, l.wallX + 28, d.shaftR - 3, frame.hubAngle * 2, px);

    ctx.restore(); // fin du découpage

    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = px;
    ctx.stroke(this.breakLine);

    this.drawCommand(ctx, theme, frame, px);
    ctx.restore();

    if (labels) this.drawLabels(ctx, theme, frame, ox, oy, S);
  }

  private mark(ctx: CanvasRenderingContext2D, color: string, x: number, r: number, angle: number, px: number) {
    ctx.fillStyle = color;
    ctx.globalAlpha = 0.45 + 0.55 * Math.abs(Math.sin(angle));
    ctx.fillRect(x - 1.5 * px, -r * Math.cos(angle) - 1.5 * px, 3 * px, 3 * px);
    ctx.globalAlpha = 1;
  }

  /** Le diaphragme, dans ses deux demi-plans : un levier et sa flexion, jusqu'au bout des doigts. */
  private drawDiaphragm(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    const d = l.dims;
    const lift = groupOffset('plate', frame);
    const edge = d.fulcrumR + 3.5;
    for (let side = -1; side <= 1; side += 2) {
      ctx.beginPath();
      const steps = 12;
      for (let k = 0; k <= steps; k++) {
        const r = d.contactR - 1 + ((edge - d.contactR + 1) * k) / steps;
        const x = diaphragmX(l, frame.travel, lift, r);
        const y = side * r;
        ctx[k === 0 ? 'moveTo' : 'lineTo'](x, y);
      }
      ctx.lineCap = 'butt';
      ctx.lineWidth = l.sheet + 1.4 * px;
      ctx.strokeStyle = theme.bg;
      ctx.stroke();
      ctx.lineWidth = l.sheet;
      ctx.strokeStyle = this.colors.lever;
      ctx.stroke();
    }
  }

  /** Les languettes : elles rappellent le plateau et lui transmettent le couple. Elles plient, sans jeu. */
  private drawStraps(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    const d = l.dims;
    const lift = groupOffset('plate', frame);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.3 * px;
    ctx.beginPath();
    for (let side = -1; side <= 1; side += 2) {
      const r = (d.liningOuterR + 3.5) * side;
      ctx.moveTo(l.plateX + 10 + lift, r);
      ctx.lineTo(l.plateX + 10 + lift, r + 0.1 * side);
      ctx.lineTo(4, (l.wallIn - 0.6) * side);
    }
    ctx.stroke();
  }

  /** Ce qui ne se réduit pas à un contour : billes, segments, ressorts de torsion. */
  private drawDiscDetails(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive) {
    const l = this.layout;
    const d = l.dims;
    const gap = groupOffset('liningFront', frame);
    const cushion = groupOffset('liningRear', frame) - gap;
    const wave = l.liningThickness + 0.3 + cushion / 2;
    // Les segments : une tôle ondulée, plus écrasée quand le plateau serre.
    ctx.strokeStyle = this.colors.steel;
    ctx.lineWidth = 0.8;
    ctx.lineJoin = 'round';
    const amp = 0.25 + cushion / 2;
    for (let side = -1; side <= 1; side += 2) {
      ctx.beginPath();
      const span = d.liningOuterR - d.liningInnerR;
      const steps = 12;
      for (let k = 0; k <= steps; k++) {
        const r = d.liningInnerR + (span * k) / steps;
        const x = gap + wave + (k % 2 === 0 ? -amp : amp) * 0.8;
        ctx[k === 0 ? 'moveTo' : 'lineTo'](x, side * r);
      }
      ctx.stroke();
    }
    // Rivets, à fleur des garnitures.
    ctx.fillStyle = theme.bg;
    for (let side = -1; side <= 1; side += 2) {
      for (const rr of [d.liningInnerR + 7, d.liningOuterR - 9]) {
        ctx.fillRect(gap + 1.6, side * rr - 1.6, 1.6, 3.2);
        ctx.fillRect(gap + l.discWidth + cushion - 3.2, side * rr - 1.6, 1.6, 3.2);
      }
    }
    if (d.dampers > 0) {
      const color = live.transmitting ? theme.accent : this.colors.steel;
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.1;
      for (let side = -1; side <= 1; side += 2) {
        ctx.beginPath();
        ctx.arc(gap + 3.7, side * l.springR, l.springD / 2 - 0.6, 0, TAU);
        ctx.stroke();
      }
    }
  }

  /** Les billes et la cage : la coupe passe au milieu d'une bille, et un point montre si elle roule. */
  private drawBearingDetails(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number, nose: number, radial: number) {
    const pitch = BEARING_PITCH_R + radial;
    const ball = BEARING_BALL / 2;
    const spin = frame.raceAngle * (pitch / ball);
    for (let side = -1; side <= 1; side += 2) {
      const x = nose + 12;
      const y = side * pitch;
      ctx.beginPath();
      ctx.arc(x, y, ball - 0.1, 0, TAU);
      ctx.fillStyle = this.colors.race;
      ctx.fill();
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 0.55 * px;
      ctx.stroke();
      ctx.fillStyle = theme.bg;
      ctx.beginPath();
      ctx.arc(x + Math.cos(spin) * ball * 0.55, y + Math.sin(spin) * ball * 0.55, 0.9, 0, TAU);
      ctx.fill();
      // Le ressort ondulé qui garde la bague fixe à plat, et la laisse glisser pour s'auto-centrer.
      ctx.strokeStyle = this.colors.steel;
      ctx.lineWidth = 0.6;
      ctx.beginPath();
      const w = nose + 20.6;
      const outer = pitch + ball;
      for (let k = 0; k <= 4; k++) {
        const yy = side * (outer + 0.2 + k * 1.1);
        const xx = w + (k % 2 === 0 ? 0 : 0.8);
        ctx[k === 0 ? 'moveTo' : 'lineTo'](xx, yy);
      }
      ctx.stroke();
    }
  }

  /** Coupe d'un ressort en arc, dans son canal plein de graisse. */
  private drawArcSections(ctx: CanvasRenderingContext2D, theme: Theme, live: ClutchLive) {
    const l = this.layout;
    const color = live.transmitting ? theme.accent : this.colors.steel;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.6;
    for (let side = -1; side <= 1; side += 2) {
      ctx.beginPath();
      ctx.arc(l.arcX, side * l.arcR, l.arcD / 2 - 2, 0, TAU);
      ctx.stroke();
      ctx.lineWidth = 0.7;
      ctx.beginPath();
      ctx.arc(l.arcX, side * l.arcR, l.arcD / 2 - 5.5, 0, TAU);
      ctx.stroke();
      ctx.lineWidth = 1.6;
    }
  }

  private drawDctLevers(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    const c = l.dct!;
    const d = l.dims;
    ctx.lineCap = 'butt';
    ctx.lineWidth = l.sheet;
    for (let lever = 0; lever < 2; lever++) {
      const isK1 = lever === 0;
      const tipR = isK1 ? c.k1LeverTipR : c.k2LeverTipR;
      const pivotR = isK1 ? c.k1LeverPivotR : c.k2LeverPivotR;
      // Le levier de K1 dépasse son appui : c'est son bout extérieur qui tire les tirants.
      const outerR = isK1 ? c.tieRodR : pivotR;
      const color = isK1 ? this.colors.lever : this.k2Lever;
      for (let side = -1; side <= 1; side += 2) {
        ctx.strokeStyle = color;
        ctx.lineWidth = l.sheet;
        ctx.beginPath();
        ctx.moveTo(dctLeverX(c, frame, isK1, tipR), side * tipR);
        ctx.lineTo(dctLeverX(c, frame, isK1, outerR), side * outerR);
        ctx.stroke();
        // L'appui du levier, tenu par le couvercle.
        ctx.fillStyle = this.colors.steel;
        ctx.beginPath();
        ctx.arc(dctLeverX(c, frame, isK1, pivotR) + 2.6, side * pivotR, 1.5, 0, TAU);
        ctx.fill();
      }
    }
    // Ce qui reporte les efforts : le tirant autour de tout, et le poussoir direct sur K2.
    ctx.strokeStyle = this.colors.steel;
    ctx.lineWidth = 1.2;
    const k2Back = c.k2PlateX + c.plateThickness + groupOffset('k2Plate', frame);
    for (let side = -1; side <= 1; side += 2) {
      ctx.beginPath();
      ctx.moveTo(k2Back, side * (d.liningOuterR - 10));
      ctx.lineTo(dctLeverX(c, frame, false, d.liningOuterR - 10), side * (d.liningOuterR - 10));
      ctx.stroke();
    }
    const nose1 = c.k1NoseX - frame.k1Travel;
    const nose2 = c.k2NoseX - frame.k2Travel;
    this.drawBearingDetails(ctx, theme, frame, px, nose1, 0);
    this.drawBearingDetails(ctx, theme, frame, px, nose2, 28);
  }

  private drawFork(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    if (l.dct) return;
    const angle = frame.travel.forkAngle;
    // La rotule, vissée dans le carter.
    ctx.fillStyle = this.colors.housing;
    ctx.fillRect(l.forkX - 4, -FORK_STUD_R + 3, 8, 26);
    ctx.fillStyle = this.colors.steel;
    ctx.beginPath();
    ctx.arc(l.forkX, -FORK_STUD_R, 6, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 0.55 * px;
    ctx.stroke();
    ctx.save();
    ctx.translate(l.forkX, -FORK_STUD_R);
    ctx.rotate(-angle);
    ctx.fillStyle = this.colors.ring;
    ctx.globalAlpha = 0.92;
    ctx.fill(this.fork);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.1 * px;
    ctx.stroke(this.fork);
    ctx.restore();
    // Le creux qui coiffe la rotule.
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.beginPath();
    ctx.arc(l.forkX, -FORK_STUD_R, 7, 0, TAU);
    ctx.stroke();
  }

  /** Pédale, maître-cylindre, liquide, récepteur et ressort de rappel. Tout se lit depuis la course réelle. */
  private drawCommand(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    const t2 = frame.travel;
    if (l.dct) {
      this.drawMechatronic(ctx, theme, frame, px);
      return;
    }
    const forkAt = (r: number) => l.forkX - (r - FORK_STUD_R) * t2.forkAngle;

    // Ressort de rappel, du haut de la fourchette à une patte du carter : plus la fourchette avance, plus il s'étire.
    const anchorX = l.forkX + 34;
    const springR = 114;
    const from = forkAt(springR) + 5;
    ctx.strokeStyle = theme.dim;
    ctx.lineWidth = 0.9;
    ctx.beginPath();
    const coils = 7;
    for (let k = 0; k <= coils * 2; k++) {
      const x = from + ((anchorX - from) * k) / (coils * 2);
      const y = -springR + (k === 0 || k === coils * 2 ? 0 : k % 2 === 0 ? -2.4 : 2.4);
      ctx[k === 0 ? 'moveTo' : 'lineTo'](x, y);
    }
    ctx.stroke();
    ctx.fillStyle = theme.dim;
    ctx.fillRect(anchorX, -springR - 3, 2.2, 8);

    // Récepteur : le liquide arrive derrière le piston et le pousse vers le moteur.
    const pistonX = l.slaveToX - 12 - t2.slave;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = this.colors.lever;
    ctx.lineWidth = 1.1 * px;
    ctx.fill(this.slaveBody);
    ctx.stroke(this.slaveBody);
    ctx.fillStyle = FLUID_COLOR;
    ctx.fillRect(pistonX + 5, -l.slaveR - 8.5, l.slaveToX - pistonX - 7, 17);
    ctx.fillStyle = this.colors.steel;
    ctx.fillRect(pistonX, -l.slaveR - 8.8, 5, 17.6);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2.2;
    ctx.beginPath();
    ctx.moveTo(pistonX, -l.slaveR);
    ctx.lineTo(forkAt(l.slaveR) + 4, -l.slaveR);
    ctx.stroke();

    // La colonne de liquide avance d'un bloc quand on appuie : incompressible, elle ne se déplace que du volume chassé.
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 4;
    ctx.stroke(this.hose);
    ctx.strokeStyle = FLUID_COLOR;
    ctx.lineWidth = 2.4;
    ctx.setLineDash(FLUID_DASH);
    ctx.lineDashOffset = -t2.master * 1.2;
    ctx.stroke(this.hose);
    ctx.setLineDash(NO_DASH);
    ctx.lineDashOffset = 0;

    // Maître-cylindre, bocal et tige.
    const masterPiston = l.masterToX - 10 - t2.master;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = this.colors.lever;
    ctx.lineWidth = 1.1 * px;
    ctx.fill(this.masterBody);
    ctx.stroke(this.masterBody);
    ctx.fillStyle = FLUID_COLOR;
    ctx.fillRect(l.masterFromX + 1.5, -l.masterR - 6.5, masterPiston - l.masterFromX - 1.5, 13);
    ctx.fillStyle = this.colors.steel;
    ctx.fillRect(masterPiston, -l.masterR - 7, 5, 14);
    const tankX = l.masterFromX + 8;
    ctx.fillStyle = theme.raised;
    ctx.fillRect(tankX, -l.masterR - 37, 24, 24);
    ctx.fillStyle = FLUID_COLOR;
    ctx.fillRect(tankX + 1, -l.masterR - 26, 22, 12);
    ctx.strokeStyle = this.colors.lever;
    ctx.lineWidth = 1.1 * px;
    ctx.strokeRect(tankX, -l.masterR - 37, 24, 24);
    ctx.strokeStyle = FLUID_COLOR;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(tankX + 12, -l.masterR - 13);
    ctx.lineTo(tankX + 12, -l.masterR - 8);
    ctx.stroke();

    const pedalAngle = Math.asin(pedalSine(t2.master));
    const rodX = l.pedalX + 52 * Math.sin(pedalAngle);
    const rodR = l.pedalR - 52 * Math.cos(pedalAngle);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(masterPiston + 5, -l.masterR);
    ctx.lineTo(rodX, -rodR);
    ctx.stroke();
    ctx.save();
    ctx.translate(l.pedalX, -l.pedalR);
    ctx.rotate(-pedalAngle);
    ctx.fillStyle = this.colors.lever;
    ctx.fill(this.pedal);
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 0.55 * px;
    ctx.stroke(this.pedal);
    ctx.restore();
    ctx.fillStyle = theme.text;
    ctx.beginPath();
    ctx.arc(l.pedalX, -l.pedalR, 3, 0, TAU);
    ctx.fill();
  }

  /** Pas de pédale sur un double embrayage : le calculateur pousse les butées, chacune sa course. */
  private drawMechatronic(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, px: number) {
    const l = this.layout;
    const c = l.dct!;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = this.colors.lever;
    ctx.lineWidth = 1.1 * px;
    const x = c.k1NoseX + 74;
    ctx.fillRect(x, -96, 58, 40);
    ctx.strokeRect(x, -96, 58, 40);
    ctx.fillStyle = theme.dim;
    ctx.font = this.mmFont;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(t('clutch.mechatronic'), x + 29, -76);
    // Deux poussées, deux tiges : on les voit s'écarter quand le couple passe de l'une à l'autre.
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(x, -66);
    ctx.lineTo(c.k1NoseX + 34 - frame.k1Travel, -66);
    ctx.lineTo(c.k1NoseX + 34 - frame.k1Travel, -30);
    ctx.moveTo(x, -84);
    ctx.lineTo(c.k2NoseX + 24 - frame.k2Travel, -84);
    ctx.lineTo(c.k2NoseX + 24 - frame.k2Travel, -60);
    ctx.stroke();
  }

  /** Une étiquette suit la pièce qu'elle nomme, y compris celles qui basculent. */
  private labelOffset(label: Label, frame: ClutchFrame): number {
    const t2 = frame.travel;
    if (label.key === 'clutch.fork') return -(label.ar - FORK_STUD_R) * t2.forkAngle;
    if (label.key === 'clutch.pedal') return 72 * (pedalSine(t2.master) - PEDAL_REST_SINE);
    if (label.key === 'clutch.diaphragm') return -t2.finger * 0.5;
    return groupOffset(label.group, frame);
  }

  private drawLabels(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, ox: number, oy: number, S: number) {
    ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.lineWidth = 0.75;
    for (const label of this.labels) {
      const offset = this.labelOffset(label, frame);
      const ax = ox + (label.ax + offset) * S;
      const ay = oy - label.ar * S;
      const lx = ox + label.lx * S;
      const ly = oy - label.lr * S;
      ctx.strokeStyle = theme.dim;
      ctx.globalAlpha = 0.65;
      ctx.beginPath();
      ctx.moveTo(lx, ly + (label.lr > label.ar ? 6 : -6));
      ctx.lineTo(ax, ay);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.fillStyle = theme.dim;
      ctx.beginPath();
      ctx.arc(ax, ay, 1.4, 0, TAU);
      ctx.fill();
      ctx.textAlign = label.align;
      ctx.fillStyle = theme.text;
      ctx.fillText(t(label.key), lx, ly);
    }
  }

  /** Les mêmes étiquettes, projetées depuis le plan de coupe du modèle 3D. De près, celles de la butée seulement. */
  drawLabels3d(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, stage: Stage3D, out: number[], width: number, height: number, focused: boolean) {
    ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.lineWidth = 0.75;
    const list = focused ? this.bearingLabels : this.labels;
    for (const label of list) {
      const offset = this.labelOffset(label, frame);
      stage.project(out, label.ax + offset, label.ar, 0);
      if (out[2] === 0) continue;
      const ax = out[0] * width;
      const ay = out[1] * height;
      stage.project(out, label.lx, label.lr, 0);
      const lx = out[0] * width;
      // Les rangées du haut restent dans le cadre, même une fois la vue resserrée.
      const ly = Math.max(out[1] * height, label.lr > 195 ? 12 : label.lr > 180 ? 28 : label.lr > 165 ? 44 : 0);
      ctx.strokeStyle = theme.dim;
      ctx.globalAlpha = 0.7;
      ctx.beginPath();
      ctx.moveTo(lx, ly);
      ctx.lineTo(ax, ay);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.fillStyle = theme.text;
      ctx.beginPath();
      ctx.arc(ax, ay, 1.6, 0, TAU);
      ctx.fill();
      ctx.textAlign = label.align;
      ctx.strokeStyle = theme.surface;
      ctx.lineWidth = 3;
      ctx.strokeText(t(label.key), lx, ly);
      ctx.lineWidth = 0.75;
      ctx.fillText(t(label.key), lx, ly);
    }
  }

  /**
   * La vue de face, côté boîte, en trois secteurs fixes : ils découvrent les couches qui tournent dessous.
   * Couvercle, doigts et butée ; disque et ressorts ; volant, ou ressorts en arc du bi-masse.
   */
  drawFace(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive, cx: number, cy: number, R: number) {
    const l = this.layout;
    const d = l.dims;
    this.refresh(ctx, theme, this.scale);
    const k = R / 100; // la boîte de 100 unités
    const u = 100 / d.flywheelR;
    ctx.save();
    ctx.translate(cx, cy);
    // Vu de la boîte, le moteur tourne dans le sens trigonométrique : l'axe vertical est retourné.
    ctx.scale(k, -k);
    const px = 1 / k;

    // Couronne : la dent devient un flou quand elle va trop vite pour l'œil.
    const spin = Math.abs(live.rpm) / 12 / 60;
    ctx.fillStyle = this.steelShade(theme, live.heat);
    ctx.beginPath();
    ctx.arc(0, 0, 100, 0, TAU);
    ctx.fill();
    ctx.save();
    ctx.rotate(frame.crankAngle);
    ctx.globalAlpha = live.frozen ? 1 : 1 - clamp((spin - 0.6) / 1.2, 0, 0.92);
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 1.1 * px;
    ctx.stroke(this.teeth);
    ctx.globalAlpha = 1;
    ctx.fillStyle = theme.bg;
    ctx.rotate(frame.massAngle - frame.crankAngle);
    ctx.fill(this.coverBolts);
    // La marque peinte, qu'on suit de l'œil.
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 3.4;
    ctx.beginPath();
    ctx.arc(0, 0, (d.flywheelR - 12) * u, 0.18, 0.46);
    ctx.stroke();
    ctx.restore();

    // Secteur du bas : le volant nu, ou la masse primaire et ses ressorts.
    this.sector(ctx, 2 * TAU / 3 - TAU / 4 - TAU / 3, 2 * TAU / 3 - TAU / 4);
    if (l.dct) this.faceDisc(ctx, theme, frame, live, u, px, frame.k2Angle, frame.k2Angle);
    else this.faceFlywheel(ctx, theme, frame, live, u, px);
    ctx.restore();

    // Secteur de gauche : le disque.
    this.sector(ctx, 2 * TAU / 3 - TAU / 4, TAU - TAU / 4);
    if (l.dct) this.faceDisc(ctx, theme, frame, live, u, px, frame.k1Angle, frame.k1Angle);
    else this.faceDisc(ctx, theme, frame, live, u, px, frame.liningAngle, frame.hubAngle);
    ctx.restore();

    // Secteur de droite : le couvercle, les doigts, la butée.
    this.sector(ctx, -TAU / 4, TAU / 3 - TAU / 4);
    this.faceCover(ctx, theme, frame, u, px);
    ctx.restore();

    // Les frontières fixes des secteurs.
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 1.2 * px;
    ctx.setLineDash(FACE_DASH);
    ctx.beginPath();
    for (let i = 0; i < 3; i++) {
      const a = -TAU / 4 + (i * TAU) / 3;
      ctx.moveTo(Math.cos(a) * (d.contactR * u + 8), Math.sin(a) * (d.contactR * u + 8));
      ctx.lineTo(Math.cos(a) * (d.flywheelR - 3) * u, Math.sin(a) * (d.flywheelR - 3) * u);
    }
    ctx.stroke();
    ctx.setLineDash(NO_DASH);
    ctx.restore();

    if (R < 70) return;
    ctx.font = '10px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.lineWidth = 2.5;
    const place = R * 0.82;
    for (let i = 0; i < 3; i++) {
      // Le repère est retourné en y : le milieu de chaque secteur, à l'écran.
      const a = TAU / 12 + -TAU / 4 + (i * TAU) / 3 + TAU / 12;
      const text = l.dct ? t(i === 0 ? 'clutch.cover' : i === 1 ? 'clutch.k2' : 'clutch.k1') : t(i === 0 ? 'clutch.diaphragm' : i === 2 ? 'clutch.disc' : d.dualMass ? 'clutch.dmf' : 'clutch.flywheel');
      const x = cx + Math.cos(a) * place;
      const y = cy - Math.sin(a) * place;
      ctx.strokeStyle = theme.surface;
      ctx.strokeText(text, x, y);
      ctx.fillStyle = theme.text;
      ctx.fillText(text, x, y);
    }
  }

  private sector(ctx: CanvasRenderingContext2D, from: number, to: number) {
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.arc(0, 0, 97, from, to);
    ctx.closePath();
    ctx.clip();
  }

  private faceFlywheel(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive, u: number, px: number) {
    const l = this.layout;
    const d = l.dims;
    ctx.fillStyle = this.steelShade(theme, live.heat);
    ctx.beginPath();
    ctx.arc(0, 0, 96, 0, TAU);
    ctx.fill();
    if (d.dualMass) {
      // Le canal : la primaire le porte, la secondaire y plonge ses bras. Deux grands ressorts travaillent entre les deux.
      const arcR = l.arcR * u;
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 15;
      ctx.beginPath();
      ctx.arc(0, 0, arcR, 0, TAU);
      ctx.stroke();
      const span = Math.PI * 0.9;
      const delta = frame.dualMassWindup;
      const color = live.transmitting ? theme.accent : this.colors.steel;
      for (let i = 0; i < 2; i++) {
        const mid = frame.crankAngle - delta / 2 + i * Math.PI;
        this.arcCoil(ctx, color, arcR, mid, span - Math.abs(delta), 6.2, px);
      }
      // Les deux bras du voile, et les butées de la primaire.
      ctx.fillStyle = this.colors.lever;
      ctx.strokeStyle = this.colors.lever;
      for (let i = 0; i < 2; i++) {
        ctx.save();
        ctx.rotate(frame.massAngle + i * Math.PI + Math.PI / 2);
        ctx.fillRect(38 * u, -5, (l.arcR + 6 - 38) * u, 10);
        ctx.restore();
        ctx.save();
        ctx.rotate(frame.crankAngle + i * Math.PI + Math.PI / 2);
        ctx.fillStyle = this.colors.ring;
        ctx.fillRect((l.arcR - 10) * u, -3.5, 20 * u, 7);
        ctx.restore();
      }
    }
    // Les boulons sur le vilebrequin, et le trou du palier pilote.
    ctx.save();
    ctx.rotate(frame.crankAngle);
    ctx.fillStyle = this.colors.steel;
    ctx.fill(this.crankBolts);
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 0.8 * px;
    ctx.stroke(this.crankBolts);
    ctx.restore();
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, 15 * u, 0, TAU);
    ctx.fill();
  }

  private arcCoil(ctx: CanvasRenderingContext2D, color: string, R: number, mid: number, span: number, width: number, px: number) {
    const coils = 18;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5 * px;
    ctx.beginPath();
    for (let k = 0; k <= coils * 2; k++) {
      const a = mid - span / 2 + (span * k) / (coils * 2);
      const r = R + (k % 2 === 0 ? -width : width);
      ctx[k === 0 ? 'moveTo' : 'lineTo'](Math.cos(a) * r, Math.sin(a) * r);
    }
    ctx.stroke();
  }

  private faceDisc(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive, u: number, px: number, lining: number, hub: number) {
    const d = this.layout.dims;
    const lo = d.liningOuterR * u;
    const li = d.liningInnerR * u;
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, 96, 0, TAU);
    ctx.fill();
    // Garnitures : couleur selon la température, rainures, rivets.
    ctx.fillStyle = this.frictionShade(live.heat);
    ctx.beginPath();
    ctx.arc(0, 0, lo, 0, TAU);
    ctx.arc(0, 0, li, TAU, 0, true);
    ctx.fill();
    ctx.save();
    ctx.rotate(lining);
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 0.9 * px;
    ctx.stroke(this.grooves);
    ctx.fillStyle = theme.bg;
    ctx.fill(this.rivets);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2.6;
    ctx.beginPath();
    ctx.arc(0, 0, (li + lo) / 2, -0.14, 0.14);
    ctx.stroke();
    ctx.restore();
    if (live.heat > 120) {
      ctx.strokeStyle = GLOW;
      ctx.globalAlpha = clamp((live.heat - 120) / 350, 0, 0.9);
      ctx.lineWidth = 4.5;
      ctx.beginPath();
      ctx.arc(0, 0, lo - 2.5, 0, TAU);
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
    if (live.slipKw > 1.5) {
      if (live.frozen) this.sparkSeed = 11;
      const count = Math.min(14, Math.floor(live.slipKw * 1.2));
      ctx.fillStyle = '#ffd9a0';
      for (let i = 0; i < count; i++) {
        const a = this.nextSpark() * TAU;
        const r = li + this.nextSpark() * (lo - li);
        ctx.fillRect(Math.cos(a) * r - 0.8, Math.sin(a) * r - 0.8, 1.6, 1.6);
      }
    }
    // Plaque de guidage, solidaire des garnitures : ses fenêtres tiennent les ressorts.
    ctx.fillStyle = this.colors.sheet;
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 0.8 * px;
    ctx.beginPath();
    ctx.arc(0, 0, li - 1, 0, TAU);
    ctx.fill();
    ctx.stroke();
    if (d.dampers > 0) this.faceDamper(ctx, theme, frame, live, u);
    this.faceHub(ctx, theme, hub, live, u);
  }

  /**
   * Les ressorts de torsion. Le moyeu et les garnitures ont chacun leur fenêtre : le ressort loge dans ce qui
   * reste de leur recouvrement. Son centre est donc à mi-chemin des deux, et sa longueur diminue de l'angle qui les sépare.
   */
  private faceDamper(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, live: ClutchLive, u: number) {
    const l = this.layout;
    const d = l.dims;
    const n = d.dampers;
    const r = l.springR * u;
    const windowSpan = (TAU / n) * 0.62;
    const half = (l.springD / 2) * u;
    const color = live.transmitting ? theme.accent : this.colors.lever;
    for (let i = 0; i < n; i++) {
      const base = (i / n) * TAU;
      // Fenêtre, en sombre, dans la plaque de guidage.
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = (half + 1.5) * 2;
      ctx.beginPath();
      ctx.arc(0, 0, r, frame.liningAngle + base - windowSpan / 2, frame.liningAngle + base + windowSpan / 2);
      ctx.stroke();
      const centre = base + (frame.liningAngle + frame.hubAngle) / 2;
      const span = windowSpan - Math.abs(frame.damperWindup);
      const coils = 7;
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let k = 0; k <= coils * 2; k++) {
        const a = centre - span / 2 + (span * k) / (coils * 2);
        const rr = r + (k === 0 || k === coils * 2 ? 0 : k % 2 === 0 ? -half : half) * 0.85;
        ctx[k === 0 ? 'moveTo' : 'lineTo'](Math.cos(a) * rr, Math.sin(a) * rr);
      }
      ctx.stroke();
    }
  }

  private faceHub(ctx: CanvasRenderingContext2D, theme: Theme, angle: number, live: ClutchLive, u: number) {
    const l = this.layout;
    const d = l.dims;
    const color = live.transmitting ? theme.accent : this.colors.lever;
    ctx.save();
    ctx.rotate(angle);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(0, 0, l.hubR * u, 0, TAU);
    ctx.fill();
    // Les bras du voile : ils poussent les ressorts.
    if (d.dampers > 0) {
      ctx.strokeStyle = color;
      ctx.lineWidth = 2.6;
      ctx.beginPath();
      const n = d.dampers;
      for (let i = 0; i < n; i++) {
        const a = ((i + 0.5) / n) * TAU;
        ctx.moveTo(Math.cos(a) * l.hubR * u, Math.sin(a) * l.hubR * u);
        ctx.lineTo(Math.cos(a) * (l.springR + 8) * u, Math.sin(a) * (l.springR + 8) * u);
      }
      ctx.stroke();
    }
    // Les cannelures, sur l'arbre primaire.
    ctx.fillStyle = theme.bg;
    ctx.fill(this.splines);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo((d.shaftR - 1.5) * u, 0);
    ctx.stroke();
    ctx.restore();
  }

  private faceCover(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, u: number, px: number) {
    const l = this.layout;
    ctx.save();
    ctx.rotate(frame.massAngle);
    // Couvercle embouti, ouvert sur les doigts.
    ctx.fillStyle = this.colors.sheet;
    ctx.beginPath();
    ctx.arc(0, 0, l.flangeR * u, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 1 * px;
    ctx.beginPath();
    ctx.arc(0, 0, l.wallOut * u, 0, TAU);
    ctx.stroke();
    ctx.fillStyle = theme.bg;
    ctx.fill(this.coverBolts);
    ctx.beginPath();
    ctx.arc(0, 0, l.coverOpening * u, 0, TAU);
    ctx.fill();
    // Doigts du diaphragme.
    ctx.fillStyle = this.colors.lever;
    ctx.fill(this.fingers);
    // Les languettes, qui courent tangentiellement du couvercle au plateau.
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2.2;
    ctx.beginPath();
    for (let i = 0; i < 3; i++) {
      const a = (i / 3) * TAU;
      const b = a + 0.35;
      ctx.moveTo(Math.cos(a) * l.wallIn * u, Math.sin(a) * l.wallIn * u);
      ctx.lineTo(Math.cos(b) * l.wallIn * u, Math.sin(b) * l.wallIn * u);
    }
    ctx.stroke();
    ctx.restore();
  }

  /**
   * La loupe sur la butée, vue de face : la raison d'être de la pièce. Bague extérieure fixe, avec la fourchette ;
   * bague intérieure entraînée par les doigts ; billes et cage à un peu moins de la moitié de sa vitesse.
   */
  drawLoupe(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, x: number, y: number, R: number, contact: boolean) {
    const k = R / (BEARING_PITCH_R + BEARING_BALL / 2 + 7);
    this.refresh(ctx, theme, this.scale);
    ctx.save();
    ctx.translate(x, y);
    ctx.fillStyle = theme.surface;
    ctx.beginPath();
    ctx.arc(0, 0, R + 3, 0, TAU);
    ctx.fill();
    ctx.lineWidth = contact ? 2 : 1;
    ctx.strokeStyle = contact ? theme.accent : theme.lineStrong;
    ctx.stroke();
    ctx.scale(k, -k);
    const pitch = BEARING_PITCH_R;
    const ball = BEARING_BALL / 2;

    // Bague extérieure : elle ne tourne pas, ses repères ne bougent pas.
    ctx.fillStyle = this.colors.carrier;
    ctx.beginPath();
    ctx.arc(0, 0, pitch + ball + 6.8, 0, TAU);
    ctx.fill();
    ctx.fillStyle = this.colors.race;
    ctx.beginPath();
    ctx.arc(0, 0, pitch + ball + 5, 0, TAU);
    ctx.fill();
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, pitch + ball - 0.4, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * TAU;
      ctx.moveTo(Math.cos(a) * (pitch + ball + 1), Math.sin(a) * (pitch + ball + 1));
      ctx.lineTo(Math.cos(a) * (pitch + ball + 4), Math.sin(a) * (pitch + ball + 4));
    }
    ctx.stroke();

    // La cage et ses billes.
    const balls = 11;
    ctx.save();
    ctx.rotate(frame.cageAngle);
    ctx.strokeStyle = this.colors.brass;
    ctx.lineWidth = 2.6;
    ctx.beginPath();
    ctx.arc(0, 0, pitch, 0, TAU);
    ctx.stroke();
    for (let i = 0; i < balls; i++) {
      const a = (i / balls) * TAU;
      const bx = Math.cos(a) * pitch;
      const by = Math.sin(a) * pitch;
      ctx.fillStyle = theme.bg;
      ctx.beginPath();
      ctx.arc(bx, by, ball + 0.7, 0, TAU);
      ctx.fill();
      ctx.fillStyle = this.colors.steel;
      ctx.beginPath();
      ctx.arc(bx, by, ball, 0, TAU);
      ctx.fill();
      ctx.fillStyle = theme.text;
      ctx.beginPath();
      ctx.arc(bx - 1.1, by - 1.1, 0.9, 0, TAU);
      ctx.fill();
    }
    // Une seule bille marquée : on la suit des yeux sur son tour.
    ctx.fillStyle = theme.accent;
    ctx.beginPath();
    ctx.arc(pitch, 0, ball - 1.2, 0, TAU);
    ctx.fill();
    ctx.restore();

    // Bague intérieure, avec son repère : elle prend la vitesse du volant dès qu'elle touche les doigts.
    ctx.save();
    ctx.rotate(frame.raceAngle);
    ctx.fillStyle = this.colors.lever;
    ctx.beginPath();
    ctx.arc(0, 0, pitch - ball + 0.4, 0, TAU);
    ctx.fill();
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, pitch - ball - 6.8, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = contact ? theme.accent : theme.dim;
    ctx.lineWidth = 2.4;
    ctx.beginPath();
    for (let i = 0; i < 3; i++) {
      const a = (i / 3) * TAU;
      ctx.moveTo(Math.cos(a) * (pitch - ball - 6), Math.sin(a) * (pitch - ball - 6));
      ctx.lineTo(Math.cos(a) * (pitch - ball - 0.5), Math.sin(a) * (pitch - ball - 0.5));
    }
    ctx.stroke();
    ctx.restore();

    // L'arbre et le tube-guide, au centre : ils ne suivent pas la bague.
    ctx.fillStyle = this.colors.housing;
    ctx.beginPath();
    ctx.arc(0, 0, this.layout.tubeOut, 0, TAU);
    ctx.fill();
    ctx.save();
    ctx.rotate(frame.hubAngle);
    ctx.fillStyle = this.colors.shaft;
    ctx.beginPath();
    ctx.arc(0, 0, this.layout.dims.shaftR, 0, TAU);
    ctx.fill();
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(this.layout.dims.shaftR - 1.5, 0);
    ctx.stroke();
    ctx.restore();
    ctx.restore();
  }

  /** Les chiffres de la loupe ne se réécrivent que s'ils changent : pas de chaîne neuve à chaque image. */
  private raceState(frame: ClutchFrame): string {
    const rounded = Math.round(frame.raceRpm / 10) * 10;
    const key = lang() + rounded;
    if (key !== this.raceKey) {
      this.raceKey = key;
      this.raceText = t('clutch.bearing.race', { rpm: whole(rounded) });
    }
    return this.raceText;
  }

  private dualState(frame: ClutchFrame): string {
    const key = Math.round(frame.k1 * 100) * 1000 + Math.round(frame.k2 * 100);
    if (key !== this.dualKey) {
      this.dualKey = key;
      this.dualText = t('clutch.dual.state', { k1: whole(frame.k1 * 100), k2: whole(frame.k2 * 100) });
    }
    return this.dualText;
  }

  /** Ce que la bague fait : on peut le lire, et pas seulement le voir. */
  drawBearingText(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, x: number, y: number, contact: boolean) {
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    ctx.strokeStyle = theme.surface;
    ctx.lineWidth = 3;
    ctx.font = '600 11px "Inter Tight", system-ui, sans-serif';
    const state = this.layout.dct ? this.dualState(frame) : t(contact ? 'clutch.bearing.contact' : 'clutch.bearing.clear');
    ctx.strokeText(state, x, y);
    ctx.fillStyle = contact ? theme.accent : theme.dim;
    ctx.fillText(state, x, y);
    ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
    const race = this.raceState(frame);
    const fixed = t('clutch.bearing.fixed');
    ctx.strokeText(race, x, y + 13);
    ctx.strokeText(fixed, x, y + 26);
    ctx.fillStyle = theme.dim;
    ctx.fillText(race, x, y + 13);
    ctx.fillText(fixed, x, y + 26);
  }

  /**
   * La butée en coupe, grossie : le nez sur les doigts, les deux bagues, la bille, la cage, le chariot sur le tube.
   * C'est ici qu'on voit la garde se fermer, à l'échelle réelle.
   */
  drawBearingZoom(ctx: CanvasRenderingContext2D, theme: Theme, frame: ClutchFrame, box: Box) {
    const l = this.layout;
    if (l.dct) return;
    const t2 = frame.travel;
    const s = Math.min(box.w / 62, box.h / 42);
    const nose = l.noseX;
    this.refresh(ctx, theme, this.scale);
    ctx.save();
    ctx.beginPath();
    ctx.rect(box.x, box.y, box.w, box.h);
    ctx.fillStyle = theme.surface;
    ctx.fill();
    ctx.strokeStyle = t2.contact ? theme.accent : theme.lineStrong;
    ctx.lineWidth = t2.contact ? 1.5 : 1;
    ctx.stroke();
    ctx.clip();
    // Le bas du cadre tombe sur le tube-guide ; le chariot et la butée se déplacent dedans, le diaphragme les rejoint.
    ctx.translate(box.x + box.w * 0.5 - (nose + 8) * s, box.y + box.h + 12 * s);
    ctx.scale(s, s);
    const px = 1 / s;
    for (const item of this.drawn) {
      const g = item.part.group;
      if (g !== 'race' && g !== 'carrier' && g !== 'cage' && item.part.id !== 'guideTube' && item.part.id !== 'shaft') continue;
      const offset = groupOffset(g, frame);
      ctx.save();
      ctx.translate(offset, 0);
      ctx.fillStyle = this.colors[item.part.material];
      ctx.fill(item.path);
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 0.8 * px;
      ctx.stroke(item.path);
      ctx.restore();
    }
    this.drawDiaphragm(ctx, theme, frame, px);
    this.drawBearingDetails(ctx, theme, frame, px, nose - t2.bearing, 0);
    // Le plot de la fourchette, qui pousse le chariot.
    const pad = l.padX - t2.bearing;
    ctx.fillStyle = this.colors.ring;
    ctx.fillRect(pad, -BEARING_PITCH_R - 10, 5, 24);
    ctx.restore();

    ctx.font = '10px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = theme.text;
    ctx.textAlign = 'left';
    ctx.fillText(t('clutch.fingers'), box.x + 4, box.y + 9);
  }

  /** Une note discrète, là où l'échelle n'est pas celle des pièces. */
  drawNote(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, align: CanvasTextAlign) {
    ctx.fillStyle = theme.dim;
    ctx.font = '10px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = align;
    ctx.textBaseline = 'alphabetic';
    ctx.fillText(t('clutch.note.lift', { factor: LIFT_EXAGGERATION }), x, y);
  }
}

const PEDAL_REST_SINE = Math.sin((12 * Math.PI) / 180);
const pedalSine = (master: number) => clamp(PEDAL_REST_SINE - master / 52, -1, 1);

const AXIS_DASH = [6, 2, 1.5, 2];
const FLUID_DASH = [4, 3];
const FACE_DASH = [2, 2];
const NO_DASH: number[] = [];

/** Un contour, et son reflet de l'autre côté de l'axe. En mm, avec y = -r. */
function mirrored(points: readonly number[]): Path2D {
  const path = new Path2D();
  for (const side of [-1, 1]) {
    for (let i = 0; i < points.length; i += 2) {
      const x = points[i];
      const y = side * points[i + 1];
      if (i === 0) path.moveTo(x, y);
      else path.lineTo(x, y);
    }
    path.closePath();
  }
  return path;
}

function addMirroredRect(path: Path2D, x0: number, x1: number, r0: number, r1: number) {
  path.rect(x0, r0, x1 - x0, r1 - r0);
  path.rect(x0, -r1, x1 - x0, r1 - r0);
}

function hexagon(path: Path2D, x: number, y: number, radius: number, angle: number) {
  for (let i = 0; i < 6; i++) {
    const a = angle + (i / 6) * TAU;
    path[i === 0 ? 'moveTo' : 'lineTo'](x + Math.cos(a) * radius, y + Math.sin(a) * radius);
  }
  path.closePath();
}

/** Les hachures d'une pièce coupée, comme sur un plan. Deux sens, pour que les pièces voisines se lisent. */
function makeHatch(ctx: CanvasRenderingContext2D, color: string, direction: 0 | 1, scale: number): CanvasPattern | null {
  const size = 6;
  const tile = document.createElement('canvas');
  tile.width = size;
  tile.height = size;
  const g = tile.getContext('2d');
  if (!g) return null;
  g.strokeStyle = color;
  g.globalAlpha = 0.55;
  g.lineWidth = 1;
  g.beginPath();
  if (direction === 0) {
    g.moveTo(-1, 1);
    g.lineTo(1, -1);
    g.moveTo(-1, size + 1);
    g.lineTo(size + 1, -1);
    g.moveTo(size - 1, size + 1);
    g.lineTo(size + 1, size - 1);
  } else {
    g.moveTo(-1, size - 1);
    g.lineTo(1, size + 1);
    g.moveTo(-1, -1);
    g.lineTo(size + 1, size + 1);
    g.moveTo(size - 1, -1);
    g.lineTo(size + 1, 1);
  }
  g.stroke();
  const pattern = ctx.createPattern(tile, 'repeat');
  pattern?.setTransform(new DOMMatrix().scale(1 / scale, 1 / scale));
  return pattern;
}

