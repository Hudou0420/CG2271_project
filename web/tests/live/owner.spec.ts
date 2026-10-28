import { test, expect } from "@playwright/test";
const database = "http://127.0.0.1:9000";
const ns = "demo-vault-default-rtdb";
const email = "team@vault.test";
const password = "owner-test-password";
async function write(path: string, value: unknown) {
  const response = await fetch(`${database}/${path}.json?ns=${ns}`, {
    method: "PUT",
    headers: {
      Authorization: "Bearer owner",
      "Content-Type": "application/json",
    },
    body: JSON.stringify(value),
  });
  if (!response.ok) throw new Error(`Emulator seed failed: ${response.status}`);
}
async function read(path: string) {
  const r = await fetch(`${database}/${path}.json?ns=${ns}`, {
    headers: { Authorization: "Bearer owner" },
  });
  return r.json();
}
const liveStatus = (state: string, extra = {}) => ({
  state,
  mcuOnline: true,
  dial: 0,
  digitsEntered: 0,
  attempts: 0,
  knocks: 0,
  closeness: 0,
  updatedAt: Date.now(),
  inputsReady: true,
  beamEnabled: false,
  lightValid: true,
  light: 194,
  ...extra,
});
let uid: string;
test.beforeAll(async () => {
  const response = await fetch(
    "http://127.0.0.1:9099/identitytoolkit.googleapis.com/v1/accounts:signUp?key=demo-key",
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email, password, returnSecureToken: true }),
    },
  );
  const data = await response.json();
  uid = data.localId;
  if (!uid) throw new Error("Could not create emulator-only owner account");
});
test.beforeEach(async () => {
  await write("", {
    access: { owners: { [uid]: true } },
    vault: { status: liveStatus("IDLE") },
  });
});
test("owner sign-in, leading-zero arm, authoritative state and disarm round trip", async ({
  page,
}) => {
  await page.goto("/");
  await expect(page.getByText("VAULT ONLINE", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Owner access" }).click();
  await page.getByLabel("Email", { exact: true }).fill(email);
  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await expect(
    page.getByRole("heading", { name: "Set the next heist." }),
  ).toBeVisible();
  await page.getByLabel("Challenger", { exact: true }).fill("Test challenger");
  await page.getByLabel("Secret code", { exact: true }).fill("007");
  await expect(page.getByLabel("Secret code", { exact: true })).toHaveAttribute(
    "type",
    "password",
  );
  await page
    .getByRole("button", { name: "Arm the vault", exact: true })
    .click();
  await expect
    .poll(async () => Object.keys((await read("vault/commands")) || {}).length)
    .toBe(1);
  const queued = await read("vault/commands");
  const key = Object.keys(queued)[0];
  expect(queued[key].code).toBe("007");
  await expect(page.getByLabel("Secret code", { exact: true })).toHaveValue("");
  await write(`vault/commandResults/${key}`, {
    result: "delivered",
    at: Date.now(),
  });
  await expect(
    page.getByRole("button", { name: "Waiting for the vault…", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("img", { name: "The vault is idle" }),
  ).toBeVisible();
  await write(`vault/events/${key}`, {
    type: "armed",
    player: "Test challenger",
    at: Date.now(),
  });
  await write("vault/status", liveStatus("LOCKED"));
  await write("vault/commands", null);
  await expect(
    page.getByRole("img", { name: "The vault is locked" }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Disarm and return to standby" })
    .click();
  await expect
    .poll(async () => Object.keys((await read("vault/commands")) || {}).length)
    .toBe(1);
  const disarm = await read("vault/commands");
  const dk = Object.keys(disarm)[0];
  await write(`vault/commandResults/${dk}`, {
    result: "delivered",
    at: Date.now(),
  });
  await write("vault/status", liveStatus("IDLE"));
  await expect(
    page.getByRole("img", { name: "The vault is idle" }),
  ).toBeVisible();
  await page.keyboard.press("Escape");
  await page.reload();
  await page.getByRole("button", { name: "Owner panel" }).click();
  await expect(
    page.getByRole("heading", { name: "Set the next heist." }),
  ).toBeVisible();
});
test("public reload recovers past solve without replaying celebration; stale link is clear", async ({
  page,
}) => {
  await write("vault/status", liveStatus("UNLOCKED", { attempts: 2 }));
  await write("vault/events", {
    a: { type: "armed", player: "Returning player", at: Date.now() - 50000 },
    b: {
      type: "solved",
      player: "Returning player",
      seconds: 42,
      attempts: 2,
      at: Date.now() - 1000,
    },
  });
  await page.goto("/");
  await expect(
    page.getByText("Returning player", { exact: true }),
  ).toBeVisible();
  await expect(page.getByText("00:42", { exact: true })).toBeVisible();
  await expect(page.locator(".vault-art")).not.toHaveClass(/celebrate/);
  await write(
    "vault/status",
    liveStatus("UNLOCKED", { updatedAt: Date.now() - 31000 }),
  );
  await expect(page.getByText("VAULT OFFLINE", { exact: true })).toBeVisible();
  await expect(
    page.getByRole("img", { name: "The vault is unknown" }),
  ).toBeVisible();
});
test("owner gets command rejection feedback and controls stay disabled during alarm", async ({
  page,
}) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Owner access" }).click();
  await page.getByLabel("Email", { exact: true }).fill(email);
  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await page.getByLabel("Challenger", { exact: true }).fill("Player");
  await page.getByLabel("Secret code", { exact: true }).fill("123");
  await page
    .getByRole("button", { name: "Arm the vault", exact: true })
    .click();
  await expect
    .poll(async () => Object.keys((await read("vault/commands")) || {}).length)
    .toBe(1);
  const key = Object.keys(await read("vault/commands"))[0];
  await write(`vault/commandResults/${key}`, {
    result: "rejected: link busy",
    at: Date.now(),
  });
  await expect(page.getByRole("status")).toContainText("rejected: link busy");
  await write("vault/status", liveStatus("ALARM"));
  await expect(
    page.getByRole("button", { name: "Arm the vault", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Disarm and return to standby" }),
  ).toBeDisabled();
});
test("a missing acknowledgement times out once and never resends the command", async ({
  page,
}) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Owner access" }).click();
  await page.getByLabel("Email", { exact: true }).fill(email);
  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await page.getByLabel("Challenger", { exact: true }).fill("Player");
  await page.getByLabel("Secret code", { exact: true }).fill("042");
  await page
    .getByRole("button", { name: "Arm the vault", exact: true })
    .click();
  await expect(page.getByRole("status")).toContainText("timeout:", {
    timeout: 13000,
  });
  expect(Object.keys(await read("vault/commands")).length).toBe(1);
  await expect(page.getByLabel("Secret code", { exact: true })).toHaveValue("");
});

for (const width of [1280, 390]) {
  test(`game master reactions expire across actions and reloads at ${width}px`, async ({
    page,
  }) => {
    await page.setViewportSize({ width, height: 850 });
    const reaction = {
      roundId: "r",
      eventId: "attempt",
      actionRevision: 3,
      message: "Bold choice. Middle digit: go higher.",
      source: "local",
      at: Date.now(),
    };
    await write(
      "vault/status",
      liveStatus("LOCKED", { roundId: "r", actionRevision: 3, attempts: 3 }),
    );
    await write("vault/events/attempt", {
      type: "attempt",
      roundId: "r",
      actionRevision: 3,
      attempt: 3,
      guess: "123",
      at: Date.now(),
    });
    await write("vault/aiMessage", reaction);
    await page.goto("/");
    const bubble = page.locator(".speech-bubble");
    await expect(bubble).toContainText(reaction.message);
    await expect(bubble.locator(".ai-source")).toHaveText("Local");
    await write("vault/aiMessage", {
      ...reaction,
      source: "gemini",
      message: "A little ambitious. Middle digit: go higher.",
    });
    await expect(bubble.locator(".ai-source")).toHaveText("Gemini");
    await write(
      "vault/status",
      liveStatus("LOCKED", {
        roundId: "r",
        actionRevision: 4,
        knocks: 1,
        attempts: 3,
      }),
    );
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
    await page.reload();
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
    // An old response arriving after a new action must remain hidden.
    await write(
      "vault/status",
      liveStatus("LOCKED", { roundId: "r", actionRevision: 4, attempts: 3 }),
    );
    await write("vault/aiMessage", reaction);
    await expect(bubble).not.toContainText(reaction.message);
    await write(
      "vault/status",
      liveStatus("LOCKED", { roundId: "next-round", actionRevision: 3 }),
    );
    await page.reload();
    await expect(bubble.locator(".ai-source")).toHaveCount(0);
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
    ).toBe(true);
  });
}

test("owner controls persist the AI setting and show private health", async ({
  page,
}) => {
  await write("vault/aiStatus", {
    enabled: true,
    configured: true,
    hasCode: false,
    health: "quota-cooldown",
    cooldownSeconds: 60,
    lastSuccessUptimeS: 123,
    freeHeap: 100000,
    updatedAt: Date.now(),
  });
  await page.goto("/");
  await expect(page.getByLabel("Enable reactions and hints")).toHaveCount(0);
  await page.getByRole("button", { name: "Owner access" }).click();
  await page.getByLabel("Email", { exact: true }).fill(email);
  await page.getByLabel("Password", { exact: true }).fill(password);
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  const toggle = page.getByLabel("Enable reactions and hints");
  await expect(toggle).toBeChecked();
  await expect(
    page.getByText("Free quota reached. Using local reactions."),
  ).toBeVisible();
  await toggle.uncheck();
  await expect.poll(() => read("vault/aiSettings/enabled")).toBe(false);
  await expect(page.getByText("Game master is off.")).toBeVisible();
  await write("vault/aiSettings/enabled", true);
  await expect(toggle).toBeChecked();
});
