<script lang="ts">
  import { onMount } from 'svelte';
  import Choices from './components/Choices.svelte';
  import Pedal from './components/Pedal.svelte';
  import Slider from './components/Slider.svelte';
  import Spectrum from './components/Spectrum.svelte';
  import Tachometer from './components/Tachometer.svelte';
  import {
    ASPIRATIONS, CUSTOM_ID, CYLINDERS, EXHAUSTS, FUELS, LAYOUTS, LIMITS, POPS, PRESETS, RELEASES,
    cylinderCc, decodeConfig, encodeConfig, findPreset, strokeMm, type EngineConfig,
  } from './lib/config';
  import { Engine, IDLE_TELEMETRY, Mode, type ModeValue, type Telemetry } from './lib/engine';

  type DriveMode = 'neutral' | 'drive' | 'lap';
  const DRIVE_MODES: { value: DriveMode; label: string; help: string; mode: ModeValue }[] = [
    { value: 'neutral', label: 'Coups de gaz', help: "Au point mort : maintiens l'accélérateur jusqu'au rupteur, puis relâche pour entendre le lever de pied.", mode: Mode.Neutral },
    { value: 'drive', label: 'Conduite', help: "Accélère et freine, la boîte passe les rapports toute seule.", mode: Mode.Drive },
    { value: 'lap', label: 'Balade', help: 'Une boucle de deux minutes en pilote automatique : ville, feu, voie rapide, dépassement.', mode: Mode.Lap },
  ];

  let telemetry = $state<Telemetry>(IDLE_TELEMETRY);
  let failure = $state<string | null>(null);
  let notice = $state<string | null>(null);
  const engine = new Engine(
    (next) => (telemetry = next),
    (message) => (failure = message),
  );

  const fromLink = decodeConfig(decodeURIComponent(location.hash.replace(/^#m=/, '')));
  let config = $state<EngineConfig>(fromLink ?? PRESETS[1].config);
  let loaded = $state(false);
  let running = $state(false);
  let starting = $state(false);
  let driveMode = $state<DriveMode>('neutral');
  let volume = $state(0.7);
  let pedal = $state(0);
  let brake = $state(0);
  // Pendant le glissé d'un curseur : l'aperçu suit, sans toucher au son.
  let draft = $state<Partial<EngineConfig>>({});

  const preset = $derived(findPreset(config));
  const shown = $derived({ ...config, ...draft });
  const current = $derived(DRIVE_MODES.find((mode) => mode.value === driveMode)!);
  const preview = $derived(loaded ? engine.preview(shown, [2000, 4000]) : { idle: 0, multiplier: 1, rpms: [0, 0] });
  const redline = $derived(config.electric ? 12000 : config.redlineRpm);
  const popCharacter = $derived(EXHAUSTS.find((item) => item.value === config.exhaust)?.pops ?? '');

  $effect(() => {
    engine.setControls(pedal, brake);
  });

  onMount(() => {
    engine.load().then(() => (loaded = true)).catch((error) => (failure = `Impossible de charger le moteur : ${error}`));
    if (import.meta.env.DEV || location.search.includes('test')) {
      (window as unknown as Record<string, unknown>).__revforge = { engine, telemetry: () => telemetry };
    }
  });

  function apply(next: Partial<EngineConfig>) {
    config = { ...config, ...next };
    draft = {};
    engine.setConfig(config);
    history.replaceState(null, '', `#m=${encodeConfig(config)}`);
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
      engine.stop();
      telemetry = IDLE_TELEMETRY;
      return;
    }
    starting = true;
    failure = null;
    try {
      await engine.start(config, current.mode, volume);
      running = true;
    } catch (error) {
      failure = `Démarrage impossible : ${error}`;
    } finally {
      starting = false;
    }
  }

  function chooseDriveMode(value: DriveMode) {
    driveMode = value;
    pedal = 0;
    brake = 0;
    if (running) engine.setMode(current.mode);
  }

  const typing = (event: KeyboardEvent) => {
    const target = event.target as HTMLElement;
    return target.tagName === 'SELECT' || target.tagName === 'INPUT';
  };

  function onKey(event: KeyboardEvent, down: boolean) {
    if (!running || driveMode === 'lap' || typing(event) || event.repeat) return;
    if (event.code === 'Space') {
      event.preventDefault();
      pedal = down ? 1 : 0;
    } else if (driveMode === 'drive' && (event.code === 'KeyB' || event.code === 'ArrowDown')) {
      event.preventDefault();
      brake = down ? 1 : 0;
    }
  }

  function releaseAll() {
    pedal = 0;
    brake = 0;
  }

  async function copyLink() {
    history.replaceState(null, '', `#m=${encodeConfig(config)}`);
    try {
      await navigator.clipboard.writeText(location.href);
      notice = 'Lien copié.';
    } catch {
      notice = "Copie refusée par le navigateur : le lien est dans la barre d'adresse.";
    }
    setTimeout(() => (notice = null), 2500);
  }

  function exportWav(scenario: 'lap' | 'blips') {
    const url = URL.createObjectURL(engine.renderWav(config, scenario));
    const link = document.createElement('a');
    link.href = url;
    link.download = `revforge-${preset?.id ?? CUSTOM_ID}-${scenario === 'lap' ? 'balade' : 'coups-de-gaz'}.wav`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
  }

  const rpmText = (value: number) => `${Math.round(value / 10) * 10} tr/min`;
</script>

<svelte:window onkeydown={(event) => onKey(event, true)} onkeyup={(event) => onKey(event, false)} onblur={releaseAll} />

<div class="page">
  <header>
    <h1>Revforge</h1>
    <p>Construis un moteur, puis écoute-le : une simulation acoustique en temps réel, dans ton navigateur.</p>
  </header>

  {#if failure}
    <p class="failure" role="alert">{failure}</p>
  {/if}

  <main>
    <section class="card cockpit" aria-label="Poste de conduite">
      <Tachometer rpm={telemetry.rpm} {redline} gear={telemetry.gear} speed={telemetry.speed} {running} showRoad={driveMode !== 'neutral'} />

      <button type="button" class="start" class:on={running} disabled={!loaded || starting} onclick={toggleEngine} data-testid="start">
        {#if !loaded}Chargement…{:else if starting}Démarrage…{:else if running}Couper le contact{:else}Démarrer le moteur{/if}
      </button>

      {#if driveMode !== 'lap'}
        <div class="pedals">
          {#if driveMode === 'drive'}
            <Pedal label="Frein" hint="Touche B" tone="danger" pressure={brake} disabled={!running} onchange={(value) => (brake = value)} />
          {/if}
          <Pedal label="Accélérateur" hint="Barre d'espace, ou maintenir ici" pressure={pedal} disabled={!running} variable onchange={(value) => (pedal = value)} />
        </div>
      {/if}

      <div class="modes">
        <Choices label="Mode" options={DRIVE_MODES} value={driveMode} onchange={chooseDriveMode} />
        <p class="help">{current.help}</p>
      </div>
    </section>

    <div class="settings">
    <section class="card sound" aria-label="Son">
      <Slider label="Volume" value={volume} min={0} max={1} step={0.01} format={(value) => `${Math.round(value * 100)} %`}
        oninput={(value) => engine.setVolume(value)}
        onchange={(value) => { volume = value; engine.setVolume(value); }} />
      <div>
        <h2>Spectre</h2>
        <Spectrum analyser={engine.analyser} {running} />
      </div>
    </section>

    <section class="card builder" aria-label="Configuration du moteur">
      <label class="preset">
        <span>Moteur</span>
        <select value={preset?.id ?? CUSTOM_ID} onchange={(event) => choosePreset(event.currentTarget.value)} data-testid="preset">
          {#each PRESETS as item (item.id)}
            <option value={item.id}>{item.name}</option>
          {/each}
          {#if !preset}
            <option value={CUSTOM_ID}>Mon moteur</option>
          {/if}
        </select>
        <small>{preset?.description ?? 'Ta configuration. Le lien de partage la garde en entier.'}</small>
      </label>

      {#if config.electric}
        <p class="help">Un moteur électrique n'a ni cylindres ni échappement. Choisis un autre moteur pour retrouver les réglages.</p>
      {:else}
        <p class="summary" data-testid="summary">
          {shown.cylinders} cylindres · {(shown.displacementCc / 1000).toFixed(1).replace('.', ',')} L · {Math.round(cylinderCc(shown))} cm³ par cylindre · course {Math.round(strokeMm(shown))} mm
        </p>

        <Choices label="Cylindres" options={CYLINDERS.map((value) => ({ value, label: String(value) }))} value={config.cylinders} onchange={(cylinders) => apply({ cylinders })} />
        <Choices label="Architecture" options={LAYOUTS} value={config.layout} onchange={(layout) => apply({ layout })} />
        <Choices label="Échappement" options={EXHAUSTS} value={config.exhaust} onchange={(exhaust) => apply({ exhaust })} />
        <Choices label="Alimentation" options={ASPIRATIONS} value={config.aspiration} onchange={(aspiration) => apply({ aspiration })} />
        {#if config.aspiration === 'turbo'}
          <Choices label="Décharge du turbo" options={RELEASES} value={config.turboRelease} onchange={(turboRelease) => apply({ turboRelease })} />
        {/if}
        <Choices label="Carburant" options={FUELS} value={config.fuel} onchange={(fuel) => apply({ fuel })} />
        {#if config.fuel === 'diesel'}
          <p class="help">Pas de pop & bang sur un diesel : il ne reste pas de carburant imbrûlé à enflammer.</p>
        {:else}
          <div>
            <Choices label="Pop & bang au lever de pied" options={POPS} value={config.pops} onchange={(pops) => apply({ pops })} />
            {#if config.pops !== 'off'}
              <p class="help">{popCharacter}</p>
            {/if}
          </div>
        {/if}

        <Slider label="Cylindrée" value={config.displacementCc} {...LIMITS.displacementCc} format={(value) => `${value} cm³`}
          oninput={(displacementCc) => (draft = { displacementCc })} onchange={(displacementCc) => apply({ displacementCc })} />
        <Slider label="Alésage (taille des cylindres)" value={config.boreMm} {...LIMITS.boreMm} format={(value) => `${value} mm`}
          oninput={(boreMm) => (draft = { boreMm })} onchange={(boreMm) => apply({ boreMm })} />
        <Slider label="Régime maximal" value={config.redlineRpm} {...LIMITS.redlineRpm} format={(value) => `${value} tr/min`}
          oninput={(redlineRpm) => (draft = { redlineRpm })} onchange={(redlineRpm) => apply({ redlineRpm })} />
        <div>
          <Slider label="Multiplicateur de régime" value={config.multiplier || preview.multiplier} {...LIMITS.multiplier} format={(value) => `×${value.toFixed(2).replace('.', ',')}`}
            oninput={(multiplier) => (draft = { multiplier })} onchange={(multiplier) => apply({ multiplier })} />
          <p class="help" data-testid="preview">Voiture à 2000 tr/min : moteur à {rpmText(preview.rpms[0])}. À 4000 : {rpmText(preview.rpms[1])}.</p>
        </div>
      {/if}

      <div class="actions">
        <button type="button" onclick={copyLink} data-testid="share">Copier le lien</button>
        <button type="button" disabled={!loaded} onclick={() => exportWav('blips')} data-testid="export-blips">WAV coups de gaz</button>
        <button type="button" disabled={!loaded} onclick={() => exportWav('lap')} data-testid="export-lap">WAV balade</button>
      </div>
      {#if notice}
        <p class="help" role="status">{notice}</p>
      {/if}
    </section>
    </div>
  </main>

  <footer>
    <p>Moteur écrit en C++, compilé en WebAssembly. Code sous licence MIT.</p>
  </footer>
</div>

<style>
  .page { max-width: 1120px; margin: 0 auto; padding: 16px 16px 40px; }
  header { margin-bottom: 12px; }
  h1 { margin: 0; font-size: 1.7rem; font-weight: 650; letter-spacing: -0.01em; }
  header p { margin: 4px 0 0; color: var(--text-dim); }
  h2 { margin: 0 0 8px; color: var(--text-dim); font-size: 0.85rem; font-weight: 400; }
  main { display: grid; grid-template-columns: minmax(0, 5fr) minmax(0, 6fr); gap: 16px; align-items: start; }
  .card { display: flex; flex-direction: column; gap: 20px; padding: 20px; background: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); }
  /* La pédale reste sous la main pendant qu'on règle le moteur. */
  .cockpit { position: sticky; top: 12px; gap: 14px; padding: 14px 20px; }
  .settings { display: flex; flex-direction: column; gap: 16px; min-width: 0; }
  .start {
    min-height: 52px;
    border: 1px solid var(--accent);
    border-radius: var(--radius);
    background: var(--accent);
    color: var(--on-accent);
    font-size: 1.05rem;
    font-weight: 650;
    cursor: pointer;
  }
  .start.on { background: transparent; color: var(--text); border-color: var(--line-strong); font-weight: 500; }
  .start:disabled { opacity: 0.6; cursor: wait; }
  .modes { display: flex; flex-direction: column; gap: 8px; }
  .help { margin: 6px 0 0; color: var(--text-dim); font-size: 0.85rem; }
  .pedals { display: flex; gap: 10px; }
  .preset { display: flex; flex-direction: column; gap: 6px; }
  .preset span { color: var(--text-dim); font-size: 0.85rem; }
  .preset small { color: var(--text-dim); font-size: 0.85rem; }
  select {
    min-height: 44px;
    padding: 0 12px;
    border: 1px solid var(--line-strong);
    border-radius: 8px;
    background: var(--surface-raised);
    color: var(--text);
    font-size: 1rem;
  }
  .summary { margin: 0; color: var(--accent); font-weight: 500; font-variant-numeric: tabular-nums; }
  .actions { display: flex; flex-wrap: wrap; gap: 8px; padding-top: 4px; border-top: 1px solid var(--line); }
  .actions button {
    min-height: 40px;
    margin-top: 12px;
    padding: 0 14px;
    border: 1px solid var(--line-strong);
    border-radius: 8px;
    background: transparent;
    color: var(--text);
    cursor: pointer;
  }
  .actions button:hover:not(:disabled) { background: var(--surface-raised); }
  .failure { margin: 0 0 16px; padding: 12px 14px; border: 1px solid var(--danger); border-radius: 8px; color: var(--text); background: color-mix(in srgb, var(--danger) 18%, transparent); }
  footer { margin-top: 20px; color: var(--text-dim); font-size: 0.85rem; }
  footer p { margin: 0; }
  @media (max-width: 860px) {
    main { grid-template-columns: minmax(0, 1fr); }
    .cockpit { position: static; }
  }
</style>
