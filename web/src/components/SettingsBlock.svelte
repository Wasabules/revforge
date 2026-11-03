<script lang="ts">
  import Choices from './Choices.svelte';
  import SettingsSection from './SettingsSection.svelte';
  import Slider from './Slider.svelte';
  import { CYLINDERS, ENGINE_CYCLES, LAYOUTS, LIMITS, ROTOR_COUNTS, coherentConfig, isRotary, strokeMm, type EngineConfig } from '../lib/config';
  import { fixed, t } from '../lib/i18n.svelte';
  import { PISTON_SPEED_LIMIT, moveStroke, pistonSpeed, strokeOutside, strokeRange, strokeShape, strokeThumb, type StrokeAnchor } from '../lib/stroke';

  /** Architecture et cycle : le nombre de cylindres, leur disposition, leurs dimensions, le régime maximal, le cycle. */
  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, onapply, onlive }: Props = $props();

  // Le curseur de course n'écrit que la cylindrée, et se souvient d'où il est parti le temps qu'on revienne.
  let strokeAnchor: StrokeAnchor | null = null;
  function strokeMoved(target: number, send: (next: Partial<EngineConfig>) => void) {
    const moved = moveStroke(shown, target, strokeAnchor);
    strokeAnchor = moved.anchor;
    send({ displacementCc: moved.displacementCc });
  }
  const stroke = $derived(strokeMm(shown));
  const redlineSpeed = $derived(pistonSpeed(stroke, shown.redlineRpm));

  const rotary = $derived(isRotary(config));
  /** Passer à un rotatif garde ce qui a un sens, et ramène le reste à ce qui existe : 2 rotors, essence. */
  function chooseLayout(layout: EngineConfig['layout']) {
    const next = coherentConfig({ ...config, layout });
    onapply({ layout, cylinders: next.cylinders, fuel: next.fuel });
  }
</script>

<SettingsSection id="block" title={t('builder.section.block')}>
  {#if rotary}
    <Choices label={t('builder.rotors')} options={ROTOR_COUNTS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  {:else}
    <Choices label={t('builder.cylinders')} options={CYLINDERS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  {/if}
  <Choices label={t('builder.layout')} options={LAYOUTS} value={config.layout} onchange={chooseLayout} />
  <Slider label={t('builder.displacement')} value={shown.displacementCc} {...LIMITS.displacementCc} format={(value) => `${value} ${t('unit.cc')}`}
    oninput={(displacementCc) => onlive({ displacementCc })} onchange={(displacementCc) => onapply({ displacementCc })} />
  {#if rotary}
    <p class="help" data-testid="rotary-help">{t('builder.rotary.help')}</p>
  {:else}
    <Slider label={t('builder.bore')} value={shown.boreMm} {...LIMITS.boreMm} format={(value) => `${value} mm`}
      oninput={(boreMm) => onlive({ boreMm })} onchange={(boreMm) => onapply({ boreMm })} />
    <div>
      <!-- Le curseur va de cran en cran, le texte dit la course réelle, même hors plage. -->
      <Slider label={t('builder.stroke')} value={strokeThumb(shown)} {...strokeRange(shown.boreMm, shown.cylinders)} format={() => `${fixed(stroke, 1)} mm`}
        oninput={(target) => strokeMoved(target, onlive)} onchange={(target) => strokeMoved(target, onapply)} />
      <p class="help">{t('builder.stroke.help')}</p>
      <p class="help" data-testid="stroke-derived">{t('builder.stroke.derived', { ratio: fixed(shown.boreMm / stroke, 2), shape: t(`builder.stroke.shape.${strokeShape(shown.boreMm, stroke)}`), speed: fixed(redlineSpeed, 1) })}</p>
      {#if redlineSpeed > PISTON_SPEED_LIMIT}
        <p class="help" data-testid="stroke-fast">{t('builder.stroke.fast', { limit: PISTON_SPEED_LIMIT })}</p>
      {/if}
      {#if strokeOutside(shown)}
        <p class="help" data-testid="stroke-outside">{t('builder.stroke.outside', { min: LIMITS.strokeMm.min, max: LIMITS.strokeMm.max })}</p>
      {/if}
    </div>
  {/if}
  <Slider label={t('builder.redline')} value={config.redlineRpm} {...LIMITS.redlineRpm} format={(value) => `${value} ${t('unit.rpm')}`}
    oninput={(redlineRpm) => onlive({ redlineRpm })} onchange={(redlineRpm) => onapply({ redlineRpm })} />
  <!-- Un rotatif a son propre cycle : ni deux ni quatre temps à choisir. -->
  {#if !rotary}
    <div data-testid="cycle">
      <Choices label={t('builder.cycle')} options={ENGINE_CYCLES} value={config.cycle} onchange={(cycle) => onapply({ cycle })} />
      <p class="help">{ENGINE_CYCLES.find((item) => item.value === config.cycle)?.help}</p>
    </div>
  {/if}
</SettingsSection>

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
