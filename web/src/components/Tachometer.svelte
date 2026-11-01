<script lang="ts">
  import { onMount } from 'svelte';
  import Gauge from './Gauge.svelte';
  import { animate } from '../lib/canvas';
  import { digitColumns } from '../lib/digits';
  import { InstrumentClock } from '../lib/instruments';
  import { t, whole } from '../lib/i18n.svelte';

  interface Props {
    /** Le régime et la vitesse, lissés par l'horloge commune. */
    rpm: () => number;
    speed: () => number;
    gear: () => number;
    redline: number;
    idle: number;
    running: boolean;
    showRoad: boolean;
    /** Lit la simulation pour cette image : à appeler avant de lire les jauges. */
    sample: (now: number) => void;
  }
  let { rpm, speed, gear, redline, idle, running, showRoad, sample }: Props = $props();

  const clock = new InstrumentClock();
  // Une graduation de plus au-delà du rupteur : la zone rouge reste visible.
  const scaleMax = $derived(Math.ceil(redline / 1000) * 1000 + (redline % 1000 === 0 ? 1000 : 0));
  let root: HTMLElement;
  let gearNode = $state<SVGTextElement>();
  let speedNode = $state<SVGTextElement>();
  /** Au centre du cadran de 320 de large, décalé à droite. */
  const SPEED_X = 204;
  const ROAD_SIZE = 40;

  function writeSpeed(node: SVGTextElement, kmh: number) {
    const text = whole(kmh);
    node.textContent = text;
    node.setAttribute('x', digitColumns(text, SPEED_X, ROAD_SIZE));
  }

  // Moteur coupé, l'aiguille retombe : on ne garde pas le dernier régime lu.
  const read = () => (running ? Math.max(rpm(), 0) : 0);

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      sample(now);
      clock.run(now);
    }, false);
    let lastGear = NaN;
    let lastSpeed = NaN;
    const road = clock.add((text) => {
      if (!text) return;
      const g = gear();
      if (gearNode && g !== lastGear) {
        lastGear = g;
        gearNode.textContent = g === 0 ? 'N' : g < 0 ? 'R' : String(g);
      }
      const s = Math.round(speed());
      if (speedNode && s !== lastSpeed) {
        lastSpeed = s;
        writeSpeed(speedNode, s);
      }
    });
    return () => {
      stop();
      road();
    };
  });

  // Rouvrir la zone route : ce nœud est neuf, il attend sa première écriture.
  $effect(() => {
    if (!showRoad) return;
    if (gearNode) gearNode.textContent = gear() === 0 ? 'N' : gear() < 0 ? 'R' : String(gear());
    if (speedNode) writeSpeed(speedNode, Math.round(speed()));
  });
</script>

<div class="tach" bind:this={root}>
  <Gauge id="rpm" large label={t('tacho.label')} note={t('tacho.note', { redline: whole(redline) })} unit={t('unit.rpm')}
    min={0} max={scaleMax} step={1000} minor={scaleMax > 12000 ? 500 : 200} tickDivisor={1000} sweep={270}
    zone={{ from: redline, to: scaleMax }} mark={idle > 0 ? idle : null} {clock} {read} digits={0} valueTestId="rpm">
    {#snippet center({ cx, cy })}
      {#if showRoad}
        <text x={cx - 44} y={cy + 92} class="road" data-testid="gear" bind:this={gearNode}>N</text>
        <text x={cx - 44} y={cy + 110} class="caption">{t('tacho.gear')}</text>
        <text x={SPEED_X} y={cy + 92} class="road" data-testid="speed" bind:this={speedNode}>0</text>
        <text x={cx + 44} y={cy + 110} class="caption">km/h</text>
      {/if}
    {/snippet}
  </Gauge>
  <p class="redline"><span data-testid="tach-redline">{t('tacho.redline', { rpm: whole(redline) })}</span>{#if idle > 0}<i aria-hidden="true"></i>{t('tacho.idle', { rpm: whole(idle) })}{/if}</p>
</div>

<style>
  .tach { width: 100%; max-width: var(--tach-max, 440px); margin: 0 auto; }
  .road { fill: var(--text); text-anchor: middle; font-family: var(--display); font-size: 40px; font-weight: 800; }
  .caption { fill: var(--text-dim); text-anchor: middle; font-size: 13px; }
  .redline i { display: inline-block; width: 7px; height: 7px; margin: 0 4px 0 10px; border-radius: 50%; background: var(--text); vertical-align: 1px; }
  .redline { margin: 2px 0 0; color: var(--text-dim); font-size: var(--type-small); text-align: center; font-variant-numeric: tabular-nums; }
</style>
