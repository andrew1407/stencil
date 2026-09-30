// The "Make a copy" stills under usecases/docs/browser/img, taken on the stills' shared page:
// the copy menus and confirmation. Names take their theme from config/browser.json.
import { waitForAnimations } from '../lib/waits.mjs';

export function makeHandoffSteps({ config, runner, pages }) {
  const { shared, blank, drawLines, openModal, closeModal, expectModalOpen, settleModalAnimations } = pages;
  const handoff = config.get('handoff');
  const withLines = async (ctx, theme) => {
    const page = await shared(ctx, theme);
    await blank(page);
    await drawLines(page);
    return page;
  };
  const openCtxCopy = async (page) => {
    await page.locator('#canvas').click({ button: 'right', position: handoff.menuAt });
    await page.locator('#ctx-menu.ctx-open').waitFor();
    await page.locator('#ctx-copy-project-menu').hover();
    await page.locator('#ctx-copy-project-menu .ctx-sub.ctx-sub-visible').waitFor();
    await waitForAnimations(page, '#ctx-menu');
  };

  return Object.freeze([
    { name: 'copy-toolbar-menu', run: async (ctx, theme) => {
      const page = await withLines(ctx, theme);
      await page.locator('#controls-body #copy-project-btn').click();   // the fullscreen strip keeps a clone
      const menu = page.locator('body > .accent-dd-menu:has([data-copy-scope])');
      await menu.waitFor();
      await waitForAnimations(page, 'body > .accent-dd-menu');
      await runner.shot(page, 'copy-toolbar-menu');
      await page.keyboard.press('Escape');
      await menu.waitFor({ state: 'hidden' }).catch(() => {});
    } },
    { name: 'copy-ctx-menu', run: async (ctx, theme) => {
      const page = await withLines(ctx, theme);
      await openCtxCopy(page);
      await runner.shot(page, 'copy-ctx-menu');
      await page.keyboard.press('Escape');
    } },
    { name: 'copy-project-modal', run: async (ctx, theme) => {
      const page = await withLines(ctx, theme);
      await openCtxCopy(page);
      // The row's own click: a pointer path to the flyout would cross rows and fold it.
      await page.locator('#ctx-cp-layout').dispatchEvent('click');
      await expectModalOpen(page, 'copy-project-modal-overlay');
      await settleModalAnimations(page, 'copy-project-modal-overlay');
      await runner.shot(page, 'copy-project-modal');
      await page.locator('#copy-project-cancel').click();
      await waitForAnimations(page);
    } },
    { name: 'copy-row-menu', run: async (ctx, theme) => {
      const page = await withLines(ctx, theme);
      await openModal(page, 'projects', 'projects-modal-overlay');
      await page.locator('.project-row[data-id] .project-more').first().click();
      await page.locator('.project-menu-item.has-sub').hover();
      await page.locator('.project-submenu').waitFor();
      await waitForAnimations(page, '.project-submenu');
      await runner.shot(page, 'copy-row-menu');
      await page.keyboard.press('Escape');
      await closeModal(page, 'projects-modal-overlay');
    } },
  ]);
}
