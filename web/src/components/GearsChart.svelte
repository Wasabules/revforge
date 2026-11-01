<script lang="ts">
  import type { BoxSpec } from '../lib/engine';
  import { t, whole as number } from '../lib/i18n.svelte';

  interface Props {
    box: BoxSpec;
    redline: number;
    /** Où l'on se trouve : vitesse de la voiture et régime du moteur. */
    rpm: number;
    speed: number;
    gear: number;
    running: boolean;
  }
  let { box, redline, rpm, speed, gear, running }: Props = $props();

  const WIDTH = 480;
  const HEIGHT = 190;
  const LEFT = 44;
  const RIGHT = 14;
  const TOP = 14;
  const BOTTOM = 24;

  const maxKmh = $derived(Math.max(...box.gears.map((g) => g.topKmh), 1));
  const topKmh = $derived(Math.ceil((maxKmh * 1.04) / 50) * 50);
  const xOf = (kmh: number) => LEFT + (kmh / topKmh) * (WIDTH - LEFT - RIGHT);
  const yOf = (value: number) => TOP + (1 - value / redline) * (HEIGHT - TOP - BOTTOM);

  /** Les dents de scie : on tire chaque rapport jusqu'au régime maximal, puis le régime tombe sur le suivant. */
  const sawPath = $derived.by(() => {
    const parts: string[] = [];
    box.gears.forEach((g, i) => {
      const next = box.gears[i + 1];
      if (i === 0) parts.push(`M${xOf(0).toFixed(1)} ${yOf(0).toFixed(1)}`);
      parts.push(`L${xOf(g.topKmh).toFixed(1)} ${yOf(redline).toFixed(1)}`);
      if (next) parts.push(`L${xOf(g.topKmh).toFixed(1)} ${yOf((redline * next.overall) / g.overall).toFixed(1)}`);
    });
    return parts.join(' ');
  });

  const speedTicks = $derived(Array.from({ length: Math.floor(topKmh / 50) + 1 }, (_, i) => i * 50).filter((value) => topKmh < 300 || value % 100 === 0));
  const rpmStep = $derived(redline > 9000 ? 3000 : 2000);
  const rpmTicks = $derived(Array.from({ length: Math.floor(redline / rpmStep) + 1 }, (_, i) => i * rpmStep));

  let hover = $state<number | null>(null);

  function onMove(event: PointerEvent) {
    const frame = (event.currentTarget as SVGElement).getBoundingClientRect();
    const x = ((event.clientX - frame.left) / frame.width) * WIDTH;
    hover = Math.min(Math.max(((x - LEFT) / (WIDTH - LEFT - RIGHT)) * topKmh, 0), topKmh);
  }

  /** À cette vitesse, le régime de chaque rapport qui ne dépasse pas le régime maximal. */
  const readings = $derived(
    hover === null
      ? []
      : box.gears
          .filter((g) => hover! <= g.topKmh)
          .map((g) => ({ gear: g.gear, rpm: (hover! / g.topKmh) * redline })),
  );
  const tipLeft = $derived(hover !== null && xOf(hover) > WIDTH * 0.62);
  const liveShown = $derived(running && gear !== 0 && speed > 1);
</script>

<figure class="chart" data-testid="gears-chart">
  <figcaption>{t('gearsChart.caption')}</figcaption>
  <svg viewBox="0 0 {WIDTH} {HEIGHT}" role="img"
    aria-label={t('gearsChart.aria', { gears: box.gears.map((g) => t('gearsChart.aria.gear', { gear: g.gear, kmh: number(g.topKmh) })).join('. ') })}
    onpointermove={onMove} onpointerleave={() => (hover = null)}>
    {#each rpmTicks as value (value)}
      <line x1={LEFT} x2={WIDTH - RIGHT} y1={yOf(value)} y2={yOf(value)} class="grid" />
      <text x={LEFT - 6} y={yOf(value) + 4} class="axis" text-anchor="end">{number(value)}</text>
    {/each}
    {#each speedTicks as value (value)}
      <text x={xOf(value)} y={HEIGHT - 8} class="axis" text-anchor="middle">{value}</text>
    {/each}
    {#each box.gears as g (g.gear)}
      <line x1={xOf(0)} y1={yOf(0)} x2={xOf(g.topKmh)} y2={yOf(redline)} class="context" class:current={running && g.gear === gear} />
      <text x={xOf(g.topKmh)} y={TOP - 4} class="label" text-anchor="middle">{g.gear}</text>
    {/each}
    <path d={sawPath} class="saw" data-testid="gears-saw" />
    {#if liveShown}
      <circle cx={xOf(Math.min(speed, topKmh))} cy={yOf(Math.min(rpm, redline))} r="5" class="dot" data-testid="gears-dot" />
    {/if}
    {#if hover !== null}
      {@const px = xOf(hover)}
      <line x1={px} x2={px} y1={TOP} y2={HEIGHT - BOTTOM} class="cross" />
      {@const anchor = tipLeft ? 'end' : 'start'}
      {@const tx = tipLeft ? px - 8 : px + 8}
      <text x={tx} y={TOP + 12} class="tip strong" text-anchor={anchor}>{number(hover)} km/h</text>
      {#each readings.slice(0, 6) as reading, i (reading.gear)}
        <text x={tx} y={TOP + 26 + i * 13} class="tip" text-anchor={anchor}>{t('gearsChart.tip', { rpm: number(reading.rpm), gear: reading.gear })}</text>
      {/each}
    {/if}
  </svg>
</figure>

<style>
  .chart { margin: 0; }
  figcaption { margin-bottom: 4px; color: var(--text-dim); font-size: 0.82rem; }
  svg { display: block; width: 100%; height: auto; touch-action: pan-y; }
  .grid { stroke: var(--line); stroke-width: 1; }
  .axis { fill: var(--text-dim); font-size: 10.5px; font-variant-numeric: tabular-nums; }
  .label { fill: var(--text-dim); font-size: 11px; font-weight: 600; font-variant-numeric: tabular-nums; }
  .context { stroke: var(--line-strong); stroke-width: 1; }
  .context.current { stroke: var(--text-dim); stroke-width: 2; }
  .saw { fill: none; stroke: var(--heat-text); stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
  .dot { fill: var(--heat-text); stroke: var(--surface); stroke-width: 2; }
  .cross { stroke: var(--text-dim); stroke-width: 1; }
  .tip { fill: var(--text-dim); font-size: 11px; font-variant-numeric: tabular-nums; paint-order: stroke; stroke: var(--surface); stroke-width: 3; }
  .tip.strong { fill: var(--text); font-weight: 600; }
</style>
