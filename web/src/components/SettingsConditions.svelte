<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import Slider from './Slider.svelte';
  import { LIMITS, START_TEMPS, type EngineConfig } from '../lib/config';
  import { t, whole } from '../lib/i18n.svelte';

  /** Conditions : l'air autour de la voiture, l'altitude, et le moteur chaud ou froid au démarrage. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();
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
</style>
