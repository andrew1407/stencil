// Captures usecases/docs/browser-extension/img/* from the unpacked MV3 extension, launched the
// way e2e/tests/browser-extension/ui-pins.spec.js does, against the demo site in ./site.
// Headed (extensions need it). Constants live in config/browserExtension.json.
//   node usecases/capture-runner/browserExtension.mjs [--only <name>]
import { chatOnlyPlan } from './lib/llmStub.mjs';
import { pairNames } from './lib/theme/selector.mjs';
import { applyShellTheme } from './lib/theme/page.mjs';
import { settle, waitForAnimations } from './lib/waits.mjs';
import { makeDropZoneSteps } from './extension/dropZoneSteps.mjs';
import {
  MIN_ROWS, TIMEOUTS, VIEWS, app, config, context, host, llmSettings, runner, site, stub,
} from './extension/session.mjs';
import {
  clickMenuItem, openFlyout, openInTab, openRowMenu, popup, popupClip, reopenPopup, surface,
} from './extension/popup.mjs';

// The options page's four cards, each named by a control only that card carries.
const OPTION_CARDS = Object.freeze([
  ['options-general', '#accent'],
  ['options-connections', '#conn-list'],
  ['options-assistant', '#llm-provider'],
  ['options-pins', '#pin-list'],
]);

const STEPS = Object.freeze([
  { name: 'site', run: () => runner.shot(host, 'site') },
  ...makeDropZoneSteps({ config, runner, host, applyShellTheme, timeouts: TIMEOUTS }),
  ...pairNames('popup-list').map((name) => ({
    name,
    run: async (ctx, theme) => {
      const ui = await popup(ctx, theme);
      await runner.shot(ui, name, await popupClip(ui));
    },
  })),
  { name: 'popup-row-menu', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await openRowMenu(ui);
    await openFlyout(ui);
    await runner.shot(ui, 'popup-row-menu', await popupClip(ui));
    await ui.keyboard.press('Escape');
  } },
  { name: 'options', run: async (ctx, theme) => {
    const options = await surface('src/options/options.html', VIEWS.options, theme);
    await options.waitForSelector('.card', { timeout: TIMEOUTS.scanMs });
    await waitForAnimations(options);
    // The element box IS the card, so the background the page centres it on never enters the frame.
    for (const [name, marker] of OPTION_CARDS) {
      const card = options.locator('.card').filter({ has: options.locator(marker) }).first();
      await card.scrollIntoViewIfNeeded();
      await waitForAnimations(options);
      await runner.shot(card, name);
    }
    await options.close();
  } },
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
  // The server's real model when a token is at hand, else the stub's canned answer.
  { name: 'popup-assistant', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    const token = config.serverToken();
    const settings = token
      ? { provider: 'stencil-server', baseUrl: '', model: '', apiKey: '', serverUrl: config.serverUrl, serverToken: token }
      : llmSettings(`${stub.url}/v1`);
    await ui.evaluate((value) => new Promise((done) => chrome.storage.local.set({ llmSettings: value }, done)), settings);
    if (!token) stub.queue(chatOnlyPlan(config.stubPlan('pageSurvey')));
    await ui.evaluate(() => document.querySelector('#sec-assistant .section-head .dlbl').click());
    await ui.locator('#sec-assistant:not(.collapsed)').waitFor();
    await ui.locator('#chat-input').fill(config.prompt('extension'));
    await ui.locator('#chat-send').click();
    await ui.locator('#chat-transcript .msg.assistant:not(.typing-row)').first().waitFor({ timeout: TIMEOUTS.replyMs });
    await ui.locator('#chat-transcript .typing-row').waitFor({ state: 'detached', timeout: TIMEOUTS.replyMs }).catch(() => {});
    await waitForAnimations(ui);
    await runner.shot(ui, 'popup-assistant', await popupClip(ui));
  } },
  { name: 'site-crop-modal', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await host.bringToFront();
    await openRowMenu(ui);
    await clickMenuItem(ui, 'Crop');
    const modal = host.frameLocator('#stencil-ext-modal iframe, iframe');
    await modal.locator('#image[src]').waitFor({ timeout: TIMEOUTS.modalMs }).catch(async () => {
      console.warn(`  crop modal status: ${await modal.locator('#status').textContent().catch(() => '?')}`);
    });
    await waitForAnimations(host);
    await runner.shot(host, 'site-crop-modal');
    await host.reload();
    await host.locator('figure img').first().waitFor();
    await reopenPopup(ctx, theme);
  } },
  // Open ▸ Here lays the editor over the page; the shot waits for the picture to be on its canvas.
  { name: 'site-editor-modal', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await host.bringToFront();
    await openRowMenu(ui);
    await openFlyout(ui);
    await clickMenuItem(ui, 'Here', true);
    const canvas = host.frameLocator('#stencil-ext-modal iframe, iframe').locator('#canvas');
    await canvas.waitFor({ timeout: TIMEOUTS.editorMs });
    // Sized, unveiled, and its arrival dust (ui/motion/dust) gone: the picture is on screen.
    const painted = (c) => c.width > 1 && c.height > 1 && getComputedStyle(c).opacity === '1'
      && !c.ownerDocument.querySelector('.canvas-dust, .swap-dust');
    const until = Date.now() + TIMEOUTS.editorMs;
    while (!(await canvas.evaluate(painted))) {
      if (Date.now() > until) throw new Error('site-editor-modal: the picture never reached the canvas');
      await settle(100);
    }
    await waitForAnimations(host);
    await runner.shot(host, 'site-editor-modal');
    await host.reload();
    await host.locator('figure img').first().waitFor();
    await reopenPopup(ctx, theme);
  } },
  { name: 'editor-tab', run: (ctx, theme) => openInTab(ctx, theme, 'In editor', 'editor-tab') },
  { name: 'editor-tab-incognito',
    run: (ctx, theme) => openInTab(ctx, theme, 'In editor (incognito)', 'editor-tab-incognito') },
  // Pin ▸ Locally marks the row and lists the image on the options page's Pinned images.
  { name: 'popup-pinned', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await openRowMenu(ui);
    await openFlyout(ui, 2);
    await clickMenuItem(ui, 'Locally', true);
    await ui.locator('.row .pin-btn.active').first().waitFor({ timeout: TIMEOUTS.scanMs });
    await waitForAnimations(ui);
    await runner.shot(ui, 'popup-pinned', await popupClip(ui));
  } },
  { name: 'side-panel', run: async (ctx, theme) => {
    const panel = await surface('src/sidepanel/sidepanel.html', VIEWS.panel, theme);
    await panel.waitForSelector('.filters', { timeout: TIMEOUTS.scanMs });
    await host.bringToFront();
    await panel.waitForFunction((min) => document.querySelectorAll('.row').length > min, MIN_ROWS, { timeout: TIMEOUTS.scanMs });
    await panel.bringToFront();
    await waitForAnimations(panel);
    await runner.shot(panel, 'side-panel');
    await panel.close();
  } },
  { name: 'crop-page', run: async (ctx, theme) => {
    const crop = await surface(`src/crop/crop.html?src=${encodeURIComponent(config.url('botIcon'))}`, VIEWS.crop, theme);
    await crop.locator('#image').waitFor();
    await crop.waitForFunction(() => document.getElementById('image')?.naturalWidth > 0, null, { timeout: TIMEOUTS.modalMs });
    await waitForAnimations(crop);
    await runner.shot(crop, 'crop-page');
    await crop.close();
  } },
]);

console.log('extension shots');
const ctx = await runner.play(STEPS, {});
if (ctx.ui && !ctx.ui.isClosed()) await ctx.ui.close();

await context.close();
await stub.close();
site.stop();
app.stop();
runner.finish();