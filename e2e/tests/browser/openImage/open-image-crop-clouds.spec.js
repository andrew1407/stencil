// Open Image's crop ratio row in motion: switching to or from Custom animates its own field
// group, and disabling Crop scatters a cloud sized to the changed control, never the whole row
// or the Incognito row under it.
import { test, expect } from '@playwright/test';
import { pngFile } from '../../../helpers/png.js';
import {
  sizeRow, customGroup, toggle, chooseRatio, openModal,
} from '../../../helpers/openImage.js';

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
