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
    await new Promise((r) => setTimeout(r, 500));
  });

  test.afterAll(async () => { await context?.close(); });

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
      // Exactly the five section headers a NON-editor page shows, in order — "Found
      // resources" (search + results) closes the filters, and the embedded Assistant
      // section sits below the list. Editor mode's own two sections ("Open editors",
      // "Images from another page") are in the markup but display:none here, so the
      // assertion is over what is actually visible.
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

      // Hover the Open submenu → its flyout shows and stays fully inside the viewport
      // (regression: the action menu's transform used to push a fixed-positioned flyout
      // off-screen; it's now absolute-positioned + viewport-clamped).
      await open.hover({ timeout: 5000 });
      await expect(flyout).toBeVisible({ timeout: 2000 });
    }).toPass({ timeout: 60_000 });
    const box = await flyout.boundingBox();
    const vp = ui.viewportSize();
    expect(box.x).toBeGreaterThanOrEqual(-1);
    expect(box.y).toBeGreaterThanOrEqual(-1);
    expect(box.x + box.width).toBeLessThanOrEqual(vp.width + 1);
    expect(box.y + box.height).toBeLessThanOrEqual(vp.height + 1);

    await host.close();
    await ui.close();
  });

  // The header logo is SPRING-LOADED: hovering it with page media MID-DRAG (no drop)
  // opens a four-item menu — Open in editor / Open in new tab / Open incognito / Crop —
  // and each item is itself a drop target, so you keep dragging onto the one you want
  // and release there. Releasing anywhere else does nothing. lib/dropEntry.js normalises
  // the released payload into a scan-row entry. Exercised on the side panel, the surface
  // that stays open while you drag from the page; the drag events are dispatched
  // directly (Playwright's native drag can't cross from a page into an extension
  // surface, nor hold a hover dwell) — the listeners are the real popup.js ones.
  test('side panel: dragging page media over the logo springs a 4-item menu you drop onto', async () => {
    test.slow();
    const { host, ui } = await openSurface(SIDEPANEL);
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    await ui.setViewportSize({ width: 420, height: 700 });

    // A page image that is NOT one of the scanned rows, so the entry is built from the
    // drag itself (unknown dimensions and all) rather than reusing a listed row.
    const dropped = `${APP_URL}__e2e__/pixel.png?logo=1`;
    const fireDrag = (type, selector, url = dropped) => ui.evaluate(({ type, selector, url }) => {
      const dt = new DataTransfer();
      dt.setData('text/uri-list', url);
      dt.setData('text/html', `<img src="${url}">`);
      document.querySelector(selector).dispatchEvent(
        new DragEvent(type, { dataTransfer: dt, bubbles: true, cancelable: true }));
    }, { type, selector, url });

    // ── While ANY compatible drag is live on the surface, the logo advertises itself
    //    as a target (a CSS pulse) — before the pointer ever reaches it. ──
    const logo = ui.locator('header .logo');
    await expect(logo).not.toHaveClass(/drag-armed/);
    await fireDrag('dragover', '#list');            // nowhere near the logo
    await expect(logo).toHaveClass(/drag-armed/);
    await fireDrag('dragend', '.row');
    await expect(logo).not.toHaveClass(/drag-armed/);

    // ── Hovering the logo springs the menu open WITHOUT a drop. ──
    await fireDrag('dragenter', 'header .logo');
    await fireDrag('dragover', 'header .logo');
    await expect(ui.locator('header .logo')).toHaveClass(/drop-over/);
    const items = ui.locator('#action-menu .drag-item');
    await expect(items).toHaveCount(4, { timeout: 5_000 });
    expect(await items.allInnerTexts()).toEqual(
      ['Open in editor', 'Open in new tab', 'Open incognito', 'Crop']);
    // Exactly four flat actions — no Download / Pin / Open in… / submenus here.
    expect(await ui.locator('#action-menu > .submenu').count()).toBe(0);
    expect(await ui.locator('#action-menu > button:not(.drag-item)').count()).toBe(0);
    // Nothing has happened yet: the menu opened on hover, not on a drop.
    expect(await host.locator('iframe').count()).toBe(0);

    // ── Releasing over "Open in editor" performs the row hand-off on the dragged
    //    media: the in-page editor modal, carrying the `#stencil=` launch payload. ──
    await fireDrag('drop', '#action-menu .drag-item[data-action="editor"]');
    const frame = host.locator('iframe').first();
    await expect(frame).toBeAttached({ timeout: 15_000 });
    await expect.poll(async () => (await frame.getAttribute('src')) || '', { timeout: 10_000 })
      .toContain('#stencil=');
    const src = await frame.getAttribute('src');
    const launch = JSON.parse(decodeURIComponent(src.slice(src.indexOf('#stencil=') + '#stencil='.length)));
    expect(src.startsWith(APP_URL)).toBeTruthy();
    expect(launch.dataUrl).toMatch(/^data:image\//);
    expect(launch.name).toBe('pixel.png');
    expect(launch.source).toBe(dropped);          // provenance = the dragged media URL
    await expect(ui.locator('#action-menu')).toBeHidden();

    // ── Crop runs ONCE: the in-page crop modal, and no second crop as a TAB.
    //    Regression: the overlay's ready-watchdog only heard from the crop page once its
    //    IMAGE had loaded, so an image that is slow (or, as here, never loads at all)
    //    tore the working modal down after 3s and re-opened crop in a tab — one release,
    //    two crops. The unloadable URL is what makes this assertion bite: with a fast
    //    image the watchdog never got the chance to misfire. ──
    const slow = `${APP_URL}__e2e__/does-not-exist.png`;
    const cropTabs = () => context.pages().filter((p) => p.url().includes('/src/crop/crop.html')).length;
    await host.evaluate(() => document.getElementById('stencil-ext-modal')?.remove());
    await fireDrag('dragenter', 'header .logo', slow);
    await fireDrag('dragover', 'header .logo', slow);
    await expect(items).toHaveCount(4, { timeout: 5_000 });
    await fireDrag('drop', '#action-menu .drag-item[data-action="crop"]', slow);
    const cropFrame = host.locator('iframe').first();
    await expect(cropFrame).toBeAttached({ timeout: 15_000 });
    await expect.poll(async () => (await cropFrame.getAttribute('src')) || '', { timeout: 10_000 })
      .toContain('/src/crop/crop.html');
    expect(cropTabs()).toBe(0);
    await ui.waitForTimeout(4_000);               // past the watchdog that used to fire
    expect(cropTabs()).toBe(0);                   // still no duplicate crop
    expect(await host.locator('iframe').count()).toBe(1);
    await expect(cropFrame).toBeAttached();       // …and the modal is still standing

    // ── Releasing OUTSIDE the menu closes it and does nothing. ──
    await host.evaluate(() => document.getElementById('stencil-ext-modal')?.remove());
    await fireDrag('dragenter', 'header .logo');
    await fireDrag('dragover', 'header .logo');
    await expect(items).toHaveCount(4, { timeout: 5_000 });
    await fireDrag('drop', '#status');
    await expect(ui.locator('#action-menu')).toBeHidden();
    await expect(ui.locator('header .logo')).not.toHaveClass(/drop-over/);
    await ui.waitForTimeout(500);
    expect(await host.locator('iframe').count()).toBe(0);   // nothing new was launched
    expect(cropTabs()).toBe(0);

    await host.close();
    await ui.close();
  });

  // The in-page modal SHELL (lib/overlay.js — title bar, frame, pop-out/close buttons)
  // is injected into someone else's page, so it can't read the extension's CSS
  // variables: its palette is handed to it as data (lib/shellTheme.js). Regression: it
  // was hardcoded light-with-a-prefers-color-scheme-override, so a user on Dark with a
  // light OS got a WHITE frame wrapped around the dark crop page.
  test('side panel: the in-page modal shell follows the extension theme', async () => {
    test.slow();
    const { host, ui } = await openSurface(SIDEPANEL);
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });

    // Read the shell from inside its shadow root (it is isolated from the host page).
    const shell = () => host.evaluate(() => {
      const h = document.getElementById('stencil-ext-modal');
      if (!h) return null;
      const q = (s) => h.shadowRoot.querySelector(s);
      return {
        theme: h.getAttribute('data-stencil-theme'),
        bar: getComputedStyle(q('.bar')).backgroundColor,
        barText: getComputedStyle(q('.bar')).color,
        panel: getComputedStyle(q('.panel')).backgroundColor,
        btn: getComputedStyle(q('.bar button')).backgroundColor,
        btnText: getComputedStyle(q('.bar button')).color,
      };
    });

    const buttonCaughtUp = () => host.evaluate(() => {
      const h = document.getElementById('stencil-ext-modal');
      if (!h) return false;
      const shellVar = (name) => {
        const n = parseInt(getComputedStyle(h).getPropertyValue(name).trim().slice(1), 16);
        return `rgb(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255})`;
      };
      const cs = getComputedStyle(h.shadowRoot.querySelector('.bar button'));
      return cs.backgroundColor === shellVar('--st-panel2') && cs.color === shellVar('--st-text');
    });

    const seen = {};
    for (const mode of ['dark', 'light']) {
      // Exactly what the header's moon button does (localStorage + the storage mirror).
      await ui.evaluate((m) => window.StencilTheme.set(m), mode);
      // The mirror is async: a shell mounted before it lands reads the OLD palette, and
      // the onChanged that would re-paint it has already fired. Wait for the commit.
      await expect.poll(() => ui.evaluate(() => new Promise((r) =>
        chrome.storage.local.get(['stencil_theme'], (v) => r(v.stencil_theme))))).toBe(mode);
      await host.evaluate(() => document.getElementById('stencil-ext-modal')?.remove());
      // Double-click a row → the quick-crop modal, mounted in the host page.
      await ui.evaluate(() => document.querySelector('.row .thumb')
        .dispatchEvent(new MouseEvent('dblclick', { bubbles: true })));
      await host.waitForFunction(() => !!document.getElementById('stencil-ext-modal'), null, { timeout: 15_000 });
      await expect.poll(async () => (await shell())?.theme, { timeout: 10_000 }).toBe(mode);
      // `.bar button` transitions background/color .15s when the live re-theme lands,
      // and under load the transition starts LATE: wait until the rendered button has
      // caught up with the shell's own --st-panel2 / --st-text, not for stable reads.
      await expect.poll(buttonCaughtUp, { timeout: 10_000 }).toBe(true);
      seen[mode] = await shell();
    }

    // The shell is painted from the SAME palette as the rest of the chrome (theme.css).
    expect(seen.dark.bar).toBe('rgb(43, 47, 58)');        // --panel, dark
    expect(seen.light.bar).toBe('rgb(255, 255, 255)');    // --panel, light
    expect(seen.dark.panel).toBe('rgb(33, 36, 45)');      // --bg, dark
    expect(seen.light.panel).toBe('rgb(244, 245, 247)');  // --bg, light
    // …and every surface of it really differs between the two, buttons included.
    for (const k of ['bar', 'barText', 'panel', 'btn', 'btnText']) {
      expect(seen.dark[k], `shell ${k} must differ between themes`).not.toBe(seen.light[k]);
    }

    await ui.evaluate(() => window.StencilTheme.set('system'));   // leave no state behind
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
