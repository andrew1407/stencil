// Open Image's crop Album/Portrait button: one fixed width across the toggle, pinned to its own
// wider face, and a click turns its glyph even while the pointer still rests on it.
import { test, expect } from '@playwright/test';
import { pngFile } from '../../../helpers/png.js';
import { toggle, openModal } from '../../../helpers/openImage.js';

// An auto-width button resized on every press — "Album" and "Portrait" are different
// lengths — a visible jump on a toggle that changes no layout otherwise (user report).
test('the Album/Portrait button keeps one fixed width across the toggle', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  const btn = page.locator('#open-image-crop-orientation');
  await expect(btn).toBeVisible();
  const w1 = await btn.evaluate((el) => el.getBoundingClientRect().width);
  await btn.click();
  await page.waitForTimeout(700);
  const w2 = await btn.evaluate((el) => el.getBoundingClientRect().width);
  expect(w2).toBe(w1);
});

// The pin must be the WIDER face's own real width (js/ui/motion.js pinWidestFace; desktop twin:
// OpenImageDialog maxes both sizeHints) — a bare CSS guess cramps one of the words (user report).
test('the Album/Portrait button is pinned to its own wider face, not a guessed width', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  const btn = page.locator('#open-image-crop-orientation');
  await expect(btn).toBeVisible();

  const { pinned, naturalAlbum, naturalPortrait } = await btn.evaluate((el) => {
    const pinned = el.getBoundingClientRect().width;
    const html0 = el.innerHTML, w0 = el.style.width;
    el.style.width = 'auto';
    el.innerHTML = el.innerHTML.replace(/Portrait|Album/, 'Album');
    const naturalAlbum = el.getBoundingClientRect().width;
    el.innerHTML = el.innerHTML.replace(/Portrait|Album/, 'Portrait');
    const naturalPortrait = el.getBoundingClientRect().width;
    el.innerHTML = html0;
    el.style.width = w0;
    return { pinned, naturalAlbum, naturalPortrait };
  });
  expect(pinned).toBeGreaterThanOrEqual(Math.max(naturalAlbum, naturalPortrait) - 1);
  expect(pinned).toBeLessThanOrEqual(Math.max(naturalAlbum, naturalPortrait) + 2);
});

// iconHover.css's `:hover` rule reads --ic-play at higher specificity and falls back to "none",
// cancelling a click's own turn while the pointer still rests on the button (user report).
test('clicking the Album/Portrait button turns its glyph even while the pointer rests on it', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  const btn = page.locator('#open-image-crop-orientation');
  await expect(btn).toBeVisible();
  await btn.hover();
  await btn.click();

  const running = await page.evaluate(() => {
    const svg = document.getElementById('open-image-crop-orientation').querySelector('.ic');
    return svg.getAnimations().some((a) => a.playState === 'running');
  });
  expect(running).toBe(true);
});
