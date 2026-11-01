<script lang="ts">
  import { onMount, tick } from 'svelte';
  import { animate, fit, readTheme, type Theme } from '../lib/canvas';
  import type { EngineConfig } from '../lib/config';
  import type { BoxSpec, Telemetry } from '../lib/engine';
  import { buildGearbox, GearboxMotion, newFrame, type GearboxLayout } from '../lib/gearboxModel';
  import { GearboxPainter } from '../lib/gearboxPaint2d';
  import { disposeOwnMaterials, GearboxScene, makeOwnMaterials } from '../lib/gearboxScene';
  import { speedLabel, t, whole, type MessageKey } from '../lib/i18n.svelte';
  import { SLOW_MOTION, type Motion } from '../lib/motion';
  import { createStage3D, probeWebGL2, type Stage3D } from '../lib/render3d';

  interface Props {
    config: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    /** Le ralenti des animations : la légende le suit. */
    slow?: number;
    box: BoxSpec;
    /** Dents de la couronne de démarreur, sur le volant. */
    starterRing?: number;
  }
  let { config, telemetry, motion, box, starterRing = 0, slow = SLOW_MOTION }: Props = $props();

  const STORED_MODE = 'revforge.boxView';
  /** Une reconstruction à la fois, même si un curseur bouge à chaque image. */
  const REBUILD_EVERY_MS = 120;
  const SOFTWARE_RATIO = 0.7;
  const YAW = 0.62;
  const PITCH = 0.34;
  const STEP_NAMES: (MessageKey | null)[] = [null, 'box.step.declutch', 'box.step.out', 'box.step.travel', 'box.step.synchro', 'box.step.engage', 'box.step.reclutch', 'box.step.handover'];

  let overlay: HTMLCanvasElement;
  let glCanvas = $state() as HTMLCanvasElement;

  function storedMode(): '2d' | '3d' | null {
    try {
      const value = localStorage.getItem(STORED_MODE);
      return value === '2d' || value === '3d' ? value : null;
    } catch {
      return null;
    }
  }

  const has3d = probeWebGL2().available;
  let mode = $state<'2d' | '3d'>(has3d && storedMode() !== '2d' ? '3d' : '2d');
  let labels = $state(false);
  let casing = $state(true);
  let lost = $state(false);
  let glGeneration = $state(0);
  let hover = -1;

  let layout: GearboxLayout = buildGearbox(box, options());
  let kin = new GearboxMotion(layout);
  let painter = new GearboxPainter(layout, translate);
  const frame = newFrame();
  let builtKey = keyOf();
  let builtBox = box;
  let builtFlywheel = config.boxFlywheel;
  let builtDrive = config.drive;
  let builtRing = starterRing;
  let lastBuild = 0;
  let missingWheel: number | undefined;
  let lastEngaged = 0;

  let stage: Stage3D | null = null;
  let scene: GearboxScene | null = null;
  let own: ReturnType<typeof makeOwnMaterials> | null = null;
  let starting = false;
  let destroyed = false;
  const projected = new Float64Array(3);
  const anchorPoint = { x: 0, y: 0 };

  function translate(key: MessageKey, params?: Record<string, string | number>): string {
    return t(key, params);
  }

  function options() {
    return { flywheel: config.boxFlywheel, drive: config.drive, starterRing };
  }

  function keyOf(): string {
    return JSON.stringify([box, config.boxFlywheel, config.drive, starterRing]);
  }

  function rebuild() {
    layout = buildGearbox(box, options());
    kin = new GearboxMotion(layout);
    painter = new GearboxPainter(layout, translate);
    painter.missingWheel = missingWheel;
    builtKey = keyOf();
    if (stage && own) buildScene();
    publish();
  }

  function buildScene() {
    if (!stage || !own) return;
    scene?.dispose();
    scene = new GearboxScene(stage, layout, own, { missingTooth: missingWheel });
    const aspect = glCanvas.clientWidth / Math.max(glCanvas.clientHeight, 1);
    const view = scene.frame(aspect, YAW, PITCH);
    stage.frameOn(view.x, view.y, view.z, view.radius, { yaw: YAW, pitch: PITCH });
  }

  async function start3d() {
    if (starting || stage || destroyed) return;
    starting = true;
    await tick();
    const created = await createStage3D(glCanvas, {
      // Sur un rendu logiciel, chaque pixel se paie : la vue se contente de moins, sans perdre ses dents.
      maxRatio: probeWebGL2().software ? SOFTWARE_RATIO : 2,
      onLost: () => (lost = true),
      onRestored: () => (lost = false),
    });
    starting = false;
    if (destroyed || mode !== '3d') {
      created?.dispose();
      return;
    }
    if (!created) {
      mode = '2d';
      return;
    }
    stage = created;
    own = makeOwnMaterials(created);
    buildScene();
    publish();
  }

  function stop3d() {
    scene?.dispose();
    scene = null;
    if (own) disposeOwnMaterials(own);
    own = null;
    if (stage) {
      stage.dispose();
      // Un canvas dont on a rendu le contexte n'en obtient plus : il en faut un neuf pour revenir à la 3D.
      glGeneration++;
    }
    stage = null;
    lost = false;
  }

  function choose(next: '2d' | '3d') {
    if (next === mode) return;
    mode = next;
    try {
      localStorage.setItem(STORED_MODE, next);
    } catch {
      // Le choix vaudra pour cette visite.
    }
    if (next === '3d') void start3d();
    else stop3d();
  }

  /** Les essais lisent la vue par ici. */
  function publish() {
    if (typeof location === 'undefined' || !location.search.includes('test')) return;
    (window as unknown as Record<string, unknown>).__revforgeBox = {
      get layout() { return layout; },
      get kin() { return kin; },
      get mode() { return mode; },
      get stage() { return stage; },
      get scene() { return scene; },
      get lost() { return lost; },
      get triangles() { return scene?.triangles ?? 0; },
      get drawCalls() { return scene?.drawCalls ?? 0; },
      get overlay() { return overlay; },
      get glCanvas() { return glCanvas; },
      setAfterDraw: (fn: (() => void) | null) => (afterDraw = fn),
      measure: (finish: boolean) => (timing = { samples: [], finish }),
    };
  }

  function readFrame() {
    const f = frame;
    f.inputTotal = motion.boxInputTotal;
    f.outputTotal = motion.boxOutputTotal;
    f.flywheelTotal = motion.flywheelTotal;
    f.inputRpm = motion.inputRpm;
    f.outputRpm = motion.outputRpm;
    f.rpm = motion.rpm;
    f.gear = telemetry.gear;
    f.meshed = telemetry.meshed !== 0;
    f.step = telemetry.shiftStep;
    f.pending = telemetry.boxPending;
    f.coneSlip = telemetry.boxConeSlip;
    f.grinding = telemetry.grinding;
    f.rattle = telemetry.boxRattle;
    f.loose0 = telemetry.boxLoose0;
    f.loose1 = telemetry.boxLoose1;
    f.loose2 = telemetry.boxLoose2;
    f.loose3 = telemetry.boxLoose3;
    f.throttle = telemetry.throttle;
    f.clutch = telemetry.clutch;
    f.clutchTorque = motion.clutchTorque;
    f.frozen = motion.reduced;
  }

  /** La dent qui cède : celle du pignon qui portait le couple. Le modèle dit qu'une dent a cassé, pas laquelle. */
  function noteBreakage() {
    const broken = telemetry.boxBroken !== 0;
    if (!broken) {
      if (kin.engaged > 0) lastEngaged = kin.engaged;
      if (missingWheel !== undefined) {
        missingWheel = undefined;
        rebuild();
      }
      return;
    }
    if (missingWheel === undefined) {
      const pair = layout.pairs.find((p) => p.gear === lastEngaged && !p.direct) ?? layout.pairs.find((p) => !p.direct);
      if (pair) {
        missingWheel = pair.driver;
        rebuild();
      }
    }
  }

  function draw(seconds: number, now: number) {
    motion.update(now, telemetry, config);
    const began = timing ? performance.now() : 0;
    // La fiche n'est un nouvel objet que si elle a été recalculée : on ne sérialise rien à chaque image.
    if (now - lastBuild > REBUILD_EVERY_MS && (box !== builtBox || config.boxFlywheel !== builtFlywheel || config.drive !== builtDrive || starterRing !== builtRing)) {
      lastBuild = now;
      builtBox = box;
      builtFlywheel = config.boxFlywheel;
      builtDrive = config.drive;
      builtRing = starterRing;
      if (keyOf() !== builtKey) rebuild();
    }
    noteBreakage();
    readFrame();
    kin.update(frame, seconds);
    const theme = readTheme(overlay);
    const { ctx, width, height } = fit(overlay);
    if (layout.wheels.length === 0 || width === 0 || height === 0) return;
    const flags = flags3dScratch;
    flags.wear = telemetry.boxWear;
    flags.broken = telemetry.boxBroken !== 0;
    const in3d = mode === '3d' && stage && scene && !stage.lost;
    if (in3d) {
      flags.grinding = telemetry.grinding > 5;
      flags.syncing = telemetry.shiftStep === 4;
      flags.casing = casing;
      scene!.setTheme(theme);
      stage!.setTheme(theme);
      // Rendu logiciel : une image sur deux. La mécanique reste lisible à trente images par seconde, et la page respire.
      skipped = stage!.software && !skipped;
      if (!skipped) {
        scene!.update(kin, flags);
        stage!.render();
      }
      if (!stage!.lost) drawOverlay3d(ctx, theme, width, height);
    } else {
      painter.draw(ctx, theme, kin, frame, width, height, flags);
      drawLabels2d(ctx, theme);
      drawStatus(ctx, theme, painter.section.x, Math.max(painter.casingTop() - 5, 12), painter.section.x + painter.section.w);
    }
    if (timing) {
      // Lire un pixel oblige le rendu logiciel à finir : sans cela, on ne mesure que les appels.
      if (timing.finish && stage && !skipped) stage.renderer.getContext().readPixels(0, 0, 1, 1, 6408, 5121, timingPixel);
      timing.samples.push(performance.now() - began);
    }
    afterDraw?.();
  }

  /** Pour les essais : appelé juste après le dessin, tant que le tampon WebGL est lisible. */
  let afterDraw: (() => void) | null = null;
  /** Pour les essais : le temps de chaque image, et si l'on attend que le processeur graphique ait fini. */
  let timing: { samples: number[]; finish: boolean } | null = null;
  const timingPixel = new Uint8Array(4);

  let skipped = false;
  const flags3dScratch = { wear: 0, broken: false, grinding: false, syncing: false, casing: true };

  /** L'état de la boîte, en une ou deux lignes : ce qu'on ne voit pas sur les pièces. */
  function drawStatus(ctx: CanvasRenderingContext2D, theme: Theme, x: number, y: number, width: number) {
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'left';
    ctx.fillStyle = theme.danger;
    if (telemetry.grinding > 5) {
      ctx.fillText(t('box.view.grinding', { rpm: whole((telemetry.grinding * 60) / (Math.PI * 2)) }), x + 2, y, width - 20);
      return;
    }
    if (telemetry.boxRattle > 0.5) {
      ctx.fillText(t('box.view.rattle'), x + 2, y, width - 20);
      return;
    }
    ctx.fillStyle = theme.cool;
    if (layout.form === 'dual_clutch' && kin.preselected > 0) ctx.fillText(t('box.view.preselected', { gear: kin.preselected }), x + 2, y, width - 20);
    else if (layout.form === 'converter') {
      ctx.fillStyle = theme.dim;
      ctx.fillText(t('box.view.planetNote'), x + 2, y, width - 20);
    }
  }

  function drawOverlay3d(ctx: CanvasRenderingContext2D, theme: Theme, width: number, height: number) {
    if (!stage) return;
    // Le numéro de chaque rapport, au sommet de sa roue menée.
    ctx.font = '600 11px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'center';
    for (const wheel of layout.wheels) {
      if ((wheel.role !== 'driven' && wheel.role !== 'reverseWheel') || layout.selectors.length === 0) continue;
      stage.project(projected, wheel.x, wheel.y + wheel.radius * 0.2, wheel.z + wheel.radius + wheel.moduleN + 3);
      if (projected[2] < 1) continue;
      const px = projected[0] * width;
      const py = projected[1] * height;
      const on = wheel.gear === kin.engaged;
      ctx.fillStyle = on ? theme.accent : theme.surface;
      ctx.beginPath();
      ctx.arc(px, py, 8, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = on ? theme.bg : theme.text;
      ctx.fillText(wheel.gear < 0 ? 'R' : String(wheel.gear), px, py + 4);
    }
    // L'écart de vitesses pendant le passage.
    if (kin.syncSelector >= 0) {
      const selector = layout.selectors[kin.syncSelector];
      stage.project(projected, selector.x, selector.y + selector.sleeveRadius, selector.z + selector.sleeveRadius);
      ctx.fillStyle = theme.text;
      ctx.fillText(`Δ ${Math.abs(Math.round(kin.syncGapRpm))}`, projected[0] * width, projected[1] * height - 8);
    }
    // Les repères.
    const anchors = layout.anchors;
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    placedCount = 0;
    for (let i = 0; i < anchors.length; i++) {
      const a = anchors[i];
      if (!(i === hover || (labels && (!a.minor || width > 700)))) continue;
      stage.project(projected, a.x, a.y, a.z);
      if (projected[2] < 1) continue;
      drawLabel(ctx, theme, t(a.key, a.params), projected[0] * width, projected[1] * height, width, height);
    }
    // La bande d'état, en haut à gauche. Elle garde aussi du sens sans la couleur.
    ctx.fillStyle = theme.dim;
    ctx.textAlign = 'left';
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.fillText(t('box.view.speeds', { input: whole(motion.inputRpm), output: whole(motion.outputRpm), unit: t('unit.rpm') }), 10, 16, width - 20);
    drawStatus(ctx, theme, 8, 32, width);
    ctx.fillStyle = theme.dim;
    ctx.textAlign = 'right';
    ctx.fillText(speedLabel(motion.slow), width - 10, height - 8);
  }

  /** Les étiquettes déjà posées sur cette image : aucune ne cache la précédente. */
  const placed = new Float64Array(64);
  let placedCount = 0;

  function drawLabel(ctx: CanvasRenderingContext2D, theme: Theme, text: string, px: number, py: number, width: number, height: number) {
    const w = ctx.measureText(text).width + 10;
    const lx = Math.max(4, Math.min(px + 10, width - w - 4));
    let ly = Math.max(4, py - 24);
    for (let attempt = 0; attempt < 6; attempt++) {
      let hit = false;
      for (let k = 0; k < placedCount; k++) {
        const a = placed[k * 4];
        const b = placed[k * 4 + 1];
        if (lx < a + placed[k * 4 + 2] && a < lx + w && ly < b + 19 && b < ly + 19) {
          hit = true;
          break;
        }
      }
      if (!hit) break;
      ly = Math.min(ly + 20, height - 20);
    }
    if (placedCount < 16) {
      placed[placedCount * 4] = lx;
      placed[placedCount * 4 + 1] = ly;
      placed[placedCount * 4 + 2] = w;
      placedCount++;
    }
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(px, py);
    ctx.lineTo(lx, ly + 16);
    ctx.stroke();
    ctx.fillStyle = theme.surface;
    ctx.beginPath();
    ctx.roundRect(lx, ly, w, 17, 4);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = theme.text;
    ctx.textAlign = 'left';
    ctx.fillText(text, lx + 5, ly + 12);
    ctx.fillStyle = theme.accent;
    ctx.beginPath();
    ctx.arc(px, py, 2.5, 0, Math.PI * 2);
    ctx.fill();
  }

  function drawLabels2d(ctx: CanvasRenderingContext2D, theme: Theme) {
    const anchors = layout.anchors;
    const width = overlay.clientWidth;
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    placedCount = 0;
    for (let i = 0; i < anchors.length; i++) {
      const a = anchors[i];
      if (!(i === hover || (labels && (!a.minor || width > 700)))) continue;
      painter.anchorAt(i, anchorPoint);
      drawLabel(ctx, theme, t(a.key, a.params), anchorPoint.x, anchorPoint.y, width, overlay.clientHeight);
    }
  }

  /** Le canvas focalisé garde ses touches : les flèches tournent la vue au lieu de passer un rapport. */
  function keepKeys(event: KeyboardEvent) {
    if (event.key.startsWith('Arrow') || event.key === '+' || event.key === '=' || event.key === '-' || event.key === '0' || event.key === 'Home') event.stopPropagation();
  }

  /** Le repère le plus proche du pointeur, s'il est assez près : les noms apparaissent au survol. */
  function onPointerMove(event: PointerEvent) {
    const rect = overlay.getBoundingClientRect();
    const px = event.clientX - rect.left;
    const py = event.clientY - rect.top;
    let best = -1;
    let bestDistance = 26;
    for (let i = 0; i < layout.anchors.length; i++) {
      const a = layout.anchors[i];
      let x: number;
      let y: number;
      if (mode === '3d' && stage && !stage.lost) {
        stage.project(projected, a.x, a.y, a.z);
        x = projected[0] * rect.width;
        y = projected[1] * rect.height;
      } else {
        painter.anchorAt(i, anchorPoint);
        x = anchorPoint.x;
        y = anchorPoint.y;
      }
      const d = Math.hypot(x - px, y - py);
      if (d < bestDistance) {
        bestDistance = d;
        best = i;
      }
    }
    hover = best;
  }

  onMount(() => {
    if (mode === '3d') void start3d();
    publish();
    const stopLoop = animate(overlay, draw);
    return () => {
      destroyed = true;
      stopLoop();
      stop3d();
    };
  });

  const current = $derived(box.gears[Math.max(Math.min((telemetry.gear > 0 ? telemetry.gear : 1) - 1, box.gears.length - 1), 0)]);
  const caption = $derived.by(() => {
    if (!current) return '';
    const parts: string[] = [];
    if (telemetry.gear < 0) parts.push(t('box.caption.reverse'));
    else if (telemetry.gear === 0) parts.push(t('box.caption.neutral'));
    else parts.push(t('box.caption.gear', { gear: telemetry.gear }));
    if (current.direct) parts.push(t('box.direct'));
    else parts.push(t('box.caption.pair', { driver: current.driver, driven: current.driven, cut: config.boxTeeth === 'spur' ? t('box.caption.spur') : t('box.helix', { angle: whole(box.helix) }) }));
    const step = STEP_NAMES[telemetry.shiftStep];
    if (step) parts.push(t(step));
    parts.push(t('box.caption.input', { rpm: whole(telemetry.clutchInputRpm) }), t('box.caption.output', { rpm: whole(telemetry.outputRpm) }));
    if (telemetry.meshed && telemetry.gear > 0 && !current.direct) {
      const hz = (telemetry.clutchInputRpm / 3000) * current.meshHz;
      parts.push(t('box.caption.mesh', { hz: whole(hz) }));
    }
    parts.push(speedLabel(slow));
    return parts.join(' · ');
  });
</script>

<figure>
  <div class="tools" role="group" aria-label={t('box.view.toolbar')}>
    <div class="switch">
      <button type="button" class:on={mode === '3d'} aria-pressed={mode === '3d'} disabled={!has3d} onclick={() => choose('3d')} data-testid="gearbox-mode-3d">{t('box.view.mode3d')}</button>
      <button type="button" class:on={mode === '2d'} aria-pressed={mode === '2d'} onclick={() => choose('2d')} data-testid="gearbox-mode-2d">{t('box.view.mode2d')}</button>
    </div>
    <button type="button" class:on={labels} aria-pressed={labels} onclick={() => (labels = !labels)} data-testid="gearbox-labels">{t('box.view.labels')}</button>
    {#if mode === '3d'}
      <button type="button" class:on={casing} aria-pressed={casing} onclick={() => (casing = !casing)} data-testid="gearbox-casing">{t('box.view.casing')}</button>
      <button type="button" onclick={() => stage?.resetView()} data-testid="gearbox-reset">{t('box.view.reset')}</button>
    {/if}
    {#if !has3d}<small data-testid="gearbox-no3d">{t('box.view.no3d')}</small>{/if}
  </div>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="stack" role="group" aria-label={t('box.aria', { caption })} data-testid="gearbox-view" data-mode={mode} data-lost={lost} onpointermove={onPointerMove} onpointerleave={() => (hover = -1)}>
    <canvas class="overlay" bind:this={overlay}></canvas>
    {#key glGeneration}
      <canvas class="gl" class:hidden={mode !== '3d' || lost} bind:this={glCanvas} tabindex={mode === '3d' ? 0 : -1} aria-label={t('box.view.canvas')} onkeydown={keepKeys}></canvas>
    {/key}
  </div>
  <figcaption data-testid="gearbox-caption">{caption}</figcaption>
</figure>

<style>
  figure { margin: 0; }
  .tools { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; margin-bottom: 8px; }
  .switch { display: inline-flex; gap: 2px; }
  button {
    min-height: 34px;
    padding: 0 10px;
    border: 0;
    background: var(--surface-raised);
    color: var(--text);
    font: inherit;
    font-size: 0.82rem;
    cursor: pointer;
  }
  button:hover:not(:disabled):not(.on) { background: var(--surface-high); }
  button:disabled { opacity: 0.45; cursor: not-allowed; }
  button.on { background: var(--control); color: var(--on-control); font-weight: 600; }
  small { color: var(--text-dim); font-size: 0.78rem; }
  .stack { position: relative; width: 100%; height: var(--view-h, 430px); touch-action: pan-y pinch-zoom; }
  canvas { position: absolute; inset: 0; display: block; width: 100%; height: 100%; }
  .gl { z-index: 0; }
  .gl:focus-visible { outline: 3px solid var(--text); outline-offset: 2px; }
  .gl.hidden { visibility: hidden; }
  .overlay { z-index: 1; pointer-events: none; }
  figcaption { margin-top: 6px; color: var(--text-dim); font-size: 0.8rem; font-variant-numeric: tabular-nums; }
  @media (max-width: 620px) { .stack { height: 480px; } }
</style>
