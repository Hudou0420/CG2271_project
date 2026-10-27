import { defineConfig } from "vitest/config";
// Firebase's Node WebSocket client ignores NO_PROXY. Emulator tests are
// loopback-only and must never route local requests through a network proxy.
for (const key of ["HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy"])
  delete process.env[key];
export default defineConfig({
  test: {
    include: ["tests/database.rules.test.ts"],
    maxWorkers: 1,
    testTimeout: 15000,
    hookTimeout: 15000,
  },
});
