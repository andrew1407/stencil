// The popup's own shots: the scanned list in both themes, an image row's and the clip's ⋯ menus,
// the editor tabs a row opens, the chooser an already-opened row brings up, and an assistant turn
// about the page.
import { chatOnlyPlan } from '../lib/llmStub.mjs';
import { pairNames } from '../lib/theme/selector.mjs';
import { waitForAnimations } from '../lib/waits.mjs';
import { TIMEOUTS, config, context, host, llmSettings, runner, stub } from './session.mjs';
import {
  ROW, atRealHeight, foldFilters, markRow, openFlyout, openInTab, openRowMenu, popup, popupClip, regionClip,
  reopenPopup, rowNamed,
} from './popup.mjs';

// The flyout is checked again as the shot is clipped: a menu the shot shows without it is wrong.
const menuShot = (name, row, flyout) => ({ name, run: async (ctx, theme) => {
  const ui = await popup(ctx, theme);
  await foldFilters(ui);
  await markRow(ui, row);
  await openRowMenu(ui, row);
  if (flyout && !(await openFlyout(ui, flyout))) throw new Error(`${name}: the ${flyout} flyout never opened`);
  const clip = await regionClip(ui, [ROW, '#action-menu', '#action-menu .flyout']);
  if (flyout && !(await openFlyout(ui, flyout))) throw new Error(`${name}: the ${flyout} flyout closed`);
  await runner.shot(ui, name, clip);
  await ui.keyboard.press('Escape');
  await ui.mouse.click(2, 2);
} });

// Thumbnails load lazily as the list scrolls: the rows in view must have theirs before a shot.
const thumbsShown = (ui) => ui.waitForFunction(() => {
  const view = document.getElementById('list').getBoundingClientRect();
  return [...document.querySelectorAll('#list img.thumb')].every((img) => {
    const box = img.getBoundingClientRect();
    return box.bottom <= view.top || box.top >= view.bottom || img.style.visibility === 'hidden'
      || (img.complete && img.naturalWidth > 0);
  });
}, null, { timeout: TIMEOUTS.scanMs });

export const makePopupSteps = ({ image, clipRow }) => Object.freeze([
  // The popup at a user's 600px, filters folded and the list run to its end, where the page's
  // icons, the clip, its poster and the background show their kinds.
  ...pairNames('popup-list').map((name) => ({ name, run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await foldFilters(ui);
    await atRealHeight(ui, true);
    await ui.evaluate(() => { const list = document.getElementById('list'); list.scrollTop = list.scrollHeight; });
    await thumbsShown(ui);
    await runner.shot(ui, name, await popupClip(ui));
    await atRealHeight(ui, false);
  } })),
  menuShot('popup-row-menu', image, 'Open'),
  menuShot('popup-video-menu', clipRow, null),
  { name: 'editor-tab', run: (ctx, theme) => openInTab(ctx, theme, image, 'In editor', 'editor-tab') },
  { name: 'editor-tab-incognito',
    run: (ctx, theme) => openInTab(ctx, theme, image, 'In editor (incognito)', 'editor-tab-incognito') },
  // Once a picture has been opened (the editor-tab shot did it), its row carries the opened flag
  // and a click on its name asks whether to resume that editor's project or add a copy.
  { name: 'popup-opened', run: async (ctx, theme) => {
    let ui = await popup(ctx, theme);
    if (!(await rowNamed(ui, image).evaluate((el) => el.classList.contains('opened')))) {
      await host.bringToFront();
      const [editor] = await Promise.all([context.waitForEvent('page'), rowNamed(ui, image).locator('.name').click()]);
      await editor.waitForFunction(() => document.getElementById('canvas')?.width > 0, null, { timeout: TIMEOUTS.editorMs });
      await editor.close();
    }
    ui = await reopenPopup(ctx, theme);
    await foldFilters(ui);
    await markRow(ui, image);
    await rowNamed(ui, image).locator('.name').click();
    await ui.locator('#action-menu').waitFor();
    await waitForAnimations(ui, '#action-menu');
    await runner.shot(ui, 'popup-opened', await regionClip(ui, [ROW, '#action-menu']));
    await ui.keyboard.press('Escape');
    await reopenPopup(ctx, theme);
  } },
  // The stub's canned turn unless a real model is asked for: the reply is written for this page.
  { name: 'popup-assistant', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    const real = config.realLlm();
    const settings = real
      ? { provider: 'stencil-server', baseUrl: '', model: '', apiKey: '', serverUrl: config.serverUrl, serverToken: config.serverToken() }
      : llmSettings(`${stub.url}/v1`);
    await ui.evaluate((value) => new Promise((done) => chrome.storage.local.set({ llmSettings: value }, done)), settings);
    if (!real) stub.queue(chatOnlyPlan(config.get('assistant.reply')));
    await ui.evaluate(() => document.querySelector('#sec-assistant .section-head .dlbl').click());
    await ui.locator('#sec-assistant:not(.collapsed)').waitFor();
    await ui.locator('#chat-input').fill(config.prompt('extension'));
    await ui.locator('#chat-send').click();
    await ui.locator('#chat-transcript .msg.assistant:not(.typing-row)').first().waitFor({ timeout: TIMEOUTS.replyMs });
    await ui.locator('#chat-transcript .typing-row').waitFor({ state: 'detached', timeout: TIMEOUTS.replyMs }).catch(() => {});
    await waitForAnimations(ui);
    await runner.shot(ui, 'popup-assistant', await regionClip(ui, ['#sec-assistant']));
  } },
]);
