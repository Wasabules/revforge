<script lang="ts">
  import { onMount } from 'svelte';
  import type { EngineConfig } from '../lib/config';
  import { firingsPerCycle, sweptCc } from '../lib/rotary';
  import { animate, fit, readTheme, type Theme } from '../lib/canvas';
  import { activeClutch, bearingTouches, clutchDimensions, CLUTCH_SMOKE_C as SMOKE_FROM, emptyFrame, emptyTravel, pedalFromShare, readFrame, travelFromPedal, type ClutchDims } from '../lib/clutchMechanism';
  import { ClutchPainter, type Box, type ClutchLive } from '../lib/clutchDraw2d';
  import { clutchLayout, type ClutchLayout } from '../lib/clutchSection';
  import type { ClutchScene } from '../lib/clutchScene3d';
  import type { Telemetry } from '../lib/engine';
  import { fixed, t, whole as number } from '../lib/i18n.svelte';
  import { CELL_OPEN, CELL_WALL, FluidField } from '../lib/fluidField';
  import type { Motion } from '../lib/motion';
  import { createStage3D, probeWebGL2, type Stage3D } from '../lib/render3d';

  interface Props {
    config: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    /** Couple maximal du moteur : le diamètre du disque et ses ressorts sont calculés pour lui. */
    peakTorque: number;
    /** Rapports de la boîte, pour le disque en attente d'un double embrayage. */
    ratios?: number[];
  }
  let { config, telemetry, motion, peakTorque, ratios = [] }: Props = $props();

  let canvas: HTMLCanvasElement;
  let glCanvas: HTMLCanvasElement;

  const COMPACT_WIDTH = 560;

  /** Fumée d'un embrayage qui surchauffe : un petit écoulement, calculé seulement quand il y a lieu. */
  let smoke: FluidField | null = null;
  let smokeCanvas: HTMLCanvasElement | null = null;
  let smokeContext: CanvasRenderingContext2D | null = null;
  let smokeImage: ImageData | null = null;
  let smokeLive = false;
  let sparkSeed = 7;

  const clamp = (value: number, low: number, high: number) => (value < low ? low : value > high ? high : value);

  function nextSpark(): number {
    sparkSeed = (sparkSeed * 1664525 + 1013904223) | 0;
    return (sparkSeed >>> 16) / 65536;
  }

  /** La 3D par défaut si l'appareil la permet, sinon une coupe en 2D. */
  let mode = $state<'3d' | '2d'>('2d');
  let available3d = $state(true);
  let glLive = $state(false);
  let labels = $state(true);
  let labelsChosen = false;
  let focused = $state(false);

  let stage: Stage3D | null = null;
  let stagePending = false;
  let scene: ClutchScene | null = null;
  let sceneKey = '';
  let destroyed = false;
  let SceneClass: typeof ClutchScene | null = null;

  /** Les cotes ne changent qu'avec le moteur, jamais à chaque image. */
  const hasClutch = $derived(!config.electric && config.boxKind !== 'converter');
  const sized = $derived(clutchDimensions({ peakTorque, cylinders: firingsPerCycle(config), displacementCc: sweptCc(config), boxFlywheel: config.boxFlywheel, boxKind: config.boxKind }));
  /** Un curseur qu'on fait glisser ne reconstruit rien tant qu'aucune cote n'a vraiment changé. */
  const dimsKey = $derived(`${sized.discDiameter}|${sized.flywheelThickness}|${sized.shaftR}|${sized.crankBolts}|${sized.coverBolts}|${sized.dampers}|${sized.dualMass}|${sized.kind}`);
  let dims: ClutchDims | null = null;
  let layout: ClutchLayout | null = null;
  let painter: ClutchPainter | null = null;
  let currentKey = '';
  const frame = emptyFrame();
  const live: ClutchLive = { heat: 0, slipKw: 0, rpm: 0, inputRpm: 0, frozen: false, transmitting: false };
  const sectionBox: Box = { x: 0, y: 0, w: 0, h: 0 };
  const zoomBox: Box = { x: 0, y: 0, w: 0, h: 0 };
  const projected = [0, 0, 0];

  function ensureGeometry(): void {
    if (dimsKey === currentKey && dims && layout && painter) return;
    currentKey = dimsKey;
    dims = sized;
    layout = clutchLayout(dims);
    painter = new ClutchPainter(layout);
  }

  async function ensureStage(): Promise<void> {
    if (stage || stagePending || !available3d) return;
    stagePending = true;
    const created = await createStage3D(glCanvas, {
      onLost: () => {
        glLive = false;
      },
      onRestored: () => {
        rendered = false;
        glLive = mode === '3d';
      },
    });
    stagePending = false;
    if (destroyed) {
      created?.dispose();
      return;
    }
    if (!created) {
      available3d = false;
      mode = '2d';
      return;
    }
    SceneClass = (await import('../lib/clutchScene3d')).ClutchScene;
    if (destroyed) {
      created.dispose();
      return;
    }
    stage = created;
    glLive = mode === '3d';
  }

  /** Le modèle se reconstruit quand le moteur change, pas à chaque image. */
  function ensureScene(): boolean {
    if (!stage || !SceneClass || !layout) return false;
    if (scene && sceneKey === currentKey) return true;
    scene?.dispose();
    scene = new SceneClass(stage, layout);
    if (focused) scene.frameBearing();
    sceneKey = currentKey;
    rendered = false;
    return true;
  }

  function selectMode(next: '3d' | '2d'): void {
    if (next === '3d' && !available3d) return;
    mode = next;
    glLive = next === '3d' && !!stage && !stage.lost;
    if (next === '3d') void ensureStage();
  }

  function resetView(): void {
    focused = false;
    scene?.frameWhole();
  }

  function focusBearing(): void {
    focused = true;
    scene?.frameBearing();
  }

  /** Quand les garnitures brûlent, elles fument : un petit panache chaud qui s'élève de la cloche. */
  function drawSmoke(ctx: CanvasRenderingContext2D, seconds: number, x: number, y: number, w: number, h: number) {
    const hot = motion.clutchSurface > SMOKE_FROM;
    if (!hot && !smokeLive) return;
    const NX = 54;
    const NY = 48;
    if (!smoke) {
      smoke = new FluidField(NX, NY, 0.012);
      smoke.gravity = 22;
      smoke.confinement = 0.5;
      smoke.smokeTau = 1.1;
      smoke.coolTau = 0.9;
      smoke.iterations = 8;
      for (let j = 0; j < NY; j++) {
        for (let i = 0; i < NX; i++) {
          const c = i + j * NX;
          if (j === 0) smoke.kind[c] = CELL_WALL;
          else if (i === 0 || i === NX - 1 || j === NY - 1) smoke.kind[c] = CELL_OPEN;
        }
      }
      smoke.finalize();
      smokeCanvas = document.createElement('canvas');
      smokeCanvas.width = NX;
      smokeCanvas.height = NY;
      smokeContext = smokeCanvas.getContext('2d');
      smokeImage = smokeContext?.createImageData(NX, NY) ?? null;
    }
    const field = smoke;
    const physical = (motion.reduced ? 0 : seconds) / motion.slow;
    if (physical > 0) {
      const strength = clamp((motion.clutchSurface - SMOKE_FROM) / 120, 0, 1);
      if (hot) {
        const from = Math.floor(NX * 0.42);
        const to = Math.floor(NX * 0.58);
        for (let i = from; i <= to; i++) {
          const c = i + 2 * NX;
          field.smoke[c] = 1;
          field.temp[c] = 180 * strength;
          field.v[c] = 0.9 + strength;
          field.u[c] += (nextSpark() - 0.5) * 0.9;
        }
      }
      field.step(physical);
    }
    let top = 0;
    const image = smokeImage;
    if (!image || !smokeContext || !smokeCanvas) return;
    const data = image.data;
    for (let j = 0; j < NY; j++) {
      let out = (NY - 1 - j) * NX * 4;
      for (let i = 0, c = j * NX; i < NX; i++, c++, out += 4) {
        const s = field.smoke[c];
        if (s > top) top = s;
        data[out] = 170;
        data[out + 1] = 170;
        data[out + 2] = 172;
        data[out + 3] = Math.min(190, s * 300);
      }
    }
    smokeLive = hot || top > 0.01;
    smokeContext.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(smokeCanvas, x, y, w, h);
  }


  /** La coupe et la vue de face partagent le canvas : côte à côte, ou l'une sur l'autre sur un téléphone. */
  function draw2d(ctx: CanvasRenderingContext2D, theme: Theme, width: number, height: number, compact: boolean, contact: boolean): void {
    const p = painter!;
    if (compact) {
      sectionBox.x = 0;
      sectionBox.y = 0;
      sectionBox.w = width;
      sectionBox.h = height * 0.68;
      p.drawSection(ctx, theme, frame, live, sectionBox, labels);
      const row = height * 0.32;
      const R = row * 0.47;
      p.drawFace(ctx, theme, frame, live, width * 0.3, height - row / 2, R);
      p.drawLoupe(ctx, theme, frame, width * 0.74, height - row / 2, R * 0.86, contact);
    } else {
      sectionBox.x = 0;
      sectionBox.y = 0;
      sectionBox.w = width * 0.67;
      sectionBox.h = height;
      p.drawSection(ctx, theme, frame, live, sectionBox, labels);
      const cx = width * 0.84;
      const R = Math.min(width * 0.15, height * 0.27);
      const cy = R + 4;
      p.drawFace(ctx, theme, frame, live, cx, cy, R);
      const panel = width * 0.33;
      const loupe = Math.min(panel * 0.2, height * 0.115);
      const rowY = cy + R + 10;
      p.drawLoupe(ctx, theme, frame, width * 0.67 + loupe + 8, rowY + loupe + 2, loupe, contact);
      zoomBox.x = width * 0.67 + 2 * loupe + 22;
      zoomBox.y = rowY;
      zoomBox.w = width - zoomBox.x - 2;
      zoomBox.h = 2 * loupe + 4;
      p.drawBearingZoom(ctx, theme, frame, zoomBox);
      p.drawBearingText(ctx, theme, frame, cx, rowY + 2 * loupe + 20, contact);
    }
    if (compact) p.drawNote(ctx, theme, 2, 12, 'left');
    else p.drawNote(ctx, theme, width - 2, height - 3, 'right');
    drawSmoke(ctx, seconds2d, width * 0.02, 0, width * 0.42, height * 0.7);
  }
  let seconds2d = 0;

  function draw3dOverlay(ctx: CanvasRenderingContext2D, theme: Theme, width: number, height: number, compact: boolean, contact: boolean): void {
    const p = painter!;
    const R = compact ? Math.min(width * 0.13, height * 0.11) : Math.min(width * 0.075, height * 0.14);
    const x = width - R - 8;
    const y = compact ? height - R - 8 : height - R - 52;
    if (labels && stage) p.drawLabels3d(ctx, theme, frame, stage, projected, width, height, focused);
    p.drawLoupe(ctx, theme, frame, x, y, R, contact);
    if (!compact) p.drawBearingText(ctx, theme, frame, Math.min(x, width - 88), y + R + 17, contact);
    p.drawNote(ctx, theme, 2, height - 3, 'left');
    drawSmoke(ctx, seconds2d, width * 0.2, 0, width * 0.42, height * 0.7);
  }

  function draw(seconds: number, now: number) {
    const theme = readTheme(canvas);
    motion.update(now, telemetry, config);
    // Ce que coûte cette vue, sans l'horloge commune : celle-ci tourne de toute façon.
    const started = performance.now();
    const { ctx, width, height } = fit(canvas);
    seconds2d = seconds;
    if (width <= 0 || height <= 0) return;

    if (!hasClutch) {
      // Pas de disque à montrer : plutôt que d'en inventer un, on le dit.
      ctx.fillStyle = theme.dim;
      ctx.font = '13px "Inter Tight", system-ui, sans-serif';
      ctx.textAlign = 'center';
      if (config.electric) {
        ctx.fillText(t('clutch.electric.line1'), width / 2, height / 2 - 9);
        ctx.fillText(t('clutch.electric.line2'), width / 2, height / 2 + 9);
      } else {
        ctx.fillText(t('clutch.converter.line1'), width / 2, height / 2 - 9);
        ctx.fillText(t('clutch.converter.line2'), width / 2, height / 2 + 9);
      }
      return;
    }
    ensureGeometry();
    const k = activeClutch(telemetry.gear);
    const idleGear = telemetry.gear < 0 ? 1 : telemetry.gear + 1 <= ratios.length ? telemetry.gear + 1 : telemetry.gear - 1;
    const current = ratios[Math.abs(telemetry.gear) - 1] ?? 1;
    const idleRatio = (ratios[idleGear - 1] ?? current) / current;
    readFrame(dims!, peakTorque, motion, k, idleRatio, frame);
    live.heat = motion.clutchSurface;
    live.slipKw = motion.clutchSlipKw;
    live.rpm = motion.rpm;
    live.inputRpm = motion.inputRpm;
    live.frozen = motion.reduced;
    live.transmitting = motion.clutchShare > 0.05 && Math.abs(motion.clutchTorque) > 3;
    const compact = width < COMPACT_WIDTH;
    if (compact && !labelsChosen && labels) labels = false;
    const contact = layout!.dct !== null || frame.travel.contact;

    if (mode === '3d' && stage && !stage.lost && ensureScene()) {
      if (shouldRender(theme, width, height)) {
        scene!.update(frame, live);
        stage.setTheme(theme);
        rendered = stage.render();
      }
      if (rendered) {
        draw3dOverlay(ctx, theme, width, height, compact, contact);
        motion.clutchCostMs += (performance.now() - started - motion.clutchCostMs) * 0.05;
        return;
      }
    }
    draw2d(ctx, theme, width, height, compact, contact);
    motion.clutchCostMs += (performance.now() - started - motion.clutchCostMs) * 0.05;
  }

  /**
   * Une image 3D ne se recalcule que si quelque chose a bougé : moteur coupé, ou animations arrêtées, elle ne coûte rien.
   * Sur un rendu logiciel, tourner à 60 images saturerait le processeur et le son avec lui : une sur trois suffit à l'œil.
   */
  let rendered = false;
  let lastSignature = NaN;
  let lastTheme: Theme | null = null;
  let frameCount = 0;
  function shouldRender(theme: Theme, width: number, height: number): boolean {
    const camera = stage!.camera.position;
    const signature = frame.crankAngle + 3 * frame.massAngle + 5 * frame.hubAngle + 7 * frame.liningAngle + 11 * frame.raceAngle + 13 * frame.cageAngle
      + 17 * frame.travel.pedal + 19 * frame.k1 + 23 * frame.k2 + 29 * live.heat + 31 * live.slipKw + 37 * camera.x + 41 * camera.y + 43 * camera.z + 47 * width + 53 * height;
    if (signature === lastSignature && theme === lastTheme && rendered) return false;
    frameCount++;
    if (stage!.software && rendered && theme === lastTheme && frameCount % 3 !== 0) return false;
    lastSignature = signature;
    lastTheme = theme;
    return true;
  }

  onMount(() => {
    available3d = probeWebGL2().available;
    if (available3d) {
      mode = '3d';
      void ensureStage();
    }
    // Les flèches tournent la vue : elles ne doivent pas aussi passer un rapport.
    const keep = (event: KeyboardEvent) => event.stopPropagation();
    glCanvas.addEventListener('keydown', keep);
    glCanvas.addEventListener('keyup', keep);
    const stop = animate(canvas, draw);
    if (location.search.includes('test')) {
      // Pour les essais : la caméra, et la taille du modèle.
      (window as unknown as Record<string, unknown>).__clutchView = {
        cameraKey: () => (stage ? stage.camera.position.toArray().map((v) => v.toFixed(2)).join() : ''),
        triangles: () => scene?.triangles ?? 0,
      };
    }
    return () => {
      destroyed = true;
      stop();
      glCanvas.removeEventListener('keydown', keep);
      glCanvas.removeEventListener('keyup', keep);
      scene?.dispose();
      stage?.dispose();
    };
  });

  const slip = $derived(Math.abs(telemetry.rpm - telemetry.clutchInputRpm));
  const stateText = $derived(
    telemetry.rpm < 50 ? t('clutch.state.stopped') :
      telemetry.clutch < 0.05 ? t('clutch.state.disengaged') :
        slip > 40 && telemetry.clutchSlipKw > 0.05 ? t('clutch.state.slipping') : t('clutch.state.engaged'),
  );
  const label = $derived(
    config.electric ? t('clutch.aria.electric') :
      config.boxKind === 'converter' ? t('clutch.aria.converter') :
      t('clutch.aria', { state: stateText, rpm: number(telemetry.rpm), input: number(telemetry.clutchInputRpm), torque: number(telemetry.clutchTorque) }),
  );
  const isDual = $derived(config.boxKind === 'dual_clutch');
  // La télémétrie arrive à 30 Hz : lue ici, elle rafraîchit aussi ce que l'horloge calcule à côté.
  const surface = $derived(telemetry.rpm >= 0 ? motion.clutchSurface : 0);
  const touches = $derived(isDual || bearingTouches(telemetry.clutch));
  /** La levée réelle, celle du calcul : le dessin, lui, l'agrandit. */
  const readout = emptyTravel();
  const realLift = $derived(travelFromPedal(sized, pedalFromShare(sized, telemetry.clutch), readout).lift);
  const dualK1 = $derived(telemetry.rpm >= 0 ? motion.clutchK1 : 0);
  const dualK2 = $derived(telemetry.rpm >= 0 ? motion.clutchK2 : 0);
</script>

<figure>
  {#if hasClutch}
    <div class="tools">
      <div class="modes" role="group" aria-label={t('clutch.mode')}>
        <button type="button" data-testid="clutch-mode-3d" aria-pressed={mode === '3d'} disabled={!available3d} title={available3d ? undefined : t('clutch.mode.unavailable')} onclick={() => selectMode('3d')}>{t('clutch.mode.3d')}</button>
        <button type="button" data-testid="clutch-mode-2d" aria-pressed={mode === '2d'} onclick={() => selectMode('2d')}>{t('clutch.mode.2d')}</button>
      </div>
      <button type="button" data-testid="clutch-labels" aria-pressed={labels} onclick={() => { labels = !labels; labelsChosen = true; }}>{t('clutch.labels')}</button>
      {#if mode === '3d' && glLive}
        <button type="button" class="icon" aria-label={t('engine.view.zoomOut')} title={t('engine.view.zoomOut')} onclick={() => stage?.zoomBy(1 / 1.25)}>−</button>
        <button type="button" class="icon" aria-label={t('engine.view.zoomIn')} title={t('engine.view.zoomIn')} onclick={() => stage?.zoomBy(1.25)}>+</button>
        <button type="button" data-testid="clutch-focus" aria-pressed={focused} onclick={focusBearing}>{t('clutch.focus')}</button>
        <button type="button" data-testid="clutch-reset" onclick={resetView}>{t('clutch.reset')}</button>
      {/if}
      {#if !available3d}
        <span class="note" data-testid="clutch-no-3d">{t('clutch.mode.unavailable')}</span>
      {/if}
    </div>
  {/if}
  <div class="stage" class:empty={!hasClutch} role="group" aria-label={label} data-testid="clutch-view" data-mode={glLive && mode === '3d' && hasClutch ? '3d' : '2d'}>
    <!-- Le canvas 2D d'abord : la coupe en 2D, les étiquettes et la loupe par-dessus la 3D. -->
    <canvas bind:this={canvas} class="flat"></canvas>
    <!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_noninteractive_element_to_interactive_role -->
    <canvas bind:this={glCanvas} class="gl" class:hidden={!(glLive && mode === '3d' && hasClutch)} tabindex={glLive && mode === '3d' && hasClutch ? 0 : -1} role="img" aria-label={t('clutch.orbit.aria')}></canvas>
  </div>
  {#if hasClutch}
    <dl class="figures">
      <div><dt>{t('common.state')}</dt><dd data-testid="clutch-state">{stateText}</dd></div>
      <div><dt>{t('clutch.flywheelDisc')}</dt><dd>{number(telemetry.rpm)} / {number(telemetry.clutchInputRpm)} {t('unit.rpm')}</dd></div>
      <div><dt>{t('clutch.torque')}</dt><dd>{number(telemetry.clutchTorque)} Nm</dd></div>
      <div><dt>{t('common.dissipated')}</dt><dd data-testid="clutch-power">{fixed(telemetry.clutchSlipKw, 1)} kW</dd></div>
      <div><dt>{t('common.linings')}</dt><dd data-testid="clutch-temp">{number(surface)} °C</dd></div>
      {#if isDual}
        <div><dt>K1 / K2</dt><dd data-testid="clutch-bearing">{t('clutch.dual.state', { k1: number(dualK1 * 100), k2: number(dualK2 * 100) })}</dd></div>
      {:else}
        <div><dt>{t('clutch.figure.bearing')}</dt><dd data-testid="clutch-bearing">{touches ? t('clutch.bearing.contact') : t('clutch.bearing.clear')}</dd></div>
        <div><dt>{t('clutch.figure.lift')}</dt><dd data-testid="clutch-lift">{fixed(realLift, 1)} mm</dd></div>
      {/if}
    </dl>
    {#if isDual}<p class="note">{t('clutch.dual.note')}</p>{/if}
  {/if}
</figure>

<style>
  figure { margin: 0; display: flex; flex-direction: column; gap: 6px; }
  .tools { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
  .modes { display: inline-flex; gap: 2px; }
  .tools button { min-height: 32px; padding: 0 10px; border: 0; background: var(--surface-raised); color: var(--text-dim); font: inherit; font-size: 0.8125rem; cursor: pointer; }
  .tools button:hover:not(:disabled) { background: var(--surface-high); }
  .tools button:disabled { opacity: 0.45; cursor: default; }
  .tools button[aria-pressed='true'], .tools button[aria-pressed='true']:hover:not(:disabled) { background: var(--control); color: var(--on-control); font-weight: 600; }
  .tools .icon { width: 32px; padding: 0; font-size: 0.95rem; }
  .tools button:focus-visible, canvas.gl:focus-visible { outline: 3px solid var(--text); outline-offset: 1px; }
  .note { font-size: 0.75rem; color: var(--text-dim); margin: 0; }
  .stage { position: relative; width: 100%; height: var(--view-h, auto); aspect-ratio: var(--view-aspect, 1.85); max-height: var(--view-max, 520px); }
  .stage.empty { aspect-ratio: var(--view-aspect, 3); }
  canvas { display: block; width: 100%; height: 100%; position: absolute; inset: 0; }
  canvas.flat { z-index: 1; pointer-events: none; }
  canvas.gl { z-index: 0; cursor: grab; }
  canvas.gl:active { cursor: grabbing; }
  canvas.gl.hidden { visibility: hidden; }
  .figures { display: flex; flex-wrap: wrap; gap: 2px 16px; margin: 0; }
  dt { color: var(--text-dim); font-size: 0.72rem; }
  dd { margin: 0; font-size: 0.82rem; font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  @media (max-width: 560px) {
    .stage { height: auto; aspect-ratio: 0.82; max-height: none; }
  }
</style>
