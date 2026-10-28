import { test, expect } from "../fixtures";
for (const width of [1280, 390]) {
  test(`production bundle renders and supports the owner dialog at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 850 });
    const errors: string[] = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.goto("/?preview=1&state=LOCKED");
    await expect(page.getByRole("img", { name: "The vault is locked" })).toBeVisible();
    await expect(page.getByText("DESIGN PREVIEW · SAMPLE DATA · NO DEVICE COMMANDS")).toBeVisible();
    await page.getByRole("button", { name: "Owner access" }).click();
    await expect(page.getByRole("dialog")).toBeVisible();
    await page.keyboard.press("Escape");
    await expect(page.getByRole("dialog")).toHaveCount(0);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    expect(errors).toEqual([]);
  });
}
