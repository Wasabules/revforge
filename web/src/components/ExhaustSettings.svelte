<script lang="ts">
  import Choices from './Choices.svelte';
  import { EXHAUSTS, POPS, ROUTINGS, type EngineConfig } from '../lib/config';
  import { fixed, t, whole } from '../lib/i18n.svelte';
  import { expansionChamber } from '../lib/twoStroke';

  interface Props {
    config: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
  }
  let { config, onapply }: Props = $props();

  const popCharacter = $derived(EXHAUSTS.find((item) => item.value === config.exhaust)?.pops ?? '');
  /** La chambre de détente est faite pour un deux-temps : proposée là, ou si un lien l'a déjà choisie. */
  const lines = $derived(EXHAUSTS.filter((item) => item.value !== 'expansion' || config.cycle === 'two_stroke' || config.exhaust === 'expansion'));
  /** La chambre se calcule sur ce moteur : sa longueur accordée, le régime visé, la vitesse du son qui l'accorde. */
  const chamber = $derived(config.exhaust === 'expansion' ? expansionChamber(config) : null);
</script>

{#if config.electric}
  <p class="help">{t('builder.electricHelp')}</p>
{:else}
  <Choices label={t('builder.exhaust')} options={lines} value={config.exhaust} onchange={(exhaust) => onapply({ exhaust })} />
  {#if chamber}
    <p class="help" data-testid="expansion-help">{t('builder.expansion.help', { length: fixed(chamber.header + chamber.diffuser + chamber.belly + chamber.baffle / 2, 2), rpm: whole(chamber.tunedRpm), speed: whole(chamber.soundMs) })}</p>
  {/if}
  <Choices label={t('builder.routing')} options={ROUTINGS} value={config.routing} onchange={(routing) => onapply({ routing })} />
  {#if config.fuel === 'diesel'}
    <p class="help">{t('builder.dieselNoPops')}</p>
  {:else}
    <div>
      <Choices label={t('builder.pops')} options={POPS} value={config.pops} onchange={(pops) => onapply({ pops })} />
      {#if config.pops !== 'off'}
        <p class="help">{popCharacter}</p>
      {/if}
    </div>
  {/if}
{/if}

<style>
  .help { margin: 6px 0 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
