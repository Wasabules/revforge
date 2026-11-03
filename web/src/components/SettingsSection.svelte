<script lang="ts">
  import type { Snippet } from 'svelte';

  /** Un groupe de réglages dans un volet : un titre, puis ses commandes. Rien n'y est caché, le titre ne fait que ranger. */
  interface Props {
    id: string;
    title: string;
    children: Snippet;
  }
  let { id, title, children }: Props = $props();

  const uid = $props.id();
</script>

<section class="section" aria-labelledby={uid} data-testid="section-{id}">
  <h3 id={uid}>{title}</h3>
  {@render children()}
</section>

<style>
  .section { display: flex; flex-direction: column; gap: 16px; min-width: 0; }
  /* Le ton sépare, pas une bordure : le titre se lit comme ceux de la page, en plus petit. */
  h3 { margin: 8px 0 -4px; color: var(--text-quiet); font-family: var(--display); font-size: 1.125rem; font-weight: 800; line-height: 1; text-transform: uppercase; letter-spacing: 0.02em; }
  .section:first-child h3 { margin-top: 0; }
</style>
