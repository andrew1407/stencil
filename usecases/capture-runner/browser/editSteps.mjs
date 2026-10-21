// The editing stills under usecases/docs/browser/img: the logo's accent list, the filter list
// previewing a filter, a split compare, the page sizes, the export choices and the canvas
// menu's script editor. Each runs on the shared page over the repo picture with the shots' lines.
import { settle, waitForAnimations } from '../lib/waits.mjs';

// The enhanced <select>'s face (ui/control/customSelectFace.js) and the list it opens.
const OPEN_LIST = 'body > .accent-dd-menu:not([hidden])';
const trigger = (selectId) => `.cs-dd:has(#${selectId}) .accent-dd-trigger`;

export function makeEditSteps({ config, runner, pages }) {
  const { shared, loadPicture } = pages;
  const lines = config.get('pictureLines');

  // The picture with the shots' two lines, unfiltered, at fit: what each still starts from.
  const picture = async (ctx, theme) => {
    const page = await shared(ctx, theme);
    await loadPicture(page);
    await page.evaluate((ls) => {
      window.stencil.apply({ filter: 'none', compareMode: 'none' });
      window.stencil.setLines(ls, { history: false });
      window.stencil.zoomFit();
    }, lines);
    await waitForAnimations(page);
    return page;
  };
  const openList = async (page, selectId) => {
    await page.locator(trigger(selectId)).click();
    await page.locator(OPEN_LIST).waitFor();
    await waitForAnimations(page, OPEN_LIST);
  };
  const close = (page) => page.keyboard.press('Escape');

  return Object.freeze([
    // Right-click on the header logo: the accent list it keeps, next to the logo it recolours.
    { name: 'accent-picker', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await page.locator('.app-logo-wrap').click({ button: 'right' });
      const menu = page.locator('.logo-accent-menu');
      await menu.waitFor({ state: 'visible' });
      await waitForAnimations(page, '.app-logo-wrap');
      const box = await menu.boundingBox();
      await runner.shot(page, 'accent-picker', { clip: { x: 0, y: 0, width: Math.ceil(box.x + box.width + 24), height: Math.ceil(box.y + box.height + 24) } });
      await close(page);
      await menu.waitFor({ state: 'hidden' }).catch(() => {});
    } },
    // Resting on a row previews it on the canvas before it is picked.
    { name: 'filter-menu', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await openList(page, 'image-filter');
      await page.locator(`${OPEN_LIST} .accent-dd-opt[data-value="sepia"]`).hover();
      await settle(config.get('timeouts.previewMs'));
      await runner.shot(page, 'filter-menu');
      await close(page);
    } },
    { name: 'compare-split', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await page.evaluate(() => window.stencil.apply({ filter: 'sepia', compareMode: 'vertical', compareSplit: 0.5 }));
      await waitForAnimations(page);
      await runner.shot(page, 'compare-split');
      await page.evaluate(() => window.stencil.apply({ filter: 'none', compareMode: 'none' }));
    } },
    { name: 'page-size', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await openList(page, 'page-size');
      await runner.shot(page, 'page-size');
      await close(page);
    } },
    // With a filter, lines and a split compare up, every export choice is on offer.
    { name: 'export-menu', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await page.evaluate(() => window.stencil.apply({ filter: 'sepia', compareMode: 'vertical', compareSplit: 0.5 }));
      await page.locator('#save-image').click({ button: 'right' });
      await page.locator(OPEN_LIST).waitFor();
      await waitForAnimations(page, OPEN_LIST);
      await runner.shot(page, 'export-menu');
      await close(page);
      await page.evaluate(() => window.stencil.apply({ filter: 'none', compareMode: 'none' }));
    } },
    { name: 'context-menu-script', run: async (ctx, theme) => {
      const page = await picture(ctx, theme);
      await page.locator('#canvas').click({ button: 'right', position: config.get('handoff.menuAt') });
      await page.locator('#ctx-menu.ctx-open').waitFor();
      await page.locator('#ctx-script').hover();
      await page.locator('#ctx-script .ctx-sub.ctx-sub-visible').waitFor();
      await waitForAnimations(page, '#ctx-menu');
      await runner.shot(page, 'context-menu-script');
      await close(page);
    } },
  ]);
}
