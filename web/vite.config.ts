import { svelte } from '@sveltejs/vite-plugin-svelte';
import { defineConfig } from 'vite';

export default defineConfig({
  // Chemins relatifs : le site marche à la racine comme dans un sous-dossier.
  base: './',
  plugins: [svelte()],
  server: { fs: { allow: ['..'] } },
});
