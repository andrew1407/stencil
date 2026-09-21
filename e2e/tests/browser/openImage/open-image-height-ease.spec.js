// The box's own height ease must never FIGHT a row's own animation (the crop read-out's
// slide): a ResizeObserver notification mid-slide used to cancel-and-restart the box's
// flight on every one of the row's own frames, extending one 380ms ease into ~700ms+ of
// visibly janky, self-correcting motion. Desktop twin: the analogous scroll-bar fight in
// OpenImageDialog::animateHeightTo, fixed alongside this.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../../helpers/boot.js';
import { pngFile } from '../../../helpers/png.js';

test('toggling crop never starts a competing box ease while the read-out is sliding', async ({ page }) => {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
  await page.setInputFiles('#open-image-file', pngFile(2400, 1700));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await page.waitForTimeout(900);   // the arrival's own flights, out of the way

  const boxFlightMs = await page.evaluate(() => new Promise((resolve) => {
    const box = document.querySelector('#open-image-modal-overlay .app-modal');
    document.getElementById('open-image-crop-toggle').click();
    const started = performance.now();
    let sawAny = false;
    const check = () => {
      const running = box.getAnimations().length > 0;
      if (running) sawAny = true;
      // Settled once nothing is running AND the read-out's own row has landed too — a fight would
      // still be going at that point.
      const dims = document.getElementById('open-image-crop-dims');
      if (!running && dims.getAnimations().length === 0 && performance.now() - started > 30) {
        resolve(sawAny ? performance.now() - started : 0);
      } else {
        requestAnimationFrame(check);
      }
    };
    requestAnimationFrame(check);
  }));
  // A fight stretches the box's own flight well past its ~380ms clock; 0 means it never
  // had to run at all (the row's own slide was smooth enough on its own).
  expect(boxFlightMs).toBeLessThan(600);
});

// The generic `button { transition: color, … }` tripped the guard above on every tab switch, so
// the height snapped instead of easing: only the read-out's scripted flight may count.
test('a tab switch still eases the box, even though its own tab button is transitioning', async ({ page }) => {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
  await page.setInputFiles('#open-image-file', pngFile(2400, 1700));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await page.waitForTimeout(900);

  const midFlightHeights = await page.evaluate(() => new Promise((resolve) => {
    const box = document.querySelector('#open-image-modal-overlay .app-modal');
    const before = box.getBoundingClientRect().height;
    document.getElementById('oi-tab-blank').click();
    const samples = [];
    const start = performance.now();
    const tick = () => {
      samples.push(box.getBoundingClientRect().height);
      if (performance.now() - start < 300) requestAnimationFrame(tick);
      else resolve({ before, after: box.getBoundingClientRect().height, samples });
    };
    requestAnimationFrame(tick);
  }));
  // A snap shows only the start and end height; an ease passes through values strictly
  // between them on the way.
  const { before, after, samples } = midFlightHeights;
  expect(after).not.toBeCloseTo(before, 0);
  expect(samples.some((h) => h > Math.min(before, after) + 2 && h < Math.max(before, after) - 2))
    .toBe(true);
});
