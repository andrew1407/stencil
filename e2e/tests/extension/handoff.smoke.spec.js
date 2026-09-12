// Extension e2e: load the unpacked MV3 extension in a persistent Chromium context and
// exercise the real ↔ editor seams — the page-scripting API, the new-tab AND in-page
// modal hand-offs, pin/unpin, and the CSS-background scan path. Extensions require a
// persistent context (the default `page` fixture can't provide one), so this suite
// manages its own. Runs headed; CI wraps the job in xvfb (see ci.yml / README).
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

const FIXTURE_URL = APP_URL + '__e2e__/page-with-image.html';
const EDITOR_URL = APP_URL;

test.describe('extension', () => {
  /** @type {import('@playwright/test').BrowserContext} */
  let context;
  /** @type {Awaited<ReturnType<typeof launchExtension>>['background']} */
  let background;

  test.beforeAll(async () => {
    ({ context, background } = await launchExtension());
    // Point the extension at the harness app (default editorUrl is :8080) and turn on the
    // opt-in page API; storage.onChanged (background.js) re-scopes the bridge and registers
    // the MAIN-world content script for subsequent navigations.
    const sw = await background();
    await sw.evaluate((editorUrl) => new Promise((r) =>
      chrome.storage.sync.set({ exposeWindowStencil: true, editorUrl }, r)), EDITOR_URL);
    await sleep(800);
  });

  test.afterAll(async () => { await context?.close(); });

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
    await Promise.all([editor.close(), host.close()]);
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

  // Right-clicking an element whose media is a CSS background (here the reported repro:
  // an inline-SVG data-URI background) has NO native image context, so the Stencil
  // actions are a group the worker reveals from the ctxTarget probe. Playwright can't
  // open Chrome's native context menu, so this drives the real path underneath it —
  // content-script probe → runtime message → chrome.contextMenus.update — by wrapping
  // the API in the service worker and reading back the flips it makes.
  test('a background-image element reveals the Stencil menu group (and plain text does not)', async () => {
    test.slow();
    const sw = await background();
    await sw.evaluate(() => {
      globalThis.__updates = [];
      const orig = chrome.contextMenus.update.bind(chrome.contextMenus);
      chrome.contextMenus.update = (id, props, cb) => {
        globalThis.__updates.push({ id, visible: props && props.visible });
        return orig(id, props, cb);
      };
      globalThis.__probes = [];
      chrome.runtime.onMessage.addListener((m) => {
        if (m && m.type === 'stencil-ctx') globalThis.__probes.push(m.data);
      });
    });
    const flips = (prefix) => sw.evaluate((p) => globalThis.__updates.filter((u) => u.id.startsWith(p)), prefix);
    const reset = () => sw.evaluate(() => { globalThis.__updates = []; globalThis.__probes = []; });

    const host = await openHost();
    await host.evaluate(() => {
      const svg = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'"
        + "%3E%3Ccircle cx='5' cy='5' r='4' fill='red'/%3E%3C/svg%3E";
      const d = document.createElement('div');
      d.id = 'svgbg';
      d.style.cssText = `width:120px;height:120px;background-image:url("${svg}")`;
      document.body.appendChild(d);
      const p = document.createElement('p');
      p.id = 'plaintext';
      p.textContent = 'nothing to grab here';
      document.body.appendChild(p);
    });
    await reset();

    // Merely HOVERING primes the probe — the group is revealed before any right-click,
    // which is what stops the reveal from racing Chrome's menu render (and from losing
    // outright when the MV3 worker has to wake up first). Keep the pointer moving while
    // polling, as a hand does: the probe is a document_idle content script, so a single
    // move fired before it attached would prime nothing.
    const box = await host.locator('#svgbg').boundingBox();
    let wiggle = 0;
    await expect.poll(async () => {
      await host.mouse.move(box.x + 20 + (wiggle++ % 4), box.y + 20);
      return (await flips('stencil-bg')).length;
    }, { timeout: 10_000 }).toBeGreaterThan(0);

    // The inline-SVG data URI resolves as the target (the scanner allows these now).
    const probed = await sw.evaluate(() => globalThis.__probes[globalThis.__probes.length - 1]);
    expect(probed.url.startsWith('data:image/svg+xml,')).toBeTruthy();

    // The group is revealed WITH ITS OWN ROOT, so the "Stencil" entry never renders empty.
    const shown = await flips('stencil-bg');
    const on = shown.filter((u) => u.visible === true).map((u) => u.id);
    expect(on).toContain('stencil-bg-root');
    expect(on).toContain('stencil-bg-open');
    expect(on).toContain('stencil-bg-crop');

    // …and over plain text the same path hides the whole group again — the failure mode
    // is "no Stencil entry", not an empty submenu.
    await reset();
    const p = await host.locator('#plaintext').boundingBox();
    await host.mouse.move(p.x + 5, p.y + 5);
    await host.dispatchEvent('#plaintext', 'contextmenu', { bubbles: true });
    await expect.poll(async () => (await flips('stencil-bg')).length, { timeout: 10_000 }).toBeGreaterThan(0);
    const offFlips = await flips('stencil-bg');
    expect(offFlips.every((u) => u.visible === false)).toBeTruthy();
    expect(offFlips.map((u) => u.id)).toContain('stencil-bg-root');

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
