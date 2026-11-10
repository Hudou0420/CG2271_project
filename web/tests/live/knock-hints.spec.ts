import { test, expect } from "../fixtures";

async function write(path: string, value: unknown) {
  const response = await fetch(`http://127.0.0.1:9000/${path}.json?ns=demo-vault-default-rtdb`, {
    method: "PUT", headers: { Authorization: "Bearer owner", "Content-Type": "application/json" },
    body: JSON.stringify(value),
  });
  if (!response.ok) throw new Error(`Emulator seed failed: ${response.status}`);
}
for (const width of [1280, 390]) {
  test(`failed rhythm hints survive reload and expire on the next try at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 850 });
    const state = { state: "LOCKED", mcuOnline: true, updatedAt: Date.now(),
      roundId: "rhythm", actionRevision: 3, knocks: 0, attempts: 0 };
    const event = { type: "knock_failed", reason: "pattern", gaps: "13", at: Date.now(),
      roundId: "rhythm", actionRevision: 3 };
    const local = { roundId: "rhythm", eventId: "failed", actionRevision: 3,
      message: "Not quite. Lengthen the pause after the opening tap.", source: "local", at: Date.now() };
    await write("vault", { status: state, events: {
      armed: { type: "armed", at: Date.now() - 10000 }, failed: event,
    }, aiMessage: local });
    await page.goto("/");
    const bubble = page.locator(".speech-bubble");
    await expect(bubble).toContainText(local.message);
    await expect(bubble.locator(".ai-source")).toHaveText("Local");
    await expect(page.locator(".attempts-panel .count-pill")).toHaveText("0");
    await expect(page.locator(".guess-tiles")).toHaveCount(0);
    await write("vault/aiMessage", { ...local, source: "gemini",
      message: "Almost. Shorten the pause before the last tap." });
    await expect(bubble.locator(".ai-source")).toHaveText("Gemini");
    await page.reload();
    await expect(bubble).toContainText("Shorten the pause before the last tap.");
    await write("vault/status", { ...state, updatedAt: Date.now(), actionRevision: 4, knocks: 1 });
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
    await write("vault/aiMessage", { ...local, source: "gemini" });
    await page.reload();
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
    await write("vault/status", { ...state, updatedAt: Date.now(), roundId: "next", actionRevision: 5 });
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
  });
}
