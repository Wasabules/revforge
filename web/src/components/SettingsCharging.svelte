<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import Slider from './Slider.svelte';
  import { ANTI_LAGS, ASPIRATIONS, BOOST_CONTROLS, INTERCOOLERS, LIMITS, ON_OFF, RELEASES, SUPERCHARGER_KINDS, TURBO_LAYOUTS, TURBO_SIZES, isRotary, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  /** Suralimentation : turbo ou compresseur, leur régulation, l'air de charge, l'eau et le protoxyde d'azote. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
    /** La fiche : le régime d'activation du protoxyde. */
    spec?: Spec;
  }
  let { config, shown, onapply, onlive, spec }: Props = $props();

  const rotary = $derived(isRotary(config));
  /** Allumé par une étincelle : l'eau et le protoxyde reculent le cliquetis, qui ne concerne pas un diesel. */
  const spark = $derived(rotary || config.fuel !== 'diesel');
</script>

<SettingsSection id="charging" title={t('builder.section.charging')}>
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
    <div>
      <Slider label={t('builder.turboPipe')} value={config.turboPipeMm} {...LIMITS.turboPipeMm} format={(value) => t('builder.turboPipe.value', { m: fixed(value / 1000, 2) })}
        oninput={(turboPipeMm) => onlive({ turboPipeMm })} onchange={(turboPipeMm) => onapply({ turboPipeMm })} />
      <p class="help" data-testid="turbo-pipe-help">{t('builder.turboPipe.help')}</p>
    </div>
    {#if rotary || config.fuel === 'petrol'}
      <div>
        <Choices label={t('builder.antiLag')} options={ANTI_LAGS} value={config.antiLag} onchange={(antiLag) => onapply({ antiLag })} />
        <p class="help" data-testid="antilag-help">{ANTI_LAGS.find((item) => item.value === config.antiLag)?.help}</p>
      </div>
    {/if}
    <!-- Biturbo et géométrie variable. -->
    <div data-testid="turbo-layout">
      <Choices label={t('builder.turboLayout')} options={TURBO_LAYOUTS} value={config.turboLayout} onchange={(turboLayout) => onapply({ turboLayout })} />
      <p class="help">{TURBO_LAYOUTS.find((item) => item.value === config.turboLayout)?.help}</p>
    </div>
    <div data-testid="vgt">
      <Choices label={t('builder.vgt')} options={ON_OFF} value={config.variableGeometry ? 'on' : 'off'} onchange={(value) => onapply({ variableGeometry: value === 'on' })} />
      <p class="help">{t('builder.vgt.help')}</p>
    </div>
    <!-- La régulation de pression. -->
    <div data-testid="boost-control">
      <Choices label={t('builder.boostControl')} options={BOOST_CONTROLS} value={config.boostControl} onchange={(boostControl) => onapply({ boostControl })} />
      <p class="help">{BOOST_CONTROLS.find((item) => item.value === config.boostControl)?.help}</p>
    </div>
  {/if}
  {#if config.aspiration === 'supercharger'}
    <div>
      <Slider label={t('builder.chargerBoost')} value={config.boostBar} {...LIMITS.boostBar} format={(value) => `${fixed(value, 1)} bar`}
        oninput={(boostBar) => onlive({ boostBar })} onchange={(boostBar) => onapply({ boostBar })} />
      <p class="help">{t('builder.chargerBoost.help')}</p>
    </div>
    <div>
      <Choices label={t('builder.chargerKind')} options={SUPERCHARGER_KINDS} value={config.superchargerKind} onchange={(superchargerKind) => onapply({ superchargerKind })} />
      <p class="help" data-testid="charger-kind-help">{SUPERCHARGER_KINDS.find((item) => item.value === config.superchargerKind)?.help}</p>
    </div>
  {/if}
  <!-- L'air de charge : échangeur, eau, protoxyde. -->
  {#if config.aspiration !== 'natural'}
    <div data-testid="intercooler">
      <Choices label={t('builder.intercooler')} options={INTERCOOLERS} value={config.intercooler} onchange={(intercooler) => onapply({ intercooler })} />
      <p class="help">{INTERCOOLERS.find((item) => item.value === config.intercooler)?.help}</p>
    </div>
  {/if}
  {#if spark}
    <div data-testid="water">
      <Choices label={t('builder.water')} options={ON_OFF} value={config.waterInjection ? 'on' : 'off'} onchange={(value) => onapply({ waterInjection: value === 'on' })} />
      <p class="help">{t('builder.water.help')}</p>
    </div>
    <div data-testid="nitrous">
      <Slider label={t('builder.nitrous')} value={shown.nitrousKw} {...LIMITS.nitrousKw} format={(value) => (value === 0 ? t('tuned.none') : t('builder.nitrous.value', { kw: value }))}
        oninput={(nitrousKw) => onlive({ nitrousKw })} onchange={(nitrousKw) => onapply({ nitrousKw })} />
      <p class="help">{t('builder.nitrous.help')}</p>
      {#if config.nitrousKw > 0 && spec && spec.nitrousRpm > 0}<p class="help" data-testid="nitrous-figures">{t('builder.nitrous.figures', { rpm: whole(spec.nitrousRpm) })}</p>{/if}
    </div>
  {/if}
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
