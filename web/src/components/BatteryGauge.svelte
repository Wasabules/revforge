<script lang="ts">
  import { onMount } from 'svelte';
  import Meter from './Meter.svelte';
  import { animate } from '../lib/canvas';
  import type { Telemetry } from '../lib/engine';
  import { fixed, lang, t, whole } from '../lib/i18n.svelte';
  import { InstrumentClock, type Readings } from '../lib/instruments';

  /**
   * La batterie (cold_start.hpp), au poste de conduite, sous le démarreur qu'elle alimente : à vide, sous le démarreur, puis
   * l'alternateur moteur tournant. Sa tension et sa température viennent du C++ (`Readings`) : l'état au départ avant le premier
   * démarrage, le moteur coupé qui refroidit, puis la télémétrie. Une barre et non un cadran : une rangée de plus dans les jauges
   * de droite repoussait le niveau et la dynamique hors de l'écran.
   */
  interface Props {
    readings: Readings;
    telemetry: () => Telemetry;
    running: () => boolean;
    /** Lit la simulation pour cette image. */
    sample: (now: number) => void;
  }
  let { readings, telemetry, running, sample }: Props = $props();

  const clock = new InstrumentClock();
  let root: HTMLElement;
  let subNode = $state<HTMLElement>();

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      sample(now);
      clock.run(now);
    }, false);
    let last = '';
    // Le courant du démarreur pendant le lancement, le préchauffage d'un diesel, le démarrage raté ; sinon sa température,
    // nœud du réseau thermique : au départ, moteur coupé, puis en marche, comme l'eau et l'huile.
    const stopText = clock.add((text) => {
      if (!text || !subNode) return;
      const live = running() ? telemetry() : null;
      const amps = live && live.cranking ? Math.round(live.starterA / 10) * 10 : 0;
      const preheat = live ? Math.ceil(live.preheat) : 0;
      const failed = live ? live.startFailed : 0;
      const celsius = Math.round(readings.batteryC);
      const key = `${amps}|${preheat}|${failed}|${celsius}|${lang()}`;
      if (key === last) return;
      last = key;
      subNode.textContent = preheat > 0 ? t('gauge.battery.preheat', { s: whole(preheat) }) : amps > 0 ? t('gauge.battery.cranking', { a: whole(amps) }) : failed ? t('gauge.battery.failed') : t('gauge.battery.sub', { c: whole(celsius) });
    });
    return () => {
      stop();
      stopText();
    };
  });
</script>

<div class="battery" bind:this={root}>
  <!-- Repères : 9,6 V, le plus bas qu'admet l'essai de charge à mi-courant ; 12,7 V, pleine au repos. -->
  <Meter id="battery" label={t('gauge.battery')} note={t('gauge.battery.note')} unit="V" min={6} max={16} ticks={[6, 9.6, 12.7, 16]} dangerBelow={9.6} digits={1}
    {clock} read={() => readings.batteryV} format={(value) => fixed(value, Number.isInteger(value) ? 0 : 1)}>
    <p class="sub" data-testid="gauge-battery-sub" bind:this={subNode}></p>
  </Meter>
</div>

<style>
  .sub { margin: 0; color: var(--text-dim); font-size: var(--type-small); font-variant-numeric: tabular-nums; min-height: 1.4em; }
</style>
