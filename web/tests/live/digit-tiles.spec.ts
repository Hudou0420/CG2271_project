import { test, expect } from "../fixtures";

async function write(path: string, value: unknown) {
  const response = await fetch(`http://127.0.0.1:9000/${path}.json?ns=demo-vault-default-rtdb`, {
    method: "PUT",
    headers: { Authorization: "Bearer owner", "Content-Type": "application/json" },
    body: JSON.stringify(value),
  });
  if (!response.ok) throw new Error(`Emulator seed failed: ${response.status}`);
}
const status = (state: string, extra = {}) => ({ state, mcuOnline: true,
  updatedAt: Date.now(), digitsEntered: 1, dial: 7, knocks: 0, attempts: 0, ...extra });

test("submitted guesses show per-digit verdicts, preserve zeroes and keep legacy data neutral", async ({ page }) => {
  const now = Date.now();
  await write("vault", {
    status: status("LOCKED"),
    events: {
      armed: { type: "armed", at: now - 10000 },
      a: { type: "attempt", guess: "007", closeness: 39, attempt: 1, at: now - 5000 },
      b: { type: "attempt", guess: "215", feedback: "110", closeness: 86, attempt: 2, at: now - 4000 },
      c: { type: "attempt", guess: "125", feedback: "220", closeness: 93, attempt: 3, at: now - 3000 },
      d: { type: "attempt", guess: "221", feedback: "021", closeness: 89, attempt: 4, at: now - 2000 },
    },
  });
  await page.goto("/");
  await expect(page.getByRole("group", { name: "Guess 007", exact: true }).locator("span")).toHaveText(["0", "0", "7"]);
  const firstExample = page.getByRole("group", { name: "Guess 215", exact: true });
  await expect(firstExample.locator("span")).toHaveClass(["feedback-present", "feedback-present", "feedback-absent"]);
  await expect(firstExample).toHaveAttribute("aria-description", "2: present elsewhere; 1: present elsewhere; 5: absent");
  const secondExample = page.getByRole("group", { name: "Guess 125", exact: true });
  await expect(secondExample.locator("span")).toHaveClass(["feedback-correct", "feedback-correct", "feedback-absent"]);
  const repeated = page.getByRole("group", { name: "Guess 221", exact: true });
  await expect(repeated.locator("span")).toHaveClass(["feedback-absent", "feedback-correct", "feedback-present"]);
  await expect(page.getByRole("group", { name: "Guess 007", exact: true }).locator(".feedback-unknown")).toHaveCount(3);
  await write("vault/events/e", { type: "attempt", guess: "bad!", feedback: "220", closeness: 85, attempt: 5, at: now - 1000 });
  await expect(page.locator(".guess-tiles.unavailable > span")).toHaveText(["—", "—", "—"]);
  await expect(page.locator(".guess-tiles")).toHaveCount(4);
  await expect(page.getByRole("group", { name: "Guess 007", exact: true })).toHaveCount(0);
  await write("vault/events/f", { type: "attempt", attempt: 6, at: now });
  await expect(page.locator(".guess-tiles.unavailable")).toHaveCount(2);
});

test("live entry stays neutral, follows dial updates, and clears when offline or rearmed", async ({ page }) => {
  await write("vault", { status: status("DIALING"), events: { armed: { type: "armed", at: Date.now() } } });
  await page.goto("/");
  const live = page.locator(".digit-slots");
  await expect(live.locator("span")).toHaveText(["•", "7", ""]);
  await expect(live.locator(".solved")).toHaveCount(0);
  await expect(page.locator(".guess-tiles")).toHaveCount(0);
  await write("vault/status", status("DIALING", { digitsEntered: 2, dial: 0 }));
  await expect(live.locator("span")).toHaveText(["•", "•", "0"]);
  await write("vault/status", status("DIALING", { updatedAt: Date.now() - 31000 }));
  await expect(live.locator("span")).toHaveText(["", "", ""]);
  await write("vault/status", status("UNLOCKED"));
  await expect(live.locator(".solved")).toHaveCount(3);
  await write("vault/status", status("LOCKED"));
  await expect(live.locator(".empty")).toHaveCount(3);
});
