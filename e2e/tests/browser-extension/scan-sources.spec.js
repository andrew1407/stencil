// Extension e2e: the page scanner finds EVERY way an image URL can appear in a document. A
// fixture page carries one of each reference type (each pointing at the harness pixel with a
// distinct query string, so URLs stay distinct and countable) and the injected window.stencil
// page API is read back. Manages its own persistent context; runs headed. The synchronous API
// does not fetch the web-app manifest, so manifest icons are covered by unit tests instead.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const FIXTURE_URL = APP_URL + '__e2e__/all-image-sources.html';
const EDITOR_URL = APP_URL;

test.describe('extension image-source coverage', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;

  test.beforeAll(async () => {
    const ext = await launchExtension();
    context = ext.context;
    const sw = await ext.background();
    await sw.evaluate((editorUrl) => new Promise((r) =>
      chrome.storage.sync.set({ exposeWindowStencil: true, editorUrl }, r)), EDITOR_URL);
    await sleep(800);
  });

  test.afterAll(async () => { await context?.close(); });

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
