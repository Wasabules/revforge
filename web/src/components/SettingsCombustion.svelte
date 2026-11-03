<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import TunedSlider from './TunedSlider.svelte';
  import { FUEL_GRADES, FUELS, LIMITS, isRotary, type EngineConfig } from '../lib/config';
  import { fixed, t } from '../lib/i18n.svelte';

  /** Combustion : le carburant, le taux de compression, l'indice d'octane. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();

  const rotary = $derived(isRotary(config));
  const diesel = $derived(config.fuel === 'diesel' && !rotary);
  /** Le taux que suppose le moteur d'aujourd'hui (`StarterSpec::compressionRatio`, `rotaryCompressionRatio`) : le point de départ du curseur. */
  const stockRatio = $derived(diesel ? 18 : rotary ? (config.aspiration === 'natural' ? 9.4 : 8.5) : config.aspiration === 'natural' ? 10.5 : 9.5);
</script>

<SettingsSection id="combustion" title={t('builder.section.combustion')}>
  {#if !rotary}
    <Choices label={t('builder.fuel')} options={FUELS} value={config.fuel} onchange={(fuel) => onapply({ fuel })} />
  {/if}
  <TunedSlider label={t('builder.compression')} value={shown.compressionRatio} {...LIMITS.compressionRatio} start={stockRatio} testid="compression"
    format={(value) => t('builder.compression.value', { ratio: fixed(value, 1) })} help={t('builder.compression.help')}
    onlive={(compressionRatio) => onlive({ compressionRatio })} onapply={(compressionRatio) => onapply({ compressionRatio })} />
  <!-- L'indice d'octane ne dit rien d'un diesel, qui s'allume par compression. -->
  {#if !diesel}
    <div data-testid="fuel-grade">
      <Choices label={t('builder.fuelGrade')} options={FUEL_GRADES} value={config.fuelGrade} onchange={(fuelGrade) => onapply({ fuelGrade })} />
      <p class="help">{FUEL_GRADES.find((item) => item.value === config.fuelGrade)?.help}</p>
    </div>
  {/if}
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
