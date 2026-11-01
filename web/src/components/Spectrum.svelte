<script lang="ts">
  import { onMount } from 'svelte';
  import { animate, fit, readTheme } from '../lib/canvas';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  interface Props {
    analyser: AnalyserNode | null;
    running: boolean;
  }
  let { analyser, running }: Props = $props();

  let canvas: HTMLCanvasElement;
  const MIN_HZ = 30;
  const MAX_HZ = 16000;
  const FLOOR_DB = -100;
  const CEIL_DB = -20;
  const BARS = 72;
  const LABELS: { hz: number; text: string }[] = [
    { hz: 100, text: '100 Hz' },
    { hz: 1000, text: '1 kHz' },
    { hz: 10000, text: '10 kHz' },
  ];
  const LEVELS = [-40, -60, -80];
  const LEFT = 34;
  const BOTTOM = 18;
  // Le dernier spectre lu, en dB, par bande : la lecture au survol y puise. Une seule allocation, à la première image.
  const bands = new Float32Array(BARS).fill(FLOOR_DB);
  let data: Float32Array<ArrayBuffer> | null = null;
  let hover = $state<{ hz: number; db: number; share: number } | null>(null);

  const hzOf = (share: number) => MIN_HZ * (MAX_HZ / MIN_HZ) ** share;

  function draw() {
    const theme = readTheme(canvas);
    const { ctx, width, height } = fit(canvas);
    const plotWidth = width - LEFT;
    const plotHeight = height - BOTTOM;
    const xOf = (hz: number) => LEFT + (Math.log(hz / MIN_HZ) / Math.log(MAX_HZ / MIN_HZ)) * plotWidth;
    const yOf = (db: number) => (1 - (db - FLOOR_DB) / (CEIL_DB - FLOOR_DB)) * plotHeight;

    ctx.font = '11px "Inter Tight", system-ui, sans-serif';
    ctx.lineWidth = 1;
    ctx.strokeStyle = theme.line;
    ctx.fillStyle = theme.dim;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let i = 0; i < LEVELS.length; i++) {
      const y = Math.round(yOf(LEVELS[i])) + 0.5;
      ctx.beginPath();
      ctx.moveTo(LEFT, y);
      ctx.lineTo(width, y);
      ctx.stroke();
      ctx.fillText(String(LEVELS[i]), LEFT - 6, y);
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    for (let i = 0; i < LABELS.length; i++) ctx.fillText(LABELS[i].text, xOf(LABELS[i].hz), height - 4);

    if (!analyser || !running) return;
    if (!data || data.length !== analyser.frequencyBinCount) data = new Float32Array(analyser.frequencyBinCount);
    analyser.getFloatFrequencyData(data);
    const binHz = analyser.context.sampleRate / analyser.fftSize;
    const barWidth = plotWidth / BARS;
    ctx.fillStyle = theme.accent;
    for (let i = 0; i < BARS; i++) {
      const low = hzOf(i / BARS);
      const high = hzOf((i + 1) / BARS);
      const first = Math.max(1, Math.floor(low / binHz));
      const last = Math.max(first + 1, Math.ceil(high / binHz));
      let top = -Infinity;
      for (let bin = first; bin < last && bin < data.length; bin++) top = Math.max(top, data[bin]);
      bands[i] = top;
      const level = Math.max(0, Math.min(1, (top - FLOOR_DB) / (CEIL_DB - FLOOR_DB)));
      const barHeight = level * plotHeight;
      if (barHeight < 0.5) continue;
      ctx.globalAlpha = 0.35 + 0.65 * level;
      ctx.fillRect(LEFT + i * barWidth + 1, plotHeight - barHeight, Math.max(1, barWidth - 2), barHeight);
    }
    ctx.globalAlpha = 1;
    const shown = hover;
    if (shown) {
      ctx.strokeStyle = theme.text;
      const x = Math.round(LEFT + shown.share * plotWidth) + 0.5;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, plotHeight);
      ctx.stroke();
    }
  }

  function onMove(event: PointerEvent) {
    const box = canvas.getBoundingClientRect();
    const share = Math.max(0, Math.min(0.999, (event.clientX - box.left - LEFT) / (box.width - LEFT)));
    hover = { hz: hzOf(share), db: bands[Math.floor(share * BARS)], share };
  }

  onMount(() => animate(canvas, draw));
</script>

<div class="spectrum" role="img" aria-label={t('spectrum.aria')} data-testid="spectrum">
  <canvas bind:this={canvas} onpointermove={onMove} onpointerleave={() => (hover = null)}></canvas>
  {#if hover && hover.db > FLOOR_DB}
    <p class="tip" data-testid="spectrum-tip"><strong>{fixed(hover.db, 0)} dB</strong> {whole(hover.hz)} Hz</p>
  {/if}
</div>

<style>
  .spectrum { position: relative; flex: 1; min-height: 180px; }
  /* Absolu : le canvas suit la carte sans jamais la pousser. */
  canvas { position: absolute; inset: 0; display: block; width: 100%; height: 100%; touch-action: pan-y; }
  .tip { position: absolute; z-index: 1; top: 0; right: 0; margin: 0; padding: 2px 8px; background: var(--surface-high); color: var(--text-dim); font-size: var(--type-small); font-variant-numeric: tabular-nums; pointer-events: none; }
  .tip strong { color: var(--text); font-weight: 600; }
</style>
