/**
 * La boîte en coupe, sur un canvas 2D : les arbres de profil, les dents qui défilent, le baladeur et sa fourchette,
 * les roulements, le carter ; à côté, de face, la paire en prise, le pont et le volant, avec leurs vraies dents.
 * Ne calcule rien : tout vient de `GearboxLayout` et de `GearboxMotion`.
 */
import { mix, type Theme } from './canvas';
import { RING_CONTACT, trackAt, type BoxFrame, type GearboxLayout, type GearboxMotion, type Wheel } from './gearboxModel';
import { toothOutline } from './gearboxGeometry';
import type { MessageKey } from './i18n.svelte';

const TAU = Math.PI * 2;
const FONT = '11px "Inter Tight", system-ui, sans-serif';
const FONT_BOLD = '600 11px "Inter Tight", system-ui, sans-serif';
const FONT_SMALL = '10px "Inter Tight", system-ui, sans-serif';

type Translate = (key: MessageKey, params?: Record<string, string | number>) => string;

interface Palette {
  key: Theme;
  accent: string;
  casing: string;
  casingFill: string;
  oil: string;
  shaft: string;
  shaftOuter: string;
  wheel: string;
  wheelFree: string;
  tooth: string;
  active: string;
  activeTooth: string;
  pre: string;
  preTooth: string;
  sleeve: string;
  brass: string;
  line: string;
  shade: string;
  shadeStrong: string;
}

interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export class GearboxPainter {
  private palette: Palette | null = null;
  private paths = new Map<number, Path2D>();
  private readonly railLow: number;
  private readonly railHigh: number;
  private readonly hubShaftRadius: number;
  /** Le plus petit écart entre deux roues menées (mm) : leurs vitesses s'écrivent dessous si elles tiennent. */
  private readonly closestDriven: number;
  /** Dernier cadre de la coupe, pour placer les repères. */
  section: Rect = { x: 0, y: 0, w: 1, h: 1 };
  /** La roue dont une dent a cédé. */
  missingWheel: number | undefined;
  private sx = 1;
  private sy = 1;
  private x0 = 0;
  private cy = 0;

  constructor(private readonly layout: GearboxLayout, private readonly tr: Translate) {
    const xs = layout.selectors.map((s) => s.x);
    this.railLow = xs.length > 0 ? Math.min(...xs) - 8 : 0;
    this.railHigh = xs.length > 0 ? Math.max(...xs) + 8 : 0;
    this.hubShaftRadius = layout.shafts.find((s) => s.body === layout.hubBody)?.radius ?? 8;
    const drivenX = layout.wheels.filter((w) => w.role === 'driven').map((w) => w.x).sort((a, b) => a - b);
    let closest = Infinity;
    for (let i = 1; i < drivenX.length; i++) closest = Math.min(closest, drivenX[i] - drivenX[i - 1]);
    this.closestDriven = closest;
  }

  private colors(theme: Theme): Palette {
    // La chaleur change dans le même objet : la paire en prise la suit.
    if (this.palette && this.palette.key === theme && this.palette.accent === theme.accent) return this.palette;
    this.palette = {
      key: theme,
      accent: theme.accent,
      casing: theme.lineStrong,
      casingFill: mix(theme.surface, theme.metal, 0.08),
      oil: mix(theme.surface, '#c98a2b', 0.28),
      shaft: mix(theme.metal, theme.text, 0.15),
      shaftOuter: mix(theme.metal, theme.cool, 0.45),
      wheel: mix(theme.raised, theme.metal, 0.35),
      wheelFree: mix(theme.raised, theme.metal, 0.18),
      tooth: mix(theme.raised, theme.text, 0.42),
      active: mix(theme.raised, theme.accent, 0.62),
      activeTooth: theme.text,
      pre: mix(theme.raised, theme.cool, 0.45),
      preTooth: mix(theme.cool, theme.text, 0.4),
      sleeve: mix(theme.raised, theme.metal, 0.75),
      brass: '#b98f46',
      line: theme.line,
      shade: 'rgba(0, 0, 0, 0.16)',
      shadeStrong: 'rgba(0, 0, 0, 0.32)',
    };
    return this.palette;
  }

  private path(wheel: Wheel): Path2D {
    let p = this.paths.get(wheel.id);
    if (p) return p;
    p = new Path2D();
    const outline = toothOutline(wheel.teeth, wheel.moduleT, wheel.moduleN, wheel.helix, { internal: wheel.internal, missing: wheel.id === this.missingWheel ? 0 : undefined });
    outline.forEach((point, i) => {
      const x = point.r * Math.cos(point.a);
      const y = point.r * Math.sin(point.a);
      if (i === 0) p!.moveTo(x, y);
      else p!.lineTo(x, y);
    });
    p.closePath();
    this.paths.set(wheel.id, p);
    return p;
  }

  private X(x: number): number {
    return this.section.x + (x - this.x0) * this.sx;
  }

  private Y(y: number): number {
    return this.cy - y * this.sy;
  }

  /** Ce que la coupe doit contenir, en millimètres. */
  private extents(): { x0: number; x1: number; y0: number; y1: number } {
    const { layout } = this;
    let x0 = layout.bounds.x0;
    let x1 = layout.bounds.x1;
    let y0 = layout.bounds.y0;
    let y1 = layout.bounds.y1;
    const g = layout.flywheelGeom;
    if (g.radius > 0) {
      x0 = Math.min(x0, g.x - g.width / 2 - 8);
      y1 = Math.max(y1, g.y + g.radius * 0.92);
      y0 = Math.min(y0, g.y - g.radius * 0.92);
    }
    if (layout.form === 'converter') x0 = Math.min(x0, -94);
    // Sous les roues, la place du rail ou du barillet : la coupe ne les met pas devant les dents.
    if (layout.selectors.length > 0) {
      y0 = Math.min(this.lowestWheel() - (layout.drum ? layout.drum.radius * 3.6 + 8 : 14), y0);
    }
    for (const s of layout.shafts) x1 = Math.max(x1, s.x1 + 4);
    const final = layout.final;
    if (final) {
      const ring = layout.wheels[final.ring];
      if (final.kind === 'bevel') {
        // La largeur, c'est `fitSection` qui la réserve : ici, seulement l'arbre qui mène au pignon.
        y1 = Math.max(y1, final.y + ring.radius + 6);
        y0 = Math.min(y0, final.y - ring.radius - 6);
      }
    }
    return { x0, x1, y0, y1 };
  }

  /** Assez de place sous chaque roue pour écrire sa vitesse : c'est ce qui montre que tout tourne, chacune à la sienne. */
  private showSpeeds(): boolean {
    return this.layout.selectors.length > 0 && this.closestDriven * this.sx > 34;
  }

  private lowestWheel(): number {
    let low = Infinity;
    for (const w of this.layout.wheels) {
      if (w.role === 'finalRing' || w.role === 'finalPinion') continue;
      low = Math.min(low, w.y - w.radius - w.moduleN);
    }
    return low;
  }

  private casingFloor(): number {
    const layout = this.layout;
    if (layout.selectors.length === 0) return layout.bounds.y0;
    return this.lowestWheel() - (layout.drum ? layout.drum.radius * 3.4 + 6 : 12);
  }

  /** Le rail ou le barillet, dans la coupe. */
  private railY(): number {
    const layout = this.layout;
    return this.lowestWheel() - (layout.drum ? layout.drum.radius * 1.8 + 4 : 7);
  }

  draw(ctx: CanvasRenderingContext2D, theme: Theme, kin: GearboxMotion, frame: BoxFrame, width: number, height: number, flags: { wear: number; broken: boolean }): void {
    const { layout } = this;
    if (layout.wheels.length === 0) return;
    const colors = this.colors(theme);
    const compact = width < 620;
    const pad = 10;
    const panelW = compact ? width - pad * 2 : Math.min(width * 0.33, 400);
    const panelH = compact ? Math.min(height * 0.36, 170) : height - pad * 2;
    this.section = compact ? { x: pad, y: pad, w: width - pad * 2, h: height - panelH - pad * 3 } : { x: pad, y: pad, w: width - panelW - pad * 3, h: height - pad * 2 };
    const panel: Rect = compact ? { x: pad, y: height - panelH - pad, w: panelW, h: panelH } : { x: width - panelW - pad, y: pad, w: panelW, h: panelH };

    this.fitSection();
    this.drawSection(ctx, theme, colors, kin, frame, flags);
    this.drawPanel(ctx, theme, colors, kin, frame, panel, compact);
  }

  private fitSection(): void {
    const e = this.extents();
    const availableH = Math.max(this.section.h - 34, 20);
    const sy = availableH / Math.max(e.y1 - e.y0, 1);
    // Les pièces vues de profil supportent un étirement le long de l'axe : ça laisse lire huit rapports.
    const final = this.layout.final;
    // La couronne conique se voit de face : elle garde sa rondeur, et prend sa place au bout de l'arbre.
    const reserved = final?.kind === 'bevel' ? 2 * (this.layout.wheels[final.ring].radius + this.layout.wheels[final.ring].moduleN) * sy + 6 : 0;
    const sx = Math.max(Math.min((this.section.w - reserved) / Math.max(e.x1 - e.x0, 1), sy * 1.6), 0.1);
    this.sx = sx;
    this.sy = sy;
    this.x0 = e.x0 - Math.max(((this.section.w - reserved) / sx - (e.x1 - e.x0)) / 2, 0);
    this.cy = this.section.y + (e.y1 * sy);
  }

  private drawSection(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame, flags: { wear: number; broken: boolean }): void {
    const { layout } = this;
    const b = { x0: layout.bounds.x0, x1: layout.bounds.x1, y0: Math.min(layout.bounds.y0, this.casingFloor()), y1: layout.bounds.y1 };
    const X = (x: number) => this.X(x);
    const Y = (y: number) => this.Y(y);
    ctx.lineCap = 'butt';

    // Le carter, en coupe : ses parois et l'huile au fond.
    ctx.fillStyle = c.casingFill;
    ctx.strokeStyle = c.casing;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.roundRect(X(b.x0), Y(b.y1), X(b.x1) - X(b.x0), Y(b.y0) - Y(b.y1), 8);
    ctx.fill();
    ctx.stroke();
    const fw = layout.flywheelGeom;
    if (fw.radius > 0 && layout.form !== 'reducer') {
      // La cloche, entre le carter et le volant.
      const bellLeft = X(fw.x - fw.width / 2 - 2);
      const bellTop = Y(Math.min(fw.y + fw.radius * 1.04, fw.y + fw.radius + 3));
      const bellBottom = Y(fw.y - fw.radius * 1.04);
      ctx.fillStyle = c.casingFill;
      ctx.beginPath();
      ctx.moveTo(X(b.x0), Y(b.y1) + 6);
      ctx.lineTo(bellLeft, bellTop);
      ctx.lineTo(bellLeft, bellBottom);
      ctx.lineTo(X(b.x0), Math.min(bellBottom + 4, Y(b.y0)));
      ctx.closePath();
      ctx.fill();
      ctx.lineWidth = 1;
      ctx.stroke();
    }
    ctx.fillStyle = c.oil;
    ctx.beginPath();
    ctx.roundRect(X(b.x0) + 2, Y(layout.oilY), X(b.x1) - X(b.x0) - 4, Y(b.y0) - Y(layout.oilY) - 2, [0, 0, 6, 6]);
    ctx.fill();

    this.drawInput(ctx, theme, c, kin, frame);
    this.drawShafts(ctx, theme, c);
    if (layout.planets.length > 0) this.drawPlanetSection(ctx, theme, c, kin);
    this.drawBearings(ctx, theme, c, kin);
    this.drawWheels(ctx, theme, c, kin, frame, flags);
    this.drawSelectors(ctx, theme, c, kin, frame);
    if (layout.drum) this.drawDrum(ctx, theme, c, kin);
    if (layout.final) this.drawFinalSection(ctx, theme, c, kin);
    if (layout.transfer) {
      // Intégrale : un arbre revient vers l'essieu avant, sous la boîte.
      const transfer = layout.transfer;
      const y = Math.min(this.Y(this.casingFloor()) + 7, this.section.y + this.section.h - 28);
      const left = Math.max(X(transfer.x0), this.section.x);
      const right = X(transfer.x1);
      const h = Math.max(transfer.radius * this.sy, 2.5);
      ctx.fillStyle = c.shaft;
      ctx.fillRect(left, y - h / 2, right - left, h);
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.strokeRect(left, y - h / 2, right - left, h);
      ctx.fillStyle = c.casingFill;
      ctx.fillRect(right - 3, this.Y(layout.bodies[layout.output].y), 10, y + h - this.Y(layout.bodies[layout.output].y));
      ctx.strokeRect(right - 3, this.Y(layout.bodies[layout.output].y), 10, y + h - this.Y(layout.bodies[layout.output].y));
    }

    // Les numéros des rapports, sous leur roue.
    ctx.font = FONT;
    ctx.textAlign = 'center';
    const baseline = this.section.y + this.section.h - 2;
    const speeds = this.showSpeeds();
    for (const wheel of layout.wheels) {
      if ((wheel.role !== 'driven' && wheel.role !== 'reverseWheel') || layout.selectors.length === 0) continue;
      const on = kin.engaged === wheel.gear;
      ctx.fillStyle = on ? theme.text : wheel.gear === kin.preselected ? c.preTooth : theme.dim;
      ctx.font = on ? FONT_BOLD : FONT;
      ctx.fillText(wheel.gear < 0 ? 'R' : String(wheel.gear), X(wheel.x), baseline - (speeds ? 12 : 0));
      if (speeds && wheel.role === 'driven') {
        ctx.font = FONT_SMALL;
        ctx.fillStyle = on ? theme.text : theme.dim;
        ctx.fillText(String(Math.round(Math.abs(kin.rpm[wheel.body]))), X(wheel.x), baseline);
      }
    }
    for (const selector of layout.selectors) {
      if (selector.left?.wheel === -1) {
        ctx.fillStyle = kin.engaged === selector.left.gear ? theme.text : theme.dim;
        ctx.fillText(String(selector.left.gear), X(selector.left.x), baseline - (speeds ? 12 : 0));
      }
    }
    ctx.font = FONT;

    if (flags.broken) {
      ctx.fillStyle = theme.danger;
      ctx.textAlign = 'left';
      ctx.font = FONT_BOLD;
      ctx.fillText(this.tr('box.view.broken'), X(b.x0) + 8, Y(b.y1) + 14);
      ctx.font = FONT;
    }
  }

  private drawShafts(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette): void {
    const { layout } = this;
    for (const s of layout.shafts) {
      const left = this.X(s.x0);
      const right = this.X(s.x1);
      const top = this.Y(s.y + s.radius);
      const height = this.Y(s.y - s.radius) - top;
      ctx.fillStyle = s.role === 'inputOuter' ? c.shaftOuter : s.role === 'lay' ? mix(theme.metal, theme.raised, 0.25) : c.shaft;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.rect(left, top, right - left, height);
      ctx.fill();
      ctx.stroke();
      if (s.bore > 0) {
        // L'arbre creux : on voit le plein passer dedans.
        const boreTop = this.Y(s.y + s.bore);
        const boreH = this.Y(s.y - s.bore) - boreTop;
        ctx.fillStyle = theme.surface;
        ctx.fillRect(left + 1, boreTop, right - left - 2, boreH);
      }
    }
    // Un arbre plein passant dans un arbre creux se dessine après ce dernier.
    for (const s of layout.shafts) {
      if (s.role !== 'input' || (layout.inputOuter < 0 && layout.planets.length === 0)) continue;
      const left = this.X(s.x0);
      const right = this.X(s.x1);
      const top = this.Y(s.y + s.radius);
      ctx.fillStyle = c.shaft;
      ctx.fillRect(left, top, right - left, this.Y(s.y - s.radius) - top);
    }
    // Traits d'axe.
    ctx.strokeStyle = theme.line;
    ctx.setLineDash([10, 3, 2, 3]);
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (const s of layout.shafts) {
      ctx.moveTo(this.X(s.x0) - 4, this.Y(s.y));
      ctx.lineTo(this.X(s.x1) + 4, this.Y(s.y));
    }
    ctx.stroke();
    ctx.setLineDash([]);
  }

  private drawBearings(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion): void {
    for (const bearing of this.layout.bearings) {
      const left = this.X(bearing.x - bearing.width / 2);
      const w = this.X(bearing.x + bearing.width / 2) - left;
      const ballY = (bearing.inner + bearing.outer) / 2;
      const roller = (sign: number) => {
        const top = this.Y(bearing.y + sign * bearing.outer);
        const bottom = this.Y(bearing.y + sign * bearing.inner);
        const y = Math.min(top, bottom);
        const h = Math.abs(bottom - top);
        ctx.strokeStyle = theme.lineStrong;
        ctx.lineWidth = 1;
        ctx.strokeRect(left, y, w, h);
        ctx.fillStyle = c.casingFill;
        ctx.fillRect(left, y, w, h);
        ctx.strokeRect(left, y, w, h);
        // La bille, en coupe, et sa rotation : un point qui passe.
        const cx = left + w / 2;
        const cy = this.Y(bearing.y + sign * ballY);
        const radius = Math.max(Math.min(h * 0.32, w * 0.34), 1.5);
        ctx.fillStyle = theme.metal;
        ctx.beginPath();
        ctx.arc(cx, cy, radius, 0, TAU);
        ctx.fill();
        const a = kin.angles[bearing.body] * 0.4;
        ctx.fillStyle = theme.bg;
        ctx.beginPath();
        ctx.arc(cx + Math.cos(a) * radius * 0.5, cy + Math.sin(a) * radius * 0.5, Math.max(radius * 0.25, 0.8), 0, TAU);
        ctx.fill();
      };
      roller(1);
      roller(-1);
    }
  }

  /** Une roue de profil : son contour et ses vraies dents qui défilent, inclinées selon l'hélice. */
  private drawWheels(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame, flags: { wear: number; broken: boolean }): void {
    const { layout } = this;
    const wheels = layout.wheels;
    for (let i = 0; i < wheels.length; i++) {
      const wheel = wheels[i];
      if (wheel.bevel || wheel.role === 'finalRing' || wheel.internal || wheel.role === 'planet' || wheel.role === 'sun') continue;
      let shift = 0;
      const idler = wheel.role === 'reverseIdler';
      if (idler) shift = (1 - kin.reverseIn) * (layout.reverse?.slide ?? 0);
      const on = wheel.gear !== 0 && wheel.gear === kin.engaged;
      const pre = wheel.gear !== 0 && wheel.gear === kin.preselected && !on;
      const left = this.X(wheel.x - wheel.width / 2 + shift);
      const right = this.X(wheel.x + wheel.width / 2 + shift);
      const tipR = wheel.radius + wheel.moduleN;
      const rootR = wheel.radius - 1.25 * wheel.moduleN;
      const top = this.Y(wheel.y + tipR);
      const bottom = this.Y(wheel.y - tipR);
      const danger = on && flags.wear > 0.5;
      const fill = on ? (danger ? mix(c.active, theme.danger, flags.wear) : c.active) : pre ? c.pre : wheel.free ? c.wheelFree : c.wheel;
      ctx.fillStyle = fill;
      ctx.strokeStyle = on ? theme.accent : pre ? theme.cool : theme.lineStrong;
      ctx.lineWidth = on ? 1.5 : 1;
      ctx.beginPath();
      ctx.rect(left, top, right - left, bottom - top);
      ctx.fill();
      ctx.stroke();

      // La racine : sous elle, le voile de la roue.
      ctx.strokeStyle = on ? theme.accent : theme.line;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(left, this.Y(wheel.y + rootR));
      ctx.lineTo(right, this.Y(wheel.y + rootR));
      ctx.moveTo(left, this.Y(wheel.y - rootR));
      ctx.lineTo(right, this.Y(wheel.y - rootR));
      ctx.stroke();

      // Une roue folle tourne sur son roulement à aiguilles : un liseré entre elle et l'arbre.
      if (wheel.free) {
        const shaftR = this.hubShaftRadius;
        ctx.fillStyle = theme.bg;
        const a = this.Y(wheel.y + shaftR + 1.6);
        const d = this.Y(wheel.y + shaftR);
        const e = this.Y(wheel.y - shaftR);
        const f = this.Y(wheel.y - shaftR - 1.6);
        ctx.fillRect(left + 1, a, right - left - 2, Math.max(d - a, 1));
        ctx.fillRect(left + 1, e, right - left - 2, Math.max(f - e, 1));
      }

      // Les dents visibles, face à nous : l'axe tourne vers nous ou nous quitte. Leur position est la vraie, à la phase près.
      const angle = kin.angles[wheel.body] + wheel.phase;
      const twist = (wheel.width * Math.tan(wheel.helix)) / wheel.radius;
      const pitch = TAU / wheel.teeth;
      const R = (wheel.radius + tipR) / 2;
      ctx.strokeStyle = on ? c.activeTooth : pre ? c.preTooth : c.tooth;
      ctx.lineWidth = 0.8;
      ctx.beginPath();
      for (let k = 0; k < wheel.teeth; k++) {
        const a = angle + k * pitch;
        if (Math.sin(a) < 0) continue;
        ctx.moveTo(left, this.Y(wheel.y + R * Math.cos(a - twist / 2)));
        ctx.lineTo(right, this.Y(wheel.y + R * Math.cos(a + twist / 2)));
      }
      ctx.stroke();
      // Le volume : la roue s'assombrit vers ses bords, comme un cylindre.
      const band = (bottom - top) * 0.16;
      ctx.fillStyle = c.shadeStrong;
      ctx.fillRect(left, top, right - left, band * 0.5);
      ctx.fillRect(left, bottom - band * 0.5, right - left, band * 0.5);
      ctx.fillStyle = c.shade;
      ctx.fillRect(left, top + band * 0.5, right - left, band);
      ctx.fillRect(left, bottom - band * 1.5, right - left, band);

      // Une roue qui claque : un tiret au sommet, qui suit sa position réelle dans le jeu.
      if (frame.rattle > 0.5 && !on) {
        for (let k = 0; k < 4; k++) {
          if (layout.rattle[k] !== wheel.body) continue;
          const share = k === 0 ? frame.loose0 : k === 1 ? frame.loose1 : k === 2 ? frame.loose2 : frame.loose3;
          ctx.strokeStyle = theme.danger;
          ctx.lineWidth = 2;
          ctx.beginPath();
          const cx = (left + right) / 2 + share * (right - left) * 0.34;
          ctx.moveTo(cx - 3, top - 3);
          ctx.lineTo(cx + 3, top - 3);
          ctx.stroke();
        }
      }

      // La marche arrière : son pignon coulisse pour entrer en prise.
      if (idler) {
        ctx.strokeStyle = theme.dim;
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.moveTo(this.X(wheel.x), this.Y(wheel.y));
        ctx.lineTo(this.X(wheel.x + (layout.reverse?.slide ?? 0)), this.Y(wheel.y));
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }
  }

  private drawSelectors(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame): void {
    const { layout } = this;
    const cone = layout.form === 'synchro' || layout.form === 'dual_clutch';
    const gridding = frame.grinding > 5;
    // Le rail, sous les baladeurs.
    if (layout.selectors[0] && !layout.drum) {
      const railY = this.Y(this.railY());
      const low = this.railLow;
      const high = this.railHigh;
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(this.X(low), railY);
      ctx.lineTo(this.X(high), railY);
      ctx.stroke();
    }
    for (let i = 0; i < layout.selectors.length; i++) {
      const s = layout.selectors[i];
      const pos = kin.pos[i];
      const offset = (pos + (i === kin.syncSelector ? kin.shake : 0)) * s.travel;
      const x = s.x + offset;
      const top = this.Y(s.y + s.sleeveRadius);
      const bottom = this.Y(s.y - s.sleeveRadius);
      const left = this.X(x - s.hubWidth / 2);
      const right = this.X(x + s.hubWidth / 2);
      const hubLeft = this.X(s.x - s.hubWidth * 0.41);
      const hubRight = this.X(s.x + s.hubWidth * 0.41);

      // Le moyeu, fixe sur l'arbre.
      ctx.fillStyle = c.shaft;
      ctx.fillRect(hubLeft, this.Y(s.y + s.hubRadius), hubRight - hubLeft, this.Y(s.y - s.hubRadius) - this.Y(s.y + s.hubRadius));

      // La couronne d'engagement ou les crabots, sur chaque roue voisine.
      for (let sideIndex = 0; sideIndex < 2; sideIndex++) {
        const side = sideIndex === 0 ? s.left : s.right;
        if (!side) continue;
        const sign = sideIndex === 0 ? -1 : 1;
        const near = s.x + sign * (s.hubWidth / 2);
        const far = near + sign * (s.dogWidth * 1.2);
        const dogTop = this.Y(s.y + s.hubRadius * 0.96);
        const dogBottom = this.Y(s.y - s.hubRadius * 0.96);
        ctx.fillStyle = cone ? c.tooth : theme.metal;
        if (cone) {
          // Le cône et, devant lui, la bague de synchro : elle touche dès que le baladeur la pousse.
          const a = this.X(Math.min(near, far));
          const w = Math.abs(this.X(far) - this.X(near));
          ctx.fillRect(a, dogTop, w, dogBottom - dogTop);
          const contact = Math.abs(pos) >= RING_CONTACT * 0.9 && Math.sign(pos) === sign;
          const ringX = near + sign * (contact ? s.dogWidth * 0.2 : -s.dogWidth * 0.1);
          ctx.fillStyle = contact ? (frame.coneSlip > 0.5 ? theme.accent : c.brass) : c.brass;
          const rw = Math.max(this.X(s.dogWidth * 0.7) - this.X(0), 2);
          ctx.fillRect(this.X(ringX) - rw / 2, this.Y(s.y + s.hubRadius * 0.98), rw, this.Y(s.y + s.hubRadius * 0.7) - this.Y(s.y + s.hubRadius * 0.98));
          ctx.fillRect(this.X(ringX) - rw / 2, this.Y(s.y - s.hubRadius * 0.7), rw, this.Y(s.y - s.hubRadius * 0.98) - this.Y(s.y - s.hubRadius * 0.7));
        } else {
          // Six crabots : quelques créneaux bien visibles.
          const span = dogBottom - dogTop;
          const a = this.X(Math.min(near, far));
          const w = Math.abs(this.X(far) - this.X(near));
          const count = Math.max(layout.dogTeeth, 3);
          const wheelAngle = kin.angles[side.body];
          for (let k = 0; k < count; k++) {
            const d = wheelAngle + (k / count) * TAU;
            if (Math.sin(d) < 0) continue;
            const yy = dogTop + span * (0.5 - 0.5 * Math.cos(d));
            ctx.fillRect(a, yy - span * 0.05, w, span * 0.1);
          }
        }
      }

      // Le baladeur : sa gorge, ses dents.
      const hit = i === kin.syncSelector && gridding;
      ctx.fillStyle = hit ? theme.danger : Math.abs(pos) > 0.85 ? mix(c.sleeve, theme.accent, 0.5) : c.sleeve;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.rect(left, top, right - left, bottom - top);
      ctx.fill();
      ctx.stroke();
      const grooveL = this.X(x - s.hubWidth * 0.18);
      const grooveR = this.X(x + s.hubWidth * 0.18);
      ctx.fillStyle = theme.bg;
      ctx.fillRect(grooveL, top, grooveR - grooveL, Math.max(this.Y(s.y + s.sleeveRadius * 0.93) - top, 1));
      ctx.fillRect(grooveL, this.Y(s.y - s.sleeveRadius * 0.93), grooveR - grooveL, Math.max(bottom - this.Y(s.y - s.sleeveRadius * 0.93), 1));
      // La fourchette, de la gorge au rail ou au barillet.
      const xMid = this.X(x);
      const railY = this.Y(this.railY());
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(xMid, bottom);
      ctx.lineTo(xMid, railY);
      ctx.stroke();
      if (!layout.drum) {
        ctx.fillStyle = theme.metal;
        ctx.fillRect(xMid - 6, railY - 4, 12, 8);
      }

      // Le choc d'engagement, et l'écart de vitesses que le cône referme.
      if (Math.abs(pos) > 0.85 && kin.clunk > 0.02) {
        ctx.strokeStyle = mix(theme.accent, theme.danger, 0.5);
        ctx.lineWidth = 2;
        ctx.globalAlpha = kin.clunk;
        ctx.beginPath();
        ctx.arc(xMid, this.Y(s.y), s.sleeveRadius * this.sy * (1.1 + 0.3 * (1 - kin.clunk)), 0, TAU);
        ctx.stroke();
        ctx.globalAlpha = 1;
      }
      if (i === kin.syncSelector && frame.step >= 3 && frame.step <= 5) {
        ctx.fillStyle = theme.text;
        ctx.textAlign = 'center';
        ctx.font = FONT;
        ctx.fillText(`Δ ${Math.abs(Math.round(kin.syncGapRpm))}`, xMid, top - 6);
      }
    }

    // La fourchette de l'intermédiaire de marche arrière.
    const reverse = layout.reverse;
    if (reverse) {
      const idler = layout.wheels[reverse.idler];
      const x = idler.x + (1 - kin.reverseIn) * reverse.slide;
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(this.X(x), this.Y(idler.y - idler.radius));
      ctx.lineTo(this.X(x), this.Y(this.railY()));
      ctx.stroke();
    }
  }

  /** Le barillet, déroulé : ses rainures passent sous les doigts des fourchettes. */
  private drawDrum(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion): void {
    const { layout } = this;
    const drum = layout.drum!;
    const left = this.X(drum.x0);
    const right = this.X(drum.x1);
    const yMid = this.Y(this.railY());
    const halfH = Math.max(drum.radius * this.sy * 1.5, 12);
    ctx.fillStyle = mix(theme.raised, theme.metal, 0.4);
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(left, yMid - halfH, right - left, halfH * 2, 3);
    ctx.fill();
    ctx.stroke();
    const positions = drum.positions.length - 1;
    ctx.save();
    ctx.beginPath();
    ctx.rect(left, yMid - halfH, right - left, halfH * 2);
    ctx.clip();
    ctx.strokeStyle = c.tooth;
    ctx.lineWidth = 1.6;
    const rowHeight = (halfH * 2) / 3;
    for (const s of layout.selectors) {
      ctx.beginPath();
      for (let k = 0; k <= positions * 5; k++) {
        const position = k / 5;
        const x = this.X(s.x + trackAt(s, drum, position) * s.travel);
        const y = yMid + (position - kin.drum) * rowHeight;
        if (k === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    ctx.restore();
    // La ligne des doigts : c'est ici que le barillet lit sa position.
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.beginPath();
    ctx.moveTo(left, yMid);
    ctx.lineTo(right, yMid);
    ctx.stroke();
    ctx.setLineDash([]);
    for (let i = 0; i < layout.selectors.length; i++) {
      const s = layout.selectors[i];
      ctx.fillStyle = theme.accent;
      ctx.beginPath();
      ctx.arc(this.X(s.x + kin.pos[i] * s.travel), yMid, 3, 0, TAU);
      ctx.fill();
    }
    // Le cliquet et son rochet, en bout : une dent par position.
    const rx = right + 12;
    const r = Math.max(halfH * 0.7, 8);
    const count = drum.positions.length + 2;
    ctx.fillStyle = mix(theme.raised, theme.metal, 0.5);
    ctx.strokeStyle = theme.lineStrong;
    ctx.beginPath();
    for (let k = 0; k < count; k++) {
      const a = -kin.drum * drum.step + (k / count) * TAU;
      const a2 = a + (TAU / count) * 0.35;
      const a3 = a + TAU / count;
      ctx.lineTo(rx + r * Math.cos(a), yMid + r * Math.sin(a));
      ctx.lineTo(rx + r * 1.28 * Math.cos(a2), yMid + r * 1.28 * Math.sin(a2));
      ctx.lineTo(rx + r * Math.cos(a3), yMid + r * Math.sin(a3));
    }
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    ctx.strokeStyle = c.brass;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(rx + r * 2.2, yMid - r * 1.4);
    ctx.lineTo(rx + r * 1.05, yMid - r * (0.8 + 0.3 * kin.pawl));
    ctx.stroke();
  }

  /** Le pont : le pignon, et selon la voiture une couronne parallèle derrière la boîte, ou un couple conique en bout. */
  private drawFinalSection(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion): void {
    const { layout } = this;
    const final = layout.final!;
    const pinion = layout.wheels[final.pinion];
    const ring = layout.wheels[final.ring];
    if (final.kind === 'bevel') {
      // La couronne est vue de face : ses dents sont toutes là.
      const prop = layout.shafts.find((s) => s.role === 'prop');
      const start = this.X(prop ? prop.x1 : final.x - ring.radius);
      const scale = this.sy;
      const cx = start + ring.radius * scale;
      const cy = this.Y(final.y);
      ctx.save();
      ctx.translate(cx, cy);
      ctx.scale(scale, scale);
      ctx.rotate(-kin.angles[final.ringBody] - ring.phase + Math.PI);
      ctx.fillStyle = c.wheel;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1 / scale;
      const path = this.path(ring);
      ctx.fill(path);
      ctx.stroke(path);
      ctx.fillStyle = theme.surface;
      ctx.beginPath();
      ctx.arc(0, 0, ring.radius * 0.5, 0, TAU);
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 3 / scale;
      ctx.beginPath();
      ctx.moveTo(-ring.radius * 0.5, 0);
      ctx.lineTo(ring.radius * 0.5, 0);
      ctx.stroke();
      ctx.restore();
      // Le pignon d'attaque, en cône.
      const tipX = start + ring.radius * 0.36 * scale;
      const backX = start - (pinion.radius + pinion.moduleN) * scale * 0.2;
      const r = pinion.radius * scale;
      const y = this.Y(final.y);
      ctx.fillStyle = c.wheel;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(backX, y - r);
      ctx.lineTo(tipX, y - r * 0.55);
      ctx.lineTo(tipX, y + r * 0.55);
      ctx.lineTo(backX, y + r);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
      const angle = kin.angles[pinion.body] + pinion.phase;
      ctx.strokeStyle = c.tooth;
      ctx.beginPath();
      for (let k = 0; k < pinion.teeth; k++) {
        const a = angle + (k / pinion.teeth) * TAU;
        if (Math.sin(a) < 0) continue;
        ctx.moveTo(backX, y - r * Math.cos(a));
        ctx.lineTo(tipX, y - r * 0.55 * Math.cos(a + 0.4));
      }
      ctx.stroke();
      return;
    }
    // Pont de traction : la couronne est derrière la boîte, on n'en voit de profil qu'un morceau, jusqu'au bord du cadre.
    const left = this.X(ring.x - ring.width / 2);
    const right = this.X(ring.x + ring.width / 2);
    const pinionBottom = this.Y(pinion.y - pinion.radius - pinion.moduleN);
    const frameBottom = this.section.y + this.section.h - 16;
    if (pinionBottom < frameBottom) {
      ctx.fillStyle = c.wheel;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(left, pinionBottom + 1);
      ctx.lineTo(right, pinionBottom + 1);
      ctx.lineTo(right, frameBottom - 3);
      const steps = 5;
      for (let k = steps; k >= 0; k--) {
        ctx.lineTo(left + ((right - left) * k) / steps, frameBottom - (k % 2 === 0 ? 3 : 0));
      }
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
      const angle = kin.angles[ring.body] + ring.phase;
      const twist = (ring.width * Math.tan(ring.helix)) / ring.radius;
      ctx.strokeStyle = c.tooth;
      ctx.beginPath();
      for (let k = 0; k < ring.teeth; k++) {
        const a = angle + (k / ring.teeth) * TAU;
        if (Math.sin(a) < 0) continue;
        const y0 = pinionBottom + 1 + (frameBottom - 3 - pinionBottom - 1) * (0.5 - 0.5 * Math.cos(a - twist / 2));
        const y1 = pinionBottom + 1 + (frameBottom - 3 - pinionBottom - 1) * (0.5 - 0.5 * Math.cos(a + twist / 2));
        ctx.moveTo(left, y0);
        ctx.lineTo(right, y1);
      }
      ctx.stroke();
    }
  }

  private drawPlanetSection(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion): void {
    const { layout } = this;
    for (const set of layout.planets) {
      const annulus = layout.wheels[set.annulus];
      const sun = layout.wheels[set.sun];
      const planet = layout.wheels[set.planets[0]];
      const left = this.X(set.x - set.width / 2);
      const right = this.X(set.x + set.width / 2);
      const w = right - left;
      const y = (v: number) => this.Y(set.y + v);
      // La couronne, en haut et en bas.
      const outer = annulus.radius + annulus.moduleN * 2;
      ctx.fillStyle = c.wheel;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      for (const sign of [1, -1]) {
        const a = y(sign * outer);
        const b = y(sign * annulus.radius);
        ctx.beginPath();
        ctx.rect(left, Math.min(a, b), w, Math.abs(b - a));
        ctx.fill();
        ctx.stroke();
      }
      // Les satellites, tels qu'ils passent devant et derrière.
      const carrier = kin.angles[layout.output];
      ctx.fillStyle = c.tooth;
      for (let n = 0; n < set.planets.length; n++) {
        const a = carrier + (n / set.planets.length) * TAU;
        if (Math.sin(a) < -0.05) continue;
        const py = set.carrierRadius * Math.cos(a);
        ctx.globalAlpha = 0.4 + 0.5 * Math.max(Math.sin(a), 0);
        ctx.beginPath();
        ctx.rect(left + 1, y(py + planet.radius), w - 2, y(py - planet.radius) - y(py + planet.radius));
        ctx.fill();
        ctx.stroke();
      }
      ctx.globalAlpha = 1;
      // Le planétaire, sur l'arbre primaire.
      ctx.fillStyle = c.active;
      ctx.beginPath();
      ctx.rect(left, y(sun.radius), w, y(-sun.radius) - y(sun.radius));
      ctx.fill();
      ctx.stroke();
      // Les disques de l'embrayage multidisque qui suit.
      ctx.strokeStyle = theme.metal;
      ctx.beginPath();
      const px = right + 4;
      for (let k = 0; k < 4; k++) {
        const x = px + k * 3;
        ctx.moveTo(x, y(outer * 0.96));
        ctx.lineTo(x, y(outer * 0.66));
        ctx.moveTo(x, y(-outer * 0.96));
        ctx.lineTo(x, y(-outer * 0.66));
      }
      ctx.stroke();
    }
  }

  /** Ce qui est en amont de l'arbre : volant, double volant, convertisseur, ou le moteur électrique. */
  private drawInput(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame): void {
    const { layout } = this;
    const g = layout.flywheelGeom;
    const X = (x: number) => this.X(x);
    const Y = (y: number) => this.Y(y);
    if (layout.form === 'reducer') {
      const left = X(g.x - g.width / 2);
      const right = X(g.x + g.width / 2);
      const rotor = g.radius * 0.52;
      ctx.fillStyle = mix(theme.raised, '#b98f46', 0.3);
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.rect(left, Y(g.y + g.radius * 0.88), right - left, Y(g.y - g.radius * 0.88) - Y(g.y + g.radius * 0.88));
      ctx.fill();
      ctx.stroke();
      ctx.fillStyle = c.shaft;
      ctx.beginPath();
      ctx.rect(left + 4, Y(g.y + rotor), right - left - 8, Y(g.y - rotor) - Y(g.y + rotor));
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = theme.line;
      ctx.beginPath();
      const a = kin.angles[layout.input];
      for (let k = 0; k < 12; k++) {
        const b = a + (k / 12) * TAU;
        if (Math.sin(b) < 0) continue;
        const y = Y(g.y + rotor * Math.cos(b));
        ctx.moveTo(left + 4, y);
        ctx.lineTo(right - 4, y);
      }
      ctx.stroke();
      return;
    }
    if (layout.form === 'converter') {
      this.drawConverterSection(ctx, theme, c, frame);
      return;
    }
    // Volant en coupe.
    const left = X(g.x - g.width / 2);
    const right = X(g.x + g.width / 2);
    const top = Y(g.y + g.radius);
    const bottom = Y(g.y - g.radius);
    ctx.fillStyle = c.wheel;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    if (layout.flywheel === 'dual_mass') {
      // Deux masses, et entre elles le canal des ressorts en arc.
      const middle = (left + right) / 2;
      ctx.beginPath();
      ctx.rect(left, top, middle - left, bottom - top);
      ctx.fill();
      ctx.stroke();
      ctx.fillStyle = c.wheelFree;
      const secTop = Y(g.y + g.radius * 0.5);
      ctx.beginPath();
      ctx.rect(middle, secTop, right - middle, Y(g.y - g.radius * 0.5) - secTop);
      ctx.fill();
      ctx.stroke();
      const r = g.radius * 0.72;
      ctx.strokeStyle = theme.accent;
      ctx.lineWidth = 1.5;
      for (const sign of [1, -1]) {
        ctx.beginPath();
        ctx.arc(middle + 4, Y(g.y + sign * r), Math.max((g.radius * 0.11 * this.sy) / 1.3, 3), 0, TAU);
        ctx.stroke();
      }
    } else {
      ctx.beginPath();
      ctx.rect(left, top, right - left, bottom - top);
      ctx.fill();
      ctx.stroke();
    }
    // La couronne de démarreur.
    if (g.ringTeeth > 0) {
      ctx.fillStyle = c.tooth;
      ctx.fillRect(left, top - 2, Math.max((right - left) * 0.32, 3), 2);
      ctx.fillRect(left, bottom, Math.max((right - left) * 0.32, 3), 2);
    }
    // L'embrayage : il a sa propre vue, ici sa place et son état.
    const clutchX = X(layout.clutchX);
    const clutchTop = Y(g.y + g.radius * 0.84);
    const clutchBottom = Y(g.y - g.radius * 0.84);
    ctx.fillStyle = mix(theme.raised, theme.accent, 0.12 + 0.3 * frame.clutch);
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    if (layout.form === 'dual_clutch') {
      const mixShare = kin.clutchMix;
      ctx.fillStyle = mix(theme.raised, theme.accent, (1 - mixShare) * frame.clutch * 0.8);
      ctx.fillRect(clutchX - 10, clutchTop, 7, clutchBottom - clutchTop);
      ctx.strokeRect(clutchX - 10, clutchTop, 7, clutchBottom - clutchTop);
      ctx.fillStyle = mix(theme.raised, theme.cool, mixShare * frame.clutch * 0.8);
      const inset = (clutchBottom - clutchTop) * 0.1;
      ctx.fillRect(clutchX + 3, clutchTop + inset, 7, clutchBottom - clutchTop - inset * 2);
      ctx.strokeRect(clutchX + 3, clutchTop + inset, 7, clutchBottom - clutchTop - inset * 2);
    } else {
      ctx.fillRect(clutchX - 4, clutchTop, 8, clutchBottom - clutchTop);
      ctx.strokeRect(clutchX - 4, clutchTop, 8, clutchBottom - clutchTop);
    }
  }

  private drawConverterSection(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, frame: BoxFrame): void {
    const { layout } = this;
    const R = layout.center * 1.02;
    const y0 = layout.flywheelGeom.y;
    const cx = this.X(-46);
    const w = (R * 0.42) * this.sx;
    const ry = R * this.sy;
    const cy = this.Y(y0);
    // La coque.
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1.5;
    ctx.fillStyle = c.casingFill;
    ctx.beginPath();
    ctx.roundRect(cx - w, cy - ry, w * 2, ry * 2, 10);
    ctx.fill();
    ctx.stroke();
    // Pompe et turbine, face à face, en haut et en bas ; le réacteur entre les deux, plus près de l'axe.
    for (const sign of [1, -1]) {
      const yTop = cy - sign * ry * 0.92;
      const yMid = cy - sign * ry * 0.46;
      ctx.fillStyle = c.tooth;
      ctx.beginPath();
      ctx.moveTo(cx - w * 0.84, yMid);
      ctx.quadraticCurveTo(cx - w * 0.84, yTop, cx - w * 0.08, yTop);
      ctx.lineTo(cx - w * 0.08, yMid);
      ctx.closePath();
      ctx.fill();
      ctx.fillStyle = c.active;
      ctx.beginPath();
      ctx.moveTo(cx + w * 0.84, yMid);
      ctx.quadraticCurveTo(cx + w * 0.84, yTop, cx + w * 0.08, yTop);
      ctx.lineTo(cx + w * 0.08, yMid);
      ctx.closePath();
      ctx.fill();
      ctx.fillStyle = c.brass;
      ctx.fillRect(cx - w * 0.14, cy - sign * ry * 0.46 - (sign > 0 ? 0 : -ry * 0.16) - (sign > 0 ? 0 : ry * 0.16), w * 0.28, ry * 0.16);
    }
    // Le pontage : serré quand le moteur et la turbine tournent ensemble.
    const locked = Math.abs(frame.rpm - frame.inputRpm) < 60 && frame.inputRpm > 300;
    ctx.fillStyle = locked ? theme.accent : theme.metal;
    const lockX = locked ? cx + w * 0.92 : cx + w * 0.97;
    ctx.fillRect(lockX - 1.5, cy - ry * 0.9, 3, ry * 0.6);
    ctx.fillRect(lockX - 1.5, cy + ry * 0.3, 3, ry * 0.6);
    // La plaque d'entraînement.
    const plate = this.X(-82);
    ctx.fillStyle = c.wheel;
    ctx.fillRect(plate - 2, cy - ry * 1.14, 4, ry * 2.28);
  }

  /** De face : la paire en prise, le pont, le volant. Une rangée sur téléphone. */
  private drawPanel(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame, panel: Rect, compact: boolean): void {
    const { layout } = this;
    const hasFinalCell = layout.final?.kind === 'helical';
    const hasInputCell = layout.flywheelBody >= 0;
    const cells: Rect[] = [];
    const count = 1 + (hasFinalCell ? 1 : 0) + (hasInputCell ? 1 : 0);
    const gap = 8;
    if (compact) {
      const w = (panel.w - gap * (count - 1)) / count;
      for (let k = 0; k < count; k++) cells.push({ x: panel.x + k * (w + gap), y: panel.y, w, h: panel.h });
    } else {
      const mainH = count === 1 ? panel.h : panel.h * 0.56;
      cells.push({ x: panel.x, y: panel.y, w: panel.w, h: mainH });
      const restY = panel.y + mainH + gap;
      const restH = panel.h - mainH - gap;
      const rest = count - 1;
      for (let k = 0; k < rest; k++) {
        const w = (panel.w - gap * (rest - 1)) / rest;
        cells.push({ x: panel.x + k * (w + gap), y: restY, w, h: restH });
      }
    }
    let index = 0;
    this.drawMainCell(ctx, theme, c, kin, frame, cells[index++]);
    if (hasFinalCell) this.drawFinalCell(ctx, theme, c, kin, cells[index++]);
    if (hasInputCell) this.drawInputCell(ctx, theme, c, kin, frame, cells[index++]);
  }

  private cellFrame(ctx: CanvasRenderingContext2D, theme: Theme, cell: Rect, title: string): void {
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(cell.x, cell.y, cell.w, cell.h, 6);
    ctx.stroke();
    ctx.fillStyle = theme.dim;
    ctx.font = FONT;
    ctx.textAlign = 'left';
    ctx.fillText(title, cell.x + 7, cell.y + 14, cell.w - 12);
  }

  /** Des roues de face, à l'échelle, dans ce cadre. `spin` : la ligne des centres est mise à l'horizontale si le cadre est large. */
  private fitTrain(ctx: CanvasRenderingContext2D, cell: Rect, wheels: readonly Wheel[], spin: number, marginTop = 18): number {
    let y0 = Infinity;
    let y1 = -Infinity;
    let z0 = Infinity;
    let z1 = -Infinity;
    const c = Math.cos(spin);
    const s = Math.sin(spin);
    let origin = Infinity;
    for (const w of wheels) if (origin === Infinity) origin = w.y;
    const r = wheels[0];
    for (const w of wheels) {
      const dy = w.y - r.y;
      const dz = w.z - r.z;
      const u = dz * c + dy * s;
      const v = dz * s - dy * c;
      const R = w.radius + w.moduleN * 1.3;
      y0 = Math.min(y0, u - R);
      y1 = Math.max(y1, u + R);
      z0 = Math.min(z0, v - R);
      z1 = Math.max(z1, v + R);
    }
    const scale = Math.min((cell.w - 10) / Math.max(y1 - y0, 1), (cell.h - marginTop - 6) / Math.max(z1 - z0, 1));
    const cx = cell.x + cell.w / 2 - (scale * (y0 + y1)) / 2;
    const cy = cell.y + marginTop + (cell.h - marginTop - 6) / 2 - (scale * (z0 + z1)) / 2;
    ctx.translate(cx, cy);
    ctx.scale(scale, scale);
    ctx.rotate(spin);
    return scale;
  }

  /** Une roue de face dans le repère de `fitTrain` : y vers le haut de l'écran, z à droite. */
  private frontWheel(ctx: CanvasRenderingContext2D, theme: Theme, kin: GearboxMotion, wheel: Wheel, origin: Wheel, scale: number, fill: string, stroke: string): void {
    ctx.save();
    ctx.translate(wheel.z - origin.z, -(wheel.y - origin.y));
    ctx.rotate(kin.angles[wheel.body] + wheel.phase - Math.PI / 2);
    ctx.fillStyle = fill;
    ctx.strokeStyle = stroke;
    ctx.lineWidth = 1 / scale;
    const path = this.path(wheel);
    ctx.fill(path);
    ctx.stroke(path);
    if (!wheel.internal) {
      // Le moyeu, et un repère : on voit tourner la roue et de combien.
      ctx.fillStyle = theme.surface;
      ctx.beginPath();
      ctx.arc(0, 0, Math.max(wheel.radius * 0.3, 2.4), 0, TAU);
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 2.5 / scale;
      ctx.beginPath();
      ctx.moveTo(wheel.radius * 0.3, 0);
      ctx.lineTo(wheel.radius * 0.8, 0);
      ctx.stroke();
    }
    ctx.restore();
  }

  private drawMainCell(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame, cell: Rect): void {
    const { layout } = this;
    if (layout.planets.length > 0) {
      this.drawPlanetCell(ctx, theme, c, kin, cell);
      return;
    }
    if (frame.gear === -1 && layout.reverse) {
      const r = layout.reverse;
      const wheels = [layout.wheels[r.pinion], layout.wheels[r.idler], layout.wheels[r.wheel]];
      this.cellFrame(ctx, theme, cell, this.tr('box.view.reverse'));
      ctx.save();
      const scale = this.fitTrain(ctx, cell, wheels, 0);
      for (const w of wheels) this.frontWheel(ctx, theme, kin, w, wheels[0], scale, c.active, theme.lineStrong);
      ctx.restore();
      return;
    }
    const pairs = layout.pairs;
    const wanted = frame.gear > 0 ? frame.gear : frame.pending > 0 ? frame.pending : 1;
    let pair = pairs[0];
    for (const p of pairs) if (p.gear === wanted) pair = p;
    if (!pair) return;
    if (pair.direct) {
      // Prise directe : aucune denture ne travaille dans la boîte.
      this.cellFrame(ctx, theme, cell, `${wanted} — ${this.tr('box.direct')}`);
      ctx.fillStyle = theme.dim;
      ctx.textAlign = 'center';
      ctx.font = FONT;
      ctx.fillText(this.tr('box.view.directNote'), cell.x + cell.w / 2, cell.y + cell.h / 2, cell.w - 14);
      return;
    }
    const driver = layout.wheels[pair.driver];
    const driven = layout.wheels[pair.driven];
    const on = kin.engaged === wanted;
    const wide = cell.w > cell.h;
    this.cellFrame(ctx, theme, cell, this.tr('box.view.pairTitle', { gear: wanted, a: driver.teeth, b: driven.teeth }));
    ctx.save();
    const wheels = [driver, driven];
    const scale = this.fitTrain(ctx, cell, wheels, wide ? -Math.PI / 2 : 0);
    const fill = on ? c.active : c.wheelFree;
    this.frontWheel(ctx, theme, kin, driver, driver, scale, fill, on ? theme.accent : theme.lineStrong);
    this.frontWheel(ctx, theme, kin, driven, driver, scale, fill, on ? theme.accent : theme.lineStrong);
    ctx.restore();
    // Les vitesses réelles de cette paire.
    ctx.fillStyle = theme.dim;
    ctx.textAlign = 'right';
    ctx.fillText(`${Math.round(Math.abs(kin.rpm[driver.body]))} / ${Math.round(Math.abs(kin.rpm[driven.body]))} ${this.tr('unit.rpm')}`, cell.x + cell.w - 7, cell.y + cell.h - 6, cell.w * 0.8);
  }

  private drawFinalCell(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, cell: Rect): void {
    const { layout } = this;
    const final = layout.final!;
    const pinion = layout.wheels[final.pinion];
    const ring = layout.wheels[final.ring];
    this.cellFrame(ctx, theme, cell, this.tr('box.view.finalTitle', { a: pinion.teeth, b: ring.teeth }));
    ctx.save();
    const scale = this.fitTrain(ctx, cell, [pinion, ring], 0);
    this.frontWheel(ctx, theme, kin, pinion, pinion, scale, c.wheel, theme.lineStrong);
    this.frontWheel(ctx, theme, kin, ring, pinion, scale, c.wheelFree, theme.lineStrong);
    ctx.restore();
  }

  private drawInputCell(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, frame: BoxFrame, cell: Rect): void {
    const { layout } = this;
    const g = layout.flywheelGeom;
    const title = layout.form === 'converter' ? this.tr('box.part.converter') : layout.flywheel === 'dual_mass' ? this.tr('box.part.dualMass') : this.tr('box.part.flywheel');
    this.cellFrame(ctx, theme, cell, title);
    const R = Math.min(cell.w, cell.h - 22) / 2 - 3;
    if (R < 8) return;
    const cx = cell.x + cell.w / 2;
    const cy = cell.y + 18 + (cell.h - 22) / 2;
    const primary = kin.angles[layout.flywheelBody];
    ctx.fillStyle = c.wheelFree;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, R, 0, TAU);
    ctx.fill();
    ctx.stroke();
    // Les dents de la couronne de démarreur : on les devine au bord.
    if (g.ringTeeth > 0) {
      ctx.strokeStyle = c.tooth;
      ctx.beginPath();
      const n = g.ringTeeth;
      for (let k = 0; k < n; k++) {
        const a = primary + (k / n) * TAU;
        ctx.moveTo(cx + Math.cos(a) * R * 0.95, cy + Math.sin(a) * R * 0.95);
        ctx.lineTo(cx + Math.cos(a) * R, cy + Math.sin(a) * R);
      }
      ctx.stroke();
    }
    if (layout.flywheel === 'dual_mass' && layout.form !== 'converter') {
      this.dualMassFace(ctx, theme, c, kin, cx, cy, R);
    } else {
      ctx.fillStyle = c.wheel;
      ctx.beginPath();
      ctx.arc(cx, cy, R * 0.55, 0, TAU);
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = theme.bg;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + Math.cos(primary) * R * 0.5, cy + Math.sin(primary) * R * 0.5);
      ctx.stroke();
      if (layout.form === 'converter') {
        const pump = Math.abs(frame.rpm);
        const turbine = Math.abs(frame.inputRpm);
        ctx.fillStyle = theme.dim;
        ctx.textAlign = 'center';
        ctx.fillText(`${Math.round(pump)} → ${Math.round(turbine)}`, cx, cy + 4);
      }
    }
  }

  /** Les ressorts en arc, vus de face : ils se tassent sous le couple. */
  private dualMassFace(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, cx: number, cy: number, R: number): void {
    const { layout } = this;
    const primary = kin.angles[layout.flywheelBody];
    const secondary = kin.angles[layout.flywheelSecondaryBody];
    const springR = R * 0.74;
    ctx.lineCap = 'round';
    for (let s = 0; s < 2; s++) {
      // La butée sur la masse primaire, la patte sur la masse secondaire : le ressort entre les deux.
      const stop = primary + s * Math.PI - 1.45;
      const tab = secondary + s * Math.PI + 1.45;
      const arc = tab - stop;
      const coils = 16;
      const half = R * 0.075;
      // Les deux bords du ressort, et une spire tous les quelques degrés : serrées quand le couple le tasse.
      ctx.strokeStyle = theme.line;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(cx, cy, springR - half, stop, tab);
      ctx.moveTo(cx + Math.cos(stop) * (springR + half), cy + Math.sin(stop) * (springR + half));
      ctx.arc(cx, cy, springR + half, stop, tab);
      ctx.stroke();
      ctx.strokeStyle = theme.accent;
      ctx.lineWidth = 1.4;
      ctx.beginPath();
      for (let k = 0; k <= coils; k++) {
        const a = stop + (arc * k) / coils;
        const lean = (arc / coils) * 0.35;
        ctx.moveTo(cx + Math.cos(a - lean) * (springR - half), cy + Math.sin(a - lean) * (springR - half));
        ctx.lineTo(cx + Math.cos(a + lean) * (springR + half), cy + Math.sin(a + lean) * (springR + half));
      }
      ctx.stroke();
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(stop) * (springR - R * 0.13), cy + Math.sin(stop) * (springR - R * 0.13));
      ctx.lineTo(cx + Math.cos(stop) * (springR + R * 0.13), cy + Math.sin(stop) * (springR + R * 0.13));
      ctx.moveTo(cx + Math.cos(tab) * (springR - R * 0.13), cy + Math.sin(tab) * (springR - R * 0.13));
      ctx.lineTo(cx + Math.cos(tab) * (springR + R * 0.13), cy + Math.sin(tab) * (springR + R * 0.13));
      ctx.stroke();
    }
    ctx.lineCap = 'butt';
    ctx.fillStyle = c.wheel;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, R * 0.5, 0, TAU);
    ctx.fill();
    ctx.stroke();
    ctx.strokeStyle = theme.bg;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(secondary) * R * 0.46, cy + Math.sin(secondary) * R * 0.46);
    ctx.stroke();
  }

  /** Un train épicycloïdal de face, le dernier de la ligne : planétaire, satellites, couronne. */
  private drawPlanetCell(ctx: CanvasRenderingContext2D, theme: Theme, c: Palette, kin: GearboxMotion, cell: Rect): void {
    const { layout } = this;
    const set = layout.planets[layout.planets.length - 1];
    const sun = layout.wheels[set.sun];
    const annulus = layout.wheels[set.annulus];
    this.cellFrame(ctx, theme, cell, this.tr('box.view.planetTitle', { sun: sun.teeth, ring: annulus.teeth, planets: set.planets.length }));
    const size = Math.min(cell.w, cell.h - 26);
    const scale = size / (2 * (annulus.radius + annulus.moduleN * 2.6));
    const cx = cell.x + cell.w / 2;
    const cy = cell.y + 20 + (cell.h - 26) / 2;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.scale(scale, scale);

    // La couronne : la peau extérieure, puis sa denture.
    ctx.save();
    ctx.rotate(kin.angles[annulus.body] + annulus.phase - Math.PI / 2);
    ctx.fillStyle = c.wheel;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1 / scale;
    ctx.beginPath();
    ctx.arc(0, 0, annulus.radius + annulus.moduleN * 2.4, 0, TAU);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = theme.surface;
    const path = this.path(annulus);
    ctx.fill(path);
    ctx.stroke(path);
    ctx.restore();

    // Les satellites, autour du planétaire.
    const carrier = kin.angles[layout.output];
    for (let n = 0; n < set.planets.length; n++) {
      const planet = layout.wheels[set.planets[n]];
      const a = carrier + (n / set.planets.length) * TAU;
      ctx.save();
      // Mêmes conventions que `frontWheel` : y vers le haut de l'écran, z à droite.
      ctx.translate(set.carrierRadius * Math.sin(a), -set.carrierRadius * Math.cos(a));
      ctx.rotate(kin.angles[planet.body] + planet.phase - Math.PI / 2);
      ctx.fillStyle = c.wheelFree;
      const p = this.path(planet);
      ctx.fill(p);
      ctx.stroke(p);
      ctx.restore();
    }
    ctx.save();
    ctx.rotate(kin.angles[sun.body] + sun.phase - Math.PI / 2);
    ctx.fillStyle = c.active;
    const s = this.path(sun);
    ctx.fill(s);
    ctx.stroke(s);
    ctx.restore();
    // Le porte-satellites : un trait d'axe à axe.
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 2 / scale;
    ctx.beginPath();
    for (let n = 0; n < set.planets.length; n++) {
      const a = carrier + (n / set.planets.length) * TAU;
      ctx.moveTo(0, 0);
      ctx.lineTo(set.carrierRadius * Math.sin(a), -set.carrierRadius * Math.cos(a));
    }
    ctx.stroke();
    ctx.restore();

  }

  /** Le haut du carter, à l'écran : l'état de la boîte s'écrit juste au-dessus. */
  casingTop(): number {
    return this.Y(this.layout.bounds.y1);
  }

  /** Les repères de pièces, à leur place dans la coupe. */
  anchorAt(index: number, out: { x: number; y: number }): void {
    const a = this.layout.anchors[index];
    out.x = this.X(a.x);
    out.y = this.Y(a.y);
  }
}
