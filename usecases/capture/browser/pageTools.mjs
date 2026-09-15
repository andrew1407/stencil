// The browser app as the capture drives it: a fresh page in a given theme, the shared page
// the long middle of the run reuses, and the canvas / modal gestures. Everything goes
// through window.stencil, the same facade the e2e specs use.
import { e2e } from '../lib/playwright.mjs';
import { applyAppTheme } from '../lib/pageTheme.mjs';
import { waitForAnimations } from '../lib/waits.mjs';

export const { gotoApp, settleModalAnimations, expectModalOpen } = await e2e('helpers/boot.js');
export const { freezeMotion } = await e2e('helpers/uiPin.js');
export const { seedLlmSettings, sendChat } = await e2e('helpers/chat.js');

export function makeBrowserPages({ config, browser }) {
  const view = config.get('viewports.app');
  const blankSize = config.get('canvas.blankSize');

  const fresh = async (theme) => {
    const page = await browser.newPage({ viewport: view });
    await gotoApp(page);
    await freezeMotion(page);
    await applyAppTheme(page, theme);
    return page;
  };

  // One page for the whole still sequence; each step states the theme it needs.
  const shared = async (ctx, theme) => {
    ctx.page ??= await fresh(theme);
    await applyAppTheme(ctx.page, theme);
    return ctx.page;
  };

  const blank = (page, color = '#ffffff') =>
    page.evaluate(([fill, size]) => window.stencil.blank(fill, { size }), [color, blankSize]);

  const drawLines = async (page) => {
    await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), config.get('canvas.lines'));
    await page.locator('#coord-tab-lines').click();
    await page.locator('#lines-list .lines-row').first().click();
    await page.locator('#selection-panel').waitFor();
    await waitForAnimations(page);
  };

  const openModal = async (page, key, overlay) => {
    await page.evaluate((k) => window.stencil.openWindow(k), key);
    await expectModalOpen(page, overlay);
    await settleModalAnimations(page, overlay);
  };

  const closeModal = async (page, overlay) => {
    await page.evaluate(() => window.stencil.closeWindow());
    await page.locator(`#${overlay}.modal-open`).waitFor({ state: 'detached', timeout: 5000 }).catch(() => {});
    await waitForAnimations(page);
  };

  // The stub's plan always mentions sepia; a real model's reply is awaited on the facade.
  const chatReplied = (page) => page.locator('#chat-transcript .chat-msg-assistant')
    .filter({ hasText: /sepia/ }).first().waitFor({ timeout: config.get('timeouts.stubReplyMs') });

  return { fresh, shared, blank, drawLines, openModal, closeModal, chatReplied,
    seedLlm: seedLlmSettings, send: sendChat, gotoApp, settleModalAnimations, expectModalOpen };
}
