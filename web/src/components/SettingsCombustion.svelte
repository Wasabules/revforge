<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import TunedSlider from './TunedSlider.svelte';
  import { FUEL_GRADES, FUELS, LIMITS, isRotary, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  /** Combustion : le carburant, le taux de compression, l'indice d'octane. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    spec?: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  const rotary = $derived(isRotary(config));
  const diesel = $derived(config.fuel === 'diesel' && !rotary);
  /** Le taux que suppose le moteur d'aujourd'hui (`stockCompressionRatio` en C++) : le point de départ du curseur. */
  const stockRatio = $derived(diesel ? 18 : rotary ? (config.aspiration === 'natural' ? 9.4 : 8.5) : config.aspiration === 'natural' ? 10.5 : 9.5);
  /**
   * Ce qui existe (`compressionRatioOf` en C++) : un diesel ne s'allume plus sous 14:1 (Mazda Skyactiv-D, le plus bas de série), et un
   * rotor n'a plus de creux au-delà de 11,9:1 (Mazda 8C, le plus haut de série).
   */
  const ratioMin = $derived(diesel ? 14 : LIMITS.compressionRatio.min);
  const ratioMax = $derived(rotary ? 11.9 : LIMITS.compressionRatio.max);
  /** La fiche : rendement, et marge au cliquetis de la carte d'avance à pleine charge. */
  const figures = $derived.by(() => {
    if (!spec || spec.combustion.ratio <= 0) return '';
    const share = fixed(spec.combustion.efficiency, 3);
    if (diesel) return t('builder.combustion.diesel', { share });
    const margin = spec.combustion.marginDeg;
    const rpm = whole(spec.combustion.marginRpm);
    if (margin >= 0) return t('builder.combustion.figures', { share, deg: fixed(margin, 1), rpm });
    const sensor = t(spec.combustion.sensor ? 'builder.combustion.sensor' : 'builder.combustion.noSensor');
    return t('builder.combustion.knocking', { share, deg: fixed(-margin, 1), rpm, sensor });
  });
</script>

<SettingsSection id="combustion" title={t('builder.section.combustion')}>
  {#if !rotary}
    <Choices label={t('builder.fuel')} options={FUELS} value={config.fuel} onchange={(fuel) => onapply({ fuel })} />
  {/if}
  <TunedSlider label={t('builder.compression')} value={shown.compressionRatio} min={ratioMin} max={ratioMax} step={LIMITS.compressionRatio.step} start={stockRatio} testid="compression"
    format={(value) => t('builder.compression.value', { ratio: fixed(value, 1) })} help={t('builder.compression.help')}
    onlive={(compressionRatio) => onlive({ compressionRatio })} onapply={(compressionRatio) => onapply({ compressionRatio })} />
  <!-- L'indice d'octane ne dit rien d'un diesel, qui s'allume par compression. -->
  {#if !diesel}
    <div data-testid="fuel-grade">
      <Choices label={t('builder.fuelGrade')} options={FUEL_GRADES} value={config.fuelGrade} onchange={(fuelGrade) => onapply({ fuelGrade })} />
      <p class="help">{FUEL_GRADES.find((item) => item.value === config.fuelGrade)?.help}</p>
    </div>
  {/if}
  {#if figures}
    <p class="help" data-testid="combustion-figures">{figures}</p>
  {/if}
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
