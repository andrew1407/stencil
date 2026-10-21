// The console shot under usecases/docs/browser/img: the console window beside the editor after
// /help. Taken on the shared page, after the still shots, so the editor holds a picture to
// report on.
import { waitForStable } from '../lib/waits.mjs';

export function makeConsoleSteps({ runner, pages }) {
  const { shared, loadPicture, openModal, closeModal } = pages;
  return [
    { name: 'console-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await loadPicture(page);
      await openModal(page, 'console', 'console-overlay');
      await page.locator('#console-input').fill('/help');
      await page.locator('#console-input').press('Enter');
      // The answer types itself in at the console text speed; the shot waits until it stops.
      await waitForStable(page, () => document.getElementById('console-overlay')?.innerText ?? '', { idleMs: 600 });
      await runner.shot(page, 'console-modal');
      await closeModal(page, 'console-overlay');
    } },
  ];
}
