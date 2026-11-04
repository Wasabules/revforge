<script lang="ts">
  import { onMount, untrack } from 'svelte';
  import { animate, fit, readTheme, type Theme } from '../lib/canvas';
  import type { EngineConfig } from '../lib/config';
  import { KW_TO_HP, type Engine, type Spec } from '../lib/engine';
  import { HISTORY_HZ, type ChannelKey, type TelemetryHistory } from '../lib/history';
  import { fixed, lang, t, whole } from '../lib/i18n.svelte';
  import ChartsLine from './ChartsLine.svelte';
  import ChartsSpectrogram from './ChartsSpectrogram.svelte';

  interface Props {
    history: TelemetryHistory;
    engine: Engine;
    config: EngineConfig;
    spec: Spec;
    rpm: number;
    running: boolean;
  }
  let { history, engine, config, spec, rpm, running }: Props = $props();

  interface Series {
    channel: ChannelKey;
    label: string;
    /** Passe de la valeur enregistrée à la valeur affichée. */
    factor?: () => number;
    abs?: boolean;
  }
  interface Chart {
    key: string;
    title: string;
    unit: string;
    digits: number;
    series: Series[];
    /** Échelle stable : l'axe ne saute pas à chaque coup de gaz. Elle s'élargit si la mesure en sort. */
    range: () => [number, number];
    steps?: boolean;
    when?: () => boolean;
  }

  const BOOST_SERIES: Series = { channel: 'boost', get label() { return t('charts.boost.series'); }, factor: () => config.boostBar };
  const BOOST_TARGET_SERIES: Series = { channel: 'boostTarget', get label() { return t('charts.boost.target'); } };

  const CHARTS: Chart[] = [
    { key: 'rpm', get title() { return t('charts.rpm'); }, get unit() { return t('unit.rpm'); }, digits: 0, series: [{ channel: 'rpm', get label() { return t('common.engine'); } }, { channel: 'inputRpm', get label() { return t('charts.rpm.input'); } }], range: () => [0, spec.redline || 7000] },
    { key: 'speed', get title() { return t('common.speed'); }, unit: 'km/h', digits: 0, series: [{ channel: 'speed', get label() { return t('common.speed'); } }], range: () => [0, Math.max(spec.topSpeed, 60)] },
    { key: 'gear', get title() { return t('charts.gear'); }, unit: '', digits: 0, series: [{ channel: 'gear', get label() { return t('common.gear'); } }], range: () => [0, Math.max(spec.gears, 1)], steps: true },
    { key: 'torque', get title() { return t('common.torque'); }, unit: 'Nm', digits: 0, series: [{ channel: 'torque', get label() { return t('charts.torque.crank'); } }, { channel: 'clutchTorque', get label() { return t('charts.torque.clutch'); } }], range: () => [-Math.max(spec.peakTorque, 50) * 0.3, Math.max(spec.peakTorque, 50) * 1.1] },
    { key: 'power', get title() { return t('common.power'); }, get unit() { return t('unit.hp'); }, digits: 0, series: [{ channel: 'power', get label() { return t('common.power'); }, factor: () => KW_TO_HP }], range: () => [-Math.max(spec.peakHp, 50) * 0.2, Math.max(spec.peakHp, 50) * 1.05] },
    { key: 'load', get title() { return t('charts.load'); }, unit: '%', digits: 0, series: [{ channel: 'load', get label() { return t('charts.load'); }, factor: () => 100 }], range: () => [0, 100] },
    { key: 'boost', get title() { return t(config.aspiration === 'supercharger' ? 'charts.boost.charger' : 'charts.boost'); }, unit: 'bar', digits: 1,
      // Sur un turbo, la pression que vise sa régulation, à côté de la vraie : la dérive du ressort et les dépassements s'y voient.
      get series() { return config.aspiration === 'turbo' ? [BOOST_SERIES, BOOST_TARGET_SERIES] : [BOOST_SERIES]; }, range: () => [0, config.boostBar * 1.1], when: () => config.aspiration !== 'natural' && !config.electric },
    { key: 'clutchSlip', get title() { return t('charts.clutchSlip'); }, get unit() { return t('unit.rpm'); }, digits: 0, series: [{ channel: 'clutchSlipRpm', get label() { return t('charts.clutchSlip.series'); } }], range: () => [0, spec.redline || 7000], when: () => !config.electric },
    { key: 'clutchPower', get title() { return t('charts.clutchPower'); }, unit: 'kW', digits: 1, series: [{ channel: 'clutchSlipKw', get label() { return t('common.dissipated'); } }], range: () => [0, 30], when: () => !config.electric },
    { key: 'clutchTemp', get title() { return t('charts.clutchTemp'); }, unit: '°C', digits: 0, series: [{ channel: 'clutchTemp', get label() { return t('common.linings'); } }], range: () => [0, 400], when: () => !config.electric },
    { key: 'slip', get title() { return t('charts.tyreSlip'); }, unit: 'm/s', digits: 1, series: [{ channel: 'tyreSlip', get label() { return t('charts.tyreSlip.series'); } }], range: () => [-2, 8] },
    { key: 'level', get title() { return t('charts.level'); }, unit: 'dB(A)', digits: 0, series: [{ channel: 'levelDbA', get label() { return t('common.level'); } }], range: () => [40, 110], when: () => history.hasLevel },
  ];

  const WINDOWS = [15, 30, 60];
  let open = $state(false);
  let selected = $state<string[]>(['rpm', 'speed', 'torque']);
  let showSpectrogram = $state(false);
  let showLine = $state(false);
  let windowSeconds = $state(30);
  let frozen = $state(false);
  let frozenAt = 0;
  let hoverX = $state<number | null>(null);
  let tick = $state(0);
  let tableRows = $state<string[][]>([]);
  let showTable = $state(false);

  /** Le niveau en dB(A) apparaît dès que la simulation le donne : la liste doit le savoir. */
  let levelKnown = $state(false);
  const available = $derived.by(() => {
    void levelKnown;
    return CHARTS.filter((chart) => !chart.when || chart.when());
  });
  const visible = $derived(available.filter((chart) => selected.includes(chart.key)));

  function toggle(key: string) {
    selected = selected.includes(key) ? selected.filter((item) => item !== key) : [...selected, key];
  }

  const canvases: Record<string, HTMLCanvasElement> = {};

  function register(node: HTMLCanvasElement, key: string) {
    canvases[key] = node;
    return { destroy() { delete canvases[key]; } };
  }

  function seriesStyle(theme: Theme, index: number): string {
    // Validées contre chaque fond par `validate_palette.js` : pas la couleur d'accent, trop claire pour le fond sombre.
    const dark = parseInt(theme.surface.slice(1, 3), 16) < 128;
    if (index === 0) return dark ? '#e36f2b' : '#c95a12';
    return dark ? '#3d8fda' : '#2a78d6';
  }

  const LEFT = 46;
  const RIGHT = 10;
  const TOP = 6;
  const BOTTOM = 18;

  const niceStep = (span: number) => {
    const raw = span / 3;
    const power = 10 ** Math.floor(Math.log10(raw || 1));
    const scaled = raw / power;
    return (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10) * power;
  };

  const number = (value: number, digits = 0) => (digits === 0 ? whole(value) : fixed(value, digits));
  /** Le pourcentage s'écrit selon la langue : « 11% » ou « 11 % ». */
  const withUnit = (text: string, unit: string) => (unit === '%' ? t('unit.percent', { value: text }) : `${text} ${unit}`.trim());

  function drawChart(chart: Chart, now: number, theme: Theme, showAxis: boolean) {
    const canvas = canvases[chart.key];
    if (!canvas) return;
    const { ctx, width, height } = fit(canvas);
    const plotW = width - LEFT - RIGHT;
    const plotH = height - TOP - (showAxis ? BOTTOM : 4);
    let [low, high] = chart.range();
    const samples = Math.min(Math.round(windowSeconds * HISTORY_HZ), history.length);
    const offset = skipped();
    // L'échelle s'élargit si la mesure en sort : jamais de courbe coupée.
    for (const series of chart.series) {
      const data = history.data[series.channel];
      const factor = series.factor ? series.factor() : 1;
      for (let k = 0; k < samples; k++) {
        const value = data[history.indexFromEnd(k + offset)] * factor;
        if (value > high) high = value;
        if (value < low) low = value;
      }
    }
    const step = niceStep(high - low);
    const X = (seconds: number) => LEFT + plotW * (1 - seconds / windowSeconds);
    const Y = (value: number) => TOP + (1 - (value - low) / (high - low)) * plotH;

    ctx.font = '10.5px "Inter Tight", system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    ctx.fillStyle = theme.dim;
    for (let v = Math.ceil(low / step) * step; v <= high + 1e-6; v += step) {
      const y = Math.round(Y(v)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(LEFT, y);
      ctx.lineTo(width - RIGHT, y);
      ctx.stroke();
      ctx.fillText(number(Math.abs(v) < step * 1e-3 ? 0 : v, chart.digits && step < 1 ? chart.digits : 0), LEFT - 6, y);
    }
    if (showAxis) {
      ctx.textAlign = 'center';
      ctx.textBaseline = 'alphabetic';
      for (let i = 0; i <= 3; i++) {
        const seconds = (windowSeconds * i) / 3;
        ctx.fillText(i === 0 ? t('common.now') : `−${Math.round(seconds)} s`, Math.min(X(seconds), width - RIGHT - 24), height - 4);
      }
    }

    const reference = frozen ? frozenAt : now;
    const frozenOffset = skipped();
    chart.series.forEach((series, index) => {
      const data = history.data[series.channel];
      const factor = series.factor ? series.factor() : 1;
      ctx.strokeStyle = seriesStyle(theme, index);
      ctx.lineWidth = 2;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.beginPath();
      let started = false;
      let lastY = 0;
      for (let k = samples - 1; k >= 0; k--) {
        const at = history.indexFromEnd(k + frozenOffset);
        const x = X((reference - history.times[at]) / 1000);
        const y = Y(data[at] * factor);
        if (x < LEFT) continue;
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          if (chart.steps) ctx.lineTo(x, lastY);
          ctx.lineTo(x, y);
        }
        lastY = y;
      }
      ctx.stroke();
      // Point d'arrivée, cerné du fond.
      if (samples > 0 && hoverX === null) {
        const at = history.indexFromEnd(frozenOffset);
        const x = X((reference - history.times[at]) / 1000);
        ctx.fillStyle = seriesStyle(theme, index);
        ctx.strokeStyle = theme.surface;
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(Math.min(x, width - RIGHT - 3), Y(data[at] * factor), 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
      }
    });

    if (hoverX !== null) {
      const x = LEFT + hoverX * plotW;
      ctx.strokeStyle = theme.dim;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, TOP);
      ctx.lineTo(x, TOP + plotH);
      ctx.stroke();
      const at = sampleAt(hoverX, reference);
      if (at >= 0) {
        chart.series.forEach((series, index) => {
          const factor = series.factor ? series.factor() : 1;
          ctx.fillStyle = seriesStyle(theme, index);
          ctx.strokeStyle = theme.surface;
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(x, Y(history.data[series.channel][at] * factor), 4, 0, Math.PI * 2);
          ctx.fill();
          ctx.stroke();
        });
      }
    }
    history.draws++;
  }

  /** Échantillons à sauter en partant de la fin, quand le graphique est figé. */
  let frozenWritten = 0;
  const skipped = () => (frozen ? Math.min(history.written - frozenWritten, history.capacity - 1) : 0);

  function sampleAt(share: number, reference: number): number {
    const seconds = windowSeconds * (1 - share);
    const target = reference - seconds * 1000;
    const samples = Math.min(Math.round(windowSeconds * HISTORY_HZ) + 2, history.length);
    const offset = skipped();
    let best = -1;
    let gap = Infinity;
    for (let k = 0; k < samples; k++) {
      const at = history.indexFromEnd(k + offset);
      const delta = Math.abs(history.times[at] - target);
      if (delta < gap) {
        gap = delta;
        best = at;
      }
    }
    return gap < 300 ? best : -1;
  }

  function onHover(event: PointerEvent) {
    const canvas = event.currentTarget as HTMLCanvasElement;
    const box = canvas.getBoundingClientRect();
    hoverX = Math.max(0, Math.min(1, (event.clientX - box.left - LEFT) / (box.width - LEFT - RIGHT)));
  }

  /** Au clavier aussi : les flèches déplacent le curseur. */
  function onKey(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      hoverX = null;
      return;
    }
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
    event.preventDefault();
    const move = event.key === 'ArrowLeft' ? -0.02 : 0.02;
    hoverX = Math.max(0, Math.min(1, (hoverX ?? 1) + move));
  }

  function toggleFreeze() {
    frozen = !frozen;
    if (frozen) {
      frozenAt = performance.now();
      frozenWritten = history.written;
    }
    refreshTable();
  }

  function refreshTable() {
    if (!showTable) return;
    const rows: string[][] = [];
    const count = Math.min(Math.round(windowSeconds * HISTORY_HZ), history.length);
    const stride = Math.max(1, Math.round(count / 30));
    const offset = skipped();
    for (let k = 0; k < count; k += stride) {
      const at = history.indexFromEnd(k + offset);
      const row = [`−${number((performance.now() - history.times[at]) / 1000, 1)} s`];
      for (const chart of visible) {
        for (const series of chart.series) {
          const factor = series.factor ? series.factor() : 1;
          row.push(number(history.data[series.channel][at] * factor, chart.digits));
        }
      }
      rows.push(row);
    }
    tableRows = rows;
  }

  // Le tableau garde du texte déjà mis en forme : il se refait quand la langue change.
  $effect(() => {
    lang();
    untrack(refreshTable);
  });

  let root: HTMLElement;
  let frame = 0;

  onMount(() => animate(root, (_seconds, now) => {
    if (!open) return;
    // Quinze fois par seconde : la télémétrie n'en dit pas plus, et l'œil non plus.
    frame++;
    if (frame % 4 !== 0 && hoverX === null) return;
    tick = (tick + 1) % 1000;
    levelKnown = history.hasLevel;
    const theme = readTheme(root);
    const list = visible;
    for (let i = 0; i < list.length; i++) drawChart(list[i], now, theme, i === list.length - 1);
  }));

  const tooltip = $derived.by(() => {
    void tick;
    if (hoverX === null) return null;
    const reference = frozen ? frozenAt : performance.now();
    const at = sampleAt(hoverX, reference);
    if (at < 0) return null;
    const rows: { value: string; label: string; index: number }[] = [];
    for (const chart of visible) {
      chart.series.forEach((series, index) => {
        const factor = series.factor ? series.factor() : 1;
        rows.push({ value: withUnit(number(history.data[series.channel][at] * factor, chart.digits), chart.unit), label: series.label, index });
      });
    }
    return { time: (reference - history.times[at]) / 1000, rows };
  });

  const current = (chart: Chart, series: Series) => {
    void tick;
    if (history.length === 0) return '—';
    const factor = series.factor ? series.factor() : 1;
    return withUnit(number(history.data[series.channel][history.indexFromEnd(skipped())] * factor, chart.digits), chart.unit);
  };
</script>

<section class="panel panel-charts" aria-label={t('charts.title')} bind:this={root}>
  <button type="button" class="head" aria-expanded={open} onclick={() => (open = !open)} data-testid="charts-toggle">
    <span>{t('charts.title')}</span>
    <small>{open ? t('charts.close') : t('charts.summary')}</small>
  </button>

  {#if open}
    <div class="body" data-testid="charts-body">
      <div class="filters">
        {#each available as chart (chart.key)}
          <label class:on={selected.includes(chart.key)}>
            <input type="checkbox" checked={selected.includes(chart.key)} onchange={() => toggle(chart.key)} />
            {chart.title}
          </label>
        {/each}
        <label class:on={showSpectrogram}>
          <input type="checkbox" bind:checked={showSpectrogram} data-testid="charts-spectrogram" /> {t('charts.spectrogram')}
        </label>
        {#if !config.electric}
          <label class:on={showLine}>
            <input type="checkbox" bind:checked={showLine} data-testid="charts-line" /> {t('charts.lineResponse')}
          </label>
        {/if}
      </div>

      <div class="controls">
        <span class="range" role="group" aria-label={t('charts.span')}>
          {#each WINDOWS as seconds (seconds)}
            <button type="button" class:on={windowSeconds === seconds} aria-pressed={windowSeconds === seconds} onclick={() => (windowSeconds = seconds)}>{seconds} s</button>
          {/each}
        </span>
        <button type="button" class="freeze" class:on={frozen} aria-pressed={frozen} onclick={toggleFreeze} data-testid="charts-freeze">{frozen ? t('charts.resume') : t('charts.freeze')}</button>
      </div>

      {#if visible.length > 0}
        <div class="stack" role="group" aria-label={t('charts.curves')} onpointerleave={() => { hoverX = null; }}>
          {#each visible as chart, index (chart.key)}
            <div class="chart" data-testid="chart-{chart.key}">
              <header>
                <h3>{chart.title}</h3>
                {#if chart.series.length > 1}
                  <ul class="legend">
                    {#each chart.series as series, seriesIndex (series.channel)}
                      <li><i class="key s{seriesIndex}"></i>{series.label} <strong>{current(chart, series)}</strong></li>
                    {/each}
                  </ul>
                {:else}
                  <p><strong>{current(chart, chart.series[0])}</strong></p>
                {/if}
              </header>
              <!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_interactive_element_to_noninteractive_role -->
              <canvas use:register={chart.key} class:axis={index === visible.length - 1} tabindex="0" role="img" aria-label={t('charts.canvas.aria', { title: chart.title, seconds: windowSeconds })}
                onpointermove={onHover} onkeydown={onKey} onblur={() => { hoverX = null; }}></canvas>
            </div>
          {/each}
          {#if tooltip}
            <div class="tooltip" style="left: clamp(8px, calc({LEFT}px + {hoverX} * (100% - {LEFT + RIGHT}px) + 12px), calc(100% - 190px))" data-testid="charts-tooltip">
              <p class="time">{t('common.ago', { seconds: number(tooltip.time, 1) })}</p>
              {#each tooltip.rows as row, i (i)}
                <p><i class="key s{row.index}"></i><strong>{row.value}</strong> {row.label}</p>
              {/each}
            </div>
          {/if}
        </div>

        <details ontoggle={(event) => { showTable = (event.currentTarget as HTMLDetailsElement).open; refreshTable(); }}>
          <summary>{t('common.showValues')}</summary>
          <div class="scroll">
            <table data-testid="charts-table">
              <thead>
                <tr>
                  <th scope="col">{t('charts.time')}</th>
                  {#each visible as chart (chart.key)}
                    {#each chart.series as series (series.channel)}
                      <th scope="col">{series.label}{chart.unit ? ` (${chart.unit})` : ''}</th>
                    {/each}
                  {/each}
                </tr>
              </thead>
              <tbody>
                {#each tableRows as row, i (i)}
                  <tr>{#each row as cell, c (c)}<td>{cell}</td>{/each}</tr>
                {/each}
              </tbody>
            </table>
          </div>
        </details>
      {/if}

      {#if showSpectrogram}
        <ChartsSpectrogram {engine} {running} {frozen} {history} />
      {/if}
      {#if showLine && !config.electric}
        <ChartsLine {engine} {config} {rpm} {running} {history} />
      {/if}
    </div>
  {/if}
</section>

<style>
  .panel { min-width: 0; background: var(--surface); }
  .head {
    display: flex; flex-wrap: wrap; align-items: baseline; gap: 4px 12px; width: 100%; padding: 12px 16px;
    border: 0; background: transparent; color: var(--text); text-align: left; cursor: pointer;
  }
  .head span { font-family: var(--display); font-size: 1.125rem; font-weight: 700; line-height: 1.1; text-transform: uppercase; letter-spacing: 0.01em; }
  .head small { color: var(--text-dim); font-size: 0.82rem; }
  .body { display: flex; flex-direction: column; gap: 10px; padding: 0 16px 14px; min-width: 0; }
  .filters { display: flex; flex-wrap: wrap; gap: 6px; }
  .filters label {
    display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; min-height: 32px;
    background: var(--surface-raised); font-size: 0.82rem; color: var(--text-dim); cursor: pointer;
  }
  .filters label.on { color: var(--text); background: var(--surface-high); }
  .filters input { accent-color: var(--control); margin: 0; }
  .controls { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 8px; }
  .range { display: inline-flex; gap: 4px; }
  .controls button {
    min-height: 32px; padding: 0 10px; border: 0;
    background: var(--surface-raised); color: var(--text-dim); font-size: 0.82rem; cursor: pointer;
  }
  .controls button.on { color: var(--on-control); background: var(--control); font-weight: 600; }
  .stack { position: relative; display: flex; flex-direction: column; gap: 8px; min-width: 0; }
  .chart header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: baseline; gap: 2px 10px; }
  .chart h3 { margin: 0; color: var(--text-dim); font-size: 0.82rem; font-weight: 400; }
  .chart p { margin: 0; font-size: 0.85rem; }
  .chart strong, .tooltip strong { font-weight: 600; font-variant-numeric: tabular-nums; }
  .legend { display: flex; flex-wrap: wrap; gap: 2px 12px; margin: 0; padding: 0; list-style: none; color: var(--text-dim); font-size: 0.8rem; }
  .legend strong { color: var(--text); }
  .key { display: inline-block; width: 14px; height: 3px; margin-right: 6px; vertical-align: middle; }
  .key.s0 { background: light-dark(#c95a12, #e36f2b); }
  .key.s1 { background: light-dark(#2a78d6, #3d8fda); }
  canvas { display: block; width: 100%; height: 72px; touch-action: pan-y; }
  canvas.axis { height: 88px; }
  .tooltip {
    position: absolute; top: 24px; z-index: 2; width: 180px; padding: 8px 10px; pointer-events: none;
    background: var(--surface-high); font-size: 0.8rem; color: var(--text-dim);
  }
  .tooltip p { margin: 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .tooltip strong { color: var(--text); }
  .tooltip .time { margin-bottom: 4px; }
  details { color: var(--text-dim); font-size: 0.82rem; }
  summary { cursor: pointer; }
  .scroll { overflow-x: auto; }
  table { width: 100%; margin-top: 6px; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  th, td { padding: 2px 8px; text-align: right; border-bottom: 1px solid var(--line); font-weight: 400; white-space: nowrap; }
</style>
