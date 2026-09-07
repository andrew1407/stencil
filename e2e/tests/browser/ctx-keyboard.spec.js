// The canvas context menu walked with real key presses (desktop QMenu parity): ↑/↓ move
// over the rows, → opens a flyout onto its first row, ← closes it back onto the parent,
// Enter picks, Escape closes — and none of it reaches the canvas pan. The arrows used to
// fall through to controlsBinder.js's arrow pan; its scroll then closed the menu.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';

test('arrow keys walk the context menu instead of panning the canvas', async ({ page }) => {
  await gotoApp(page);
  // A zoomed-in image so the viewport CAN scroll — an arrow pan would show up here.
  await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 1600, height: 1200 } });
    window.stencil.zoomLevel = 200;
  });
  const viewport = page.locator('#canvas-viewport');
  const scrollOf = () => viewport.evaluate((el) => [el.scrollLeft, el.scrollTop]);
  // The zoom re-centres the view on its own tick — settle first, then pin a spot.
  await expect.poll(() => page.evaluate(() => window.stencil.zoomLevel)).toBe(200);
  await page.waitForTimeout(500);
  await viewport.evaluate((el) => { el.scrollLeft = 200; el.scrollTop = 150; });
  await page.waitForTimeout(300);
  const pinned = await scrollOf();
  expect(pinned[0]).toBeGreaterThan(0);

  // Right-click the middle of the VIEWPORT box (a locator click would first scroll its
  // target into view and move the very scroll this test pins).
  const box = await viewport.boundingBox();
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2, { button: 'right' });
  const menu = page.locator('#ctx-menu');
  await expect(menu).toHaveClass(/ctx-open/);
  // The highlighted row's own label (the first .ctx-label under it — a parent row's
  // nested flyout labels come after).
  const highlighted = page.locator('#ctx-menu .ctx-item.ctx-kb .ctx-label').first();

  await page.keyboard.press('ArrowDown');
  await expect(highlighted).toHaveText('Fit to Window');
  // The keyboard row is a hover: its icon plays its motion and the row content nudges.
  const kbRow = page.locator('#ctx-menu .ctx-item.ctx-kb');
  // The hover trigger (animations.css) flips --ic-on to 1 on the glyph and its parts —
  // hold-mode icons like Fit pose off that variable, settle-mode ones also animate.
  await expect.poll(() => kbRow.evaluate((row) => [...row.querySelectorAll(':scope > .ctx-icon *')]
    .some((n) => getComputedStyle(n).getPropertyValue('--ic-on').trim() === '1'))).toBe(true);
  await expect.poll(() => kbRow.evaluate((el) => getComputedStyle(el).paddingLeft)).toBe('17px');
  await page.keyboard.press('ArrowDown');
  await expect(highlighted).toHaveText('Image / Layout');

  // The first → only reveals the flyout (the highlight stays on the parent row, so ←
  // can fold it straight back); the second → enters it, here onto its first row.
  await page.keyboard.press('ArrowRight');
  const flyout = page.locator('#ctx-layout-sub');
  await expect(flyout).toHaveClass(/ctx-sub-visible/);
  await expect(highlighted).toHaveText('Image / Layout');
  await page.keyboard.press('ArrowRight');
  await expect(highlighted).toHaveText('Copy Image');
  await page.keyboard.press('ArrowDown');
  await expect(highlighted).toHaveText('Paste Image');

  // ← closes it back onto the parent row; the root menu stays.
  await page.keyboard.press('ArrowLeft');
  await expect(flyout).not.toHaveClass(/ctx-sub-visible/);
  await expect(highlighted).toHaveText('Image / Layout');
  await expect(menu).toHaveClass(/ctx-open/);

  // ↑ wraps from the first row to the last.
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await expect(highlighted).toHaveText('Tooltip');

  // Enter picks the highlighted row: Show Points toggles.
  const before = await page.evaluate(() => window.stencil.showPoints);
  for (let i = 0; i < 20 && (await highlighted.textContent()) !== 'Show Points'; i++) {
    await page.keyboard.press('ArrowDown');
  }
  await expect(highlighted).toHaveText('Show Points');
  await page.keyboard.press('Enter');
  await expect.poll(() => page.evaluate(() => window.stencil.showPoints)).toBe(!before);

  // A flyout with controls: the first → reveals Style, the second lands in its first
  // control, and Tab walks on through the spinners.
  for (let i = 0; i < 20 && (await highlighted.textContent()) !== 'Style'; i++) {
    await page.keyboard.press('ArrowDown');
  }
  await page.keyboard.press('ArrowRight');
  const style = page.locator('#ctx-style-sub');
  await expect(style).toHaveClass(/ctx-sub-visible/);
  await expect(highlighted).toHaveText('Style');
  const focusedId = () => page.evaluate(() => document.activeElement?.id || '');
  await page.keyboard.press('ArrowRight');
  await expect.poll(focusedId).toBe('ctx-point-size');
  await page.keyboard.press('Tab');
  await expect.poll(focusedId).toBe('ctx-thickness');
  await page.keyboard.press('Shift+Tab');
  await expect.poll(focusedId).toBe('ctx-point-size');
  await page.keyboard.press('Shift+Tab');   // …and wraps to the last control, a radio
  await expect.poll(() => page.evaluate(() => document.activeElement?.name || '')).toBe('ctxLineStyle');
  await expect(menu).toHaveClass(/ctx-open/);
  // ← from a control (not a text field) folds the flyout back onto its parent row.
  await page.keyboard.press('ArrowLeft');
  await expect(style).not.toHaveClass(/ctx-sub-visible/);
  await expect(highlighted).toHaveText('Style');
  // Walking off a parent row folds its flyout too.
  await page.keyboard.press('ArrowRight');
  await expect(style).toHaveClass(/ctx-sub-visible/);
  await page.keyboard.press('ArrowDown');
  await expect(style).not.toHaveClass(/ctx-sub-visible/);
  await expect(highlighted).toHaveText('Image Filter');

  // A checkbox flyout: the second → into Tooltip lands on its first checkbox, and the
  // ring that shows it is the accent box-shadow (appearance:none dropped the native one).
  for (let i = 0; i < 20 && (await highlighted.textContent()) !== 'Tooltip'; i++) {
    await page.keyboard.press('ArrowDown');
  }
  await page.keyboard.press('ArrowRight');
  await expect(page.locator('#ctx-tooltip-sub')).toHaveClass(/ctx-sub-visible/);
  await page.keyboard.press('ArrowRight');
  await expect.poll(focusedId).toBe('ctx-tt-enabled');
  expect(await page.locator('#ctx-tt-enabled').evaluate((el) => getComputedStyle(el).boxShadow)).not.toBe('none');

  await page.keyboard.press('Escape');
  await expect(menu).not.toHaveClass(/ctx-open/);
  // Every arrow above went to the menu — the viewport never moved…
  expect(await scrollOf()).toEqual(pinned);
  // …and the pan itself is live: the same key HELD with the menu closed scrolls it
  // (the pan runs per animation frame while the key is down — a tap releases first).
  await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(200);
  await page.keyboard.up('ArrowRight');
  await expect.poll(async () => (await scrollOf())[0]).toBeGreaterThan(pinned[0]);
});
