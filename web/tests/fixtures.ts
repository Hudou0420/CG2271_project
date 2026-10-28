import { test as base, expect } from "@playwright/test";
// Browser tests are loopback-only. Fonts are cosmetic and deterministic here.
export const test = base.extend<{ networkIsolation: void }>({
  networkIsolation: [async ({ context }, use) => {
    const unexpected: string[] = [];
    await context.route("**/*", async route => {
      const url = new URL(route.request().url());
      if (["127.0.0.1", "localhost", "[::1]"].includes(url.hostname)) {
        await route.continue();
      } else if (url.hostname === "fonts.googleapis.com") {
        await route.fulfill({ contentType: "text/css", body: "" });
      } else if (url.hostname === "fonts.gstatic.com") {
        await route.abort();
      } else {
        unexpected.push(url.origin + url.pathname);
        await route.abort();
      }
    });
    await context.routeWebSocket(/.*/, socket => {
      const url = new URL(socket.url());
      if (["127.0.0.1", "localhost", "[::1]"].includes(url.hostname)) socket.connectToServer();
      else { unexpected.push(url.origin); socket.close(); }
    });
    await use();
    expect(unexpected, "Tests must never contact production services").toEqual([]);
  }, { auto: true }],
});
export { expect };
