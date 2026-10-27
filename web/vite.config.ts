import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";

export default defineConfig({
  envDir: fileURLToPath(new URL("../../Project_Secrets/", import.meta.url)),
  plugins: [react()],
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          firebase: ["firebase/app", "firebase/auth", "firebase/database"],
        },
      },
    },
  },
  test: { include: ["src/**/*.test.ts"], maxWorkers: 2 },
});
