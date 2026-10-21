// The shots that need a collaboration server: the connection made through the options page's own
// form, a picture pinned on that server through the pin dialog and another pinned locally, the
// list that shows both, and the options page's cards once there is something to list.
import { waitForAnimations } from '../lib/waits.mjs';
import { TIMEOUTS, VIEWS, collab, config, host, runner } from './session.mjs';
import {
  ROW, clickMenuItem, foldFilters, markRow, openFlyout, openRowMenu, popup, regionClip, reopenPopup, rowNamed, surface,
} from './popup.mjs';

// The options page's four cards, each named by a control only that card carries.
const OPTION_CARDS = Object.freeze([
  ['options-general', '#accent'],
  ['options-connections', '#conn-list'],
  ['options-assistant', '#llm-provider'],
  ['options-pins', '#pin-list'],
]);

const setLlm = (page, value) =>
  page.evaluate((v) => new Promise((done) => chrome.storage.local.set({ llmSettings: v }, done)), value);

export const makeServerSteps = ({ serverPin, localPin }) => Object.freeze([
  // No shot: the connection the later shots need, made the way a user makes it.
  { name: 'options-connect', run: async (ctx, theme) => {
    const options = await surface('src/options/options.html', VIEWS.options, theme);
    await options.waitForSelector('#conn-add', { timeout: TIMEOUTS.scanMs });
    await options.locator('#conn-url').fill(collab.url);
    await options.locator('#conn-token').fill(await collab.mint());
    await options.locator('#conn-add').click();
    await options.locator('#conn-list li').first().waitFor({ timeout: TIMEOUTS.scanMs });
    await options.close();
    await reopenPopup(ctx, theme);
  } },
  // With a server connected, the pin button asks where the pin goes; the server choice is picked.
  { name: 'popup-pin-dialog', run: async (ctx, theme) => {
    const ui = await popup(ctx, theme);
    await foldFilters(ui);
    await markRow(ui, serverPin);
    await rowNamed(ui, serverPin).locator('.pin-btn').click();
    const dialog = ui.locator('.dialog-back .dialog');
    await dialog.waitFor({ timeout: TIMEOUTS.scanMs });
    // A .value set is what re-labels the enhanced list's trigger (lib/control/customSelect.js).
    await dialog.locator('select').evaluate((sel) => { sel.value = sel.options[1].value; });
    await waitForAnimations(ui);
    await runner.shot(ui, 'popup-pin-dialog', await regionClip(ui, [ROW, '.dialog-back .dialog']));
    await dialog.locator('button.primary').click();
    await ui.locator('.row.shared').first().waitFor({ timeout: TIMEOUTS.scanMs });
  } },
  { name: 'popup-pinned', run: async (ctx, theme) => {
    let ui = await popup(ctx, theme);
    await openRowMenu(ui, localPin);
    await openFlyout(ui, 'Pin');
    await clickMenuItem(ui, 'Locally', true);
    await ui.locator('.row .pin-btn.active').nth(1).waitFor({ timeout: TIMEOUTS.scanMs });
    ui = await reopenPopup(ctx, theme);
    await foldFilters(ui);
    await ui.locator('.row.shared').first().waitFor({ timeout: TIMEOUTS.scanMs });
    // The top of the popup down to the fourth row: pinned and server rows float to the top.
    await runner.shot(ui, 'popup-pinned', await regionClip(ui, ['header', '#list > li:nth-child(-n+4)'], 0));
  } },
  // A provider set up the way a user would, so the assistant card shows a model in place.
  { name: 'options', run: async (ctx, theme) => {
    const options = await surface('src/options/options.html', VIEWS.options, theme);
    await setLlm(options, config.get('assistant.options'));
    await options.reload();
    await options.waitForSelector('.card', { timeout: TIMEOUTS.scanMs });
    await options.locator('#conn-list li').first().waitFor({ timeout: TIMEOUTS.scanMs });
    await options.locator('#pin-list li').first().waitFor({ timeout: TIMEOUTS.scanMs });
    await waitForAnimations(options);
    // The element box IS the card, so the background the page centres it on never enters the frame.
    for (const [name, marker] of OPTION_CARDS) {
      const card = options.locator('.card').filter({ has: options.locator(marker) }).first();
      await card.scrollIntoViewIfNeeded();
      await waitForAnimations(options);
      await runner.shot(card, name);
    }
    await options.close();
    await host.bringToFront();
  } },
]);
