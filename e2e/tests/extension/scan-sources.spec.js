// Extension e2e: prove the page scanner finds EVERY way an image URL can appear in a
// document, not just <img>/background. Loads the unpacked MV3 extension, opens a fixture
// page that carries one of each reference type (each pointing at the harness pixel with a
// distinct ?marker query so URLs stay distinct + countable), and reads the injected
// window.stencil page API. Extensions need a persistent context, so this suite manages its
// own (same pattern as handoff.smoke.spec.js). Runs headed; CI wraps the job in xvfb.
//
// The synchronous window.stencil API does NOT fetch the web-app manifest (that needs an
// async request), so manifest icons are covered by the pageImages.manifestIconUrls unit
// test + the inline copy in lib/imageScan.js, not here.
import { test, expect, chromium } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { APP_URL } from '../../helpers/config.js';

const EXT_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../extension');
const FIXTURE_URL = APP_URL + '__e2e__/all-image-sources.html';
const EDITOR_URL = APP_URL;

test.describe('extension image-source coverage', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;

  test.beforeAll(async () => {
    context = await chromium.launchPersistentContext('', {
      headless: false, // extensions load most reliably headed; CI runs under xvfb
      channel: 'chromium',
      args: [`--disable-extensions-except=${EXT_PATH}`, `--load-extension=${EXT_PATH}`],
    });
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

  async function openHost() {
    const host = await context.newPage();
    await host.goto(FIXTURE_URL);
    await host.waitForFunction(() => !!(/** @type {any} */ (window).stencil?.items), null, { timeout: 15_000 })
      .catch(async () => { await host.reload(); await host.waitForFunction(() => !!window.stencil?.items, null, { timeout: 15_000 }); });
    return host;
  }

  test('scans every HTML/CSS image reference on the page', async () => {
    const host = await openHost();
    const seen = await host.evaluate(() => ({
      images: window.stencil.images.map((e) => e.url),
      backgrounds: window.stencil.backgrounds.map((e) => e.url),
      icons: window.stencil.icons.map((e) => e.url),
    }));

    // Real content images (kind 'image', not meta) — <img>, srcset alternates,
    // <picture><source>, <input type=image>, <svg><image>/<feImage>.
    expect(seen.images).toEqual(expect.arrayContaining([
      expect.stringContaining('img=plain'),
      expect.stringContaining('srcset=1x'),
      expect.stringContaining('srcset=2x'),
      expect.stringContaining('picture=webp'),
      expect.stringContaining('input=btn'),
      expect.stringContaining('svg=image'),
      expect.stringContaining('svg=feimage'),
    ]));

    // Icon / metadata images (meta flag) — favicons, resource hints, social <meta>.
    // These are excluded from `images`; they live in `icons` under the "Icons & metadata" toggle.
    expect(seen.icons).toEqual(expect.arrayContaining([
      expect.stringContaining('icon=favicon'),
      expect.stringContaining('icon=apple'),
      expect.stringContaining('link=preload'),
      expect.stringContaining('link=prefetch'),
      expect.stringContaining('meta=og'),
      expect.stringContaining('meta=twitter'),
    ]));
    // …and they must NOT leak into the content-image list.
    expect(seen.images.some((u) => u.includes('icon=') || u.includes('meta='))).toBe(false);

    // CSS image references (kind 'background') — background-image, ::before content,
    // border-image, list-style-image, mask-image, cursor.
    expect(seen.backgrounds).toEqual(expect.arrayContaining([
      expect.stringContaining('css=bg'),
      expect.stringContaining('css=content'),
      expect.stringContaining('css=border'),
      expect.stringContaining('css=list'),
      expect.stringContaining('css=mask'),
      expect.stringContaining('css=cursor'),
    ]));

    await host.close();
  });

  test('the Icons & metadata toggle (kinds.meta) hides page-furniture images', async () => {
    const host = await openHost();
    const withMeta = await host.evaluate(() => window.stencil.items.length);
    const filtered = await host.evaluate(() => {
      window.stencil.kinds.meta = false;                 // same as unchecking "Icons & metadata"
      const items = window.stencil.items.length;
      const anyMetaLeft = window.stencil.items.some((e) => e.meta);
      window.stencil.kinds.meta = true;                  // restore
      return { items, anyMetaLeft };
    });
    expect(filtered.items).toBeLessThan(withMeta);       // furniture removed
    expect(filtered.anyMetaLeft).toBe(false);
    await host.close();
  });

  test('a #fragment paint/clip ref is NOT scanned as an image', async () => {
    const host = await openHost();
    // url(#id) targets (mask/clip/filter paint servers) must never surface as image rows.
    const hasFragment = await host.evaluate(() => window.stencil.items.some((e) => (e.url || '').includes('#')));
    expect(hasFragment).toBe(false);
    await host.close();
  });
});
