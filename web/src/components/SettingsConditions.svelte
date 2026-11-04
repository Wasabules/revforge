<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import Slider from './Slider.svelte';
  import { ambientAirOf, REFERENCE_K, REFERENCE_PA } from '../lib/atmosphere';
  import { LIMITS, START_TEMPS, type EngineConfig } from '../lib/config';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  /** Conditions : l'air autour de la voiture, l'altitude, et le moteur chaud ou froid au démarrage. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();

  /** L'air de référence, 20 °C au niveau de la mer : celui de toutes les calibrations. */
  const REFERENCE_DENSITY = REFERENCE_PA / (287 * REFERENCE_K);
  const air = $derived(ambientAirOf(shown));
</script>

<SettingsSection id="conditions" title={t('builder.section.conditions')}>
  <div data-testid="ambient">
    <Slider label={t('builder.ambient')} value={shown.ambientC} {...LIMITS.ambientC} format={(value) => t('builder.ambient.value', { c: value })}
      oninput={(ambientC) => onlive({ ambientC })} onchange={(ambientC) => onapply({ ambientC })} />
    <p class="help">{t('builder.ambient.help')}</p>
  </div>
  <div data-testid="altitude">
    <Slider label={t('builder.altitude')} value={shown.altitudeM} {...LIMITS.altitudeM} format={(value) => t('builder.altitude.value', { m: whole(value) })}
      oninput={(altitudeM) => onlive({ altitudeM })} onchange={(altitudeM) => onapply({ altitudeM })} />
    <p class="help">{t('builder.altitude.help')}</p>
  </div>
  <!-- Ce que la simulation en tire : le même calcul que le C++ (atmosphere.ts). -->
  <p class="air" data-testid="ambient-air">
    {t('builder.ambientAir', { p: whole(air.pressurePa / 100), rho: fixed(air.densityKgM3, 2), share: whole((100 * air.densityKgM3) / REFERENCE_DENSITY), c: whole(air.soundSpeedMs) })}
  </p>
  <!-- Un moteur électrique n'a ni eau ni huile à chauffer. -->
  {#if !config.electric}
    <div data-testid="start-temp">
      <Choices label={t('builder.startTemp')} options={START_TEMPS} value={config.startTemp} onchange={(startTemp) => onapply({ startTemp })} />
      <p class="help">{START_TEMPS.find((item) => item.value === config.startTemp)?.help}</p>
    </div>
  {/if}
</SettingsSection>

<style>
  .help { margin: 6px 0 0; color: var(--text-dim); font-size: var(--type-small); }
  .air { margin: 0; font-size: var(--type-small); font-variant-numeric: tabular-nums; }
</style>
