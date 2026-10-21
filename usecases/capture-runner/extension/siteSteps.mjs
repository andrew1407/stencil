// The shots taken on the demo site's own tab: the page, its highlighted elements, and the quick
// crop and the editor the popup lays over it. Each in-page action closes the popup, as the real
// one does, so the step reopens it after reloading the site.
import { settle, waitForAnimations } from '../lib/waits.mjs';
import { TIMEOUTS, VIEWS, host, runner } from './session.mjs';
import { clickMenuItem, openFlyout, openRowMenu, popup, reopenPopup } from './popup.mjs';

const modalFrame = () => host.frameLocator('#stencil-ext-modal iframe, iframe');

const reloadSite = async (ctx, theme) => {
  await host.setViewportSize(VIEWS.site);
  await host.reload();
  await host.locator('figure img').first().waitFor();
  await host.waitForFunction(() => document.getElementById('clip')?.currentTime > 1, null, { timeout: TIMEOUTS.scanMs });
  await reopenPopup(ctx, theme);
};

export const makeSiteSteps = ({ image }) => Object.freeze([
  { name: 'site', run: () => runner.shot(host, 'site') },
  { name: 'site-highlight', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('f-highlight').click());
    await host.locator('#stencil-hl-style').waitFor({ state: 'attached', timeout: TIMEOUTS.scanMs });
    await waitForAnimations(host);
    await runner.shot(host, 'site-highlight');
    await ui.evaluate(() => document.getElementById('f-highlight').click());
    await ui.bringToFront();
  } },
  // Taller than the site shots, so the page shows above and below the window.
  { name: 'site-crop-modal', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await host.setViewportSize(VIEWS.cropModal);
    await host.bringToFront();
    await openRowMenu(ui, image);
    await clickMenuItem(ui, 'Crop');
    await modalFrame().locator('#image[src]').waitFor({ timeout: TIMEOUTS.modalMs });
    // The window stops at 760px tall, so its last controls — the open choice and the button —
    // are reached by scrolling it, as a user does.
    await modalFrame().locator('body').evaluate(() => {
      const doc = document.scrollingElement;
      doc.scrollTop = doc.scrollHeight;
    });
    await waitForAnimations(host);
    await runner.shot(host, 'site-crop-modal');
    await reloadSite(ctx, theme);
  } },
  // Open ▸ Here lays the editor over the page; the shot waits for the picture to be on its canvas.
  { name: 'site-editor-modal', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await host.bringToFront();
    await openRowMenu(ui, image);
    await openFlyout(ui, 'Open');
    await clickMenuItem(ui, 'Here', true);
    const canvas = modalFrame().locator('#canvas');
    await canvas.waitFor({ timeout: TIMEOUTS.editorMs });
    // Sized, unveiled, and its arrival dust (ui/motion/dust) gone: the picture is on screen.
    const painted = (c) => c.width > 1 && c.height > 1 && getComputedStyle(c).opacity === '1'
      && !c.ownerDocument.querySelector('.canvas-dust, .swap-dust');
    const until = Date.now() + TIMEOUTS.editorMs;
    while (!(await canvas.evaluate(painted))) {
      if (Date.now() > until) throw new Error('site-editor-modal: the picture never reached the canvas');
      await settle(100);
    }
    await canvas.evaluate((c) => c.ownerDocument.querySelectorAll('.notify-toast').forEach((t) => t.remove()));
    await waitForAnimations(host);
    await runner.shot(host, 'site-editor-modal');
    await reloadSite(ctx, theme);
  } },
]);
