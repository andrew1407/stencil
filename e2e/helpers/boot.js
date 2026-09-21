// Browser-app boot helpers. The app is driven through its `window.stencil` scripting
// facade (browser/js/console/stencilApi.js) — the same core methods the toolbar uses —
// so tests never click through brittle UI. `window.stencil` is defined right after the
// `stencil:ready` event in browser/js/index.js, so its presence is the readiness gate.
import { expect } from '@playwright/test';
import { APP_URL } from './config.js';

/** @typedef {import('../../browser/js/console/stencilApi.js').Stencil} Stencil */
/** @typedef {Window & { stencil?: Stencil }} StencilWindow */

export { APP_URL };

// A 1×1 PNG as a data: URL — a trivially loadable image for deep-link / handoff paths.
export const PNG_DATA_URL =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==';

// `motion` seeds ui/prefs.js before first paint (prePaintTheme.js stamps <html
// data-motion>), so 'none' means no entrance ever starts; motion specs must not pass it.
export async function gotoApp(page, { hash = '', motion = '' } = {}) {
  await page.addInitScript((m) => {
    try {
      localStorage.clear();
      if (m) localStorage.setItem('drawingApp_motion', JSON.stringify({ mode: m }));
    } catch { /* blocked */ }
  }, motion);
  await page.goto(APP_URL + hash);
  await page.waitForFunction(() => !!(/** @type {StencilWindow} */ (window).stencil), null, { timeout: 15_000 });
  return page;
}

// Returns the modal rows plus the row of the project that is NOT active — clicking the
// active row only closes the list.
export async function seedProjectsAndOpenList(page, { extra = 0 } = {}) {
  await page.evaluate(async (n) => {
    await window.stencil.blank('#ffffff', { size: { width: 200, height: 150 } });
    window.stencil.newEditor();
    await window.stencil.blank('#000000', { size: { width: 200, height: 150 } });
    for (let i = 0; i < n; i++) {
      window.stencil.newEditor();
      await window.stencil.blank('#ff0000', { size: { width: 200, height: 150 } });
    }
  }, extra);
  await page.locator('#projects-btn').click();
  const rows = page.locator('.project-row[data-id]');
  await expect(rows).toHaveCount(2 + extra, { timeout: 5000 });
  // The dialog flies in from its toolbar icon (base.js modalFromIcon, ~0.5s): geometry
  // read — or a tap aimed — mid-flight is scaled toward the icon, so wait it out.
  await settleModalAnimations(page, 'projects-modal-overlay');
  const idx = await rows.evaluateAll((els) => els.findIndex((e) => !e.classList.contains('project-active')));
  expect(idx, 'an inactive project row exists').toBeGreaterThanOrEqual(0);
  return { rows, target: rows.nth(idx) };
}

// The modal's open flight (modalFromIcon) scales the dialog out of its toolbar icon, so any
// box measured while it runs is mid-flight.
export const settleModalAnimations = (page, overlayId) => page.waitForFunction((oid) => {
  const overlay = document.getElementById(oid);
  if (!overlay || !overlay.classList.contains('modal-open')) return false;
  const box = overlay.querySelector('.app-modal') || overlay.firstElementChild;
  if (!box) return false;
  const anims = overlay.getAnimations({ subtree: true });
  if (anims.length) return anims.every((a) => a.playState === 'finished');
  // The flight may not have STARTED yet, so with no animation live accept only a box whose
  // on-screen rect matches its untransformed layout size.
  const r = box.getBoundingClientRect();
  return Math.abs(r.width - box.offsetWidth) < 1 && Math.abs(r.height - box.offsetHeight) < 1;
}, overlayId, { timeout: 5000 });

// Assert a modal overlay is open — the app adds `.modal-open` to `#<name>-modal-overlay`.
export async function expectModalOpen(page, overlayId) {
  await expect(page.locator(`#${overlayId}`)).toHaveClass(/modal-open/, { timeout: 5000 });
}

// Server-project ids visible to this page through the window.stencil facade.
export const serverProjectIds = (page) =>
  page.evaluate(async () => (await window.stencil.serverProjects()).map((r) => r.id));

// A client's local `current.id` can differ from the server id, so a fresh project is named by
// diffing the server set against a pre-create snapshot.
export async function waitForNewServerProjectId(page, baseline, { tries = 30, gapMs = 500 } = {}) {
  for (let i = 0; i < tries; i++) {
    const fresh = (await serverProjectIds(page)).find((id) => !baseline.has(id));
    if (fresh) return fresh;
    await page.waitForTimeout(gapMs);
  }
  return null;
}
