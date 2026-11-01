<script lang="ts">
  import { onDestroy } from 'svelte';
  import type { EngineConfig } from '../lib/config';
  import { IDLE_TELEMETRY, type Telemetry } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  interface Props {
    config: EngineConfig;
    telemetry: () => Telemetry;
    running: () => boolean;
    onreset: () => void;
  }
  let { config, telemetry, running, onreset }: Props = $props();

  /** Des chiffres qu'on a le temps de lire : la télémétrie arrive 30 fois par seconde, un compteur se lit quatre fois. */
  const REFRESH_MS = 250;

  let now = $state.raw<Telemetry>(IDLE_TELEMETRY);
  let live = $state(false);
  const timer = setInterval(() => {
    now = telemetry();
    live = running();
  }, REFRESH_MS);
  onDestroy(() => clearInterval(timer));

  /** Une valeur qui n'a pas encore de sens (à l'arrêt, moins de 100 m parcourus) : un tiret, pas un zéro trompeur. */
  const when = (value: number, digits: number) => (value > 0 ? fixed(value, digits) : '—');
</script>

<div class="consumption" data-testid="conso">
  <header>
    <h2>{t('conso.title')}</h2>
    <button type="button" onclick={onreset} data-testid="conso-reset">{t('conso.reset')}</button>
  </header>

  {#if config.electric}
    <dl>
      <div><dt>{t('conso.energyNow')}</dt><dd data-testid="conso-energy">{live && now.energyKwhPer100km !== 0 ? fixed(now.energyKwhPer100km, 1) : '—'} <small>kWh/100 km</small></dd></div>
      <div><dt>{t('conso.average')}</dt><dd data-testid="conso-energy-avg">{when(now.energyAvgKwhPer100km, 1)} <small>kWh/100 km</small></dd></div>
      <div><dt>{t('conso.trip')}</dt><dd data-testid="conso-energy-trip">{fixed(now.energyTripKwh, 2)} <small>kWh</small></dd></div>
      <div><dt>{t('conso.distance')}</dt><dd data-testid="conso-km">{fixed(now.tripKm, 1)} <small>km</small></dd></div>
    </dl>
    <p class="note">{t('conso.electricNote')}</p>
  {:else}
    <dl>
      <div><dt>{t('conso.now')}</dt><dd data-testid="conso-lph">{fixed(live ? now.fuelLph : 0, 1)} <small>L/h</small></dd></div>
      <div><dt>{t('conso.nowDistance')}</dt><dd data-testid="conso-l100">{live ? when(now.fuelLper100km, 1) : '—'} <small>L/100 km</small></dd></div>
      <div><dt>{t('conso.average')}</dt><dd data-testid="conso-avg">{when(now.fuelAvgLper100km, 1)} <small>L/100 km</small></dd></div>
      <div><dt>{t('conso.trip')}</dt><dd data-testid="conso-trip">{fixed(now.fuelTripL, 2)} <small>L</small></dd></div>
      <div><dt>{t('conso.distance')}</dt><dd data-testid="conso-km">{fixed(now.tripKm, 1)} <small>km</small></dd></div>
      <div><dt>{t('conso.co2')}</dt><dd data-testid="conso-co2">{now.co2GPerKm > 0 ? whole(now.co2GPerKm) : '—'} <small>g/km</small></dd></div>
    </dl>
    <h3>{t('conso.oilTitle')}</h3>
    <dl>
      <div><dt>{t('conso.now')}</dt><dd data-testid="conso-oil-gph">{fixed(live ? now.oilGph : 0, 1)} <small>g/h</small></dd></div>
      <div><dt>{t('conso.average')}</dt><dd data-testid="conso-oil-avg">{now.oilMlPer1000km > 0 ? whole(now.oilMlPer1000km) : '—'} <small>mL/1000 km</small></dd></div>
      <div><dt>{t('conso.trip')}</dt><dd data-testid="conso-oil-trip">{fixed(now.oilTripMl, 1)} <small>mL</small></dd></div>
    </dl>
  {/if}

  <details>
    <summary data-testid="conso-explain">{t('conso.explainTitle')}</summary>
    {#if config.electric}
      <p>{t('conso.explainElectric')}</p>
    {:else}
      <p>{t('conso.explainFuel')}</p>
      <p>{t('conso.explainLimits')}</p>
      <p>{t('conso.explainOil')}</p>
    {/if}
  </details>
</div>

<style>
  .consumption { display: flex; flex-direction: column; gap: 6px; }
  header { display: flex; justify-content: space-between; align-items: center; gap: 8px; }
  h2, h3 { margin: 0; color: var(--text-dim); font-family: var(--display); font-size: 1.125rem; font-weight: 700; line-height: 1.1; text-transform: uppercase; letter-spacing: 0.01em; }
  h3 { padding-top: 6px; }
  header button { min-height: 30px; padding: 0 10px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-small); cursor: pointer; }
  header button:hover { background: var(--surface-high); }
  dl { display: grid; grid-template-columns: repeat(auto-fit, minmax(96px, 1fr)); gap: 4px 12px; margin: 0; }
  dt { color: var(--text-dim); font-size: var(--type-label); letter-spacing: 0.02em; }
  dd { margin: 0; font-size: var(--type-body); font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  small { color: var(--text-dim); font-size: var(--type-tiny); font-weight: 400; }
  .note { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  details { color: var(--text-dim); font-size: var(--type-small); }
  summary { cursor: pointer; }
  details p { margin: 6px 0 0; }
</style>
