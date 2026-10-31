<script lang="ts">
  import { onMount } from 'svelte';

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
  const LABELS = [100, 1000, 10000];

  onMount(() => {
    const ctx = canvas.getContext('2d')!;
    const styles = getComputedStyle(canvas);
    const accent = styles.getPropertyValue('--accent').trim();
    const dim = styles.getPropertyValue('--text-dim').trim();
    let frame = 0;

    const draw = () => {
      frame = requestAnimationFrame(draw);
      const ratio = window.devicePixelRatio || 1;
      const width = Math.round(canvas.clientWidth * ratio);
      const height = Math.round(canvas.clientHeight * ratio);
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      ctx.clearRect(0, 0, width, height);
      const plotHeight = height - 18 * ratio;
      const xOf = (hz: number) => (Math.log(hz / MIN_HZ) / Math.log(MAX_HZ / MIN_HZ)) * width;

      ctx.fillStyle = dim;
      ctx.font = `${11 * ratio}px system-ui, sans-serif`;
      ctx.textAlign = 'center';
      for (const hz of LABELS) ctx.fillText(hz >= 1000 ? `${hz / 1000} kHz` : `${hz} Hz`, xOf(hz), height - 3 * ratio);

      if (!analyser || !running) return;
      const data = new Float32Array(analyser.frequencyBinCount);
      analyser.getFloatFrequencyData(data);
      const binHz = analyser.context.sampleRate / analyser.fftSize;
      const barWidth = width / BARS;
      ctx.fillStyle = accent;
      for (let i = 0; i < BARS; i++) {
        const low = MIN_HZ * (MAX_HZ / MIN_HZ) ** (i / BARS);
        const high = MIN_HZ * (MAX_HZ / MIN_HZ) ** ((i + 1) / BARS);
        const first = Math.max(1, Math.floor(low / binHz));
        const last = Math.max(first + 1, Math.ceil(high / binHz));
        let top = -Infinity;
        for (let bin = first; bin < last && bin < data.length; bin++) top = Math.max(top, data[bin]);
        const level = Math.max(0, Math.min(1, (top - FLOOR_DB) / (CEIL_DB - FLOOR_DB)));
        const barHeight = level * plotHeight;
        ctx.globalAlpha = 0.35 + 0.65 * level;
        ctx.fillRect(i * barWidth + 1, plotHeight - barHeight, Math.max(1, barWidth - 2), barHeight);
      }
      ctx.globalAlpha = 1;
    };
    draw();
    return () => cancelAnimationFrame(frame);
  });
</script>

<div role="img" aria-label="Spectre du son, des graves aux aigus">
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  canvas { display: block; width: 100%; height: 120px; }
</style>
