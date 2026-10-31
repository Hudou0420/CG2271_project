import { test, expect } from "../fixtures";
for (const state of [
  "IDLE",
  "LOCKED",
  "DIALING",
  "UNLOCKED",
  "ALARM",
  "UNKNOWN",
]) {
  test(`renders ${state} without console errors or horizontal overflow`, async ({
    page,
  }) => {
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.goto(`/?preview=1&state=${state}`);
    await expect(
      page.getByText("DESIGN PREVIEW · SAMPLE DATA · NO DEVICE COMMANDS"),
    ).toBeVisible();
    await expect(
      page.getByRole("img", { name: `The vault is ${state.toLowerCase()}` }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Enable sound" }),
    ).toHaveAttribute("aria-pressed", "false");
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
    ).toBe(true);
    expect(errors).toEqual([]);
  });
}
test("shows masked digit progress, current digit, recent guesses and full rankings", async ({
  page,
}) => {
  await page.goto("/?preview=1&state=DIALING");
  await expect(page.getByLabel("1 of 3 digits confirmed")).toBeVisible();
  await expect(page.getByRole("group", { name: "Guess 428, cool, 67% closeness", exact: true })).toBeVisible();
  await expect(page.locator(".digit-slots > span")).toHaveText(["•", "7", ""]);
  await expect(page.locator(".digit-slots > .solved")).toHaveCount(0);
  const latest = page.getByRole("group", { name: "Guess 735, warm, 85% closeness", exact: true });
  await expect(latest.locator("span")).toHaveText(["7", "3", "5"]);
  await expect(latest.locator(".feedback-present")).toHaveText("7");
  await expect(latest.locator(".feedback-correct")).toHaveText("3");
  await expect(latest.locator(".feedback-absent")).toHaveText("5");
  await page.getByRole("button", { name: "The full leaderboard" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await expect(
    page.getByRole("dialog").getByText("The getaway driver"),
  ).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("dialog")).toHaveCount(0);
});
test("public preview cannot send device commands, and help keeps the rhythm secret", async ({
  page,
}) => {
  await page.goto("/?preview=1");
  await page.getByRole("button", { name: "Owner access" }).click();
  await expect(
    page.getByRole("dialog").getByText(/design preview with sample data/),
  ).toBeVisible();
  await expect(page.getByRole("button", { name: "Arm the vault" })).toHaveCount(
    0,
  );
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "How to play" }).click();
  await expect(
    page.getByRole("dialog").getByText(/300|900|0\.3|0\.9/),
  ).toHaveCount(0);
});
test("phone layout and reduced motion remain usable", async ({ page }, testInfo) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.goto("/?preview=1&state=ALARM");
  await expect(
    page.getByRole("heading", { name: "Caught in the act." }),
  ).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.getByRole("button", { name: "Owner access" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page.screenshot({
    path: testInfo.outputPath("phone-owner.png"),
    fullPage: true,
  });
});
test("screenshots for desktop and phone", async ({ page }, testInfo) => {
  await page.setViewportSize({ width: 1440, height: 1100 });
  await page.goto("/?preview=1&state=DIALING");
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: testInfo.outputPath("desktop.png"), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: testInfo.outputPath("phone.png"), fullPage: true });
});

test('connection form identifies a missing database URL in a Firebase console snippet', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Connection', exact: true }).click();
  await page.getByLabel('Firebase web configuration').fill(`const firebaseConfig = {
    apiKey: 'public-fixture',
    authDomain: 'example.firebaseapp.com',
    projectId: 'example',
  };`);
  await page.getByRole('button', { name: 'Save connection' }).click();
  await expect(page.getByRole('alert')).toContainText('Missing databaseURL');
  await expect(page.getByRole('alert')).toContainText('Realtime Database');
});

for (const width of [320, 390, 1280]) {
  test(`digit tiles remain square and fit at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 });
    await page.emulateMedia({ reducedMotion: "reduce" });
    await page.goto("/?preview=1&state=DIALING");
    const tiles = page.locator(".digit-slots > span, .guess-tiles > span");
    expect(await tiles.count()).toBe(9);
    for (const tile of await tiles.all()) {
      const bounds = await tile.boundingBox();
      expect(bounds).not.toBeNull();
      expect(Math.abs(bounds!.width - bounds!.height)).toBeLessThanOrEqual(1);
      expect(bounds!.x).toBeGreaterThanOrEqual(0);
      expect(bounds!.x + bounds!.width).toBeLessThanOrEqual(width);
    }
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    await page.getByRole("combobox", { name: "Scene", exact: true }).selectOption("UNLOCKED");
    await expect(page.locator(".digit-slots > .solved")).toHaveCount(3);
  });
}
