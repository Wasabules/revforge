<script lang="ts">
  import Choices from './Choices.svelte';
  import Slider from './Slider.svelte';
  import { t } from '../lib/i18n.svelte';

  /**
   * Un nombre dont 0 veut dire « d'origine » (le moteur garde ce qu'il a) ou « sans » : d'abord ce choix, puis le curseur s'il
   * est réglé. `start` est la valeur proposée en quittant le 0, `none` dit « Sans » / « Monté » au lieu de « D'origine » / « Réglé ».
   */
  interface Props {
    label: string;
    /** Ce que le curseur lit : `shown` pendant le glissé. */
    value: number;
    min: number;
    max: number;
    step: number;
    start: number;
    format: (value: number) => string;
    none?: boolean;
    /** Le nom du curseur : « Valeur » par défaut. */
    valueLabel?: string;
    help?: string;
    testid?: string;
    onlive: (value: number) => void;
    onapply: (value: number) => void;
  }
  let { label, value, min, max, step, start, format, none = false, valueLabel, help, testid, onlive, onapply }: Props = $props();

  const options = $derived([
    { value: 'zero', label: t(none ? 'tuned.none' : 'tuned.stock') },
    { value: 'set', label: t(none ? 'tuned.fitted' : 'tuned.custom') },
  ]);
</script>

<div class="tuned" data-testid={testid}>
  <Choices {label} {options} value={value === 0 ? 'zero' : 'set'} onchange={(choice) => onapply(choice === 'zero' ? 0 : start)} />
  {#if value !== 0}
    <!-- « Valeur » se lit sous son titre ; le curseur, lui, se nomme en entier pour qui ne voit pas le titre. -->
    <Slider label={valueLabel ?? t('tuned.value')} ariaLabel={`${label}, ${valueLabel ?? t('tuned.value')}`} {value} {min} {max} {step} {format} oninput={onlive} onchange={onapply} />
  {/if}
  {#if help}
    <p class="help">{help}</p>
  {/if}
</div>

<style>
  .tuned { display: flex; flex-direction: column; gap: 8px; min-width: 0; }
  .help { margin: 0; color: var(--text-dim); font-size: var(--type-small); }
</style>
