<script lang="ts">
  import SettingsBlock from './SettingsBlock.svelte';
  import SettingsCharging from './SettingsCharging.svelte';
  import SettingsCombustion from './SettingsCombustion.svelte';
  import SettingsEcu from './SettingsEcu.svelte';
  import SettingsFueling from './SettingsFueling.svelte';
  import SettingsValvetrain from './SettingsValvetrain.svelte';
  import { cylinderCc, strokeMm, type EngineConfig, isRotary } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';
  import { rotaryShape } from '../lib/rotary';

  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    spec: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  const rotary = $derived(isRotary(config));
  const shape = $derived(rotaryShape(cylinderCc(shown)));
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
  <!-- Une section par sujet, chacune dans son fichier : architecture et cycle, combustion, distribution, alimentation, suralimentation, calculateur. -->
  <SettingsBlock {config} {shown} {onapply} {onlive} />
  <SettingsCombustion {config} {shown} {onapply} {onlive} />
  <SettingsValvetrain {config} {shown} {spec} {onapply} {onlive} />
  <SettingsFueling {config} {spec} {onapply} />
  <SettingsCharging {config} {shown} {onapply} {onlive} />
  <SettingsEcu {config} {shown} {onapply} {onlive} />
{/if}

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .summary { margin: 0; font-weight: 500; font-size: var(--type-body); font-variant-numeric: tabular-nums; }
</style>
