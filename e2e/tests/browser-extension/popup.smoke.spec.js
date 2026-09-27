// Extension e2e: the popup + side panel (both driven by src/popup/popup.js). Playwright can't
// pop the toolbar popup or dock a real side panel, so their HTML is opened as ordinary
// chrome-extension:// pages in the persistent context. Both surfaces scan the ACTIVE tab of
// their window, so a fixture host tab is brought to front to give them something to list.
// Runs headed; CI wraps the job in xvfb.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';
const POPUP = 'src/popup/popup.html';
const SIDEPANEL = 'src/sidepanel/sidepanel.html';

test.describe('extension popup + side panel UI', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  let extId = '';

  test.beforeAll(async () => {
    const ext = await launchExtension();
    context = ext.context;
    extId = ext.extId;
    // Point the editor hand-off at the harness app so nothing reaches a real host.
    const sw = await ext.background();
    await sw.evaluate((editorUrl) => new Promise((r) => chrome.storage.sync.set({ editorUrl }, r)), APP_URL);
    await sleep(500);
  });

  test.afterAll(async () => { await context?.close(); });

  // The surface's first scan runs against itself (a chrome-extension:// page → "can't scan"), so
  // callers that need rows bring the host to front and re-scan.
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
      // The five section headers a NON-editor page shows, in order. Editor mode's own two are in the
      // markup but display:none here, so the assertion is over what is actually visible.
      expect(await ui.evaluate(() => [...document.querySelectorAll('.section-head .dlbl')]
        .filter((el) => getComputedStyle(el.closest('.fsection')).display !== 'none')
        .map((el) => el.textContent)))
        .toEqual(['Elements to include', 'Formats', 'Size (px)', 'Found resources', 'Assistant chat']);
      // Found resources is the last VISIBLE section of .filters, and its body holds
      // #f-search (the source-page picker sits after it in the markup, hidden here).
      expect(await ui.evaluate(() => {
        const last = [...document.querySelector('.filters').children]
          .filter((el) => getComputedStyle(el).display !== 'none').pop();
        return last.id === 'sec-search'
          && last.querySelector('.section-head .dlbl')?.textContent === 'Found resources'
          && last.querySelector('.section-body #f-search') !== null;
      })).toBe(true);
      // Collapsing Found resources folds the results list + status with it.
      expect(await ui.evaluate(() => {
        const head = document.querySelector('#sec-search .section-head');
        head.querySelector('.dlbl').click();
        const folded = getComputedStyle(document.getElementById('list')).display === 'none';
        head.querySelector('.dlbl').click();   // restore for the assertions below
        return folded;
      })).toBe(true);
      // The Assistant section ships collapsed, below the list.
      expect(await ui.evaluate(() => {
        const sec = document.getElementById('sec-assistant');
        return !!sec && sec.classList.contains('collapsed')
          && sec.compareDocumentPosition(document.getElementById('list')) === Node.DOCUMENT_POSITION_PRECEDING;
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
      await Promise.all([host.close(), ui.close()]);
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

    // Retried: under xvfb Chromium may natively scroll the list once right after the
    // click, which closes the menu by design; the next attempt runs on a settled list.
    const open = ui.locator('#action-menu > .submenu').first();
    const flyout = open.locator('.flyout');
    await expect(async () => {
      await ui.locator('.row .more-btn').first().click();
      await expect(ui.locator('#action-menu')).toBeVisible({ timeout: 2000 });

      // Open, Open in…, and Pin are submenus; Crop is a plain top-level action (no submenu / caret).
      expect(await ui.locator('#action-menu > .submenu > .submenu-head .submenu-label').allTextContents())
        .toEqual(['Open', 'Open in…', 'Pin']);
      expect(await ui.locator('#action-menu > button').allInnerTexts()).toContain('Crop');

      // The flyout must stay fully inside the viewport: it is absolute-positioned and clamped,
      // because the action menu's transform pushes a fixed-positioned one off-screen.
      await open.hover({ timeout: 5000 });
      await expect(flyout).toBeVisible({ timeout: 2000 });
    }).toPass({ timeout: 60_000 });
    const box = await flyout.boundingBox();
    const vp = ui.viewportSize();
    expect(box.x).toBeGreaterThanOrEqual(-1);
    expect(box.y).toBeGreaterThanOrEqual(-1);
    expect(box.x + box.width).toBeLessThanOrEqual(vp.width + 1);
    expect(box.y + box.height).toBeLessThanOrEqual(vp.height + 1);

    await Promise.all([host.close(), ui.close()]);
  });
});
