import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./tests/live",
  workers: 1,
  use: { baseURL: "http://127.0.0.1:4174" },
  webServer: {
    command:
      "VITE_FIREBASE_API_KEY=demo-key VITE_FIREBASE_AUTH_DOMAIN=demo-vault.firebaseapp.com VITE_FIREBASE_DATABASE_URL=https://demo-vault-default-rtdb.firebaseio.com VITE_FIREBASE_PROJECT_ID=demo-vault VITE_USE_EMULATORS=true npm run dev -- --port 4174",
    url: "http://127.0.0.1:4174",
    reuseExistingServer: false,
  },
});
