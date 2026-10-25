// Extension e2e — a reload in real Chromium with a tab open (content/orphan.js): the reloaded
// content scripts run in a fresh isolated world, and the orphaned copies in the old one, their
// chrome.runtime gone, drop every listener. Each world's listeners are read over DevTools.
import { test, expect } from '@playwright/test';
import { SITE_URL } from '../../helpers/config.js';
import { launchExtension } from '../../helpers/extension.js';

// Every extension world in the tab: whether its runtime is live and the listeners it holds on
// `document` and `window`, by type ({ contextmenu: 1, message: 4, … }).
const extensionWorlds = async (page) => {
  const cdp = await page.context().newCDPSession(page);
  try {
    const contexts = [];
    cdp.on('Runtime.executionContextCreated', (e) => contexts.push(e.context));
    await cdp.send('Runtime.enable');
    const worlds = [];
    for (const c of contexts.filter((x) => x.origin?.startsWith('chrome-extension://'))) {
      const { result: live } = await cdp.send('Runtime.evaluate', { expression: '!!chrome?.runtime?.id', contextId: c.id });
      const listeners = {};
      for (const expression of ['document', 'window']) {
        const { result } = await cdp.send('Runtime.evaluate', { expression, contextId: c.id });
        for (const l of (await cdp.send('DOMDebugger.getEventListeners', { objectId: result.objectId })).listeners) {
          listeners[l.type] = (listeners[l.type] || 0) + 1;
        }
      }
      worlds.push({ live: live.value, listeners });
    }
    return worlds;
  } finally {
    await cdp.detach();
  }
};
const liveWorld = async (page) => (await extensionWorlds(page)).find((w) => w.live && w.listeners.contextmenu) ?? null;

// The CTX messages the probe sent this worker since `listenForCtx`.
const listenForCtx = (sw) => sw.evaluate(() => {
  globalThis.__e2eCtx = 0;
  chrome.runtime.onMessage.addListener((m) => { if (m?.type === 'stencil-ctx') globalThis.__e2eCtx += 1; });
});
const ctxHeard = (sw) => sw.evaluate(() => globalThis.__e2eCtx);

// The Extensions page's own reload. A command-line-loaded extension comes back from it disabled
// under Playwright's Chromium, so it is turned on again, as the page's toggle would.
const reloadExtension = async (context, id) => {
  const ext = await context.newPage();
  try {
    await ext.goto('chrome://extensions');
    await ext.evaluate((x) => new Promise((ok) => chrome.developerPrivate.reload(x, { failQuietly: true }, ok)), id);
    await expect.poll(() => ext.evaluate((x) => new Promise((ok) => chrome.developerPrivate.getExtensionInfo(x, (i) => {
      if (i?.state === 'DISABLED') chrome.management.setEnabled(x, true, () => ok('DISABLED'));
      else ok(i?.state);
    })), id), { timeout: 15_000 }).toBe('ENABLED');
  } finally {
    await ext.close();
  }
};

test.describe('extension: reload with a tab open', () => {
  test('the orphaned content scripts drop their listeners and the reloaded ones take over', async () => {
    const { context, background } = await launchExtension();
    try {
      let sw = await background();
      const id = new URL(sw.url()).host;
      // The editor bridge and the page API register for this origin too, so all three re-inject.
      await sw.evaluate((site) => chrome.storage.sync.set({ editorUrl: site, exposeWindowStencil: true }), SITE_URL);
      const page = await context.newPage();
      // editorBridge's three `message` listeners and pageApiBridge's one, beside the probe.
      await expect.poll(async () => {
        await page.goto(`${SITE_URL}__e2e__/page-with-image.html`);
        return (await liveWorld(page))?.listeners.message ?? 0;
      }, { timeout: 15_000 }).toBe(4);
      const before = (await liveWorld(page)).listeners;
      await listenForCtx(sw);
      await page.dispatchEvent('body', 'contextmenu');
      await expect.poll(() => ctxHeard(sw)).toBe(1);

      await reloadExtension(context, id);
      await expect.poll(() => context.serviceWorkers().length, { timeout: 15_000 }).toBeGreaterThan(0);
      sw = context.serviceWorkers()[0];
      await expect.poll(async () => (await liveWorld(page))?.listeners.message ?? 0, { timeout: 15_000 }).toBe(4);

      const worlds = await extensionWorlds(page);
      expect(worlds.filter((w) => !w.live).length, 'the reload left an orphaned world').toBeGreaterThan(0);
      for (const orphan of worlds.filter((w) => !w.live)) expect(orphan.listeners).toEqual({});
      const { pointermove: _once, ...steady } = before;   // the probe's one-shot pointermove may have fired
      expect((await liveWorld(page)).listeners).toMatchObject(steady);

      await listenForCtx(sw);
      await page.dispatchEvent('body', 'contextmenu');
      await expect.poll(() => ctxHeard(sw)).toBe(1);
    } finally {
      await context.close();
    }
  });
});
