<script lang="ts">
  interface Props {
    label: string;
    value: number;
    min: number;
    max: number;
    step: number;
    format: (value: number) => string;
    disabled?: boolean;
    /** Pendant le glissé : affichage seulement. */
    oninput?: (value: number) => void;
    /** Au relâchement : le changer à chaque pixel couperait le son sans cesse. */
    onchange: (value: number) => void;
  }
  let { label, value, min, max, step, format, disabled = false, oninput, onchange }: Props = $props();

  const uid = $props.id();
  let dragging = $state<number | null>(null);
  const shown = $derived(dragging ?? value);
</script>

<div class="slider" class:disabled>
  <div class="head">
    <label for={uid}>{label}</label>
    <output for={uid}>{format(shown)}</output>
  </div>
  <input
    id={uid}
    type="range"
    {min}
    {max}
    {step}
    value={shown}
    {disabled}
    style:--progress="{((shown - min) / (max - min)) * 100}%"
    oninput={(event) => {
      dragging = Number(event.currentTarget.value);
      oninput?.(dragging);
    }}
    onchange={(event) => {
      const next = Number(event.currentTarget.value);
      dragging = null;
      onchange(next);
    }}
  />
</div>

<style>
  .disabled { opacity: 0.45; }
  .head { display: flex; justify-content: space-between; gap: 12px; margin-bottom: 6px; }
  label { color: var(--text-dim); font-size: 0.85rem; }
  output { font-size: 0.95rem; color: var(--text); font-weight: 600; font-variant-numeric: tabular-nums; white-space: nowrap; }
  input {
    width: 100%;
    height: 28px;
    margin: 0;
    background: transparent;
    appearance: none;
    -webkit-appearance: none;
    cursor: pointer;
  }
  input:disabled { cursor: not-allowed; }
  input::-webkit-slider-runnable-track {
    height: 6px;
    background: linear-gradient(to right, var(--control) var(--progress), var(--line-strong) var(--progress));
  }
  input::-moz-range-track { height: 6px; background: var(--line-strong); }
  input::-moz-range-progress { height: 6px; background: var(--control); }
  input::-webkit-slider-thumb {
    appearance: none;
    -webkit-appearance: none;
    width: 12px;
    height: 24px;
    margin-top: -9px;
    border: 0;
    border-radius: 0;
    background: var(--text);
  }
  input::-moz-range-thumb { width: 12px; height: 24px; border: 0; border-radius: 0; background: var(--text); }
  /* Au doigt, la cible fait ses 44 px. */
  @media (pointer: coarse) { input { height: 44px; } }
</style>
