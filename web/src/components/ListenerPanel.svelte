<script lang="ts">
  import { onMount } from 'svelte';
  import Choices from './Choices.svelte';
  import { animate, fit, readTheme } from '../lib/canvas';
  import { heatColors } from '../lib/heat';
  import type { EngineConfig } from '../lib/config';
  import { Listener, type CarPlan, type Engine, type ListenerValue, type SoundSheet, type Telemetry } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';
  import { nearestLandmark } from '../lib/instruments';
  import type { Motion } from '../lib/motion';

  /** Où l'on écoute : une position toute prête, ou un point du plan. Le WAV exporté part de la même. */
  export type Place = ListenerValue | { x: number; y: number; z: number };

  const OUTLET = heatColors(750).fill;

  interface Props {
    /** Le réglage, pendant un glissé compris : pour le dessin. */
    config: EngineConfig;
    /** Le réglage validé : la fiche se recalcule seulement à ce moment. */
    settled: EngineConfig;
    telemetry: Telemetry;
    motion: Motion;
    engine: Engine;
    loaded: boolean;
    running: boolean;
    place: Place;
  }
  let { config, settled, telemetry, motion, engine, loaded, running, place = $bindable() }: Props = $props();

  const POSITIONS: { value: ListenerValue | -1; label: string }[] = [
    { value: Listener.Outside, get label() { return t('listener.pos.behind'); } },
    { value: Listener.Front, get label() { return t('listener.pos.front'); } },
    { value: Listener.Side, get label() { return t('listener.pos.side'); } },
    { value: Listener.Far, get label() { return t('listener.pos.far'); } },
    { value: Listener.Pipe, get label() { return t('listener.pos.pipe'); } },
    { value: Listener.Cabin, get label() { return t('listener.pos.cabin'); } },
    { value: Listener.EngineBay, get label() { return t('listener.pos.bay'); } },
  ];
  const WINDOWS: { value: 'closed' | 'open'; label: string }[] = [
    { value: 'closed', get label() { return t('listener.windows.closed'); } },
    { value: 'open', get label() { return t('listener.windows.open'); } },
  ];
  const PLAN_HEIGHT_FAR = 1.6;
  /** Sur le plan, la distance se resserre en s'éloignant de la voiture, qui reste à l'échelle. */
  const COMPRESS_M = 1.2;
  const MAX_DISTANCE_M = 45;

  let windows = $state<'closed' | 'open'>('closed');
  let canvas: HTMLCanvasElement;
  let dragging = $state(false);
  let pointerAt = $state<{ x: number; y: number } | null>(null);

  const plan = $derived<CarPlan | null>(loaded ? engine.plan(config) : null);
  // La fiche demande de jouer le son quelques instants : seulement une fois le réglage posé.
  let sheet = $state<SoundSheet | null>(null);
  let sheetTimer: ReturnType<typeof setTimeout> | undefined;
  $effect(() => {
    const next = settled;
    if (!loaded) return;
    clearTimeout(sheetTimer);
    sheetTimer = setTimeout(() => (sheet = engine.soundSheet(next)), 80);
  });

  // Le moteur reçoit ce qu'on a choisi avant de démarrer, puis chaque changement.
  $effect(() => {
    if (!running) return;
    const chosen = place;
    if (typeof chosen === 'number') engine.setListener(chosen);
    else engine.setListenerPosition(chosen.x, chosen.y, chosen.z);
  });
  $effect(() => {
    if (running) engine.setWindows(windows === 'open');
  });

  const inside = $derived(plan ? insideCabin(telemetry.listenerX, telemetry.listenerY) : false);
  const head = $derived(pointerAt ?? (running ? { x: telemetry.listenerX, y: telemetry.listenerY } : presetPoint(place)));
  const db = $derived(running && telemetry.levelDbA > 5 ? telemetry.levelDbA : 0);
  const landmark = $derived(db > 0 ? nearestLandmark(db) : null);
  const danger = $derived(db >= 120 ? t('listener.danger.120') : db >= 100 ? t('listener.danger.100') : db >= 85 ? t('listener.danger.85') : '');

  function insideCabin(x: number, y: number): boolean {
    if (!plan) return false;
    return Math.abs(y) < plan.width / 2 && x > plan.seat.x - 0.95 && x < plan.seat.x + 0.95;
  }

  /** Où serait la tête pour une position toute prête, avant que le moteur ne tourne. */
  function presetPoint(chosen: Place): { x: number; y: number } {
    if (typeof chosen !== 'number') return chosen;
    if (!plan) return { x: -7, y: -1 };
    const outlet = plan.sources[0];
    switch (chosen) {
      case Listener.Cabin: return plan.seat;
      case Listener.EngineBay: return { x: plan.engineX, y: 0.1 };
      case Listener.Front: return { x: plan.length / 2 + 3, y: 0.5 };
      case Listener.Side: return { x: 0, y: -5 };
      case Listener.Far: return { x: -20, y: -24 };
      case Listener.Pipe: return { x: outlet.x - 0.14, y: outlet.y + (outlet.y >= 0 ? 0.1 : -0.1) };
      default: return { x: -plan.length / 2 - 5, y: -1 };
    }
  }

  /** La hauteur de la tête suit le plan : debout dehors, assise dans l'habitacle, baissée vers ce qu'on écoute de près. */
  function heightAt(x: number, y: number): number {
    if (!plan) return PLAN_HEIGHT_FAR;
    if (insideCabin(x, y)) return plan.seatZ;
    const toEngine = Math.hypot(x - plan.engineX, y);
    if (toEngine < 0.8) return plan.engineZ + 0.55;
    let height = PLAN_HEIGHT_FAR;
    for (const outlet of plan.sources.slice(0, 2)) {
      const distance = Math.hypot(x - outlet.x, y - outlet.y);
      const t = Math.min(Math.max((distance - 0.12) / 1.1, 0), 1);
      height = Math.min(height, outlet.z + 0.02 + (PLAN_HEIGHT_FAR - outlet.z - 0.02) * t * t * (3 - 2 * t));
    }
    return height;
  }

  // ── Le plan : la voiture à l'échelle, et autour d'elle la distance resserrée ──

  function geometry(width: number, height: number) {
    const margin = COMPRESS_M * Math.log(1 + MAX_DISTANCE_M / COMPRESS_M);
    const length = plan?.length ?? 4.3;
    const wide = plan?.width ?? 1.8;
    const scale = Math.min((width / 2 - 6) / (length / 2 + margin), (height / 2 - 6) / (wide / 2 + margin));
    return { scale, cx: width / 2, cy: height / 2, halfL: length / 2, halfW: wide / 2 };
  }

  /** Du mètre au point de l'écran. Avant de la voiture à droite, comme sur la vue de profil. Sa gauche est en haut. */
  function toScreen(x: number, y: number, g: ReturnType<typeof geometry>): [number, number] {
    const qx = Math.max(-g.halfL, Math.min(g.halfL, x));
    const qy = Math.max(-g.halfW, Math.min(g.halfW, y));
    const dx = x - qx;
    const dy = y - qy;
    const d = Math.hypot(dx, dy);
    const s = d > 0 ? COMPRESS_M * Math.log(1 + d / COMPRESS_M) / d : 0;
    return [g.cx + (qx + dx * s) * g.scale, g.cy - (qy + dy * s) * g.scale];
  }

  /** L'inverse : d'un point de l'écran au mètre. */
  function fromScreen(px: number, py: number, g: ReturnType<typeof geometry>): { x: number; y: number } {
    const sx = (px - g.cx) / g.scale;
    const sy = -(py - g.cy) / g.scale;
    const qx = Math.max(-g.halfL, Math.min(g.halfL, sx));
    const qy = Math.max(-g.halfW, Math.min(g.halfW, sy));
    const dx = sx - qx;
    const dy = sy - qy;
    const reach = Math.hypot(dx, dy);
    if (reach < 1e-4) return { x: sx, y: sy };
    const d = Math.min(COMPRESS_M * (Math.exp(reach / COMPRESS_M) - 1), MAX_DISTANCE_M);
    return { x: qx + (dx / reach) * d, y: qy + (dy / reach) * d };
  }

  function draw(_seconds: number, now: number) {
    const theme = readTheme(canvas);
    const { ctx, width, height } = fit(canvas);
    motion.update(now, telemetry, config);
    const g = geometry(width, height);

    // Courbes de même distance : elles entourent la voiture.
    ctx.lineWidth = 1;
    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const metres of [1, 5, 20]) {
      const reach = COMPRESS_M * Math.log(1 + metres / COMPRESS_M) * g.scale;
      const [x0, y0] = toScreen(-g.halfL, g.halfW, g);
      const [x1, y1] = toScreen(g.halfL, -g.halfW, g);
      ctx.strokeStyle = theme.line;
      ctx.beginPath();
      ctx.roundRect(x0 - reach, y0 - reach, x1 - x0 + 2 * reach, y1 - y0 + 2 * reach, reach);
      ctx.stroke();
      ctx.fillStyle = theme.dim;
      // Au-dessus de sa courbe, ou juste dessous si elle touche le haut : sur un téléphone, les côtés se serrent.
      const above = y0 - reach - 7;
      ctx.fillText(`${metres} m`, (x0 + x1) / 2, above < 8 ? y0 - reach + 8 : above);
    }

    // La voiture.
    const [left, top] = toScreen(-g.halfL, g.halfW, g);
    const [right, bottom] = toScreen(g.halfL, -g.halfW, g);
    ctx.fillStyle = theme.raised;
    ctx.strokeStyle = theme.text;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(left, top, right - left, bottom - top, 9);
    ctx.fill();
    ctx.stroke();

    if (plan) {
      // Son habitacle, son siège.
      const [cabinA] = toScreen(plan.seat.x - 0.95, 0, g);
      const [cabinB] = toScreen(plan.seat.x + 0.95, 0, g);
      ctx.fillStyle = theme.surface;
      ctx.strokeStyle = theme.lineStrong;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.roundRect(cabinA, top + 3, cabinB - cabinA, bottom - top - 6, 6);
      ctx.fill();
      ctx.stroke();

      // Le moteur.
      if (!config.electric) {
        const [ex] = toScreen(plan.engineX, 0, g);
        const blockHalf = 0.36 * g.scale;
        ctx.fillStyle = theme.metal;
        ctx.globalAlpha = 0.85;
        ctx.fillRect(ex - blockHalf, g.cy - blockHalf * 0.85, blockHalf * 2, blockHalf * 1.7);
        ctx.globalAlpha = 1;
      }

      // Les sorties d'échappement, et l'onde qui en part au rythme du moteur (même horloge que les autres vues).
      const ripple = (motion.turns * Math.max(config.cylinders / 2, 1)) % 1;
      const outlets = config.electric ? [] : config.routing === 'single' ? plan.sources.slice(0, 1) : plan.sources.slice(0, 2);
      for (const outlet of outlets) {
        const [ox, oy] = toScreen(outlet.x, outlet.y, g);
        // La légende du plan dit « en orange » : cette couleur-là ne suit pas la chaleur.
        ctx.fillStyle = OUTLET;
        ctx.beginPath();
        ctx.arc(ox, oy, 3, 0, Math.PI * 2);
        ctx.fill();
        if (telemetry.rpm > 100) {
          ctx.strokeStyle = OUTLET;
          ctx.lineWidth = 1;
          ctx.globalAlpha = 0.5 * (1 - ripple);
          ctx.beginPath();
          ctx.arc(ox, oy, 4 + ripple * (10 + 16 * motion.load), Math.PI * 0.55, Math.PI * 1.45);
          ctx.stroke();
          ctx.globalAlpha = 1;
        }
      }
      // L'admission.
      if (!config.electric) {
        const [ix, iy] = toScreen(plan.sources[2].x, plan.sources[2].y, g);
        ctx.fillStyle = theme.cool;
        ctx.beginPath();
        ctx.arc(ix, iy, 2.5, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // L'auditeur : une tête, deux oreilles.
    const [hx, hy] = toScreen(head.x, head.y, g);
    const facing = insideCabin(head.x, head.y) ? 0 : Math.atan2(head.y, head.x) + Math.PI;
    ctx.save();
    ctx.translate(hx, hy);
    ctx.rotate(-facing);
    ctx.fillStyle = dragging ? theme.accent : theme.text;
    ctx.beginPath();
    ctx.arc(0, -8, 2.2, 0, Math.PI * 2);
    ctx.arc(0, 8, 2.2, 0, Math.PI * 2);
    ctx.fill();
    ctx.beginPath();
    ctx.arc(0, 0, 7.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = theme.surface;
    ctx.beginPath();
    ctx.moveTo(9, 0);
    ctx.lineTo(3, -3.5);
    ctx.lineTo(3, 3.5);
    ctx.fill();
    ctx.restore();
  }

  function pointerPlace(event: PointerEvent): { x: number; y: number } {
    const box = canvas.getBoundingClientRect();
    return fromScreen(event.clientX - box.left, event.clientY - box.top, geometry(box.width, box.height));
  }

  function setFromPointer(event: PointerEvent) {
    const at = pointerPlace(event);
    pointerAt = at;
    place = { x: at.x, y: at.y, z: heightAt(at.x, at.y) };
  }

  function onPointerDown(event: PointerEvent) {
    canvas.setPointerCapture(event.pointerId);
    dragging = true;
    setFromPointer(event);
  }

  function onPointerMove(event: PointerEvent) {
    if (dragging) setFromPointer(event);
  }

  function onPointerUp() {
    dragging = false;
    pointerAt = null;
  }

  onMount(() => animate(canvas, draw));

  /** Le nom de l'endroit, pour celui qui ne voit pas le plan. */
  const where = $derived.by(() => {
    if (!plan) return '';
    const { x, y } = head;
    if (insideCabin(x, y)) return t('listener.where.cabin');
    const qx = Math.max(-plan.length / 2, Math.min(plan.length / 2, x));
    const qy = Math.max(-plan.width / 2, Math.min(plan.width / 2, y));
    const distance = Math.hypot(x - qx, y - qy);
    const engineNear = Math.hypot(x - plan.engineX, y) < 0.9;
    if (engineNear && distance < 0.5) return t('listener.where.bay');
    const outletNear = Math.min(...plan.sources.slice(0, 2).map((o) => Math.hypot(x - o.x, y - o.y)));
    if (!config.electric && outletNear < 0.35) return t('listener.where.outlet', { cm: Math.round(outletNear * 100) });
    const side = t(x > plan.length / 2 ? 'listener.where.front' : x < -plan.length / 2 ? 'listener.where.behind' : 'listener.where.side');
    return t('listener.where.distance', { side, metres: distance < 10 ? fixed(distance, 1) : Math.round(distance) });
  });

  const shown = (value: number | undefined) => (value === undefined ? '…' : `${Math.round(value)} dB(A)`);
</script>

<div class="panel">
  <div class="map">
    <div class="plan" role="application" aria-label={t('listener.map.aria', { where })} data-testid="listener-map">
      <canvas bind:this={canvas} onpointerdown={onPointerDown} onpointermove={onPointerMove} onpointerup={onPointerUp} onpointercancel={onPointerUp} class:dragging></canvas>
    </div>
    <p class="help">{t('listener.map.help')}</p>
    <Choices label={t('listener.position')} options={POSITIONS} value={typeof place === 'number' ? place : -1} onchange={(value) => { place = value as ListenerValue; }} />
  </div>

  <div class="readout">
    <p class="where" data-testid="listener-where">{where}</p>
    <p class="level" aria-live="off"><strong data-testid="level-dba">{db > 0 ? `≈ ${Math.round(db)}` : '—'}</strong> dB(A)</p>
    <p class="help">{t('listener.estimate')} {#if landmark}{t('listener.like', { landmark: t(`listener.landmark.${landmark}`) })}{/if}</p>
    {#if danger}<p class="help warn">{danger}</p>{/if}
    {#if inside}
      <Choices label={t('listener.windows')} options={WINDOWS} value={windows} onchange={(value) => (windows = value)} />
    {/if}

    {#if sheet && !config.electric}
      <h3>{t('listener.sheet')}</h3>
      <dl class="sheet" data-testid="sound-sheet">
        <div><dt>{t('listener.sheet.standstill')}</dt><dd data-testid="sheet-standstill">{shown(sheet.standstill)}</dd></div>
        <div><dt>{t('listener.sheet.outside')}</dt><dd data-testid="sheet-outside">{shown(sheet.outsideFull)}</dd></div>
        <div><dt>{t('listener.sheet.cabinIdle')}</dt><dd data-testid="sheet-cabin-idle">{shown(sheet.cabinIdle)}</dd></div>
        <div><dt>{t('listener.sheet.cabinFull')}</dt><dd data-testid="sheet-cabin-full">{shown(sheet.cabinFull)}</dd></div>
      </dl>
      <div class="loss" role="img" aria-label={t('listener.loss.aria')} data-testid="line-loss">
        {#each sheet.lineLoss as band (band.hz)}
          <div class="band" title={t('listener.loss.band', { db: Math.round(band.db), hz: band.hz })}>
            <span class="bar" style="height: {Math.min(Math.max(band.db, 0), 60) / 60 * 100}%"></span>
            <small>{band.hz >= 1000 ? `${band.hz / 1000}k` : band.hz}</small>
          </div>
        {/each}
        <p class="help">{t('listener.loss.help')}</p>
      </div>
    {/if}
  </div>
</div>

<style>
  .panel { display: flex; flex-direction: column; gap: 16px; }
  .map { display: flex; flex-direction: column; gap: 10px; min-width: 0; }
  canvas { display: block; width: 100%; height: 280px; touch-action: none; cursor: grab; background: var(--bg); }
  canvas.dragging { cursor: grabbing; }
  .readout { display: flex; flex-direction: column; gap: 8px; min-width: 0; }
  .where { margin: 0; color: var(--text-dim); font-size: 0.9rem; }
  .level { margin: 0; font-size: 1rem; font-variant-numeric: tabular-nums; }
  .level strong { font-family: var(--display); font-size: 2.75rem; font-weight: 800; line-height: 1; }
  .help { margin: 0; color: var(--text-dim); font-size: 0.82rem; }
  .warn { color: var(--text); }
  .warn::before { content: '!'; display: inline-block; width: 1.2em; margin-right: 4px; background: var(--warn); color: var(--on-warn); font-weight: 700; text-align: center; }
  h3 { margin: 6px 0 0; color: var(--text-dim); font-size: 0.85rem; font-weight: 400; }
  .sheet { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px 14px; margin: 0; }
  dt { color: var(--text-dim); font-size: 0.78rem; }
  dd { margin: 0; font-weight: 600; font-variant-numeric: tabular-nums; }
  .loss { display: flex; flex-wrap: wrap; align-items: flex-end; gap: 4px; height: auto; }
  .band { position: relative; display: flex; flex-direction: column; justify-content: flex-end; align-items: center; flex: 1 1 0; height: 54px; min-width: 22px; }
  .bar { display: block; width: 62%; background: var(--heat-text); }
  .band small { color: var(--text-dim); font-size: 0.68rem; }
  .loss .help { flex: 1 0 100%; }
</style>
