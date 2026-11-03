<script lang="ts">
  import { onDestroy, onMount } from 'svelte';
  import Meter from './Meter.svelte';
  import { animate } from '../lib/canvas';
  import { CLUTCH_SMOKE_C } from '../lib/clutchMechanism';
  import type { EngineConfig } from '../lib/config';
  import { KW_TO_HP, type Telemetry } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';
  import { InstrumentClock, type Readings } from '../lib/instruments';
  import type { Motion } from '../lib/motion';

  interface Props {
    config: EngineConfig;
    motion: Motion;
    readings: Readings;
    telemetry: () => Telemetry;
    running: () => boolean;
    sample: (now: number) => void;
  }
  let { config, motion, readings, telemetry, running, sample }: Props = $props();

  /** Le voyant de patinage du poste de conduite s'allume ici. */
  const SLIP_WARN = 4;

  const clock = new InstrumentClock();
  let root: HTMLElement;
  let inputNode: HTMLElement;
  let outputNode: HTMLElement;
  let wheelsNode: HTMLElement;
  let peakNode: HTMLElement;

  const percent = (value: number) => t('unit.percent', { value: whole(value) });
  const on = (value: () => number) => () => (running() ? value() : 0);

  onMount(() => {
    const stop = animate(root, (_seconds, now) => {
      sample(now);
      clock.run(now);
    }, false);
    const rpm = (value: number) => `${whole(value)} ${t('unit.rpm')}`;
    const stops = [
      clock.number(inputNode, on(() => telemetry().clutchInputRpm), rpm, 10),
      clock.number(outputNode, on(() => telemetry().outputRpm), rpm, 10),
      clock.number(wheelsNode, on(() => telemetry().wheelRpm), rpm),
      clock.number(peakNode, () => readings.peakDb, (value) => (value <= -60 ? '—' : `${whole(value)} dBFS`)),
    ];
    return () => {
      stop();
      stops.forEach((item) => item());
    };
  });

  // ── Le tableau de toutes les valeurs : le jumeau textuel des jauges, recalculé seulement s'il est ouvert. ──
  interface Row { label: string; value: () => string; source: () => string }
  const sim = () => t('source.sim');
  const gasModel = () => t('source.gas');
  const calculated = () => t('source.derived');
  /** La consommation arrive avec un autre chantier : une ligne seulement si le champ existe, et elle reste une estimation. */
  const extra = (key: string) => {
    const value = (telemetry() as unknown as Record<string, unknown>)[key];
    return typeof value === 'number' ? value : null;
  };
  const rows = $derived<Row[]>([
    { label: t('tacho.label'), value: () => `${whole(telemetry().rpm)} ${t('unit.rpm')}`, source: sim },
    { label: t('common.speed'), value: () => `${whole(telemetry().speed)} km/h`, source: sim },
    { label: t('common.gear'), value: () => String(telemetry().gear), source: sim },
    { label: t('common.power'), value: () => `${whole(Math.max(telemetry().power, 0) * KW_TO_HP)} ${t('unit.hp')}`, source: sim },
    { label: t('common.torque'), value: () => `${whole(telemetry().torque)} Nm`, source: sim },
    { label: t('meter.throttle'), value: () => percent(telemetry().throttle * 100), source: sim },
    { label: t('meter.load'), value: () => percent(telemetry().load * 100), source: sim },
    ...(config.electric ? [] : [
      { label: t('gauge.manifold'), value: () => `${fixed(readings.manifoldBar, 2)} bar`, source: gasModel },
      { label: t('gauge.collector'), value: () => `${whole(readings.collectorC)} °C`, source: gasModel },
      { label: t('gauge.tailpipeName'), value: () => `${whole(readings.tailpipeC)} °C`, source: gasModel },
      { label: t('gauge.back'), value: () => `${fixed(readings.backBar, 2)} bar`, source: gasModel },
    ]),
    ...(config.aspiration === 'turbo' && !config.electric ? [
      { label: t('live.turbo'), value: () => `${fixed(telemetry().boost * config.boostBar, 2)} bar`, source: sim },
      { label: t('gauge.turbineName'), value: () => `${whole((telemetry() as Telemetry & { turbineRpm?: number }).turbineRpm ?? motion.turbineRpm(config))} ${t('unit.rpm')}`, source: calculated },
      { label: t('gauge.turbineInlet'), value: () => `${whole(telemetry().turboGasC)} °C`, source: sim },
      ...(config.fuel === 'petrol' && config.antiLag !== 'off' ? [
        { label: t('gauge.antiLagHeat'), value: () => `${fixed(telemetry().antiLagHeatKw, 1)} kW`, source: sim },
        { label: t('gauge.antiLagLost'), value: () => `${whole(telemetry().antiLagLostNm)} Nm`, source: sim },
      ] : []),
    ] : []),
    ...(config.aspiration === 'supercharger' && !config.electric ? [
      { label: t('live.charger'), value: () => `${fixed(telemetry().boost * config.boostBar, 2)} bar`, source: sim },
      { label: t('gauge.chargerName'), value: () => `${whole(telemetry().chargerRpm)} ${t('unit.rpm')}`, source: sim },
      { label: t('gauge.chargerKw'), value: () => `${fixed(telemetry().chargerKw, 1)} kW`, source: sim },
    ] : []),
    // Le calculateur : ce qu'il demande et ce qu'il coupe, lu de la simulation. Avance et richesse sur un moteur à étincelle.
    ...(!config.electric && config.fuel !== 'diesel' ? [
      { label: t('gauge.ecuAdvance'), value: () => `${telemetry().ecuAdvance > 0 ? '+' : ''}${fixed(telemetry().ecuAdvance, 1)}°`, source: sim },
      { label: t('gauge.ecuLambda'), value: () => (telemetry().ecuLambda > 0 ? fixed(telemetry().ecuLambda, 2) : '—'), source: sim },
    ] : []),
    ...(config.electric ? [] : [{ label: t('gauge.ecuFired'), value: () => percent(telemetry().ecuFired * 100), source: sim }]),
    ...(config.tractionControl !== 'off' ? [{ label: t('gauge.ecuTraction'), value: () => percent(telemetry().ecuTraction * 100), source: sim }] : []),
    { label: t('meter.accel'), value: () => `${fixed(readings.accelG, 2)} g`, source: calculated },
    { label: t('meter.slip'), value: () => `${fixed(telemetry().slip, 1)} m/s`, source: sim },
    { label: t('meter.clutchKw'), value: () => `${fixed(telemetry().clutchSlipKw, 1)} kW`, source: sim },
    { label: t('meter.clutchC'), value: () => `${whole(motion.clutchSurface)} °C`, source: calculated },
    { label: t('stat.input'), value: () => `${whole(telemetry().clutchInputRpm)} ${t('unit.rpm')}`, source: sim },
    { label: t('stat.output'), value: () => `${whole(telemetry().outputRpm)} ${t('unit.rpm')}`, source: sim },
    { label: t('stat.wheels'), value: () => `${whole(telemetry().wheelRpm)} ${t('unit.rpm')}`, source: sim },
    { label: t('meter.wear'), value: () => percent(telemetry().boxWear * 100), source: sim },
    { label: t('level.title'), value: () => `${whole(telemetry().levelDbA)} dB(A)`, source: sim },
    ...(extra('fuelLph') !== null ? [{ label: t('table.fuel'), value: () => `${fixed(extra('fuelLph') ?? 0, 1)} L/h`, source: () => t('source.estimate') }] : []),
    ...(extra('oilGph') !== null ? [{ label: t('table.oil'), value: () => `${fixed(extra('oilGph') ?? 0, 1)} g/h`, source: () => t('source.estimate') }] : []),
    { label: t('stat.peak'), value: () => (readings.peakDb <= -60 ? '—' : `${whole(readings.peakDb)} dBFS`), source: sim },
  ]);
  let tableOpen = $state(false);
  let cells = $state<string[]>([]);
  let timer: ReturnType<typeof setInterval> | undefined;

  function refresh() {
    cells = rows.map((row) => row.value());
  }

  function onToggle(event: Event) {
    tableOpen = (event.currentTarget as HTMLDetailsElement).open;
    clearInterval(timer);
    if (tableOpen) {
      refresh();
      timer = setInterval(refresh, 250);
    }
  }

  onDestroy(() => clearInterval(timer));
</script>

<div class="dynamics" bind:this={root} data-testid="dynamics">
  <div class="meters">
    <Meter id="throttle" label={t('meter.throttle')} note={t('meter.throttle.note')} unit="%" min={0} max={100} {clock} read={on(() => telemetry().throttle * 100)} format={(value) => whole(value)} />
    <Meter id="load" label={t('meter.load')} note={t('meter.load.note')} unit="%" min={0} max={100} {clock} read={on(() => telemetry().load * 100)} format={(value) => whole(value)} />
    <Meter id="accel" label={t('meter.accel')} note={t('meter.accel.note')} unit="g" min={-1.5} max={1.5} zero={0} ticks={[-1.5, 0, 1.5]} digits={2} signed {clock} read={() => readings.accelG} format={(value) => fixed(value, Math.abs(value) === 1.5 ? 1 : 2)} />
    <Meter id="slip" label={t('meter.slip')} note={t('meter.slip.note')} unit="m/s" min={-2} max={8} zero={0} ticks={[-2, 0, 8]} warnAt={SLIP_WARN} digits={1} signed {clock} read={on(() => telemetry().slip)} format={(value) => fixed(value, 1)} />
    {#if !config.electric}
      <Meter id="clutch-kw" label={t('meter.clutchKw')} note={t('meter.clutchKw.note')} unit="kW" min={0} max={30} digits={1} {clock} read={on(() => telemetry().clutchSlipKw)} format={(value) => fixed(value, 1)} />
      <Meter id="clutch-c" label={t('meter.clutchC')} note={t('meter.clutchC.note')} unit="°C" min={0} max={400} warnAt={CLUTCH_SMOKE_C} {clock} read={() => motion.clutchSurface} format={(value) => whole(value)} />
      <Meter id="wear" label={t('meter.wear')} note={t('meter.wear.note')} unit="%" min={0} max={100} dangerAt={100} {clock} read={() => telemetry().boxWear * 100} format={(value) => whole(value)} />
    {/if}
  </div>
  <dl class="shafts">
    <div><dt>{t('stat.input')}</dt><dd data-testid="stat-input" bind:this={inputNode}>—</dd></div>
    <div><dt>{t('stat.output')}</dt><dd data-testid="stat-output" bind:this={outputNode}>—</dd></div>
    <div><dt>{t('stat.wheels')}</dt><dd data-testid="stat-wheels" bind:this={wheelsNode}>—</dd></div>
    <div><dt>{t('stat.peak')}</dt><dd data-testid="stat-peak" bind:this={peakNode}>—</dd></div>
  </dl>
  <details ontoggle={onToggle}>
    <summary data-testid="readings-toggle">{t('table.title')}</summary>
    {#if tableOpen}
      <div class="scroll">
        <table data-testid="readings-table">
          <thead><tr><th scope="col">{t('table.reading')}</th><th scope="col">{t('table.value')}</th><th scope="col">{t('table.source')}</th></tr></thead>
          <tbody>
            {#each rows as row, i (row.label)}
              <tr><th scope="row">{row.label}</th><td>{cells[i] ?? row.value()}</td><td>{row.source()}</td></tr>
            {/each}
          </tbody>
        </table>
      </div>
    {/if}
  </details>
</div>

<style>
  .dynamics { display: flex; flex-direction: column; gap: 10px; flex: 1; }
  .meters { display: grid; grid-template-columns: repeat(auto-fit, minmax(165px, 1fr)); gap: 8px 16px; }
  .shafts { display: grid; grid-template-columns: repeat(auto-fit, minmax(84px, 1fr)); gap: 4px 12px; margin: 0; padding-top: 4px; }
  dt { color: var(--text-dim); font-size: var(--type-label); letter-spacing: 0.02em; }
  dd { margin: 0; font-size: var(--type-body); font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  details { color: var(--text-dim); font-size: var(--type-small); }
  summary { cursor: pointer; }
  .scroll { overflow-x: auto; }
  table { width: 100%; margin-top: 6px; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  th, td { padding: 2px 8px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; white-space: nowrap; }
  th[scope='row'], thead th:first-child { text-align: left; }
  td:first-of-type { color: var(--text); }
</style>
