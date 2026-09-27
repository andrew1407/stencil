// Open Image's crop ASPECT RATIO picker: a stand-in for the project's own page, plus a
// handful of plain ratios (every named ISO page shares one ratio, so listing the whole
// A/B/C series said nothing a single "Page" entry doesn't already say). Picking one only
// ever affects THIS preview's crop, never the project's own page.
import { test, expect } from '@playwright/test';
import { pngFile } from '../../../helpers/png.js';
import {
  sizeRow, sizeSel, customGroup, customW, customH, dims, toggle, chooseRatio, openModal,
} from '../../../helpers/openImage.js';

test('the ratio row appears with Crop and defaults to "Page"', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await expect(sizeRow(page)).toBeHidden();
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();
  await expect(sizeSel(page)).toHaveValue('page');
});

test('1:1 gives a perfectly square crop', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();

  await chooseRatio(page, '1:1');
  await expect.poll(async () => {
    const box = await page.locator('#open-image-crop-box').boundingBox();
    return Math.round(box.width) === Math.round(box.height);
  }).toBe(true);
});

test('Custom, with a genuinely different aspect, reshapes the crop — never the project page', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();
  const before = await dims(page).textContent();

  await chooseRatio(page, 'Custom');
  await expect(customGroup(page)).toBeVisible();
  await customW(page).fill('10');
  await customH(page).fill('30');
  await customH(page).dispatchEvent('input');
  await expect.poll(() => dims(page).textContent()).not.toBe(before);

  // Only the crop's own ratio moved — the project's own page-size control is untouched.
  await expect(page.locator('#page-size')).not.toHaveValue('custom');
});

test('1:1 and 2:3 give distinctly different crops', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();

  await chooseRatio(page, '1:1');
  const after11 = await dims(page).textContent();
  await chooseRatio(page, '2:3');
  await expect.poll(() => dims(page).textContent()).not.toBe(after11);
});

// "Page" reads as the default choice, not the project's physical page size, and the row lines up
// with Crop above it as one table (user report).
test('"Page" reads as the default, its row lines up with Crop above it, and the short list has no search box', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();

  const trigger = sizeSel(page).locator('xpath=..').locator('.accent-dd-trigger');
  await expect(trigger).toHaveText('Page — Default');

  const cropCtrlX = await page.locator('#open-image-crop-row .oi-crop-opt')
    .evaluate((el) => el.getBoundingClientRect().x);
  const sizeCtrlX = await page.locator('#open-image-crop-size-row .oi-crop-size')
    .evaluate((el) => el.getBoundingClientRect().x);
  expect(Math.abs(cropCtrlX - sizeCtrlX)).toBeLessThanOrEqual(1);

  await trigger.click();
  const menu = page.locator('.accent-dd-menu:visible');
  await expect(menu.locator('.accent-dd-search')).toHaveCount(0);   // four entries: nothing to search
  await expect(menu.locator('.accent-dd-opt', { hasText: '2:3' })).toBeVisible();
});

// The Custom W/H fields sit BESIDE the ratio selector. numericInput.js flips enhanced fields to
// type="text", which silently breaks any `input[type="number"]` width rule (user report).
test('the Custom W/H fields sit on the same row as the ratio selector, not wrapped below it', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await chooseRatio(page, 'Custom');
  await expect(customGroup(page)).toBeVisible();

  const trigger = sizeSel(page).locator('xpath=..').locator('.accent-dd-trigger');
  const selTop = await trigger.evaluate((el) => el.getBoundingClientRect().top);
  const wTop = await customW(page).evaluate((el) => el.getBoundingClientRect().top);
  expect(Math.abs(selTop - wTop)).toBeLessThanOrEqual(2);
});
