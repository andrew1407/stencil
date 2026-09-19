// Modal popovers (js/ui/popover.js + base.js wireModalShell): every toolbar icon that opens a
// covering modal also opens a COMPACT anchored version on dblclick / right-click, closed by
// Escape or a click outside; single click keeps the full modal. The gesture matrix is unit-tested
// DOM-free in browser/tests/popover.test.js — this pins the real-DOM classes, placement and both
// close routes.
import { test, expect } from '@playwright/test';
import { gotoApp, settleModalAnimations } from '../../helpers/boot.js';

const overlayState = (page, id) => page.evaluate((oid) => {
  const overlay = document.getElementById(oid);
  const box = overlay.querySelector('.app-modal') || overlay.firstElementChild;
  const r = box.getBoundingClientRect();
  return {
    open: overlay.classList.contains('modal-open'),
    popover: overlay.classList.contains('modal-popover'),
    background: getComputedStyle(overlay).backgroundColor,
    left: r.left,
    top: r.top,
  };
}, id);

test.describe('modal popovers', () => {
  test.beforeEach(async ({ page }) => {
    await gotoApp(page);
  });

  test('dblclick on the icon opens the compact popover next to it; Escape closes it', async ({ page }) => {
    await page.dblclick('#projects-btn');
    // The box flies in from the icon (base.js modalFromIcon) — measure it settled.
    await settleModalAnimations(page, 'projects-modal-overlay');
    const s = await overlayState(page, 'projects-modal-overlay');
    expect(s.open && s.popover).toBe(true);
    // Anchored: left edges aligned with the icon, box just below it — not centred.
    const anchor = await page.evaluate(() => {
      const r = document.getElementById('projects-btn').getBoundingClientRect();
      return { left: r.left, bottom: r.bottom };
    });
    expect(Math.abs(s.left - anchor.left)).toBeLessThan(60);   // clamped at most, never centred
    expect(s.top).toBeGreaterThan(anchor.bottom);
    // The page stays visible: the popover backdrop is transparent, not the dim overlay.
    expect(s.background).toBe('rgba(0, 0, 0, 0)');

    await page.keyboard.press('Escape');
    const closed = await overlayState(page, 'projects-modal-overlay');
    expect(closed.open).toBe(false);
    // The popover shape class stays on through the fly-back-to-icon close animation
    // (base.js modalToIcon, ~340ms) — poll it away rather than racing the flight.
    await expect.poll(async () => (await overlayState(page, 'projects-modal-overlay')).popover).toBe(false);
  });

  test('right-click opens the popover; a click outside closes it', async ({ page }) => {
    await page.click('#connect-btn', { button: 'right' });
    expect((await overlayState(page, 'connect-modal-overlay')).popover).toBe(true);
    await page.mouse.click(900, 700);   // the transparent overlay still catches outside clicks
    expect((await overlayState(page, 'connect-modal-overlay')).open).toBe(false);
  });

  test('a single click still opens the FULL covering modal, and only after the dblclick interval', async ({ page }) => {
    await page.click('#projects-btn');
    // The click defers one double-click interval (projects-list rule) so the full modal
    // can never flash under a dblclick; after it, the ordinary centred modal is up.
    await expect.poll(async () => (await overlayState(page, 'projects-modal-overlay')).open).toBe(true);
    const s = await overlayState(page, 'projects-modal-overlay');
    expect(s.popover).toBe(false);
    expect(s.background).not.toBe('rgba(0, 0, 0, 0)');   // the dimmed backdrop is back
    await page.keyboard.press('Escape');
  });

  test('the settings modal (own-id shell) popovers too', async ({ page }) => {
    await page.dblclick('#settings-btn');
    const s = await overlayState(page, 'settings-modal-overlay');
    expect(s.open && s.popover).toBe(true);
    await page.keyboard.press('Escape');
    expect((await overlayState(page, 'settings-modal-overlay')).open).toBe(false);
  });
});
