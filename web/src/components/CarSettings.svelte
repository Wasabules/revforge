<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsConditions from './SettingsConditions.svelte';
  import Slider from './Slider.svelte';
  import { BODIES, DRIVES, INSULATIONS, LIMITS, PLACEMENTS, type Body, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';

  interface Props {
    config: EngineConfig;
    /** Pendant le glissé d'un curseur : ce que montrent les conditions. */
    shown: EngineConfig;
    spec: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  function chooseBody(value: Body) {
    const chosen = BODIES.find((item) => item.value === value);
    if (!chosen) return;
    // Une carrosserie peut imposer son isolation : en la quittant, celle qu'elle avait imposée ne la suit pas.
    const imposed = BODIES.find((item) => item.value === config.body)?.insulation;
    const insulation = chosen.insulation ?? (imposed === config.insulation ? 'standard' : config.insulation);
    onapply({ body: chosen.value, massKg: chosen.massKg, rimInch: chosen.rimInch, drive: chosen.drive, placement: chosen.placement, insulation });
  }
</script>

<Choices label={t('builder.body')} options={BODIES} value={config.body} onchange={chooseBody} />
<Choices label={t('builder.drive')} options={DRIVES} value={config.drive} onchange={(drive) => onapply({ drive })} />
<Choices label={t('builder.placement')} options={PLACEMENTS} value={config.placement} onchange={(placement) => onapply({ placement })} />
<div>
  <Choices label={t('builder.insulation')} options={INSULATIONS} value={config.insulation} onchange={(insulation) => onapply({ insulation })} />
  <p class="help">{INSULATIONS.find((item) => item.value === config.insulation)?.help}</p>
</div>
<Slider label={t('builder.mass')} value={config.massKg} {...LIMITS.massKg} format={(value) => `${value} kg`}
  oninput={(massKg) => onlive({ massKg })} onchange={(massKg) => onapply({ massKg })} />
<Slider label={t('builder.rims')} value={config.rimInch} {...LIMITS.rimInch} format={(value) => `${value} ${t('unit.inches')}`}
  oninput={(rimInch) => onlive({ rimInch })} onchange={(rimInch) => onapply({ rimInch })} />
{#if spec.starter.kw > 0}
  <p class="help" data-testid="starter">{t('builder.starter', { type: t(spec.starter.direct ? 'builder.starter.direct' : 'builder.starter.reduction'), kw: fixed(spec.starter.kw, 1), pinion: spec.starter.pinion, ring: spec.starter.ring, teeth: spec.starter.helix > 0 ? t('builder.starter.helix', { angle: Math.round(spec.starter.helix) }) : t('builder.starter.straight') })}</p>
{/if}
{#if spec.ratios.length}
  <p class="help" data-testid="gearing">{t('builder.gearing', { ratios: spec.ratios.map((ratio) => fixed(ratio, 2)).join(' · ') })}{config.boxFinal > 0 ? '' : ` ${t('builder.gearing.auto')}`}</p>
{/if}
<!-- Les conditions du trajet : l'air, l'altitude, le moteur au démarrage. Dans son fichier, pour les sujets qui s'en servent. -->
<SettingsConditions {config} {shown} {spec} {onapply} {onlive} />

<style>
  .help { margin: 6px 0 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
