<script lang="ts">
  import { onMount } from 'svelte';
  import { BODIES, EXHAUST_LINES, TURBO_SCALE, type Body, type EngineConfig } from '../lib/config';
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
    const turboX = left - 0.07;
    const turbineY = engineY + 0.14;
    const compressorY = engineY + 0.14 + 0.13 * sizeScale;
    const wheelR = 0.062 * sizeScale;
    const airX = atRear ? 0.14 : behind ? shape.length * 0.44 : Math.min(left + blockWidth + 0.32, shape.length * 0.93);
    const airY = behind ? shape.height * 0.66 : engineY + blockHeight * 1.05;
    const coolerRight = behind ? (atRear ? left + blockWidth + 0.5 : left + blockWidth + 0.45) : shape.length * 0.965;
    const coolerLeft = coolerRight - 0.24;
    const coolerBottom = behind ? engineY + 0.18 : floor + 0.04;
    const coolerTop = coolerBottom + 0.26;
    const throttleX = left + blockWidth * 0.86;
    const throttleY = engineY + blockHeight * 0.92;
    const way: Record<string, [number, number][]> = {
      filter: [toCar(airX + 0.09, airY), toCar(airX, airY)],
      inlet: [toCar(airX, airY), toCar(left + blockWidth * 0.5, engineY + blockHeight + 0.06), toCar(turboX, compressorY + 0.05)],
      hot: [toCar(turboX - 0.02, compressorY - 0.05), toCar(turboX - 0.05, floor + 0.01), toCar(coolerLeft - 0.02, floor + 0.01), toCar(coolerLeft, coolerBottom + 0.08)],
      cooler: [toCar(coolerLeft, coolerBottom + 0.13), toCar(coolerRight, coolerBottom + 0.13)],
      cold: [toCar(coolerRight, coolerTop - 0.04), toCar(coolerRight - 0.12, coolerTop + 0.08), toCar(throttleX, throttleY)],
    };
    if (behind) way.hot = [toCar(turboX - 0.02, compressorY - 0.05), toCar(turboX, floor + 0.03), toCar(coolerLeft - 0.03, floor + 0.05), toCar(coolerLeft, coolerBottom + 0.08)];
    const partFor = (kind: string) => way[kind];

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

    // L'échangeur : des tranches à la température de l'air, du chaud (côté tube chaud) au froid.
    const [cx0, cy0] = toCar(coolerLeft, coolerTop);
    const coolerW = (coolerRight - coolerLeft) * scale;
    const coolerH = (coolerTop - coolerBottom) * scale;
    if (ready && !motion.reduced) {
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
    labels.push({ x: coolerRight - 0.12, text: t('gas.intercooler') });

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
    outlinePath(ctx, shape.outline, bodyX, bodyY);
    ctx.fillStyle = theme.surface;
    ctx.globalAlpha = 0.88;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.stroke();
    // Vitres : des arêtes nettes, un montant au milieu.
    ctx.beginPath();
    shape.glass.forEach(([gx, gy], i) => (i ? ctx.lineTo(bodyX(gx), bodyY(gy)) : ctx.moveTo(bodyX(gx), bodyY(gy))));
    ctx.closePath();
    ctx.fillStyle = theme.raised;
    ctx.fill();
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.stroke();
    const pillar = (shape.glass[1][0] + shape.glass[2][0]) / 2;
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
    const front = shape.outline.reduce((best, p) => (p[0] > best[0] ? p : best));
    ctx.fillStyle = mix(theme.text, theme.accent, 0.15);
    ctx.fillRect(bodyX(0.985) - 12, bodyY(front[1] + 0.12), 12, 4);
    ctx.fillStyle = theme.danger;
    ctx.fillRect(bodyX(0.003), bodyY(shape.outline[1][1] - 0.05), 6, 5 + 26 * telemetryBrake());

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
      const row = config.layout === 'inline' ? config.cylinders : Math.ceil(config.cylinders / 2);
      const perCell = config.cylinders / row;
      const order = firingOrder(config.cylinders);
      const cellWidth = (blockWidth * scale - 6) / row;
      for (let i = 0; i < config.cylinders; i++) {
        const cell = Math.floor(i / perCell);
        const phase = (motion.cycle + 1 - order.indexOf(i + 1) / config.cylinders) % 1;
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
      }
      labels.push({ x: engineX, text: t('car.engineLabel', { cylinders: config.cylinders, litres: fixed(litres, 1) }) });

      // Ligne d'échappement : du collecteur à la sortie, ses volumes suivent le réglage.
      const tail = 0.02;
      const line: [number, number][] = behind
        ? [toCar(Math.max(left, tail + 0.1), engineY + 0.1), toCar(Math.max(left - 0.12, tail + 0.05), floor), toCar(tail, floor + 0.02)]
        : [toCar(left, engineY + 0.1), toCar(left - 0.1, floor), toCar(rearX + 0.1, floor), toCar(rearX, floor + 0.14), toCar(tail + 0.2, floor + 0.12), toCar(tail, floor + 0.04)];
      const pipeWidth = Math.max(3, (config.exhaust === 'straight' ? 0.075 : 0.06) * scale);
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
