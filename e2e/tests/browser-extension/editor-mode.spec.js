// Extension e2e: EDITOR MODE — what the panel becomes on a tab that IS the configured editor.
// The editor URL points at the harness app and popup.html is driven as an ordinary
// chrome-extension:// page. Markup is asserted only where the surface contract lives; WHAT the
// list holds is asserted over the frozen EDITOR_LIST message, as a wire literal. The source
// picker is listed but not fed — that would need a second origin the harness has not got.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const EDITOR_URL = APP_URL;
const POPUP = 'src/popup/popup.html';
const EDITOR_LIST = 'stencil-editor-list';   // MSG.EDITOR_LIST — browser-extension/src/lib/messages.js

test.describe('extension editor mode', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  let extId = '';

  test.beforeAll(async () => {
    const ext = await launchExtension();
    context = ext.context;
    extId = ext.extId;
    // The harness app becomes "the editor": every tab on APP_URL now counts as one, and the
    // editor bridge is scoped to that origin.
    const sw = await ext.background();
    await sw.evaluate((editorUrl) => new Promise((r) => chrome.storage.sync.set({ editorUrl }, r)), EDITOR_URL);
    await sleep(800);
  });

  test.afterAll(async () => { await context?.close(); });

  test('the panel switches to the editor surface and lists the open editor tab', async () => {
    test.slow();
    // The editor tab: the real browser app. Waiting for its own facade means the page — and
    // with it the extension's editor bridge — is really up before the panel asks it anything.
    const editor = await context.newPage();
    await editor.goto(APP_URL);
    await editor.waitForFunction(() => !!(/** @type {any} */ (window).stencil), null, { timeout: 15_000 });

    const ui = await context.newPage();
    await ui.goto(`chrome-extension://${extId}/${POPUP}`);
    await ui.waitForSelector('.filters', { timeout: 15_000 });

    // The surface scans the ACTIVE tab of its window and its first scan ran against itself, so
    // bringing the editor to front and re-scanning is what flips the surface.
    await editor.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.body.classList.contains('editor-mode'), null, { timeout: 15_000 });

    expect(await ui.evaluate(() => ['sec-editors', 'sec-source-tab'].map((id) => {
      const sec = document.getElementById(id);
      return !!sec && sec.classList.contains('fsection')
        && !!sec.querySelector('.section-head .dlbl')
        && !!sec.querySelector('.section-body')
        && getComputedStyle(sec).display !== 'none';
    }))).toEqual([true, true]);

    // The row's tooltip is the app's own (lib/tip.js data-title, not the native title) and LEADS
    // with the project name, so the tab URL is asserted as a line of it rather than as its prefix.
    await ui.waitForFunction(
      () => document.querySelectorAll('#ed-list .ed-row').length > 0, null, { timeout: 15_000 });
    expect(await ui.evaluate((app) => [...document.querySelectorAll('#ed-list .ed-row')]
      .every((el) => (el.dataset.title || '').split('\n').includes(app)), APP_URL)).toBe(true);

    // One request/response round-trip to the service worker; a missing receiver resolves to an
    // error string instead of hanging the test.
    const listed = await ui.evaluate((type) => new Promise((resolve) => {
      chrome.runtime.sendMessage({ type, thumbnails: false }, (res) =>
        resolve(res || { ok: false, error: chrome.runtime.lastError?.message || 'no receiver' }));
    }), EDITOR_LIST);
    expect(listed.ok, listed.error).toBe(true);
    const row = listed.editors.find((e) => e.url.startsWith(APP_URL));
    expect(row, 'the open editor tab must be listed').toBeTruthy();
    expect(typeof row.tabId).toBe('number');
    expect(Array.isArray(row.projects)).toBe(true);   // normalised even when a bridge stays silent
    // Nothing but editor tabs is in that list — the panel's own page is never one of them.
    expect(listed.editors.every((e) => e.url.startsWith(APP_URL))).toBe(true);

    await editor.close();
    await ui.close();
  });
});
