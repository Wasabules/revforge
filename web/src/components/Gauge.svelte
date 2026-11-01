<script lang="ts">
  import { onMount, type Snippet } from 'svelte';
  import { digitColumns } from '../lib/digits';
  import { heated } from '../lib/heat';
  import { arcPath, DASH_LENGTH, DASH_OFFSET, polar, type InstrumentClock } from '../lib/instruments';
  import { fixed, lang, whole } from '../lib/i18n.svelte';

  interface Zone {
    from: number;
    to: number;
  }

  interface Props {
    /** Sert aux identifiants de test : `gauge-<id>`, `gauge-<id>-value`. */
    id: string;
    label: string;
    /** Ce que mesure la jauge, et d'où vient la valeur : en infobulle. */
    note?: string;
    unit: string;
    min: number;
    max: number;
    step: number;
    /** Graduations mineures, 0 pour aucune. */
    minor?: number;
    /** D'où part l'arc : à 0 sur un manomètre, entre dépression et pression. */
    zero?: number;
    /** La zone interdite : au-delà, l'arc passe à la couleur d'alerte. */
    zone?: Zone | null;
    /** Un repère en plus sur le cadran : la pression réglée, le ralenti. */
    mark?: number | null;
    digits?: number;
    /** Afficher le signe : une dépression et une pression ne se confondent pas. */
    signed?: boolean;
    /** Les graduations s'écrivent divisées : 7 pour 7000 tr/min. */
    tickDivisor?: number;
    tickDigits?: number;
    sweep?: number;
    large?: boolean;
    /** Le chiffre prend la couleur de la chaleur : la jauge de la température, pas les autres. */
    warm?: boolean;
    clock: InstrumentClock;
    read: () => number;
    valueTestId?: string;
    /** Ce qui s'ajoute au centre du cadran (coordonnées du cadran). */
    center?: Snippet<[{ cx: number; cy: number }]>;
    /** Une ligne sous le cadran. */
    children?: Snippet;
  }
  let { id, label, note, unit, min, max, step, minor = 0, zero, zone = null, mark = null, digits = 0, signed = false, tickDivisor = 1, tickDigits = 0, sweep = 240, large = false, warm = false, clock, read, valueTestId, center, children }: Props = $props();

  const SIZE = $derived(large ? 320 : 200);
  const cx = $derived(SIZE / 2);
  const cy = $derived(SIZE / 2);
  /** Un arc épais : il se lit de loin, et c'est lui qui porte la chaleur. */
  const band = $derived(large ? 22 : 14);
  const radius = $derived(SIZE / 2 - band / 2 - 2);
  /** Le bord intérieur de l'arc : les graduations partent de là. */
  const inner = $derived(radius - band / 2);
  const readingSize = $derived(large ? 76 : 44);
  const start = $derived(90 + (360 - sweep) / 2);
  const height = $derived(cy + radius * Math.sin((((sweep - 180) / 2) * Math.PI) / 180) + band / 2 + (large ? 8 : 6));
  const angleOf = (value: number) => start + (sweep * (Math.min(Math.max(value, min), max) - min)) / (max - min);
  const origin = $derived(zero ?? min);

  const track = $derived(arcPath(cx, cy, radius, start, start + sweep));
  // La zone interdite, en liseré sur le bord extérieur : l'arc passe dessous sans la cacher.
  const zonePath = $derived(zone ? arcPath(cx, cy, radius + band / 2 - (large ? 3.5 : 2.5), angleOf(zone.from), angleOf(zone.to)) : '');

  /** Les graduations ne se recalculent que si l'échelle change. */
  const counted = (from: number, to: number, by: number) => Array.from({ length: Math.round((to - from) / by) + 1 }, (_, i) => from + i * by);
  const majors = $derived(counted(min, max, step));
  const minors = $derived(minor > 0 ? counted(min, max, minor).filter((value) => Math.abs(value / step - Math.round(value / step)) > 1e-6) : []);
  /** Un vrai signe moins, comme sur la lecture : la graduation ne dit pas autre chose qu'elle. */
  const tickLabel = (value: number) => {
    const scaled = Math.abs(value / tickDivisor);
    const text = tickDigits > 0 ? fixed(scaled, tickDigits) : whole(scaled);
    return value < -1e-9 ? `−${text}` : text;
  };
  const line = (value: number, inner: number, outer: number) => {
    const [x1, y1] = polar(cx, cy, inner, angleOf(value));
    const [x2, y2] = polar(cx, cy, outer, angleOf(value));
    return { x1, y1, x2, y2 };
  };
  const hot = (value: number) => zone !== null && value >= zone.from && value <= zone.to;

  let root: HTMLElement;
  let needle: SVGLineElement;
  let arc: SVGPathElement;
  let valueNode: SVGTextElement;

  const shown = (value: number) => {
    const text = digits > 0 ? fixed(Math.abs(value), digits) : whole(Math.abs(value));
    if (!signed) return value < 0 ? `−${text}` : text;
    return value < 0 ? `−${text}` : value > 0 ? `+${text}` : text;
  };

  onMount(() => {
    // Un objet qu'on tourne, pas un attribut à réécrire : tourner l'aiguille n'alloue rien.
    const turn = needle.transform.baseVal.initialize(needle.ownerSVGElement!.createSVGTransform());
    let lastAngle = NaN;
    let lastFrom = -1;
    let lastLength = -1;
    let lastHot = false;
    let lastBelow = false;
    let lastKey = NaN;
    let lastLang = lang();
    const stop = clock.add((text, aria) => {
      const value = read();
      const angle = angleOf(value);
      // `NaN` à la première image : l'aiguille prend sa place tout de suite.
      if (!(Math.abs(angle - lastAngle) <= 0.15)) {
        lastAngle = angle;
        turn.setRotate(angle + 90, cx, cy);
      }
      const span = max - min;
      const here = Math.round(((Math.min(Math.max(value, min), max) - min) / span) * 1000);
      const home = Math.round(((origin - min) / span) * 1000);
      const from = Math.min(here, home);
      const length = Math.abs(here - home);
      if (length !== lastLength) {
        lastLength = length;
        arc.setAttribute('stroke-dasharray', DASH_LENGTH[length]);
      }
      if (from !== lastFrom) {
        lastFrom = from;
        arc.setAttribute('stroke-dashoffset', DASH_OFFSET[from]);
      }
      const isHot = hot(value);
      if (isHot !== lastHot) {
        lastHot = isHot;
        arc.classList.toggle('hot', isHot);
      }
      const below = value < origin;
      if (below !== lastBelow) {
        lastBelow = below;
        arc.classList.toggle('below', below);
      }
      if (text) {
        const key = Math.round(value * 10 ** digits);
        // La langue change le séparateur décimal : le chiffre se réécrit aussi à ce moment.
        if (key !== lastKey || lang() !== lastLang) {
          lastKey = key;
          lastLang = lang();
          const written = shown(key / 10 ** digits);
          valueNode.textContent = written;
          valueNode.setAttribute('x', digitColumns(written, cx, readingSize));
        }
      }
      if (aria) {
        root.setAttribute('aria-valuenow', value.toFixed(digits));
        root.setAttribute('aria-valuetext', `${shown(value)} ${unit}`);
      }
    });
    return stop;
  });
</script>

<div class="gauge" class:large role="meter" aria-label={label} aria-valuemin={min} aria-valuemax={max} aria-valuenow={0} title={note} data-testid="gauge-{id}" bind:this={root}>
  {#if !large}<p class="title">{label}</p>{/if}
  <svg viewBox="0 0 {SIZE} {height}" aria-hidden="true">
    <path d={track} class="track" />
    {#if zonePath}<path d={zonePath} class="zone" />{/if}
    <!-- Seuls l'arc et le chiffre suivent la chaleur : les dizaines de graduations n'ont pas à recalculer leur style. -->
    <g use:heated><path d={track} class="value" pathLength="1000" stroke-dasharray="0 2000" bind:this={arc} /></g>
    {#each minors as value (value)}
      <line {...line(value, inner - (large ? 7 : 5), inner - 2)} class="tick minor" />
    {/each}
    {#each majors as value (value)}
      {@const [tx, ty] = polar(cx, cy, inner - (large ? 24 : 18), angleOf(value))}
      <line {...line(value, inner - (large ? 11 : 8), inner - 2)} class="tick" class:hot={hot(value)} class:origin={zero !== undefined && Math.abs(value - zero) < 1e-6} />
      <text x={tx} y={ty} class="scale" class:hot={hot(value)}>{tickLabel(value)}</text>
    {/each}
    {#if mark !== null}
      {@const [mx, my] = polar(cx, cy, radius, angleOf(mark))}
      <circle cx={mx} cy={my} r={large ? 5 : 3.5} class="mark" data-testid="gauge-{id}-mark" />
    {/if}
    <line x1={cx} y1={cy - radius - band / 2} x2={cx} y2={cy - inner + (large ? 5 : 3)} class="needle" bind:this={needle} />
    <g use:heated><text x={cx} y={cy + readingSize * 0.3} class="reading" class:warm data-testid={valueTestId ?? `gauge-${id}-value`} bind:this={valueNode}>0</text></g>
    <text x={cx} y={cy + (large ? 48 : 32)} class="unit">{unit}</text>
    {#if tickDivisor > 1}<text x={cx} y={cy - (large ? 52 : 38)} class="scale-note">×{whole(tickDivisor)}</text>{/if}
    {#if center}{@render center({ cx, cy })}{/if}
  </svg>
  {#if children}<p class="sub">{@render children()}</p>{/if}
</div>

<style>
  .gauge { display: flex; flex-direction: column; min-width: 0; contain: layout paint style; }
  .title { margin: 0 0 4px; color: var(--text-dim); font-size: var(--type-small); }
  svg { display: block; width: 100%; height: auto; user-select: none; }
  path { fill: none; stroke-linecap: butt; }
  .track { stroke: var(--line); stroke-width: 14; }
  .large .track { stroke-width: 22; }
  .zone { stroke: var(--danger); stroke-width: 5; }
  .large .zone { stroke-width: 7; }
  .value { stroke: var(--heat-text); stroke-width: 14; }
  .large .value { stroke-width: 22; }
  .value:global(.hot) { stroke: var(--danger); }
  /* Sous le zéro, de la dépression : rien ne chauffe, la couleur reste neutre. */
  .value:global(.below) { stroke: var(--metal); }
  .tick { stroke: var(--text-dim); stroke-width: 2; }
  .tick.minor { stroke: var(--line-strong); stroke-width: 1.5; }
  .tick.hot { stroke: var(--danger); }
  .tick.origin { stroke: var(--text); }
  text { fill: var(--text); text-anchor: middle; font-variant-numeric: tabular-nums; }
  .scale { fill: var(--text-dim); font-size: 13px; dominant-baseline: central; }
  .large .scale { font-size: 15px; }
  .scale.hot { fill: var(--danger); }
  .scale-note { fill: var(--text-dim); font-size: 13px; }
  .mark { fill: var(--text); stroke: var(--surface); stroke-width: 2; }
  .needle { stroke: var(--text); stroke-width: 3; }
  .large .needle { stroke-width: 4; }
  .reading { font-family: var(--display); font-size: 44px; font-weight: 800; }
  .large .reading { font-size: 76px; }
  .reading.warm { fill: var(--heat-text); }
  .unit { fill: var(--text-dim); font-size: 13px; }
  .large .unit { font-size: 15px; }
  .sub { margin: 0; color: var(--text-dim); font-size: var(--type-small); font-variant-numeric: tabular-nums; text-align: center; min-height: 1.4em; }
</style>
