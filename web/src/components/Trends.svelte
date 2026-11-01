<script lang="ts">
  import { onMount } from 'svelte';
  import { animate, fit, readTheme } from '../lib/canvas';
  import { HISTORY_HZ, type ChannelKey, type TelemetryHistory } from '../lib/history';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  interface Row {
    channel: ChannelKey;
    label: () => string;
    /** Échelle stable : un coup de gaz ne doit pas faire sauter la courbe. */
    range: () => [number, number];
    text: (value: number) => string;
    factor?: number;
  }
  interface Props {
    history: TelemetryHistory;
    redline: number;
    peakTorque: number;
    topSpeed: number;
  }
  let { history, redline, peakTorque, topSpeed }: Props = $props();

  const SECONDS = 30;
  const ROWS: Row[] = [
    { channel: 'rpm', label: () => t('tacho.label'), range: () => [0, Math.max(redline, 1000)], text: (value) => `${whole(value)} ${t('unit.rpm')}` },
    { channel: 'speed', label: () => t('common.speed'), range: () => [0, Math.max(topSpeed, 60)], text: (value) => `${whole(value)} km/h` },
    { channel: 'torque', label: () => t('common.torque'), range: () => [-Math.max(peakTorque, 50) * 0.3, Math.max(peakTorque, 50) * 1.1], text: (value) => `${whole(value)} Nm` },
    { channel: 'load', label: () => t('meter.load'), range: () => [0, 100], text: (value) => t('unit.percent', { value: whole(value) }), factor: 100 },
    { channel: 'levelDbA', label: () => t('common.level'), range: () => [30, 120], text: (value) => `${whole(value)} dB(A)` },
  ];

  const canvases = $state<HTMLCanvasElement[]>([]);
  const values = $state<HTMLElement[]>([]);
  let root: HTMLElement;
  let hoverShare = $state<number | null>(null);
  let frame = 0;

  /** Le dernier échantillon à afficher, ou celui sous le curseur. */
  function sampleIndex(now: number): number {
    if (history.length === 0) return -1;
    if (hoverShare === null) return history.indexFromEnd(0);
    const back = Math.round((1 - hoverShare) * SECONDS * HISTORY_HZ);
    if (back >= history.length) return -1;
    const at = history.indexFromEnd(back);
    return now - history.times[at] < (SECONDS + 1) * 1000 ? at : -1;
  }

  function draw(now: number) {
    const theme = readTheme(root);
    const samples = Math.min(SECONDS * HISTORY_HZ, history.length);
    const at = sampleIndex(now);
    for (let r = 0; r < ROWS.length; r++) {
      const row = ROWS[r];
      const { ctx, width, height } = fit(canvases[r]);
      const data = history.data[row.channel];
      const factor = row.factor ?? 1;
      const [low, high] = row.range();
      const Y = (value: number) => 2 + (1 - (Math.min(Math.max(value, low), high) - low) / (high - low)) * (height - 4);
      const X = (seconds: number) => (1 - seconds / SECONDS) * width;
      ctx.strokeStyle = theme.line;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, height - 0.5);
      ctx.lineTo(width, height - 0.5);
      ctx.stroke();
      ctx.strokeStyle = theme.accent;
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      ctx.beginPath();
      let started = false;
      // Une valeur par pixel suffit : plus la colonne est étroite, moins il y a de points.
      const stride = Math.max(1, Math.floor(samples / Math.max(width, 1)));
      for (let k = samples - 1; k >= 0; k -= stride) {
        const index = history.indexFromEnd(k);
        const x = X((now - history.times[index]) / 1000);
        if (x < 0) continue;
        const y = Y(data[index] * factor);
        if (started) ctx.lineTo(x, y);
        else {
          ctx.moveTo(x, y);
          started = true;
        }
      }
      ctx.stroke();
      if (hoverShare !== null) {
        const x = Math.round(hoverShare * width) + 0.5;
        ctx.strokeStyle = theme.dim;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
        ctx.stroke();
      }
      values[r].textContent = at >= 0 ? row.text(data[at] * factor) : '—';
    }
  }

  function onMove(event: PointerEvent) {
    const box = (event.currentTarget as HTMLElement).getBoundingClientRect();
    hoverShare = Math.max(0, Math.min(1, (event.clientX - box.left) / box.width));
  }

  onMount(() => animate(root, (_seconds, now) => {
    // Dix fois par seconde : ces courbes se lisent sur trente secondes, pas à l'image près.
    frame = (frame + 1) % 6;
    if (frame === 0) draw(now);
  }));

  const ago = $derived(hoverShare === null ? null : (1 - hoverShare) * SECONDS);
</script>

<div class="trends" bind:this={root} data-testid="trends">
  <div class="rows" role="group" aria-label={t('trends.aria', { seconds: SECONDS })} onpointerleave={() => (hoverShare = null)}>
    {#each ROWS as row, r (row.channel)}
      <div class="row">
        <span class="label">{row.label()}</span>
        <div class="plot">
          <!-- svelte-ignore a11y_no_interactive_element_to_noninteractive_role -->
          <canvas bind:this={canvases[r]} onpointermove={onMove} role="img" aria-label={t('trends.row.aria', { name: row.label(), seconds: SECONDS })} data-testid="trend-{row.channel}"></canvas>
        </div>
        <output class="value" data-testid="trend-{row.channel}-value" bind:this={values[r]}>—</output>
      </div>
    {/each}
  </div>
  <p class="foot">{ago === null ? t('trends.foot', { seconds: SECONDS }) : t('common.ago', { seconds: fixed(ago, 1) })}</p>
</div>

<style>
  .trends { display: flex; flex-direction: column; gap: 6px; flex: 1; }
  .rows { display: flex; flex-direction: column; gap: 8px; flex: 1; }
  .row { flex: 1; display: grid; grid-template-columns: minmax(78px, 7rem) minmax(0, 1fr) minmax(78px, 6rem); align-items: stretch; gap: 8px; min-height: 28px; }
  .label, .value { align-self: center; }
  .label { color: var(--text-dim); font-size: var(--type-label); letter-spacing: 0.02em; }
  .plot { position: relative; min-width: 0; min-height: 28px; }
  /* Absolu : le canvas suit sa rangée sans jamais la pousser, sinon sa taille et celle de la carte s'entraînent. */
  canvas { position: absolute; inset: 0; display: block; width: 100%; height: 100%; touch-action: pan-y; }
  .value { font-size: var(--type-body); font-weight: 600; font-variant-numeric: tabular-nums; text-align: right; white-space: nowrap; }
  .foot { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
