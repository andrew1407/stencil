// The shots taken on the demo site's own tab: the page, its highlighted elements, and the quick
// crop and the editor the popup lays over it. Each in-page action closes the popup, as the real
// one does, so the step reopens it after reloading the site.
import { settle, waitForAnimations } from '../lib/waits.mjs';
import { TIMEOUTS, VIEWS, app, host, inClosedShadow, runner } from './session.mjs';
import { clickMenuItem, openFlyout, openRowMenu, popup, reopenPopup } from './popup.mjs';

// The modal's shell is a closed shadow root that document.getAnimations() never reaches: its frame
// is found by its page's URL, and ready once `ready` holds inside it and the shell has formed.
const shellFormed = () => inClosedShadow(host, 'stencil-ext-modal', function formed() {
  return this.getAnimations().every((a) => a.playState === 'finished' || a.playState === 'idle');
});
export const modalFrame = async (part, ready, timeout) => {
  const until = Date.now() + timeout;
  for (;;) {
    const frame = host.frames().find((f) => f !== host.mainFrame() && f.url().includes(part));
    if (frame && await frame.evaluate(ready).catch(() => false) && await shellFormed()) return frame;
    if (Date.now() > until) throw new Error(`no frame of ${part} became ready`);
    await settle(100);
  }
};
export const CROP_PAGE = '/src/crop/crop.html';

export const reloadSite = async (ctx, theme) => {
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
    const crop = await modalFrame(CROP_PAGE, () => document.getElementById('image')?.naturalWidth > 0, TIMEOUTS.modalMs);
    await waitForAnimations(crop);
    // The window stops at 760px tall, so its last controls — the open choice and the button —
    // are reached by scrolling it, as a user does.
    await crop.evaluate(() => {
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
    // Sized, unveiled, and its arrival dust (ui/motion/dust) gone: the picture is on screen.
    const editor = await modalFrame(app.url, () => {
      const c = document.getElementById('canvas');
      return !!c && c.width > 1 && c.height > 1 && getComputedStyle(c).opacity === '1'
        && !document.querySelector('.canvas-dust, .swap-dust');
    }, TIMEOUTS.editorMs);
    await waitForAnimations(editor);
    await editor.evaluate(() => document.querySelectorAll('.notify-toast').forEach((t) => t.remove()));
    await waitForAnimations(host);
    await runner.shot(host, 'site-editor-modal');
    await reloadSite(ctx, theme);
  } },
]);
