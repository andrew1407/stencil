// The still shots under usecases/docs/browser/img, in the order they are taken. Names ending in
// -light / -dark are the documented pairs; every other name takes its theme from
// config/browser.json.
import { pairNames } from '../lib/theme/selector.mjs';
import { canvasSize, waitForAnimations, waitForCanvasChange } from '../lib/waits.mjs';
import { applyAppTheme } from '../lib/theme/page.mjs';

const DESCRIPTION = 'Kitchen layout for the renovation: the red line is the counter run, the blue dashed one the new wall.';

export function makeStillSteps({ config, runner, pages, stub, ollama, appUrl, browser }) {
  const { fresh, shared, blank, loadPicture, loadTrimmed, projectsPage, drawLines, openModal, closeModal, chatReplied,
    gotoApp, expectModalOpen, settleModalAnimations } = pages;

  const timeouts = config.get('timeouts');
  const pairStep = (base, run) => pairNames(base).map((name) => ({ name, run }));
  // What a window shows besides its frame: the open project's words, a configured assistant.
  const fill = {
    keywords: (page) => page.evaluate(() => { window.stencil.current.keywords = ['kitchen', 'floor plan', 'survey', 'draft', 'north wall']; }),
    description: (page) => page.evaluate((text) => { window.stencil.current.description = text; }, DESCRIPTION),
    'assistant-settings': (page) => page.evaluate(([baseUrl, model]) => {
      window.stencil.llm.setup({ provider: 'ollama', baseUrl, model });
    }, [ollama.url, config.get('ollama.model')]),
  };
  // `trigger` names a control to press instead of the facade (a modal whose contents are filled
  // by that control's own handler); `ready` an element that must have loaded; `url` a repo file
  // the window's URL tab previews; `image` a repo picture opened first.
  const modalStep = ({ name, key, overlay, trigger, ready, image, trimmed, url }) => ({
    name,
    run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      if (image) await (trimmed ? loadTrimmed(page, image) : loadPicture(page, image));
      await fill[key]?.(page);
      if (trigger) {
        await page.locator(trigger).click();
        await expectModalOpen(page, overlay);
        await settleModalAnimations(page, overlay);
      } else await openModal(page, key, overlay);
      if (url) await previewUrl(page, config.url(url), overlay);
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
  // The URL tab previews into its own media pair (mediaPairs.js); the crop row is what a landed
  // decode shows.
  const previewUrl = async (page, url, overlay) => {
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(url);
    await page.locator('#open-image-url-preview').click();
    await page.locator('#open-image-crop-row').waitFor({ timeout: timeouts.loadMs });
    await waitForAnimations(page, `#${overlay}`);
  };

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
    // Through the window, as a person does it: Crop stays off, so the whole picture opens.
    { name: 'open-from-url', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      const before = await canvasSize(page);
      await openModal(page, 'open-image', 'open-image-modal-overlay');
      await previewUrl(page, config.url('botIcon'), 'open-image-modal-overlay');
      await page.locator('#open-image-here').click();
      await waitForCanvasChange(page, before, timeouts.loadMs);
      await page.locator('#open-image-modal-overlay.modal-open').waitFor({ state: 'detached' }).catch(() => {});
      await page.evaluate(() => window.stencil.zoomFit());
      await waitForAnimations(page);
      await runner.shot(page, 'open-from-url');
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
    { name: 'script-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await openModal(page, 'script', 'script-overlay');
      await page.locator('#script-editor').fill(config.get('canvas.script').join('\n'));
      await waitForAnimations(page, '#script-overlay');
      await runner.shot(page, 'script-modal');
      await closeModal(page, 'script-overlay');
    } },
    { name: 'projects-modal', run: async (ctx, theme) => {
      const page = await projectsPage(ctx, theme);
      await openModal(page, 'projects', 'projects-modal-overlay');
      await page.locator('.project-row[data-id]').first().waitFor();
      await runner.shot(page, 'projects-modal');
      await closeModal(page, 'projects-modal-overlay');
    } },
    // The stub's plan frames the S of the repo picture and warms it; a real model answers only
    // when config.realLlm() says one is behind the server.
    { name: 'assistant', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await loadPicture(page);
      const real = config.realLlm();
      if (real) {
        await page.evaluate(async ([url, key]) => {
          await window.stencil.connect({ url, token: key });
          window.stencil.llm.setup({ provider: 'stencil-server', serverUrl: url });
        }, [config.serverUrl, config.serverToken()]);
      } else {
        await pages.seedLlm(page, `${stub.url}/v1`);
        stub.queue(config.stubPlan('sepiaOutline'));
      }
      await page.evaluate(() => window.stencil.chat.open());
      await pages.send(page, config.prompt(real ? 'real' : 'stub'));
      if (real) {
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
    { name: 'open-in-modal', run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await blank(page);
      await openModal(page, 'open-in', 'open-in-modal-overlay');
      await runner.shot(page, 'open-in-modal');
      await closeModal(page, 'open-in-modal-overlay');
    } },
    // A `#stencil=` fragment carrying a `.stc`, as VS Code's hand-off writes it: the app opens it
    // in the Script window, ready to run, and runs nothing on arrival.
    { name: 'script-link', run: async (ctx, theme) => {
      const script = `@source ${config.url('botIcon')}:\n    @filter sepia\n    @use line #1e63c8 3px dashed\n    @rect (15%, 15%) (85%, 85%)\n`;
      const hash = `#stencil=${encodeURIComponent(JSON.stringify({ script }))}`;
      const page = await browser.newPage({ viewport: config.get('viewports.app'), deviceScaleFactor: config.get('dpr') });
      await gotoApp(page, { hash, motion: 'none' });
      await applyAppTheme(page, theme);
      await page.locator('#script-overlay.modal-open').waitFor({ timeout: timeouts.loadMs });
      await waitForAnimations(page);
      await runner.shot(page, 'script-link');
      await page.close();
    } },
    // The desktop-app bounce page: what a chat link to the desktop app lands on. It forwards to
    // the stencil:// scheme at once, which never "loads" here.
    { name: 'launch-page', run: async (ctx, theme) => {
      const page = await browser.newPage({ viewport: config.get('viewports.launch'), colorScheme: theme, deviceScaleFactor: config.get('dpr') });
      const target = encodeURIComponent(`stencil://open?src=${config.url('favicon')}`);
      await page.goto(`${appUrl}launch.html#stencil-desktop=${target}`,
        { waitUntil: 'commit', timeout: 5000 }).catch(() => {});
      await page.locator('body').waitFor();
      await waitForAnimations(page);
      // The card and a margin of page around it: the rest of the viewport is empty backdrop.
      const box = await page.locator('main').boundingBox(), pad = 32;
      await runner.shot(page, 'launch-page', { clip: { x: Math.max(0, box.x - pad), y: Math.max(0, box.y - pad),
        width: box.width + 2 * pad, height: box.height + 2 * pad } });
      await page.close();
    } },
  ]);
}
