<script lang="ts">
  import { onMount } from 'svelte';
  import CarSettings from './components/CarSettings.svelte';
  import CarStage from './components/CarStage.svelte';
  import ChartsPanel from './components/ChartsPanel.svelte';
  import Choices from './components/Choices.svelte';
  import ConsumptionPanel from './components/ConsumptionPanel.svelte';
  import ClutchView from './components/ClutchView.svelte';
  import DrivingChoices from './components/DrivingChoices.svelte';
  import Dynamics from './components/Dynamics.svelte';
  import EngineSettings from './components/EngineSettings.svelte';
  import EngineView from './components/EngineView.svelte';
  import ExhaustSettings from './components/ExhaustSettings.svelte';
  import ExhaustView from './components/ExhaustView.svelte';
  import GaugeBank from './components/GaugeBank.svelte';
  import GearboxPanel from './components/GearboxPanel.svelte';
  import GearboxView from './components/GearboxView.svelte';
  import LevelMeter from './components/LevelMeter.svelte';
  import ListenerPanel, { type Place } from './components/ListenerPanel.svelte';
  import Panel from './components/Panel.svelte';
  import Pedal from './components/Pedal.svelte';
  import PowerCurves from './components/PowerCurves.svelte';
  import Slider from './components/Slider.svelte';
  import Spectrum from './components/Spectrum.svelte';
  import Tachometer from './components/Tachometer.svelte';
  import Trends from './components/Trends.svelte';
  import { AIR_FILTERS, BODIES, BOX_KINDS, CUSTOM_ID, DRIVES, EXHAUSTS, PRESETS, ROUTINGS, decodeConfig, encodeConfig, findPreset, strokeMm, type EngineConfig, type ShiftModeId, ANTI_LAGS } from './lib/config';
  import { heated, updateHeat, wallCelsius } from './lib/heat';
  import { TelemetryHistory } from './lib/history';
  import { Readings } from './lib/instruments';
  import { Motion, rememberAnimations, rememberSpeed, SPEEDS, systemReducesMotion } from './lib/motion';
  import { EMPTY_SPEC, Engine, IDLE_TELEMETRY, KW_TO_HP, Listener, Mode, type ModeValue, type Telemetry } from './lib/engine';
  import { fixed, lang, setLang, t, type MessageKey } from './lib/i18n.svelte';

  type DriveMode = 'neutral' | 'drive' | 'lap';
  const DRIVE_MODES: { value: DriveMode; label: string; help: string; mode: ModeValue }[] = [
    { value: 'neutral', get label() { return t('mode.neutral'); }, get help() { return t('mode.neutral.help'); }, mode: Mode.Neutral },
    { value: 'drive', get label() { return t('mode.drive'); }, get help() { return t('mode.drive.help'); }, mode: Mode.Drive },
    { value: 'lap', get label() { return t('mode.lap'); }, get help() { return t('mode.lap.help'); }, mode: Mode.Lap },
  ];
  type Gearbox = 'auto' | 'manual';
  const GEARBOXES: { value: Gearbox; label: string }[] = [
    { value: 'auto', get label() { return t('gearboxMode.auto'); } },
    { value: 'manual', get label() { return t('gearboxMode.manual'); } },
  ];
  const CLUTCH_MODES = [
    { value: 'assist', get label() { return t('clutchMode.assist'); } },
    { value: 'foot', get label() { return t('clutchMode.foot'); } },
  ];

  // Remplacée trente fois par seconde : un simple objet, sans proxy par champ.
  let telemetry = $state.raw<Telemetry>(IDLE_TELEMETRY);
  // Des fonctions plutôt que du texte : un message déjà affiché suit lui aussi la langue.
  let failure = $state<(() => string) | null>(null);
  let notice = $state<(() => string) | null>(null);
  // Toutes les vues partagent la même horloge : pistons, bouffées et turbine restent en phase.
  const motion = new Motion();
  let animated = $state(!motion.reduced);
  let speed = $state(motion.slow);

  function setSpeed(slow: number) {
    speed = slow;
    motion.slow = slow;
    rememberSpeed(slow);
  }

  function setAnimated(on: boolean) {
    animated = on;
    motion.reduced = !on;
    rememberAnimations(on);
  }
  const readings = new Readings(motion);
  // Les dernières minutes, pour les graphiques à la demande : mémoire fixe, rien n'est dessiné panneau fermé.
  const telemetryLog = new TelemetryHistory(motion);
  // Le rupteur coupe par à-coups trop brefs pour être lus : le voyant reste allumé un instant.
  let limiter = $state(false);
  let limiterUntil = 0;
  const engine = new Engine(
    (next) => {
      telemetry = next;
      const now = performance.now();
      telemetryLog.push(next, now);
      if (next.cut && !next.cranking) limiterUntil = now + 400;
      limiter = now < limiterUntil;
    },
    (message) => (failure = () => message),
  );

  const fromLink = decodeConfig(decodeURIComponent(location.hash.replace(/^#m=/, '')));
  let config = $state<EngineConfig>(fromLink ?? PRESETS[1].config);
  let loaded = $state(false);
  let running = $state(false);
  let starting = $state(false);
  let driveMode = $state<DriveMode>('neutral');
  let gearbox = $state<Gearbox>('auto');
  let shiftMode = $state<ShiftModeId>('normal');
  let listener = $state<Place>(Listener.Outside);
  let volume = $state(0.7);
  let pedal = $state(0);
  let brake = $state(0);
  let clutch = $state(0);
  // Assisté : le levier actionne aussi l'embrayage. Au pied : passer sans débrayer gratte.
  let footClutch = $state(false);
  let gamepad = $state<string | null>(null);
  // Pendant le glissé d'un curseur : l'aperçu et le son suivent, le lien attend le relâchement.
  let draft = $state<Partial<EngineConfig>>({});

  const shown = $derived({ ...config, ...draft });
  const preset = $derived(findPreset(config));
  const current = $derived(DRIVE_MODES.find((mode) => mode.value === driveMode)!);
  const spec = $derived(loaded ? engine.spec(shown) : EMPTY_SPEC);
  const manual = $derived(driveMode === 'drive' && gearbox === 'manual' && !config.electric);
  // Aux palettes, c'est aussi le conducteur qui passe les rapports, sans pédale d'embrayage.
  const paddles = $derived(driveMode === 'drive' && gearbox === 'auto' && shiftMode === 'paddles' && !config.electric);
  const liveHp = $derived(running ? Math.max(telemetry.power, 0) * KW_TO_HP : 0);
  const liveTorque = $derived(running ? telemetry.torque : 0);
  const weightRatio = $derived(spec.peakHp > 0 ? Math.round((spec.peakHp / shown.massKg) * 1000) : 0);

  $effect(() => {
    engine.setControls(pedal, brake, clutch);
  });

  $effect(() => {
    engine.setManual(gearbox === 'manual');
  });

  $effect(() => {
    engine.setClutchAssist(!footClutch);
  });

  onMount(() => {
    engine.load().then(() => (loaded = true)).catch((error) => (failure = () => t('error.load', { error })));
    if (import.meta.env.DEV || location.search.includes('test')) {
      (window as unknown as Record<string, unknown>).__revforge = { engine, telemetry: () => telemetry, spec: () => spec, motion, telemetryLog, readings };
    }
    const stopGamepad = pollGamepad();
    const stopHeat = followHeat();
    return () => {
      stopGamepad();
      stopHeat();
    };
  });

  function apply(next: Partial<EngineConfig>) {
    config = { ...config, ...next };
    draft = {};
    engine.setConfig(config);
    history.replaceState(null, '', `#m=${encodeConfig(config)}`);
  }

  /** Au glissé : le son change en direct, sans couper, tant que le moteur garde ses pièces. */
  function live(next: Partial<EngineConfig>) {
    draft = next;
    engine.setConfig({ ...config, ...next });
  }

  function choosePreset(id: string) {
    const chosen = PRESETS.find((item) => item.id === id);
    if (!chosen) return;
    config = chosen.config;
    draft = {};
    engine.setConfig(config);
    history.replaceState(null, '', `#m=${encodeConfig(config)}`);
  }

  async function toggleEngine() {
    if (running) {
      running = false;
      pedal = 0;
      brake = 0;
      clutch = 0;
      engine.stop();
      return;
    }
    starting = true;
    failure = null;
    try {
      await engine.start(config, current.mode, volume);
      running = true;
    } catch (error) {
      failure = () => t('error.start', { error: String(error) });
    } finally {
      starting = false;
    }
  }

  function chooseDriveMode(value: DriveMode) {
    driveMode = value;
    pedal = 0;
    brake = 0;
    clutch = 0;
    if (running) engine.setMode(current.mode);
  }

  const typing = (event: KeyboardEvent) => {
    const target = event.target as HTMLElement;
    return target.tagName === 'SELECT' || (target.tagName === 'INPUT' && event.code !== 'Space');
  };

  function onKey(event: KeyboardEvent, down: boolean) {
    // Échap ferme le volet, même si une touche de conduite ne fait rien à cet instant.
    if (down && event.key === 'Escape' && panel) {
      event.preventDefault();
      closePanel();
      return;
    }
    if (!running || typing(event)) return;
    if (down && !event.repeat && event.code === 'KeyD' && telemetry.stalled) engine.crank();
    if (driveMode === 'lap') {
      // Pas de pédale en balade : Espace ne fait pas défiler, mais active encore un bouton sélectionné.
      if (event.code === 'Space' && (event.target as HTMLElement).tagName !== 'BUTTON') event.preventDefault();
      return;
    }
    const drive = driveMode === 'drive';
    let action: (() => void) | null = null;
    if (event.code === 'Space' || event.code === 'ArrowUp') {
      // Maj : à mi-course, pour doser au clavier.
      action = () => (pedal = down ? (event.shiftKey ? 0.45 : 1) : 0);
    } else if (drive && (event.code === 'KeyB' || event.code === 'ArrowDown')) {
      action = () => (brake = down ? 1 : 0);
    } else if (manual && event.code === 'KeyC') {
      action = () => (clutch = down ? 1 : 0);
    } else if ((manual || paddles) && event.code === 'ArrowRight') {
      action = () => down && engine.shift(1);
    } else if ((manual || paddles) && event.code === 'ArrowLeft') {
      action = () => down && engine.shift(-1);
    }
    if (!action) return;
    // Une touche maintenue se répète : chaque répétition ferait défiler la page si on ne la retenait pas.
    event.preventDefault();
    if (!event.repeat) action();
  }

  function releaseAll() {
    pedal = 0;
    brake = 0;
    clutch = 0;
  }

  /** Manette : gâchettes analogiques pour l'accélérateur et le frein, boutons d'épaule pour la boîte. */
  function pollGamepad() {
    let frame = 0;
    let wasUp = false;
    let wasDown = false;
    let wasActive = false;
    const poll = () => {
      frame = requestAnimationFrame(poll);
      const pad = [...(navigator.getGamepads?.() ?? [])].find((item) => item);
      gamepad = pad ? pad.id : null;
      if (!pad || !running || driveMode === 'lap') return;
      const throttle = pad.buttons[7]?.value ?? 0;
      const stop = pad.buttons[6]?.value ?? 0;
      const clutchHeld = pad.buttons[0]?.pressed ? 1 : 0;
      const active = throttle > 0.02 || stop > 0.02 || clutchHeld > 0;
      if (active || wasActive) {
        pedal = throttle;
        brake = driveMode === 'drive' ? stop : 0;
        if (manual) clutch = clutchHeld;
      }
      wasActive = active;
      const up = pad.buttons[5]?.pressed ?? false;
      const down = pad.buttons[4]?.pressed ?? false;
      if ((manual || paddles) && up && !wasUp) engine.shift(1);
      if ((manual || paddles) && down && !wasDown) engine.shift(-1);
      wasUp = up;
      wasDown = down;
    };
    poll();
    return () => cancelAnimationFrame(frame);
  }

  /**
   * Le thème suit la température de la ligne, que telle ou telle vue soit à l'écran ou non : `Motion` ne compte qu'une fois
   * par image, et ne calcule pas le gaz pour autant. Une voiture électrique n'a pas de ligne : pas de température à montrer.
   */
  function followHeat() {
    let frame = 0;
    const tick = (now: number) => {
      frame = requestAnimationFrame(tick);
      motion.update(now, telemetry, shown, false);
      updateHeat(now, shown.electric ? null : wallCelsius(motion.heat), motion.reduced);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }

  async function copyLink() {
    history.replaceState(null, '', `#m=${encodeConfig(config)}`);
    // Le lien ne porte pas la langue : celui qui l'ouvre garde la sienne.
    const link = new URL(location.href);
    link.searchParams.delete('lang');
    try {
      await navigator.clipboard.writeText(link.href);
      notice = () => t('notice.copied');
    } catch {
      history.replaceState(null, '', link);
      notice = () => t('notice.copyRefused');
    }
    setTimeout(() => (notice = null), 2500);
  }

  function exportWav(scenario: 'lap' | 'blips') {
    const url = URL.createObjectURL(engine.renderWav(config, scenario, listener));
    const link = document.createElement('a');
    link.href = url;
    link.download = `revforge-${preset?.id ?? CUSTOM_ID}-${t(scenario === 'lap' ? 'file.lap' : 'file.blips')}.wav`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
  }

  const decimal = (value: number, digits = 1) => fixed(value, digits);

  // ── Les jauges lisent la simulation une fois par image, hors du cycle de rendu de la page. ──

  /** `gas` : seules les jauges qui en dépendent font avancer le modèle du gaz. Le reste ne le demande pas. */
  function sample(now: number, gas = false) {
    motion.update(now, telemetry, shown, gas);
    readings.update(now, telemetry, shown, running);
  }

  // ── Les vues : une à la fois, montée la première fois qu'on la demande, puis en pause cachée. ──

  type ViewId = 'engine' | 'clutch' | 'gearbox' | 'gas';
  const VIEW_KEY = 'revforge.view';
  const VIEWS: { id: ViewId; label: MessageKey }[] = [
    { id: 'engine', label: 'view.engine' },
    { id: 'clutch', label: 'view.clutch' },
    { id: 'gearbox', label: 'view.gearbox' },
    { id: 'gas', label: 'view.gas' },
  ];
  function storedView(): ViewId {
    try {
      const stored = localStorage.getItem(VIEW_KEY);
      if (VIEWS.some((item) => item.id === stored)) return stored as ViewId;
    } catch {
      // Rien de retenu.
    }
    return 'engine';
  }
  let view = $state<ViewId>(storedView());
  let mounted = $state<Record<ViewId, boolean>>({ engine: false, clutch: false, gearbox: false, gas: false, [storedView()]: true });

  function chooseView(next: ViewId) {
    mounted[next] = true;
    view = next;
    try {
      localStorage.setItem(VIEW_KEY, next);
    } catch {
      // Le choix vaudra pour cette visite.
    }
  }
  /** Une vue cachée ne reçoit plus la télémétrie : son texte ne se recalcule pas pour rien. */
  const feed = (id: ViewId) => (view === id ? telemetry : IDLE_TELEMETRY);

  // ── Les volets de réglage ──

  type PanelId = 'engine' | 'exhaust' | 'gearbox' | 'car' | 'listener' | 'share' | 'display';
  const PANEL_IDS: PanelId[] = ['engine', 'exhaust', 'gearbox', 'car', 'listener', 'share', 'display'];
  const PANEL_LABEL: Record<PanelId, MessageKey> = { engine: 'panel.engine', exhaust: 'panel.exhaust', gearbox: 'panel.gearbox', car: 'panel.car', listener: 'panel.listener', share: 'panel.share', display: 'panel.display' };
  const openers: Partial<Record<PanelId, HTMLButtonElement>> = {};
  let panel = $state<PanelId | null>(null);

  function openerAction(node: HTMLButtonElement, id: PanelId) {
    openers[id] = node;
    return { destroy() { delete openers[id]; } };
  }

  function togglePanel(id: PanelId) {
    if (panel === id) closePanel();
    else panel = id;
  }

  /** Le focus revient au bouton qui a ouvert le volet. */
  function closePanel() {
    const from = panel;
    panel = null;
    if (from) openers[from]?.focus();
  }

  const POSITION_LABEL: Record<number, MessageKey> = {
    [Listener.Outside]: 'listener.pos.behind',
    [Listener.Front]: 'listener.pos.front',
    [Listener.Side]: 'listener.pos.side',
    [Listener.Far]: 'listener.pos.far',
    [Listener.Pipe]: 'listener.pos.pipe',
    [Listener.Cabin]: 'listener.pos.cabin',
    [Listener.EngineBay]: 'listener.pos.bay',
  };

  /** Ce qui est réglé, écrit sur chaque bouton : rien n'est caché derrière un volet fermé. */
  // Un turbo à essence et un système choisi : sinon, le réglage n'agit pas, et rien ne doit le laisser croire.
  const antiLagFitted = $derived(!shown.electric && shown.aspiration === 'turbo' && shown.fuel === 'petrol' && shown.antiLag !== 'off');
  const summaries = $derived.by((): Record<PanelId, string> => {
    const c = shown;
    const hp = Math.round(spec.peakHp);
    const label = <T extends string>(list: { value: T; label: string }[], value: T) => list.find((item) => item.value === value)?.label ?? '';
    return {
      engine: c.electric ? t('summary.electric', { hp }) : t('summary.engine', { litres: decimal(c.displacementCc / 1000), cylinders: c.cylinders, hp, bore: c.boreMm, stroke: Math.round(strokeMm(c)) }) + (c.airFilter === 'stock' ? '' : ` · ${label(AIR_FILTERS, c.airFilter)}`) + (c.aspiration === 'turbo' && c.turboPipeMm !== 400 ? t('summary.turboPipe', { m: decimal(c.turboPipeMm / 1000) }) : '') + (antiLagFitted ? ` · ${label(ANTI_LAGS, c.antiLag)}` : ''),
      exhaust: c.electric ? t('summary.noExhaust') : t('summary.exhaust', { line: label(EXHAUSTS, c.exhaust), outlets: label(ROUTINGS, c.routing) }),
      gearbox: spec.gears === 1 ? t('spec.gears.one') : t('summary.gearbox', { gears: t('spec.gears.many', { gears: spec.gears }), kind: label(BOX_KINDS, c.boxKind) }),
      car: t('summary.car', { body: label(BODIES, c.body), mass: c.massKg, drive: label(DRIVES, c.drive) }),
      listener: typeof listener === 'number' ? t(POSITION_LABEL[listener]) : t('listener.free'),
      share: preset?.name ?? t('builder.custom'),
      display: animated ? t('summary.display', { lang: t('display.languageName'), speed: t(`speed.${speed}` as MessageKey) }) : t('summary.displayOff', { lang: t('display.languageName') }),
    };
  });

  // Les dimensions de la barre et du poste de conduite collé en bas du téléphone : le volet se cale dessus.
  let barHeight = $state(0);
  let dockHeight = $state(0);
</script>

<svelte:window onkeydown={(event) => onKey(event, true)} onkeyup={(event) => onKey(event, false)} onblur={releaseAll} />

<div class="page" style:--panel-top="{barHeight + 8}px" style:--dock-h="{dockHeight}px">
  <div class="bar" data-sticky-bar bind:clientHeight={barHeight} use:heated>
    <div class="heat-bar" aria-hidden="true"></div>
    <div class="bar-row">
      <header class="top">
        <h1>Rev<span>forge</span></h1>
        <p>{t('header.tagline')}</p>
      </header>
      <nav class="config" aria-label={t('panel.nav')}>
        {#each PANEL_IDS as id (id)}
          <button type="button" use:openerAction={id} class="plate" class:open={panel === id} aria-haspopup="dialog" aria-expanded={panel === id} aria-controls="panel-{id}" aria-labelledby="config-{id}" aria-describedby="config-{id}-summary" onclick={() => togglePanel(id)} data-testid="open-{id}">
            <span class="name" id="config-{id}">{t(PANEL_LABEL[id])}</span>
            <span class="value" id="config-{id}-summary" data-testid="open-{id}-summary">{summaries[id]}</span>
          </button>
        {/each}
      </nav>
    </div>
  </div>

  {#if !animated && systemReducesMotion()}
    <p class="motion-note" data-testid="animations-note">
      {t('animations.system')}
      <button type="button" onclick={() => setAnimated(true)}>{t('animations.enable')}</button>
    </p>
  {/if}

  {#if failure}
    <p class="failure" role="alert">{failure()}</p>
  {/if}

  <main class:with-panel={panel !== null}>
    <section class="card cockpit" aria-label={t('section.cockpit')}>
      <div class="dial">
        <Tachometer rpm={() => motion.rpm} speed={() => motion.speed} gear={() => telemetry.gear} redline={spec.redline} idle={spec.idle} {running} showRoad={driveMode !== 'neutral'} sample={(now) => sample(now)} />
      </div>

      <div class="live" aria-live="off">
        <div><span>{t('common.power')}</span><strong data-testid="live-hp">{Math.round(liveHp)} {t('unit.hp')}</strong></div>
        <div><span>{t('common.torque')}</span><strong data-testid="live-torque">{Math.round(liveTorque)} Nm</strong></div>
        <div><span>{t('common.level')}</span><strong data-testid="live-dba">{running && telemetry.levelDbA > 5 ? `${Math.round(telemetry.levelDbA)} dB(A)` : '—'}</strong></div>
        {#if shown.aspiration === 'turbo' && !shown.electric}
          <div><span>{t('live.turbo')}</span><strong data-testid="live-boost">{decimal(telemetry.boost * shown.boostBar)} bar</strong></div>
        {/if}
      </div>

      <!-- Une place réservée : le voyant qui s'allume ne déplace pas la pédale sous le doigt. -->
      <div class="flags">
        {#if running && telemetry.stalled}
          <p class="flag danger"><span>{t('common.state')}</span><strong data-testid="stalled">{t('state.stalled')}</strong></p>
        {:else if running && telemetry.boxBroken}
          <p class="flag danger"><span>{t('common.state')}</span><strong data-testid="box-broken">{t('state.boxBroken')}</strong></p>
        {:else if running && telemetry.grinding > 5}
          <p class="flag"><span>{t('common.state')}</span><strong data-testid="box-grinding">{t('state.grinding')}</strong></p>
        {:else if running && telemetry.overload > 1}
          <p class="flag"><span>{t('common.state')}</span><strong data-testid="box-overload">{t('state.overload')}</strong></p>
        {:else if running && telemetry.lugging}
          <p class="flag"><span>{t('common.state')}</span><strong>{t('state.lugging')}</strong></p>
        {:else if limiter && running && !telemetry.cranking}
          <p class="flag danger"><span>{t('common.state')}</span><strong>{t('state.limiter')}</strong></p>
        {:else if telemetry.cranking}
          <p class="flag info"><span>{t('common.state')}</span><strong>{t('state.cranking')}</strong></p>
        {:else if Math.abs(telemetry.slip) > 4}
          <p class="flag"><span>{t('common.state')}</span><strong>{t('state.slip')}</strong></p>
        {:else if running && telemetry.antiLag}
          <p class="flag info"><span>{t('common.state')}</span><strong data-testid="live-antilag">{t('state.antiLag')}</strong></p>
        {/if}
      </div>

      <div class="dock" class:manual bind:clientHeight={dockHeight}>
        <div class="dock-actions" use:heated>
          <button type="button" class="start plate" class:on={running} disabled={!loaded || starting} onclick={toggleEngine} data-testid="start">
            {#if !loaded}{t('start.loading')}{:else if starting}{t('start.starting')}{:else if running}{t('start.stop')}{:else}{t('start.start')}{/if}
          </button>
          {#if running && telemetry.stalled}
            <button type="button" class="restart plate" onclick={() => engine.crank()} data-testid="restart">{t('start.restart')}</button>
          {/if}
          {#if running && telemetry.boxBroken}
            <button type="button" class="restart plate" onclick={() => engine.repairGearbox()} data-testid="box-repair">{t('start.repair')}</button>
          {/if}
        </div>

        {#if manual || paddles}
          <div class="shifter">
            <button type="button" disabled={!running} onclick={() => engine.shift(-1)} data-testid="shift-down" aria-label={t('shift.down.aria')}>{t('shift.down')}</button>
            <output data-testid="manual-gear">{telemetry.gear === 0 ? 'N' : telemetry.gear < 0 ? 'R' : telemetry.gear}</output>
            <button type="button" disabled={!running} onclick={() => engine.shift(1)} data-testid="shift-up" aria-label={t('shift.up.aria')}>{t('shift.up')}</button>
          </div>
        {/if}

        {#if driveMode !== 'lap'}
          <div class="pedals">
            {#if manual}
              <Pedal label={t('common.clutch')} hint={t('pedal.clutch.hint')} pressure={clutch} disabled={!running} onchange={(value) => (clutch = value)} />
            {/if}
            {#if driveMode === 'drive'}
              <Pedal label={t('pedal.brake')} hint={t('pedal.brake.hint')} tone="danger" pressure={brake} disabled={!running} onchange={(value) => (brake = value)} />
            {/if}
            <Pedal label={t('pedal.throttle')} hint={t('pedal.throttle.hint')} pressure={pedal} disabled={!running} variable onchange={(value) => (pedal = value)} />
          </div>
        {/if}
      </div>

      <div class="modes">
        <Choices label={t('mode.label')} options={DRIVE_MODES} value={driveMode} onchange={chooseDriveMode} />
        <p class="help">{current.help}</p>
        {#if driveMode === 'drive' && !config.electric}
          <Choices label={t('common.gearbox')} options={GEARBOXES} value={gearbox} onchange={(value) => (gearbox = value)} />
        {/if}
        <DrivingChoices {engine} {driveMode} {gearbox} electric={config.electric} {running} {telemetry} bind:shiftMode />
        {#if manual}
          <p class="help">{t('shift.help')}</p>
          <Choices label={t('common.clutch')} options={CLUTCH_MODES} value={footClutch ? 'foot' : 'assist'} onchange={(value) => (footClutch = value === 'foot')} />
        {/if}
        <Slider label={t('volume')} value={volume} min={0} max={1} step={0.01} format={(value) => t('unit.percent', { value: Math.round(value * 100) })}
          oninput={(value) => engine.setVolume(value)}
          onchange={(value) => { volume = value; engine.setVolume(value); }} />
        {#if gamepad}
          <p class="help" data-testid="gamepad">{t('gamepad')}</p>
        {/if}
      </div>
    </section>

    <div class="center">
      <section class="card stage" aria-label={t('section.car')}>
        <CarStage config={shown} {telemetry} {motion} {brake} />
        <dl class="figures" data-testid="spec">
          {#if spec.peakHp > 0}
            <div><dt>{t('common.power')}</dt><dd data-testid="spec-hp">{Math.round(spec.peakHp)} {t('unit.hp')}</dd></div>
            <div><dt>{t('common.torque')}</dt><dd data-testid="spec-torque">{Math.round(spec.peakTorque)} Nm</dd></div>
            <div><dt>{t('spec.weightRatio')}</dt><dd>{weightRatio} {t('unit.hpPerTon')}</dd></div>
            <div><dt>{t('spec.zeroTo100')}</dt><dd data-testid="spec-100">{spec.zeroTo100 > 0 ? `${decimal(spec.zeroTo100)} s` : '—'}</dd></div>
            <div><dt>{t('spec.topSpeed')}</dt><dd data-testid="spec-top">{Math.round(spec.topSpeed)} km/h</dd></div>
            <div><dt>{t('common.gearbox')}</dt><dd>{spec.gears === 1 ? t('spec.gears.one') : t('spec.gears.many', { gears: spec.gears })}</dd></div>
          {/if}
        </dl>
      </section>

      <section class="card views">
        <div class="tabs" role="group" aria-label={t('view.group')} use:heated>
          {#each VIEWS as item (item.id)}
            <button type="button" aria-pressed={view === item.id} onclick={() => chooseView(item.id)} data-testid="view-{item.id}">{t(item.label)}</button>
          {/each}
        </div>
        {#if mounted.engine}
          <section class="pane" hidden={view !== 'engine'} aria-label={t('section.engine')}>
            <EngineView config={shown} telemetry={feed('engine')} {motion} slow={speed} />
          </section>
        {/if}
        {#if mounted.clutch}
          <section class="pane" hidden={view !== 'clutch'} aria-label={t('section.clutch')}>
            <ClutchView config={shown} telemetry={feed('clutch')} {motion} peakTorque={spec.peakTorque} ratios={spec.ratios} />
          </section>
        {/if}
        {#if mounted.gearbox}
          <section class="pane" hidden={view !== 'gearbox'} aria-label={t('section.gearbox.aria')}>
            <GearboxView config={shown} telemetry={feed('gearbox')} {motion} slow={speed} box={spec.box} starterRing={spec.starter.ring} />
          </section>
        {/if}
        {#if mounted.gas}
          <section class="pane" hidden={view !== 'gas'} aria-label={t('section.gas.aria')}>
            <ExhaustView config={shown} telemetry={feed('gas')} {motion} slow={speed} />
          </section>
        {/if}
      </section>
      <!-- Dans la largeur, sous la vue : une lecture de plus, sur peu de hauteur. -->
      <section class="card conso" aria-label={t('conso.title')}><ConsumptionPanel config={shown} telemetry={() => telemetry} running={() => running} onreset={() => engine.resetTrip()} /></section>
    </div>

    <aside class="rail" aria-label={t('section.instruments')}>
      <section class="card">
        <GaugeBank config={shown} {spec} {motion} {readings} telemetry={() => telemetry} running={() => running} {sample} />
      </section>
      <section class="card" aria-label={t('section.curves.aria')}>
        <PowerCurves {spec} rpm={telemetry.rpm} {liveHp} liveTorque={liveTorque} {running} />
      </section>
      <section class="card">
        <LevelMeter telemetry={() => telemetry} running={() => running} sample={(now) => sample(now)} />
      </section>
    </aside>
  </main>

  <div class="analysis">
    <section class="card" aria-label={t('section.dynamics')}>
      <h2>{t('section.dynamics')}</h2>
      <Dynamics config={shown} {motion} {readings} telemetry={() => telemetry} running={() => running} sample={(now) => sample(now)} />
    </section>
    <section class="card" aria-label={t('section.trends')}>
      <h2>{t('section.trends')}</h2>
      <Trends history={telemetryLog} redline={spec.redline} peakTorque={spec.peakTorque} topSpeed={spec.topSpeed} />
    </section>
    <section class="card" aria-label={t('section.spectrum.aria')}>
      <h2>{t('section.spectrum')}</h2>
      <Spectrum analyser={engine.analyser} {running} />
    </section>
  </div>

  <ChartsPanel history={telemetryLog} {engine} config={shown} {spec} rpm={telemetry.rpm} {running} />

  <footer>
    <p aria-hidden="true">{t('header.tagline')}</p>
    <p>{t('footer.note')}</p>
  </footer>

  <!-- Les volets restent montés, cachés : leur état survit, et le lien, la position d'écoute et la fiche continuent de vivre. -->
  <Panel id="engine" title={t('panel.engine')} open={panel === 'engine'} onclose={closePanel}>
    <EngineSettings {config} {shown} {spec} onapply={apply} onlive={live} />
  </Panel>
  <Panel id="exhaust" title={t('panel.exhaust')} open={panel === 'exhaust'} onclose={closePanel}>
    <ExhaustSettings {config} onapply={apply} />
  </Panel>
  <Panel id="gearbox" title={t('panel.gearbox')} open={panel === 'gearbox'} onclose={closePanel}>
    <GearboxPanel {config} {shown} {spec} rpm={panel === 'gearbox' ? telemetry.rpm : 0} speed={panel === 'gearbox' ? telemetry.speed : 0} gear={panel === 'gearbox' ? telemetry.gear : 0} {running} onapply={apply} onlive={live} />
  </Panel>
  <Panel id="car" title={t('panel.car')} open={panel === 'car'} onclose={closePanel}>
    <CarSettings {config} {spec} onapply={apply} onlive={live} />
  </Panel>
  <Panel id="listener" title={t('section.listener')} open={panel === 'listener'} onclose={closePanel}>
    <ListenerPanel config={shown} settled={config} telemetry={panel === 'listener' ? telemetry : IDLE_TELEMETRY} {motion} {engine} {loaded} {running} bind:place={listener} />
  </Panel>
  <Panel id="share" title={t('panel.share')} open={panel === 'share'} onclose={closePanel}>
    <label class="preset">
      <span>{t('builder.preset')}</span>
      <select value={preset?.id ?? CUSTOM_ID} onchange={(event) => choosePreset(event.currentTarget.value)} data-testid="preset">
        {#each PRESETS as item (item.id)}
          <option value={item.id}>{item.name}</option>
        {/each}
        {#if !preset}
          <option value={CUSTOM_ID}>{t('builder.custom')}</option>
        {/if}
      </select>
      <small>{preset?.description ?? t('builder.customHelp')}</small>
    </label>
    <div class="actions">
      <button type="button" onclick={copyLink} data-testid="share">{t('builder.copyLink')}</button>
      <button type="button" disabled={!loaded} onclick={() => exportWav('blips')} data-testid="export-blips">{t('builder.wavBlips')}</button>
      <button type="button" disabled={!loaded} onclick={() => exportWav('lap')} data-testid="export-lap">{t('builder.wavLap')}</button>
    </div>
    {#if notice}
      <p class="help" role="status">{notice()}</p>
    {/if}
  </Panel>
  <Panel id="display" title={t('panel.display')} open={panel === 'display'} onclose={closePanel}>
    <fieldset class="group">
      <legend>{t('display.language')}</legend>
      <div class="lang" role="group" aria-label={t('lang.group')}>
        <button type="button" lang="en" aria-label="English" aria-pressed={lang() === 'en'} onclick={() => setLang('en')} data-testid="lang-en">EN</button>
        <button type="button" lang="fr" aria-label="Français" aria-pressed={lang() === 'fr'} onclick={() => setLang('fr')} data-testid="lang-fr">FR</button>
      </div>
    </fieldset>
    <fieldset class="group">
      <legend>{t('display.animations')}</legend>
      <button type="button" class="animations" aria-pressed={animated} onclick={() => setAnimated(!animated)} data-testid="animations">{t(animated ? 'animations.on' : 'animations.off')}</button>
    </fieldset>
    <div>
      <Choices label={t('speed.label')} options={SPEEDS.map((value) => ({ value, label: t(`speed.${value}`) }))} value={speed} onchange={setSpeed} />
      {#if speed === 1}<p class="help" data-testid="speed-help">{t('speed.help')}</p>{/if}
    </div>
  </Panel>
</div>

<style>
  /* Le volet a la largeur du rail, qu'il recouvre sur un écran moyen : il laisse la vue et le poste de conduite. */
  .page { --panel-w: clamp(320px, 25vw, 460px); padding: 0 12px 24px; }
  /* Les boutons de réglage restent à portée : le volet s'accroche sous cette barre. */
  .bar { position: sticky; top: 0; z-index: 10; margin: 0 -12px 8px; background: var(--bg); }
  .heat-bar { height: 4px; background: var(--heat); }
  .bar-row { display: flex; align-items: stretch; gap: 16px; padding: 10px 12px 8px; }
  .top { flex: none; display: flex; flex-direction: column; justify-content: center; }
  h1 { margin: 0; font-family: var(--display); font-size: clamp(2rem, 3vw, 2.75rem); font-weight: 800; line-height: 1; letter-spacing: 0.01em; text-transform: uppercase; }
  h1 span { color: var(--heat-text); }
  /* L'accroche se lit au pied de la page : la barre garde sa hauteur pour les plaques. */
  .top p { position: absolute; width: 1px; height: 1px; margin: -1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
  .config { flex: 1; min-width: 0; display: grid; grid-template-columns: repeat(7, minmax(0, 1fr)); gap: 8px; }
  .config button { display: flex; flex-direction: column; justify-content: space-between; gap: 4px; min-width: 0; min-height: clamp(72px, 9.5vh, 92px); padding: 10px 14px; color: var(--text); text-align: left; cursor: pointer; --cut: 16px; }
  .config button:hover { --plate: var(--surface-high); }
  .config button.open, .config button.open:hover { --plate: var(--heat); color: var(--on-heat); }
  .name { font-family: var(--display); font-size: clamp(1.25rem, 1.65vw, 1.875rem); font-weight: 800; line-height: 1; text-transform: uppercase; }
  .value { color: var(--text-dim); font-size: clamp(0.75rem, 0.95vw, 0.9375rem); line-height: 1.2; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
  .open .value { color: inherit; font-weight: 600; }

  .motion-note { margin: 0 0 8px; padding: 8px 12px; background: var(--surface); color: var(--text-dim); font-size: var(--type-body); }
  .motion-note button { margin-left: 8px; min-height: 30px; padding: 0 10px; border: 0; background: var(--control); color: var(--on-control); font-size: var(--type-body); font-weight: 600; cursor: pointer; }
  .failure { margin: 0 0 8px; padding: 10px 14px; color: var(--text); background: color-mix(in srgb, var(--danger) 26%, var(--surface)); }

  main {
    --cockpit-w: clamp(290px, 21vw, 460px);
    --rail-w: clamp(310px, 25vw, 640px);
    display: grid;
    grid-template-columns: var(--cockpit-w) minmax(0, 1fr) var(--rail-w);
    gap: 8px;
    align-items: start;
  }
  .card { display: flex; flex-direction: column; gap: 8px; min-width: 0; padding: 12px 14px; background: var(--surface); }
  h2 { margin: 0; color: var(--text-dim); font-family: var(--display); font-size: 1.125rem; font-weight: 700; line-height: 1.1; text-transform: uppercase; letter-spacing: 0.01em; }

  /* Le poste de conduite reste sous la main : ce qu'il montre ne demande pas le gaz, il ne coûte rien de plus. */
  .cockpit { position: sticky; top: calc(var(--panel-top, 0px)); gap: 8px; max-height: calc(100vh - var(--panel-top, 0px) - 8px); overflow-y: auto; scrollbar-width: thin; scrollbar-color: var(--line-strong) transparent; --pedal-h: 92px; }
  .center { display: flex; flex-direction: column; gap: 8px; min-width: 0; }
  .rail { display: flex; flex-direction: column; gap: 8px; min-width: 0; }
  .stage { gap: 4px; padding: 6px 14px 10px; }
  .views { gap: 8px; }
  .pane { min-width: 0; }
  .pane[hidden] { display: none; }
  .tabs { display: flex; flex-wrap: wrap; gap: 4px 20px; }
  .tabs button { min-height: 36px; padding: 0 2px; border: 0; background: transparent; color: var(--text-quiet); font-family: var(--display); font-size: 1.375rem; font-weight: 700; text-transform: uppercase; line-height: 1; cursor: pointer; }
  .tabs button:hover { color: var(--text); }
  .tabs button[aria-pressed='true'] { color: var(--text); box-shadow: inset 0 -3px 0 var(--heat-text); }

  .figures { display: flex; flex-wrap: wrap; justify-content: space-between; gap: 2px 16px; margin: 0; min-height: 44px; }
  dt { color: var(--text-dim); font-size: var(--type-label); }
  dd { margin: 0; font-family: var(--display); font-size: 1.5rem; font-weight: 700; line-height: 1.1; white-space: nowrap; }

  .live { display: grid; grid-auto-flow: column; grid-auto-columns: minmax(0, 1fr); gap: 2px 6px; }
  .live div { display: flex; flex-direction: column; align-items: center; min-width: 0; text-align: center; }
  .live span { color: var(--text-dim); font-size: var(--type-small); line-height: 1.2; }
  /* Ces chiffres changent sans cesse : Big Shoulders n'a pas de chiffres de largeur fixe, Inter Tight si. */
  .live strong { font-size: clamp(0.9375rem, 1.15vw, 1.25rem); font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  .flags { min-height: 24px; }
  .flag { display: flex; justify-content: center; align-items: baseline; gap: 6px; margin: 0; padding: 1px 8px; background: color-mix(in srgb, var(--warn) 22%, var(--surface)); }
  .flag span { color: var(--text-dim); font-size: var(--type-small); }
  .flag strong { font-size: var(--type-body); font-weight: 600; }
  .flag.danger { background: color-mix(in srgb, var(--danger) 30%, var(--surface)); }
  .flag.info { background: var(--surface-raised); }

  .dock { display: flex; flex-direction: column; gap: 8px; }
  .dock-actions { display: flex; flex-wrap: wrap; gap: 8px; }
  .dock-actions > * { flex: 1 1 auto; }
  .start { min-height: 48px; padding: 0 16px; color: var(--on-control); font-family: var(--display); font-size: 1.5rem; font-weight: 800; line-height: 1; text-transform: uppercase; cursor: pointer; --plate: var(--control); --cut: 10px; }
  .start.on { color: var(--text); --plate: var(--surface-high); }
  .start:disabled { opacity: 0.6; cursor: wait; }
  .restart { min-height: 48px; padding: 0 12px; color: var(--on-heat); font-family: var(--display); font-size: 1.25rem; font-weight: 800; line-height: 1; text-transform: uppercase; cursor: pointer; --plate: var(--heat); --cut: 10px; }
  .pedals { display: flex; gap: 8px; }
  .shifter { display: grid; grid-template-columns: 1fr auto 1fr; align-items: center; gap: 8px; }
  .shifter button { min-height: 40px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-body); cursor: pointer; }
  .shifter button:hover:not(:disabled) { background: var(--surface-high); }
  .shifter button:disabled { opacity: 0.45; cursor: not-allowed; }
  .shifter output { min-width: 2ch; font-family: var(--display); font-size: 2rem; font-weight: 800; line-height: 1; text-align: center; }
  .modes { display: flex; flex-direction: column; gap: 10px; padding-top: 4px; }
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }

  .analysis { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; margin-top: 8px; margin-bottom: 8px; align-items: stretch; }
  footer { display: flex; flex-direction: column; gap: 2px; margin-top: 12px; color: var(--text-dim); font-size: var(--type-small); }
  footer p { margin: 0; }

  .preset { display: flex; flex-direction: column; gap: 6px; }
  .preset span, .preset small { color: var(--text-dim); font-size: var(--type-small); }
  select { min-height: 44px; padding: 0 12px; border: 0; background: var(--surface-raised); color: var(--text); font-size: 1rem; }
  .actions { display: flex; flex-wrap: wrap; gap: 8px; padding-top: 4px; }
  .actions button { min-height: 44px; padding: 0 14px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-body); cursor: pointer; }
  .actions button:hover:not(:disabled) { background: var(--surface-high); }
  .actions button:disabled { opacity: 0.45; cursor: not-allowed; }
  .group { margin: 0; padding: 0; border: 0; min-width: 0; }
  legend { padding: 0; margin-bottom: 8px; color: var(--text-dim); font-size: 0.85rem; }
  .lang { display: inline-flex; gap: 2px; }
  .lang button { min-width: 48px; min-height: 44px; padding: 0 8px; border: 0; background: var(--surface-raised); color: var(--text-dim); font-size: var(--type-body); cursor: pointer; }
  .lang button:hover:not([aria-pressed='true']) { background: var(--surface-high); }
  .lang button[aria-pressed='true'] { background: var(--control); color: var(--on-control); font-weight: 600; }
  .animations { min-height: 44px; padding: 0 14px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-body); cursor: pointer; }
  .animations:hover { background: var(--surface-high); }

  /* Assez large pour un volet en plus : la page se resserre, rien de ce qu'elle montre n'est couvert. */
  @media (min-width: 1800px) {
    main.with-panel { padding-right: calc(var(--panel-w) + 8px); --rail-w: clamp(310px, 20vw, 460px); }
  }
  @media (min-width: 1181px) {
    main {
      --car-h: clamp(170px, 25vh, 360px);
      --view-h: clamp(340px, calc(100vh - 25vh - 370px), 900px);
      --view-aspect: auto;
      --view-max: none;
    }
  }
  /* Un grand écran : la vue laisse assez de place pour que le bas du tableau tienne aussi sans défiler. */
  @media (min-width: 1181px) and (min-height: 1200px) {
    main {
      /* Les plaques prennent de la hauteur en haut : la voiture en rend autant, pour que l'analyse reste au même endroit. */
      --car-h: clamp(170px, calc(25vh - 44px), 360px);
      --view-h: clamp(420px, calc(100vh - 25vh - 740px), 900px);
    }
  }
  @media (max-width: 1180px) {
    main { --cockpit-w: 300px; grid-template-columns: var(--cockpit-w) minmax(0, 1fr); }
    .rail { grid-column: 1 / -1; display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); align-items: start; }
    .analysis { grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
    .config { grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .config button { min-height: 60px; padding: 8px 12px; --cut: 12px; }
    /* Sept plaques sur quatre colonnes : la dernière ferme la rangée. */
    .config button:last-child { grid-column: span 2; }
  }
  @media (max-width: 860px) {
    /* Sur un téléphone, ce qu'on regarde d'abord : le compte-tours, puis la voiture, puis les réglages. */
    .page { display: flex; flex-direction: column; gap: 8px; padding-bottom: calc(var(--dock-h, 0px) + 16px); }
    .bar, .bar-row, main, .center, .cockpit { display: contents; }
    .heat-bar { order: -1; position: sticky; top: 0; z-index: 10; margin: 0 -12px; }
    .top { order: 0; margin: 6px 0 0; }
    h1 { font-size: 2.25rem; }
    .motion-note, .failure { order: 0; }
    .dial { order: 1; --tach-max: 300px; }
    .live { order: 1; }
    .flags { order: 1; }
    .stage { order: 2; }
    .config { order: 3; flex: none; grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .config button { min-height: 68px; }
    .name { font-size: 1.5rem; }
    .value { font-size: 0.8125rem; }
    .modes { order: 4; padding: 0; }
    .rail { order: 5; }
    .conso { order: 5; }
    .views { order: 6; }
    .analysis { order: 7; margin: 0; }
    :global(.panel-charts) { order: 8; }
    footer { order: 9; margin: 0; }
    /* Le démarrage et les pédales se collent au bas de l'écran : toujours sous le pouce. */
    .dock { position: fixed; left: 0; right: 0; bottom: 0; z-index: 30; flex-direction: row; flex-wrap: wrap; padding: 8px 8px calc(8px + env(safe-area-inset-bottom)); background: var(--surface); box-shadow: 0 -12px 28px rgb(0 0 0 / 0.55); --pedal-h: 72px; }
    .dock-actions { flex: 0 0 96px; flex-direction: column; }
    .dock-actions > * { flex: 1 1 auto; font-size: 1.125rem; }
    .pedals { flex: 1 1 200px; }
    .dock.manual .pedals { flex-basis: 100%; }
    .shifter { flex: 1 1 200px; }
    .shifter button { min-height: 44px; padding: 0 4px; font-size: var(--type-small); }
    .tabs button { flex: 1 1 auto; min-height: 44px; }
  }
</style>
