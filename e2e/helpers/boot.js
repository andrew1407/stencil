// Browser-app boot helpers. The app is driven through its `window.stencil` scripting
// facade (browser/js/console/stencilApi.js) — the same core methods the toolbar uses —
// so tests never click through brittle UI. `window.stencil` is defined right after the
// `stencil:ready` event in browser/js/index.js, so its presence is the readiness gate.
import { expect } from '@playwright/test';
import { APP_URL } from './config.js';

export { APP_URL };

// A 1×1 PNG as a data: URL — a trivially loadable image for deep-link / handoff paths.
export const PNG_DATA_URL =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==';

// Navigate to the app, clear any persisted state so runs are independent, and wait
// until the scripting facade is live.
export async function gotoApp(page, { hash = '' } = {}) {
  await page.addInitScript(() => {
    try { localStorage.clear(); } catch { /* blocked */ }
  });
  await page.goto(APP_URL + hash);
  await page.waitForFunction(() => !!(/** @type {any} */ (window).stencil), null, { timeout: 15_000 });
  return page;
}

// Assert a modal overlay is open — the app adds `.modal-open` to `#<name>-modal-overlay`.
export async function expectModalOpen(page, overlayId) {
  await expect(page.locator(`#${overlayId}`)).toHaveClass(/modal-open/, { timeout: 5000 });
}

// Server-project ids visible to this page through the window.stencil facade.
export const serverProjectIds = (page) =>
  page.evaluate(async () => (await window.stencil.serverProjects()).map((r) => r.id));

// Poll the page's server projects until one appears whose id isn't in `baseline`, then
// return it (null if none within the window). A client's local `current.id` can differ
// from the server id, so the only reliable way to name a fresh project is to diff the
// server set against a pre-create snapshot.
export async function waitForNewServerProjectId(page, baseline, { tries = 30, gapMs = 500 } = {}) {
  for (let i = 0; i < tries; i++) {
    const fresh = (await serverProjectIds(page)).find((id) => !baseline.has(id));
    if (fresh) return fresh;
    await page.waitForTimeout(gapMs);
  }
  return null;
}
