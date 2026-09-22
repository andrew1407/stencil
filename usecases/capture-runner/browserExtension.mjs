// Captures usecases/docs/browser-extension/img/* from the unpacked MV3 extension, launched the
// way e2e/tests/browser-extension/ui-pins.spec.js does, against the demo site in ./site.
// Headed (extensions need it). Constants live in config/browserExtension.json.
//   node usecases/capture-runner/browserExtension.mjs [--only <name>]
import path from 'node:path';
import { e2e } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, scratchDir } from './lib/paths.mjs';
import { startAppServer, startSiteServer } from './lib/servers.mjs';
import { startLlmStub, chatOnlyPlan } from './lib/llmStub.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { pairNames } from './lib/themeSelector.mjs';
import { applyAppTheme, applyShellTheme } from './lib/pageTheme.mjs';
import { film } from './lib/shots.mjs';
import { framesToGif } from './lib/gifTools.mjs';
import { waitForAnimations } from './lib/waits.mjs';
import { makeDropZoneSteps } from './extension/dropZoneSteps.mjs';

const config = loadCaptureConfig('browserExtension');
const runner = makeShotRunner({ config, out: outDir('browser-extension') });
const VIEWS = config.get('viewports');
const TIMEOUTS = config.get('timeouts');
const MIN_ROWS = config.get('minRows');

const { launchExtension } = await e2e('helpers/extension.js');
const { freezeMotion } = await e2e('helpers/uiPin.js');
const { llmSettings } = await e2e('helpers/chat.js');

const app = await startAppServer();
const site = await startSiteServer(config.get('sitePort'));
const stub = await startLlmStub();
const { context, background, extId } = await launchExtension();
const sw = await background();
await sw.evaluate((editorUrl) => new Promise((done) => chrome.storage.sync.set({ editorUrl }, done)), app.url);

const host = await context.newPage();
await host.setViewportSize(VIEWS.site);
await host.goto(site.url);
await host.locator('figure img').first().waitFor();

// A real popup box stops at 600px and scrolls its list; the picture is of what the popup HOLDS,
// so the capture lets it stand at its full height rather than cutting a row in half.
const POPUP_UNCAPPED = `html, body { max-height: none !important; overflow: visible !important; }
  .list { overflow: visible !important; max-height: none !important; }`;

// The shot is the popup and nothing else: the clip ends at its last pixel, so none of the
// viewport left under it rides along.
const popupClip = async (ui) => {
  await waitForAnimations(ui);
  const height = await ui.evaluate(() => {
    const ends = [document.body.getBoundingClientRect().bottom];
    for (const over of document.querySelectorAll('#action-menu, #action-menu .flyout')) {
      const box = over.getBoundingClientRect();
      if (box.height > 0) ends.push(box.bottom);
    }
    return Math.ceil(Math.max(...ends));
  });
  return { clip: { x: 0, y: 0, width: VIEWS.popup.width, height } };
};

const surface = async (rel, viewport, theme) => {
  const page = await context.newPage();
  await page.setViewportSize(viewport);
  await page.goto(`chrome-extension://${extId}/${rel}`);
  await freezeMotion(page);
  await applyShellTheme(page, theme);
  return page;
};
// The popup scans the ACTIVE tab: bring the site forward, rescan, then bring the popup back.
const scanSite = async (ui) => {
  await host.bringToFront();
  await ui.evaluate(() => document.getElementById('rescan').click());
  await ui.waitForFunction((min) => document.querySelectorAll('.row').length > min, MIN_ROWS, { timeout: TIMEOUTS.scanMs });
  await ui.bringToFront();
  // The scan's status line leaves on a 200ms timer while freezeMotion has already faded it to
  // nothing, so an unwaited shot keeps its empty band above the first row.
  await ui.waitForFunction(() => !document.getElementById('status').textContent, null, { timeout: TIMEOUTS.scanMs });
  await waitForAnimations(ui);
};
const openPopup = async (theme) => {
  const ui = await surface('src/popup/popup.html', VIEWS.popup, theme);
  await ui.waitForSelector('.filters', { timeout: TIMEOUTS.scanMs });
  await ui.addStyleTag({ content: POPUP_UNCAPPED });
  await scanSite(ui);
  return ui;
};
// A popup closes itself after an in-page action (crop, open here), as the real one does.
const popup = async (ctx, theme) => {
  ctx.ui ??= await openPopup(theme);
  if (!ctx.ui.isClosed()) await applyShellTheme(ctx.ui, theme);
  return ctx.ui;
};
const reopenPopup = async (ctx, theme) => {
  ctx.ui = await openPopup(theme);
  return ctx.ui;
};
const openRowMenu = async (ui, nth = 1) => {
  await ui.locator('.row .more-btn').nth(nth).click();
  await ui.locator('#action-menu').waitFor();
  await waitForAnimations(ui, '#action-menu');
};
const openFlyout = async (ui, nth = 0) => {
  await ui.locator('#action-menu > .submenu').nth(nth).hover();
  await ui.locator('#action-menu .flyout').nth(nth).waitFor();
  await waitForAnimations(ui, '#action-menu');
};
const clickMenuItem = (ui, text, inFlyout = false) => ui.evaluate(([label, flyout]) => {
  const scope = flyout ? '#action-menu .flyout button' : '#action-menu button';
  [...document.querySelectorAll(scope)].find((btn) => btn.textContent.trim() === label).click();
}, [text, inFlyout]);

// The hand-off routes: the row menu's Open ▸ "In editor" / "In editor (incognito)" open a
// new editor tab carrying the image.
const openInTab = async (ctx, theme, label, name) => {
  const ui = await popup(ctx, theme);
  await host.bringToFront();
  await openRowMenu(ui);
  await openFlyout(ui);
  const [tab] = await Promise.all([context.waitForEvent('page'), clickMenuItem(ui, label, true)]);
  await tab.setViewportSize(VIEWS.site);
  await tab.waitForFunction(() => document.getElementById('canvas')?.width > 0, null, { timeout: TIMEOUTS.editorMs });
  await freezeMotion(tab);
  await applyAppTheme(tab, theme);
  await runner.shot(tab, name);
  await tab.close();
  await reopenPopup(ctx, theme);
};

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
  { name: 'open-here', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    const clip = config.get('clips.open-here');
    await host.bringToFront();
    await openRowMenu(ui);
    const frames = scratchDir('ext-open-here');
    const filming = film(host, frames, clip.ms, clip.everyMs);
    await openFlyout(ui);
    await clickMenuItem(ui, 'Here', true);
    const shot = await filming;
    framesToGif(frames, path.join(runner.out, 'open-here.gif'), { ...config.gifLook, inFps: shot.fps });
    console.log('  open-here.gif');
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
