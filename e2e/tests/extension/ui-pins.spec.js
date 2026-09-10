// UI regression pins for the MV3 extension — the same guard as tests/browser/ui-pins.spec.js
// (helpers/uiPin.js): each surface is driven into a state and its subtree recorded as
// computed styles + DOM shape, deep-equalled against e2e/pins/<name>.json. No screenshots.
//
// Context setup mirrors popup.smoke.spec.js: a persistent context with the unpacked
// extension, the editor hand-off pointed at the harness app, and the popup / side panel
// opened as ordinary chrome-extension:// pages against a fixture host tab.
//
// Record/refresh the baselines with UPDATE_PINS=1 (see ../../README.md).
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';
import { expectPin, freezeMotion } from '../../helpers/uiPin.js';

const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';
// A fixed box for every surface: the popup's own width, so the pins record the real
// narrow layout rather than whatever the launched window happened to be.
const POPUP_VIEWPORT = { width: 380, height: 720 };

test.describe('extension UI pins', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  let extId = '';

  test.beforeAll(async () => {
    const ext = await launchExtension();
    context = ext.context;
    extId = ext.extId;
    const sw = await ext.background();
    await sw.evaluate((editorUrl) => new Promise((r) => chrome.storage.sync.set({ editorUrl }, r)), APP_URL);
    await new Promise((r) => setTimeout(r, 500));
  });

  test.afterAll(async () => { await context?.close(); });

  // A surface page at a fixed size, with motion and theme frozen.
  const openSurface = async (rel) => {
    const ui = await context.newPage();
    await ui.setViewportSize(POPUP_VIEWPORT);
    await ui.goto(`chrome-extension://${extId}/${rel}`);
    await freezeMotion(ui);
    return ui;
  };

  test('popup: list, filter accordion, row action menu, assistant chat', async () => {
    test.slow();
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    const ui = await openSurface('src/popup/popup.html');
    await ui.waitForSelector('.filters', { timeout: 15_000 });

    // The popup's first scan ran against itself (a chrome-extension:// page); make the
    // fixture the active tab and re-scan so the list has the fixture's images in it.
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    await ui.bringToFront();   // the popup never re-scans on its own, so the rows persist

    // The filter accordion in its shipped state — every section open but Assistant.
    await expectPin(ui, { name: 'popup-filter-open', root: '.filters' });
    await expectPin(ui, { name: 'popup-list', root: '#list' });

    // The ⋯ row menu (retried: under xvfb the list can scroll itself once right after
    // the click, which closes the menu by design — popup.smoke.spec.js hits the same).
    await expect(async () => {
      await ui.locator('.row .more-btn').first().click();
      await expect(ui.locator('#action-menu')).toBeVisible({ timeout: 2000 });
    }).toPass({ timeout: 30_000 });
    await expectPin(ui, { name: 'popup-row-menu', root: '#action-menu' });
    await ui.keyboard.press('Escape');
    await expect(ui.locator('#action-menu')).toBeHidden();

    // The embedded Assistant section ships collapsed; its header opens the chat.
    await ui.evaluate(() => document.querySelector('#sec-assistant .section-head .dlbl').click());
    await expect(ui.locator('#sec-assistant')).not.toHaveClass(/collapsed/);
    await expectPin(ui, { name: 'assistant-chat', root: '#sec-assistant' });

    await host.close();
    await ui.close();
  });

  test('side panel and the options page', async () => {
    test.slow();
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    const panel = await openSurface('src/sidepanel/sidepanel.html');
    await panel.waitForSelector('.filters', { timeout: 15_000 });
    // The side panel re-scans when the active tab changes (tabs.onActivated), so the
    // fixture coming to front is what fills its list.
    await host.bringToFront();
    await panel.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    await panel.bringToFront();
    await expectPin(panel, { name: 'side-panel', root: 'body' });
    await panel.close();

    const options = await openSurface('src/options/options.html');
    await options.waitForSelector('.card', { timeout: 15_000 });
    await expectPin(options, { name: 'options-page', root: 'body' });

    await options.close();
    await host.close();
  });
});
