<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import TunedSlider from './TunedSlider.svelte';
  import { CAMSHAFTS, LIMITS, VALVE_SPRINGS, VALVE_TIMINGS, isRotary, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  /** Distribution : l'arbre à cames et ses soupapes. Un rotatif et un deux-temps n'en ont pas : la section ne s'affiche pas. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    /** La fiche du C++ : le calage et ce que la came calcule (`rf_cam_spec`). */
    spec?: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  const valves = $derived(!isRotary(config) && config.cycle !== 'two_stroke');
  const cam = $derived(spec?.cam);
  const twoStep = $derived(config.valveTiming === 'two_step' || config.valveTiming === 'phased_two_step');
  // Ce que le C++ calcule de la distribution : l'affolement, au-dessus ou au-dessous de la zone rouge.
  const floating = $derived(spec && spec.valves.floatRpm > 0 ? spec.valves : null);
  const margin = $derived(floating && spec ? (floating.floatRpm / Math.max(spec.redline, 1) - 1) * 100 : 0);
</script>

{#if valves}
  <SettingsSection id="valvetrain" title={t('builder.section.valvetrain')}>
    <div>
      <Choices label={t('builder.camshaft')} options={CAMSHAFTS} value={config.camshaft} onchange={(camshaft) => onapply({ camshaft })} />
      <p class="help" data-testid="camshaft-help">{CAMSHAFTS.find((item) => item.value === config.camshaft)?.help}</p>
    </div>
    <!-- Le profil de came : 0, d'origine. Le curseur part d'une came de route. -->
    <TunedSlider label={t('builder.camDuration')} value={shown.camDurationDeg} {...LIMITS.camDurationDeg} start={250} testid="cam-duration"
      format={(value) => t('builder.camDuration.value', { deg: value })} help={t('builder.camDuration.help')}
      onlive={(camDurationDeg) => onlive({ camDurationDeg })} onapply={(camDurationDeg) => onapply({ camDurationDeg })} />
    <TunedSlider label={t('builder.camLift')} value={shown.camLiftMm} {...LIMITS.camLiftMm} start={10} testid="cam-lift"
      format={(value) => t('builder.camLift.value', { mm: fixed(value, 1) })} help={t('builder.camLift.help')}
      onlive={(camLiftMm) => onlive({ camLiftMm })} onapply={(camLiftMm) => onapply({ camLiftMm })} />
    <TunedSlider label={t('builder.lsa')} value={shown.lobeSeparationDeg} {...LIMITS.lobeSeparationDeg} start={112} testid="lsa"
      format={(value) => t('builder.lsa.value', { deg: value })} help={t('builder.lsa.help')}
      onlive={(lobeSeparationDeg) => onlive({ lobeSeparationDeg })} onapply={(lobeSeparationDeg) => onapply({ lobeSeparationDeg })} />
    <div data-testid="valve-timing">
      <Choices label={t('builder.valveTiming')} options={VALVE_TIMINGS} value={config.valveTiming} onchange={(valveTiming) => onapply({ valveTiming })} />
      <p class="help">{VALVE_TIMINGS.find((item) => item.value === config.valveTiming)?.help}</p>
    </div>
    {#if cam && cam.ivc > 0}
      <!-- Ce que la came calcule : rien de saisi. -->
      <div class="figures" data-testid="cam-spec">
        <p class="help" data-testid="cam-timing">{t('builder.cam.timing', { ivo: whole(360 - cam.ivo), ivc: whole(cam.ivc - 540), evo: whole(180 - cam.evo), evc: whole(cam.evc - 360) })}</p>
        <p class="help" data-testid="cam-figures">{t('builder.cam.figures', { overlap: whole(cam.overlap), ratio: fixed(cam.effectiveRatio, 1), kpa: whole(cam.idleKpa), residual: whole(cam.idleResidual * 100), cov: whole(cam.idleCov * 100) })}</p>
        {#if twoStep}
          <p class="help" data-testid="cam-switch">{cam.switchRpm > 0 ? t('builder.cam.switch', { rpm: whole(Math.round(cam.switchRpm / 50) * 50) }) : t('builder.cam.noSwitch')}</p>
        {/if}
      </div>
    {/if}
    <div data-testid="valve-springs">
      <Choices label={t('builder.valveSprings')} options={VALVE_SPRINGS} value={config.valveSprings} onchange={(valveSprings) => onapply({ valveSprings })} />
      <p class="help">{VALVE_SPRINGS.find((item) => item.value === config.valveSprings)?.help}</p>
      {#if floating}
        <p class="help" data-testid="valve-float" class:warn={margin < 0}>
          {t(margin < 0 ? 'builder.valveFloat.below' : 'builder.valveFloat', { rpm: whole(floating.floatRpm), margin: fixed(Math.abs(margin), 0) })}
        </p>
        <p class="help" data-testid="valve-parts">
          {t('builder.valveParts', { mass: whole(floating.massG), seat: whole(floating.seatN), open: whole(floating.openN), hz: whole(floating.naturalHz) })}
        </p>
      {/if}
    </div>
  </SettingsSection>
{/if}

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .figures { display: grid; gap: 4px; font-variant-numeric: tabular-nums; }
  .warn { color: var(--text); font-weight: 500; }
</style>
