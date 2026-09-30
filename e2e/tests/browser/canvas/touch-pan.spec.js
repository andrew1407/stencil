// One-finger canvas pan on a phone (browser/js/core/touch/pan.js): a press on empty canvas that
// wanders past the tap tolerance scrolls the zoomed picture under the finger, while a still tap
// stays a tap. Driven by a real CDP finger, so the browser's own scroll never takes the gesture.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';
import { finger } from '../../../helpers/drag.js';

const scroll = (page) => page.evaluate(() => {
  const v = document.getElementById('canvas-viewport');
  return { left: v.scrollLeft, top: v.scrollTop, room: v.scrollWidth - v.clientWidth };
});

test.describe('canvas: one-finger touch pan', () => {
  test.use({ viewport: { width: 390, height: 780 }, hasTouch: true, isMobile: true });

  test('a drag on empty canvas pans the zoomed picture; a tap does not', async ({ page }) => {
    await gotoApp(page, { motion: 'none' });
    await page.evaluate(async () => {
      await window.stencil.blank('#ffffff', { size: { width: 1600, height: 1200 } });
      window.stencil.zoom(4);
    });
    // The zoom eases in: wait for the scroll room to stop growing before measuring.
    await expect.poll(async () => {
      const a = await scroll(page);
      await page.waitForTimeout(200);
      const b = await scroll(page);
      return a.room > 200 && a.room === b.room && a.left === b.left;
    }).toBe(true);

    // The phone layout stacks the canvas under the toolbar: bring it on screen first.
    const viewport = page.locator('#canvas-viewport');
    await viewport.scrollIntoViewIfNeeded();
    const box = await viewport.boundingBox();
    const cx = box.x + box.width / 2;
    const cy = box.y + box.height / 2;
    const before = await scroll(page);

    await page.touchscreen.tap(cx, cy);
    expect(await scroll(page)).toEqual(before);

    const f = await finger(page);
    await f.down(cx, cy);
    await f.glide(cx, cy, cx - 120, cy - 40);
    await f.up(cx - 120, cy - 40);
    const after = await scroll(page);
    expect(after.left - before.left).toBeGreaterThan(100);
    expect(after.top - before.top).toBeGreaterThan(30);
    expect(await page.evaluate(() => window.stencil.lines.length)).toBe(0);
  });
});
