// The still shots under usecases/docs/browser/img, in the order they are taken. Names ending in
// -light / -dark are the documented pairs; every other name takes its theme from
// config/browser.json.
import { pairNames } from '../lib/themeSelector.mjs';
import { canvasSize, waitForAnimations, waitForCanvasChange } from '../lib/waits.mjs';
import { applyAppTheme } from '../lib/pageTheme.mjs';

export function makeStillSteps({ config, runner, pages, stub, appUrl, browser }) {
  const { fresh, shared, blank, drawLines, openModal, closeModal, chatReplied,
    gotoApp, expectModalOpen, settleModalAnimations } = pages;

  const timeouts = config.get('timeouts');
  const pairStep = (base, run) => pairNames(base).map((name) => ({ name, run }));
  // A blank page carrying one of the repo's own pictures: what the image-shaped modals show.
  const withImage = async (page, key) => {
    await blank(page);
    const before = await canvasSize(page);
    await page.evaluate((url) => window.stencil.load(url), config.url(key));
    await waitForCanvasChange(page, before, timeouts.loadMs);
  };
  // `trigger` names a control to press instead of the facade (a modal whose contents are
  // filled by that control's own handler); `ready` an element that must have loaded.
  const modalStep = ({ name, key, overlay, trigger, ready, image }) => ({
    name,
    run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      if (image) await withImage(page, image);
      if (trigger) {
        await page.locator(trigger).click();
        await expectModalOpen(page, overlay);
        await settleModalAnimations(page, overlay);
      } else await openModal(page, key, overlay);
      if (ready) {
        await page.waitForFunction((sel) => {
          const el = document.querySelector(sel);
          return !!el && (!('naturalWidth' in el) || el.naturalWidth > 0);
        }, ready, { timeout: timeouts.loadMs });
      }
      await runner.shot(page, name);
      await closeModal(page, overlay);
    },
  });

  return Object.freeze([
    ...pairStep('editor-empty', async (ctx, theme, name) => {
      const page = await fresh(theme);
      await runner.shot(page, name);
      await page.close();
    }),
    ...pairStep('lines-selection', async (ctx, theme, name) => {
      const page = await fresh(theme);
      await blank(page);
      await drawLines(page);
      await runner.shot(page, name);
      await page.close();
    }),
    { name: 'blank-white', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await runner.shot(page, 'blank-white');
    } },
    { name: 'blank-black', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page, '#000000');
      await runner.shot(page, 'blank-black');
    } },
    { name: 'open-from-url', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      const before = await canvasSize(page);
      try {
        await page.evaluate((url) => window.stencil.load(url), config.url('favicon'));
        await waitForCanvasChange(page, before, timeouts.loadMs);
        await runner.shot(page, 'open-from-url');
      } catch (err) { console.warn(`  open-from-url skipped: ${err.message.split('\n')[0]}`); }
    } },
    { name: 'context-menu', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await page.locator('#canvas').click({ button: 'right', position: { x: 60, y: 60 } });
      await page.locator('#ctx-menu.ctx-open').waitFor();
      await waitForAnimations(page, '#ctx-menu');
      await runner.shot(page, 'context-menu');
      await page.keyboard.press('Escape');
    } },
    ...config.get('modals').map(modalStep),
    { name: 'shortcuts-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openModal(page, 'shortcuts', 'settings-modal-overlay');
      await page.locator('#hotkey-search').fill('zoom');
      await page.waitForFunction(() => [...document.querySelectorAll('.hotkey-row')]
        .some((row) => row.style.display === 'none'));
      await runner.shot(page, 'shortcuts-modal');
      await closeModal(page, 'settings-modal-overlay');
    } },
    { name: 'visuals-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openModal(page, 'visuals', 'visuals-modal-overlay');
      await runner.shot(page, 'visuals-modal');
      await closeModal(page, 'visuals-modal-overlay');
    } },
    { name: 'accent-picker', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openModal(page, 'visuals', 'visuals-modal-overlay');
      await page.locator('#vs-accent .accent-dd-trigger').click();
      await page.locator('body > .accent-dd-menu').waitFor();
      await waitForAnimations(page, 'body > .accent-dd-menu');
      await runner.shot(page, 'accent-picker');
      await page.keyboard.press('Escape');
      await closeModal(page, 'visuals-modal-overlay');
    } },
    { name: 'script-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openModal(page, 'script', 'script-overlay');
      await page.locator('#script-editor').fill(config.get('canvas.script').join('\n'));
      await waitForAnimations(page, '#script-overlay');
      await runner.shot(page, 'script-modal');
      await closeModal(page, 'script-overlay');
    } },
    { name: 'projects-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await page.evaluate(async (size) => {
        window.stencil.newEditor();
        await window.stencil.blank('#000000', { size });
      }, config.get('canvas.blankSize'));
      await openModal(page, 'projects', 'projects-modal-overlay');
      await page.locator('.project-row[data-id]').first().waitFor();
      await runner.shot(page, 'projects-modal');
      await closeModal(page, 'projects-modal-overlay');
    } },
    // Docked and floating, out of one turn: a real model through the collaboration server's
    // proxy when a token is at hand (the working image rides every turn, so the reply is
    // about the picture); else the stub's canned plan.
    { name: 'assistant', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      const token = config.serverToken();
      if (token) {
        const before = await canvasSize(page);
        await page.evaluate((url) => window.stencil.load(url), config.url('favicon'));
        await waitForCanvasChange(page, before, timeouts.loadMs).catch(() => blank(page));
        await page.evaluate(async ([url, key]) => {
          await window.stencil.connect({ url, token: key });
          window.stencil.llm.setup({ provider: 'stencil-server', serverUrl: url });
        }, [config.serverUrl, token]);
      } else {
        await blank(page);
        await pages.seedLlm(page, `${stub.url}/v1`);
        stub.queue(config.stubPlan('sepiaOutline'));
      }
      await page.evaluate(() => window.stencil.chat.open());
      await pages.send(page, config.prompt(token ? 'real' : 'stub'));
      if (token) {
        await page.waitForFunction(() => !window.stencil.chat.isSending
          && window.stencil.chat.history.some((m) => m.role === 'assistant'),
        null, { timeout: timeouts.realReplyMs });
      } else await chatReplied(page);
      await page.evaluate(() => window.stencil.zoomFit());
      await waitForAnimations(page);
      await runner.shot(page, 'assistant-docked');
      await page.evaluate(() => window.stencil.chat.dock('float'));
      await waitForAnimations(page, '#chat-panel');
      await runner.shot(page, 'assistant-floating');
      await page.evaluate(() => window.stencil.chat.close());
    } },
    { name: 'fullscreen-strip', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await page.evaluate(() => { window.stencil.fullscreen = true; });
      await page.mouse.move(400, 2);
      const panel = page.locator('#fs-controls-panel.fs-panel-visible');
      await panel.waitFor();
      const box = await panel.boundingBox();
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
      await waitForAnimations(page, '#fs-controls-panel');
      await runner.shot(page, 'fullscreen-strip');
      await page.evaluate(() => { window.stencil.fullscreen = false; });
    } },
    { name: 'open-in-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      try {
        await openModal(page, 'open-in', 'open-in-modal-overlay');
        await runner.shot(page, 'open-in-modal');
        await closeModal(page, 'open-in-modal-overlay');
      } catch (err) { console.warn(`  open-in-modal skipped: ${err.message.split('\n')[0]}`); }
    } },
    // A script handed over by the VS Code extension: the same `#stencil=` fragment the Chrome
    // extension writes, carrying a `.stc` beside the picture. The app runs it on arrival and
    // keeps the source in its script window — which is what this shot is of.
    { name: 'script-handoff', run: async (ctx, theme) => {
      const script = `@source ${config.url('botIcon')}:\n    @filter sepia\n    @use line #1e63c8 3px dashed\n    @rect (15%, 15%) (85%, 85%)\n`;
      const hash = `#stencil=${encodeURIComponent(JSON.stringify({ script }))}`;
      const page = await browser.newPage({ viewport: config.get('viewports.app') });
      await gotoApp(page, { hash, motion: 'none' });
      await applyAppTheme(page, theme);
      await page.waitForFunction(() => window.stencil.lines.length > 0, null, { timeout: timeouts.loadMs });
      await waitForAnimations(page);
      await runner.shot(page, 'script-handoff');
      await page.close();
    } },
    // The desktop-app bounce page: what a chat link to the desktop app lands on. It
    // forwards to the stencil:// scheme at once, which never "loads" here.
    ...pairStep('launch-page', async (ctx, theme, name) => {
      const page = await browser.newPage({ viewport: config.get('viewports.launch'), colorScheme: theme });
      const target = encodeURIComponent(`stencil://open?src=${config.url('favicon')}`);
      await page.goto(`${appUrl}launch.html#stencil-desktop=${target}`,
        { waitUntil: 'commit', timeout: 5000 }).catch(() => {});
      await page.locator('body').waitFor();
      await waitForAnimations(page);
      await runner.shot(page, name);
      await page.close();
    }),
  ]);
}
