<script lang="ts">
  import { onMount } from 'svelte';
  import { animate, readTheme, type Theme } from '../lib/canvas';
  import type { Engine } from '../lib/engine';
  import { Fft } from '../lib/fft';
  import type { TelemetryHistory } from '../lib/history';
  import { fixed, t, whole as number } from '../lib/i18n.svelte';

  interface Props {
    engine: Engine;
    running: boolean;
    frozen: boolean;
    history: TelemetryHistory;
  }
  let { engine, running, frozen, history }: Props = $props();

  /** Fenêtre courte : une raie qui ondule s'étale et disparaît dans une fenêtre longue. */
  const SIZE = 2048;
  const MIN_HZ = 30;
  const MAX_HZ = 16000;
  const FLOOR_DB = -100;
  const CEIL_DB = -20;
  const SECONDS = 10;
  const TICKS = [100, 1000, 10000];
  const LEFT = 42;
  const BOTTOM = 16;

  let canvas: HTMLCanvasElement;
  let root: HTMLElement;
  let hover = $state<{ x: number; y: number; hz: number; db: number; ago: number } | null>(null);
  let peaks = $state<{ hz: number; db: number }[]>([]);

  const fft = new Fft(SIZE);
  const window_ = Float32Array.from({ length: SIZE }, (_, i) => 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (SIZE - 1)));
  const re = new Float32Array(SIZE);
  const im = new Float32Array(SIZE);
  const time = new Float32Array(4096);
  const spectrum = new Float32Array(SIZE / 2 + 1);

  let plotW = 0;
  let plotH = 0;
  let levels = new Float32Array(0);
  let binOf = new Float32Array(0);
  let image: ImageData | null = null;
  let image1: ImageData | null = null;
  let ramp = new Uint8ClampedArray(0);
  let rampFor: Theme | null = null;
  let scratch: HTMLCanvasElement | null = null;
  let scratchContext: CanvasRenderingContext2D | null = null;
  let writeAt = 0;
  let pending = 0;
  let ctx: CanvasRenderingContext2D | null = null;
  let ratio = 1;
  let rate = 48000;

  /** Une seule teinte chaude, du fond vers le plus intense : le niveau, rien d'autre. */
  function buildRamp(theme: Theme) {
    rampFor = theme;
    const dark = parseInt(theme.surface.slice(1, 3), 16) < 128;
    const stops = dark
      ? [[21, 17, 14], [92, 42, 20], [227, 111, 43], [255, 224, 176]]
      : [[252, 252, 251], [245, 201, 166], [201, 90, 18], [74, 28, 3]];
    ramp = new Uint8ClampedArray(256 * 3);
    for (let i = 0; i < 256; i++) {
      const t = (i / 255) * (stops.length - 1);
      const k = Math.min(Math.floor(t), stops.length - 2);
      const f = t - k;
      for (let c = 0; c < 3; c++) ramp[i * 3 + c] = stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f;
    }
  }

  function mapFrequencies() {
    // Le taux n'est connu qu'une fois le son démarré.
    rate = engine.analyser?.context.sampleRate ?? 48000;
    const binHz = rate / SIZE;
    for (let y = 0; y < plotH; y++) binOf[y] = (MIN_HZ * (MAX_HZ / MIN_HZ) ** (1 - y / (plotH - 1))) / binHz;
  }

  function resize(width: number, height: number) {
    plotW = Math.max(16, Math.round((width - LEFT) * ratio));
    plotH = Math.max(16, Math.round((height - BOTTOM) * ratio));
    levels = new Float32Array(plotW * plotH).fill(FLOOR_DB);
    binOf = new Float32Array(plotH);
    mapFrequencies();
    if (!scratch) {
      scratch = document.createElement('canvas');
      scratchContext = scratch.getContext('2d');
    }
    scratch.width = plotW;
    scratch.height = plotH;
    image = scratchContext?.createImageData(plotW, plotH) ?? null;
    image1 = scratchContext?.createImageData(1, plotH) ?? null;
    scratchContext?.clearRect(0, 0, plotW, plotH);
    writeAt = 0;
  }

  function capture(): boolean {
    const analyser = engine.analyser;
    if (!analyser) return false;
    const samples = time.length <= analyser.fftSize ? time : time.subarray(0, analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    const offset = samples.length - SIZE;
    if (offset < 0) return false;
    for (let i = 0; i < SIZE; i++) {
      re[i] = samples[offset + i] * window_[i];
      im[i] = 0;
    }
    fft.transform(re, im);
    // Un sinus d'amplitude 1 vaut 0 dB : la fenêtre de Hann a un gain moyen de 0,5.
    const scale = 2 / (SIZE * 0.5);
    for (let k = 0; k <= SIZE / 2; k++) {
      const m = Math.sqrt(re[k] * re[k] + im[k] * im[k]) * scale;
      spectrum[k] = 20 * Math.log10(m + 1e-7);
    }
    return true;
  }

  function writeColumn(count: number) {
    if (!image1 || !scratchContext) return;
    const data = image1.data;
    const column = writeAt;
    for (let y = 0; y < plotH; y++) {
      const b = binOf[y];
      const i = b | 0;
      const f = b - i;
      const db = spectrum[i] + (spectrum[Math.min(i + 1, SIZE / 2)] - spectrum[i]) * f;
      levels[y * plotW + column] = db;
      const norm = (db - FLOOR_DB) / (CEIL_DB - FLOOR_DB);
      const level = norm <= 0 ? 0 : norm >= 1 ? 255 : (norm * 255) | 0;
      const out = y * 4;
      data[out] = ramp[level * 3];
      data[out + 1] = ramp[level * 3 + 1];
      data[out + 2] = ramp[level * 3 + 2];
      data[out + 3] = 255;
    }
    for (let c = 0; c < count; c++) {
      scratchContext.putImageData(image1, (writeAt + c) % plotW, 0);
      if (c > 0) {
        for (let y = 0; y < plotH; y++) levels[y * plotW + (writeAt + c) % plotW] = levels[y * plotW + column];
      }
    }
    writeAt = (writeAt + count) % plotW;
  }

  function paint(theme: Theme, width: number, height: number) {
    if (!ctx || !scratch) return;
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    ctx.clearRect(0, 0, width, height);
    const plotCssW = plotW / ratio;
    const plotCssH = plotH / ratio;
    // Le tampon est circulaire : la colonne la plus récente arrive toujours à droite.
    ctx.imageSmoothingEnabled = false;
    const first = plotW - writeAt;
    ctx.drawImage(scratch, writeAt, 0, first, plotH, LEFT, 0, first / ratio, plotCssH);
    if (writeAt > 0) ctx.drawImage(scratch, 0, 0, writeAt, plotH, LEFT + first / ratio, 0, writeAt / ratio, plotCssH);

    ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
    ctx.fillStyle = theme.dim;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    for (const hz of TICKS) {
      const y = (1 - Math.log(hz / MIN_HZ) / Math.log(MAX_HZ / MIN_HZ)) * plotCssH;
      ctx.fillText(hz >= 1000 ? `${hz / 1000} kHz` : `${hz} Hz`, LEFT - 6, y);
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    for (let i = 0; i <= 2; i++) {
      const seconds = (SECONDS * i) / 2;
      ctx.fillText(i === 0 ? t('common.now') : `−${seconds} s`, Math.min(LEFT + plotCssW * (1 - i / 2), width - 26), height - 3);
    }
    if (hover) {
      ctx.strokeStyle = theme.text;
      ctx.beginPath();
      ctx.moveTo(hover.x, 0);
      ctx.lineTo(hover.x, plotCssH);
      ctx.moveTo(LEFT, hover.y);
      ctx.lineTo(width, hover.y);
      ctx.stroke();
    }
    history.draws++;
  }

  function updatePeaks() {
    // Le tableau : les raies les plus fortes de cet instant.
    const list: { hz: number; db: number }[] = [];
    const binHz = (engine.analyser?.context.sampleRate ?? 48000) / SIZE;
    for (let k = 2; k < SIZE / 2 - 1; k++) {
      if (spectrum[k] > spectrum[k - 1] && spectrum[k] >= spectrum[k + 1] && spectrum[k] > FLOOR_DB + 20) list.push({ hz: k * binHz, db: spectrum[k] });
    }
    list.sort((a, b) => b.db - a.db);
    peaks = list.slice(0, 8).sort((a, b) => a.hz - b.hz);
  }

  function onMove(event: PointerEvent) {
    const box = canvas.getBoundingClientRect();
    const x = event.clientX - box.left;
    const y = event.clientY - box.top;
    const plotCssW = plotW / ratio;
    const plotCssH = plotH / ratio;
    if (x < LEFT || y > plotCssH) {
      hover = null;
      return;
    }
    const py = Math.min(plotH - 1, Math.max(0, Math.round(y * ratio)));
    const px = Math.min(plotW - 1, Math.max(0, Math.round((x - LEFT) * ratio)));
    const column = (writeAt + px) % plotW;
    hover = {
      x,
      y,
      hz: MIN_HZ * (MAX_HZ / MIN_HZ) ** (1 - y / plotCssH),
      db: levels[py * plotW + column],
      ago: SECONDS * (1 - (x - LEFT) / plotCssW),
    };
  }

  let sinceTable = 0;

  onMount(() => animate(root, (seconds) => {
    const theme = readTheme(canvas);
    if (theme !== rampFor) buildRamp(theme);
    ratio = Math.min(window.devicePixelRatio || 1, 2);
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    const w = Math.round(width * ratio);
    const h = Math.round(height * ratio);
    if (canvas.width !== w || canvas.height !== h || !image) {
      canvas.width = w;
      canvas.height = h;
      ctx = canvas.getContext('2d');
      resize(width, height);
    }
    if (engine.analyser && engine.analyser.context.sampleRate !== rate) mapFrequencies();
    if (running && !frozen && capture()) {
      pending += (plotW / SECONDS) * seconds;
      const count = Math.floor(pending);
      if (count > 0) {
        pending -= count;
        writeColumn(Math.min(count, plotW));
      }
      sinceTable += seconds;
      if (sinceTable > 0.5) {
        sinceTable = 0;
        updatePeaks();
      }
    }
    paint(theme, width, height);
  }));
</script>

<div class="block" bind:this={root} data-testid="chart-spectrogram">
  <header>
    <h3>{t('charts.spectrogram')} <small>{t('spectrogram.windows', { ms: Math.round((SIZE / 48000) * 1000) })}</small></h3>
    <div class="scale" aria-hidden="true"><span>{FLOOR_DB} dB</span><i></i><span>{CEIL_DB} dB</span></div>
  </header>
  <div class="plot">
    <!-- svelte-ignore a11y_no_interactive_element_to_noninteractive_role -->
    <canvas bind:this={canvas} role="img" aria-label={t('spectrogram.aria')} onpointermove={onMove} onpointerleave={() => (hover = null)}></canvas>
    {#if hover}
      <div class="tooltip" style="left: min({hover.x + 12}px, calc(100% - 160px)); top: {Math.max(hover.y - 40, 2)}px">
        <p><strong>{Math.round(hover.db)} dB</strong> {t('spectrogram.at', { frequency: hover.hz >= 1000 ? `${fixed(hover.hz / 1000, 2)} kHz` : `${number(hover.hz)} Hz` })}</p>
        <p>{t('common.ago', { seconds: fixed(hover.ago, 1) })}</p>
      </div>
    {/if}
  </div>
  {#if !running}
    <p class="note">{t('spectrogram.start')}</p>
  {/if}
  <details>
    <summary>{t('spectrogram.peaks')}</summary>
    <table>
      <thead><tr><th scope="col">{t('spectrogram.frequencyHz')}</th><th scope="col">{t('common.levelDb')}</th></tr></thead>
      <tbody>
        {#each peaks as peak (peak.hz)}
          <tr><td>{number(peak.hz)}</td><td>{Math.round(peak.db)}</td></tr>
        {/each}
      </tbody>
    </table>
  </details>
</div>

<style>
  .block { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
  header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline; gap: 2px 12px; }
  h3 { margin: 0; color: var(--text-dim); font-size: 0.82rem; font-weight: 400; }
  small { font-size: 0.75rem; }
  .scale { display: inline-flex; align-items: center; gap: 6px; color: var(--text-dim); font-size: 0.75rem; font-variant-numeric: tabular-nums; }
  .scale i { display: block; width: 90px; height: 8px; background: linear-gradient(to right, light-dark(#fcfcfb, #15110e), light-dark(#f5c9a6, #5c2a14), light-dark(#c95a12, #e36f2b), light-dark(#4a1c03, #ffe0b0)); }
  .plot { position: relative; }
  canvas { display: block; width: 100%; height: 200px; touch-action: pan-y; }
  .tooltip { position: absolute; z-index: 2; padding: 6px 8px; pointer-events: none; background: var(--surface-high); font-size: 0.8rem; color: var(--text-dim); white-space: nowrap; }
  .tooltip p { margin: 0; }
  .tooltip strong { color: var(--text); font-weight: 600; font-variant-numeric: tabular-nums; }
  .note { margin: 0; color: var(--text-dim); font-size: 0.82rem; }
  details { color: var(--text-dim); font-size: 0.82rem; }
  summary { cursor: pointer; }
  table { margin-top: 6px; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  th, td { padding: 2px 10px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; }
</style>
