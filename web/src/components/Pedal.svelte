<script lang="ts">
  import { heated } from '../lib/heat';
  interface Props {
    label: string;
    hint: string;
    pressure: number;
    tone?: 'accent' | 'danger';
    disabled?: boolean;
    variable?: boolean;
    onchange: (pressure: number) => void;
  }
  let { label, hint, pressure, tone = 'accent', disabled = false, variable = false, onchange }: Props = $props();

  let held = false;

  /** Plus on appuie haut, plus on enfonce : de 30 % en bas à 100 % en haut. */
  function pressureAt(event: PointerEvent): number {
    if (!variable) return 1;
    const box = (event.currentTarget as HTMLElement).getBoundingClientRect();
    const height = 1 - (event.clientY - box.top) / box.height;
    return Math.min(1, 0.3 + Math.max(0, height) * 1);
  }

  function down(event: PointerEvent) {
    if (disabled) return;
    event.preventDefault();
    (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
    held = true;
    onchange(pressureAt(event));
  }
  function move(event: PointerEvent) {
    if (held) onchange(pressureAt(event));
  }
  function up() {
    if (!held) return;
    held = false;
    onchange(0);
  }
</script>

<button
  type="button"
  class="pedal {tone}"
  class:active={pressure > 0}
  {disabled}
  aria-label={label}
  aria-pressed={pressure > 0}
  onpointerdown={down}
  onpointermove={move}
  onpointerup={up}
  onpointercancel={up}
  onlostpointercapture={up}
  oncontextmenu={(event) => event.preventDefault()}
>
  <span class="fill" style:height="{pressure * 100}%" use:heated></span>
  <span class="text">
    <strong>{label}</strong>
    <small>{hint}</small>
  </span>
</button>

<style>
  .pedal {
    position: relative;
    flex: 1;
    /* Trois pédales dans une colonne étroite : leur nom se règle sur leur largeur, sans déborder. */
    container-type: inline-size;
    min-width: 0;
    padding: 6px 3px;
    min-height: var(--pedal-h, 112px);
    overflow: hidden;
    border: 0;
    background: var(--surface-raised);
    color: var(--text);
    cursor: pointer;
    touch-action: none;
    user-select: none;
    -webkit-user-select: none;
  }
  .pedal:disabled { opacity: 0.45; cursor: not-allowed; }
  .fill { position: absolute; left: 0; right: 0; bottom: 0; background: var(--heat-text); opacity: 0.5; }
  .danger .fill { background: var(--danger); }
  .active small { color: var(--text); }
  .text { position: relative; display: flex; flex-direction: column; gap: 4px; align-items: center; pointer-events: none; }
  strong { font-family: var(--display); font-size: clamp(0.9rem, 17cqi, 1.375rem); font-weight: 700; line-height: 1.1; text-transform: uppercase; white-space: nowrap; }
  small { color: var(--text-dim); font-size: clamp(0.65rem, 11cqi, var(--type-small)); line-height: 1.2; }
</style>
