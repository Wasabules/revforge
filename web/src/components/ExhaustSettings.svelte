<script lang="ts">
  import Choices from './Choices.svelte';
  import { EXHAUSTS, POPS, ROUTINGS, type EngineConfig } from '../lib/config';
  import { t } from '../lib/i18n.svelte';

  interface Props {
    config: EngineConfig;
    onapply: (next: Partial<EngineConfig>) => void;
  }
  let { config, onapply }: Props = $props();

  const popCharacter = $derived(EXHAUSTS.find((item) => item.value === config.exhaust)?.pops ?? '');
  /** La chambre de détente est faite pour un deux-temps : proposée là, ou si un lien l'a déjà choisie. */
  const lines = $derived(EXHAUSTS.filter((item) => item.value !== 'expansion' || config.cycle === 'two_stroke' || config.exhaust === 'expansion'));
</script>

{#if config.electric}
  <p class="help">{t('builder.electricHelp')}</p>
{:else}
  <Choices label={t('builder.exhaust')} options={lines} value={config.exhaust} onchange={(exhaust) => onapply({ exhaust })} />
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
