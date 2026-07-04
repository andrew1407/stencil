// Extension e2e: load the unpacked MV3 extension in a persistent Chromium context and
// exercise the real ↔ editor seams — the page-scripting API, the new-tab AND in-page
// modal hand-offs, pin/unpin, and the CSS-background scan path. Extensions require a
// persistent context (the default `page` fixture can't provide one), so this suite
// manages its own. Runs headed; CI wraps the job in xvfb (see ci.yml / README).
import { test, expect, chromium } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { APP_URL } from '../../helpers/config.js';

const EXT_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../extension');
const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';
const EDITOR_URL = APP_URL;

test.describe('extension', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;

  test.beforeAll(async () => {
    context = await chromium.launchPersistentContext('', {
      headless: false, // extensions load most reliably headed; CI runs under xvfb
      channel: 'chromium',
      args: [`--disable-extensions-except=${EXT_PATH}`, `--load-extension=${EXT_PATH}`],
    });
    // Point the extension at the harness app (default editorUrl is :8080) and turn on the
    // opt-in page API; storage.onChanged (background.js) re-scopes the bridge and registers
    // the MAIN-world content script for subsequent navigations.
    const sw = await background();
    await sw.evaluate((editorUrl) => new Promise((r) =>
      chrome.storage.sync.set({ exposeWindowStencil: true, editorUrl }, r)), EDITOR_URL);
    await new Promise((r) => setTimeout(r, 800));
  });

  test.afterAll(async () => { await context?.close(); });

  async function background() {
    let [sw] = context.serviceWorkers();
    if (!sw) sw = await context.waitForEvent('serviceworker', { timeout: 15_000 });
    return sw;
  }

  // Open the fixture host page and wait for the injected page API to be live.
  async function openHost() {
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    await host.waitForFunction(() => !!(/** @type {any} */ (window).stencil?.images), null, { timeout: 15_000 })
      .catch(async () => { await host.reload(); await host.waitForFunction(() => !!window.stencil?.images, null, { timeout: 15_000 }); });
    return host;
  }

  test('registers its background service worker', async () => {
    const sw = await background();
    expect(sw.url()).toContain('background.js');
  });

  test('scans images and CSS backgrounds on the page', async () => {
    const host = await openHost();
    const counts = await host.evaluate(() => ({
      images: window.stencil.images.length,
      backgrounds: window.stencil.backgrounds.length,
      bgUrl: window.stencil.backgrounds[0]?.url || '',
    }));
    expect(counts.images).toBeGreaterThan(0);
    expect(counts.backgrounds).toBeGreaterThan(0);
    expect(counts.bgUrl).toContain('pixel.png');
    await host.close();
  });

  test('hands an image off to a NEW editor tab', async () => {
    test.slow();
    const host = await openHost();
    const editorPagePromise = context.waitForEvent('page', { timeout: 15_000 });
    await host.evaluate(() => window.stencil.images[0].open({ newTab: true }));
    const editor = await editorPagePromise;

    await editor.waitForLoadState('domcontentloaded');
    expect(editor.url().startsWith(EDITOR_URL)).toBeTruthy();
    expect(editor.url()).toContain('#stencil=');
    await editor.waitForFunction(() => !!(/** @type {any} */ (window).stencil?.current?.imageName), null, { timeout: 15_000 });
    expect(await editor.evaluate(() => !!window.stencil.current.imageName)).toBeTruthy();
    await editor.close();
    await host.close();
  });

  test('hands an image off to an IN-PAGE modal iframe (default)', async () => {
    test.slow();
    const host = await openHost();
    // Default open() (no newTab) mounts the editor as an in-page iframe overlay in the host tab.
    await host.evaluate(() => window.stencil.images[0].open());
    const frame = host.locator('iframe');
    await expect(frame.first()).toBeAttached({ timeout: 15_000 });
    await expect.poll(async () => (await frame.first().getAttribute('src')) || '', { timeout: 10_000 })
      .toContain('#stencil=');
    await host.close();
  });

  test('pins and unpins a page image via the page API', async () => {
    const host = await openHost();
    // Pin the first image; the SW writes chrome.storage, the bridge mirrors it back, and
    // the entry's `pinned` flips true.
    await host.evaluate(() => window.stencil.images[0].pin());
    await expect.poll(async () => host.evaluate(() => window.stencil.images[0].pinned), { timeout: 10_000 }).toBe(true);

    await host.evaluate(() => window.stencil.images[0].unpin());
    await expect.poll(async () => host.evaluate(() => window.stencil.images[0].pinned), { timeout: 10_000 }).toBe(false);
    await host.close();
  });
});
