<script lang="ts">
  import type { EngineConfig } from '../lib/config';
  import { firingsPerCycle } from '../lib/rotary';
  import type { Engine } from '../lib/engine';
  import { Fft } from '../lib/fft';
  import type { TelemetryHistory } from '../lib/history';
  import { fixed, t, whole as number } from '../lib/i18n.svelte';

  interface Props {
    engine: Engine;
    config: EngineConfig;
    rpm: number;
    running: boolean;
    history: TelemetryHistory;
  }
  let { engine, config, rpm, running, history }: Props = $props();

  const WIDTH = 680;
  const HEIGHT = 190;
  const LEFT = 42;
  const RIGHT = 12;
  const TOP = 10;
  const BOTTOM = 22;
  const MIN_HZ = 30;
  const MAX_HZ = 8000;
  const FLOOR = -60;
  const CEIL = 6;
  const RATE = 44100;
  const SIZE = 32768;
  const POINTS = 220;
  const ORDERS = 8;

  const fft = new Fft(SIZE);

  /** La réponse calculée par `ExhaustLine`, la même qui filtre le son. Elle ne dépend que des pièces, pas du régime. */
  const response = $derived.by(() => {
    const impulse = engine.lineImpulse(config);
    if (!impulse) return [];
    const re = new Float32Array(SIZE);
    const im = new Float32Array(SIZE);
    re.set(impulse.subarray(0, Math.min(impulse.length, SIZE)));
    fft.transform(re, im);
    const binHz = RATE / SIZE;
    const raw = Array.from({ length: POINTS }, (_, i) => {
      const hz = MIN_HZ * (MAX_HZ / MIN_HZ) ** (i / (POINTS - 1));
      const next = MIN_HZ * (MAX_HZ / MIN_HZ) ** ((i + 1) / (POINTS - 1));
      const from = Math.max(1, Math.round(hz / binHz));
      const to = Math.max(from + 1, Math.round(next / binHz));
      let sum = 0;
      for (let k = from; k < to; k++) sum += re[k] * re[k] + im[k] * im[k];
      return { hz, power: sum / (to - from) };
    });
    const top = Math.max(...raw.map((point) => point.power), 1e-12);
    return raw.map((point) => ({ hz: point.hz, db: 10 * Math.log10(Math.max(point.power / top, 1e-9)) }));
  });

  const xOf = (hz: number) => LEFT + (Math.log(hz / MIN_HZ) / Math.log(MAX_HZ / MIN_HZ)) * (WIDTH - LEFT - RIGHT);
  const yOf = (db: number) => TOP + (1 - (Math.max(db, FLOOR) - FLOOR) / (CEIL - FLOOR)) * (HEIGHT - TOP - BOTTOM);
  const path = $derived(response.map((point, i) => `${i ? 'L' : 'M'}${xOf(point.hz).toFixed(1)} ${yOf(point.db).toFixed(1)}`).join(' '));
  const firing = $derived((rpm / 120) * firingsPerCycle(config));
  const orders = $derived(running && firing > 1 ? Array.from({ length: ORDERS }, (_, i) => firing * (i + 1)).filter((hz) => hz < MAX_HZ && hz > MIN_HZ) : []);

  let hover = $state<number | null>(null);
  const hovered = $derived(hover === null ? null : response[hover]);

  function onMove(event: PointerEvent) {
    const box = (event.currentTarget as SVGElement).getBoundingClientRect();
    const x = ((event.clientX - box.left) / box.width) * WIDTH;
    const share = (x - LEFT) / (WIDTH - LEFT - RIGHT);
    hover = Math.max(0, Math.min(response.length - 1, Math.round(share * (POINTS - 1))));
    history.draws++;
  }

  /** Le tableau : une valeur par octave. */
  const table = $derived([63, 125, 250, 500, 1000, 2000, 4000].map((hz) => {
    const near = response.reduce((best, point) => (Math.abs(point.hz - hz) < Math.abs(best.hz - hz) ? point : best), response[0] ?? { hz, db: 0 });
    return { hz, db: near.db };
  }));
  const hzLabel = (hz: number) => (hz >= 1000 ? `${fixed(hz / 1000, hz >= 10000 ? 0 : 1)} kHz` : `${Math.round(hz)} Hz`);
</script>

<div class="block" data-testid="chart-line">
  <header>
    <h3>{t('line.title')}</h3>
    <p>{#if orders.length}{t('line.orders', { rpm: number(rpm) })}{:else}{t('line.start')}{/if}</p>
  </header>
  <svg viewBox="0 0 {WIDTH} {HEIGHT}" role="img" aria-label={t('line.aria')}
    onpointermove={onMove} onpointerleave={() => (hover = null)}>
    {#each [0, -20, -40, -60] as db (db)}
      <line x1={LEFT} x2={WIDTH - RIGHT} y1={yOf(db)} y2={yOf(db)} class="grid" />
      <text x={LEFT - 6} y={yOf(db) + 4} class="axis" text-anchor="end">{db}</text>
    {/each}
    {#each [100, 300, 1000, 3000] as hz (hz)}
      <text x={xOf(hz)} y={HEIGHT - 6} class="axis" text-anchor="middle">{hzLabel(hz)}</text>
    {/each}
    {#each orders as hz, i (i)}
      <line x1={xOf(hz)} x2={xOf(hz)} y1={TOP} y2={HEIGHT - BOTTOM} class="order" style="opacity: {1 - i * 0.09}" data-testid="line-order" />
    {/each}
    <path d={path} class="curve" />
    {#if hovered}
      {@const px = xOf(hovered.hz)}
      {@const py = yOf(hovered.db)}
      {@const toLeft = px > WIDTH * 0.62}
      <line x1={px} x2={px} y1={TOP} y2={HEIGHT - BOTTOM} class="cross" />
      <circle cx={px} cy={py} r="4" class="dot" />
      <text x={toLeft ? px - 10 : px + 10} y={Math.max(py - 8, TOP + 12)} class="tip" text-anchor={toLeft ? 'end' : 'start'}>{Math.round(hovered.db)} dB · {hzLabel(hovered.hz)}</text>
    {/if}
  </svg>
  <details>
    <summary>{t('common.showValues')}</summary>
    <table>
      <thead><tr><th scope="col">{t('line.frequency')}</th><th scope="col">{t('common.levelDb')}</th></tr></thead>
      <tbody>
        {#each table as row (row.hz)}
          <tr><td>{hzLabel(row.hz)}</td><td>{Math.round(row.db)}</td></tr>
        {/each}
      </tbody>
    </table>
  </details>
</div>

<style>
  .block { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
  header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline; gap: 2px 12px; }
  h3 { margin: 0; color: var(--text-dim); font-size: 0.82rem; font-weight: 400; }
  header p { margin: 0; color: var(--text-dim); font-size: 0.8rem; }
  svg { display: block; width: 100%; height: auto; touch-action: pan-y; }
  .grid { stroke: var(--line); stroke-width: 1; }
  .axis { fill: var(--text-dim); font-size: 10.5px; font-variant-numeric: tabular-nums; }
  .curve { fill: none; stroke: light-dark(#c95a12, #e36f2b); stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
  .order { stroke: var(--text-dim); stroke-width: 1; }
  .cross { stroke: var(--text-dim); stroke-width: 1; }
  .dot { fill: light-dark(#c95a12, #e36f2b); stroke: var(--surface); stroke-width: 2; }
  .tip { fill: var(--text); font-size: 11.5px; font-variant-numeric: tabular-nums; paint-order: stroke; stroke: var(--surface); stroke-width: 3; }
  details { color: var(--text-dim); font-size: 0.82rem; }
  summary { cursor: pointer; }
  table { margin-top: 6px; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  th, td { padding: 2px 10px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; }
</style>
