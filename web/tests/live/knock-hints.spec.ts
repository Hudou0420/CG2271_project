import { test, expect } from "../fixtures";

async function write(path: string, value: unknown) {
  const response = await fetch(`http://127.0.0.1:9000/${path}.json?ns=demo-vault-default-rtdb`, {
    method: "PUT", headers: { Authorization: "Bearer owner", "Content-Type": "application/json" },
    body: JSON.stringify(value),
  });
  if (!response.ok) throw new Error(`Emulator seed failed: ${response.status}`);
}
const status = (extra = {}) => ({ state: "LOCKED", mcuOnline: true,
  updatedAt: Date.now(), roundId: "rhythm", actionRevision: 3, knocks: 0, attempts: 0, ...extra });
const failure = (extra = {}) => ({ type: "knock_failed", at: Date.now(),
  roundId: "rhythm", actionRevision: 3, reason: "pattern", gaps: "13", ...extra });

for (const width of [1280, 390]) {
  test(`fixed knock feedback works without AI and stays readable at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 });
    await page.emulateMedia({ reducedMotion: "reduce" });
    await write("vault", { status: status(), aiSettings: { enabled: false }, events: { k: failure() } });
    await page.goto("/");
    const card = page.getByRole("region", { name: "Knock timing feedback" });
    await expect(card).toBeVisible();
    await expect(card.locator(".knock-pause strong")).toHaveText(["Too short", "Too long"]);
    await expect(card).toContainText("Wait longer.");
    await expect(card).toContainText("Tap sooner.");
    await expect(page.locator(".count-pill")).toHaveText("0");
    await expect(card).not.toContainText(/300|900|Gemini|Local/);
    await write("vault/aiSettings/enabled", true);
    await expect(card).toBeVisible();
    // Older firmware's generated/local knock bubbles must not compete with the card.
    for (const source of ["local", "gemini"]) {
      await write("vault/aiMessage", {roundId: "rhythm", eventId: "k", actionRevision: 3,
        message: "An old generated knock hint.", source, at: Date.now()});
      await expect(page.locator(".speech-bubble .ai-source")).toHaveCount(0);
    }
    await page.reload();
    await expect(card).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    await card.screenshot({ path: `../test-reports/knock-feedback-${width}.png` });

    await write("vault/status", status({ knocks: 1, actionRevision: 4 }));
    await expect(card).toHaveCount(0);
    await write("vault/status", status({ knocks: 0, actionRevision: 4 }));
    await expect(card).toHaveCount(0);
    await write("vault/events/k2", failure({ reason: "timeout", gaps: "20", actionRevision: 5 }));
    await write("vault/status", status({ actionRevision: 5 }));
    await expect(card).toContainText("Sequence timed out.");
    await expect(card.locator(".knock-pause strong")).toHaveText(["Timing good", "Not captured"]);
    for (const state of ["DIALING", "UNLOCKED", "ALARM", "IDLE"]) {
      await write("vault/status", status({ state, actionRevision: 5 }));
      await expect(card).toHaveCount(0);
    }
    await write("vault/status", status({ actionRevision: 5, mcuOnline: false }));
    await expect(card).toHaveCount(0);
    await write("vault/status", status({ actionRevision: 5, updatedAt: Date.now() - 31000 }));
    await expect(card).toHaveCount(0);
    await write("vault/status", status({ roundId: "new", actionRevision: 5 }));
    await expect(card).toHaveCount(0);
    await write("vault/events/k2", failure({ actionRevision: 5, gaps: "bad" }));
    await write("vault/status", status({ actionRevision: 5 }));
    await expect(card).toHaveCount(0);
  });
}
