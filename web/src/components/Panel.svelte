<script lang="ts">
  import type { Snippet } from 'svelte';
  import { heated } from '../lib/heat';
  import { t } from '../lib/i18n.svelte';

  interface Props {
    id: string;
    title: string;
    open: boolean;
    onclose: () => void;
    children: Snippet;
  }
  let { id, title, open, onclose, children }: Props = $props();

  const uid = $props.id();
  let box: HTMLDialogElement;

  // Le réglage s'écoute et se voit pendant qu'on le change : la fenêtre n'est pas modale, le reste de la page reste vivant.
  $effect(() => {
    if (open && !box.open) {
      box.show();
      // Sur le cadre, pas sur le premier bouton : Espace reste la pédale.
      box.focus();
    } else if (!open && box.open) {
      box.close();
    }
  });
</script>

<dialog id="panel-{id}" class="panel" tabindex="-1" aria-labelledby={uid} data-testid="panel-{id}" bind:this={box} use:heated>
  <header>
    <h2 id={uid}>{title}</h2>
    <button type="button" class="close" aria-label={t('panel.close', { title })} onclick={onclose} data-testid="panel-{id}-close">✕</button>
  </header>
  <div class="body">
    {@render children()}
  </div>
</dialog>

<style>
  .panel {
    position: fixed;
    inset: var(--panel-top, 12px) 12px 12px auto;
    z-index: 20;
    display: none;
    flex-direction: column;
    width: var(--panel-w, 440px);
    height: auto;
    max-width: none;
    max-height: none;
    margin: 0;
    padding: 0;
    overflow: hidden;
    border: 0;
    background: var(--surface);
    color: var(--text);
    box-shadow: 0 0 0 8px var(--bg), 0 20px 48px rgb(0 0 0 / 0.7);
  }
  .panel[open] { display: flex; }
  .panel:focus { outline: none; }
  header { flex: none; display: flex; justify-content: space-between; align-items: center; gap: 8px; padding: 8px 8px 4px 16px; }
  h2 { margin: 0; font-family: var(--display); font-size: 1.75rem; font-weight: 800; line-height: 1; text-transform: uppercase; }
  .close { flex: none; width: 44px; height: 44px; border: 0; background: transparent; color: var(--text-dim); font-size: 1rem; cursor: pointer; }
  .close:hover { background: var(--surface-raised); color: var(--text); }
  .body { flex: 1; min-height: 0; overflow-y: auto; overscroll-behavior: contain; padding: 14px 16px 18px; display: flex; flex-direction: column; gap: 16px; }
  /* Sur un téléphone, un volet par le bas, posé sur le poste de conduite : les pédales restent libres. */
  @media (max-width: 860px) {
    .panel {
      inset: auto 0 var(--dock-h, 0) 0;
      width: auto;
      height: min(calc(100dvh - var(--dock-h, 0px) - 220px), 620px);
      min-height: 280px;
      box-shadow: 0 -16px 36px rgb(0 0 0 / 0.65);
    }
  }
</style>
