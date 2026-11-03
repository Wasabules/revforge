<script lang="ts">
  import { onMount } from 'svelte';
  import Gauge from './Gauge.svelte';
  import { animate } from '../lib/canvas';
  import type { EngineConfig } from '../lib/config';
  import { KW_TO_HP, type Spec, type Telemetry } from '../lib/engine';
  import { lang, t, whole } from '../lib/i18n.svelte';
  import { InstrumentClock, niceStep, type Readings } from '../lib/instruments';
  import type { Motion } from '../lib/motion';

  interface Props {
    config: EngineConfig;
    spec: Spec;
    motion: Motion;
    readings: Readings;
    telemetry: () => Telemetry;
    running: () => boolean;
    /** Lit la simulation pour cette image. `gas` : ces jauges lisent le modèle du gaz, il doit avancer. */
    sample: (now: number, gas: boolean) => void;
  }
  let { config, spec, motion, readings, telemetry, running, sample }: Props = $props();

  const clock = new InstrumentClock();
  let root: HTMLElement;
  let turbineNode = $state<HTMLElement>();
  let tailpipeNode = $state<HTMLElement>();
  let intakeNode = $state<HTMLElement>();

  const turbo = $derived(config.aspiration === 'turbo' && !config.electric);
  // L'air d'admission n'a d'intérêt que s'il n'est plus celui du dehors : un filtre sous le capot.
  const hotAir = $derived(!config.electric && (config.airFilter === 'cone' || config.airFilter === 'stacks'));
  const charged = $derived(config.aspiration === 'supercharger' && !config.electric);
  const boosted = $derived(turbo || charged);
  /** Une échelle qui finit sur une graduation : tout ce que la simulation peut atteindre y tient. */
  const roundedUp = (value: number, step: number) => Math.ceil(value / step - 1e-9) * step;

  const speedStep = $derived(niceStep(Math.max(spec.topSpeed, 60), 6));
  const speedMax = $derived(roundedUp(Math.max(spec.topSpeed, 60), speedStep));
  const manifoldStep = $derived(boosted && config.boostBar >= 2 ? 1 : 0.5);
  // De la dépression totale à un peu plus que la pression réglée ; sans turbo, l'atmosphère est presque le bout.
  const manifoldMax = $derived(boosted ? roundedUp(config.boostBar * 1.1, 0.5) : 0.5);
  // Sans turbine, un compresseur laisse la ligne libre, mais tout l'air qu'il a poussé doit bien sortir.
  const backMax = $derived(turbo ? roundedUp(Math.max(config.boostBar, 1), 0.5) : charged ? 1 : 0.6);
  const backStep = $derived(turbo ? 0.5 : 0.2);
  const powerStep = $derived(niceStep(Math.max(spec.peakHp, 50), 5));
  const powerMax = $derived(roundedUp(Math.max(spec.peakHp, 50) * 1.05, powerStep));

  /** La vitesse de l'arbre si la télémétrie la donne, sinon l'estimation de `Motion`. */
  const turbineRpm = () => (charged ? telemetry().chargerRpm : (telemetry() as Telemetry & { turbineRpm?: number }).turbineRpm ?? motion.turbineRpm(config));

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      motion.meterWanted = true;
      sample(now, true);
      clock.run(now);
    }, false);
    let lastTurbine = NaN;
    let lastLang = lang();
    let lastTailpipe = NaN;
    let lastTailLang = lang();
    let lastIntake = NaN;
    let lastIntakeLang = lang();
    const stops = [
      clock.add((text) => {
        if (!text) return;
        if (!turbineNode) return;
        const rpm = Math.round(turbineRpm() / 1000);
        if (rpm === lastTurbine && lang() === lastLang) return;
        lastTurbine = rpm;
        lastLang = lang();
        turbineNode.textContent = t(charged ? 'gauge.charger' : 'gauge.turbine', { rpm: whole(rpm * 1000) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!tailpipeNode) return;
        const c = Math.round(readings.tailpipeC);
        if (c === lastTailpipe && lang() === lastTailLang) return;
        lastTailpipe = c;
        lastTailLang = lang();
        tailpipeNode.textContent = t('gauge.tailpipe', { c: whole(c) });
      }),
      clock.add((text) => {
        if (!text) return;
        if (!intakeNode) return;
        const c = Math.round(running() ? telemetry().intakeAirC : 20);
        if (c === lastIntake && lang() === lastIntakeLang) return;
        lastIntake = c;
        lastIntakeLang = lang();
        intakeNode.textContent = t('gauge.intakeAir', { c: whole(c) });
      }),
    ];
    return () => {
      stop();
      stops.forEach((item) => item());
    };
  });
</script>

<div class="bank" bind:this={root} data-testid="gauge-bank">
  <Gauge id="speed" label={t('common.speed')} note={t('gauge.speed.note')} unit="km/h" min={0} max={speedMax} step={speedStep} minor={speedStep / 2} {clock} read={() => motion.speed}>
    {t('gauge.speed.top', { speed: whole(spec.topSpeed) })}
  </Gauge>
  {#if config.electric}
    <Gauge id="power" label={t('common.power')} note={t('gauge.power.note')} unit={t('unit.hp')} min={0} max={powerMax} step={powerStep} {clock} read={() => (running() ? Math.max(telemetry().power, 0) * KW_TO_HP : 0)}>
      {t('gauge.electric')}
    </Gauge>
  {:else}
    <Gauge id="manifold" label={t('gauge.manifold')} note={t('gauge.manifold.note')} unit="bar" min={-1} max={manifoldMax} step={manifoldStep} minor={manifoldStep / 5} zero={0} mark={boosted ? config.boostBar : null}
      digits={2} signed tickDigits={1} {clock} read={() => readings.manifoldBar}>
      {#if boosted}<span data-testid="gauge-turbine" bind:this={turbineNode}></span>{:else}{t('gauge.manifold.natural')}{/if}
      {#if hotAir}<span data-testid="gauge-intake-air" bind:this={intakeNode}></span>{/if}
    </Gauge>
    <Gauge id="collector" label={t('gauge.collector')} note={t('gauge.collector.note')} unit="°C" min={0} max={1000} step={200} minor={100} warm {clock} read={() => readings.collectorC}>
      <span data-testid="gauge-tailpipe" bind:this={tailpipeNode}></span>
    </Gauge>
    <Gauge id="back" label={t('gauge.back')} note={t('gauge.back.note')} unit="bar" min={0} max={backMax} step={backStep} minor={backStep / 2} digits={2} tickDigits={1} {clock} read={() => Math.max(readings.backBar, 0)}>
      {t('gauge.back.sub')}
    </Gauge>
  {/if}
</div>

<style>
  .bank { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 12px; }
</style>
