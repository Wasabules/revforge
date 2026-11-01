<script lang="ts">
  import Choices from './Choices.svelte';
  import GearsChart from './GearsChart.svelte';
  import Slider from './Slider.svelte';
  import { BOX_FAMILIES, BOX_FLYWHEELS, BOX_KINDS, BOX_MODELS, BOX_TEETH, LIMITS, gearRatios, type BoxFamily, type EngineConfig } from '../lib/config';
  import type { Spec } from '../lib/engine';
  import { fixed, t, whole } from '../lib/i18n.svelte';

  interface Props {
    config: EngineConfig;
    shown: EngineConfig;
    spec: Spec;
    rpm: number;
    speed: number;
    gear: number;
    running: boolean;
    onapply: (next: Partial<EngineConfig>) => void;
    onlive: (next: Partial<EngineConfig>) => void;
  }
  let { config, shown, spec, rpm, speed, gear, running, onapply, onlive }: Props = $props();

  const AUTO = 'auto';
  const CHOSEN = 'chosen';
  const CHOICE_FINAL = [
    { value: AUTO, get label() { return t('box.final.auto'); } },
    { value: CHOSEN, get label() { return t('box.final.chosen'); } },
  ];
  const CHOICE_LIMIT = [
    { value: AUTO, get label() { return t('box.limit.auto'); } },
    { value: CHOSEN, get label() { return t('box.limit.chosen'); } },
  ];
  const CHOICE_MODULE = [
    { value: AUTO, get label() { return t('box.module.auto'); } },
    { value: CHOSEN, get label() { return t('box.module.chosen'); } },
  ];
  const GEAR_COUNTS = [
    { value: 0, get label() { return t('box.gears.auto'); } },
    ...[4, 5, 6, 7, 8].map((value) => ({ value, get label() { return t('spec.gears.many', { gears: value }); } })),
  ];

  const box = $derived(spec.box);
  const kindHelp = $derived(BOX_KINDS.find((item) => item.value === config.boxKind)?.help ?? '');
  const familyHelp = $derived(BOX_FAMILIES.find((item) => item.value === config.boxFamily)?.help ?? '');
  const decimal = (value: number, digits = 2) => fixed(value, digits);

  /** Une boîte du catalogue reste choisie tant que ses réglages n'ont pas bougé. */
  const modelId = $derived(BOX_MODELS.find((model) => Object.entries(model.settings).every(([key, value]) => config[key as keyof EngineConfig] === value))?.id ?? '');
  const modelNote = $derived(BOX_MODELS.find((model) => model.id === modelId)?.note ?? t('box.model.tuned'));

  /** Passer en sur mesure part des rapports actuels : rien ne saute. */
  function chooseFamily(value: BoxFamily) {
    if (value === 'custom' && config.boxFamily !== 'custom') {
      const ratios = box.gears.map((g) => g.ratio.toFixed(3)).join(',');
      onapply({ boxFamily: value, boxRatios: ratios, boxGears: box.gears.length });
    } else {
      onapply({ boxFamily: value, boxRatios: value === 'custom' ? config.boxRatios : '' });
    }
  }

  function chooseGears(value: number) {
    // Sur mesure, le nombre de rapports et leurs valeurs vont ensemble.
    if (config.boxFamily === 'custom' && value > 0) {
      const ratios = gearRatios(config);
      const next = Array.from({ length: value }, (_, i) => ratios[i] ?? (ratios[ratios.length - 1] ?? 1) * 0.85 ** (i - ratios.length + 1));
      onapply({ boxGears: value, boxRatios: next.map((ratio) => ratio.toFixed(3)).join(',') });
    } else {
      onapply({ boxGears: value, ...(config.boxFamily === 'custom' ? { boxFamily: 'matched' as BoxFamily, boxRatios: '' } : {}) });
    }
  }

  function ratioChange(index: number, value: number): Partial<EngineConfig> {
    const ratios = gearRatios(shown);
    ratios[index] = value;
    return { boxRatios: ratios.map((ratio) => ratio.toFixed(3)).join(',') };
  }

  const tooWeak = $derived(box.limit > 0 && spec.peakTorque > box.limit);
</script>

<div class="box" data-testid="gearbox-panel">
  {#if config.electric}
    <p class="help">{t('box.electricHelp')}</p>
    <p class="summary" data-testid="box-summary">
      {#if box.gears[0]}{t('box.stages', { a: box.gears[0].driver, b: box.gears[0].driven, c: box.finalPinion, d: box.finalRing, overall: decimal(box.gears[0].overall) })}{/if}
    </p>
  {:else}
    <label class="pick">
      <span>{t('box.model')}</span>
      <select value={modelId} data-testid="box-model" onchange={(event) => {
        const chosen = BOX_MODELS.find((model) => model.id === event.currentTarget.value);
        if (chosen) onapply(chosen.settings);
        else onapply({ boxKind: 'synchro', boxGears: 0, boxFamily: 'matched', boxRatios: '', boxFinal: 0, boxTeeth: 'helical', boxHelix: 0, boxModule: 0, boxLimit: 0, boxFlywheel: 'single' });
      }}>
        <option value="">{t('box.model.built')}</option>
        {#each BOX_MODELS as model (model.id)}
          <option value={model.id}>{model.label}</option>
        {/each}
      </select>
      <small>{modelNote}</small>
    </label>

    <div class="columns">
      <div class="column">
        <div>
          <Choices label={t('box.type')} options={BOX_KINDS} value={config.boxKind} onchange={(boxKind) => onapply({ boxKind })} />
          <p class="help">{kindHelp}</p>
        </div>
        <Choices label={t('box.gears')} options={GEAR_COUNTS} value={config.boxGears} onchange={chooseGears} />
        <div>
          <Choices label={t('box.family')} options={BOX_FAMILIES} value={config.boxFamily} onchange={chooseFamily} />
          <p class="help">{familyHelp}</p>
        </div>
        {#if config.boxFamily === 'custom'}
          <div class="ratios" data-testid="box-ratios">
            {#each gearRatios(config) as ratio, i (i)}
              <Slider label={t('box.ratio', { gear: i + 1 })} value={ratio} {...LIMITS.boxRatio} format={(value) => decimal(value, 3)}
                oninput={(value) => onlive(ratioChange(i, value))} onchange={(value) => onapply(ratioChange(i, value))} />
            {/each}
          </div>
        {/if}
        <div>
          <Choices label={t('box.final')} options={CHOICE_FINAL} value={config.boxFinal > 0 ? CHOSEN : AUTO} onchange={(value) => onapply({ boxFinal: value === CHOSEN ? Math.round(box.finalDrive * 100) / 100 : 0 })} />
          {#if config.boxFinal > 0}
            <Slider label={t('box.final.ratio')} value={config.boxFinal} {...LIMITS.boxFinal} format={(value) => decimal(value)}
              oninput={(boxFinal) => onlive({ boxFinal })} onchange={(boxFinal) => onapply({ boxFinal })} />
          {/if}
        </div>
      </div>
      <div class="column">
        <Choices label={t('box.teeth')} options={BOX_TEETH} value={config.boxTeeth} onchange={(boxTeeth) => onapply({ boxTeeth })} />
        {#if config.boxTeeth === 'helical'}
          <Slider label={t('box.helixAngle')} value={config.boxHelix > 0 ? config.boxHelix : box.helix || 28} {...LIMITS.boxHelix} format={(value) => `${Math.round(value)}°`}
            oninput={(boxHelix) => onlive({ boxHelix })} onchange={(boxHelix) => onapply({ boxHelix })} />
        {/if}
        <div>
          <Choices label={t('box.module')} options={CHOICE_MODULE} value={config.boxModule > 0 ? CHOSEN : AUTO} onchange={(value) => onapply({ boxModule: value === CHOSEN ? Math.round(box.moduleMm * 20) / 20 : 0 })} />
          {#if config.boxModule > 0}
            <Slider label={t('box.module')} value={config.boxModule} {...LIMITS.boxModule} format={(value) => `${decimal(value)} mm`}
              oninput={(boxModule) => onlive({ boxModule })} onchange={(boxModule) => onapply({ boxModule })} />
            <p class="help">{t('box.module.help')}</p>
          {/if}
        </div>
        <div>
          <Choices label={t('box.limit')} options={CHOICE_LIMIT} value={config.boxLimit > 0 ? CHOSEN : AUTO} onchange={(value) => onapply({ boxLimit: value === CHOSEN ? Math.round(box.limit / 10) * 10 : 0 })} />
          {#if config.boxLimit > 0}
            <Slider label={t('box.limit')} value={config.boxLimit} {...LIMITS.boxLimit} format={(value) => `${value} Nm`}
              oninput={(boxLimit) => onlive({ boxLimit })} onchange={(boxLimit) => onapply({ boxLimit })} />
          {/if}
        </div>
        <Choices label={t('box.flywheel')} options={BOX_FLYWHEELS} value={config.boxFlywheel} onchange={(boxFlywheel) => onapply({ boxFlywheel })} />
      </div>
    </div>

    <p class="summary" data-testid="box-summary">
      {t('box.summary', { pinion: box.finalPinion, ring: box.finalRing, final: decimal(box.finalDrive), center: whole(box.center), module: decimal(box.moduleMm), teeth: box.helix > 0 ? t('box.helix', { angle: whole(box.helix) }) : t('box.straightTeeth'), limit: whole(box.limit), efficiency: whole(box.efficiency * 100) })}{box.inputDriver > 0 ? ` · ${t('box.constantMesh', { a: box.inputDriver, b: box.inputDriven })}` : ''}
    </p>
    {#if tooWeak}
      <p class="warning" role="status" data-testid="box-too-weak">{t('box.tooWeak', { torque: whole(spec.peakTorque) })}</p>
    {/if}

    <div class="wide">
      <table data-testid="box-table">
        <caption>{t('box.table.caption', { rpm: whole(spec.redline) })}</caption>
        <thead>
          <tr>
            <th scope="col">{t('common.gear')}</th>
            <th scope="col">{t('box.table.teeth')}</th>
            <th scope="col">{t('box.table.ratio')}</th>
            <th scope="col">{t('box.table.overall')}</th>
            <th scope="col">{t('common.speed')}</th>
            <th scope="col">{t('box.table.drop')}</th>
            <th scope="col">{t('box.table.whine')}</th>
            <th scope="col">{t('box.table.contact')}</th>
          </tr>
        </thead>
        <tbody>
          {#each box.gears as g, i (g.gear)}
            {@const next = box.gears[i + 1]}
            <tr>
              <th scope="row">{g.gear}</th>
              <td>{g.direct ? t('box.direct') : `${g.driver}/${g.driven}`}</td>
              <td>{decimal(g.ratio, 3)}</td>
              <td>{decimal(g.overall)}</td>
              <td data-testid="box-top-{g.gear}">{whole(g.topKmh)} km/h</td>
              <td>{next ? `${whole(spec.redline - (spec.redline * next.overall) / g.overall)} ${t('unit.rpm')}` : '—'}</td>
              <td>{g.direct ? '—' : `${whole(g.meshHz)} Hz`}</td>
              <td>{g.direct ? '—' : decimal(g.contact, 1)}</td>
            </tr>
          {/each}
          {#if box.reverseOverall > 0}
            <tr>
              <th scope="row">R</th>
              <td colspan="2">{t('box.reverseRow')}</td>
              <td>{decimal(box.reverseOverall)}</td>
              <td colspan="4">{whole((box.gears[0].topKmh * box.gears[0].overall) / box.reverseOverall)} km/h</td>
            </tr>
          {/if}
        </tbody>
      </table>
    </div>
    <div class="saw">
      <GearsChart {box} redline={spec.redline} {rpm} {speed} {gear} {running} />
    </div>
  {/if}
</div>

<style>
  .box { display: flex; flex-direction: column; gap: 16px; }
  .help { margin: 6px 0 0; color: var(--text-dim); font-size: 0.82rem; }
  .pick { display: flex; flex-direction: column; gap: 6px; }
  .pick span, .pick small { color: var(--text-dim); font-size: 0.85rem; }
  select {
    min-height: 44px;
    padding: 0 12px;
    border: 0;
    background: var(--surface-raised);
    color: var(--text);
    font-size: 1rem;
  }
  .columns, .column { display: flex; flex-direction: column; gap: 16px; min-width: 0; }
  .ratios { display: flex; flex-direction: column; gap: 10px; }
  .summary { margin: 0; color: var(--text); font-weight: 500; font-size: var(--type-body); font-variant-numeric: tabular-nums; }
  .warning { margin: 0; padding: 10px 12px; color: var(--text); background: color-mix(in srgb, var(--danger) 26%, var(--surface)); font-size: 0.9rem; }
  .wide { overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: 0.85rem; font-variant-numeric: tabular-nums; }
  caption { margin-bottom: 6px; color: var(--text-dim); text-align: left; font-size: 0.82rem; }
  th, td { padding: 4px 8px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; white-space: nowrap; }
  thead th { color: var(--text-dim); }
  tbody th { color: var(--text); font-weight: 600; }
</style>
