<script lang="ts">
  interface Props {
    rpm: number;
    redline: number;
    gear: number;
    speed: number;
    running: boolean;
    showRoad: boolean;
  }
  let { rpm, redline, gear, speed, running, showRoad }: Props = $props();

  const START = 135;
  const SWEEP = 270;
  const CENTER = 160;
  const RADIUS = 134;

  const scaleMax = $derived(Math.ceil(redline / 1000) * 1000 + (redline % 1000 === 0 ? 1000 : 0));
  const ticks = $derived(Array.from({ length: scaleMax / 1000 + 1 }, (_, i) => i));

  const point = (angle: number, radius: number) => {
    const radians = (angle * Math.PI) / 180;
    return [CENTER + radius * Math.cos(radians), CENTER + radius * Math.sin(radians)];
  };
  const angleOf = (value: number) => START + (SWEEP * Math.min(value, scaleMax)) / scaleMax;
  const arc = (from: number, to: number, radius: number) => {
    if (to - from < 0.1) return '';
    const [x1, y1] = point(from, radius);
    const [x2, y2] = point(to, radius);
    return `M ${x1} ${y1} A ${radius} ${radius} 0 ${to - from > 180 ? 1 : 0} 1 ${x2} ${y2}`;
  };
  const gearLabel = $derived(gear === 0 ? 'N' : String(gear));
  const inRed = $derived(rpm >= redline * 0.985);
</script>

<svg class="tacho" viewBox="0 0 320 300" role="img" aria-label="Compte-tours : {Math.round(rpm)} tours par minute">
  <path d={arc(START, START + SWEEP, RADIUS)} class="track" />
  <path d={arc(angleOf(redline), START + SWEEP, RADIUS)} class="red-zone" />
  <path d={arc(START, angleOf(rpm), RADIUS)} class="value" class:in-red={inRed} />
  {#each ticks as tick (tick)}
    {@const angle = angleOf(tick * 1000)}
    {@const [x1, y1] = point(angle, RADIUS - 16)}
    {@const [x2, y2] = point(angle, RADIUS - 26)}
    {@const [tx, ty] = point(angle, RADIUS - 42)}
    <line {x1} {y1} {x2} {y2} class="tick" class:hot={tick * 1000 >= redline} />
    <text x={tx} y={ty} class="tick-label" class:hot={tick * 1000 >= redline}>{tick}</text>
  {/each}
  <text x={CENTER} y="150" class="rpm" class:off={!running} data-testid="rpm">{running ? Math.round(rpm / 10) * 10 : 0}</text>
  <text x={CENTER} y="174" class="unit">tr/min</text>
  {#if showRoad}
    <text x="128" y="226" class="road-value" data-testid="gear">{gearLabel}</text>
    <text x="128" y="244" class="unit">rapport</text>
    <text x="192" y="226" class="road-value" data-testid="speed">{Math.round(speed)}</text>
    <text x="192" y="244" class="unit">km/h</text>
  {/if}
</svg>

<style>
  .tacho { width: 100%; max-width: 280px; display: block; margin: 0 auto; user-select: none; }
  @media (max-width: 860px) { .tacho { max-width: 340px; } }
  path { fill: none; stroke-linecap: round; }
  .track { stroke: var(--line); stroke-width: 12; }
  .red-zone { stroke: var(--danger); stroke-width: 12; opacity: 0.5; stroke-linecap: butt; }
  .value { stroke: var(--accent); stroke-width: 12; }
  .value.in-red { stroke: var(--danger); }
  .tick { stroke: var(--text-dim); stroke-width: 2; }
  .tick.hot { stroke: var(--danger); }
  text { fill: var(--text); text-anchor: middle; font-variant-numeric: tabular-nums; }
  .tick-label { fill: var(--text-dim); font-size: 14px; dominant-baseline: central; }
  .tick-label.hot { fill: var(--danger); }
  .rpm { font-size: 56px; font-weight: 300; letter-spacing: -1px; }
  .rpm.off { fill: var(--text-dim); }
  .unit { fill: var(--text-dim); font-size: 13px; }
  .road-value { font-size: 26px; font-weight: 500; }
</style>
