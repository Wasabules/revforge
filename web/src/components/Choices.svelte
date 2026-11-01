<script lang="ts" generics="T extends string | number">
  interface Props {
    label: string;
    options: { value: T; label: string }[];
    value: T;
    disabled?: boolean;
    onchange: (value: T) => void;
  }
  let { label, options, value, disabled = false, onchange }: Props = $props();
</script>

<fieldset {disabled}>
  <legend>{label}</legend>
  <div class="row">
    {#each options as option (option.value)}
      <button type="button" class:selected={option.value === value} aria-pressed={option.value === value} {disabled} onclick={() => option.value !== value && onchange(option.value)}>
        {option.label}
      </button>
    {/each}
  </div>
</fieldset>

<style>
  fieldset { border: 0; padding: 0; margin: 0; min-width: 0; }
  fieldset:disabled { opacity: 0.45; }
  legend { padding: 0; margin-bottom: 8px; color: var(--text-dim); font-size: 0.85rem; }
  .row { display: flex; flex-wrap: wrap; gap: 4px; }
  button {
    min-height: 40px;
    min-width: 40px;
    padding: 0 12px;
    border: 0;
    background: var(--surface-raised);
    color: var(--text);
    font: inherit;
    font-size: 0.9rem;
    cursor: pointer;
  }
  button:hover:not(:disabled):not(.selected) { background: var(--surface-high); }
  button.selected { background: var(--control); color: var(--on-control); font-weight: 600; }
  button:disabled { cursor: not-allowed; }
</style>
