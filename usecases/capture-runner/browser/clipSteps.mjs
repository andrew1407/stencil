// The recorded clips under usecases/docs/browser/img. A clip is a real interaction filmed at
// speed, so its pauses are deliberate: they come from config/browser.json, not from
// guessing how long the app needs.
import { recordGif } from '../lib/shot/shots.mjs';
import { settle, waitForAnimations } from '../lib/waits.mjs';

export function makeClipSteps({ config, runner, browser, pages }) {
  const clips = config.get('clips');
  const { gotoApp, settleModalAnimations, expectModalOpen } = pages;
  const start = async (page, theme) => {
    await page.emulateMedia({ colorScheme: theme });
    await gotoApp(page);
  };
  // The recording rewinds a little before the mark, so the page has to be still by then: gotoApp
  // returns on the facade, ahead of the first paint, and a mark right after it films the fade-in.
  const steady = async (page, look) => {
    await waitForAnimations(page);
    await settle(look.leadMs);
  };
  const clip = (name, drive) => ({
    name,
    run: () => recordGif(browser, runner.out, name,
      (page, mark) => drive(page, mark, clips[name]), { ...config.gifLook, ...clips[name] }),
  });

  return Object.freeze([
    clip('theme-swap', async (page, mark, look) => {
      await start(page, look.startTheme);
      await pages.blank(page);
      await pages.drawLines(page);
      await steady(page, look);
      mark();
      await page.locator('#theme-toggle').click();
      await settle(look.holdMs);
    }),
    clip('create-blank', async (page, mark, look) => {
      await start(page, runner.themeOf('create-blank'));
      await steady(page, look);
      mark();
      await page.locator('#create-blank-btn').click();
      await expectModalOpen(page, 'open-image-modal-overlay');
      await settleModalAnimations(page, 'open-image-modal-overlay');
      await page.locator('#blank-image-black').click();
      await page.locator('#blank-image-create').click();
      await page.locator('#canvas').waitFor();
      await page.evaluate(() => window.stencil.startDrawing());
      const box = await page.locator('#canvas').boundingBox();
      for (const [fx, fy] of look.points) {
        await page.mouse.click(box.x + box.width * fx, box.y + box.height * fy);
        await settle(look.stepMs);
      }
      await page.evaluate(() => window.stencil.stopDrawing());
      await settle(look.holdMs);
    }),
    clip('assistant-turn', async (page, mark, look) => {
      await start(page, runner.themeOf('assistant-turn'));
      await pages.seedLlm(page, `${pages.stubUrl}/v1`);
      await pages.blank(page);
      pages.queuePlan(config.stubPlan('sepiaOutline'));
      await steady(page, look);
      mark();
      await page.evaluate(() => window.stencil.chat.open());
      await page.locator('#chat-input').waitFor();
      await page.locator('#chat-input').click();
      await page.keyboard.type(config.prompt('stub'), { delay: look.typeDelayMs });
      await page.keyboard.press('Enter');
      await pages.chatReplied(page);
      await settle(look.holdMs);
    }),
    // Typed, run, and the window closed on its result: the cropped, warmed page with the frame.
    clip('script-run', async (page, mark, look) => {
      await start(page, runner.themeOf('script-run'));
      await pages.blank(page);
      await steady(page, look);
      mark();
      await page.evaluate(() => window.stencil.openWindow('script'));
      await settleModalAnimations(page, 'script-overlay');
      await page.locator('#script-editor').click();
      await page.keyboard.type(config.get('canvas.script').join('\n'), { delay: look.typeDelayMs });
      await page.locator('#script-run').click();
      await page.locator('.notify-toast').filter({ hasText: /executed/ }).waitFor();
      await settle(look.ranMs);
      await page.evaluate(() => window.stencil.closeWindow());
      await page.locator('#script-overlay.modal-open').waitFor({ state: 'detached' });
      await settle(look.holdMs);
    }),
    // Fullscreen: the toolbar is gone until the pointer touches the top edge, and leaves again
    // once the pointer is back down. It rests beside the picture, where no line raises its tip.
    clip('fullscreen-strip', async (page, mark, look) => {
      await start(page, runner.themeOf('fullscreen-strip'));
      await pages.blank(page);
      await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), config.get('canvas.lines'));
      await page.evaluate(() => { window.stencil.fullscreen = true; });
      const view = page.viewportSize();
      const away = { x: view.width * look.parkAt[0], y: view.height * look.parkAt[1] };
      await page.mouse.move(away.x, away.y);
      await steady(page, look);
      mark();
      await page.mouse.move(view.width / 2, look.edgeY);
      const strip = page.locator('#fs-controls-panel.fs-panel-visible');
      await strip.waitFor();
      await settle(look.holdMs);
      await page.mouse.move(away.x, away.y);
      await strip.waitFor({ state: 'detached' });
      await settle(look.holdMs);
    }),
    // A phone: one finger on empty canvas drags the zoomed picture. Touch is emulated so the
    // app takes its touch path; the finger is CDP's, as e2e's touch-pan spec drives it.
    clip('touch-pan', async (page, mark, look) => {
      await start(page, runner.themeOf('touch-pan'));
      const cdp = await page.context().newCDPSession(page);
      await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 1 });
      await pages.blank(page, look.fill);
      await page.evaluate(([lines, zoom]) => {
        window.stencil.setLines(lines, { history: false });
        window.stencil.zoom(zoom);
      }, [config.get('canvas.lines'), look.zoom]);
      // Folded, as a phone user keeps them: the canvas then fills the screen under the header.
      await page.locator('#toggle-controls').click();
      const viewport = page.locator('#canvas-viewport');
      await viewport.scrollIntoViewIfNeeded();
      await settle(look.settleMs);
      const box = await viewport.boundingBox();
      let [x, y] = [box.x + box.width / 2, box.y + box.height / 2];
      const f = await pages.finger(page);
      mark();
      await f.down(x, y);
      for (const [dx, dy] of look.glide) {
        await f.glide(x, y, x + dx, y + dy, look.steps);
        [x, y] = [x + dx, y + dy];
      }
      await f.up(x, y);
      await settle(look.holdMs);
    }),
  ]);
}
