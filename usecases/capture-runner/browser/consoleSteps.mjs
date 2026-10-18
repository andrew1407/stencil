// The console shots under usecases/docs/browser/img: the console window beside the editor after
// /help, then CLI mode over the whole page after /status and /help. Taken on the shared page,
// after the still shots, so the editor already holds a blank picture to report on.
export function makeConsoleSteps({ runner, pages }) {
  const { shared, blank, openModal, closeModal } = pages;
  return [
    { name: 'console-window', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await openModal(page, 'console', 'console-overlay');
      await page.locator('#console-input').fill('/help');
      await page.locator('#console-input').press('Enter');
      await page.waitForTimeout(1500);
      await runner.shot(page, 'console-window');
      await closeModal(page, 'console-overlay');
    } },
    { name: 'cli-mode', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await page.evaluate(() => { window.stencil.EasterEggs.cliMode = true; });
      await page.locator('.cli-stage').waitFor({ timeout: 5000 });
      for (const line of ['/status', '/help']) {
        await page.locator('.cli-input').fill(line);
        await page.locator('.cli-input').press('Enter');
      }
      await page.waitForTimeout(2500);
      await runner.shot(page, 'cli-mode');
      await page.evaluate(() => { window.stencil.EasterEggs.cliMode = false; });
    } },
  ];
}
