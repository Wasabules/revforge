<script lang="ts">
  import { onMount } from 'svelte';
  import { BODIES, EXHAUST_LINES, TURBO_SCALE, isRotary, type Body, type EngineConfig } from '../lib/config';
  import { animate, fit, gasColor, heatedPart, mix, pipeColor, readTheme, type Theme } from '../lib/canvas';
  import type { Telemetry } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';
  import { PLUME_CELL_M, PLUME_EXIT_ROW, PLUME_PIPE_CELLS } from '../lib/fluid';
  import { firingOrder, type Motion } from '../lib/motion';

  interface Props {
    config: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    brake: number;
  }
  let { config, telemetry, motion, brake }: Props = $props();

  /** Silhouette vue de profil, avant à droite : longueur et hauteur en mètres, points en fractions. */
  interface Shape {
    length: number;
    height: number;
    rearAxle: number;
    wheelbase: number;
    outline: [number, number][];
    glass: [number, number][];
    /** Une caisse à arêtes franches : sans lui, le contour s'arrondit et les passages de roue suivent le pneu. */
    trim?: Trim;
  }
  /** Ce qui fait une voiture carrée : tout en fractions de la silhouette, sauf le rayon et le jeu, en mètres. */
  interface Trim {
    corner: number;
    pillar: number;
    archGap: number;
    /** De `x0` à `x1`, de `y0` à `y1`. */
    frontBumper: [number, number, number, number];
    rearBumper: [number, number, number, number];
    headlight: [number, number, number, number];
    taillight: [number, number, number, number];
    strip: [number, number, number];
    gutter: [number, number, number];
    mirror: [number, number];
    handle: [number, number];
  }
  const SHAPES: Record<Body, Shape> = {
    city: {
      length: 3.75, height: 1.5, rearAxle: 0.6, wheelbase: 2.45,
      outline: [[0, 0.3], [0.005, 0.56], [0.05, 0.86], [0.17, 0.99], [0.48, 1], [0.67, 0.67], [0.88, 0.59], [0.99, 0.46], [1, 0.26], [0.97, 0.15], [0.03, 0.15]],
      glass: [[0.07, 0.62], [0.17, 0.92], [0.46, 0.93], [0.62, 0.65]],
    },
    compact: {
      length: 4.3, height: 1.45, rearAxle: 0.75, wheelbase: 2.62,
      outline: [[0, 0.32], [0.005, 0.56], [0.06, 0.8], [0.2, 0.98], [0.47, 1], [0.66, 0.68], [0.9, 0.58], [0.99, 0.44], [1, 0.25], [0.97, 0.14], [0.03, 0.14]],
      glass: [[0.09, 0.64], [0.2, 0.92], [0.45, 0.93], [0.61, 0.66]],
    },
    coupe: {
      length: 4.6, height: 1.3, rearAxle: 0.85, wheelbase: 2.72,
      outline: [[0, 0.35], [0.01, 0.6], [0.06, 0.68], [0.28, 0.97], [0.45, 1], [0.6, 0.68], [0.88, 0.54], [0.99, 0.42], [1, 0.24], [0.97, 0.13], [0.03, 0.13]],
      glass: [[0.15, 0.67], [0.3, 0.92], [0.44, 0.93], [0.56, 0.67]],
    },
    supercar: {
      length: 4.55, height: 1.15, rearAxle: 0.95, wheelbase: 2.65,
      outline: [[0, 0.42], [0.005, 0.74], [0.12, 0.8], [0.33, 0.97], [0.5, 1], [0.7, 0.58], [0.93, 0.36], [1, 0.26], [0.995, 0.15], [0.96, 0.1], [0.03, 0.1]],
      glass: [[0.3, 0.72], [0.37, 0.92], [0.49, 0.93], [0.64, 0.6]],
    },
    van: {
      length: 4.9, height: 1.95, rearAxle: 1.0, wheelbase: 3.2,
      outline: [[0, 0.24], [0, 0.93], [0.03, 1], [0.66, 1], [0.8, 0.62], [0.93, 0.5], [1, 0.36], [1, 0.2], [0.97, 0.12], [0.03, 0.12]],
      glass: [[0.56, 0.6], [0.56, 0.92], [0.66, 0.93], [0.76, 0.62]],
    },
    retro: {
      length: 4.25, height: 1.36, rearAxle: 0.95, wheelbase: 2.5,
      outline: [[0.02, 0.15], [0.004, 0.24], [0.002, 0.66], [0.012, 0.715], [0.2, 0.735], [0.29, 0.995], [0.6, 1], [0.69, 0.71], [0.975, 0.645], [0.998, 0.615], [1, 0.27], [0.985, 0.16]],
      glass: [[0.222, 0.725], [0.302, 0.95], [0.587, 0.955], [0.664, 0.715]],
      trim: {
        corner: 0.025, pillar: 0.415, archGap: 0.06,
        frontBumper: [0.905, 1.012, 0.26, 0.385],
        rearBumper: [-0.012, 0.125, 0.26, 0.385],
        headlight: [0.966, 0.997, 0.5, 0.585],
        taillight: [0.003, 0.034, 0.53, 0.655],
        strip: [0.125, 0.905, 0.46],
        gutter: [0.3, 0.592, 0.975],
        mirror: [0.652, 0.748],
        handle: [0.43, 0.625],
      },
    },
  };

  let canvas: HTMLCanvasElement;
  let scroll = 0;
  const smoke: { x: number; y: number; age: number; size: number }[] = [];

  function outlinePath(ctx: CanvasRenderingContext2D, points: [number, number][], toX: (x: number) => number, toY: (y: number) => number) {
    ctx.beginPath();
    const last = points[points.length - 1];
    ctx.moveTo((toX(last[0]) + toX(points[0][0])) / 2, (toY(last[1]) + toY(points[0][1])) / 2);
    for (let i = 0; i < points.length; i++) {
      const [x, y] = points[i];
      const [nx, ny] = points[(i + 1) % points.length];
      ctx.quadraticCurveTo(toX(x), toY(y), (toX(x) + toX(nx)) / 2, (toY(y) + toY(ny)) / 2);
    }
    ctx.closePath();
  }

  /** Le contour à arêtes franches : des segments droits, chaque angle à peine cassé. */
  function angularPath(ctx: CanvasRenderingContext2D, points: [number, number][], toX: (x: number) => number, toY: (y: number) => number, radius: number) {
    ctx.beginPath();
    const last = points[points.length - 1];
    ctx.moveTo((toX(last[0]) + toX(points[0][0])) / 2, (toY(last[1]) + toY(points[0][1])) / 2);
    for (let i = 0; i < points.length; i++) {
      const [x, y] = points[i];
      const [nx, ny] = points[(i + 1) % points.length];
      ctx.arcTo(toX(x), toY(y), (toX(x) + toX(nx)) / 2, (toY(y) + toY(ny)) / 2, radius);
    }
    ctx.closePath();
  }

  /** Un passage de roue carré : droit de chaque côté du pneu, plat au-dessus, les angles coupés. */
  function squareArch(ctx: CanvasRenderingContext2D, theme: Theme, center: number, tyre: number, gap: number, sill: number, X: (m: number) => number, Y: (m: number) => number) {
    const half = tyre + gap;
    const top = 2 * tyre + gap;
    const cut = gap;
    ctx.beginPath();
    ctx.moveTo(X(center - half), Y(sill - 0.03));
    ctx.lineTo(X(center - half), Y(top - cut));
    ctx.lineTo(X(center - half + cut), Y(top));
    ctx.lineTo(X(center + half - cut), Y(top));
    ctx.lineTo(X(center + half), Y(top - cut));
    ctx.lineTo(X(center + half), Y(sill - 0.03));
    ctx.fillStyle = theme.bg;
    ctx.fill();
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'miter';
    ctx.stroke();
  }

  /** Pare-chocs épais, baguette, phare et feu rectangulaires, gouttière, rétroviseur et poignée carrés. */
  function drawTrim(ctx: CanvasRenderingContext2D, theme: Theme, trim: Trim, bodyX: (f: number) => number, bodyY: (f: number) => number, braking: number) {
    const block = ([x0, x1, y0, y1]: [number, number, number, number], fill: string, edge: string, radius: number) => {
      ctx.beginPath();
      ctx.roundRect(bodyX(x0), bodyY(y1), bodyX(x1) - bodyX(x0), bodyY(y0) - bodyY(y1), radius);
      ctx.fillStyle = fill;
      ctx.fill();
      ctx.strokeStyle = edge;
      ctx.lineWidth = 1;
      ctx.stroke();
    };
    const line = (x0: number, x1: number, y: number, color: string, width: number) => {
      ctx.strokeStyle = color;
      ctx.lineWidth = width;
      ctx.lineCap = 'butt';
      ctx.beginPath();
      ctx.moveTo(bodyX(x0), bodyY(y));
      ctx.lineTo(bodyX(x1), bodyY(y));
      ctx.stroke();
    };
    line(trim.strip[0], trim.strip[1], trim.strip[2], theme.lineStrong, 2);
    line(trim.gutter[0], trim.gutter[1], trim.gutter[2], theme.lineStrong, 1);
    const rubber = mix(theme.raised, theme.text, 0.18);
    block(trim.frontBumper, rubber, theme.text, 2);
    block(trim.rearBumper, rubber, theme.text, 2);
    block(trim.headlight, mix(theme.text, theme.accent, 0.15), theme.text, 1);
    block(trim.taillight, mix(theme.raised, theme.danger, 0.65 + 0.35 * braking), theme.text, 1);
    // La poignée et le rétroviseur : deux petits rectangles, à l'échelle de la voiture.
    const size = Math.max(2, (bodyX(0.03) - bodyX(0)) * 0.6);
    ctx.fillStyle = theme.lineStrong;
    ctx.fillRect(bodyX(trim.handle[0]), bodyY(trim.handle[1]), size * 1.6, Math.max(2, size * 0.4));
    ctx.fillStyle = theme.text;
    ctx.fillRect(bodyX(trim.mirror[0]), bodyY(trim.mirror[1]), size, size * 0.7);
  }

  function tube(ctx: CanvasRenderingContext2D, points: [number, number][], width: number, color: string) {
    ctx.beginPath();
    ctx.moveTo(points[0][0], points[0][1]);
    for (const [x, y] of points.slice(1)) ctx.lineTo(x, y);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.lineWidth = width;
    ctx.strokeStyle = color;
    ctx.stroke();
  }

  /** Point à une fraction du parcours, en longueur. */
  function along(points: [number, number][], t: number): [number, number] {
    let total = 0;
    const lengths = points.slice(1).map((p, i) => {
      const length = Math.hypot(p[0] - points[i][0], p[1] - points[i][1]);
      total += length;
      return length;
    });
    let left = t * total;
    for (let i = 0; i < lengths.length; i++) {
      if (left <= lengths[i]) {
        const f = lengths[i] > 0 ? left / lengths[i] : 0;
        return [points[i][0] + (points[i + 1][0] - points[i][0]) * f, points[i][1] + (points[i + 1][1] - points[i][1]) * f];
      }
      left -= lengths[i];
    }
    return points[points.length - 1];
  }

  /** Comme `along`, sans rien allouer : le résultat va dans `spot`. */
  const spot = new Float64Array(2);
  function place(points: [number, number][], lengths: Float64Array, total: number, t: number) {
    let left = t * total;
    for (let i = 0; i < points.length - 1; i++) {
      if (left <= lengths[i] || i === points.length - 2) {
        const f = lengths[i] > 0 ? Math.min(left / lengths[i], 1) : 0;
        spot[0] = points[i][0] + (points[i + 1][0] - points[i][0]) * f;
        spot[1] = points[i][1] + (points[i + 1][1] - points[i][1]) * f;
        return;
      }
      left -= lengths[i];
    }
  }

  const lineLengths = new Float64Array(8);
  const airLengths = new Float64Array(8);
  function measure(points: [number, number][], into: Float64Array): number {
    let total = 0;
    for (let i = 0; i < points.length - 1; i++) {
      into[i] = Math.hypot(points[i + 1][0] - points[i][0], points[i + 1][1] - points[i][1]);
      total += into[i];
    }
    return total;
  }

  /** La ligne de la voiture : la température calculée, cellule après cellule, et la matière qui y avance. */
  function drawLineGas(ctx: CanvasRenderingContext2D, theme: Theme, line: [number, number][], width: number) {
    const gas = motion.gas;
    if (gas.net.n === 0 || motion.reduced) return;
    const net = gas.net;
    const total = measure(line, lineLengths);
    const painter = motion.painter;
    ctx.lineCap = 'butt';
    ctx.lineWidth = width * 0.72;
    for (let i = 0; i < net.lineCount; i++) {
      const cell = net.lineFirst + i;
      ctx.strokeStyle = painter.gasStyle(theme, net.temp[cell], net.y[cell]);
      ctx.beginPath();
      place(line, lineLengths, total, i / net.lineCount);
      ctx.moveTo(spot[0], spot[1]);
      place(line, lineLengths, total, (i + 1.2) / net.lineCount);
      ctx.lineTo(spot[0], spot[1]);
      ctx.stroke();
    }
    if (width < 5) return;
    const { pSection, pX, pBurnt, pY } = gas;
    const hot = '#ffeed6';
    const cold = '#cfe6ff';
    ctx.globalAlpha = 0.85;
    const cellSize = net.cell;
    const length = net.lineLength;
    for (let k = 0; k < gas.count; k++) {
      const s = pSection[k];
      if (s < 0 || s >= 100) continue;
      if (gas.chamberOf[s]) continue;
      const section = net.sections[s];
      if (!section) continue;
      const t = (section.from + pX[k]) / Math.max(length, cellSize);
      place(line, lineLengths, total, t < 0 ? 0 : t > 1 ? 1 : t);
      ctx.fillStyle = pBurnt[k] > 0.5 ? hot : cold;
      ctx.fillRect(spot[0] - 0.8, spot[1] - 0.8 + pY[k] * width * 0.22, 1.6, 1.6);
    }
    ctx.globalAlpha = 1;
  }

  /** L'admission : la densité réelle de l'air, et ses particules à leur vitesse. */
  function drawAirGas(ctx: CanvasRenderingContext2D, theme: Theme, path: [number, number][], width: number) {
    const gas = motion.gas;
    if (gas.net.n === 0 || motion.reduced) return;
    const net = gas.net;
    const total = measure(path, airLengths);
    const n = net.intakeCount;
    ctx.lineCap = 'butt';
    ctx.lineWidth = width;
    for (let i = 0; i < n; i++) {
      const cell = net.intakeFirst + i;
      const level = net.rho[cell] / 2.6;
      ctx.strokeStyle = theme.cool;
      ctx.globalAlpha = 0.12 + 0.75 * (level > 1 ? 1 : level);
      ctx.beginPath();
      place(path, airLengths, total, i / n);
      ctx.moveTo(spot[0], spot[1]);
      place(path, airLengths, total, (i + 1) / n);
      ctx.lineTo(spot[0], spot[1]);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
    const { pSection, pX } = gas;
    const cell = net.cell;
    ctx.fillStyle = theme.text;
    ctx.globalAlpha = 0.75;
    for (let k = 0; k < gas.count; k++) {
      const s = pSection[k];
      if (s < 100) continue;
      const duct = gas.ducts[s - 100];
      if (!duct) continue;
      const t = (duct.first - net.intakeFirst + pX[k] / cell) / n;
      place(path, airLengths, total, t < 0 ? 0 : t > 1 ? 1 : t);
      ctx.fillRect(spot[0] - 0.8, spot[1] - 0.8, 1.6, 1.6);
    }
    ctx.globalAlpha = 1;
  }

  /** Un carter en colimaçon et sa roue : ses aubes suivent l'arbre, en disque flou quand l'œil ne les suit plus. */
  function turboWheel(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, radius: number, fill: string, blades: number, angle: number) {
    ctx.fillStyle = fill;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let k = 0; k <= 20; k++) {
      const a = angle * 0 + 2.2 + (k / 20) * Math.PI * 2;
      const r = radius * (0.86 + 0.28 * (k / 20));
      if (k === 0) ctx.moveTo(x + Math.cos(a) * r, y + Math.sin(a) * r);
      else ctx.lineTo(x + Math.cos(a) * r, y + Math.sin(a) * r);
    }
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    const blur = motion.turbineBlur;
    if (blur > 0.02) {
      ctx.globalAlpha = 0.35 * blur;
      ctx.fillStyle = theme.metal;
      ctx.beginPath();
      ctx.arc(x, y, radius * 0.72, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1 - 0.8 * blur;
    ctx.lineWidth = 1;
    ctx.strokeStyle = theme.text;
    ctx.beginPath();
    for (let i = 0; i < blades; i++) {
      const a = angle + (i / blades) * Math.PI * 2;
      ctx.moveTo(x + Math.cos(a) * radius * 0.12, y + Math.sin(a) * radius * 0.12);
      ctx.lineTo(x + Math.cos(a + 0.45) * radius * 0.72, y + Math.sin(a + 0.45) * radius * 0.72);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  /**
   * Le chemin de l'air d'un turbo : boîte à air, compresseur, tube chaud, échangeur tout à l'avant, tube froid, papillon.
   * Moteur central ou arrière : l'échangeur est sur le côté, près de lui. La température calculée se lit tout du long.
   */
  function drawTurboChain(ctx: CanvasRenderingContext2D, theme: Theme, at: {
    behind: boolean; atRear: boolean; left: number; blockWidth: number; blockHeight: number; engineY: number; floor: number; shape: Shape; scale: number;
    toCar: (x: number, y: number) => [number, number]; labels: { x: number; text: string }[];
  }) {
    const { behind, atRear, left, blockWidth, blockHeight, engineY, floor, shape, scale, toCar, labels } = at;
    const net = motion.gas.net;
    const ready = gasReady() && net.compressorCell >= 0;
    const painter = motion.painter;
    const pipe = Math.max(2.5, 0.04 * scale);
    const sizeScale = TURBO_SCALE[config.turboSize ?? 'medium'];
    // Le turbo : à l'arrière du bloc, du côté de l'échappement. La turbine en bas, le compresseur juste au-dessus sur le même arbre.
    // Déporté, il part vers l'arrière de la voiture de la longueur réelle du tube, sous le plancher, sans sortir de la caisse.
    const remote = Math.max(((config.turboPipeMm ?? 400) - 400) / 1000, 0);
    const turboX = Math.max(left - 0.07 - remote, 0.14);
    const lowered = Math.min(remote, 0.1);
    const turbineY = engineY + 0.14 - lowered;
    const compressorY = turbineY + 0.13 * sizeScale;
    const wheelR = 0.062 * sizeScale;
    // Bien déporté, le filtre part avec lui : l'air n'a pas à traverser l'habitacle pour l'atteindre.
    const nearTurbo = remote >= 0.5 && !behind;
    const airX = nearTurbo ? turboX + 0.3 : atRear ? 0.14 : behind ? shape.length * 0.44 : Math.min(left + blockWidth + 0.32, shape.length * 0.93);
    const airY = nearTurbo ? compressorY + 0.12 : behind ? shape.height * 0.66 : engineY + blockHeight * 1.05;
    const throttleX = left + blockWidth * 0.86;
    const throttleY = engineY + blockHeight * 0.92;
    // L'échangeur d'origine, tout à l'avant ; une brique air-eau, juste devant le moteur et sous le papillon ; aucun : le tube chaud
    // remonte de là droit au papillon (voir `ChargePath`, core/src/charge_air.cpp).
    const cooling = config.intercooler ?? 'air_to_air';
    const nearEngine = cooling !== 'air_to_air';
    const coolerRight = nearEngine ? left + blockWidth + 0.03 + (cooling === 'none' ? 0 : 0.12) : behind ? (atRear ? left + blockWidth + 0.5 : left + blockWidth + 0.45) : shape.length * 0.965;
    const coolerLeft = nearEngine ? left + blockWidth + 0.03 : coolerRight - 0.24;
    const coolerBottom = nearEngine ? throttleY - 0.24 : behind ? engineY + 0.18 : floor + 0.04;
    const coolerTop = coolerBottom + (nearEngine ? 0.14 : 0.26);
    const way: Record<string, [number, number][]> = {
      filter: [toCar(airX + 0.09, airY), toCar(airX, airY)],
      inlet: nearTurbo ? [toCar(airX, airY), toCar(turboX, compressorY + 0.05)] : [toCar(airX, airY), toCar(left + blockWidth * 0.5, engineY + blockHeight + 0.06), toCar(turboX, compressorY + 0.05)],
      hot: [toCar(turboX - 0.02, compressorY - 0.05), toCar(turboX - 0.05, floor + 0.01), toCar(coolerLeft - 0.02, floor + 0.01), toCar(coolerLeft, coolerBottom + 0.08)],
      cooler: [toCar(coolerLeft, (coolerBottom + coolerTop) / 2), toCar(coolerRight, (coolerBottom + coolerTop) / 2)],
      cold: cooling === 'none'
        ? [toCar(coolerLeft, coolerBottom + 0.08), toCar(throttleX, throttleY)]
        : [toCar(coolerRight, coolerTop - 0.04), toCar(coolerRight - 0.12, coolerTop + 0.08), toCar(throttleX, throttleY)],
    };
    if (behind) way.hot = [toCar(turboX - 0.02, compressorY - 0.05), toCar(turboX, floor + 0.03), toCar(coolerLeft - 0.03, floor + 0.05), toCar(coolerLeft, coolerBottom + 0.08)];
    const partFor = (kind: string) => way[kind];

    // Le tube d'échappement jusqu'au turbo déporté.
    if (remote > 0) tube(ctx, [toCar(left, engineY + 0.1), toCar(left - 0.05, turbineY), toCar(turboX, turbineY)], Math.max(3, 0.06 * scale), pipeColor(theme, motion.heat));

    // Le fond des tubes, puis l'air calculé, cellule par cellule.
    for (const kind of ['inlet', 'hot', 'cold']) tube(ctx, partFor(kind), pipe + 1.5, theme.lineStrong);
    if (ready && !motion.reduced) {
      ctx.lineCap = 'butt';
      ctx.lineWidth = pipe;
      for (const part of net.intakeParts) {
        if (part.kind === 'filter' || part.kind === 'cooler' || part.kind === 'venturi' || part.kind === 'duct') continue;
        const points = partFor(part.kind);
        const total = measure(points, airLengths);
        for (let k = 0; k < part.count; k++) {
          const cell = part.first + k;
          ctx.strokeStyle = painter.airStyle(theme, net.temp[cell], net.rho[cell]);
          ctx.beginPath();
          place(points, airLengths, total, k / part.count);
          ctx.moveTo(spot[0], spot[1]);
          place(points, airLengths, total, (k + 1) / part.count);
          ctx.lineTo(spot[0], spot[1]);
          ctx.stroke();
        }
      }
    } else {
      for (const kind of ['inlet', 'hot', 'cold']) tube(ctx, partFor(kind), pipe, theme.raised);
    }

    // La boîte à air.
    const [bx, by] = toCar(airX, airY + 0.075);
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(bx, by, 0.18 * scale, 0.15 * scale, 3);
    ctx.fill();
    ctx.stroke();
    labels.push({ x: airX + 0.09, text: t('gas.airbox') });

    // L'échangeur : des tranches à la température de l'air, du chaud (côté tube chaud) au froid. Sans échangeur, rien.
    if (cooling !== 'none') {
      const [cx0, cy0] = toCar(coolerLeft, coolerTop);
      const coolerW = (coolerRight - coolerLeft) * scale;
      const coolerH = (coolerTop - coolerBottom) * scale;
      if (ready && !motion.reduced && net.coolerCount > 0) {
        const steps = Math.min(8, net.coolerCount);
        for (let k = 0; k < steps; k++) {
          const cell = net.coolerFirst + Math.floor((k * net.coolerCount) / steps);
          ctx.fillStyle = painter.airStyle(theme, net.temp[cell], net.rho[cell]);
          ctx.fillRect(cx0 + (coolerW * k) / steps, cy0, coolerW / steps + 0.5, coolerH);
        }
      } else {
        ctx.fillStyle = theme.raised;
        ctx.fillRect(cx0, cy0, coolerW, coolerH);
      }
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 1;
      ctx.globalAlpha = 0.5;
      ctx.beginPath();
      for (let k = 1; k < 7; k++) {
        ctx.moveTo(cx0 + (coolerW * k) / 7, cy0 + 1);
        ctx.lineTo(cx0 + (coolerW * k) / 7, cy0 + coolerH - 1);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.lineWidth = 1.5;
      ctx.strokeRect(cx0, cy0, coolerW, coolerH);
      labels.push({ x: nearEngine ? (coolerLeft + coolerRight) / 2 : coolerRight - 0.12, text: t('gas.intercooler') });
    }

    // La soupape de décharge, sur le tube froid, qui souffle quand elle s'ouvre.
    if (config.turboRelease !== 'flutter') {
      const cold = partFor('cold');
      const total = measure(cold, airLengths);
      place(cold, airLengths, total, 0.55);
      const vx = spot[0];
      const vy = spot[1];
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = motion.blowoff > 0.05 ? theme.accent : theme.text;
      ctx.lineWidth = 1.25;
      ctx.beginPath();
      ctx.arc(vx, vy - pipe * 0.8, Math.max(3, pipe * 0.75), 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
      if (motion.blowoff > 0.03 && !motion.reduced) {
        ctx.strokeStyle = theme.cool;
        for (let i = 0; i < 4; i++) {
          const a = -Math.PI / 2 + (i - 1.5) * 0.45;
          const phase = (motion.turns * 22 + i * 0.37) % 1;
          ctx.globalAlpha = (1 - phase) * Math.min(1, motion.blowoff + 0.2);
          ctx.beginPath();
          ctx.moveTo(vx + Math.cos(a) * (pipe + phase * 10), vy - pipe + Math.sin(a) * (pipe + phase * 10));
          ctx.lineTo(vx + Math.cos(a) * (pipe + phase * 10 + 4), vy - pipe + Math.sin(a) * (pipe + phase * 10 + 4));
          ctx.stroke();
        }
        ctx.globalAlpha = 1;
      }
    }

    // L'arbre, puis les deux roues : la turbine rougit, le compresseur prend la teinte de l'air qui en sort.
    const [tx, ty] = toCar(turboX, turbineY);
    const [kx, ky] = toCar(turboX, compressorY);
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(tx, ty);
    ctx.lineTo(kx, ky);
    ctx.stroke();
    const radius = wheelR * scale;
    const outFill = ready ? painter.airStyle(theme, net.temp[net.compressorCell], net.rho[net.compressorCell]) : theme.raised;
    turboWheel(ctx, theme, kx, ky, radius * 1.15, outFill, 9, -motion.turbineAngle);
    turboWheel(ctx, theme, tx, ty, radius, heatedPart(theme.raised, motion.heat), 7, motion.turbineAngle);
    labels.push({ x: turboX, text: t('live.turbo') });
  }

  function wheel(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, radius: number, rimRadius: number, angle: number, turning: number, braking: number, driven: boolean) {
    ctx.save();
    ctx.translate(x, y);
    ctx.fillStyle = theme.bg;
    ctx.beginPath();
    ctx.arc(0, 0, radius + 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = '#0c0a09';
    ctx.strokeStyle = driven ? theme.accent : theme.lineStrong;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    // Disque de frein : rougit quand on freine.
    ctx.fillStyle = mix(theme.raised, theme.danger, braking);
    ctx.beginPath();
    ctx.arc(0, 0, rimRadius * 0.72, 0, Math.PI * 2);
    ctx.fill();
    ctx.rotate(angle);
    // Au-delà d'une certaine vitesse, l'œil ne suit plus les branches : un disque flou.
    // Cinq branches : l'image se mettrait à tourner à l'envers au-delà de 38 rad/s.
    const blur = Math.min(1, Math.max(0, (turning - 10) / 20));
    ctx.strokeStyle = theme.metal;
    ctx.globalAlpha = 1 - blur * 0.92;
    ctx.lineWidth = Math.max(1.5, rimRadius * 0.14);
    for (let i = 0; i < 5; i++) {
      ctx.rotate((Math.PI * 2) / 5);
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.lineTo(rimRadius, 0);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(0, 0, rimRadius, 0, Math.PI * 2);
    ctx.stroke();
    ctx.fillStyle = theme.lineStrong;
    ctx.beginPath();
    ctx.arc(0, 0, rimRadius * 0.16, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  function draw(seconds: number, now: number) {
    const theme = readTheme(canvas);
    const { ctx, width, height } = fit(canvas);
    const shape = SHAPES[config.body];
    // Moteur au centre ou à l'arrière : il est derrière les sièges, et la ligne est courte.
    const behind = config.placement !== 'front';
    const atRear = config.placement === 'rear';
    motion.update(now, telemetry, config);

    // Échelle : la voiture occupe la largeur, mais tient en hauteur.
    const ground = height - 44;
    const scale = Math.min((width - 40) / (shape.length + 1.1), (ground - 14) / (shape.height + 0.12));
    const carLeft = (width - shape.length * scale) / 2;
    const X = (m: number) => carLeft + m * scale;
    const Y = (m: number) => ground - m * scale;

    // Route et décor : ils défilent à la vitesse de la voiture.
    scroll += (motion.speed / 3.6) * seconds;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(0, ground);
    ctx.lineTo(width, ground);
    ctx.stroke();
    ctx.fillStyle = theme.dim;
    const dash = 3 * scale;
    for (let i = -1; i <= width / dash + 1; i++) {
      const x = i * dash - (scroll * scale) % dash;
      ctx.fillRect(x, ground + 10, dash * 0.45, 2);
    }
    ctx.fillStyle = theme.line;
    // Balises en retrait : elles défilent moins vite, c'est la profondeur.
    const post = 16 * scale;
    for (let i = -1; i <= width / post + 1; i++) {
      const x = i * post - (scroll * scale * 0.5) % post;
      ctx.fillStyle = theme.line;
      ctx.fillRect(x, ground - 0.55 * scale, 4, 0.55 * scale);
      ctx.fillStyle = theme.lineStrong;
      ctx.fillRect(x, ground - 0.55 * scale, 4, 0.1 * scale);
    }

    // La caisse plonge au freinage et s'assoit à l'accélération.
    const pitch = Math.max(-0.035, Math.min(0.035, motion.acceleration * 0.0065));
    const rimM = (config.rimInch * 25.4) / 2000;
    const tyreM = rimM + 0.095 - 0.002 * (config.rimInch - 14);
    const rearX = shape.rearAxle;
    const frontX = shape.rearAxle + shape.wheelbase;
    const midX = (rearX + frontX) / 2;
    const toCar = (mx: number, my: number): [number, number] => [X(mx), Y(my)];

    ctx.save();
    ctx.translate(X(midX), Y(0.5));
    ctx.rotate(-pitch);
    ctx.translate(-X(midX), -Y(0.5));

    // Caisse transparente : on voit la mécanique à travers.
    const bodyX = (f: number) => X(f * shape.length);
    const bodyY = (f: number) => Y(f * shape.height);
    const trim = shape.trim;
    if (trim) angularPath(ctx, shape.outline, bodyX, bodyY, trim.corner * scale);
    else outlinePath(ctx, shape.outline, bodyX, bodyY);
    ctx.fillStyle = theme.surface;
    ctx.globalAlpha = 0.88;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.stroke();
    if (trim) {
      const sill = shape.outline[0][1] * shape.height;
      squareArch(ctx, theme, rearX, tyreM, trim.archGap, sill, X, Y);
      squareArch(ctx, theme, frontX, tyreM, trim.archGap, sill, X, Y);
    }
    // Vitres : des arêtes nettes, un montant au milieu.
    ctx.beginPath();
    shape.glass.forEach(([gx, gy], i) => (i ? ctx.lineTo(bodyX(gx), bodyY(gy)) : ctx.moveTo(bodyX(gx), bodyY(gy))));
    ctx.closePath();
    ctx.fillStyle = theme.raised;
    ctx.fill();
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.stroke();
    const pillar = trim?.pillar ?? (shape.glass[1][0] + shape.glass[2][0]) / 2;
    const glassBottom = Math.min(shape.glass[0][1], shape.glass[3][1]);
    ctx.lineWidth = 3;
    ctx.strokeStyle = theme.surface;
    ctx.beginPath();
    ctx.moveTo(bodyX(pillar), bodyY(shape.glass[1][1]));
    ctx.lineTo(bodyX(pillar), bodyY(glassBottom));
    ctx.stroke();
    // Portière, phares et feux.
    ctx.lineWidth = 1;
    ctx.strokeStyle = theme.line;
    ctx.beginPath();
    ctx.moveTo(bodyX(pillar), bodyY(glassBottom));
    ctx.lineTo(bodyX(pillar), bodyY(0.2));
    ctx.stroke();
    if (trim) {
      drawTrim(ctx, theme, trim, bodyX, bodyY, telemetryBrake() / 0.4);
    } else {
      const front = shape.outline.reduce((best, p) => (p[0] > best[0] ? p : best));
      ctx.fillStyle = mix(theme.text, theme.accent, 0.15);
      ctx.fillRect(bodyX(0.985) - 12, bodyY(front[1] + 0.12), 12, 4);
      ctx.fillStyle = theme.danger;
      ctx.fillRect(bodyX(0.003), bodyY(shape.outline[1][1] - 0.05), 6, 5 + 26 * telemetryBrake());
    }

    const floor = 0.26;
    const litres = config.displacementCc / 1000;
    const labels: { x: number; text: string }[] = [];

    if (config.electric) {
      // Batterie dans le plancher, moteur sur l'essieu.
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.cool;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.roundRect(X(rearX + 0.25), Y(floor + 0.14), (shape.wheelbase - 0.5) * scale, 0.14 * scale, 3);
      ctx.fill();
      ctx.stroke();
      const cells = 10;
      ctx.fillStyle = theme.cool;
      for (let i = 0; i < cells; i++) {
        ctx.globalAlpha = 0.25 + 0.6 * motion.load;
        ctx.fillRect(X(rearX + 0.3 + ((shape.wheelbase - 0.6) * i) / cells), Y(floor + 0.11), ((shape.wheelbase - 0.6) / cells - 0.03) * scale, 0.08 * scale);
      }
      ctx.globalAlpha = 1;
      const motorX = config.drive === 'front' ? frontX : rearX;
      ctx.fillStyle = theme.raised;
      ctx.beginPath();
      ctx.arc(X(motorX), Y(0.32), 0.17 * scale, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = theme.accent;
      ctx.stroke();
      labels.push({ x: midX, text: t('car.battery') }, { x: motorX, text: t('car.motor') });
    } else {
      // Moteur : sa taille suit la cylindrée, ses cylindres se comptent.
      const blockWidth = Math.min((0.42 + 0.075 * config.cylinders * (config.layout === 'inline' ? 1 : 0.62) + 0.035 * litres) * 0.85, shape.wheelbase * 0.42);
      // Il reste sous le capot, même à 7 litres.
      const blockHeight = Math.min(config.layout === 'boxer' ? 0.26 + 0.015 * litres : 0.36 + 0.022 * litres, shape.height * (behind ? 0.42 : 0.3));
      // Devant, il sort juste derrière la roue pour rester visible ; au centre, derrière les sièges ; à l'arrière, en porte-à-faux.
      const engineX = atRear ? rearX - 0.2 : behind ? rearX + tyreM + blockWidth / 2 - 0.05 : frontX - tyreM - blockWidth / 2 + 0.12;
      const engineY = floor + 0.14;
      const left = engineX - blockWidth / 2;

      // Transmission : de la boîte vers les roues motrices.
      const shaftY = floor + 0.08;
      const reaches: number[] = [];
      if (config.drive !== 'front' || behind) reaches.push(rearX);
      if (config.drive !== 'rear' || !behind) reaches.push(frontX);
      if (config.drive === 'rear' && !behind) reaches.pop();
      for (const reach of reaches) tube(ctx, [toCar(left, shaftY), toCar(reach, shaftY)], 2.5, theme.lineStrong);

      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.roundRect(X(left), Y(engineY + blockHeight), blockWidth * scale, blockHeight * scale, 4);
      ctx.fill();
      ctx.stroke();
      // Chaque case s'allume quand c'est le tour de son cylindre, à la même horloge que les autres vues.
      const rotary = isRotary(config);
      // Un rotor s'allume à chaque tour d'arbre : son horloge va deux fois plus vite, et chacun a sa case.
      const row = config.layout === 'inline' || rotary ? config.cylinders : Math.ceil(config.cylinders / 2);
      const perCell = config.cylinders / row;
      const order = firingOrder(config.cylinders);
      const cellWidth = (blockWidth * scale - 6) / row;
      for (let i = 0; i < config.cylinders; i++) {
        const cell = Math.floor(i / perCell);
        const phase = ((motion.cycle * (rotary ? 2 : 1)) + 1 - order.indexOf(i + 1) / config.cylinders) % 1;
        if (i % perCell !== 0 && !(motion.burning && phase < 0.25)) continue;
        ctx.fillStyle = motion.burning && phase < 0.25 ? gasColor(0.4 + 0.6 * motion.load) : theme.bg;
        ctx.fillRect(X(left) + 3 + cell * cellWidth, Y(engineY + blockHeight) + 3, Math.max(cellWidth - 2, 1), blockHeight * scale * 0.42);
      }
      // L'air entre à la vitesse que demande le moteur, plus dense sous pression.
      if (config.aspiration === 'turbo') {
        drawTurboChain(ctx, theme, { behind, atRear, left, blockWidth, blockHeight, engineY, floor, shape, scale, toCar, labels });
      } else {
        const airPath: [number, number][] = behind
          ? [toCar(atRear ? 0.1 : shape.length * 0.42, shape.height * 0.72), toCar(left + blockWidth / 2, engineY + blockHeight)]
          : [toCar(shape.length * 0.96, 0.42), toCar(left + blockWidth, engineY + blockHeight * 0.8)];
        tube(ctx, airPath, Math.max(2.5, 0.04 * scale) + 1, theme.line);
        drawAirGas(ctx, theme, airPath, Math.max(2.5, 0.04 * scale));
        // Le compresseur mécanique : sur le chemin de l'air, entraîné par le vilebrequin. Ni turbine ni tube chaud.
        if (config.aspiration === 'supercharger') {
          const at = airPath[airPath.length - 1];
          const radius = Math.max(4, 0.09 * scale);
          turboWheel(ctx, theme, at[0], at[1] - radius, radius, motion.painter.airStyle(theme, motion.compressorOutK, 1 + motion.boost * config.boostBar), config.superchargerKind === 'centrifugal' ? 9 : 3, motion.chargerAngle);
          labels.push({ x: engineX + blockWidth * 0.4, text: t('live.charger') });
        }
      }
      labels.push({ x: engineX, text: t(rotary ? 'car.engineLabel.rotary' : 'car.engineLabel', { cylinders: config.cylinders, litres: fixed(litres, 1) }) });

      // Ligne d'échappement : du collecteur à la sortie, ses volumes suivent le réglage.
      const tail = 0.02;
      const line: [number, number][] = behind
        ? [toCar(Math.max(left, tail + 0.1), engineY + 0.1), toCar(Math.max(left - 0.12, tail + 0.05), floor), toCar(tail, floor + 0.02)]
        : [toCar(left, engineY + 0.1), toCar(left - 0.1, floor), toCar(rearX + 0.1, floor), toCar(rearX, floor + 0.14), toCar(tail + 0.2, floor + 0.12), toCar(tail, floor + 0.04)];
      // Socle : la chambre de détente se dessine comme la ligne directe.
      const pipeWidth = Math.max(3, (config.exhaust === 'straight' || config.exhaust === 'expansion' ? 0.075 : 0.06) * scale);
      tube(ctx, line, pipeWidth, pipeColor(theme, motion.heat));

      // Le panache d'abord, derrière la ligne : il sort de la grille de calcul, à l'échelle de la voiture.
      if (!motion.reduced && gasReady()) {
        const [exitX, exitY] = along(line, 1);
        const cell = PLUME_CELL_M * scale;
        const ratio = canvas.width / Math.max(width, 1);
        motion.painter.shimmer(ctx, canvas, ratio, exitX, exitY, cell, -1, PLUME_PIPE_CELLS, PLUME_EXIT_ROW, 0.03);
        motion.painter.drawPlume(ctx, theme, exitX, exitY, cell, -1, PLUME_PIPE_CELLS, PLUME_EXIT_ROW);
      }
      drawLineGas(ctx, theme, line, pipeWidth);

      // Les volumes, à la température calculée du gaz qu'ils contiennent.
      const volumeStyle = (kind: string) => {
        const net = motion.gas.net;
        for (const section of net.sections) {
          if (section.kind !== kind) continue;
          const middle = section.first + (section.count >> 1);
          return motion.painter.gasStyle(theme, net.temp[middle], net.y[middle]);
        }
        return theme.raised;
      };
      const box = (center: number, length: number, tall: number, text: string, kind: string) => {
        const [x, y] = toCar(center - length / 2, floor + tall / 2 + 0.02);
        ctx.fillStyle = gasReady() ? volumeStyle(kind) : theme.raised;
        ctx.strokeStyle = theme.metal;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.roundRect(x, y, length * scale, tall * scale, 4);
        ctx.fill();
        ctx.stroke();
        labels.push({ x: center, text });
      };
      const span = behind ? Math.max(left - 0.3, 0.3) : Math.max(left - rearX - 0.2, 0.3);
      const startX = behind ? 0.15 : rearX + 0.15;
      const parts = EXHAUST_LINES[config.exhaust].map((item) => item.part);
      const quietLine = config.exhaust === 'stock' || config.exhaust === 'quiet';
      if (parts.includes('cat')) box(startX + span * 0.72, Math.min(0.3, span * 0.3), 0.08, t('part.cat'), 'cat');
      if (parts.includes('resonator')) box(startX + span * 0.32, Math.min(0.32, span * 0.3), 0.11, t('part.res'), 'res');
      if (parts.includes('muffler') && !behind) box(0.28, 0.42, quietLine ? 0.2 : 0.15, t('part.muffler'), 'muffler');
      if (parts.includes('muffler') && behind) box(0.2, 0.3, 0.14, t('part.muffler'), 'muffler');

    }
    ctx.restore();

    // Roues : les motrices tournent à leur vitesse, patinage compris.
    const driven = config.drive;
    const rearDriven = driven !== 'front';
    const frontDriven = driven !== 'rear';
    const rollRpm = ((motion.speed / 3.6) / tyreM) * (60 / (2 * Math.PI));
    const rates = [rearDriven ? motion.wheelRpm : rollRpm, frontDriven ? motion.wheelRpm : rollRpm].map((rpm) => (rpm * 2 * Math.PI) / 60);
    wheelAngles[0] += rates[0] * seconds;
    wheelAngles[1] += rates[1] * seconds;
    const braking = brake > 0.05 && motion.speed > 1 ? Math.min(1, brake) : 0;
    wheel(ctx, theme, X(rearX), ground - tyreM * scale, tyreM * scale, rimM * scale, wheelAngles[0], rates[0], braking, rearDriven);
    wheel(ctx, theme, X(frontX), ground - tyreM * scale, tyreM * scale, rimM * scale, wheelAngles[1], rates[1], braking, frontDriven);

    // Patinage : fumée de pneus derrière la roue motrice.
    const slipping = Math.abs(motion.slip) > 4;
    if (slipping && Math.random() < seconds * 60) {
      const at = X(rearDriven ? rearX : frontX);
      smoke.push({ x: at - tyreM * scale * 0.6, y: ground - 3, age: 0, size: 6 });
    }
    for (let i = smoke.length - 1; i >= 0; i--) {
      const s = smoke[i];
      s.age += seconds;
      s.x -= (20 + motion.speed * 0.9) * seconds;
      s.y -= 12 * seconds;
      s.size += 16 * seconds;
      if (s.age > 1.1) {
        smoke.splice(i, 1);
        continue;
      }
      ctx.fillStyle = theme.dim;
      ctx.globalAlpha = 0.28 * (1 - s.age / 1.1);
      ctx.beginPath();
      ctx.arc(s.x, s.y, s.size, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1;

    (canvas as unknown as { __labels: string[] }).__labels = labels.map((label) => label.text);
    // Légende sous la route : chaque nom pointe sur sa pièce, sans se chevaucher.
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    const placed: { from: number; to: number; row: number }[] = [];
    for (const label of [...labels].sort((a, b) => a.x - b.x)) {
      const x = X(label.x);
      const half = ctx.measureText(label.text).width / 2 + 5;
      // Une seconde ligne, plutôt que de perdre un nom.
      const row = [0, 1].find((r) => !placed.some((p) => p.row === r && x - half < p.to && x + half > p.from));
      if (row === undefined) continue;
      placed.push({ from: x - half, to: x + half, row });
      ctx.fillStyle = theme.dim;
      ctx.fillText(label.text, x, ground + 30 + row * 12);
    }
  }

  const wheelAngles = [0, 0];
  const telemetryBrake = () => (brake > 0.05 ? 0.4 : 0);
  const gasReady = () => motion.gas.active && motion.gas.net.n > 0;

  onMount(() => animate(canvas, draw));

  const bodyName = $derived(BODIES.find((item) => item.value === config.body)?.label ?? '');
</script>

<div class="stage" role="img" aria-label={t('car.aria', { body: bodyName })} data-testid="car-stage">
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  .stage { width: 100%; }
  canvas { display: block; width: 100%; height: var(--car-h, clamp(170px, 27vw, 310px)); }
</style>
