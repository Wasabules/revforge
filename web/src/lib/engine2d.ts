/**
 * Le moteur en écorché 2D : une coupe de face (un rang, avec l'angle des rangées, la culasse, la manivelle) et une
 * coupe le long du vilebrequin (tous les cylindres d'une rangée, l'arbre, le volant, la distribution). Même `EngineLayout`,
 * même `EnginePose` que la 3D. On dessine en millimètres, y vers le haut. Les formes sont calculées une fois, à la
 * construction ; dessiner une image n'alloue rien.
 */
import { mix, type Theme } from './canvas';
import { collectorPosition, DEG, type Cylinder, type EngineLayout, type EnginePose, lobeNoseDeg, lobeRadius, plenumPosition, stackOf, rockerOutline, hasOpenStacks } from './engineLayout';
import { chargerUnit, lobedOutline, type ChargerUnit } from './superchargerLayout';
import { turboChain, type TurboChain, type TurboUnit } from './turboChain';
import { EVC, EVO, IVC, IVO } from './fluidGas';
import { heatColors } from './heat';
import type { GasPainter } from './fluidRender';

type P2 = [number, number];

/** La pointe d'une bougie de préchauffage, du rouge sombre à l'orange vif : une table, pas une chaîne par image. */
const GLOW_STYLES: string[] = Array.from({ length: 17 }, (_, k) => {
  const g = k / 16;
  return `rgb(${Math.round(90 + 165 * g)}, ${Math.round(40 + 130 * g * g)}, ${Math.round(20 + 40 * g * g)})`;
});
const NO_DASH: number[] = [];
const DASH: number[] = [3, 3];

function polyPath(points: readonly P2[]): Path2D {
  const path = new Path2D();
  points.forEach((p, k) => (k === 0 ? path.moveTo(p[0], p[1]) : path.lineTo(p[0], p[1])));
  path.closePath();
  return path;
}

/** Les textes à poser sur le dessin : déjà traduits par la vue. */
export interface Labels2D {
  front: string;
  side: string;
  parts: boolean;
  crank: string;
  flywheel: string;
  piston: string;
  rod: string;
  cam: string;
  valve: string;
  chain: string;
  ignition: string;
  ring: string;
  pan: string;
  head: string;
  block: string;
  /** Le culbuteur d'un simple arbre. */
  rocker?: string;
  /** Le carburateur : absent sur une injection. */
  carburetor?: string;
  /** Les pièces du turbo : absentes sur un moteur qui n'en a pas. */
  compressor?: string;
  turbine?: string;
  shaft?: string;
  intercooler?: string;
  airbox?: string;
  maf?: string;
  chargeHot?: string;
  /** Ce que mesure le tube : toujours écrit, car la coupe de côté peut le replier. */
  turboPipeLength?: string;
  chargeCold?: string;
  blowoff?: string;
  wastegate?: string;
  /** La courroie du compresseur mécanique. */
  belt?: string;
}

export class Engine2D {
  /** Le chemin de l'air, sur un moteur qui a un turbo : `null` sinon, et rien à dessiner. */
  readonly chain: TurboChain | null = null;
  /** Le compresseur mécanique : `null` sur un moteur qui n'en a pas. */
  readonly charger: ChargerUnit | null = null;
  private readonly web: Path2D;
  private readonly webPoints: P2[];
  private readonly rod: Path2D;
  private readonly head: Path2D;
  private readonly block: Path2D;
  private readonly pan: Path2D;
  private readonly oil: Path2D;
  /** Les bossages de came : admission et échappement de chaque cylindre. */
  private readonly lobesIn: Path2D[] = [];
  private readonly lobesEx: Path2D[] = [];
  private readonly portIn: Path2D;
  private readonly portEx: Path2D;
  private readonly cover: Path2D;
  private hatch: CanvasPattern | null = null;
  private hatchTheme: Theme | null = null;
  private readonly frontBounds: { xMin: number; xMax: number; yMin: number; yMax: number };
  private readonly chainDash: number[];
  private readonly rockerPath: Path2D | null;
  private readonly bankList: readonly number[];
  private readonly signList: readonly number[];

  constructor(private readonly layout: EngineLayout) {
    const { bore, crank, rod, pinRadius, mainRadius, deck, valve } = layout;
    this.chainDash = [bore * 0.11, bore * 0.05];
    this.bankList = layout.flat ? [0, 1] : [0];
    this.signList = layout.flat ? [1, -1] : [1];
    // Le flasque : même contour que la 3D.
    const headR = pinRadius + bore * 0.06;
    const waist = mainRadius * 0.92;
    const clear = rod - crank - layout.pistonBottom - bore * 0.04;
    const weightR = Math.min(Math.max(crank * 1.22, mainRadius * 1.5), clear);
    const web: P2[] = [];
    for (let k = 0; k <= 10; k++) {
      const a = (k / 10) * Math.PI;
      web.push([Math.cos(a) * headR, crank + Math.sin(a) * headR]);
    }
    web.push([-waist, mainRadius * 0.12]);
    const spread = 68 * DEG;
    for (let k = 0; k <= 12; k++) {
      const a = -Math.PI / 2 - spread + (2 * spread * k) / 12;
      web.push([Math.cos(a) * weightR, Math.sin(a) * weightR]);
    }
    web.push([waist, mainRadius * 0.12]);
    this.webPoints = web;
    this.web = polyPath(web);

    // La bielle : grosse tête, corps fuselé, petite tête.
    const big = pinRadius + bore * 0.07;
    const small = bore * 0.145;
    const rodPath = new Path2D();
    rodPath.arc(0, 0, big, Math.PI * 0.15, Math.PI * 0.85, true);
    rodPath.lineTo(-bore * 0.085, rod - small);
    rodPath.arc(0, rod, small, Math.PI * 1.3, Math.PI * 1.7 - Math.PI * 2, true);
    rodPath.lineTo(bore * 0.085, rod - small);
    rodPath.closePath();
    rodPath.moveTo(big, 0);
    rodPath.arc(0, 0, big, 0, Math.PI * 2);
    this.rod = rodPath;

    // Ce qui est coupé, hachuré : la culasse, et le bloc avec ses chemises.
    const W = bore * 1.04;
    const topY = valve.headTop;
    const headPath: P2[] = [[-W, deck], [W, deck], [W, deck + bore * 0.85], [W * 0.92, topY], [-W * 0.92, topY], [-W, deck + bore * 0.85]];
    this.head = polyPath(headPath);
    const coverW = valve.coverW;
    const coverTop = valve.coverTop;
    this.rockerPath = valve.rocker ? polyPath(rockerOutline(valve.rocker, bore)) : null;
    this.cover = polyPath([[-coverW, topY], [coverW, topY], [coverW, coverTop - bore * 0.14], [coverW - bore * 0.18, coverTop], [-coverW + bore * 0.18, coverTop], [-coverW, coverTop - bore * 0.14]]);

    const half = bore * 0.66;
    const swing = Math.max(crank * 1.25 + bore * 0.1, mainRadius * 1.6);
    const flange = Math.max(swing + bore * 0.05, half);
    let panTop: number;
    let yBottom: number;
    let panWidth: number;
    const blockPath = new Path2D();
    const toFront = (beta: number, x: number, y: number): P2 => {
      const b = beta * DEG;
      return [x * Math.cos(b) + y * Math.sin(b), -x * Math.sin(b) + y * Math.cos(b)];
    };
    if (layout.banks === 1) {
      panTop = -swing * 0.7;
      panWidth = flange;
      const points: P2[] = [[-flange, panTop], [flange, panTop], [flange, deck * 0.08], [half, deck * 0.42], [half, deck], [-half, deck], [-half, deck * 0.42], [-flange, deck * 0.08]];
      points.forEach((p, k) => (k === 0 ? blockPath.moveTo(p[0], p[1]) : blockPath.lineTo(p[0], p[1])));
    } else if (layout.flat) {
      const cc = Math.max(swing, half);
      panTop = -cc;
      panWidth = cc * 0.8;
      const points: P2[] = [[-deck, -half], [-cc, -half], [-cc * 0.8, -cc], [cc * 0.8, -cc], [cc, -half], [deck, -half], [deck, half], [cc, half], [cc * 0.8, cc], [-cc * 0.8, cc], [-cc, half], [-deck, half]];
      points.forEach((p, k) => (k === 0 ? blockPath.moveTo(p[0], p[1]) : blockPath.lineTo(p[0], p[1])));
    } else {
      panTop = -swing * 0.72;
      panWidth = flange;
      const h = layout.halfDeg;
      const valleyY = toFront(h, -half, half / Math.tan(h * DEG))[1];
      const points: P2[] = [[-flange, panTop], [flange, panTop], [flange, swing * 0.3], toFront(h, half, deck), toFront(h, -half, deck), [0, valleyY], toFront(-h, half, deck), toFront(-h, -half, deck), [-flange, swing * 0.3]];
      points.forEach((p, k) => (k === 0 ? blockPath.moveTo(p[0], p[1]) : blockPath.lineTo(p[0], p[1])));
    }
    blockPath.closePath();
    this.block = blockPath;
    const panDepth = layout.flat ? bore * 0.55 : Math.max(crank * 1.25 + bore * 0.55, bore * 1.1);
    yBottom = panTop - panDepth;
    this.pan = polyPath([[-panWidth, panTop], [-panWidth, panTop - panDepth * 0.2], [-panWidth * 0.86, yBottom], [panWidth * 0.86, yBottom], [panWidth, panTop - panDepth * 0.2], [panWidth, panTop]]);
    const oilTop = layout.flat ? panTop - bore * 0.3 : panTop - panDepth * 0.62;
    this.oil = polyPath([[-panWidth * 0.86, yBottom + 0.5], [panWidth * 0.86, yBottom + 0.5], [panWidth * 0.93, oilTop], [-panWidth * 0.93, oilTop]]);

    // Les conduits, dans la culasse.
    const port = (radius: number): Path2D => {
      const path = new Path2D();
      const portY = deck + bore * 0.62;
      const seatX = valve.x;
      const seatY = deck + valve.seatRise + radius * 0.2;
      path.moveTo(W, portY + radius);
      path.bezierCurveTo(W * 0.72, portY + radius, seatX + radius * 1.1, portY + radius * 0.4, seatX + radius, seatY);
      path.lineTo(seatX - radius, seatY);
      path.bezierCurveTo(seatX - radius * 0.6, portY - radius, W * 0.7, portY - radius, W, portY - radius);
      path.closePath();
      return path;
    };
    this.portIn = port(valve.radiusIn * 0.68);
    this.portEx = port(valve.radiusEx * 0.68);

    // Les bossages de came, un par cylindre, tout autour de leur arbre.
    for (const cylinder of layout.cylinders) {
      for (const exhaust of [false, true]) {
        const side = exhaust ? -cylinder.intakeSide : cylinder.intakeSide;
        const open = exhaust ? EVO : IVO;
        const close = exhaust ? EVC : IVC;
        const nose = lobeNoseDeg(cylinder, valve.followerDeg, side, open, close);
        const points: P2[] = [];
        for (let k = 0; k < 44; k++) {
          const a = (k / 44) * 360;
          const r = lobeRadius(valve.camBase, valve.camLift, a - nose, open, close);
          points.push([Math.sin(a * DEG) * r, Math.cos(a * DEG) * r]);
        }
        (exhaust ? this.lobesEx : this.lobesIn)[cylinder.index] = polyPath(points);
      }
    }

    // Le cadre de la coupe de face.
    const ringR = layout.ring.radius + bore * 0.06;
    let xMax = ringR;
    let yMax = 0;
    for (const cylinder of layout.cylinders) {
      const top = deck + bore * 1.95;
      const p = toFront(cylinder.betaDeg, 0, top);
      const c = toFront(cylinder.betaDeg, coverW, top);
      const d = toFront(cylinder.betaDeg, -coverW, top);
      xMax = Math.max(xMax, Math.abs(p[0]), Math.abs(c[0]), Math.abs(d[0]));
      yMax = Math.max(yMax, p[1], c[1], d[1]);
    }
    const plenum = plenumPosition(layout);
    xMax = Math.max(xMax, Math.abs(plenum.x) + plenum.radius);
    yMax = Math.max(yMax, plenum.y + plenum.radius);
    if (layout.carb) {
      // Le carburateur dépasse du plénum : de côté en ligne si chaque cylindre a le sien, debout sinon.
      if (layout.carb.kind === 'multi' && layout.banks === 1) xMax = Math.max(xMax, bore * 2.75);
      else yMax = Math.max(yMax, plenum.y + bore * 1.5);
    } else if (!layout.turbo && !layout.charger) {
      // Le filtre, et ses cornets : ils dépassent du moteur.
      if (layout.airFilter === 'stacks') {
        for (const cylinder of layout.cylinders) {
          const stack = stackOf(layout, cylinder);
          xMax = Math.max(xMax, Math.abs(stack.to[0]) + stack.radius * 2);
          yMax = Math.max(yMax, stack.to[1] + stack.radius * 2);
        }
      } else {
        const at = this.filterAt();
        xMax = Math.max(xMax, Math.abs(at.x) + bore * 0.72);
        yMax = Math.max(yMax, at.y + bore * 0.72);
      }
    }
    for (let b = 0; b < layout.banks; b++) {
      const collector = collectorPosition(layout, b);
      xMax = Math.max(xMax, Math.abs(collector.x) + bore * 0.45);
      // La turbine pend sous le collecteur.
      if (layout.turbo) yBottom = Math.min(yBottom, collector.y - bore * 1.0);
    }
    // Le chemin de l'air fait le tour du moteur : il lui faut sa place autour.
    if (layout.turbo) {
      const chain = turboChain(layout);
      this.chain = chain;
      xMax = Math.max(xMax, chain.xMax);
      yBottom = Math.min(yBottom, chain.yMin);
      yMax = Math.max(yMax, chain.yMax);
    }
    if (layout.charger) {
      const charger = chargerUnit(layout, layout.charger);
      this.charger = charger;
      xMax = Math.max(xMax, charger.xMax);
      // Un peu de place pour son nom, sous le titre de la vue.
      yMax = Math.max(yMax, charger.yMax + bore * 0.3);
    }
    this.frontBounds = { xMin: -xMax, xMax, yMin: Math.min(yBottom, -ringR), yMax };
  }

  private pattern(ctx: CanvasRenderingContext2D, theme: Theme): CanvasPattern | string {
    if (this.hatchTheme !== theme || !this.hatch) {
      this.hatchTheme = theme;
      const tile = document.createElement('canvas');
      tile.width = 7;
      tile.height = 7;
      const c = tile.getContext('2d');
      if (!c) return theme.raised;
      c.fillStyle = theme.raised;
      c.fillRect(0, 0, 7, 7);
      c.strokeStyle = theme.lineStrong;
      c.lineWidth = 0.8;
      c.beginPath();
      c.moveTo(-1, 8);
      c.lineTo(8, -1);
      c.moveTo(-1, 1);
      c.lineTo(1, -1);
      c.moveTo(6, 8);
      c.lineTo(8, 6);
      c.stroke();
      this.hatch = ctx.createPattern(tile, 'repeat');
    }
    return this.hatch ?? theme.raised;
  }

  draw(ctx: CanvasRenderingContext2D, width: number, height: number, pose: EnginePose, painter: GasPainter, theme: Theme, labels: Labels2D): void {
    const wide = width >= 620;
    const gap = 8;
    const titleH = 16;
    let frontX: number;
    let frontY: number;
    let frontW: number;
    let frontH: number;
    let sideX: number;
    let sideY: number;
    let sideW: number;
    let sideH: number;
    if (wide) {
      frontW = width * 0.44 - gap;
      frontH = height - titleH;
      frontX = 0;
      frontY = titleH;
      sideX = frontW + gap * 2;
      sideY = titleH;
      sideW = width - sideX;
      sideH = height - titleH;
    } else {
      // Un boxer est large et plat de face, et demande de la hauteur le long de l'arbre.
      frontW = width;
      frontH = (height - titleH * 2 - gap) * (this.layout.flat ? 0.4 : 0.62);
      frontX = 0;
      frontY = titleH;
      sideX = 0;
      sideY = frontY + frontH + gap + titleH;
      sideW = width;
      sideH = height - sideY;
    }
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'alphabetic';
    ctx.textAlign = 'left';
    ctx.fillStyle = theme.dim;
    ctx.fillText(labels.front, frontX + 2, frontY - 5);
    ctx.fillText(labels.side, sideX + 2, sideY - 5);

    this.drawFront(ctx, frontX, frontY, frontW, frontH, pose, painter, theme, labels);
    this.drawSide(ctx, sideX, sideY, sideW, sideH, pose, painter, theme, labels);
  }

  // ── La coupe de face ──

  private drawFront(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, pose: EnginePose, painter: GasPainter, theme: Theme, labels: Labels2D): void {
    const layout = this.layout;
    const b = this.frontBounds;
    const scale = Math.min(w / (b.xMax - b.xMin), h / (b.yMax - b.yMin));
    const ox = x + w / 2;
    const oy = y + h / 2 + ((b.yMax + b.yMin) / 2) * scale;
    const { bore } = layout;
    const px = 1 / scale;

    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.clip();
    ctx.translate(ox, oy);
    ctx.scale(scale, -scale);

    const hatch = this.pattern(ctx, theme);
    // ── Le volant, derrière tout : sa couronne qui tourne, et le pignon du démarreur en prise ──
    const crankRad = pose.crankDeg * DEG;
    const { ring, pinion } = layout;
    ctx.lineWidth = px;
    ctx.strokeStyle = theme.line;
    ctx.fillStyle = theme.surface;
    ctx.beginPath();
    ctx.arc(0, 0, ring.radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    const shownTeeth = Math.min(ring.teeth, scale * ring.radius * 2 * Math.PI < ring.teeth * 3 ? ring.teeth >> 1 : ring.teeth);
    const module = (ring.radius * 2) / ring.teeth;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.2 * px;
    ctx.beginPath();
    for (let k = 0; k < shownTeeth; k++) {
      const a = (k / shownTeeth) * Math.PI * 2 - crankRad;
      const c = Math.cos(a);
      const s = Math.sin(a);
      ctx.moveTo(c * (ring.radius - module * 0.6), s * (ring.radius - module * 0.6));
      ctx.lineTo(c * (ring.radius + module * 0.6), s * (ring.radius + module * 0.6));
    }
    ctx.stroke();
    // Le pignon : il n'apparaît en prise qu'au démarreur.
    if (pose.starter > 0.01) {
      ctx.globalAlpha = pose.starter;
      ctx.save();
      ctx.translate(pinion.x, pinion.y);
      ctx.fillStyle = theme.metal;
      ctx.beginPath();
      ctx.arc(0, 0, pinion.radius, 0, Math.PI * 2);
      ctx.fill();
      ctx.rotate(pose.pinionAngle);
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.2 * px;
      ctx.beginPath();
      for (let k = 0; k < pinion.teeth; k++) {
        const a = (k / pinion.teeth) * Math.PI * 2;
        ctx.moveTo(Math.cos(a) * pinion.radius * 0.6, Math.sin(a) * pinion.radius * 0.6);
        ctx.lineTo(Math.cos(a) * (pinion.radius + module * 0.6), Math.sin(a) * (pinion.radius + module * 0.6));
      }
      ctx.stroke();
      ctx.restore();
      ctx.globalAlpha = 1;
    }

    // ── Le bloc et le carter ──
    ctx.fillStyle = theme.surface;
    ctx.globalAlpha = 0.85;
    ctx.fill(this.block);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.5 * px;
    ctx.stroke(this.block);
    ctx.fillStyle = theme.bg;
    ctx.fill(this.pan);
    ctx.fillStyle = '#8a5a14';
    ctx.globalAlpha = 0.55;
    ctx.fill(this.oil);
    ctx.globalAlpha = 1;
    ctx.stroke(this.pan);

    // ── Un rang : la section la plus à l'avant de chaque rangée ──
    for (let bank = 0; bank < layout.banks; bank++) {
      const cylinder = layout.byBank[bank][0];
      this.drawBank(ctx, cylinder, pose, painter, theme, hatch, px);
    }

    // ── L'attelage : la bielle la plus loin d'abord, puis la manivelle ──
    const main = layout.mainRadius;
    const first = layout.byBank[0][0];
    const firstPin = first.pinDeg + pose.crankDeg;
    for (let bank = 0; bank < layout.banks; bank++) {
      const cylinder = layout.byBank[bank][0];
      const i = cylinder.index;
      ctx.save();
      ctx.translate(pose.pinX[i], pose.pinY[i]);
      ctx.rotate(-pose.rodAngle[i]);
      ctx.fillStyle = theme.metal;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.1 * px;
      ctx.fill(this.rod);
      ctx.stroke(this.rod);
      ctx.restore();
    }
    ctx.save();
    ctx.rotate(-firstPin * DEG);
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.fill(this.web);
    ctx.stroke(this.web);
    ctx.restore();
    // Les manetons de ce rang, et le tourillon.
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    for (let bank = 0; bank < layout.banks; bank++) {
      const i = layout.byBank[bank][0].index;
      ctx.beginPath();
      ctx.arc(pose.pinX[i], pose.pinY[i], layout.pinRadius, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }
    ctx.beginPath();
    ctx.arc(0, 0, main, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    // Le repère de rotation.
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 2 * px;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(Math.sin(firstPin * DEG) * main * 0.8, Math.cos(firstPin * DEG) * main * 0.8);
    ctx.stroke();

    this.drawNaturalFilter(ctx, painter, theme, px);
    this.drawTurboFront(ctx, pose, painter, theme, px);
    this.drawChargerFront(ctx, pose, painter, theme, px);

    ctx.restore();

    if (labels.parts) {
      ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
      ctx.fillStyle = theme.dim;
      ctx.textAlign = 'center';
      const labelAt = (text: string, mx: number, my: number) => ctx.fillText(text, ox + mx * scale, oy - my * scale);
      this.labelTurboFront(ctx, labels, labelAt);
      const charger = this.charger;
      if (charger) {
        // Sous le carter : au-dessus, il y a la boîte à air.
        if (labels.compressor) labelAt(labels.compressor, charger.x, charger.y - charger.halfY - bore * 0.2);
        if (labels.belt) labelAt(labels.belt, charger.pulleyX / 2 + bore * 0.2, charger.pulleyY / 2);
        if (labels.airbox) labelAt(labels.airbox, charger.airbox[0], charger.airbox[1] + charger.airboxSize[1] + bore * 0.08);
      }
      labelAt(labels.flywheel, ring.radius * 0.72, -ring.radius * 0.72);
      labelAt(labels.pan, 0, -ring.radius * 0.88);
      labelAt(labels.head, 0, layout.deck + bore * 2.08);
      if (labels.rocker && layout.valve.rocker) {
        // Au-dessus du culbuteur d'admission de la première rangée.
        const first = layout.byBank[0][0];
        const b = first.betaDeg * DEG;
        const x = first.intakeSide * layout.valve.rocker.pivotX;
        const y = layout.valve.coverTop + bore * 0.08;
        labelAt(labels.rocker, x * Math.cos(b) + y * Math.sin(b), -x * Math.sin(b) + y * Math.cos(b));
      }
      if (labels.carburetor) {
        const plenum = plenumPosition(layout);
        if (layout.carb?.kind === 'multi' && layout.banks === 1) labelAt(labels.carburetor, -bore * 2.1, plenum.y + bore * 0.95);
        else labelAt(labels.carburetor, plenum.x, plenum.y + bore * 1.45);
      }
      labelAt(labels.crank, -layout.crank * 1.8, -layout.crank * 0.1);
    }
    ctx.textAlign = 'left';
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
  }

  /** Un corps de carburateur, en coupe : l'étranglement du venturi, et le volet qui tourne dessous. `ux, uy` : vers où il aspire. */
  private drawVenturi(ctx: CanvasRenderingContext2D, x: number, y: number, ux: number, uy: number, open: number, theme: Theme, px: number): void {
    const carb = this.layout.carb;
    if (!carb) return;
    const { bore } = this.layout;
    const throat = (carb.throatM * 1000) / 2;
    const height = bore * 0.95;
    const outer = throat * 1.5;
    // (along, across) dans le repère du corps.
    const put = (along: number, across: number): [number, number] => [x + ux * along - uy * across, y + uy * along + ux * across];
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = px * 1.2;
    for (const sign of [-1, 1]) {
      ctx.beginPath();
      ctx.moveTo(...put(0, sign * outer));
      ctx.lineTo(...put(height, sign * outer * 1.25));
      ctx.lineTo(...put(height, sign * throat * 1.3));
      ctx.lineTo(...put(height * 0.42, sign * throat));
      ctx.lineTo(...put(0, sign * throat * 1.2));
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
    }
    // Le volet : à plat fermé, dans l'axe grand ouvert.
    const angle = 0.14 + open * 1.32;
    const along = Math.sin(angle) * throat * 1.1;
    const across = Math.cos(angle) * throat * 1.1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px * 1.6;
    ctx.beginPath();
    ctx.moveTo(...put(height * 0.16 - along, -across));
    ctx.lineTo(...put(height * 0.16 + along, across));
    ctx.stroke();
  }

  private drawBank(ctx: CanvasRenderingContext2D, cylinder: Cylinder, pose: EnginePose, painter: GasPainter, theme: Theme, hatch: CanvasPattern | string, px: number): void {
    const layout = this.layout;
    const { bore, deck, valve } = layout;
    const i = cylinder.index;
    const side = cylinder.intakeSide;
    const s = pose.piston[i];
    const R = bore / 2;
    const crown = s + layout.pistonTop;
    const net = painter;

    ctx.save();
    ctx.rotate(-cylinder.betaDeg * DEG);

    // La culasse hachurée, le couvre-culasse en verre.
    ctx.fillStyle = theme.surface;
    ctx.globalAlpha = 0.5;
    ctx.fill(this.cover);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = px;
    ctx.stroke(this.cover);
    ctx.fillStyle = hatch;
    ctx.fill(this.head);
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.5 * px;
    ctx.stroke(this.head);

    // Les conduits : la couleur du gaz qu'ils contiennent.
    ctx.save();
    if (side < 0) ctx.scale(-1, 1);
    ctx.fillStyle = theme.cool;
    ctx.globalAlpha = 0.55;
    ctx.fill(this.portIn);
    ctx.restore();
    ctx.save();
    if (side > 0) ctx.scale(-1, 1);
    ctx.fillStyle = pose.gasLive ? net.gasStyle(theme, pose.gasT[i], pose.gasBurnt[i]) : theme.metal;
    ctx.globalAlpha = 0.75;
    ctx.fill(this.portEx);
    ctx.restore();

    // Le cylindre : parois coupées, chemise, gaz.
    const linerBottom = deck - layout.stroke - layout.pistonTop - layout.pistonBottom - bore * 0.02;
    const wall = bore * 0.11;
    ctx.fillStyle = hatch;
    ctx.fillRect(-R - wall, linerBottom, wall, deck - linerBottom);
    ctx.fillRect(R, linerBottom, wall, deck - linerBottom);
    // Les chambres d'eau, de chaque côté.
    ctx.fillStyle = theme.cool;
    ctx.globalAlpha = 0.22;
    ctx.fillRect(-R - wall * 0.68, deck - layout.stroke * 0.85, wall * 0.42, layout.stroke * 0.78);
    ctx.fillRect(R + wall * 0.26, deck - layout.stroke * 0.85, wall * 0.42, layout.stroke * 0.78);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.beginPath();
    ctx.moveTo(-R, linerBottom);
    ctx.lineTo(-R, deck);
    ctx.moveTo(R, linerBottom);
    ctx.lineTo(R, deck);
    ctx.stroke();

    // Le fond du cylindre, sous le piston, reste sombre.
    ctx.fillStyle = theme.bg;
    ctx.fillRect(-R, linerBottom, bore, Math.max(crown - linerBottom, 0));

    // La chambre : la bâtière, le gaz, la flamme.
    const roof = valve.roofRise;
    ctx.beginPath();
    ctx.moveTo(-R, crown);
    ctx.lineTo(-R, deck);
    ctx.lineTo(0, deck + roof);
    ctx.lineTo(R, deck);
    ctx.lineTo(R, crown);
    ctx.closePath();
    const burning = pose.flame[i] > 0;
    if (pose.gasLive) {
      const kelvin = pose.gasT[i];
      const shown = kelvin < 800 ? kelvin : 800 + (kelvin - 800) * 0.33;
      ctx.fillStyle = net.gasStyle(theme, shown, burning ? 0 : pose.gasBurnt[i]);
    } else {
      ctx.fillStyle = theme.bg;
    }
    ctx.fill();
    if (burning) {
      ctx.save();
      ctx.clip();
      // La flamme part de la bougie. La masse brûlée est calculée (Wiebe), la forme du front est une approximation.
      const radius = bore * 0.08 + bore * 0.72 * Math.sqrt(pose.flame[i]);
      ctx.beginPath();
      ctx.arc(0, deck + roof - bore * 0.03, radius, 0, Math.PI * 2);
      ctx.fillStyle = net.gasStyle(theme, 1050 + 300 * (1 - pose.flame[i]), 1);
      ctx.fill();
      ctx.strokeStyle = '#fff4dc';
      ctx.lineWidth = px;
      ctx.stroke();
      ctx.restore();
    }

    // Le carburant.
    if (pose.spray[i] > 0) {
      ctx.strokeStyle = '#bfe4ff';
      ctx.globalAlpha = pose.spray[i];
      ctx.lineWidth = 1.5 * px;
      ctx.setLineDash(DASH);
      ctx.beginPath();
      if (layout.fuel === 'diesel') {
        for (const dir of [-1, 1]) {
          ctx.moveTo(0, deck + roof - bore * 0.06);
          ctx.lineTo(dir * bore * 0.36, deck + roof - bore * 0.06 - bore * 0.14);
        }
      } else {
        ctx.moveTo(side * bore * 0.4, deck + bore * 0.03);
        ctx.lineTo(-side * bore * 0.1, deck - bore * 0.2);
        ctx.moveTo(side * bore * 0.4, deck + bore * 0.03);
        ctx.lineTo(-side * bore * 0.02, deck - bore * 0.08);
      }
      ctx.stroke();
      ctx.setLineDash(NO_DASH);
      ctx.globalAlpha = 1;
    }

    // Le piston : tête, gorges, jupe, axe.
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.beginPath();
    ctx.rect(-R + 0.3, crown - bore * 0.24, bore - 0.6, bore * 0.24);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = theme.bg;
    for (let k = 0; k < 3; k++) {
      const gy = crown - bore * (0.07 + 0.06 * k);
      ctx.fillRect(-R + 0.3, gy, bore * 0.02, bore * 0.018);
      ctx.fillRect(R - 0.3 - bore * 0.02, gy, bore * 0.02, bore * 0.018);
    }
    ctx.fillStyle = theme.metal;
    ctx.globalAlpha = 0.75;
    ctx.fillRect(-R + 0.3, s - layout.pistonBottom, bore * 0.05, crown - bore * 0.24 - (s - layout.pistonBottom));
    ctx.fillRect(R - 0.3 - bore * 0.05, s - layout.pistonBottom, bore * 0.05, crown - bore * 0.24 - (s - layout.pistonBottom));
    ctx.globalAlpha = 1;
    if (layout.fuel === 'diesel') {
      ctx.fillStyle = theme.bg;
      ctx.beginPath();
      ctx.ellipse(0, crown, bore * 0.27, bore * 0.14, 0, Math.PI, Math.PI * 2);
      ctx.fill();
    }

    // Les soupapes et leur commande : la coupe passe par une admission et un échappement.
    this.drawValve(ctx, cylinder, side, valve.radiusIn, pose.liftIn[i] * valve.lift, theme.metal, theme, px, false, pose);
    this.drawValve(ctx, cylinder, -side, valve.radiusEx, pose.liftEx[i] * valve.lift, theme.text, theme, px, true, pose);

    // L'allumage. Sur un simple arbre, l'arbre occupe le milieu : bougie ou injecteur entrent à côté, penchés vers l'échappement.
    const tip = deck + roof - bore * 0.03;
    if (valve.plugTiltDeg !== 0) {
      ctx.save();
      ctx.translate(0, tip);
      ctx.rotate(side * valve.plugTiltDeg * DEG);
      ctx.translate(0, -tip);
    }
    if (layout.fuel === 'diesel') {
      // Injecteur au milieu, bougie de préchauffage à côté, dont la pointe rougit.
      ctx.fillStyle = theme.metal;
      ctx.fillRect(-bore * 0.05, tip + bore * 0.02, bore * 0.1, bore * 1.26);
      ctx.fillRect(-bore * 0.03, tip, bore * 0.06, bore * 0.04);
      ctx.fillStyle = theme.dim;
      ctx.fillRect(-bore * 0.07, tip + bore * 1.28, bore * 0.14, bore * 0.18);
      ctx.save();
      ctx.translate(side * bore * 0.15, tip + bore * 0.02);
      ctx.rotate(side * 0.2);
      ctx.fillStyle = theme.dim;
      ctx.fillRect(-bore * 0.022, bore * 0.04, bore * 0.044, bore * 1.0);
      const g = pose.glow;
      ctx.fillStyle = g > 0.05 ? GLOW_STYLES[Math.round(g * 16)] : theme.lineStrong;
      ctx.fillRect(-bore * 0.018, -bore * 0.17, bore * 0.036, bore * 0.21);
      ctx.restore();
    } else {
      // Bougie : électrode, culot, isolant blanc, puis la bobine crayon.
      ctx.fillStyle = theme.metal;
      ctx.fillRect(-bore * 0.05, tip + bore * 0.05, bore * 0.1, bore * 0.32);
      ctx.fillStyle = '#e9e6dd';
      ctx.fillRect(-bore * 0.04, tip + bore * 0.36, bore * 0.08, bore * 0.32);
      ctx.fillStyle = theme.text;
      ctx.fillRect(-bore * 0.009, tip - bore * 0.03, bore * 0.018, bore * 0.08);
      ctx.fillStyle = theme.dim;
      ctx.fillRect(-bore * 0.08, tip + bore * 0.5, bore * 0.16, bore * 0.86);
      ctx.fillRect(-bore * 0.11, tip + bore * 1.32, bore * 0.22, bore * 0.22);
      if (pose.spark[i] > 0) {
        const size = bore * (0.05 + 0.1 * pose.spark[i]);
        ctx.fillStyle = '#ffffff';
        ctx.strokeStyle = '#a8d8ff';
        ctx.lineWidth = 1.5 * px;
        ctx.globalAlpha = 0.4 + 0.6 * pose.spark[i];
        ctx.beginPath();
        for (let k = 0; k < 4; k++) {
          const a = (k / 4) * Math.PI;
          ctx.moveTo(-Math.cos(a) * size, tip - bore * 0.035 - Math.sin(a) * size);
          ctx.lineTo(Math.cos(a) * size, tip - bore * 0.035 + Math.sin(a) * size);
        }
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(0, tip - bore * 0.035, size * 0.34, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
    }
    if (valve.plugTiltDeg !== 0) ctx.restore();

    // Les jets aux soupapes.
    const jetIn = pose.jetIn[i];
    const jetEx = pose.jetEx[i];
    const space = Math.max(deck - crown + roof * 0.5, 1);
    if (jetIn > 0.02 || jetIn < -0.02) this.jet(ctx, side, deck + valve.seatRise - pose.liftIn[i] * valve.lift, Math.min(Math.abs(jetIn) * 1.8, 1) * space, jetIn > 0 ? theme.cool : theme.accent, px);
    if (jetEx > 0.02 || jetEx < -0.02) this.jet(ctx, -side, deck + valve.seatRise - pose.liftEx[i] * valve.lift, Math.min(Math.abs(jetEx) * 1.4, 1) * space, jetEx > 0 ? '#ffeed6' : theme.cool, px);

    ctx.restore();

    // Admission et échappement : le raccord, dans le plan de la coupe.
    this.drawManifolds(ctx, cylinder, pose, theme, painter, px);
  }

  private jet(ctx: CanvasRenderingContext2D, side: number, from: number, length: number, color: string, px: number): void {
    const { bore, valve } = this.layout;
    const x = side * valve.x;
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.5 * px;
    ctx.setLineDash(DASH);
    ctx.lineDashOffset = from;
    ctx.beginPath();
    for (let k = -1; k <= 1; k++) {
      ctx.moveTo(x + k * bore * 0.09, from);
      ctx.lineTo(x + k * bore * 0.14, from - length);
    }
    ctx.stroke();
    ctx.setLineDash(NO_DASH);
    ctx.lineDashOffset = 0;
  }

  private drawValve(ctx: CanvasRenderingContext2D, cylinder: Cylinder, side: number, radius: number, lift: number, color: string, theme: Theme, px: number, exhaust: boolean, pose: EnginePose): void {
    const { bore, deck, valve } = this.layout;
    ctx.save();
    ctx.translate(side * valve.x, deck + valve.seatRise);
    ctx.rotate(-side * valve.tiltDeg * DEG);
    // Le ressort reste posé : il se comprime de la levée.
    const springBase = valve.stem * 0.55;
    const retainerY = valve.stem - bore * 0.08;
    const top = retainerY - lift;
    const coils = 6;
    const rad = bore * 0.085;
    ctx.strokeStyle = theme.dim;
    ctx.lineWidth = 1.1 * px;
    ctx.beginPath();
    ctx.moveTo(-rad, springBase);
    for (let k = 1; k <= coils * 2; k++) {
      ctx.lineTo(k % 2 === 0 ? -rad : rad, springBase + ((top - springBase) * k) / (coils * 2));
    }
    ctx.stroke();
    // La soupape.
    ctx.translate(0, -lift);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.moveTo(-radius, 0);
    ctx.lineTo(radius, 0);
    ctx.lineTo(radius, bore * 0.02);
    ctx.lineTo(valve.stemRadius, bore * 0.17);
    ctx.lineTo(valve.stemRadius, valve.stem);
    ctx.lineTo(-valve.stemRadius, valve.stem);
    ctx.lineTo(-valve.stemRadius, bore * 0.17);
    ctx.lineTo(-radius, bore * 0.02);
    ctx.closePath();
    ctx.fill();
    // Coupelle et poussoir.
    ctx.fillStyle = theme.lineStrong;
    ctx.fillRect(-bore * 0.095, retainerY, bore * 0.19, bore * 0.018);
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    if (!valve.rocker) {
      ctx.beginPath();
      ctx.rect(-bore * 0.105, valve.stem, bore * 0.21, bore * 0.05);
      ctx.fill();
      ctx.stroke();
    }
    ctx.restore();

    if (valve.rocker && this.rockerPath) {
      // Le culbuteur : dessiné du côté +1, en miroir de l'autre. Il bascule autour de son axe.
      ctx.save();
      ctx.translate(side * valve.rocker.pivotX, valve.rocker.pivotY);
      ctx.scale(side, 1);
      ctx.rotate(-(exhaust ? pose.rockerEx[cylinder.index] : pose.rockerIn[cylinder.index]));
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = px;
      ctx.fill(this.rockerPath);
      ctx.stroke(this.rockerPath);
      ctx.fillStyle = theme.metal;
      ctx.beginPath();
      ctx.arc(0, 0, bore * 0.04, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      ctx.restore();
    }

    // La came, à sa place sur la culasse, qui tourne à demi-vitesse.
    const camX = side * valve.camX;
    const camY = valve.camY;
    ctx.save();
    ctx.translate(camX, camY);
    // Le bossage est décrit dans le repère de la vue de face : on annule l'inclinaison de la rangée, puis la came tourne.
    ctx.rotate((cylinder.betaDeg - pose.camDeg) * DEG);
    const lobe = (exhaust ? this.lobesEx : this.lobesIn)[cylinder.index];
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.fill(lobe);
    ctx.stroke(lobe);
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, valve.camBase * 0.28, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  private drawManifolds(ctx: CanvasRenderingContext2D, cylinder: Cylinder, pose: EnginePose, theme: Theme, painter: GasPainter, px: number): void {
    const layout = this.layout;
    const { bore, deck } = layout;
    const i = cylinder.index;
    const b = cylinder.betaDeg * DEG;
    const c = Math.cos(b);
    const s = Math.sin(b);
    const W = bore * 1.04;
    const portY = deck + bore * 0.62;
    const side = cylinder.intakeSide;
    const plenum = plenumPosition(layout);
    const radius = bore * 0.14;
    const carb = layout.carb;
    const multi = carb?.kind === 'multi';
    if (hasOpenStacks(layout)) {
      // Le cornet : un tube droit, sans plénum, qui s'évase à sa bouche.
      this.drawStack(ctx, stackOf(layout, cylinder), theme, px);
    } else {
      // Le conduit d'admission, du plénum à la culasse ; sur un corps par cylindre, jusqu'à son carburateur.
      const ax = side * W * c + portY * s;
      const ay = -side * W * s + portY * c;
      const toX = multi ? (layout.banks === 1 ? -bore * 1.7 : ax * (layout.flat ? 1 : 0.72)) : plenum.x;
      const toY = multi ? (layout.banks === 1 ? portY : plenum.y - bore * 0.3) : plenum.y;
      ctx.strokeStyle = theme.cool;
      ctx.globalAlpha = 0.6;
      ctx.lineWidth = radius * 2;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(ax, ay);
      ctx.quadraticCurveTo(toX * 0.5 + ax * 0.5, Math.max(ay, toY), toX, toY);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.lineCap = 'butt';
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = px * 1.2;
      if (multi) {
        if (layout.banks === 1) this.drawVenturi(ctx, toX, toY, -1, 0, pose.throttle, theme, px);
        else this.drawVenturi(ctx, toX, toY, 0, 1, pose.throttle, theme, px);
      } else {
      ctx.beginPath();
      ctx.arc(plenum.x, plenum.y, plenum.radius, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      if (carb) this.drawVenturi(ctx, plenum.x, plenum.y + plenum.radius * 0.9, 0, 1, pose.throttle, theme, px);
      }
    }
    // Le conduit d'échappement : il prend la couleur du gaz qui y passe, la pièce chauffe avec la ligne.
    const collector = collectorPosition(layout, cylinder.bank);
    const ex = -side * W * c + portY * s;
    const ey = side * W * s + portY * c;
    ctx.strokeStyle = pose.gasLive ? painter.gasStyle(theme, pose.gasT[i], pose.gasBurnt[i]) : theme.metal;
    ctx.lineWidth = bore * 0.26;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(ex, ey);
    ctx.quadraticCurveTo((ex + collector.x) / 2 + Math.sign(ex || 1) * bore * 0.1, (ey + collector.y) / 2 + bore * 0.2, collector.x, collector.y);
    ctx.stroke();
    ctx.lineCap = 'butt';
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.lineStrong;
    ctx.beginPath();
    ctx.arc(collector.x, collector.y, bore * 0.18, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }

  /** Un cornet : le tube, et sa bouche qui s'évase. L'air y entre : il garde la couleur de l'admission. */
  private drawStack(ctx: CanvasRenderingContext2D, stack: { from: number[]; to: number[]; radius: number }, theme: Theme, px: number): void {
    const dx = stack.to[0] - stack.from[0];
    const dy = stack.to[1] - stack.from[1];
    const length = Math.hypot(dx, dy) || 1;
    const nx = -dy / length;
    const ny = dx / length;
    const r = stack.radius;
    const flareAt = 0.74;
    const at = (share: number, across: number): [number, number] => [stack.from[0] + dx * share + nx * across, stack.from[1] + dy * share + ny * across];
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.beginPath();
    ctx.moveTo(...at(0, r));
    ctx.lineTo(...at(flareAt, r));
    ctx.quadraticCurveTo(...at(0.93, r), ...at(1, r * 1.95));
    ctx.lineTo(...at(1, -r * 1.95));
    ctx.quadraticCurveTo(...at(0.93, -r), ...at(flareAt, -r));
    ctx.lineTo(...at(0, -r));
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // L'air qui y passe, dans l'axe.
    ctx.strokeStyle = theme.cool;
    ctx.globalAlpha = 0.6;
    ctx.lineWidth = r * 1.1;
    ctx.beginPath();
    ctx.moveTo(...at(0.02, 0));
    ctx.lineTo(...at(0.98, 0));
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  /** Où est le filtre d'un moteur sans turbo, vu de face : un peu au-dessus du plénum, côté libre. */
  private filterAt(): { x: number; y: number } {
    const { bore } = this.layout;
    const plenum = plenumPosition(this.layout);
    return { x: plenum.x + (plenum.x < 0 ? -1 : 1) * bore * 0.28, y: plenum.y + bore * 1.05 };
  }

  private drawNaturalFilter(ctx: CanvasRenderingContext2D, painter: GasPainter, theme: Theme, px: number): void {
    const layout = this.layout;
    if (layout.turbo || layout.charger || layout.carb || layout.airFilter === 'stacks') return;
    const { bore } = layout;
    const plenum = plenumPosition(layout);
    const at = this.filterAt();
    ctx.strokeStyle = painter.airStyle(theme, 293, 1.2);
    ctx.lineWidth = bore * 0.2;
    ctx.beginPath();
    ctx.moveTo(plenum.x, plenum.y);
    ctx.lineTo(at.x, at.y - bore * 0.2);
    ctx.stroke();
    this.drawFilterBody(ctx, at.x, at.y, bore * 0.66, bore * 0.34, theme, px);
  }

  /** Le filtre lui-même, sa sortie en bas : la boîte et ses plis, le cône, ou la bouche nue. Même dessin avec ou sans turbo. */
  private drawFilterBody(ctx: CanvasRenderingContext2D, x: number, y: number, hx: number, hy: number, theme: Theme, px: number): void {
    const filter = this.layout.airFilter;
    const { bore } = this.layout;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.beginPath();
    if (filter === 'cone') {
      const base = y - hy;
      const top = y + hy * 1.5;
      ctx.moveTo(x - bore * 0.14, base);
      ctx.lineTo(x - hx * 0.82, base + hy * 0.32);
      ctx.lineTo(x - hx * 0.5, top);
      ctx.lineTo(x + hx * 0.5, top);
      ctx.lineTo(x + hx * 0.82, base + hy * 0.32);
      ctx.lineTo(x + bore * 0.14, base);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = theme.danger;
      ctx.lineWidth = 0.9 * px;
      ctx.beginPath();
      for (let k = 1; k < 8; k++) {
        const share = -1 + (2 * k) / 8;
        ctx.moveTo(x + share * hx * 0.76, base + hy * 0.42);
        ctx.lineTo(x + share * hx * 0.46, top - hy * 0.06);
      }
      ctx.stroke();
      return;
    }
    if (filter === 'stacks') {
      const base = y - hy;
      ctx.moveTo(x - bore * 0.13, base);
      ctx.quadraticCurveTo(x - bore * 0.14, base + hy * 0.7, x - bore * 0.36, base + hy * 0.95);
      ctx.lineTo(x + bore * 0.36, base + hy * 0.95);
      ctx.quadraticCurveTo(x + bore * 0.14, base + hy * 0.7, x + bore * 0.13, base);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
      return;
    }
    ctx.rect(x - hx, y - hy, 2 * hx, 2 * hy);
    ctx.fill();
    ctx.stroke();
    // Les plis de l'élément : ceux d'un panneau sport se voient à sa couleur.
    ctx.strokeStyle = filter === 'panel' ? theme.danger : theme.lineStrong;
    ctx.lineWidth = (filter === 'panel' ? 1.1 : 0.75) * px;
    ctx.beginPath();
    for (let k = 1; k < 8; k++) {
      const fx = x - hx + (2 * hx * k) / 8;
      ctx.moveTo(fx, y - hy * 0.7);
      ctx.lineTo(fx, y + hy * 0.7);
    }
    ctx.stroke();
    ctx.fillStyle = theme.metal;
    ctx.fillRect(x - bore * 0.14, y - hy - bore * 0.3, bore * 0.28, bore * 0.16);
  }

  /** Une roue vue de face : ses aubes qui tournent, ou un disque flou quand l'œil ne les suit plus. */
  private drawWheelFront(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, angle: number, blades: number, sweep: number, blur: number, theme: Theme, px: number): void {
    if (blur > 0.02) {
      ctx.globalAlpha = 0.35 * blur;
      ctx.fillStyle = theme.metal;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1 - 0.8 * blur;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.1 * px;
    ctx.beginPath();
    for (let k = 0; k < blades; k++) {
      const a = angle + (k / blades) * Math.PI * 2;
      ctx.moveTo(x + Math.cos(a) * r * 0.2, y + Math.sin(a) * r * 0.2);
      ctx.lineTo(x + Math.cos(a + sweep) * r, y + Math.sin(a + sweep) * r);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  /** Un carter en colimaçon. */
  private volute(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, start: number): void {
    ctx.beginPath();
    for (let k = 0; k <= 24; k++) {
      const a = start + (k / 24) * Math.PI * 2;
      const radius = r * (0.82 + 0.3 * (k / 24));
      if (k === 0) ctx.moveTo(x + Math.cos(a) * radius, y + Math.sin(a) * radius);
      else ctx.lineTo(x + Math.cos(a) * radius, y + Math.sin(a) * radius);
    }
    ctx.closePath();
  }

  /** Vu de face : tout le chemin de l'air. L'arbre, lui, est dans l'axe du regard : voir `drawTurboSide`. */
  private drawTurboFront(ctx: CanvasRenderingContext2D, pose: EnginePose, painter: GasPainter, theme: Theme, px: number): void {
    const chain = this.chain;
    if (!chain) return;
    const { bore } = this.layout;
    const hot = painter.airStyle(theme, pose.compressorOutK, 2);
    const cold = painter.airStyle(theme, pose.coolerOutK, 2);
    const stroke = (from: number[], cx: number, cy: number, to: number[], width: number, color: string) => {
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineCap = 'round';
      ctx.lineWidth = width + 2 * px;
      ctx.beginPath();
      ctx.moveTo(from[0], from[1]);
      ctx.quadraticCurveTo(cx, cy, to[0], to[1]);
      ctx.stroke();
      ctx.strokeStyle = color;
      ctx.lineWidth = width;
      ctx.stroke();
      ctx.lineCap = 'butt';
    };
    // Le tube froid d'abord : il passe derrière le reste.
    const [cx, cy] = chain.cooler;
    const [chx, chy] = chain.coolerSize;
    // Sur deux rangées, il monte du milieu au milieu : la courbe fait le tour du moteur plutôt que de traverser la coupe.
    const coldMidX = chain.coldFrom[0] === 0 ? chain.units[0].side * chain.xMax * 1.85 : chain.coldFrom[0] + Math.sign(chain.coldFrom[0]) * bore * 0.5;
    stroke(chain.coldFrom, coldMidX, (chain.coldFrom[1] + chain.coldTo[1]) * 0.55, chain.coldTo, bore * 0.2, cold);
    // La soupape de décharge, sur le tube froid : un corps et un clapet qui se lève.
    const t = 0.55;
    const qx = (1 - t) * (1 - t) * chain.coldFrom[0] + 2 * t * (1 - t) * coldMidX + t * t * chain.coldTo[0];
    const qy = (1 - t) * (1 - t) * chain.coldFrom[1] + 2 * t * (1 - t) * (chain.coldFrom[1] + chain.coldTo[1]) * 0.55 + t * t * chain.coldTo[1];
    if (this.layout.releaseValve) {
      const dx = Math.sign(qx || 1);
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = px;
      ctx.beginPath();
      ctx.rect(qx + dx * bore * 0.06 - (dx < 0 ? bore * 0.26 : 0), qy - bore * 0.12, bore * 0.26, bore * 0.24);
      ctx.fill();
      ctx.stroke();
      const lift = bore * 0.18 * pose.blowoff;
      ctx.strokeStyle = pose.blowoff > 0.05 ? theme.accent : theme.text;
      ctx.lineWidth = 2 * px;
      ctx.beginPath();
      ctx.moveTo(qx + dx * (bore * 0.34 + lift), qy - bore * 0.14);
      ctx.lineTo(qx + dx * (bore * 0.34 + lift), qy + bore * 0.14);
      ctx.stroke();
    }
    // L'échangeur, sous le moteur : ses ailettes, et la température de l'air qui le traverse, du chaud au froid.
    const steps = 12;
    const firstSide = chain.units[0].side;
    for (let k = 0; k < steps; k++) {
      const share = k / (steps - 1);
      const T = pose.compressorOutK + (pose.coolerOutK - pose.compressorOutK) * share;
      ctx.fillStyle = painter.airStyle(theme, T, 2);
      const x0 = firstSide > 0 ? cx + chx - ((k + 1) / steps) * 2 * chx : cx - chx + (k / steps) * 2 * chx;
      ctx.fillRect(x0, cy - chy, (2 * chx) / steps + px, 2 * chy);
    }
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 0.75 * px;
    ctx.beginPath();
    for (let k = 1; k < 18; k++) {
      const fx = cx - chx + (2 * chx * k) / 18;
      ctx.moveTo(fx, cy - chy);
      ctx.lineTo(fx, cy + chy);
    }
    ctx.stroke();
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.strokeRect(cx - chx, cy - chy, 2 * chx, 2 * chy);
    for (const unit of chain.units) this.drawUnitFront(ctx, unit, pose, painter, theme, px, stroke, hot);
  }

  private drawUnitFront(ctx: CanvasRenderingContext2D, unit: TurboUnit, pose: EnginePose, painter: GasPainter, theme: Theme, px: number, stroke: (from: number[], cx: number, cy: number, to: number[], width: number, color: string) => void, hot: string): void {
    const { bore } = this.layout;
    // La boîte à air et son conduit, avec le débitmètre.
    const [ax, ay] = unit.airbox;
    const [ahx, ahy] = unit.airboxSize;
    const fresh = painter.airStyle(theme, 293, 1.2);
    stroke([ax, ay - ahy], unit.x + unit.side * bore * 0.6, unit.y + bore * 0.6, [unit.x, unit.y + unit.compressorHousing * 0.2], bore * 0.18, fresh);
    this.drawFilterBody(ctx, ax, ay, ahx, ahy, theme, px);
    // Le tube chaud, jusqu'à l'échangeur.
    stroke(unit.hotFrom, unit.hotFrom[0] + unit.side * bore * 0.2, unit.hotTo[1], unit.hotTo, bore * 0.2, hot);
    // Déporté, le tube d'échappement descend du collecteur à la turbine.
    if (unit.feedRun.length > 0) {
      const from = unit.feedRun[0];
      const to = unit.feedRun[unit.feedRun.length - 1];
      stroke(from, to[0], (from[1] + to[1]) / 2, to, bore * 0.26, mix(theme.raised, heatColors(550).fill, pose.gasLive ? 0.75 : 0.4));
    }
    // Le compresseur, derrière la turbine sur le même arbre : on voit sa volute dépasser.
    const blur = pose.turbineBlur;
    ctx.fillStyle = hot;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    this.volute(ctx, unit.x, unit.y, unit.compressorHousing * 1.12, 1.2);
    ctx.fill();
    ctx.stroke();
    // La turbine et sa volute, qui rougit avec la ligne. Sa roue est devant dans cette vue.
    ctx.fillStyle = theme.raised;
    ctx.beginPath();
    ctx.arc(unit.x, unit.y, unit.turbineHousing * 0.96, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = theme.text;
    ctx.stroke();
    this.drawWheelFront(ctx, unit.x, unit.y, unit.turbineWheel, pose.turbineAngle, 9, 0.5, blur, theme, px);
    // L'actionneur de la wastegate : sa tige sort selon l'ouverture réelle.
    const [wx, wy] = unit.actuator;
    const out = bore * 0.2 * pose.wastegate;
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.beginPath();
    ctx.arc(wx, wy, bore * 0.13, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.lineWidth = 1.5 * px;
    ctx.strokeStyle = pose.wastegate > 0.05 ? theme.accent : theme.text;
    ctx.beginPath();
    ctx.moveTo(wx, wy - bore * 0.13);
    ctx.lineTo(wx - unit.side * bore * 0.08, wy - bore * 0.3 - out);
    ctx.stroke();
  }

  /** Vu de côté : l'arbre et ses deux roues, l'une à côté de l'autre. */
  private drawTurboSide(ctx: CanvasRenderingContext2D, pose: EnginePose, painter: GasPainter, theme: Theme, px: number): void {
    const chain = this.chain;
    if (!chain) return;
    const { bore } = this.layout;
    const unit = chain.units[0];
    // Dans cette coupe, le turbo cacherait les cylindres : il est dessiné plus bas, dans le prolongement de l'échangeur.
    const y = chain.cooler[1];
    const hot = painter.airStyle(theme, pose.compressorOutK, 2);
    const cold = painter.airStyle(theme, pose.coolerOutK, 2);
    // Déporté, le tube d'échappement part de sous le moteur et arrive sur la volute. Replié, ses deux traits disent qu'il est plus long que dessiné.
    if (chain.extraMm > 0) {
      const high = y + unit.turbineHousing + bore * 0.3;
      const start = this.layout.rearZ + bore * 0.3;
      ctx.lineCap = 'round';
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = bore * 0.26 + 2 * px;
      const feed = () => {
        ctx.beginPath();
        ctx.moveTo(start, high + bore * 0.45);
        ctx.lineTo(start, high);
        ctx.lineTo(unit.sideZ, high);
        ctx.lineTo(unit.sideZ, y + unit.turbineHousing);
        ctx.stroke();
      };
      feed();
      ctx.strokeStyle = mix(theme.raised, heatColors(550).fill, pose.gasLive ? 0.75 : 0.4);
      ctx.lineWidth = bore * 0.26;
      feed();
      ctx.lineCap = 'butt';
      if (chain.sideFolded) {
        const middle = (start + unit.sideZ) / 2;
        const half = bore * 0.26;
        ctx.fillStyle = theme.surface;
        ctx.fillRect(middle - bore * 0.07, high - half, bore * 0.14, 2 * half);
        ctx.strokeStyle = theme.text;
        ctx.lineWidth = 1.2 * px;
        ctx.beginPath();
        for (const at of [middle - bore * 0.07, middle + bore * 0.07]) {
          ctx.moveTo(at - bore * 0.06, high - half);
          ctx.lineTo(at + bore * 0.06, high + half);
        }
        ctx.stroke();
      }
    }
    // Le tube chaud longe le moteur jusqu'à l'échangeur, à l'avant.
    const [, coolerY, coolerZ] = chain.cooler;
    const [, coolerHalfY, coolerHalfZ] = chain.coolerSize;
    ctx.lineCap = 'round';
    ctx.lineWidth = bore * 0.18;
    ctx.strokeStyle = hot;
    ctx.beginPath();
    ctx.moveTo(unit.sideCompressorZ + bore * 0.24, y);
    ctx.lineTo(coolerZ - coolerHalfZ, y);
    ctx.stroke();
    ctx.lineCap = 'butt';
    // L'échangeur, vu sur la tranche, et le tube froid qui remonte.
    ctx.fillStyle = cold;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.fillRect(coolerZ - coolerHalfZ, coolerY - coolerHalfY, 2 * coolerHalfZ, 2 * coolerHalfY);
    ctx.strokeRect(coolerZ - coolerHalfZ, coolerY - coolerHalfY, 2 * coolerHalfZ, 2 * coolerHalfY);
    ctx.strokeStyle = cold;
    ctx.lineWidth = bore * 0.18;
    ctx.beginPath();
    ctx.moveTo(coolerZ, coolerY + coolerHalfY);
    ctx.lineTo(coolerZ, this.layout.deck + bore * 1.2);
    ctx.stroke();
    // L'arbre : il traverse le palier, avec une roue à chaque bout.
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.fillRect(unit.sideZ, y - bore * 0.04, unit.sideCompressorZ - unit.sideZ, bore * 0.08);
    ctx.strokeRect(unit.sideZ, y - bore * 0.04, unit.sideCompressorZ - unit.sideZ, bore * 0.08);
    ctx.fillStyle = theme.raised;
    ctx.fillRect(unit.sideZ + bore * 0.26, y - bore * 0.18, unit.sideCompressorZ - unit.sideZ - bore * 0.52, bore * 0.36);
    ctx.strokeRect(unit.sideZ + bore * 0.26, y - bore * 0.18, unit.sideCompressorZ - unit.sideZ - bore * 0.52, bore * 0.36);
    // Les carters, en coupe : chaud du côté turbine, de la couleur de l'air du côté compresseur.
    ctx.fillStyle = mix(theme.raised, heatColors(550).fill, Math.min(1, pose.gasLive ? 0.75 : 0.4));
    ctx.beginPath();
    ctx.rect(unit.sideZ - bore * 0.24, y - unit.turbineHousing, bore * 0.48, 2 * unit.turbineHousing);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = hot;
    ctx.beginPath();
    ctx.rect(unit.sideCompressorZ - bore * 0.24, y - unit.compressorHousing, bore * 0.48, 2 * unit.compressorHousing);
    ctx.fill();
    ctx.stroke();
    // Vue de côté, on voit passer les aubes : leur hauteur apparente suit l'angle réel de l'arbre.
    ctx.strokeStyle = theme.text;
    ctx.globalAlpha = 1 - 0.8 * pose.turbineBlur;
    ctx.lineWidth = 1.1 * px;
    ctx.beginPath();
    for (let k = 0; k < 5; k++) {
      const a = pose.turbineAngle + (k / 5) * Math.PI;
      const h1 = Math.cos(a) * unit.turbineWheel;
      const h2 = Math.cos(a) * unit.compressorWheel;
      ctx.moveTo(unit.sideZ - bore * 0.14, y + h1);
      ctx.lineTo(unit.sideZ + bore * 0.14, y + h1 * 0.5);
      ctx.moveTo(unit.sideCompressorZ + bore * 0.14, y + h2 * 0.5);
      ctx.lineTo(unit.sideCompressorZ - bore * 0.14, y + h2);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  /** Ce que le visiteur peut lire : les noms, seulement si on les lui demande. */
  /** Vu de face : la courroie depuis le vilebrequin, le carter et ce qui tourne dedans, la boîte à air et son conduit. */
  private drawChargerFront(ctx: CanvasRenderingContext2D, pose: EnginePose, painter: GasPainter, theme: Theme, px: number): void {
    const unit = this.charger;
    if (!unit) return;
    const { bore } = this.layout;
    // La courroie : les deux tangentes extérieures aux poulies.
    const dx = unit.pulleyX;
    const dy = unit.pulleyY;
    const distance = Math.hypot(dx, dy);
    const base = Math.atan2(dy, dx);
    const open = Math.acos(Math.min(Math.max((unit.crankPulley - unit.pulleyRadius) / distance, -1), 1));
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = bore * 0.05;
    ctx.beginPath();
    for (const side of [-1, 1]) {
      const a = base + side * open;
      ctx.moveTo(Math.cos(a) * unit.crankPulley, Math.sin(a) * unit.crankPulley);
      ctx.lineTo(dx + Math.cos(a) * unit.pulleyRadius, dy + Math.sin(a) * unit.pulleyRadius);
    }
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(0, 0, unit.crankPulley, 0, Math.PI * 2);
    ctx.lineWidth = bore * 0.03;
    ctx.stroke();

    // La boîte à air, et son conduit.
    const fresh = painter.airStyle(theme, 293, 1.2);
    const hot = painter.airStyle(theme, pose.compressorOutK, 1.6);
    const [ax, ay] = unit.airbox;
    const [ahx, ahy] = unit.airboxSize;
    const eyeY = unit.volumetric ? unit.y + unit.halfY : unit.y;
    ctx.strokeStyle = fresh;
    ctx.lineCap = 'round';
    ctx.lineWidth = bore * 0.2;
    ctx.beginPath();
    ctx.moveTo(ax, ay - ahy);
    ctx.lineTo(unit.x, eyeY);
    ctx.stroke();
    ctx.lineCap = 'butt';
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.fillRect(ax - ahx, ay - ahy, 2 * ahx, 2 * ahy);
    ctx.strokeRect(ax - ahx, ay - ahy, 2 * ahx, 2 * ahy);

    if (unit.volumetric) {
      ctx.fillStyle = hot;
      ctx.beginPath();
      ctx.roundRect(unit.x - unit.halfX, unit.y - unit.halfY, 2 * unit.halfX, 2 * unit.halfY, bore * 0.1);
      ctx.fill();
      ctx.stroke();
      const rotor = (cx: number, lobes: number, angle: number) => {
        const outline = lobes === 3 ? this.lobesThree(unit) : this.lobesFive(unit);
        ctx.save();
        ctx.translate(cx, unit.y);
        ctx.rotate(angle);
        ctx.fillStyle = theme.metal;
        ctx.fill(outline);
        ctx.strokeStyle = theme.text;
        ctx.lineWidth = px;
        ctx.stroke(outline);
        ctx.restore();
      };
      rotor(unit.x - unit.rotorSpacing, unit.lobesA, pose.chargerAngle);
      rotor(unit.x + unit.rotorSpacing, unit.lobesB, -pose.chargerAngle * (unit.lobesA / unit.lobesB));
    } else {
      // De la volute au papillon, et la roue.
      ctx.strokeStyle = hot;
      ctx.lineCap = 'round';
      ctx.lineWidth = bore * 0.2;
      ctx.beginPath();
      ctx.moveTo(unit.outFrom[0], unit.outFrom[1]);
      ctx.lineTo(unit.outTo[0], unit.outTo[1]);
      ctx.stroke();
      ctx.lineCap = 'butt';
      ctx.fillStyle = hot;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.2 * px;
      ctx.beginPath();
      ctx.arc(unit.x, unit.y, unit.halfX, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      this.drawWheelFront(ctx, unit.x, unit.y, unit.wheelRadius, pose.chargerAngle, 11, -0.55, pose.turbineBlur, theme, px);
    }
    // La poulie, devant.
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.beginPath();
    ctx.arc(unit.pulleyX, unit.pulleyY, unit.pulleyRadius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.beginPath();
    for (let k = 0; k < 3; k++) {
      const a = pose.chargerAngle + (k / 3) * Math.PI * 2;
      ctx.moveTo(unit.pulleyX, unit.pulleyY);
      ctx.lineTo(unit.pulleyX + Math.cos(a) * unit.pulleyRadius, unit.pulleyY + Math.sin(a) * unit.pulleyRadius);
    }
    ctx.stroke();
  }

  private lobeCache: { three: Path2D | null; five: Path2D | null } = { three: null, five: null };

  private lobesThree(unit: ChargerUnit): Path2D {
    return (this.lobeCache.three ??= this.lobePath(3, unit.rotorRadius));
  }

  private lobesFive(unit: ChargerUnit): Path2D {
    return (this.lobeCache.five ??= this.lobePath(5, unit.rotorRadius * 0.92));
  }

  private lobePath(lobes: number, radius: number): Path2D {
    const path = new Path2D();
    lobedOutline(lobes, radius).forEach(([x, y], k) => (k === 0 ? path.moveTo(x, y) : path.lineTo(x, y)));
    path.closePath();
    return path;
  }

  /** Dans cette coupe, le carter cacherait la culasse : il est dessiné au-dessus d'elle. */
  private chargerSideY(unit: ChargerUnit): number {
    return this.layout.deck + this.layout.bore * 2.08 + unit.halfY;
  }

  /** Vu de côté : le carter, et sa poulie devant le moteur, reliée à celle du vilebrequin. */
  private drawChargerSide(ctx: CanvasRenderingContext2D, pose: EnginePose, painter: GasPainter, theme: Theme, px: number): void {
    const placed = this.charger;
    if (!placed) return;
    const { bore } = this.layout;
    const hot = painter.airStyle(theme, pose.compressorOutK, 1.6);
    const y = this.chargerSideY(placed);
    const unit = { ...placed, y, pulleyY: y };
    // La courroie, et l'arbre jusqu'à sa poulie.
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = bore * 0.05;
    ctx.beginPath();
    ctx.moveTo(unit.beltZ, unit.pulleyY);
    ctx.lineTo(unit.beltZ, 0);
    ctx.moveTo(unit.z + unit.halfZ, unit.pulleyY);
    ctx.lineTo(unit.beltZ, unit.pulleyY);
    ctx.stroke();
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.2 * px;
    ctx.fillRect(unit.beltZ - bore * 0.08, unit.pulleyY - unit.pulleyRadius, bore * 0.16, 2 * unit.pulleyRadius);
    ctx.strokeRect(unit.beltZ - bore * 0.08, unit.pulleyY - unit.pulleyRadius, bore * 0.16, 2 * unit.pulleyRadius);
    ctx.fillStyle = hot;
    ctx.fillRect(unit.z - unit.halfZ, unit.y - unit.halfY, 2 * unit.halfZ, 2 * unit.halfY);
    ctx.strokeRect(unit.z - unit.halfZ, unit.y - unit.halfY, 2 * unit.halfZ, 2 * unit.halfY);
    if (unit.volumetric) {
      // L'hélice qui défile : ce qu'on voit de rotors qui tournent, le long de leur axe.
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = px;
      const pitch = (2 * unit.halfZ) / 5;
      const shift = (((pose.chargerAngle / (Math.PI * 2)) % 1) + 1) % 1;
      ctx.beginPath();
      for (let k = -1; k < 5; k++) {
        const from = unit.z - unit.halfZ + (k + shift) * pitch;
        const a = Math.max(from, unit.z - unit.halfZ);
        const b = Math.min(from + pitch * 0.8, unit.z + unit.halfZ);
        if (b <= a) continue;
        ctx.moveTo(a, unit.y - unit.halfY * 0.75);
        ctx.lineTo(b, unit.y + unit.halfY * 0.75);
      }
      ctx.stroke();
    }
  }

  private labelTurboFront(ctx: CanvasRenderingContext2D, labels: Labels2D, at: (text: string, x: number, y: number) => void): void {
    const chain = this.chain;
    if (!chain) return;
    const { bore } = this.layout;
    const unit = chain.units[0];
    ctx.textAlign = 'center';
    if (labels.intercooler) at(labels.intercooler, chain.cooler[0], chain.cooler[1] - chain.coolerSize[1] - bore * 0.02);
    if (labels.airbox) at(labels.airbox, unit.airbox[0], unit.airbox[1] + unit.airboxSize[1] + bore * 0.08);
    if (labels.turbine) at(labels.turbine, unit.x, unit.y + bore * 0.02);
    if (labels.chargeHot) at(labels.chargeHot, unit.hotTo[0] + unit.side * bore * 0.2, unit.hotTo[1] + bore * 1.1);
    if (labels.chargeCold) at(labels.chargeCold, (chain.coldFrom[0] + chain.coldTo[0]) * 0.28, (chain.coldFrom[1] + chain.coldTo[1]) * 0.28);
  }

  private labelTurboSide(ctx: CanvasRenderingContext2D, labels: Labels2D, at: (text: string, z: number, y: number) => void): void {
    const chain = this.chain;
    if (!chain) return;
    const { bore } = this.layout;
    const unit = chain.units[0];
    const y = chain.cooler[1];
    ctx.textAlign = 'center';
    if (labels.shaft) at(labels.shaft, (unit.sideZ + unit.sideCompressorZ) / 2, y + bore * 0.26);
    if (labels.compressor) at(labels.compressor, unit.sideCompressorZ + bore * 0.3, y - unit.compressorHousing - bore * 0.22);
    if (labels.turbine) at(labels.turbine, unit.sideZ, y - unit.turbineHousing - bore * 0.22);
    ctx.textAlign = 'right';
    if (labels.intercooler) at(labels.intercooler, chain.cooler[2] - chain.coolerSize[2] - bore * 0.08, chain.cooler[1] + chain.coolerSize[1] * 0.6);
    ctx.textAlign = 'center';
  }

  // ── La coupe le long du vilebrequin ──

  private drawSide(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, pose: EnginePose, painter: GasPainter, theme: Theme, labels: Labels2D): void {
    const layout = this.layout;
    const { bore, deck, valve, ring } = layout;
    const flat = layout.flat;
    const zMin = Math.min(ring.z - bore * 0.5, this.chain ? this.chain.sideZMin : Infinity);
    // L'échangeur est devant la poulie : la coupe s'allonge pour lui.
    const zMax = this.chain ? this.chain.zMax : this.charger ? this.charger.zMax : layout.pulleyZ + bore * 0.2;
    const top = this.charger ? this.chargerSideY(this.charger) + this.charger.halfY + bore * 0.3 : deck + bore * 2.0;
    const bottom = Math.min(flat ? -top : Math.min(-ring.radius, -(layout.crank * 1.25 + bore * 1.0)), this.chain ? this.chain.yMin : 0);
    const scale = Math.min(w / (zMax - zMin), h / (top - bottom));
    // Sur un turbo, la coupe n'est plus symétrique : le zéro se place depuis l'arrière.
    const ox = this.chain || this.charger ? x + (w - (zMax - zMin) * scale) / 2 - zMin * scale : x + (w - (zMax - zMin) * scale) / 2 + zMax * scale;
    const oy = y + (h - (top - bottom) * scale) / 2 + top * scale;
    const px = 1 / scale;
    // L'avant à droite, côté distribution : u = z.
    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.clip();
    ctx.translate(ox, oy);
    ctx.scale(scale, -scale);
    const hatch = this.pattern(ctx, theme);

    const uBlockFront = layout.frontZ + bore * 0.06;
    const uBlockRear = layout.rearZ - bore * 0.04;
    for (const bank of this.bankList) {
      const sign = flat && bank === 1 ? -1 : 1;
      const cylinders = layout.byBank[bank];
      ctx.save();
      ctx.scale(1, sign);
      // Le bloc et la culasse, coupés dans le plan de la rangée.
      ctx.fillStyle = hatch;
      ctx.fillRect(uBlockRear, deck, uBlockFront - uBlockRear, bore * 1.0);
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1.5 * px;
      ctx.strokeRect(uBlockRear, deck, uBlockFront - uBlockRear, bore * 1.0);
      const linerBottom = deck - layout.stroke - layout.pistonTop - layout.pistonBottom - bore * 0.02;
      ctx.fillStyle = theme.surface;
      ctx.fillRect(uBlockRear, linerBottom, uBlockFront - uBlockRear, deck - linerBottom);
      ctx.strokeRect(uBlockRear, linerBottom, uBlockFront - uBlockRear, deck - linerBottom);
      // Le couvre-culasse.
      ctx.fillStyle = theme.surface;
      ctx.globalAlpha = 0.5;
      ctx.fillRect(uBlockRear, deck + bore, uBlockFront - uBlockRear, bore * 0.75);
      ctx.globalAlpha = 1;
      ctx.lineWidth = px;
      ctx.strokeRect(uBlockRear, deck + bore, uBlockFront - uBlockRear, bore * 0.75);
      // L'arbre à cames : un trait, ses bossages.
      ctx.fillStyle = theme.metal;
      ctx.fillRect(uBlockRear, valve.camY - valve.camBase * 0.35, uBlockFront + bore * 0.22 - uBlockRear, valve.camBase * 0.7);
      for (const cylinder of cylinders) this.drawSideCylinder(ctx, cylinder, pose, painter, theme, hatch, px, linerBottom);
      ctx.restore();
    }

    // ── Le vilebrequin vu de côté ──
    const r = layout.crank;
    const axisRef = flat ? 90 : layout.byBank[0][0].betaDeg;
    const crankAngle = pose.crankDeg - axisRef;
    const points = this.webPoints;
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    for (const main of layout.mains) {
      ctx.fillRect(main.z - main.width / 2 - 1, -layout.mainRadius, main.width + 2, layout.mainRadius * 2);
      ctx.strokeRect(main.z - main.width / 2 - 1, -layout.mainRadius, main.width + 2, layout.mainRadius * 2);
    }
    const webFill = theme.raised;
    for (const item of layout.throws) {
      const front = item.pins[0];
      const rear = item.pins[item.pins.length - 1];
      for (let k = 0; k < 2; k++) {
        const deg = (k === 0 ? front.deg : rear.deg) + crankAngle;
        const u = k === 0 ? item.zFront - layout.webWidth : item.zRear;
        const a = deg * DEG;
        const cos = Math.cos(a);
        const sin = Math.sin(a);
        let lo = Infinity;
        let hi = -Infinity;
        // La hauteur vue de côté : le contour tourné, projeté sur l'axe de la rangée.
        for (let p = 0; p < points.length; p++) {
          const v = points[p][1] * cos - points[p][0] * sin;
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
        ctx.fillStyle = webFill;
        ctx.fillRect(u, lo, layout.webWidth, hi - lo);
        ctx.strokeRect(u, lo, layout.webWidth, hi - lo);
      }
      for (const pin of item.pins) {
        const v = r * Math.cos((pin.deg + crankAngle) * DEG);
        ctx.fillStyle = theme.metal;
        ctx.fillRect(pin.z - pin.width / 2, v - layout.pinRadius, pin.width, layout.pinRadius * 2);
        ctx.strokeRect(pin.z - pin.width / 2, v - layout.pinRadius, pin.width, layout.pinRadius * 2);
      }
    }
    // Le nez, le pignon, le damper.
    ctx.fillStyle = theme.metal;
    ctx.fillRect(layout.frontZ, -bore * 0.17, layout.pulleyZ + bore * 0.1 - layout.frontZ, bore * 0.34);
    for (const z of layout.sprocket.z) ctx.fillRect(z - bore * 0.045, -layout.sprocket.crankRadius, bore * 0.09, layout.sprocket.crankRadius * 2);
    ctx.fillStyle = theme.raised;
    ctx.fillRect(layout.pulleyZ - bore * 0.13, -bore * 0.72, bore * 0.26, bore * 1.44);
    ctx.strokeRect(layout.pulleyZ - bore * 0.13, -bore * 0.72, bore * 0.26, bore * 1.44);
    // La chaîne, du pignon de vilebrequin à celui de la came. Ses maillons avancent avec le moteur.
    const sprocketZ = layout.sprocket.z[0];
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = bore * 0.03;
    ctx.setLineDash(this.chainDash);
    ctx.lineDashOffset = (pose.chainTurns * Math.PI * 2 * layout.sprocket.crankRadius) % (bore * 0.16);
    const signs = this.signList;
    for (const s of signs) {
      ctx.beginPath();
      ctx.moveTo(sprocketZ, s * layout.sprocket.crankRadius);
      ctx.lineTo(sprocketZ, s * (valve.camY + layout.sprocket.camRadius));
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(sprocketZ, s * -layout.sprocket.crankRadius);
      ctx.lineTo(sprocketZ, s * (valve.camY - layout.sprocket.camRadius));
      ctx.stroke();
    }
    ctx.setLineDash(NO_DASH);
    ctx.lineDashOffset = 0;
    ctx.fillStyle = theme.metal;
    for (const s of signs) {
      const cy = s * valve.camY;
      ctx.fillRect(sprocketZ - bore * 0.045, cy - layout.sprocket.camRadius, bore * 0.09, layout.sprocket.camRadius * 2);
    }

    // ── Le volant et le démarreur ──
    const thick = bore * 0.32;
    const face = ring.z - ring.width / 2;
    ctx.fillStyle = theme.metal;
    ctx.fillRect(face - thick + ring.width, -ring.radius + 2, thick, ring.radius * 2 - 4);
    ctx.fillStyle = theme.lineStrong;
    ctx.fillRect(face, -ring.radius - 2, ring.width, ring.radius * 2 + 4);
    // Les dents qui défilent, sur la tranche.
    const module = (ring.radius * 2) / ring.teeth;
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = px;
    ctx.beginPath();
    const shift = (pose.crankDeg * DEG * ring.radius) % module;
    const shownStep = module < 3 * px ? module * 2 : module;
    for (let v = -ring.radius + (shift % shownStep); v < ring.radius; v += shownStep) {
      ctx.moveTo(face, v);
      ctx.lineTo(face + ring.width, v);
    }
    ctx.stroke();
    ctx.fillStyle = theme.metal;
    ctx.fillRect(layout.rearZ - 1, -layout.mainRadius * 1.25, face + ring.width - layout.rearZ + 1, layout.mainRadius * 2.5);
    if (layout.dualMass) {
      const z2 = face - thick - bore * 0.26;
      ctx.fillStyle = theme.raised;
      ctx.fillRect(z2 - bore * 0.11, -ring.radius * 0.85, bore * 0.22, ring.radius * 1.7);
      ctx.strokeStyle = theme.lineStrong;
      ctx.strokeRect(z2 - bore * 0.11, -ring.radius * 0.85, bore * 0.22, ring.radius * 1.7);
      // Les ressorts en arc entre les deux masses.
      ctx.strokeStyle = theme.accent;
      ctx.lineWidth = 1.5 * px;
      ctx.beginPath();
      for (let k = 0; k <= 8; k++) {
        const u = z2 + bore * 0.11 + (k % 2) * bore * 0.1;
        const v = ring.radius * 0.72 - k * bore * 0.04;
        if (k === 0) ctx.moveTo(u, v);
        else ctx.lineTo(u, v);
      }
      ctx.stroke();
    }
    // Le pignon, qui entre dans la couronne, et le corps du démarreur devant lui.
    const starterV = -ring.radius - layout.pinion.radius;
    const pinionZ = ring.z + bore * 0.22 * (1 - pose.starter);
    ctx.fillStyle = theme.raised;
    ctx.fillRect(ring.z + bore * 0.34, starterV - bore * 0.32, bore * 1.4, bore * 0.64);
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = px;
    ctx.strokeRect(ring.z + bore * 0.34, starterV - bore * 0.32, bore * 1.4, bore * 0.64);
    ctx.fillStyle = pose.starter > 0.5 ? theme.accent : theme.metal;
    ctx.fillRect(pinionZ - ring.width * 0.55, starterV - layout.pinion.radius, ring.width * 1.1, layout.pinion.radius * 2);

    if (!flat) {
      // Le carter d'huile, à l'échelle.
      const panTop = -Math.max(layout.crank * 1.25 + bore * 0.1, layout.mainRadius * 1.6) * 0.72;
      const depth = Math.max(layout.crank * 1.25 + bore * 0.55, bore * 1.1);
      ctx.fillStyle = theme.bg;
      ctx.fillRect(uBlockRear, panTop - depth, uBlockFront - uBlockRear, depth);
      ctx.fillStyle = '#8a5a14';
      ctx.globalAlpha = 0.55;
      ctx.fillRect(uBlockRear, panTop - depth, uBlockFront - uBlockRear, depth * 0.38);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1.2 * px;
      ctx.strokeRect(uBlockRear, panTop - depth, uBlockFront - uBlockRear, depth);
      // L'attelage repasse devant le carter.
      ctx.restore();
      ctx.save();
      ctx.beginPath();
      ctx.rect(x, y, w, h);
      ctx.clip();
      ctx.translate(ox, oy);
      ctx.scale(scale, -scale);
      this.redrawCrankOverPan(ctx, crankAngle, theme, px);
    }
    this.drawTurboSide(ctx, pose, painter, theme, px);
    this.drawChargerSide(ctx, pose, painter, theme, px);
    ctx.restore();

    // La longueur du tube se lit sans demander les noms : c'est elle que le dessin peut replier.
    if (labels.turboPipeLength && this.chain && this.chain.extraMm > 0) {
      const unit = this.chain.units[0];
      ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
      ctx.fillStyle = theme.text;
      ctx.textAlign = 'center';
      const feedY = this.chain.cooler[1] + unit.turbineHousing + bore * 0.3 + bore * 0.26;
      ctx.fillText(labels.turboPipeLength, ox + ((layout.rearZ + bore * 0.3 + unit.sideZ) / 2) * scale, oy - feedY * scale);
      ctx.textAlign = 'left';
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    }

    if (labels.parts) {
      ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
      ctx.fillStyle = theme.dim;
      ctx.textAlign = 'center';
      const labelAt = (text: string, u: number, v: number) => ctx.fillText(text, ox + u * scale, oy - v * scale);
      this.labelTurboSide(ctx, labels, labelAt);
      const charger = this.charger;
      if (charger && labels.compressor) labelAt(labels.compressor, charger.z, this.chargerSideY(charger) + charger.halfY + bore * 0.08);
      const mid = (layout.frontZ + layout.rearZ) / 2;
      labelAt(labels.cam, mid, valve.camY + bore * 0.3);
      labelAt(labels.chain, layout.sprocket.z[0], deck + bore * 2);
      labelAt(labels.flywheel, ring.z, ring.radius + bore * 0.1);
      labelAt(labels.block, mid, deck - layout.stroke * 0.4);
      ctx.textAlign = 'left';
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    }
  }

  private redrawCrankOverPan(ctx: CanvasRenderingContext2D, crankAngle: number, theme: Theme, px: number): void {
    const layout = this.layout;
    const points = this.webPoints;
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    for (const main of layout.mains) {
      ctx.fillRect(main.z - main.width / 2 - 1, -layout.mainRadius, main.width + 2, layout.mainRadius * 2);
      ctx.strokeRect(main.z - main.width / 2 - 1, -layout.mainRadius, main.width + 2, layout.mainRadius * 2);
    }
    for (const item of layout.throws) {
      const front = item.pins[0];
      const rear = item.pins[item.pins.length - 1];
      for (let k = 0; k < 2; k++) {
        const a = ((k === 0 ? front.deg : rear.deg) + crankAngle) * DEG;
        const u = k === 0 ? item.zFront - layout.webWidth : item.zRear;
        const cos = Math.cos(a);
        const sin = Math.sin(a);
        let lo = Infinity;
        let hi = -Infinity;
        for (let p = 0; p < points.length; p++) {
          const v = points[p][1] * cos - points[p][0] * sin;
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
        ctx.fillStyle = theme.raised;
        ctx.fillRect(u, lo, layout.webWidth, hi - lo);
        ctx.strokeRect(u, lo, layout.webWidth, hi - lo);
      }
      for (const pin of item.pins) {
        const v = layout.crank * Math.cos((pin.deg + crankAngle) * DEG);
        ctx.fillStyle = theme.metal;
        ctx.fillRect(pin.z - pin.width / 2, v - layout.pinRadius, pin.width, layout.pinRadius * 2);
        ctx.strokeRect(pin.z - pin.width / 2, v - layout.pinRadius, pin.width, layout.pinRadius * 2);
      }
    }
  }

  private drawSideCylinder(ctx: CanvasRenderingContext2D, cylinder: Cylinder, pose: EnginePose, painter: GasPainter, theme: Theme, hatch: CanvasPattern | string, px: number, linerBottom: number): void {
    const layout = this.layout;
    const { bore, deck, valve } = layout;
    const i = cylinder.index;
    const u = cylinder.z;
    const R = bore / 2;
    const s = pose.piston[i];
    const crown = s + layout.pistonTop;
    // Les parois entre les cylindres.
    const wall = bore * 0.08;
    ctx.fillStyle = hatch;
    ctx.fillRect(u - R - wall, linerBottom, wall, deck - linerBottom);
    ctx.fillRect(u + R, linerBottom, wall, deck - linerBottom);
    ctx.fillStyle = theme.bg;
    ctx.fillRect(u - R, linerBottom, bore, crown - linerBottom);
    const burning = pose.flame[i] > 0;
    if (pose.gasLive) {
      const kelvin = pose.gasT[i];
      const shown = kelvin < 800 ? kelvin : 800 + (kelvin - 800) * 0.33;
      ctx.fillStyle = painter.gasStyle(theme, shown, burning ? 0 : pose.gasBurnt[i]);
    } else {
      ctx.fillStyle = theme.bg;
    }
    const top = deck + valve.roofRise * 0.55;
    ctx.fillRect(u - R, crown, bore, top - crown);
    if (burning) {
      const radius = Math.min(bore * 0.08 + bore * 0.72 * Math.sqrt(pose.flame[i]), R);
      ctx.fillStyle = painter.gasStyle(theme, 1050 + 300 * (1 - pose.flame[i]), 1);
      ctx.beginPath();
      ctx.ellipse(u, top, radius, Math.min(radius, Math.max(top - crown, 1)), 0, Math.PI, Math.PI * 2);
      ctx.fill();
    }
    // La bielle, de l'axe au maneton projeté : elle raccourcit à l'œil quand elle s'incline.
    const pinV = layout.crank * Math.cos(pose.alpha[i] * DEG);
    ctx.fillStyle = theme.metal;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = px;
    ctx.fillRect(u - layout.rodWidth / 2, pinV, layout.rodWidth, s - pinV);
    ctx.strokeRect(u - layout.rodWidth / 2, pinV, layout.rodWidth, s - pinV);
    // Le piston.
    ctx.fillStyle = theme.raised;
    ctx.fillRect(u - R + 0.3, s - layout.pistonBottom, bore - 0.6, layout.pistonBottom + layout.pistonTop);
    ctx.strokeRect(u - R + 0.3, s - layout.pistonBottom, bore - 0.6, layout.pistonBottom + layout.pistonTop);
    ctx.fillStyle = theme.lineStrong;
    for (let k = 0; k < 3; k++) ctx.fillRect(u - R + 0.3, crown - bore * (0.07 + 0.06 * k), bore - 0.6, bore * 0.018);
    ctx.fillStyle = theme.text;
    ctx.beginPath();
    ctx.arc(u, s, bore * 0.1, 0, Math.PI * 2);
    ctx.fill();
    // Double arbre : une paire de soupapes d'admission, vues dans le plan. La coupe les montre devant le conduit.
    // Simple arbre : l'admission et l'échappement, côte à côte, chacune sous son bossage et son culbuteur.
    for (let v = 0; v < valve.perCylinder; v++) {
      const slot = valve.slots[v];
      if (!valve.sohc && slot.exhaust) continue;
      const exhaust = slot.exhaust;
      const side = exhaust ? -cylinder.intakeSide : cylinder.intakeSide;
      const dz = valve.sohc ? side * valve.zOffset : slot.zSign * valve.zOffset;
      const radius = exhaust ? valve.radiusEx : valve.radiusIn;
      const open = exhaust ? EVO : IVO;
      const close = exhaust ? EVC : IVC;
      const share = exhaust ? pose.liftEx[i] : pose.liftIn[i];
      const lift = share * valve.lift;
      const stemTop = deck + valve.seatRise + Math.cos(valve.tiltDeg * DEG) * valve.stem;
      const seat = deck + valve.seatRise - lift;
      ctx.fillStyle = theme.metal;
      // Sur un simple arbre, les têtes se chevauchent vues de côté : chacune est décalée, on n'en montre que la moitié de la coupe.
      const shownRadius = valve.sohc ? Math.min(radius, bore * 0.17) : radius;
      ctx.fillRect(u + dz - shownRadius, seat, shownRadius * 2, bore * 0.03);
      ctx.fillRect(u + dz - valve.stemRadius, seat, valve.stemRadius * 2, stemTop - lift - seat);
      ctx.fillStyle = theme.raised;
      const cap = valve.sohc ? bore * 0.05 : bore * 0.105;
      ctx.fillRect(u + dz - cap, stemTop - lift, cap * 2, bore * 0.05);
      ctx.strokeRect(u + dz - cap, stemTop - lift, cap * 2, bore * 0.05);
      // La came : la hauteur vue de côté suit le bossage, vers la soupape et à l'opposé.
      const fromNose = ((open + close) / 2 - pose.theta[i]) / 2;
      const down = valve.camBase + share * valve.camLift;
      const up = lobeRadius(valve.camBase, valve.camLift, fromNose + 180, open, close);
      ctx.fillStyle = theme.text;
      const lobeHalf = valve.sohc ? bore * 0.065 : bore * 0.075;
      ctx.fillRect(u + dz - lobeHalf, valve.camY - down, lobeHalf * 2, down + up);
    }
    // L'allumage, au milieu ; à côté de l'arbre sur un simple arbre.
    const plugU = u + valve.plugZ;
    const tip = deck + valve.roofRise - bore * 0.03;
    if (layout.fuel === 'diesel') {
      ctx.fillStyle = theme.dim;
      ctx.fillRect(plugU - bore * 0.05, tip, bore * 0.1, bore * 1.3);
      const g = pose.glow;
      if (g > 0.05) {
        ctx.fillStyle = GLOW_STYLES[Math.round(g * 16)];
        ctx.fillRect(plugU + bore * 0.1, tip - bore * 0.14, bore * 0.04, bore * 0.2);
      }
    } else {
      ctx.fillStyle = theme.dim;
      ctx.fillRect(plugU - bore * 0.08, tip + bore * 0.5, bore * 0.16, bore * 1);
      ctx.fillStyle = '#e9e6dd';
      ctx.fillRect(plugU - bore * 0.04, tip + bore * 0.2, bore * 0.08, bore * 0.3);
      if (pose.spark[i] > 0) {
        ctx.fillStyle = '#ffffff';
        ctx.globalAlpha = 0.4 + 0.6 * pose.spark[i];
        ctx.beginPath();
        ctx.arc(plugU, tip - bore * 0.03, bore * (0.04 + 0.06 * pose.spark[i]), 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
    }
  }
}
