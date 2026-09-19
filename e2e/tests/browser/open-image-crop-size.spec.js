// Open Image's crop ASPECT RATIO picker: a stand-in for the project's own page, plus a
// handful of plain ratios (every named ISO page shares one ratio, so listing the whole
// A/B/C series said nothing a single "Page" entry doesn't already say). Picking one only
// ever affects THIS preview's crop, never the project's own page.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';
import { pngFile } from '../../helpers/png.js';

const sizeRow = (page) => page.locator('#open-image-crop-size-row');
const sizeSel = (page) => page.locator('#open-image-crop-size');
const customGroup = (page) => page.locator('#open-image-crop-size-custom');
const customW = (page) => page.locator('#open-image-crop-size-w');
const customH = (page) => page.locator('#open-image-crop-size-h');
const dims = (page) => page.locator('#open-image-crop-dims');
const toggle = (page) => page.locator('#open-image-crop-toggle');

// Every <select> wears js/ui/customSelect.js — the native node is hidden but still the source of
// truth, so choosing its option through the rendered menu is what drives the change handler.
async function chooseRatio(page, text) {
  await sizeSel(page).locator('xpath=..').locator('.accent-dd-trigger').click();
  const menu = page.locator('.accent-dd-menu:visible');
  await menu.locator('.accent-dd-opt', { hasText: text }).click();
}

async function openModal(page) {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
}

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

// Switching to/from Custom used to be a plain display toggle — no cloud of its own, unlike
// every other arrival/departure in this dialog (user report).
test('switching to/from Custom animates its own field group, not just the row', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();

  const switchAndSample = (val) => page.evaluate(async (v) => {
    const wrap = document.getElementById('open-image-crop-size').closest('.cs-dd');
    wrap.querySelector('.accent-dd-trigger').click();
    const menu = document.querySelector('.accent-dd-menu:not([hidden])');
    menu.querySelector(`.accent-dd-opt[data-value="${v}"]`).click();
    let saw = false;
    for (let i = 0; i < 16; i++) {
      await new Promise((r) => setTimeout(r, 40));
      if (document.querySelector('.disintegrate-host')) saw = true;
    }
    return saw;
  }, val);

  expect(await switchAndSample('custom')).toBe(true);
  await expect(customGroup(page)).toBeVisible();
  expect(await switchAndSample('page')).toBe(true);
  await expect(customGroup(page)).toBeHidden();
});

// The row's cloud covers its own control only: carrying the Custom fields along made a box tall
// enough to scatter over the Incognito row under it (user report).
test('disabling Crop while Custom is picked keeps its cloud off the Incognito row', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();
  await chooseRatio(page, 'Custom');
  await expect(customGroup(page)).toBeVisible();
  await page.waitForTimeout(700);   // the arrival settles before the disable under test

  const overshoot = await page.evaluate(async () => {
    const incogTop = document.getElementById('open-image-incognito-row').getBoundingClientRect().top;
    document.getElementById('open-image-crop-toggle').click();
    let maxBottom = 0;
    for (let i = 0; i < 16; i++) {
      await new Promise((r) => setTimeout(r, 40));
      for (const host of document.querySelectorAll('.disintegrate-host')) {
        const b = host.getBoundingClientRect().bottom;
        if (b > maxBottom) maxBottom = b;
      }
    }
    return maxBottom - incogTop;
  });
  expect(overshoot).toBeLessThanOrEqual(4);
});

// The cloud is sized to the changed control, not the row — a plain <div> fills its container and
// spanned the whole modal (user report).
test('disabling Crop scatters a cloud no wider than the selector, not the whole row', async ({ page }) => {
  await openModal(page);
  await page.setInputFiles('#open-image-file', pngFile(600, 800, 'tall.png'));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await toggle(page).check();
  await expect(sizeRow(page)).toBeVisible();
  await page.waitForTimeout(700);

  const { maxWidth, ctrlWidth } = await page.evaluate(async () => {
    const ctrl = document.querySelector('#open-image-crop-size-row .oi-crop-size');
    const ctrlWidth = ctrl.getBoundingClientRect().width;
    document.getElementById('open-image-crop-toggle').click();
    let maxWidth = 0;
    for (let i = 0; i < 16; i++) {
      await new Promise((r) => setTimeout(r, 40));
      for (const host of document.querySelectorAll('.disintegrate-host'))
        maxWidth = Math.max(maxWidth, host.getBoundingClientRect().width);
    }
    return { maxWidth, ctrlWidth };
  });
  expect(maxWidth).toBeLessThanOrEqual(ctrlWidth + 2);
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
