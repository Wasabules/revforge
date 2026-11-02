import { defineConfig } from '@playwright/test';

// Un port par arbre de travail : deux lancements simultanés ne testent pas le même `web/dist`.
const PORT = process.env.REVFORGE_PW_PORT ?? '4173';

export default defineConfig({
  testDir: 'tests',
  timeout: 60_000,
  fullyParallel: false,
  workers: 1,
  use: {
    baseURL: `http://127.0.0.1:${PORT}/`,
    // Ces spécifications lisent le texte français ; i18n.spec.ts repart d'un stockage vide pour tester l'anglais.
    storageState: { cookies: [], origins: [{ origin: `http://127.0.0.1:${PORT}`, localStorage: [{ name: 'revforge.lang', value: 'fr' }] }] },
  },
  projects: [
    { name: 'chromium', use: { browserName: 'chromium', launchOptions: { args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'] } } },
    { name: 'firefox', use: { browserName: 'firefox', launchOptions: { firefoxUserPrefs: { 'media.volume_scale': '0.0', 'media.autoplay.default': 0, 'media.navigator.streams.fake': true } } } },
  ],
  webServer: {
    command: `npm run preview -- --host 127.0.0.1 --port ${PORT} --strictPort`,
    url: `http://127.0.0.1:${PORT}/`,
    reuseExistingServer: true,
  },
});
