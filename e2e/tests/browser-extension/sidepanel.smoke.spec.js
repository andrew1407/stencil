// Extension e2e: the side panel (driven by src/popup/popup.js, like the popup). Its HTML is
// opened as an ordinary chrome-extension:// page in the persistent context and scans the ACTIVE
// tab of its window, so a fixture host tab is brought to front to give it something to list.
// Runs headed; CI wraps the job in xvfb.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL, SITE_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const FIXTURE_URL = SITE_URL + '__e2e__/page-with-image.html';
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

  // The header logo is spring-loaded: hovering it mid-drag opens a menu whose items are each drop
  // targets (lib/drop/entry.js). Events are dispatched — a native drag can't enter a surface.
  test('side panel: dragging page media over the logo springs a 4-item menu you drop onto', async () => {
    test.slow();
    const { host, ui } = await openSurface(SIDEPANEL);
    await host.bringToFront();
    await ui.evaluate(() => document.getElementById('rescan').click());
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    await ui.setViewportSize({ width: 420, height: 700 });

    // A page image that is NOT one of the scanned rows, so the entry is built from the
    // drag itself (unknown dimensions and all) rather than reusing a listed row.
    const dropped = `${SITE_URL}__e2e__/pixel.png?logo=1`;
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

    // Crop runs ONCE: the in-page modal, and no second crop as a tab. The unloadable URL is what
    // makes it bite — the overlay's ready-watchdog can only misfire on an image that never loads.
    const slow = `${SITE_URL}__e2e__/does-not-exist.png`;
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

    await Promise.all([host.close(), ui.close()]);
  });

  // The injected modal shell (lib/drop/overlay.js) sits in someone else's page and cannot read the
  // extension's CSS variables, so its palette is handed to it as data (lib/prefs/shellTheme.js).
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
      // `.bar button` transitions background/color .15s and starts late under load, so wait until the
      // rendered button has caught up with the shell's own --st-panel2 / --st-text.
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
    await Promise.all([host.close(), ui.close()]);
  });

  test('side panel: re-scans and lists images when the active tab changes', async () => {
    test.slow();
    const { host, ui } = await openSurface(SIDEPANEL);
    // The side panel listens for tabs.onActivated; bringing the fixture to front fires it,
    // and the panel re-scans that tab (unlike the popup, which only scans on open/rescan).
    await host.bringToFront();
    await ui.waitForFunction(() => document.querySelectorAll('.row').length > 0, null, { timeout: 15_000 });
    expect(await ui.locator('.row').count()).toBeGreaterThan(0);
    await Promise.all([host.close(), ui.close()]);
  });
});
