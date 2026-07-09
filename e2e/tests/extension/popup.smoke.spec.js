// Extension e2e: the popup + side-panel UI (both driven by src/popup/popup.js). Playwright
// can't pop the toolbar popup or dock a real side panel, so we open their HTML as ordinary
// chrome-extension:// pages in the persistent context and drive them like any page. Both
// surfaces scan the ACTIVE tab of their window (chrome.tabs.query {active,currentWindow}),
// so a fixture host tab is brought to front to give them something real to list.
//
// Covered: the collapsible filter accordion + search-at-bottom (shared markup), the popup's
// ⋯ action menu with a submenu flyout that must stay fully on-screen (the fixed→absolute
// positioning fix), Crop being a single flat action (no submenu), and the side panel's
// re-scan when the active tab changes. Runs headed; CI wraps the job in xvfb (see ci.yml).
import { test, expect, chromium } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { APP_URL } from '../../helpers/config.js';

const EXT_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../extension');
const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';
const POPUP = 'src/popup/popup.html';
const SIDEPANEL = 'src/sidepanel/sidepanel.html';

test.describe('extension popup + side panel UI', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  let extId = '';

  test.beforeAll(async () => {
    context = await chromium.launchPersistentContext('', {
      headless: false, // extensions load most reliably headed; CI runs under xvfb
      channel: 'chromium',
      args: [`--disable-extensions-except=${EXT_PATH}`, `--load-extension=${EXT_PATH}`],
    });
    const sw = await background();
    extId = new URL(sw.url()).host;
    // Point the editor hand-off at the harness app so nothing reaches a real host.
    await sw.evaluate((editorUrl) => new Promise((r) => chrome.storage.sync.set({ editorUrl }, r)), APP_URL);
    await new Promise((r) => setTimeout(r, 500));
  });

  test.afterAll(async () => { await context?.close(); });

  async function background() {
    let [sw] = context.serviceWorkers();
    if (!sw) sw = await context.waitForEvent('serviceworker', { timeout: 15_000 });
    return sw;
  }

  // Open a host fixture tab + the given surface page (popup/sidepanel). The surface's
  // first scan runs against itself (a chrome-extension:// page → cleanly "can't scan"),
  // so callers that need rows bring the host to front and re-scan.
  async function openSurface(rel) {
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    const ui = await context.newPage();
    await ui.goto(`chrome-extension://${extId}/${rel}`);
    await ui.waitForSelector('.filters', { timeout: 15_000 });
    return { host, ui };
  }

  // ── Shared filter chrome: the collapsible sections + search moved to the bottom. ──
  for (const [label, rel] of [['popup', POPUP], ['side panel', SIDEPANEL]]) {
    test(`${label}: renders collapsible filter sections with search at the bottom`, async () => {
      const { host, ui } = await openSurface(rel);
      // Exactly the four section headers, in order — Search is now its own collapsible
      // section at the bottom (its body holds #f-search).
      expect(await ui.locator('.section-head .dlbl').allTextContents())
        .toEqual(['Elements to include', 'Formats', 'Size (px)', 'Search']);
      // Search is the LAST section of .filters, and its body holds #f-search.
      expect(await ui.evaluate(() => {
        const last = document.querySelector('.filters').lastElementChild;
        return last.querySelector('.section-head .dlbl')?.textContent === 'Search'
          && last.querySelector('.section-body #f-search') !== null;
      })).toBe(true);
      // Accordion: clicking a header collapses its body (hidden) and marks the section.
      const state = await ui.evaluate(() => {
        const head = [...document.querySelectorAll('.section-head')]
          .find((h) => h.querySelector('.dlbl')?.textContent === 'Formats');
        head.querySelector('.dlbl').click();
        const sec = head.closest('.fsection');
        return { collapsed: sec.classList.contains('collapsed'), hidden: getComputedStyle(sec.querySelector('.section-body')).display === 'none' };
      });
      expect(state.collapsed).toBe(true);
      expect(state.hidden).toBe(true);
      await host.close();
      await ui.close();
    });
  }

  test('popup: ⋯ menu opens a submenu flyout fully on-screen, and Crop is a single flat action', async () => {
    test.slow();
    const { host, ui } = await openSurface(POPUP);
    // The initial scan hit the popup page itself; make the fixture the active tab and re-scan.
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });

    // A realistic narrow popup width so the flyout must flip left near the right edge.
    await ui.setViewportSize({ width: 360, height: 600 });
    await ui.bringToFront(); // popup has no active-tab re-scan, so rows persist

    await ui.locator('.row .more-btn').first().click();
    await expect(ui.locator('#action-menu')).toBeVisible();

    // Open, Open in…, and Pin are submenus; Crop is a plain top-level action (no submenu / caret).
    expect(await ui.locator('#action-menu > .submenu > .submenu-head .submenu-label').allTextContents())
      .toEqual(['Open', 'Open in…', 'Pin']);
    expect(await ui.locator('#action-menu > button').allInnerTexts()).toContain('Crop');

    // Hover the Open submenu → its flyout shows and stays fully inside the viewport
    // (regression: the action menu's transform used to push a fixed-positioned flyout
    // off-screen; it's now absolute-positioned + viewport-clamped).
    const open = ui.locator('#action-menu > .submenu').first();
    await open.hover();
    const flyout = open.locator('.flyout');
    await expect(flyout).toBeVisible();
    const box = await flyout.boundingBox();
    const vp = ui.viewportSize();
    expect(box.x).toBeGreaterThanOrEqual(-1);
    expect(box.y).toBeGreaterThanOrEqual(-1);
    expect(box.x + box.width).toBeLessThanOrEqual(vp.width + 1);
    expect(box.y + box.height).toBeLessThanOrEqual(vp.height + 1);

    await host.close();
    await ui.close();
  });

  test('side panel: re-scans and lists images when the active tab changes', async () => {
    test.slow();
    const { host, ui } = await openSurface(SIDEPANEL);
    // The side panel listens for tabs.onActivated; bringing the fixture to front fires it,
    // and the panel re-scans that tab (unlike the popup, which only scans on open/rescan).
    await host.bringToFront();
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    expect(await ui.locator('.row').count()).toBeGreaterThan(0);
    await host.close();
    await ui.close();
  });
});
