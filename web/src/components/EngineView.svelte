<script lang="ts">
  import { onDestroy, onMount } from 'svelte';
  import { type EngineConfig, strokeMm } from '../lib/config';
  import { animate, fit, readTheme, type Theme } from '../lib/canvas';
  import type { Telemetry } from '../lib/engine';
  import { Engine2D, type Labels2D } from '../lib/engine2d';
  import { engineLayout, EnginePose, engineState, type EngineLayout, layoutKey } from '../lib/engineLayout';
  import type { ElectricScene, EngineScene } from '../lib/engineScene';
  import { lang, speedLabel, t, type MessageKey } from '../lib/i18n.svelte';
  import { firingOrder, SLOW_MOTION, type Motion } from '../lib/motion';
  import { createStage3D, probeWebGL2, type Stage3D } from '../lib/render3d';

  interface Props {
    config: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    /** Le ralenti des animations : la légende le suit. */
    slow?: number;
  }
  let { config, telemetry, motion, slow = SLOW_MOTION }: Props = $props();

  const MODE_KEY = 'revforge.engineView';
  const rememberedMode = (): '2d' | '3d' | null => {
    try {
      const value = localStorage.getItem(MODE_KEY);
      return value === '2d' || value === '3d' ? value : null;
    } catch {
      return null;
    }
  };

  // Sans WebGL2, la vue reste en 2D et le dit. Rien de three.js n'est chargé avant la première image 3D.
  let available = $state(true);
  let mode = $state<'2d' | '3d'>('3d');
  let names = $state(false);
  /** La 3D dessine : sinon c'est le canvas 2D qu'on voit, pendant le chargement ou après une perte de contexte. */
  let live3d = $state(false);

  let box: HTMLDivElement;
  let flat: HTMLCanvasElement;
  let solid: HTMLCanvasElement | undefined = $state();
  let mounted = $state(false);
  let stage: Stage3D | null = null;
  let scene: EngineScene | ElectricScene | null = null;
  let sceneKey = '';
  let creating = false;
  let scenes: typeof import('../lib/engineScene') | null = null;
  let engineScene: EngineScene | null = null;
  let electricScene: ElectricScene | null = null;

  let currentKey = '';
  let lastConfig: EngineConfig | null = null;
  let layout: EngineLayout | null = null;
  let pose: EnginePose | null = null;
  let flatView: Engine2D | null = null;
  let labelsCache: Labels2D | null = null;
  let labelsLang = '';
  let labelsFuel = '';
  let overlayUsed = false;
  const projected = new Float64Array(3);
  let labelX = new Float64Array(16);
  let labelY = new Float64Array(16);
  let labelOrder = new Int32Array(16);
  // Pour les tests et le coût par image.
  const debug = { frames: 0, drawn: 0, skipped: 0, lastMs: 0, avgMs: 0, renderMs: 0, sparksDrawn: 0, flamesDrawn: 0, spraysDrawn: 0, mode: () => mode, live3d: () => live3d, stage: () => stage, scene: () => scene, pose: () => pose, layout: () => layout, flat: () => flatView };

  function ensureLayout() {
    // Le réglage ne change d'objet que s'il change vraiment : pas de texte à refaire à chaque image.
    if (config === lastConfig) return;
    lastConfig = config;
    const key = layoutKey(config);
    if (key === currentKey) return;
    currentKey = key;
    if (config.electric) {
      layout = null;
      pose = null;
      flatView = null;
      return;
    }
    layout = engineLayout(config);
    pose = new EnginePose(layout);
    flatView = new Engine2D(layout);
    labelsCache = null;
  }

  async function startStage() {
    if (stage || creating || !solid) return;
    creating = true;
    const canvas = solid;
    // Les pièces 3D ne se chargent, elles aussi, qu'à la première image 3D.
    const [created, built] = await Promise.all([
      createStage3D(canvas, {
        shadows: true,
        onLost: () => { live3d = false; },
        onRestored: () => { live3d = true; },
      }),
      import('../lib/engineScene').catch(() => null),
    ]);
    creating = false;
    scenes = built;
    if (!mounted || mode !== '3d' || solid !== canvas) {
      created?.dispose();
      return;
    }
    if (!created || !built) {
      created?.dispose();
      available = false;
      mode = '2d';
      return;
    }
    stage = created;
    live3d = true;
  }

  function stopStage() {
    scene?.dispose();
    scene = null;
    engineScene = null;
    electricScene = null;
    sceneKey = '';
    stage?.dispose();
    stage = null;
    live3d = false;
  }

  function setMode(next: '2d' | '3d') {
    if (next === '3d' && !available) return;
    mode = next;
    try {
      localStorage.setItem(MODE_KEY, next);
    } catch {
      // Le choix vaudra pour cette visite.
    }
    if (next === '2d') stopStage();
  }

  // Le canvas 3D n'existe qu'en mode 3D : le contexte part avec lui.
  $effect(() => {
    if (mode === '3d' && solid && mounted) void startStage();
  });

  function ensureScene(s: Stage3D) {
    if (sceneKey === currentKey && scene) return;
    if (!scenes) return;
    scene?.dispose();
    sceneKey = currentKey;
    engineScene = null;
    electricScene = null;
    if (config.electric || !layout) {
      scene = electricScene = new scenes.ElectricScene(s);
    } else {
      scene = engineScene = new scenes.EngineScene(s, layout);
    }
    s.frameOn(scene.center[0], scene.center[1], scene.center[2], scene.radius, { yaw: config.electric ? 0.8 : 0.72, pitch: 0.3 });
  }

  function labels2d(): Labels2D {
    // Relu à chaque image pour suivre la langue, mais refait seulement quand quelque chose change.
    if (labelsCache && labelsLang === lang() && labelsCache.parts === names && labelsFuel === config.fuel && !!labelsCache.compressor === !!layout?.turbo) return labelsCache;
    labelsLang = lang();
    labelsFuel = config.fuel;
    labelsCache = {
      front: t('engine.view.front'),
      // Un V ne montre qu'une rangée dans cette coupe : on le dit sur son titre.
      side: layout && layout.banks > 1 && !layout.flat ? `${t('engine.view.side')} · ${t('engine.view.bank')}` : t('engine.view.side'),
      parts: names,
      crank: t('engine.part.crank'),
      flywheel: t('engine.part.flywheel'),
      piston: t('engine.part.piston'),
      rod: t('engine.part.rod'),
      cam: t('engine.part.cam'),
      valve: t('engine.part.valve'),
      chain: t('engine.part.chain'),
      ignition: t(config.fuel === 'diesel' ? 'engine.part.glow' : 'engine.part.plug'),
      ring: t('engine.part.ring'),
      pan: t('engine.part.pan'),
      head: t('engine.part.head'),
      block: t('engine.part.block'),
      ...(layout?.turbo ? {
        compressor: t('engine.part.compressor'),
        turbine: t('engine.part.turbine'),
        shaft: t('engine.part.shaft'),
        intercooler: t('engine.part.intercooler'),
        airbox: t('engine.part.airbox'),
        maf: t('engine.part.maf'),
        chargeHot: t('engine.part.chargeHot'),
        chargeCold: t('engine.part.chargeCold'),
        blowoff: t('engine.part.blowoff'),
        wastegate: t('engine.part.wastegate'),
      } : {}),
    };
    return labelsCache;
  }

  function drawElectric(ctx: CanvasRenderingContext2D, theme: Theme, cx: number, cy: number, radius: number) {
    const rotor = motion.rotorAngle;
    ctx.strokeStyle = theme.lineStrong;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.stroke();
    for (let i = 0; i < 12; i++) {
      const a = (i / 12) * Math.PI * 2;
      const lit = ((Math.floor((rotor * 12) / (Math.PI * 2)) + i) % 3 === 0) && motion.load > 0.02;
      ctx.fillStyle = lit ? theme.cool : theme.raised;
      ctx.save();
      ctx.translate(cx, cy);
      ctx.rotate(a);
      ctx.fillRect(radius * 0.72, -radius * 0.08, radius * 0.24, radius * 0.16);
      ctx.restore();
    }
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(rotor);
    ctx.fillStyle = theme.metal;
    ctx.beginPath();
    ctx.arc(0, 0, radius * 0.6, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = theme.accent;
    ctx.lineWidth = 3;
    for (let i = 0; i < 4; i++) {
      ctx.rotate(Math.PI / 2);
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.lineTo(radius * 0.55, 0);
      ctx.stroke();
    }
    ctx.restore();
  }

  /**
   * Les noms des pièces, par-dessus la 3D : rangés de chaque côté, dans l'ordre de hauteur de ce qu'ils désignent,
   * pour que les traits ne se croisent pas. Seulement si le visiteur les demande.
   */
  function drawLabels3d(s: Stage3D, ctx: CanvasRenderingContext2D, theme: Theme, width: number, height: number) {
    const list = scene?.labels ?? [];
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.lineWidth = 1;
    ctx.textBaseline = 'middle';
    const count = list.length;
    if (labelX.length < count) {
      labelX = new Float64Array(count);
      labelY = new Float64Array(count);
      labelOrder = new Int32Array(count);
    }
    for (let k = 0; k < count; k++) {
      s.project(projected, list[k].x, list[k].y, list[k].z);
      labelX[k] = projected[0] * width;
      labelY[k] = projected[1] * height;
      labelOrder[k] = k;
    }
    // Un tri par insertion, sur place.
    for (let i = 1; i < count; i++) {
      const item = labelOrder[i];
      let j = i - 1;
      while (j >= 0 && labelY[labelOrder[j]] > labelY[item]) {
        labelOrder[j + 1] = labelOrder[j];
        j--;
      }
      labelOrder[j + 1] = item;
    }
    for (let sideIndex = 0; sideIndex < 2; sideIndex++) {
      const left = sideIndex === 0;
      let onSide = 0;
      for (let k = 0; k < count; k++) if (labelX[k] < width / 2 === left) onSide++;
      const top = 22;
      const step = onSide > 1 ? Math.min(30, (height - top - 14) / (onSide - 1)) : 0;
      let row = 0;
      for (let k = 0; k < count; k++) {
        const index = labelOrder[k];
        const px = labelX[index];
        if (px < width / 2 !== left) continue;
        const py = labelY[index];
        const y = top + (height - top - 14 - step * (onSide - 1)) / 2 + step * row;
        row++;
        const x = left ? 6 : width - 6;
        const text = t(list[index].key as MessageKey);
        const textWidth = ctx.measureText(text).width;
        const edge = left ? x + textWidth + 4 : x - textWidth - 4;
        ctx.strokeStyle = theme.dim;
        ctx.globalAlpha = 0.7;
        ctx.beginPath();
        ctx.moveTo(edge, y);
        ctx.lineTo(px, py);
        ctx.stroke();
        ctx.fillStyle = theme.surface;
        ctx.globalAlpha = 0.82;
        ctx.fillRect(left ? x - 3 : edge, y - 8, textWidth + 7, 16);
        ctx.globalAlpha = 1;
        ctx.fillStyle = theme.text;
        ctx.beginPath();
        ctx.arc(px, py, 2, 0, Math.PI * 2);
        ctx.fill();
        ctx.textAlign = left ? 'left' : 'right';
        ctx.fillText(text, x, y);
      }
    }
    ctx.textBaseline = 'alphabetic';
    ctx.textAlign = 'left';
  }

  function draw(seconds: number, now: number) {
    const theme = readTheme(box);
    motion.update(now, telemetry, config);
    // Le coût de cette vue seule : l'horloge et le gaz sont comptés à part, c'est la page qui les paie.
    const started = performance.now();
    ensureLayout();
    if (pose) pose.update(motion, telemetry, motion.gas.net, motion.gas.active, seconds);

    const showing3d = mode === '3d' && !!stage && !stage.lost;
    if (showing3d && stage && stage.busy()) {
      // Le GPU n'a pas fini : l'image précédente reste à l'écran, et la page garde son rythme.
      debug.skipped++;
    } else if (showing3d && stage) {
      debug.drawn++;
      ensureScene(stage);
      stage.setTheme(theme);
      if (engineScene && pose) engineScene.update(pose, motion, theme);
      else if (electricScene) electricScene.update(motion, theme);
      const rendered = stage.render();
      // Le canvas 2D sert alors aux noms : on ne l'efface que s'il a servi.
      if (rendered && (names || overlayUsed)) {
        const { ctx, width, height } = fit(flat);
        overlayUsed = names;
        if (names) drawLabels3d(stage, ctx, theme, width, height);
      }
      if (engineScene) {
        debug.sparksDrawn = engineScene.drawnSparks;
        debug.flamesDrawn = engineScene.drawnFlames;
        debug.spraysDrawn = engineScene.drawnSprays;
      }
    } else {
      overlayUsed = true;
      const { ctx, width, height } = fit(flat);
      if (config.electric) {
        drawElectric(ctx, theme, width / 2, height / 2, Math.min(width, height) * 0.4);
      } else if (flatView && pose) {
        flatView.draw(ctx, width, height, pose, motion.painter, theme, labels2d());
      }
    }
    debug.lastMs = performance.now() - started;
    debug.avgMs += (debug.lastMs - debug.avgMs) * 0.05;
    debug.frames++;
  }

  let unsubscribe: (() => void) | null = null;

  onMount(() => {
    mounted = true;
    (box as unknown as { __engine: typeof debug }).__engine = debug;
    if (!probeWebGL2().available) {
      available = false;
      mode = '2d';
    } else {
      mode = rememberedMode() ?? '3d';
    }
    unsubscribe = animate(box, draw);
  });

  onDestroy(() => {
    mounted = false;
    unsubscribe?.();
    stopStage();
  });

  const order = $derived(firingOrder(config.cylinders));
  const crankNote = $derived.by(() => {
    if (config.electric) return '';
    const shape = engineLayout(config);
    const parts = [t('engine.caption.crank', { angles: shape.throwAngles.map((value) => `${value}°`).join(' ') })];
    if (shape.splitDeg > 0) parts.push(t('engine.caption.split', { angle: shape.splitDeg }));
    if (shape.strokeClamped) parts.push(t('engine.caption.clamped'));
    return parts.join(' · ');
  });
  const caption = $derived(
    config.electric
      ? t('engine.caption.electric', { slow: speedLabel(slow) })
      : t('engine.caption', { cylinders: config.cylinders, layout: t(`engine.layout.${config.layout}`), bore: config.boreMm, stroke: Math.round(strokeMm(config)), order: order.join('-'), slow: speedLabel(slow) }) + ` · ${crankNote}`,
  );
  const stateKey = $derived(`engine.state.${engineState(telemetry)}` as MessageKey);
</script>

<figure>
  <div class="tools">
    <div class="modes" role="group" aria-label={t('engine.view.mode')}>
      <button type="button" aria-pressed={mode === '3d'} disabled={!available} onclick={() => setMode('3d')} data-testid="engine-mode-3d">{t('engine.view.3d')}</button>
      <button type="button" aria-pressed={mode === '2d'} onclick={() => setMode('2d')} data-testid="engine-mode-2d">{t('engine.view.2d')}</button>
    </div>
    {#if mode === '3d' && live3d}
      <button type="button" class="icon" aria-label={t('engine.view.zoomOut')} title={t('engine.view.zoomOut')} onclick={() => stage?.zoomBy(1 / 1.25)}>−</button>
      <button type="button" class="icon" aria-label={t('engine.view.zoomIn')} title={t('engine.view.zoomIn')} onclick={() => stage?.zoomBy(1.25)}>+</button>
      <button type="button" onclick={() => stage?.resetView()} data-testid="engine-reset">{t('engine.view.reset')}</button>
    {/if}
    <button type="button" aria-pressed={names} onclick={() => { names = !names; }} data-testid="engine-names">{t('engine.view.labels')}</button>
  </div>
  <div class="stage" class:three={mode === '3d'} class:two={mode === '2d'} class:fallback={mode === '3d' && !live3d} aria-label={t('engine.aria', { caption })} role="group" data-testid="engine-view" bind:this={box}>
    {#if !config.electric}
      <span class="state" data-testid="engine-state">{t(stateKey)}</span>
    {/if}
    <canvas class="flat" class:overlay={live3d} bind:this={flat}></canvas>
    {#if mode === '3d'}
      <canvas class="solid" class:hidden={!live3d} bind:this={solid} tabindex="0" aria-label={t('engine.view.orbit')}></canvas>
    {/if}
  </div>
  {#if !available}
    <p class="note" data-testid="engine-unavailable">{t('engine.view.unavailable')}</p>
  {/if}
  <figcaption>{caption}</figcaption>
</figure>

<style>
  figure { margin: 0; container-type: inline-size; }
  /* Assez large, les deux coupes se mettent côte à côte (`engine2d.ts`) : moins de hauteur suffit. */
  .tools { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; margin-bottom: 6px; }
  .tools button { min-height: 32px; padding: 0 10px; border: 0; background: var(--surface-raised); color: var(--text-dim); font: inherit; font-size: 0.8125rem; cursor: pointer; }
  .tools button:hover:not(:disabled) { background: var(--surface-high); }
  .tools button:disabled { opacity: 0.45; cursor: default; }
  .tools button[aria-pressed='true'], .tools button[aria-pressed='true']:hover:not(:disabled) { background: var(--control); color: var(--on-control); font-weight: 600; }
  .tools .icon { width: 32px; padding: 0; font-size: 0.95rem; }
  .modes { display: inline-flex; gap: 2px; }
  .state { position: absolute; top: 0; right: 2px; z-index: 2; color: var(--text-dim); font-size: 0.76rem; pointer-events: none; }
  .stage { position: relative; width: 100%; height: var(--view-h, 400px); }
  .stage.two { height: max(var(--view-h, 560px), 560px); }
  .stage.fallback { height: var(--view-h, 400px); }
  @container (min-width: 620px) { .stage.two { height: var(--view-h, 430px); } }
  canvas { display: block; width: 100%; height: 100%; }
  .flat { position: absolute; inset: 0; }
  .flat.overlay { pointer-events: none; z-index: 1; }
  .solid { position: absolute; inset: 0; cursor: grab; outline-offset: -2px; }
  .solid:active { cursor: grabbing; }
  .solid.hidden { visibility: hidden; }
  .note { margin: 4px 0 0; color: var(--text-dim); font-size: 0.78rem; }
  figcaption { margin-top: 6px; color: var(--text-dim); font-size: 0.8rem; font-variant-numeric: tabular-nums; }
</style>
