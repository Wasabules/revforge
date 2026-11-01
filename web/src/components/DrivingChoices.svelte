<script lang="ts">
  import { DRIVE_SCENARIOS, LEG_KINDS, SHIFT_MODES, type DriveScenarioId, type ShiftModeId } from '../lib/config';
  import type { Engine, Telemetry } from '../lib/engine';
  import { t, type MessageKey } from '../lib/i18n.svelte';

  /** Les choix de conduite, sans rapport avec les pièces de la voiture : ils restent hors du lien de partage. */
  interface Props {
    engine: Engine;
    driveMode: 'neutral' | 'drive' | 'lap';
    gearbox: 'auto' | 'manual';
    electric: boolean;
    running: boolean;
    telemetry: Telemetry;
    shiftMode: ShiftModeId;
  }
  let { engine, driveMode, gearbox, electric, running, telemetry, shiftMode = $bindable() }: Props = $props();

  let scenario = $state<DriveScenarioId>('city');

  // Aucun rapport à choisir en électrique ni en manuelle, et le pilote automatique ne se sert pas des palettes.
  const showsShift = $derived(!electric && (driveMode === 'lap' || (driveMode === 'drive' && gearbox === 'auto')));
  const shiftOptions = $derived(driveMode === 'lap' ? SHIFT_MODES.filter((mode) => mode.value !== 'paddles') : SHIFT_MODES);
  const shownShift = $derived(driveMode === 'lap' && shiftMode === 'paddles' ? 'normal' : shiftMode);
  const shiftHelp = $derived(SHIFT_MODES.find((mode) => mode.value === shownShift)!.help);
  const chosen = $derived(DRIVE_SCENARIOS.find((item) => item.value === scenario)!);

  const leg = $derived.by(() => {
    if (!running || driveMode !== 'lap') return '';
    const name = t(`leg.${LEG_KINDS[telemetry.legKind] ?? LEG_KINDS[0]}` as MessageKey);
    const pct = Math.round(Math.abs(telemetry.gradePct));
    return t('leg.current', { scenario: chosen.label, leg: pct >= 1 ? t('leg.withGrade', { leg: name, pct }) : name });
  });

  function chooseShift(event: Event & { currentTarget: HTMLSelectElement }) {
    shiftMode = event.currentTarget.value as ShiftModeId;
    engine.setShiftMode(SHIFT_MODES.findIndex((mode) => mode.value === shiftMode));
    // Le clavier conduit : il ne doit pas rester dans la liste.
    event.currentTarget.blur();
  }

  function chooseScenario(event: Event & { currentTarget: HTMLSelectElement }) {
    scenario = event.currentTarget.value as DriveScenarioId;
    engine.setScenario(DRIVE_SCENARIOS.findIndex((item) => item.value === scenario));
    event.currentTarget.blur();
  }
</script>

{#if showsShift}
  <label class="choice">
    <span>{t('shiftMode.label')}</span>
    <select value={shownShift} onchange={chooseShift} data-testid="shift-mode">
      {#each shiftOptions as mode (mode.value)}
        <option value={mode.value}>{mode.label}</option>
      {/each}
    </select>
    <small data-testid="shift-mode-help">{shiftHelp}</small>
  </label>
{/if}

{#if driveMode === 'lap'}
  <label class="choice">
    <span>{t('scenario.label')}</span>
    <select value={scenario} onchange={chooseScenario} data-testid="drive-scenario">
      {#each DRIVE_SCENARIOS as item (item.value)}
        <option value={item.value}>{item.label}</option>
      {/each}
    </select>
    <small>{chosen.help}</small>
  </label>
  {#if leg}
    <p class="leg" data-testid="drive-leg">{leg}</p>
    {#if telemetry.underpowered}
      <p class="leg weak" data-testid="drive-underpowered">{t('leg.underpowered', { target: Math.round(telemetry.legTarget) })}</p>
    {/if}
  {/if}
{/if}

<style>
  .choice { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
  .choice span, .choice small { color: var(--text-dim); font-size: var(--type-small); }
  select { min-height: 38px; padding: 0 8px; border: 0; background: var(--surface-raised); color: var(--text); font-size: var(--type-body); }
  .leg { margin: 0; font-size: var(--type-body); font-weight: 600; }
  .weak { color: var(--warn); font-weight: 500; font-size: var(--type-small); }
</style>
