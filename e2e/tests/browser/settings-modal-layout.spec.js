// The Settings (keyboard shortcuts) dialog on a phone-sized screen.
//
// The hotkey table's four columns each carried a minimum width — the combo cell's 110px
// floor plus two keycap columns that never wrapped — which together came to more than
// a phone is wide. `.settings-body` scrolls horizontally, so nothing spilled off the page:
// the Default column and the per-row reset button were simply parked off to the side,
// reachable only by scrolling the table sideways. These pin the narrow-width rules that
// make the whole table fit, and the breakpoint that keeps them off the desktop dialog.
import { test, expect } from '@playwright/test';
import { gotoApp, settleModalAnimations } from '../../helpers/boot.js';

async function openSettings(page) {
  await gotoApp(page);
  await page.locator('#settings-btn').click();
  await expect(page.locator('#settings-modal-overlay')).toHaveClass(/modal-open/);
  await expect(page.locator('#hotkey-table .hotkey-row').first()).toBeVisible();
  // The dialog flies in from its icon (base.js modalFromIcon) — column widths read
  // mid-flight are scaled down, so measure only once the motion has settled.
  await settleModalAnimations(page, 'settings-modal-overlay');
}

// Table geometry + whether its scroller has anything hidden to the side.
const tableBox = (page) => page.evaluate(() => {
  const body = document.querySelector('.settings-body');
  const table = document.getElementById('hotkey-table');
  const b = table.getBoundingClientRect();
  return {
    right: b.right, width: b.width,
    scrollW: body.scrollWidth, clientW: body.clientWidth,
    heads: [...table.querySelectorAll('.hotkey-head > span')].map((th) => {
      const r = th.getBoundingClientRect();
      return { text: th.textContent.trim(), left: r.left, right: r.right, width: r.width };
    }),
  };
});

test.describe('settings modal: phone layout', () => {
  test.use({ viewport: { width: 393, height: 851 }, hasTouch: true, isMobile: true });

  test('the whole hotkey table fits — no sideways scrolling to reach a column', async ({ page }) => {
    await openSettings(page);
    const t = await tableBox(page);
    const w = page.viewportSize().width;

    expect(t.scrollW, 'the table scroller has nothing parked off to the side').toBe(t.clientW);
    expect(t.right, 'the table ends inside the screen').toBeLessThanOrEqual(w);
    // Every column — including Default and the reset-button column — is on screen and real.
    expect(t.heads.map((h) => h.text)).toEqual(['Action', 'Current shortcut', 'Default', '']);
    for (const h of t.heads) {
      expect(h.left, `${h.text || 'reset'} column starts on screen`).toBeGreaterThanOrEqual(0);
      expect(h.right, `${h.text || 'reset'} column ends on screen`).toBeLessThanOrEqual(w);
      expect(h.width, `${h.text || 'reset'} column has width`).toBeGreaterThan(20);
    }
    // Nothing pushed the page itself sideways either.
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(w);
  });
});

// The narrow rules are breakpoint-gated (max-width: 680px) — the desktop dialog keeps the
// roomy cell chrome it had before.
test.describe('settings modal: desktop layout is unaffected', () => {
  test.use({ viewport: { width: 1280, height: 800 } });

  test('the hotkey table keeps its full-size columns at 1280px', async ({ page }) => {
    await openSettings(page);
    const t = await tableBox(page);
    expect(t.scrollW).toBe(t.clientW);
    // The combo cell's 110px floor still applies here, so the Shortcut column stays wide.
    const shortcut = t.heads.find((h) => h.text === 'Current shortcut');
    expect(shortcut.width, 'Shortcut column keeps its desktop width').toBeGreaterThan(120);
    expect(await page.evaluate(() => getComputedStyle(document.querySelector('.hotkey-cell')).minWidth))
      .toBe('110px');
  });
});
