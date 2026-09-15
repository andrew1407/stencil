// The recorded clips under usecases/docs/browser/img. A clip is a real interaction filmed at
// speed, so its pauses are deliberate: they come from config/browser.json, not from
// guessing how long the app needs.
import { recordGif } from '../lib/shots.mjs';
import { settle } from '../lib/waits.mjs';

export function makeClipSteps({ config, runner, browser, pages }) {
  const clips = config.get('clips');
  const { gotoApp, settleModalAnimations, expectModalOpen } = pages;
  const start = async (page, theme) => {
    await page.emulateMedia({ colorScheme: theme });
    await gotoApp(page);
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
      mark();
      await page.locator('#theme-toggle').click();
      await settle(look.holdMs);
    }),
    clip('create-blank', async (page, mark, look) => {
      await start(page, runner.themeOf('create-blank'));
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
      await page.evaluate(() => window.stencil.chat.open().chat.dock('right'));
      await page.locator('#chat-input').waitFor();
      mark();
      await page.locator('#chat-input').click();
      await page.keyboard.type(config.prompt('stub'), { delay: look.typeDelayMs });
      await page.keyboard.press('Enter');
      await pages.chatReplied(page);
      await settle(look.holdMs);
    }),
    clip('script-run', async (page, mark, look) => {
      await start(page, runner.themeOf('script-run'));
      await pages.blank(page);
      mark();
      await page.evaluate(() => window.stencil.openWindow('script'));
      await settleModalAnimations(page, 'script-overlay');
      await page.locator('#script-editor').click();
      await page.keyboard.type(config.get('canvas.script').join('\n'), { delay: look.typeDelayMs });
      await page.locator('#script-run').click();
      await settle(look.holdMs);
    }),
  ]);
}
