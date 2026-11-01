<script lang="ts">
  import Choices from './Choices.svelte';
  import Slider from './Slider.svelte';
  import { ASPIRATIONS, CYLINDERS, FUELS, LAYOUTS, LIMITS, RELEASES, TURBO_SIZES, cylinderCc, strokeMm, type EngineConfig } from '../lib/config';
  import { fixed, t } from '../lib/i18n.svelte';

  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();
</script>

{#if config.electric}
  <p class="help">{t('builder.electricHelp')}</p>
{:else}
  <p class="summary" data-testid="summary">
    {t('builder.summary', { cylinders: shown.cylinders, litres: fixed(shown.displacementCc / 1000, 1), cc: Math.round(cylinderCc(shown)), stroke: Math.round(strokeMm(shown)) })}
  </p>
  <Choices label={t('builder.cylinders')} options={CYLINDERS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  <Choices label={t('builder.layout')} options={LAYOUTS} value={config.layout} onchange={(layout) => onapply({ layout })} />
  <Slider label={t('builder.displacement')} value={config.displacementCc} {...LIMITS.displacementCc} format={(value) => `${value} ${t('unit.cc')}`}
    oninput={(displacementCc) => onlive({ displacementCc })} onchange={(displacementCc) => onapply({ displacementCc })} />
  <Slider label={t('builder.bore')} value={config.boreMm} {...LIMITS.boreMm} format={(value) => `${value} mm`}
    oninput={(boreMm) => onlive({ boreMm })} onchange={(boreMm) => onapply({ boreMm })} />
  <Slider label={t('builder.redline')} value={config.redlineRpm} {...LIMITS.redlineRpm} format={(value) => `${value} ${t('unit.rpm')}`}
    oninput={(redlineRpm) => onlive({ redlineRpm })} onchange={(redlineRpm) => onapply({ redlineRpm })} />
  <Choices label={t('builder.fuel')} options={FUELS} value={config.fuel} onchange={(fuel) => onapply({ fuel })} />
  <Choices label={t('builder.aspiration')} options={ASPIRATIONS} value={config.aspiration} onchange={(aspiration) => onapply({ aspiration })} />
  {#if config.aspiration === 'turbo'}
    <div>
      <Slider label={t('builder.boost')} value={config.boostBar} {...LIMITS.boostBar} format={(value) => `${fixed(value, 1)} bar`}
        oninput={(boostBar) => onlive({ boostBar })} onchange={(boostBar) => onapply({ boostBar })} />
      <p class="help">{t('builder.boost.help')}</p>
    </div>
    <div>
      <Choices label={t('builder.turboSize')} options={TURBO_SIZES} value={config.turboSize} onchange={(turboSize) => onapply({ turboSize })} />
      <p class="help" data-testid="turbo-size-help">{TURBO_SIZES.find((item) => item.value === config.turboSize)?.help}</p>
    </div>
    <Choices label={t('builder.release')} options={RELEASES} value={config.turboRelease} onchange={(turboRelease) => onapply({ turboRelease })} />
  {/if}
{/if}

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .summary { margin: 0; font-weight: 500; font-size: var(--type-body); font-variant-numeric: tabular-nums; }
</style>
