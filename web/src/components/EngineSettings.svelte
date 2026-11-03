<script lang="ts">
  import Choices from './Choices.svelte';
  import Slider from './Slider.svelte';
  import { AIR_FILTERS, ASPIRATIONS, CYLINDERS, FUELS, LAYOUTS, LIMITS, RELEASES, TURBO_SIZES, cylinderCc, strokeMm, type EngineConfig, ANTI_LAGS, CAMSHAFTS, SUPERCHARGER_KINDS, FUEL_SYSTEMS, ROTOR_COUNTS, coherentConfig, isRotary } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t } from '../lib/i18n.svelte';
  import { PISTON_SPEED_LIMIT, moveStroke, pistonSpeed, strokeOutside, strokeRange, strokeShape, strokeThumb, type StrokeAnchor } from '../lib/stroke';
  import { rotaryShape } from '../lib/rotary';

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
  {#if !rotary}
    <Choices label={t('builder.fuel')} options={FUELS} value={config.fuel} onchange={(fuel) => onapply({ fuel })} />
  {/if}
  {#if rotary || config.fuel !== 'diesel'}
    <div data-testid="fuel-system">
      <Choices label={t('builder.fuelSystem')} options={FUEL_SYSTEMS} value={config.fuelSystem} onchange={(fuelSystem) => onapply({ fuelSystem })} />
      <p class="help">{FUEL_SYSTEMS.find((item) => item.value === config.fuelSystem)?.help}</p>
      {#if spec && spec.carb.throats > 0 && config.fuelSystem !== 'injection'}
        <p class="help" data-testid="carb-spec">{t('builder.carb.spec', { throats: spec.carb.throats, mm: Math.round(spec.carb.throatMm), kpa: fixed(spec.carb.dropKpa, 1) })}</p>
      {/if}
      {#if config.fuelSystem !== 'injection' && config.aspiration !== 'natural'}
        <p class="help" data-testid="carb-blown">{t('builder.carb.blown')}</p>
      {/if}
    </div>
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
    <div>
      <Slider label={t('builder.turboPipe')} value={config.turboPipeMm} {...LIMITS.turboPipeMm} format={(value) => t('builder.turboPipe.value', { m: fixed(value / 1000, 2) })}
        oninput={(turboPipeMm) => onlive({ turboPipeMm })} onchange={(turboPipeMm) => onapply({ turboPipeMm })} />
      <p class="help" data-testid="turbo-pipe-help">{t('builder.turboPipe.help')}</p>
    </div>
    {#if config.fuel === 'petrol'}
      <div>
        <Choices label={t('builder.antiLag')} options={ANTI_LAGS} value={config.antiLag} onchange={(antiLag) => onapply({ antiLag })} />
        <p class="help" data-testid="antilag-help">{ANTI_LAGS.find((item) => item.value === config.antiLag)?.help}</p>
      </div>
    {/if}
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
  <div>
    <Choices label={t('builder.airFilter')} options={AIR_FILTERS} value={config.airFilter} onchange={(airFilter) => onapply({ airFilter })} />
    <p class="help" data-testid="air-filter-help">{AIR_FILTERS.find((item) => item.value === config.airFilter)?.help}</p>
    <p class="help" data-testid="air-filter-figures">{t('builder.airFilter.figures', { kpa: fixed(spec.intakeDropKpa, 1), c: Math.round(spec.intakeAirC) })}</p>
  </div>
  <div>
    <Choices label={t('builder.camshaft')} options={CAMSHAFTS} value={config.camshaft} onchange={(camshaft) => onapply({ camshaft })} />
    <p class="help" data-testid="camshaft-help">{CAMSHAFTS.find((item) => item.value === config.camshaft)?.help}</p>
  </div>
{/if}

<style>
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
  .summary { margin: 0; font-weight: 500; font-size: var(--type-body); font-variant-numeric: tabular-nums; }
</style>
