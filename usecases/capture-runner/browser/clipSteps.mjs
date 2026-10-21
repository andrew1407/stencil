// The recorded clips under usecases/docs/browser/img. A clip is a real interaction filmed at
// speed, so its pauses are deliberate: they come from config/browser.json, not from
// guessing how long the app needs. Lines are drawn in colours the picture under them lacks.
import { recordGif } from '../lib/shot/shots.mjs';
import { settle, waitForAnimations } from '../lib/waits.mjs';

export function makeClipSteps({ config, runner, browser, pages }) {
  const clips = config.get('clips');
  const { gotoApp, settleModalAnimations, expectModalOpen } = pages;
  const start = async (page, theme) => {
    await page.emulateMedia({ colorScheme: theme });
    await gotoApp(page);
  };
  // The camera starts at the mark, so the page has to be still by then: gotoApp returns on the
  // facade, ahead of the first paint, and a mark right after it films the fade-in.
  const steady = async (page, look) => {
    await waitForAnimations(page);
    await settle(look.leadMs);
  };
  const clickAt = async (page, box, [fx, fy], look) => {
    await page.mouse.click(box.x + box.width * fx, box.y + box.height * fy);
    await settle(look.stepMs);
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
      await mark();
      await page.locator('#theme-toggle').click();
      await settle(look.holdMs);
    }),
    clip('create-blank', async (page, mark, look) => {
      await start(page, runner.themeOf('create-blank'));
      await page.evaluate(([color, thickness]) => window.stencil.apply({ lineColor: color, thickness }), [look.lineColor, look.thickness]);
      await steady(page, look);
      await mark();
      await page.locator('#create-blank-btn').click();
      await expectModalOpen(page, 'open-image-modal-overlay');
      await settleModalAnimations(page, 'open-image-modal-overlay');
      await page.locator('#blank-image-white').click();
      await settle(look.stepMs);
      await page.locator('#blank-image-create').click();
      await page.locator('#canvas').waitFor();
      await page.evaluate(() => window.stencil.startDrawing());
      const box = await page.locator('#canvas').boundingBox();
      for (const point of look.points) await clickAt(page, box, point, look);
      await page.evaluate(() => window.stencil.stopDrawing());
      // Off the page, so the coordinate tip does not cover the result.
      await page.mouse.move(...look.parkAt.map((f, i) => f * page.viewportSize()[i ? 'height' : 'width']), { steps: 8 });
      await settle(look.holdMs);
    }),
    // The standard edit, as the desktop's edit-draw films it: two lines clicked in point by
    // point, then a filter. Driven through the facade, so no toolbar tip rises over the picture.
    clip('edit-draw', async (page, mark, look) => {
      await start(page, runner.themeOf('edit-draw'));
      await pages.loadPicture(page, look.image);
      await page.evaluate((thickness) => { window.stencil.thickness = thickness; }, look.thickness);
      // Off the picture: a pointer resting on it raises the coordinate tip over the result.
      const park = (steps = 1) => page.mouse.move(...look.parkAt.map((f, i) => f * page.viewportSize()[i ? 'height' : 'width']), { steps });
      await park();
      await steady(page, look);
      await mark();
      const box = await page.locator('#canvas').boundingBox();
      for (const [i, line] of look.lines.entries()) {
        await page.evaluate((color) => { window.stencil.lineColor = color; window.stencil.startDrawing(); }, look.lineColors[i]);
        for (const point of line) {
          await page.mouse.move(box.x + box.width * point[0], box.y + box.height * point[1], { steps: look.moveSteps });
          await page.mouse.down();
          await page.mouse.up();
          await settle(look.stepMs);
        }
        await page.evaluate(() => window.stencil.stopDrawing());
        await settle(look.stepMs);
      }
      await park(look.moveSteps);
      await page.evaluate((filter) => window.stencil.apply({ filter }), look.filter);
      await settle(look.holdMs);
    }),
    // A rectangle swept with the Rect tool takes a fill from the selected-line bar; then a line
    // clicked back onto its first point closes into an area, picked in the Lines tab and filled.
    clip('draw-shapes', async (page, mark, look) => {
      await start(page, runner.themeOf('draw-shapes'));
      await pages.blank(page, look.page);
      await page.evaluate((color) => { window.stencil.lineColor = color; window.stencil.thickness = 4; }, look.lineColor);
      await steady(page, look);
      await mark();
      const box = await page.locator('#canvas').boundingBox();
      const at = ([fx, fy]) => [box.x + box.width * fx, box.y + box.height * fy];
      await page.locator('#draw-mode-toggle').click();
      await settle(look.stepMs);
      await page.mouse.move(...at(look.rect[0]));
      await page.mouse.down();
      await page.mouse.move(...at(look.rect[1]), { steps: look.dragSteps });
      await page.mouse.up();
      const fillSelected = async ([color, alpha]) => {
        await page.locator('#sel-fill-group').waitFor({ state: 'visible' });
        await page.locator('#sel-fill').fill(color);
        await page.locator('#sel-fill-alpha').fill(String(alpha));
        await page.locator('#sel-fill-alpha').press('Enter');
        await settle(look.stepMs);
        await page.locator('#sel-deselect').click();
      };
      await fillSelected(look.fills[0]);
      // Nothing selected now, so the clicks start a new line instead of extending the rectangle.
      await page.locator('#draw-mode-toggle').click();
      await page.evaluate(() => window.stencil.startDrawing());
      for (const point of [...look.area, look.area[0]]) await clickAt(page, box, point, look);
      await page.locator('#coord-tab-lines').click();
      await page.locator('#lines-list .lines-row').nth(1).locator('td').first().click();
      await fillSelected(look.fills[1]);
      await settle(look.holdMs);
    }),
    clip('assistant-turn', async (page, mark, look) => {
      await start(page, runner.themeOf('assistant-turn'));
      await pages.seedLlm(page, `${pages.stubUrl}/v1`);
      await pages.loadPicture(page);
      pages.queuePlan(config.stubPlan('sepiaOutline'));
      await steady(page, look);
      await mark();
      await page.evaluate(() => window.stencil.chat.open());
      await page.locator('#chat-input').waitFor();
      await page.locator('#chat-input').click();
      await page.keyboard.type(config.prompt('stub'), { delay: look.typeDelayMs });
      await page.keyboard.press('Enter');
      await pages.chatReplied(page);
      await settle(look.holdMs);
    }),
    // Typed, run, and the window closed on its result: the cropped, warmed picture with the frame.
    clip('script-run', async (page, mark, look) => {
      await start(page, runner.themeOf('script-run'));
      await pages.loadPicture(page);
      await steady(page, look);
      await mark();
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
      await mark();
      await page.mouse.move(view.width / 2, look.edgeY, { steps: look.moveSteps });
      const strip = page.locator('#fs-controls-panel.fs-panel-visible');
      await strip.waitFor();
      await settle(look.holdMs);
      await page.mouse.move(away.x, away.y, { steps: look.moveSteps });
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
      // A phone shows no arrow: only the fingertip once the touch starts.
      await page.evaluate(() => window.__docsPointer?.hide());
      await mark();
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
