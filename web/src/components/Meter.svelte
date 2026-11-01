<script lang="ts">
  import { heated } from '../lib/heat';
  import { onMount, type Snippet } from 'svelte';
  import { WIDTHS, type InstrumentClock } from '../lib/instruments';
  import { lang, t } from '../lib/i18n.svelte';

  interface Props {
    id: string;
    label: string;
    note?: string;
    unit: string;
    min: number;
    max: number;
    /** D'où part la barre : 0 au milieu pour une grandeur qui change de signe. */
    zero?: number;
    /** Des repères sous la barre, écrits en clair. */
    ticks?: number[];
    /** Les seuils d'alerte : au-delà, la barre change de couleur. */
    warnAt?: number | null;
    dangerAt?: number | null;
    digits?: number;
    signed?: boolean;
    clock: InstrumentClock;
    read: () => number;
    format: (value: number) => string;
    children?: Snippet;
  }
  let { id, label, note, unit, min, max, zero, ticks = [], warnAt = null, dangerAt = null, digits = 0, signed = false, clock, read, format, children }: Props = $props();

  let root: HTMLElement;
  let fill: SVGRectElement;
  let valueNode: HTMLElement;

  const place = (value: number) => (Math.min(Math.max(value, min), max) - min) / (max - min);
  const shown = (value: number) => {
    const text = format(Math.abs(value));
    if (value < 0) return `−${text}`;
    return signed && value > 0 ? `+${text}` : text;
  };
  const origin = $derived(zero ?? min);

  onMount(() => {
    let lastX = -1;
    let lastWidth = -1;
    let lastTone = 0;
    let lastKey = NaN;
    let lastLang = lang();
    const quantum = 10 ** digits;
    return clock.add((text, aria) => {
      const value = read();
      const here = Math.round(place(value) * 1000);
      const home = Math.round(place(origin) * 1000);
      const x = Math.min(here, home);
      const width = Math.abs(here - home);
      if (x !== lastX) {
        lastX = x;
        fill.setAttribute('x', WIDTHS[x]);
      }
      if (width !== lastWidth) {
        lastWidth = width;
        fill.setAttribute('width', WIDTHS[width]);
      }
      const tone = dangerAt !== null && value >= dangerAt ? 2 : warnAt !== null && value >= warnAt ? 1 : 0;
      if (tone !== lastTone) {
        fill.classList.toggle('warn', tone === 1);
        fill.classList.toggle('danger', tone === 2);
        lastTone = tone;
      }
      if (text) {
        const key = Math.round(value * quantum);
        if (key !== lastKey || lang() !== lastLang) {
          lastKey = key;
          lastLang = lang();
          valueNode.textContent = `${shown(key / quantum)} ${unit}`.trim();
        }
      }
      if (aria) {
        root.setAttribute('aria-valuenow', value.toFixed(digits));
        root.setAttribute('aria-valuetext', `${shown(value)} ${unit}`.trim());
      }
    });
  });
</script>

<div class="meter" role="meter" aria-label={label} aria-valuemin={min} aria-valuemax={max} aria-valuenow={0} title={note} data-testid="meter-{id}" bind:this={root}>
  <div class="head">
    <span class="label">{label}</span>
    <span class="value" data-testid="meter-{id}-value" bind:this={valueNode}>{t('meter.none')}</span>
  </div>
  <svg viewBox="0 0 1000 6" preserveAspectRatio="none" aria-hidden="true" use:heated>
    <rect x="0" y="0" width="1000" height="6" class="track" />
    <rect x="0" y="0" width="0" height="6" class="fill" bind:this={fill} />
    {#if zero !== undefined}<rect x={place(zero) * 1000 - 2} y="0" width="4" height="6" class="zero" />{/if}
  </svg>
  {#if ticks.length}
    <div class="ticks" aria-hidden="true">
      {#each ticks as tick (tick)}
        <span style:left="{place(tick) * 100}%">{tick < 0 ? `−${format(-tick)}` : format(tick)}</span>
      {/each}
    </div>
  {/if}
  {#if children}{@render children()}{/if}
</div>

<style>
  .meter { min-width: 0; contain: layout paint style; }
  .head { display: flex; justify-content: space-between; align-items: baseline; gap: 8px; margin-bottom: 3px; }
  .label { color: var(--text-dim); font-size: var(--type-label); letter-spacing: 0.02em; }
  .value { font-size: var(--type-body); font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  svg { display: block; width: 100%; height: 8px; overflow: hidden; }
  .track { fill: var(--line); }
  .fill { fill: var(--heat-text); }
  .fill:global(.warn) { fill: var(--warn); }
  .fill:global(.danger) { fill: var(--danger); }
  .zero { fill: var(--text-dim); }
  .ticks { position: relative; height: 16px; margin-top: 2px; color: var(--text-dim); font-size: var(--type-tiny); font-variant-numeric: tabular-nums; }
  .ticks span { position: absolute; transform: translateX(-50%); white-space: nowrap; }
  .ticks span:first-child { transform: none; }
  .ticks span:last-child { transform: translateX(-100%); }
</style>
