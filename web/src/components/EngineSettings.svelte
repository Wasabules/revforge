<script lang="ts">
  import Choices from './Choices.svelte';
  import Slider from './Slider.svelte';
  import { AIR_FILTERS, ASPIRATIONS, CYLINDERS, FUELS, LAYOUTS, LIMITS, RELEASES, TURBO_SIZES, cylinderCc, strokeMm, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';
  import { PISTON_SPEED_LIMIT, moveStroke, pistonSpeed, strokeOutside, strokeRange, strokeShape, strokeThumb, type StrokeAnchor } from '../lib/stroke';

  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    spec: Spec;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, onapply, onlive }: Props = $props();

  // Le curseur de course n'écrit que la cylindrée, et se souvient d'où il est parti le temps qu'on revienne.
  let strokeAnchor: StrokeAnchor | null = null;
  function strokeMoved(target: number, send: (next: Partial<EngineConfig>) => void) {
    const moved = moveStroke(shown, target, strokeAnchor);
    strokeAnchor = moved.anchor;
    send({ displacementCc: moved.displacementCc });
  }
  const stroke = $derived(strokeMm(shown));
  const redlineSpeed = $derived(pistonSpeed(stroke, shown.redlineRpm));
</script>

{#if config.electric}
  <p class="help">{t('builder.electricHelp')}</p>
{:else}
  <p class="summary" data-testid="summary">
    {t('builder.summary', { cylinders: shown.cylinders, litres: fixed(shown.displacementCc / 1000, 1), cc: Math.round(cylinderCc(shown)), stroke: Math.round(strokeMm(shown)) })}
  </p>
  <Choices label={t('builder.cylinders')} options={CYLINDERS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => onapply({ cylinders })} />
  <Choices label={t('builder.layout')} options={LAYOUTS} value={config.layout} onchange={(layout) => onapply({ layout })} />
  <Slider label={t('builder.displacement')} value={shown.displacementCc} {...LIMITS.displacementCc} format={(value) => `${value} ${t('unit.cc')}`}
    oninput={(displacementCc) => onlive({ displacementCc })} onchange={(displacementCc) => onapply({ displacementCc })} />
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
  <div>
    <Choices label={t('builder.airFilter')} options={AIR_FILTERS} value={config.airFilter} onchange={(airFilter) => onapply({ airFilter })} />
    <p class="help" data-testid="air-filter-help">{AIR_FILTERS.find((item) => item.value === config.airFilter)?.help}</p>
    <p class="help" data-testid="air-filter-figures">{t('builder.airFilter.figures', { kpa: fixed(spec.intakeDropKpa, 1), c: Math.round(spec.intakeAirC) })}</p>
  </div>
{/if}

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .summary { margin: 0; font-weight: 500; font-size: var(--type-body); font-variant-numeric: tabular-nums; }
</style>
