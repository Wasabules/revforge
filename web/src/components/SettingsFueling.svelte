<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import { AIR_FILTERS, DIESEL_INJECTIONS, FUEL_SYSTEMS, ON_OFF, isRotary, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';

  /** Alimentation : le carburant (carburateur, injection, diesel), ce qui revient de l'échappement, et l'air qui entre. */
  interface Props {
    config: EngineConfig;
    spec: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
  }
  let { config, spec, onapply }: Props = $props();

  const rotary = $derived(isRotary(config));
  const diesel = $derived(config.fuel === 'diesel' && !rotary);
</script>

<SettingsSection id="fueling" title={t('builder.section.fueling')}>
  {#if !diesel}
    <div data-testid="fuel-system">
      <Choices label={t('builder.fuelSystem')} options={rotary ? FUEL_SYSTEMS.map((item) => (item.value === 'multi' ? { value: item.value, label: t('fuelSystem.multi.rotary') } : item)) : FUEL_SYSTEMS} value={config.fuelSystem} onchange={(fuelSystem) => onapply({ fuelSystem })} />
      <p class="help">{FUEL_SYSTEMS.find((item) => item.value === config.fuelSystem)?.help}</p>
      {#if spec && spec.carb.throats > 0 && config.fuelSystem !== 'injection'}
        <p class="help" data-testid="carb-spec">{t('builder.carb.spec', { throats: spec.carb.throats, mm: Math.round(spec.carb.throatMm), kpa: fixed(spec.carb.dropKpa, 1) })}</p>
      {/if}
      {#if (config.fuelSystem === 'single' || config.fuelSystem === 'multi') && config.aspiration !== 'natural'}
        <p class="help" data-testid="carb-blown">{t('builder.carb.blown')}</p>
      {/if}
    </div>
  {:else}
    <div data-testid="diesel-injection">
      <Choices label={t('builder.dieselInjection')} options={DIESEL_INJECTIONS} value={config.dieselInjection} onchange={(dieselInjection) => onapply({ dieselInjection })} />
      <p class="help">{DIESEL_INJECTIONS.find((item) => item.value === config.dieselInjection)?.help}</p>
    </div>
  {/if}
  {#if !rotary}
    <div data-testid="egr">
      <Choices label={t('builder.egr')} options={ON_OFF} value={config.egr ? 'on' : 'off'} onchange={(value) => onapply({ egr: value === 'on' })} />
      <p class="help">{t('builder.egr.help')}</p>
    </div>
  {/if}
  <!-- Un filtre à particules suit un diesel, ou une injection directe d'essence : les seuls à faire de la suie. -->
  {#if diesel || config.fuelSystem === 'direct'}
    <div data-testid="dpf">
      <Choices label={t('builder.dpf')} options={ON_OFF} value={config.particulateFilter ? 'on' : 'off'} onchange={(value) => onapply({ particulateFilter: value === 'on' })} />
      <p class="help">{t('builder.dpf.help')}</p>
    </div>
  {/if}
  <div>
    <Choices label={t('builder.airFilter')} options={AIR_FILTERS} value={config.airFilter} onchange={(airFilter) => onapply({ airFilter })} />
    <p class="help" data-testid="air-filter-help">{AIR_FILTERS.find((item) => item.value === config.airFilter)?.help}</p>
    <p class="help" data-testid="air-filter-figures">{t('builder.airFilter.figures', { kpa: fixed(spec.intakeDropKpa, 1), c: Math.round(spec.intakeAirC) })}</p>
  </div>
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
