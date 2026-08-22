// Extension e2e: EDITOR MODE — what the panel becomes on a tab that IS the configured editor.
// The editor URL points at the harness app (nothing reaches a real host) and popup.html is
// driven as an ordinary chrome-extension:// page, exactly as popup.smoke.spec.js does.
//
// Deliberately ONE flow, no drag/focus/menu choreography: these runs are headed under xvfb,
// where pointer sequences are the flakiest thing we own. Markup is asserted only where the
// surface contract lives (body.editor-mode, the two sections, a row rather than the empty
// state); WHAT the list holds is asserted over the frozen EDITOR_LIST message, spelled out as
// a wire literal like the rest of this harness, so restyling a row can't turn this red.
//
// The source picker is listed but not fed: the harness serves its fixtures from the app's own
// ORIGIN, and while an origin match is only the pre-filter (a tab must also answer the bridge
// to count as an editor), feeding it here would need a second origin the harness has not got.
// Which pages the picker offers is covered by the pure helpers' unit tests instead.
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const EDITOR_URL = APP_URL;
const POPUP = 'src/popup/popup.html';
const EDITOR_LIST = 'stencil-editor-list';   // MSG.EDITOR_LIST — extension/src/lib/messages.js

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
    await new Promise((r) => setTimeout(r, 800));
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

    // The surface scans the ACTIVE tab of its window, and its first scan ran against itself
    // (a chrome-extension:// page). Bringing the editor to front and re-scanning is what
    // hands it an editor tab — and flips the surface.
    await editor.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.body.classList.contains('editor-mode'), null, { timeout: 15_000 });

    // Both editor sections are ordinary accordion sections (the same .fsection markup as the
    // filters, so collapse / drag-spring / styling come for free) and are shown — on any
    // other tab they stay hidden.
    expect(await ui.evaluate(() => ['sec-editors', 'sec-source-tab'].map((id) => {
      const sec = document.getElementById(id);
      return !!sec && sec.classList.contains('fsection')
        && !!sec.querySelector('.section-head .dlbl')
        && !!sec.querySelector('.section-body')
        && getComputedStyle(sec).display !== 'none';
    }))).toEqual([true, true]);

    // The open-editors list renders a real ROW (not its "no editor tabs are open" empty
    // state), and every row is an editor tab — the row's tooltip carries the tab URL. The
    // tooltip LEADS with the project name (the row itself ellipsizes it, and the "this tab"
    // badge was dropped for an accent outline), so the URL is asserted as a line of it
    // rather than as its prefix.
    await ui.waitForFunction(
      () => document.querySelectorAll('#ed-list .ed-row').length > 0, null, { timeout: 15_000 });
    expect(await ui.evaluate((app) => [...document.querySelectorAll('#ed-list .ed-row')]
      .every((el) => el.title.split('\n').includes(app)), APP_URL)).toBe(true);

    // …and it is the editor tab we opened that it lists. One request/response round-trip to
    // the service worker, from the panel page (which has chrome.runtime, like the real popup):
    // a missing receiver resolves to an error string instead of hanging the test.
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
