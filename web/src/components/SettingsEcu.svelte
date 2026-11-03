<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import Slider from './Slider.svelte';
  import TunedSlider from './TunedSlider.svelte';
  import { LIMITS, ON_OFF, REV_LIMITERS, TRACTION_CONTROLS, isRotary, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  /** Calculateur : la carte d'avance et de richesse, le rupteur, le launch control, le passage à fond, l'antipatinage. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    spec: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  /** Avance et richesse se règlent sur un moteur à étincelle : un diesel s'allume par compression, toujours en excès d'air. */
  const spark = $derived(isRotary(config) || config.fuel !== 'diesel');
  /** Un carburateur dose lui-même son mélange : la richesse n'est pas au calculateur (même règle que `fullLoadLambdaOf`). */
  const carburetted = $derived(config.fuelSystem === 'single' || config.fuelSystem === 'multi');
  /** La richesse d'origine à pleine charge, celle de la consommation : d'où part le curseur. */
  const stockLambda = $derived(config.aspiration === 'natural' ? 0.88 : 0.82);
  const signed = (value: number) => (value > 0 ? `+${value}` : String(value));
</script>

<SettingsSection id="ecu" title={t('builder.section.ecu')}>
  {#if spark}
    <div data-testid="advance">
      <Slider label={t('builder.advance')} value={shown.ignitionOffsetDeg} {...LIMITS.ignitionOffsetDeg} format={(value) => t('builder.advance.value', { deg: signed(value) })}
        oninput={(ignitionOffsetDeg) => onlive({ ignitionOffsetDeg })} onchange={(ignitionOffsetDeg) => onapply({ ignitionOffsetDeg })} />
      <p class="help">{t('builder.advance.help')}</p>
    </div>
    {#if carburetted}
      <p class="help" data-testid="lambda-carb">{t('builder.lambda.carb')}</p>
    {:else}
      <!-- 0 : la carte d'origine. Le curseur part de la richesse d'origine de ce moteur à pleine charge. -->
      <TunedSlider label={t('builder.lambda')} value={shown.fullLoadLambda} {...LIMITS.fullLoadLambda} start={stockLambda} testid="lambda"
        format={(value) => t('builder.lambda.value', { lambda: fixed(value, 2) })} help={t('builder.lambda.help')}
        onlive={(fullLoadLambda) => onlive({ fullLoadLambda })} onapply={(fullLoadLambda) => onapply({ fullLoadLambda })} />
    {/if}
    {#if spec.ecu.rpm > 0}
      <!-- Ce que la carte donne, calculé par le C++ : rien n'est écrit à la main. -->
      <p class="figures" data-testid="ecu-figures">
        {t('builder.ecu.figures', { rpm: whole(spec.ecu.rpm), deg: whole(spec.ecu.sparkDeg), share: fixed(spec.ecu.fullShare, 3), egt: whole(spec.ecu.exhaustC), stock: whole(spec.ecu.stockExhaustC) })}
      </p>
    {/if}
  {/if}
  <div data-testid="rev-limiter">
    <Choices label={t('builder.revLimiter')} options={REV_LIMITERS} value={config.revLimiter} onchange={(revLimiter) => onapply({ revLimiter })} />
    <p class="help">{REV_LIMITERS.find((item) => item.value === config.revLimiter)?.help}</p>
  </div>
  <TunedSlider label={t('builder.launch')} value={shown.launchRpm} {...LIMITS.launchRpm} start={4000} none valueLabel={t('builder.launch.rpm')} testid="launch"
    format={(value) => `${value} ${t('unit.rpm')}`} help={t('builder.launch.help')}
    onlive={(launchRpm) => onlive({ launchRpm })} onapply={(launchRpm) => onapply({ launchRpm })} />
  <div data-testid="flat-shift">
    <Choices label={t('builder.flatShift')} options={ON_OFF} value={config.flatShift ? 'on' : 'off'} onchange={(value) => onapply({ flatShift: value === 'on' })} />
    <p class="help">{t('builder.flatShift.help')}</p>
  </div>
  <div data-testid="traction">
    <Choices label={t('builder.traction')} options={TRACTION_CONTROLS} value={config.tractionControl} onchange={(tractionControl) => onapply({ tractionControl })} />
    <p class="help">{TRACTION_CONTROLS.find((item) => item.value === config.tractionControl)?.help}</p>
  </div>
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .figures { margin: 0; font-size: var(--type-small); font-variant-numeric: tabular-nums; }
</style>
