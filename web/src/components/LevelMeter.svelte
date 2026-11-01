<script lang="ts">
  import { onMount } from 'svelte';
  import Meter from './Meter.svelte';
  import { animate } from '../lib/canvas';
  import type { Telemetry } from '../lib/engine';
  import { t, whole, type MessageKey } from '../lib/i18n.svelte';
  import { InstrumentClock, nearestLandmark, type LevelLandmark } from '../lib/instruments';

  interface Props {
    telemetry: () => Telemetry;
    running: () => boolean;
    sample: (now: number) => void;
  }
  let { telemetry, running, sample }: Props = $props();

  const clock = new InstrumentClock();
  let root: HTMLElement;
  // Ces textes ne changent qu'en passant un repère ou un seuil : le gabarit ne travaille qu'à ce moment.
  let landmark = $state<LevelLandmark | null>(null);
  let danger = $state<MessageKey | null>(null);

  const read = () => {
    if (!running()) return 0;
    const level = telemetry().levelDbA;
    return level > 5 ? level : 0;
  };

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      sample(now);
      clock.run(now);
    }, false);
    const stopText = clock.add((text) => {
      if (!text) return;
      const db = read();
      landmark = db > 0 ? nearestLandmark(db) : null;
      danger = db >= 120 ? 'listener.danger.120' : db >= 100 ? 'listener.danger.100' : db >= 85 ? 'listener.danger.85' : null;
    });
    return () => {
      stop();
      stopText();
    };
  });
</script>

<div class="level" bind:this={root} data-testid="level-meter">
  <Meter id="level" label={t('level.title')} note={t('listener.estimate')} unit="dB(A)" min={20} max={140} ticks={[30, 60, 90, 120]} warnAt={85} dangerAt={100} {clock} {read}
    format={(value) => (value > 5 ? whole(value) : '—')} />
  <p class="landmark" data-testid="level-landmark">
    {#if landmark}{t('listener.like', { landmark: t(`listener.landmark.${landmark}`) })}{:else}{t('level.off')}{/if}
    {#if danger}<span class="warn" data-testid="level-danger"><i aria-hidden="true">!</i>{t(danger)}</span>{/if}
  </p>
</div>

<style>
  .landmark { margin: 2px 0 0; min-height: 2.8em; color: var(--text-dim); font-size: var(--type-small); }
  .warn { display: block; color: var(--text); }
  i { display: inline-block; width: 1.2em; margin-right: 4px; background: var(--warn); color: var(--on-warn); font-style: normal; font-weight: 700; text-align: center; }
</style>
