<script lang="ts">
  import { onMount } from 'svelte';
  import { TURBO_SCALE, isRotary, type EngineConfig } from '../lib/config';
  import { animate, fit, heatedPart, mix, pipeColor, readTheme, type Theme } from '../lib/canvas';
  import type { Telemetry } from '../lib/engine';
  import { fixed, speedLabel, t, whole } from '../lib/i18n.svelte';
  import { PLUME_EXIT_ROW, PLUME_PIPE_CELLS } from '../lib/fluid';
  import { P_ATM } from '../lib/fluidGas';
  import type { Motion } from '../lib/motion';

  interface Props {
    config: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    /** Le ralenti des animations : la légende le suit. */
    slow?: number;
  }
  let { config, telemetry, motion, slow = 12 }: Props = $props();

  let canvas: HTMLCanvasElement;

  /** Le trajet du gaz à l'écran. Calculé une fois par taille et par moteur : rien n'est alloué à chaque image. */
  interface Layout {
    width: number;
    height: number;
    net: unknown;
    engineLeft: number;
    engineRight: number;
    engineTop: number;
    engineBottom: number;
    tube: number;
    lineY: number;
    intakeY: number;
    /** Abscisse des bords de chaque cellule de la ligne (lineCount + 1 valeurs). */
    lineX: Float32Array;
    /** Hauteur à l'écran de chaque tronçon. */
    sectionTall: Float32Array;
    /** Points des collecteurs : x, y par cellule, à la suite. */
    runnerPoints: Float32Array[];
    runnerWidth: number;
    bankPoints: Float32Array[];
    /** Le tube déporté jusqu'à la turbine : x, y par cellule ; vide à la référence. */
    pipePoints: Float32Array;
    /** Où les tubes se rejoignent. */
    joinX: number;
    turbineX: number;
    turbineR: number;
    /** Abscisse de chaque cellule d'admission (intakeCount + 1). */
    intakeX: Float32Array;
    /** Ordonnée de chaque cellule d'admission : sur un turbo, l'air fait un détour par l'échangeur. */
    intakeYs: Float32Array;
    /** Où le tracé de pression place chaque cellule : tout le conduit à plat, dans l'ordre où l'air le parcourt. */
    traceX: Float32Array;
    /** Hauteur à l'écran de chaque tronçon de l'admission. */
    partTall: Float32Array;
    coolerX0: number;
    coolerX1: number;
    coolerY: number;
    coolerTall: number;
    compressorR: number;
    plenumLeft: number;
    plenumRight: number;
    plenumTall: number;
    throttleX: number;
    filterX: number;
    /** Lignes de base des tracés de pression, et leur amplitude. */
    lineTraceY: number;
    intakeTraceY: number;
    traceRange: number;
    exitX: number;
    plumeScale: number;
    /**
     * Deux turbos : une turbine au bout de chaque collecteur, côte à côte, chacune sous son compresseur ; le second chemin d'air,
     * dessiné comme le premier mais décalé. `turbineXs`, `turbineYs` : le centre de chaque turbine ; `intakeX2`, `intakeYs2` :
     * les cellules du second chemin d'air.
     */
    twin: boolean;
    turbineXs: Float32Array;
    turbineYs: Float32Array;
    intakeX2: Float32Array;
    intakeYs2: Float32Array;
    partTall2: Float32Array;
  }

  let layout: Layout | null = null;
  /** Pression moyenne de chaque cellule : l'onde, c'est ce qui s'en écarte. */
  let meanPressure = new Float32Array(0);
  let readoutAt = 0;
  let readoutLine = '';
  let readoutPlenum = '';
  let readoutCharge = '';
  let wallStyles: string[] = [];
  let densityStyles: string[] = [];
  let stylesFor: Theme | null = null;

  const clamp = (value: number, low: number, high: number) => (value < low ? low : value > high ? high : value);

  function refreshStyles(theme: Theme) {
    if (theme === stylesFor) return;
    stylesFor = theme;
    wallStyles = Array.from({ length: 16 }, (_, i) => pipeColor(theme, i / 15));
    densityStyles = Array.from({ length: 16 }, (_, i) => mix(theme.bg, theme.cool, 0.08 + (0.9 * i) / 15));
  }

  function buildLayout(width: number, height: number): Layout {
    const net = motion.gas.net;
    const narrow = width < 520;
    const engineLeft = 6;
    const engineWidth = clamp(width * 0.08, 28, 62);
    const engineRight = engineLeft + engineWidth;
    const tube = clamp(height * 0.048, 6, 14);
    const intakeY = height * 0.215;
    const lineY = height * 0.62;
    const lineTraceY = height * 0.9;
    const intakeTraceY = height * 0.075;
    const traceRange = height * 0.065;
    const plumeWidth = width * (narrow ? 0.2 : 0.235);
    const exitX = width - plumeWidth;
    const turboShare = net.compressorCell >= 0;

    // Collecteurs : un tube par cylindre, qui se rejoignent ; en V, un par rangée, puis la jonction.
    // Sur un turbo, le collecteur s'allonge : il faut de la place entre le papillon et le compresseur.
    // Deux turbos : ils sont au bout des collecteurs, et l'échangeur a besoin de place avant eux ; leurs tubes se rejoignent vite.
    const twinShare = narrow ? 0.46 : 0.42;
    const span = (exitX - engineRight) * (turboShare ? (net.hasTwin ? twinShare : net.banks === 2 ? (narrow ? 0.3 : 0.27) : narrow ? 0.5 : 0.45) : narrow ? 0.12 : 0.1);
    const collectorX = engineRight + span;
    const bankGap = net.banks === 2 ? height * 0.085 : 0;
    const joinX = net.banks === 2 ? collectorX + span * (net.hasTwin ? (narrow ? 0.55 : 0.42) : 0.75) : collectorX;
    const runnerPoints: Float32Array[] = [];
    const perBank = Math.ceil(net.runners / net.banks);
    const spreadBank = net.banks === 2 ? height * 0.06 : height * 0.1;
    for (let c = 0; c < net.runners; c++) {
      const bank = net.bank[c];
      // Un moteur en ligne à deux turbos est coupé en avant et arrière : la rangée se lit dans l'ordre des cylindres.
      const row = net.banks === 2 ? (net.frontRear ? c % perBank : Math.floor(c / 2)) : c;
      const place = perBank > 1 ? row / (perBank - 1) - 0.5 : 0;
      const centre = lineY + (net.banks === 2 ? (bank === 0 ? -bankGap : bankGap) : 0);
      const y0 = centre + place * 2 * spreadBank;
      const n = net.runnerCells[c];
      const points = new Float32Array((n + 1) * 2);
      for (let k = 0; k <= n; k++) {
        const t = k / n;
        const s = t * t * (3 - 2 * t);
        points[k * 2] = engineRight + (collectorX - engineRight) * t;
        points[k * 2 + 1] = y0 + (centre - y0) * s;
      }
      runnerPoints.push(points);
    }
    const bankPoints: Float32Array[] = [];
    if (net.banks === 2) {
      for (let b = 0; b < 2; b++) {
        const n = net.bankCells;
        const points = new Float32Array((n + 1) * 2);
        const centre = lineY + (b === 0 ? -bankGap : bankGap);
        for (let k = 0; k <= n; k++) {
          const t = k / n;
          const s = t * t * (3 - 2 * t);
          points[k * 2] = collectorX + (joinX - collectorX) * t;
          points[k * 2 + 1] = centre + (lineY - centre) * s;
        }
        bankPoints.push(points);
      }
    }

    // La ligne : les volumes un peu grossis, pour qu'on y voie l'écoulement.
    // Les roues suivent la taille choisie.
    const turbineR = turboShare ? clamp(height * 0.055, 8, 17) * (net.hasTurbine ? TURBO_SCALE[config.turboSize ?? 'medium'] : 1.1) : 0;
    const compressorR = turbineR * 1.15;
    // Un tube déporté prend sa place, à son échelle jusqu'à un quart de ce qu'il y a jusqu'à la sortie : la ligne se resserre d'autant.
    const pipeSpan = net.pipeCells > 0 ? (exitX - joinX) * 0.25 * Math.min(1, (net.pipeCells * net.cell) / 2.6) : 0;
    const pipePoints = new Float32Array(net.pipeCells > 0 ? (net.pipeCells + 1) * 2 : 0);
    for (let k = 0; k <= net.pipeCells && net.pipeCells > 0; k++) {
      pipePoints[k * 2] = joinX + (pipeSpan * k) / net.pipeCells;
      pipePoints[k * 2 + 1] = lineY;
    }
    // Deux turbos : chacun au bout de son collecteur, côte à côte, et la ligne part de la jonction.
    const twin = net.hasTwin;
    const turbineXs = new Float32Array(2);
    const turbineYs = new Float32Array(2);
    turbineXs[0] = collectorX + turbineR + 3;
    turbineXs[1] = turbineXs[0] + compressorR * (narrow ? 2.1 : 2.6);
    turbineYs[0] = lineY - bankGap;
    turbineYs[1] = lineY + bankGap;
    const turbineX = twin ? turbineXs[0] : joinX + pipeSpan + turbineR + 3;
    const lineStart = turboShare && net.hasTurbine && !twin ? turbineX + turbineR + 2 : joinX;
    let weight = 0;
    for (const section of net.sections) weight += section.length * (section.kind === 'tube' || section.kind === 'tail' ? 0.8 : 1.5);
    const lineX = new Float32Array(net.lineCount + 1);
    const sectionTall = new Float32Array(net.sections.length);
    let x = lineStart;
    const pxPerWeight = (exitX - lineStart) / weight;
    for (let s = 0; s < net.sections.length; s++) {
      const section = net.sections[s];
      const w = section.length * (section.kind === 'tube' || section.kind === 'tail' ? 0.8 : 1.5) * pxPerWeight;
      for (let k = 0; k < section.count; k++) lineX[section.first - net.lineFirst + k] = x + (w * k) / section.count;
      x += w;
      sectionTall[s] = Math.min(tube * Math.sqrt(section.area), height * 0.24);
    }
    lineX[net.lineCount] = x;

    // L'admission : l'air arrive par la droite, le compresseur est sur le même arbre que la turbine.
    const plenumLeft = engineRight + 6;
    const plenumRight = plenumLeft + clamp(width * 0.05, 18, 40);
    const throttleX = plenumRight + 5;
    const filterX = turboShare ? Math.min((twin ? turbineXs[1] : turbineX) + width * 0.17, exitX - 14) : Math.min(throttleX + width * 0.28, exitX - 14);
    const intakeX = new Float32Array(net.intakeCount + 1);
    const intakeYs = new Float32Array(net.intakeCount + 1).fill(intakeY);
    const traceX = new Float32Array(net.intakeCount + 1);
    const partTall = new Float32Array(net.intakeParts.length);
    const coolerX0 = throttleX + clamp(width * 0.045, 16, 44);
    const coolerX1 = turbineX - compressorR - clamp(width * 0.012, 4, 12);
    const coolerTall = clamp(height * 0.11, 14, 36);
    const coolerY = (intakeY + lineY) / 2 - (net.banks === 2 ? height * 0.045 : height * 0.02);
    for (let i = 0; i <= net.intakeCount; i++) traceX[i] = filterX + (throttleX - filterX) * (i / net.intakeCount);
    const intakeX2 = new Float32Array(net.intake2Count + 1);
    const intakeYs2 = new Float32Array(net.intake2Count + 1).fill(intakeY);
    const partTall2 = new Float32Array(net.intakeParts2.length);
    if (turboShare) {
      // Le chemin de l'air : la boîte à air à droite, le compresseur, puis un détour par l'échangeur, entre les deux rangées, avant le papillon.
      const filterRight = Math.min(filterX + clamp(width * 0.045, 12, 40), exitX - 3);
      // Deux turbos : le premier chemin arrive d'un peu plus haut et passe au-dessus du second compresseur ; chacun traverse sa moitié de l'échangeur.
      const placeChain = (parts: typeof net.intakeParts, first: number, xs: Float32Array, ys: Float32Array, talls: Float32Array, compressorX: number, rowY: number, coolerAt: number) => {
        const lift = rowY === intakeY ? [] : ([[compressorX + compressorR * 1.6, rowY], [compressorX + compressorR * 0.3, intakeY - compressorR * 0.9]] as [number, number][]);
        const way: Record<string, [number, number][]> = {
          filter: [[filterRight, rowY], [filterX, rowY]],
          inlet: lift.length > 0 ? [[filterX, rowY], ...lift] : [[filterX, rowY], [compressorX + compressorR, rowY]],
          hot: [[compressorX - compressorR * 0.9, intakeY + compressorR * 0.4], [coolerX1 + 2, intakeY + compressorR * 0.9], [coolerX1 + 2, coolerAt - coolerTall * 0.25], [coolerX1, coolerAt]],
          cooler: [[coolerX1, coolerAt], [coolerX0, coolerAt]],
          cold: [[coolerX0, coolerAt], [throttleX + 1, coolerAt], [throttleX + 1, intakeY + tube * 0.9], [throttleX, intakeY]],
          // Monté soufflé : le carburateur attend juste avant le papillon.
          venturi: [[throttleX, intakeY], [throttleX, intakeY]],
          duct: [[throttleX, intakeY], [throttleX, intakeY]],
        };
        parts.forEach((part, p) => {
          const points = way[part.kind];
          const lengths = points.slice(1).map((point, k) => Math.hypot(point[0] - points[k][0], point[1] - points[k][1]));
          const total = lengths.reduce((a, b) => a + b, 0);
          for (let i = 0; i <= part.count; i++) {
            let left = (total * i) / part.count;
            let k = 0;
            while (k < lengths.length - 1 && left > lengths[k]) left -= lengths[k++];
            const f = lengths[k] > 0 ? Math.min(left / lengths[k], 1) : 0;
            const cell = part.first - first + i;
            xs[cell] = points[k][0] + (points[k + 1][0] - points[k][0]) * f;
            ys[cell] = points[k][1] + (points[k + 1][1] - points[k][1]) * f;
          }
          talls[p] = part.kind === 'filter' ? tube * (twin ? 1.9 : 2.6) : part.kind === 'cooler' ? coolerTall * (twin ? 0.45 : 1) : tube * 0.95;
        });
      };
      if (twin) {
        placeChain(net.intakeParts, net.intakeFirst, intakeX, intakeYs, partTall, turbineXs[0], intakeY - compressorR * 1.7, coolerY - coolerTall * 0.25);
        placeChain(net.intakeParts2, net.intake2First, intakeX2, intakeYs2, partTall2, turbineXs[1], intakeY, coolerY + coolerTall * 0.25);
      } else {
        placeChain(net.intakeParts, net.intakeFirst, intakeX, intakeYs, partTall, turbineX, intakeY, coolerY);
      }
    } else {
      for (let i = 0; i <= net.intakeCount; i++) intakeX[i] = traceX[i];
      partTall[0] = tube * 1.1;
      // Le col du venturi, et sa pipe : la largeur dessinée suit la section.
      net.intakeParts.forEach((part, p) => {
        if (p > 0) partTall[p] = tube * 1.1 * Math.min(Math.sqrt(part.area), 1.4);
      });
    }

    return {
      width, height, net: net.sections, engineLeft, engineRight,
      engineTop: intakeY - height * 0.1,
      engineBottom: lineY + height * 0.17,
      tube, lineY, intakeY, lineX, sectionTall, runnerPoints,
      runnerWidth: Math.max(2, tube * Math.sqrt(net.area[0] / net.area[net.lineFirst])),
      bankPoints, pipePoints, joinX, turbineX, turbineR, intakeX, intakeYs, traceX, partTall, coolerX0, coolerX1, coolerY, coolerTall, compressorR, plenumLeft, plenumRight,
      plenumTall: clamp(height * 0.15, 22, 44),
      throttleX, filterX, lineTraceY, intakeTraceY, traceRange, exitX,
      plumeScale: plumeWidth / (motion.gas.plume.nx - PLUME_PIPE_CELLS),
      twin, turbineXs, turbineYs, intakeX2, intakeYs2, partTall2,
    };
  }

  /** Pression à l'écran : la petite onde reste visible à côté de la bouffée de plusieurs bars. */
  function traceOffset(pressure: number, range: number): number {
    const bar = (pressure - P_ATM) / 1e5;
    const size = bar < 0 ? -bar : bar;
    const squeezed = Math.log1p(size / 0.03) / Math.log1p(2 / 0.03);
    return (bar < 0 ? squeezed : -squeezed) * range;
  }

  function strokeSegments(ctx: CanvasRenderingContext2D, theme: Theme, points: Float32Array, first: number, count: number, width: number) {
    const net = motion.gas.net;
    const painter = motion.painter;
    ctx.lineWidth = width;
    ctx.lineCap = 'butt';
    for (let k = 0; k < count; k++) {
      const cell = first + k;
      ctx.strokeStyle = painter.gasStyle(theme, net.temp[cell], net.y[cell]);
      ctx.beginPath();
      ctx.moveTo(points[k * 2], points[k * 2 + 1]);
      ctx.lineTo(points[k * 2 + 2] + 0.4, points[k * 2 + 3]);
      ctx.stroke();
    }
  }

  function drawEngine(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    const painter = motion.painter;
    const top = view.engineTop;
    const height = view.engineBottom - top;
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(view.engineLeft, top, view.engineRight - view.engineLeft, height, 4);
    ctx.fill();
    ctx.stroke();
    // Un cercle par cylindre, vu d'en haut : on y lit la température réelle de ce qu'il contient.
    const columns = net.banks;
    const rows = Math.ceil(net.cylinders / columns);
    const cellW = (view.engineRight - view.engineLeft - 6) / columns;
    const cellH = (height - 6) / rows;
    const radius = Math.max(2, Math.min(cellW, cellH) * 0.42);
    for (let c = 0; c < net.cylinders; c++) {
      const column = columns === 2 ? c % 2 : 0;
      const row = columns === 2 ? Math.floor(c / 2) : c;
      ctx.fillStyle = painter.gasStyle(theme, net.cylT[c] < 800 ? net.cylT[c] : 800 + (net.cylT[c] - 800) * 0.33, net.cylY[c] / Math.max(net.cylM[c], 1e-9));
      ctx.beginPath();
      ctx.arc(view.engineLeft + 3 + (column + 0.5) * cellW, top + 3 + (row + 0.5) * cellH, radius, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  function drawRunners(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    ctx.lineJoin = 'round';
    for (let c = 0; c < net.runners; c++) {
      const points = view.runnerPoints[c];
      const n = net.runnerCells[c];
      // Le dessous, en acier, puis le gaz par-dessus.
      ctx.strokeStyle = wallStyles[Math.round(clamp(motion.heat * 15, 0, 15))];
      ctx.lineWidth = view.runnerWidth + 1.5;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(points[0], points[1]);
      for (let k = 1; k <= n; k++) ctx.lineTo(points[k * 2], points[k * 2 + 1]);
      ctx.stroke();
    }
    let first = 0;
    for (let c = 0; c < net.runners; c++) {
      strokeSegments(ctx, theme, view.runnerPoints[c], first, net.runnerCells[c], view.runnerWidth);
      first += net.runnerCells[c];
    }
    for (let b = 0; b < view.bankPoints.length; b++) {
      const points = view.bankPoints[b];
      const n = net.bankCells;
      ctx.strokeStyle = wallStyles[Math.round(clamp(motion.heat * 13, 0, 15))];
      ctx.lineWidth = view.tube + 1.5;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(points[0], points[1]);
      for (let k = 1; k <= n; k++) ctx.lineTo(points[k * 2], points[k * 2 + 1]);
      ctx.stroke();
      strokeSegments(ctx, theme, points, net.bankFirst[b], n, view.tube);
    }
    if (net.pipeCells > 0) {
      const points = view.pipePoints;
      ctx.strokeStyle = wallStyles[Math.round(clamp(motion.heat * 13, 0, 15))];
      ctx.lineWidth = view.tube + 1.5;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(points[0], points[1]);
      ctx.lineTo(points[net.pipeCells * 2], points[net.pipeCells * 2 + 1]);
      ctx.stroke();
      strokeSegments(ctx, theme, points, net.pipeFirst, net.pipeCells, view.tube);
    }
  }

  function label(ctx: CanvasRenderingContext2D, theme: Theme, text: string, x: number, y: number) {
    ctx.fillStyle = theme.dim;
    ctx.fillText(text, x, y);
  }

  function drawLine(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const gas = motion.gas;
    const net = gas.net;
    const painter = motion.painter;
    const y = view.lineY;
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    let chamberIndex = 0;
    for (let s = 0; s < net.sections.length; s++) {
      const section = net.sections[s];
      const offset = section.first - net.lineFirst;
      const x0 = view.lineX[offset];
      const x1 = view.lineX[offset + section.count];
      const tall = view.sectionTall[s];
      const top = y - tall / 2;
      if (gas.chamberOf[s]) {
        // Un volume : l'écoulement à deux dimensions, calculé à part et piloté par la ligne.
        ctx.fillStyle = theme.bg;
        ctx.fillRect(x0, top, x1 - x0, tall);
        painter.drawChamber(ctx, theme, chamberIndex, x0, top, x1 - x0, tall);
        const chamber = gas.chambers[chamberIndex];
        const field = chamber.field;
        ctx.strokeStyle = theme.metal;
        ctx.lineWidth = 1;
        for (const baffle of chamber.baffles) {
          const bx = x0 + ((baffle.column + 0.5) / field.nx) * (x1 - x0);
          const openTop = top + (1 - (baffle.to + 1) / field.ny) * tall;
          const openBottom = top + (1 - baffle.from / field.ny) * tall;
          ctx.beginPath();
          ctx.moveTo(bx, top);
          ctx.lineTo(bx, openTop);
          ctx.moveTo(bx, openBottom);
          ctx.lineTo(bx, top + tall);
          ctx.stroke();
        }
        if (section.kind === 'cat') {
          // Le nid d'abeille : des canaux droits, là où l'écoulement cesse de tourner.
          const mx0 = x0 + (chamber.monolithFrom / field.nx) * (x1 - x0);
          const mx1 = x0 + (chamber.monolithTo / field.nx) * (x1 - x0);
          ctx.strokeStyle = theme.metal;
          ctx.globalAlpha = 0.55;
          ctx.lineWidth = 0.75;
          const channels = Math.max(3, Math.floor(tall / 4));
          ctx.beginPath();
          for (let c = 1; c < channels; c++) {
            const cy = top + (tall * c) / channels;
            ctx.moveTo(mx0, cy);
            ctx.lineTo(mx1, cy);
          }
          ctx.stroke();
          ctx.globalAlpha = 1;
        }
        ctx.strokeStyle = wallStyles[Math.round(clamp(motion.heat * 15 * Math.exp(-section.from / 2.6), 0, 15))];
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.roundRect(x0, top, x1 - x0, tall, 3);
        ctx.stroke();
        if (x1 - x0 > (net.compressorCell >= 0 ? 34 : 46)) {
          const name = t(section.kind === 'cat' ? 'part.cat' : section.kind === 'res' ? 'part.res' : 'part.muffler');
          label(ctx, theme, name, (x0 + x1) / 2, top - 4);
        }
        chamberIndex++;
      } else {
        // Un tube : une température par cellule, c'est le calcul à une dimension tel quel.
        for (let k = 0; k < section.count; k++) {
          const cell = section.first + k;
          const cellX = view.lineX[offset + k];
          const cellW = view.lineX[offset + k + 1] - cellX + 0.5;
          ctx.fillStyle = painter.gasStyle(theme, net.temp[cell], net.y[cell]);
          ctx.fillRect(cellX, top, cellW, tall);
          // Par-dessus, l'onde : plus clair en surpression, plus sombre en dépression.
          const wave = (net.p[cell] - meanPressure[offset + k]) / 1e5;
          const size = wave < 0 ? -wave : wave;
          if (size > 0.004) {
            const alpha = Math.sqrt(size) * 0.75;
            ctx.globalAlpha = alpha > 0.6 ? 0.6 : alpha;
            ctx.fillStyle = wave > 0 ? '#ffffff' : '#000000';
            ctx.fillRect(cellX, top, cellW, tall);
            ctx.globalAlpha = 1;
          }
        }
        ctx.strokeStyle = wallStyles[Math.round(clamp(motion.heat * 15 * Math.exp(-section.from / 2.6), 0, 15))];
        ctx.lineWidth = 1.25;
        ctx.beginPath();
        ctx.moveTo(x0, top);
        ctx.lineTo(x1, top);
        ctx.moveTo(x0, top + tall);
        ctx.lineTo(x1, top + tall);
        ctx.stroke();
      }
    }
    // La sortie : un petit bord, et rien après.
    const lastSection = net.sections.length - 1;
    const tall = view.sectionTall[lastSection];
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(view.exitX, y - tall / 2 - 1);
    ctx.lineTo(view.exitX, y - tall / 2 + 1.5);
    ctx.moveTo(view.exitX, y + tall / 2 - 1.5);
    ctx.lineTo(view.exitX, y + tall / 2 + 1);
    ctx.stroke();
  }

  function drawParticles(ctx: CanvasRenderingContext2D, view: Layout) {
    const gas = motion.gas;
    const net = gas.net;
    const { pSection, pX, pY, pBurnt } = gas;
    const cell = net.cell;
    const hot = '#ffeed6';
    const cold = '#cfe6ff';
    const y = view.lineY;
    ctx.globalAlpha = 0.85;
    for (let pass = 0; pass < 2; pass++) {
      ctx.fillStyle = pass === 0 ? hot : cold;
      for (let k = 0; k < gas.count; k++) {
        const s = pSection[k];
        if (s < 0 || s >= 100) continue;
        if ((pBurnt[k] > 0.5) !== (pass === 0)) continue;
        const section = net.sections[s];
        if (!section) continue;
        const offset = section.first - net.lineFirst;
        const x0 = view.lineX[offset];
        const x1 = view.lineX[offset + section.count];
        const tall = view.sectionTall[s];
        const chamber = gas.chamberOf[s];
        if (chamber) {
          const field = chamber.field;
          const px = x0 + (pX[k] / field.nx) * (x1 - x0);
          const py = y - tall / 2 + (1 - (pY[k] + 0.5) / field.ny) * tall;
          ctx.fillRect(px - 0.75, py - 0.75, 1.5, 1.5);
        } else {
          const px = x0 + (pX[k] / (section.count * cell)) * (x1 - x0);
          const py = y + pY[k] * (tall / 2 - 1);
          ctx.fillRect(px - 0.8, py - 0.8, 1.6, 1.6);
        }
      }
    }
    ctx.globalAlpha = 1;
  }

  /** Une volute : un cercle dont le rayon grandit sur un tour, comme le carter d'une roue. */
  function voluteOutline(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, start: number) {
    ctx.beginPath();
    for (let k = 0; k <= 26; k++) {
      const a = start + (k / 26) * Math.PI * 2;
      const radius = r * (0.92 + 0.34 * (k / 26));
      if (k === 0) ctx.moveTo(x + Math.cos(a) * radius, y + Math.sin(a) * radius);
      else ctx.lineTo(x + Math.cos(a) * radius, y + Math.sin(a) * radius);
    }
    ctx.closePath();
  }

  function drawWheel(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, r: number, angle: number, blades: number, sweep: number) {
    // Trop vite pour l'œil : on ne voit plus qu'un disque, comme sur la vraie roue.
    if (motion.turbineBlur > 0.02) {
      ctx.globalAlpha = 0.35 * motion.turbineBlur;
      ctx.fillStyle = theme.metal;
      ctx.beginPath();
      ctx.arc(x, y, r * 0.84, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1 - 0.8 * motion.turbineBlur;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.1;
    ctx.beginPath();
    for (let i = 0; i < blades; i++) {
      const a = angle + (i / blades) * Math.PI * 2;
      ctx.moveTo(x + Math.cos(a) * r * 0.15, y + Math.sin(a) * r * 0.15);
      ctx.quadraticCurveTo(x + Math.cos(a + sweep * 0.3) * r * 0.5, y + Math.sin(a + sweep * 0.3) * r * 0.5, x + Math.cos(a + sweep) * r * 0.84, y + Math.sin(a + sweep) * r * 0.84);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  /** Le compresseur mécanique : sur le chemin de l'air, sa courroie qui descend vers le moteur. Les rotors ou la roue suivent le vilebrequin. */
  function drawCharger(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    const x = view.turbineX;
    const y = view.intakeY;
    const r = view.compressorR;
    // La courroie, jusqu'à la poulie du vilebrequin, derrière le carter.
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 2;
    ctx.setLineDash([5, 4]);
    ctx.lineDashOffset = -motion.chargerAngle * 4;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x, view.lineY - 6, view.engineRight, view.lineY - 6);
    ctx.stroke();
    ctx.setLineDash([]);

    const outCell = net.compressorCell;
    ctx.fillStyle = motion.painter.airStyle(theme, net.temp[outCell], net.rho[outCell]);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    if (config.superchargerKind === 'centrifugal') {
      voluteOutline(ctx, x, y, r, -0.6);
      ctx.fill();
      ctx.stroke();
      drawWheel(ctx, theme, x, y, r, motion.chargerAngle, 11, -0.55);
    } else {
      ctx.beginPath();
      ctx.roundRect(x - r * 1.5, y - r, r * 3, r * 2, 3);
      ctx.fill();
      ctx.stroke();
      const lobes = config.superchargerKind === 'twin_screw' ? 5 : 3;
      lobedRotor(ctx, theme, x - r * 0.62, y, r * 0.72, 3, motion.chargerAngle);
      lobedRotor(ctx, theme, x + r * 0.62, y, r * 0.72, lobes, -motion.chargerAngle * (3 / lobes));
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1;
    }
    if (view.width > 420) {
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      label(ctx, theme, t('gas.compressor'), x, y - r * 1.3 - 4);
    }
  }

  function lobedRotor(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, radius: number, lobes: number, angle: number) {
    ctx.fillStyle = mix(theme.raised, theme.metal, 0.4);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let k = 0; k <= 36; k++) {
      const a = (k / 36) * Math.PI * 2;
      const rr = radius * (0.7 + 0.3 * Math.cos(lobes * a));
      const px = x + Math.cos(a + angle) * rr;
      const py = y + Math.sin(a + angle) * rr;
      if (k === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }

  /** Deux turbos : chacun son arbre, sa soupape, sa turbine au bout de son collecteur, son compresseur sur son chemin d'air. */
  function drawTwinTurbos(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    const r = view.turbineR;
    const compressorR = view.compressorR;
    for (let b = 0; b < 2; b++) {
      const x = view.turbineXs[b];
      const ty = view.turbineYs[b];
      const angle = b === 0 ? motion.turbineAngle : motion.turbine2Angle;
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.moveTo(x, ty - r);
      ctx.lineTo(x, view.intakeY + compressorR);
      ctx.stroke();
      // La soupape : un contournement sous la volute, son clapet levé selon l'ouverture réelle. Des aubes n'en ont pas.
      if (!config.variableGeometry) {
        const gate = clamp(b === 0 ? motion.wastegate : motion.wastegate2, 0, 1);
        const wgY = ty + r * 1.7;
        ctx.strokeStyle = gate > 0.02 ? mix(theme.metal, theme.accent, gate) : theme.metal;
        ctx.lineWidth = 1.5 + 2 * gate;
        ctx.beginPath();
        ctx.moveTo(x - r - 2, ty + 2);
        ctx.bezierCurveTo(x - r, wgY, x + r, wgY, x + r + 2, ty + 2);
        ctx.stroke();
      }
      ctx.fillStyle = heatedPart(theme.raised, motion.heat);
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.5;
      voluteOutline(ctx, x, ty, r, 2.2);
      ctx.fill();
      ctx.stroke();
      drawWheel(ctx, theme, x, ty, r, angle, 9, 0.5);
      const outCell = b === 0 ? net.compressorCell : net.compressor2Cell;
      ctx.fillStyle = motion.painter.airStyle(theme, net.temp[outCell], net.rho[outCell]);
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.5;
      voluteOutline(ctx, x, view.intakeY, compressorR, -0.6);
      ctx.fill();
      ctx.stroke();
      drawWheel(ctx, theme, x, view.intakeY, compressorR, -angle, 11, -0.55);
    }
    if (view.width > 420) {
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      label(ctx, theme, t('gas.twinTurbines'), (view.turbineXs[0] + view.turbineXs[1]) / 2, view.turbineYs[1] + r * 2.4 + 10);
    }
  }

  function drawTurbo(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    if (net.compressorCell < 0) return;
    const x = view.turbineX;
    const r = view.turbineR;
    const compressorR = view.compressorR;
    if (!net.hasTurbine) {
      drawCharger(ctx, theme, view);
      return;
    }
    if (view.twin) {
      drawTwinTurbos(ctx, theme, view);
      return;
    }
    // L'arbre : turbine et compresseur tournent ensemble, dans leur palier.
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(x, view.lineY - r);
    ctx.lineTo(x, view.intakeY + compressorR);
    ctx.stroke();
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    const bearingY = (view.lineY + view.intakeY) / 2;
    ctx.beginPath();
    ctx.roundRect(x - r * 0.42, bearingY - r * 0.55, r * 0.84, r * 1.1, 2);
    ctx.fill();
    ctx.stroke();

    // La wastegate : un tube qui contourne la turbine, et son clapet sur le trajet, levé selon son ouverture réelle.
    const gate = clamp(motion.wastegate, 0, 1);
    const wgY = view.lineY + r * 1.9;
    ctx.strokeStyle = gate > 0.02 ? mix(theme.metal, theme.accent, gate) : theme.metal;
    ctx.lineWidth = 1.5 + 2 * gate;
    ctx.beginPath();
    ctx.moveTo(x - r - 2, view.lineY + 2);
    ctx.bezierCurveTo(x - r, wgY, x + r, wgY, x + r + 2, view.lineY + 2);
    ctx.stroke();
    const flapX = x;
    const flapY = view.lineY + r * 1.45;
    const lift = gate * 1.05;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.moveTo(flapX - Math.cos(lift) * r * 0.42, flapY - Math.sin(lift) * r * 0.42);
    ctx.lineTo(flapX + Math.cos(lift) * r * 0.42, flapY + Math.sin(lift) * r * 0.42);
    ctx.stroke();

    // La turbine dans sa volute, qui rougit avec la ligne.
    ctx.fillStyle = heatedPart(theme.raised, motion.heat);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    voluteOutline(ctx, x, view.lineY, r, 2.2);
    ctx.fill();
    ctx.stroke();
    drawWheel(ctx, theme, x, view.lineY, r, motion.turbineAngle, 9, 0.5);

    // Le compresseur dans la sienne, de la couleur de l'air qui en sort.
    const outCell = net.compressorCell;
    ctx.fillStyle = motion.painter.airStyle(theme, net.temp[outCell], net.rho[outCell]);
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    voluteOutline(ctx, x, view.intakeY, compressorR, -0.6);
    ctx.fill();
    ctx.stroke();
    drawWheel(ctx, theme, x, view.intakeY, compressorR, -motion.turbineAngle, 11, -0.55);

    // L'anti-lag : une vanne d'air depuis le tube froid jusqu'au collecteur. La flamme, elle, se voit à la couleur du gaz.
    if (config.antiLag === 'air' && config.fuel === 'petrol') {
      const from = net.releaseCell - net.intakeFirst;
      const flowing = clamp(motion.antiLagAirFlow / 0.03, 0, 1);
      ctx.strokeStyle = flowing > 0.02 ? mix(theme.metal, theme.accent, flowing) : theme.metal;
      ctx.lineWidth = 1.5 + 2 * flowing;
      ctx.setLineDash(flowing > 0.02 ? [] : [4, 4]);
      ctx.beginPath();
      ctx.moveTo(view.intakeX[from], view.intakeYs[from]);
      ctx.bezierCurveTo(view.intakeX[from], (view.intakeYs[from] + view.lineY) / 2, x - r * 2.2, view.lineY - r * 2, x - r - 1, view.lineY - r * 0.4);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    if (view.width > 420) {
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      label(ctx, theme, t('gas.turbine'), x, wgY + 12);
      label(ctx, theme, t('gas.compressor'), x, view.intakeY - compressorR * 1.3 - 4);
      if (view.width > 620) {
        ctx.textAlign = 'left';
        label(ctx, theme, t('gas.wastegate'), x + r + 6, wgY + 2);
        ctx.textAlign = 'center';
      }
    }
  }

  function drawIntake(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const gas = motion.gas;
    const net = gas.net;
    const y = view.intakeY;
    const tall = view.tube * 1.1;
    const top = y - tall / 2;
    const painter = motion.painter;
    const turbo = net.compressorCell >= 0;
    // La densité de l'air : creux derrière un papillon fermé, plein sous la pression du turbo.
    const densityLevel = (rho: number) => {
      const level = (rho / 2.6) * 15;
      return level < 0 ? 0 : level > 15 ? 15 : level | 0;
    };
    if (turbo) {
      // Chaque tronçon a sa largeur : on y lit la température réelle, de la boîte à air jusqu'au papillon.
      ctx.lineCap = 'butt';
      for (let p = 0; p < net.intakeParts.length; p++) {
        const part = net.intakeParts[p];
        ctx.lineWidth = view.partTall[p];
        for (let k = 0; k < part.count; k++) {
          const cell = part.first + k;
          const i = cell - net.intakeFirst;
          ctx.strokeStyle = painter.airStyle(theme, net.temp[cell], net.rho[cell]);
          ctx.beginPath();
          ctx.moveTo(view.intakeX[i], view.intakeYs[i]);
          ctx.lineTo(view.intakeX[i + 1], view.intakeYs[i + 1]);
          ctx.stroke();
        }
      }
      for (let p = 0; p < net.intakeParts2.length; p++) {
        const part = net.intakeParts2[p];
        ctx.lineWidth = view.partTall2[p];
        for (let k = 0; k < part.count; k++) {
          const cell = part.first + k;
          const i = cell - net.intake2First;
          ctx.strokeStyle = painter.airStyle(theme, net.temp[cell], net.rho[cell]);
          ctx.beginPath();
          ctx.moveTo(view.intakeX2[i], view.intakeYs2[i]);
          ctx.lineTo(view.intakeX2[i + 1], view.intakeYs2[i + 1]);
          ctx.stroke();
        }
      }
      drawIntakeWalls(ctx, theme, view);
    } else {
      for (let i = 0; i < net.intakeCount; i++) {
        const cell = net.intakeFirst + i;
        ctx.fillStyle = densityStyles[densityLevel(net.rho[cell])];
        const x0 = view.intakeX[i];
        const x1 = view.intakeX[i + 1];
        if (x0 === x1) continue;
        ctx.fillRect(Math.min(x0, x1), top, Math.abs(x1 - x0) + 0.5, tall);
      }
      // Le col : les parois se resserrent, le gaz passe au milieu.
      for (let p = 0; p < net.intakeParts.length; p++) {
        const part = net.intakeParts[p];
        if (part.kind !== 'venturi') continue;
        const i0 = part.first - net.intakeFirst;
        const x0 = Math.min(view.intakeX[i0], view.intakeX[i0 + part.count]);
        const x1 = Math.max(view.intakeX[i0], view.intakeX[i0 + part.count]);
        const pinch = Math.max(3, (tall - view.partTall[p]) / 2);
        ctx.fillStyle = theme.text;
        ctx.beginPath();
        ctx.moveTo(x0 - 5, top - 1);
        ctx.lineTo((x0 + x1) / 2, top + pinch);
        ctx.lineTo(x1 + 5, top - 1);
        ctx.moveTo(x0 - 5, top + tall + 1);
        ctx.lineTo((x0 + x1) / 2, top + tall - pinch);
        ctx.lineTo(x1 + 5, top + tall + 1);
        ctx.fill();
        label(ctx, theme, t('engine.part.carburetor'), (x0 + x1) / 2, top - 6);
      }
      // Parois du conduit.
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 1.25;
      ctx.beginPath();
      ctx.moveTo(view.intakeX[0], top);
      ctx.lineTo(view.intakeX[net.intakeCount], top);
      ctx.moveTo(view.intakeX[0], top + tall);
      ctx.lineTo(view.intakeX[net.intakeCount], top + tall);
      ctx.stroke();

      // Filtre à air : là où entre l'air frais.
      const filterW = clamp(view.width * 0.022, 6, 16);
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 1.25;
      ctx.beginPath();
      ctx.roundRect(view.filterX, y - tall * 1.2, filterW, tall * 2.4, 2);
      ctx.fill();
      ctx.stroke();
      ctx.globalAlpha = 0.6;
      ctx.beginPath();
      for (let i = 1; i < 4; i++) {
        const px = view.filterX + (filterW * i) / 4;
        ctx.moveTo(px, y - tall * 1.2 + 2);
        ctx.lineTo(px, y + tall * 1.2 - 2);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    // Plénum et papillon.
    const plenumTop = y - view.plenumTall / 2;
    const plenumRho = net.zM[net.zonePlenum] / net.zV[net.zonePlenum];
    ctx.fillStyle = turbo ? painter.airStyle(theme, net.zT[net.zonePlenum], plenumRho) : densityStyles[densityLevel(plenumRho)];
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(view.plenumLeft, plenumTop, view.plenumRight - view.plenumLeft, view.plenumTall, 3);
    ctx.fill();
    ctx.stroke();
    const share = net.throttleShare;
    const tilt = (1 - clamp(share * 2.2, 0, 1)) * 1.25;
    const halfLength = tall * 0.55;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(view.throttleX - Math.cos(tilt) * halfLength, y - Math.sin(tilt) * halfLength);
    ctx.lineTo(view.throttleX + Math.cos(tilt) * halfLength, y + Math.sin(tilt) * halfLength);
    ctx.stroke();
    // Le filet d'air qui passe derrière le papillon : presque rien, mais au son.
    if (net.throttleFlow > 0.0005) {
      const length = clamp(Math.sqrt(net.throttleFlow) * 40, 2, view.plenumRight - view.plenumLeft);
      ctx.strokeStyle = theme.cool;
      ctx.lineWidth = 1;
      ctx.globalAlpha = 0.7;
      ctx.beginPath();
      ctx.moveTo(view.throttleX - 2, y - 2);
      ctx.lineTo(view.throttleX - 2 - length, y - 4);
      ctx.moveTo(view.throttleX - 2, y + 2);
      ctx.lineTo(view.throttleX - 2 - length, y + 4);
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
    // Un tube par cylindre : il s'éclaire pendant que le cylindre aspire.
    ctx.lineWidth = 1.5;
    for (let c = 0; c < net.cylinders; c++) {
      const place = net.cylinders > 1 ? c / (net.cylinders - 1) - 0.5 : 0;
      const flow = net.cylIntake[c];
      ctx.strokeStyle = flow > 0.002 ? theme.cool : theme.metal;
      ctx.globalAlpha = flow > 0.002 ? clamp(0.5 + flow * 20, 0.5, 1) : 0.5;
      ctx.beginPath();
      ctx.moveTo(view.plenumLeft, y + place * view.plenumTall * 0.8);
      ctx.lineTo(view.engineRight, y + place * view.plenumTall * 0.8);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // L'air lui-même : mêmes particules que sur la ligne, à la vitesse réelle du conduit.
    ctx.fillStyle = theme.text;
    const cell = net.cell;
    const { pSection, pX, pY } = gas;
    for (let k = 0; k < gas.count; k++) {
      const s = pSection[k];
      if (s < 100) continue;
      const duct = gas.ducts[s - 100];
      if (!duct) continue;
      // Le long du trajet, cellule par cellule : il n'est pas droit sur un turbo.
      const along = duct.first - net.intakeFirst + clamp(pX[k] / cell, 0, duct.count - 0.001);
      const i0 = along | 0;
      const f = along - i0;
      const px = view.intakeX[i0] + (view.intakeX[i0 + 1] - view.intakeX[i0]) * f;
      const across = turbo ? 3 : tall / 2 - 1;
      const py = view.intakeYs[i0] + (view.intakeYs[i0 + 1] - view.intakeYs[i0]) * f + pY[k] * across;
      ctx.globalAlpha = 0.75;
      ctx.fillRect(px - 0.7, py - 0.7, 1.4, 1.4);
    }
    ctx.globalAlpha = 1;

    // Décharge du turbo : le clapet se lève selon son ouverture réelle, et l'air s'échappe en éventail. Sans soupape (flutter), rien à dessiner.
    if (net.releaseCell >= 0 && config.turboRelease !== 'flutter') {
      const i = net.releaseCell - net.intakeFirst;
      const vx = view.intakeX[i] + view.tube * 0.5;
      const vy = view.intakeYs[i];
      const open = clamp(motion.blowoff, 0, 1);
      const pipe = view.tube * 0.95;
      // Elle part du tube froid, vers l'échangeur : son corps, puis son clapet qui s'écarte selon l'ouverture réelle.
      ctx.fillStyle = theme.raised;
      ctx.strokeStyle = theme.text;
      ctx.lineWidth = 1.25;
      ctx.beginPath();
      ctx.roundRect(vx, vy - 4.5, pipe, 9, 2);
      ctx.fill();
      ctx.stroke();
      ctx.strokeStyle = open > 0.05 ? theme.accent : theme.text;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(vx + pipe + 1 + open * 4, vy - 4.5);
      ctx.lineTo(vx + pipe + 1 + open * 4, vy + 4.5);
      ctx.stroke();
      if (net.releaseFlow > 0.002 || open > 0.03) {
        const strength = clamp(Math.max(net.releaseFlow * 6, open * 0.6), 0, 1);
        ctx.strokeStyle = theme.cool;
        ctx.lineWidth = 1.25;
        for (let k = 0; k < 5; k++) {
          const a = (k - 2) * 0.32;
          const phase = (motion.turns * 22 + k * 0.37) % 1;
          const r0 = 3 + phase * 16 * strength;
          const r1 = r0 + 4 + 6 * strength;
          ctx.globalAlpha = (1 - phase) * clamp(strength + 0.2, 0, 1);
          ctx.beginPath();
          ctx.moveTo(vx + pipe + 3 + Math.cos(a) * r0, vy + Math.sin(a) * r0);
          ctx.lineTo(vx + pipe + 3 + Math.cos(a) * r1, vy + Math.sin(a) * r1);
          ctx.stroke();
        }
        ctx.globalAlpha = 1;
      }
    }

    if (view.width > 420) {
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      label(ctx, theme, t('gas.throttle'), view.throttleX + 18, y - tall / 2 - 5);
      if (!turbo) label(ctx, theme, t('gas.filter'), view.filterX + clamp(view.width * 0.022, 6, 16) / 2, y + tall * 1.2 + 13);
    }
  }

  /** Les pièces de la chaîne du turbo, par-dessus l'air qu'elles contiennent. */
  function drawIntakeWalls(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    // Deux turbos : la boîte à air du premier chemin est un peu plus haut, celle du second à sa place.
    const y = view.intakeYs[0];
    if (view.twin && net.intakeParts2.length > 0) {
      const second = net.intakeParts2[0];
      const right2 = view.intakeX2[0];
      const left2 = view.intakeX2[second.count];
      const tall2 = view.partTall2[0];
      ctx.strokeStyle = theme.metal;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.roundRect(left2, view.intakeYs2[0] - tall2 / 2, right2 - left2, tall2, 3);
      ctx.stroke();
    }
    // Boîte à air, avec sa cartouche plissée.
    const filter = net.intakeParts[0];
    const boxRight = view.intakeX[0];
    const boxLeft = view.intakeX[filter.count];
    const boxTall = view.partTall[0];
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(boxLeft, y - boxTall / 2, boxRight - boxLeft, boxTall, 3);
    ctx.stroke();
    ctx.globalAlpha = 0.6;
    ctx.lineWidth = 1;
    ctx.beginPath();
    const pleats = Math.max(3, Math.floor((boxRight - boxLeft) / 4));
    for (let i = 1; i < pleats; i++) {
      const px = boxLeft + ((boxRight - boxLeft) * i) / pleats;
      ctx.moveTo(px, y - boxTall / 2 + 2);
      ctx.lineTo(px, y + boxTall / 2 - 2);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
    // Le débitmètre d'air, sur le conduit d'entrée : un petit fil chaud en travers.
    const inlet = net.intakeParts[1];
    const inletMiddle = inlet.first - net.intakeFirst + (inlet.count >> 1);
    const mafX = view.intakeX[inletMiddle];
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(mafX - 3, y - view.tube * 0.95, 6, view.tube * 1.9, 1.5);
    ctx.fill();
    ctx.stroke();

    // L'échangeur : son corps et ses ailettes. La température de l'air s'y lit de droite à gauche.
    const fins = Math.max(4, Math.floor((view.coolerX1 - view.coolerX0) / 5));
    const coolerTop = view.coolerY - view.coolerTall / 2;
    ctx.strokeStyle = theme.metal;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(view.coolerX0, coolerTop, view.coolerX1 - view.coolerX0, view.coolerTall, 2);
    ctx.stroke();
    ctx.globalAlpha = 0.45;
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 1; i < fins; i++) {
      const fx = view.coolerX0 + ((view.coolerX1 - view.coolerX0) * i) / fins;
      ctx.moveTo(fx, coolerTop + 1);
      ctx.lineTo(fx, coolerTop + view.coolerTall - 1);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;

    if (view.width > 420) {
      ctx.font = '11px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      const coolerBelow = coolerTop + view.coolerTall + 11;
      label(ctx, theme, t('gas.intercooler'), (view.coolerX0 + view.coolerX1) / 2, coolerBelow);
      label(ctx, theme, t('gas.airbox'), (boxLeft + boxRight) / 2, y + boxTall / 2 + 12);
      if (view.width > 620) {
        label(ctx, theme, t('gas.maf'), mafX, y - view.tube * 0.95 - 4);
        // Les températures, de part et d'autre de l'échangeur, et les tubes qui les portent.
        const hot = Math.round(net.temp[net.coolerInCell] - 273.15);
        const cold = Math.round(net.temp[net.coolerOutCell] - 273.15);
        ctx.textAlign = 'right';
        label(ctx, theme, t('gas.tempOut', { deg: hot }), view.coolerX1 - 3, coolerTop - 3);
        label(ctx, theme, t('gas.chargeHot'), view.coolerX1 - 3, coolerTop - 15);
        ctx.textAlign = 'left';
        label(ctx, theme, t('gas.tempOut', { deg: cold }), view.coolerX0 + 2, coolerTop - 3);
        const coldCell = net.intakeParts[4];
        const coldAt = coldCell.first - net.intakeFirst + (coldCell.count >> 2);
        label(ctx, theme, t('gas.chargeCold'), view.intakeX[coldAt] - view.tube * 0.5 - 2 > view.plenumRight ? view.plenumRight + 2 : view.plenumRight + 2, coolerBelow);
        if (net.releaseCell >= 0 && config.turboRelease !== 'flutter') {
          const i = net.releaseCell - net.intakeFirst;
          label(ctx, theme, t('gas.blowoff'), view.intakeX[i] + view.tube * 2.2, view.intakeYs[i] - 8);
        }
        ctx.textAlign = 'center';
      }
    }
  }

  /** Le tracé de la pression le long du tube : c'est là qu'on voit les ondes courir et se réfléchir. */
  function drawTraces(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout) {
    const net = motion.gas.net;
    const base = view.lineTraceY;
    const range = view.traceRange;
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(view.engineRight, base);
    ctx.lineTo(view.exitX, base);
    ctx.moveTo(view.engineRight, view.intakeTraceY);
    ctx.lineTo(view.filterX, view.intakeTraceY);
    ctx.stroke();

    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.25;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    // Depuis le collecteur : sa pression, puis cellule après cellule.
    ctx.moveTo(view.joinX, base + traceOffset(net.zP[net.zoneJoin], range));
    for (let i = 0; i < net.pipeCells; i++) ctx.lineTo(view.pipePoints[i * 2], base + traceOffset(net.p[net.pipeFirst + i], range));
    for (let i = 0; i < net.lineCount; i++) {
      ctx.lineTo(view.lineX[i], base + traceOffset(net.p[net.lineFirst + i], range));
    }
    ctx.lineTo(view.exitX, base + traceOffset(net.exitP, range));
    ctx.stroke();

    ctx.beginPath();
    // Toute la chaîne à plat, dans l'ordre : on y voit la dépression du filtre, le saut du compresseur, la perte de l'échangeur, puis le papillon.
    ctx.moveTo(view.traceX[0], view.intakeTraceY + traceOffset(net.p[net.intakeFirst], range));
    for (let i = 1; i < net.intakeCount; i++) {
      ctx.lineTo(view.traceX[i], view.intakeTraceY + traceOffset(net.p[net.intakeFirst + i], range));
    }
    const py = view.intakeTraceY + traceOffset(net.zP[net.zonePlenum], range);
    ctx.lineTo(view.throttleX, py);
    ctx.lineTo(view.plenumLeft, py);
    ctx.stroke();
    if (net.compressorCell >= 0) {
      // Repères sur le tracé : le compresseur, l'échangeur, le papillon.
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (const cell of [net.compressorCell, net.coolerFirst, net.coolerOutCell, net.intakeFirst + net.intakeCount - 1]) {
        const tx = view.traceX[cell - net.intakeFirst];
        ctx.moveTo(tx, view.intakeTraceY - 3);
        ctx.lineTo(tx, view.intakeTraceY + 3);
      }
      ctx.stroke();
    }

    // Une lecture en clair, rafraîchie sans fabriquer de texte à chaque image.
    const now = performance.now();
    if (now - readoutAt > 200) {
      readoutAt = now;
      readoutLine = t('gas.manifold', { bar: fixed(net.zP[net.hasTwin ? 0 : net.zoneJoin] / 1e5, 2) });
      readoutPlenum = t('gas.plenum', { bar: fixed(net.zP[net.zonePlenum] / 1e5, 2) });
      if (net.compressorCell >= 0) readoutCharge = t('gas.charge', { bar: fixed(net.p[net.intakeFirst + net.intakeCount - 1] / 1e5, 2) });
    }
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'alphabetic';
    ctx.textAlign = 'left';
    ctx.fillStyle = theme.dim;
    if (net.compressorCell >= 0 && view.width > 520) {
      // Avant et après le papillon : la pression que le turbo fournit, et celle que le moteur reçoit.
      ctx.textAlign = 'right';
      ctx.fillText(readoutCharge, view.exitX, view.intakeTraceY - 3);
      ctx.fillText(readoutPlenum, view.exitX, view.intakeTraceY + 10);
      ctx.textAlign = 'left';
    } else {
      ctx.fillText(readoutPlenum, view.filterX + 18, view.intakeTraceY + 4);
    }
    ctx.fillText(readoutLine, view.joinX, Math.min(base + range + 11, view.height - 2));
  }

  /** Une fine mire derrière le panache : la chaleur qui dévie la lumière s'y voit, comme en strioscopie. */
  function drawMire(ctx: CanvasRenderingContext2D, theme: Theme, view: Layout, groundY: number) {
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    ctx.beginPath();
    const step = 12;
    for (let x = view.exitX + 4; x < view.width; x += step) {
      ctx.moveTo(x, 4);
      ctx.lineTo(x, groundY);
    }
    for (let y = groundY - step; y > 4; y -= step) {
      ctx.moveTo(view.exitX + 2, y);
      ctx.lineTo(view.width, y);
    }
    ctx.stroke();
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(view.exitX - 30, groundY);
    ctx.lineTo(view.width, groundY);
    ctx.stroke();
  }

  function draw(_seconds: number, now: number) {
    const theme = readTheme(canvas);
    const { ctx, width, height } = fit(canvas);
    motion.update(now, telemetry, config);
    const gas = motion.gas;
    gas.wantChambers = true;

    if (config.electric) {
      ctx.fillStyle = theme.dim;
      ctx.font = '13px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(t('gas.electric'), width / 2, height / 2);
      return;
    }
    if (gas.net.n === 0) return;
    refreshStyles(theme);
    // `sections` est refait à chaque reconstruction du réseau : la taille du turbo et la chaîne en découlent.
    if (!layout || layout.width !== width || layout.height !== height || layout.net !== gas.net.sections) {
      layout = buildLayout(width, height);
    }
    const lineCells = gas.net.lineCount;
    if (meanPressure.length !== lineCells) meanPressure = new Float32Array(lineCells).fill(P_ATM);
    for (let i = 0; i < lineCells; i++) meanPressure[i] += (gas.net.p[gas.net.lineFirst + i] - meanPressure[i]) * 0.03;
    const view = layout;
    const painter = motion.painter;
    const ratio = canvas.width / Math.max(width, 1);
    const scale = view.plumeScale;
    const groundY = view.lineY + PLUME_EXIT_ROW * scale;

    drawMire(ctx, theme, view, groundY);
    if (!motion.reduced) {
      painter.shimmer(ctx, canvas, ratio, view.exitX, view.lineY, scale, 1, PLUME_PIPE_CELLS, PLUME_EXIT_ROW, 0.03);
      painter.drawPlume(ctx, theme, view.exitX, view.lineY, scale, 1, PLUME_PIPE_CELLS, PLUME_EXIT_ROW);
    }
    drawEngine(ctx, theme, view);
    drawRunners(ctx, theme, view);
    drawLine(ctx, theme, view);
    drawIntake(ctx, theme, view);
    drawTurbo(ctx, theme, view);
    if (!motion.reduced) drawParticles(ctx, view);
    drawTraces(ctx, theme, view);
  }

  onMount(() => animate(canvas, draw));

  const dual = $derived(config.layout !== 'inline' && !isRotary(config) && config.cylinders >= 4);
  const caption = $derived(
    config.electric ? '' : [t(`gas.line.${config.exhaust}`), ...(dual ? [t('gas.banks')] : []), ...(config.aspiration === 'turbo' ? [t('gas.turboPulses')] : []), ...(config.aspiration === 'supercharger' ? [t('gas.chargerNote')] : []), ...(config.aspiration === 'turbo' && config.fuel === 'petrol' && config.antiLag === 'air' ? [t('gas.antiLagAir')] : []), t('gas.legend', { speed: speedLabel(slow) })].join(' '),
  );
</script>

<figure>
  <div role="img" aria-label={t('gas.aria', { caption })} data-testid="exhaust-view">
    <canvas bind:this={canvas}></canvas>
  </div>
  {#if config.aspiration === 'turbo' && !config.electric}
    <p class="gauge" data-testid="turbo-readout">
      {t('gas.gauge.turbo')} <strong>{fixed(telemetry.boost * config.boostBar, 2)} / {fixed(config.boostBar, 1)} bar</strong>
      · {t('gas.gauge.turbine', { rpm: whole(Math.round(telemetry.turboRpm / 1000) * 1000) })}{telemetry.turboWastegate > 0.1 ? ` · ${t('gas.gauge.wastegate')}` : ''}
      · {t('gas.gauge.inlet', { celsius: whole(telemetry.turboGasC) })}{telemetry.antiLag ? ` · ${t('gas.gauge.antiLag', { kw: fixed(telemetry.antiLagHeatKw, 0) })}` : ''}
    </p>
  {:else if config.aspiration === 'supercharger' && !config.electric}
    <p class="gauge" data-testid="turbo-readout">
      {t('gas.gauge.charger')} <strong>{fixed(telemetry.boost * config.boostBar, 2)} / {fixed(config.boostBar, 1)} bar</strong>
      · {t('gas.gauge.turbine', { rpm: whole(Math.round(telemetry.chargerRpm / 100) * 100) })} · {t('gas.gauge.chargerKw', { kw: fixed(telemetry.chargerKw, 1) })}{telemetry.chargerSpill > 0.3 ? ` · ${t('gas.gauge.bypass')}` : ''}
    </p>
  {/if}
  <figcaption>{caption}</figcaption>
</figure>

<style>
  figure { margin: 0; }
  canvas { display: block; width: 100%; height: var(--view-h, clamp(250px, 26vw, 330px)); }
  figcaption { margin-top: 6px; color: var(--text-dim); font-size: 0.8rem; }
  .gauge { margin: 6px 0 0; color: var(--text-dim); font-size: 0.82rem; font-variant-numeric: tabular-nums; }
  .gauge strong { color: var(--text); font-weight: 600; }
</style>
