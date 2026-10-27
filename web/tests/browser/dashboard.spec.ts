import { test, expect } from "@playwright/test";
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
  await expect(page.getByText("428", { exact: true })).toBeVisible();
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
test("phone layout and reduced motion remain usable", async ({ page }) => {
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
    path: "test-results/phone-owner.png",
    fullPage: true,
  });
});
test("screenshots for desktop and phone", async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 1100 });
  await page.goto("/?preview=1&state=DIALING");
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: "test-results/desktop.png", fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.screenshot({ path: "test-results/phone.png", fullPage: true });
});
