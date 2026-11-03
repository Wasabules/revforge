<script lang="ts">
  import Choices from './Choices.svelte';
  import Slider from './Slider.svelte';
  import { ASPIRATIONS, CYLINDERS, FUELS, LAYOUTS, LIMITS, RELEASES, ROTOR_COUNTS, TURBO_SIZES, coherentConfig, cylinderCc, isRotary, strokeMm, type EngineConfig } from '../lib/config';
  import { fixed, t } from '../lib/i18n.svelte';
  import { rotaryShape } from '../lib/rotary';

  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();

  const rotary = $derived(isRotary(config));
  const shape = $derived(rotaryShape(cylinderCc(shown)));
  /** Passer à un rotatif garde ce qui a un sens, et ramène le reste à ce qui existe : 2 rotors, essence. */
  function chooseLayout(layout: EngineConfig['layout']) {
    const next = coherentConfig({ ...config, layout });
    onapply({ layout, cylinders: next.cylinders, fuel: next.fuel });
  }
</script>

{#if config.electric}
  <p class="help">{t('builder.electricHelp')}</p>
{:else}
  <p class="summary" data-testid="summary">
    {#if rotary}
      {t('builder.summary.rotary', { rotors: shown.cylinders, litres: fixed(shown.displacementCc / 1000, 1), cc: Math.round(cylinderCc(shown)), r: Math.round(shape.radius), e: fixed(shape.eccentricity, 1), b: Math.round(shape.width) })}
    {:else}
      {t('builder.summary', { cylinders: shown.cylinders, litres: fixed(shown.displacementCc / 1000, 1), cc: Math.round(cylinderCc(shown)), stroke: Math.round(strokeMm(shown)) })}
    {/if}
  </p>
  {#if rotary}
    <Choices label={t('builder.rotors')} options={ROTOR_COUNTS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  {:else}
    <Choices label={t('builder.cylinders')} options={CYLINDERS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  {/if}
  <Choices label={t('builder.layout')} options={LAYOUTS} value={config.layout} onchange={chooseLayout} />
  <Slider label={t('builder.displacement')} value={config.displacementCc} {...LIMITS.displacementCc} format={(value) => `${value} ${t('unit.cc')}`}
    oninput={(displacementCc) => onlive({ displacementCc })} onchange={(displacementCc) => onapply({ displacementCc })} />
  {#if rotary}
    <p class="help" data-testid="rotary-help">{t('builder.rotary.help')}</p>
  {:else}
    <Slider label={t('builder.bore')} value={config.boreMm} {...LIMITS.boreMm} format={(value) => `${value} mm`}
      oninput={(boreMm) => onlive({ boreMm })} onchange={(boreMm) => onapply({ boreMm })} />
  {/if}
  <Slider label={t('builder.redline')} value={config.redlineRpm} {...LIMITS.redlineRpm} format={(value) => `${value} ${t('unit.rpm')}`}
    oninput={(redlineRpm) => onlive({ redlineRpm })} onchange={(redlineRpm) => onapply({ redlineRpm })} />
  {#if !rotary}
    <Choices label={t('builder.fuel')} options={FUELS} value={config.fuel} onchange={(fuel) => onapply({ fuel })} />
  {/if}
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
