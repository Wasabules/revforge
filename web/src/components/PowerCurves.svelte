<script lang="ts">
  import { heated } from '../lib/heat';
  import type { Spec } from '../lib/engine';
  import { t, whole as number } from '../lib/i18n.svelte';

  interface Props {
    spec: Spec;
    rpm: number;
    /** Ce que le moteur produit à cet instant : sous la courbe tant qu'on n'est pas à fond. */
    liveHp: number;
    liveTorque: number;
    running: boolean;
  }
  let { spec, rpm, liveHp, liveTorque, running }: Props = $props();

  // Une colonne étroite : l'unité du dessin reste proche du pixel, les graduations se lisent.
  const WIDTH = 340;
  const HEIGHT = 108;
  const LEFT = 38;
  const RIGHT = 10;
  const TOP = 8;
  const BOTTOM = 20;

  /** Deux graphiques sur le même axe de régime plutôt qu'un seul à deux échelles. */
  const charts = $derived([
    { key: 'hp' as const, title: t('common.power'), unit: t('unit.hp'), peak: spec.peakHp, peakRpm: spec.peakHpRpm },
    { key: 'torque' as const, title: t('common.torque'), unit: 'Nm', peak: spec.peakTorque, peakRpm: spec.peakTorqueRpm },
  ]);

  const niceStep = (top: number) => {
    const raw = top / 3;
    const power = 10 ** Math.floor(Math.log10(raw || 1));
    const scaled = raw / power;
    return (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10) * power;
  };

  const xOf = (value: number) => {
    const first = spec.curve[0]?.rpm ?? 0;
    const last = spec.curve[spec.curve.length - 1]?.rpm ?? 1;
    return LEFT + ((value - first) / Math.max(last - first, 1)) * (WIDTH - LEFT - RIGHT);
  };

  let hover = $state<number | null>(null);
  const hovered = $derived(hover === null ? null : spec.curve[hover]);

  function onMove(event: PointerEvent) {
    const box = (event.currentTarget as SVGElement).getBoundingClientRect();
    const x = ((event.clientX - box.left) / box.width) * WIDTH;
    let best = 0;
    let bestGap = Infinity;
    spec.curve.forEach((point, index) => {
      const gap = Math.abs(xOf(point.rpm) - x);
      if (gap < bestGap) {
        bestGap = gap;
        best = index;
      }
    });
    hover = best;
  }
</script>

<div class="curves" data-testid="power-curves">
  {#each charts as chart (chart.key)}
    {@const top = Math.max(chart.peak * 1.08, 1)}
    {@const step = niceStep(top)}
    {@const yOf = (value: number) => TOP + (1 - value / top) * (HEIGHT - TOP - BOTTOM)}
    {@const grid = Array.from({ length: Math.floor(top / step) + 1 }, (_, i) => i * step)}
    {@const path = spec.curve.map((p, i) => `${i ? 'L' : 'M'}${xOf(p.rpm).toFixed(1)} ${yOf(p[chart.key]).toFixed(1)}`).join(' ')}
    {@const live = running ? Math.max(chart.key === 'hp' ? liveHp : liveTorque, 0) : null}
    <section>
      <header>
        <h3>{chart.title}</h3>
        <p>
          {#if live !== null}<strong class="live" data-testid="curve-live-{chart.key}">{number(live)}</strong> / {/if}<strong data-testid="peak-{chart.key}">{number(chart.peak)} {chart.unit}</strong> {t('curves.atRpm', { rpm: number(chart.peakRpm) })}
        </p>
      </header>
      <svg viewBox="0 0 {WIDTH} {HEIGHT}" role="img" aria-label={t('curves.aria', { title: chart.title, peak: number(chart.peak), unit: chart.unit, rpm: number(chart.peakRpm) })}
        onpointermove={onMove} onpointerleave={() => (hover = null)}>
        {#each grid as value (value)}
          <line x1={LEFT} x2={WIDTH - RIGHT} y1={yOf(value)} y2={yOf(value)} class="grid" />
          <text x={LEFT - 6} y={yOf(value) + 4} class="axis" text-anchor="end">{number(value)}</text>
        {/each}
        {#each [0.25, 0.5, 0.75, 1] as share (share)}
          {@const value = Math.round((spec.redline * share) / 500) * 500}
          <text x={Math.min(xOf(value), WIDTH - RIGHT - 14)} y={HEIGHT - 6} class="axis" text-anchor="middle">{number(value)}</text>
        {/each}
        <path d={path} class="line" use:heated />
        {#if live !== null}
          <line x1={xOf(rpm)} x2={xOf(rpm)} y1={TOP} y2={HEIGHT - BOTTOM} class="now" />
          <circle cx={xOf(rpm)} cy={yOf(Math.min(live, top))} r="5" class="dot" data-testid="live-{chart.key}-dot" use:heated />
        {/if}
        {#if hovered}
          {@const px = xOf(hovered.rpm)}
          {@const py = yOf(hovered[chart.key])}
          {@const label = `${number(hovered[chart.key])} ${chart.unit} · ${number(hovered.rpm)} ${t('unit.rpm')}`}
          {@const toLeft = px > WIDTH * 0.62}
          <line x1={px} x2={px} y1={TOP} y2={HEIGHT - BOTTOM} class="cross" />
          <circle cx={px} cy={py} r="5" class="dot" use:heated />
          <text x={toLeft ? px - 10 : px + 10} y={Math.max(py - 8, TOP + 12)} class="tip" text-anchor={toLeft ? 'end' : 'start'}>{label}</text>
        {/if}
      </svg>
    </section>
  {/each}

  <details>
    <summary>{t('common.showValues')}</summary>
    <table>
      <thead><tr><th scope="col">{t('unit.rpm')}</th><th scope="col">{t('unit.hp')}</th><th scope="col">Nm</th></tr></thead>
      <tbody>
        {#each spec.curve.filter((_, i) => i % 5 === 0) as point (point.rpm)}
          <tr><td>{number(point.rpm)}</td><td>{number(point.hp)}</td><td>{number(point.torque)}</td></tr>
        {/each}
      </tbody>
    </table>
  </details>
</div>

<style>
  .curves { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 8px 16px; }
  header { display: flex; justify-content: space-between; align-items: baseline; gap: 8px; }
  h3 { margin: 0; color: var(--text-dim); font-family: var(--display); font-size: 1.125rem; font-weight: 700; line-height: 1.1; text-transform: uppercase; letter-spacing: 0.01em; }
  header p { margin: 0; color: var(--text-dim); font-size: var(--type-small); font-variant-numeric: tabular-nums; }
  strong { color: var(--text); font-size: var(--type-body); font-weight: 600; }
  strong.live { font-size: 1rem; }
  svg { display: block; width: 100%; height: auto; touch-action: pan-y; }
  .grid { stroke: var(--line); stroke-width: 1; }
  .axis { fill: var(--text-dim); font-size: 10.5px; font-variant-numeric: tabular-nums; }
  .line { fill: none; stroke: var(--heat-text); stroke-width: 2.5; stroke-linejoin: round; stroke-linecap: round; }
  .now { stroke: var(--line-strong); stroke-width: 1; }
  .cross { stroke: var(--text-dim); stroke-width: 1; }
  .dot { fill: var(--heat-text); stroke: var(--surface); stroke-width: 2; }
  .tip { fill: var(--text); font-size: 11.5px; font-variant-numeric: tabular-nums; paint-order: stroke; stroke: var(--surface); stroke-width: 3; }
  details { grid-column: 1 / -1; color: var(--text-dim); font-size: var(--type-small); }
  summary { cursor: pointer; }
  table { width: 100%; margin-top: 6px; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  th, td { padding: 2px 8px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; }
</style>
