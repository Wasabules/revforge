<script lang="ts">
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
  <span class="fill" style:height="{pressure * 100}%"></span>
  <span class="text">
    <strong>{label}</strong>
    <small>{hint}</small>
  </span>
</button>

<style>
  .pedal {
    position: relative;
    flex: 1;
    min-height: 112px;
    overflow: hidden;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius);
    background: var(--surface-raised);
    color: var(--text);
    cursor: pointer;
    touch-action: none;
    user-select: none;
    -webkit-user-select: none;
  }
  .pedal:disabled { opacity: 0.45; cursor: not-allowed; }
  .pedal.active { border-color: var(--accent); }
  .pedal.danger.active { border-color: var(--danger); }
  .fill { position: absolute; left: 0; right: 0; bottom: 0; background: var(--accent); opacity: 0.32; }
  .danger .fill { background: var(--danger); }
  .text { position: relative; display: flex; flex-direction: column; gap: 4px; align-items: center; pointer-events: none; }
  strong { font-size: 1.05rem; font-weight: 600; }
  small { color: var(--text-dim); font-size: 0.8rem; }
</style>
