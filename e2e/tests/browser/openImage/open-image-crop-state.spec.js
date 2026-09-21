// The Crop choice belongs to the PICTURE on screen and to the TAB that made it: editing a
// URL leaves the old picture up, and a tab switched away from and back keeps what it had.
// Both used to drop the row (and the rect) while the picture stayed, so a ticked Crop
// simply vanished. Desktop twin: OpenImageDialog::stalePreview / applyMode.
import { test, expect } from '@playwright/test';
import { gotoApp, APP_URL } from '../../../helpers/boot.js';
import { pngFile } from '../../../helpers/png.js';

const PICTURE = `${APP_URL}__e2e__/preview.png`;
const cropRow = (page) => page.locator('#open-image-crop-row');
const cropBox = (page) => page.locator('#open-image-crop-box');
const toggle = (page) => page.locator('#open-image-crop-toggle');

async function openModal(page) {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
}

test.describe('Open Image crop state', () => {
  test('choosing a NEW local file mid-crop hides the OLD box at once, not after the decode', async ({ page }) => {
    await openModal(page);
    await page.setInputFiles('#open-image-file', pngFile(400, 300, 'first.png'));
    await expect(cropRow(page)).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    // A file input fires no 'input' signal beforehand, so loadPreviewMedia's replacing branch is the
    // only thing that can hide the box — read back in the SAME task, or a retry waits out a timeout.
    const result = await page.evaluate(() => new Promise((resolve) => {
      const canvas = document.createElement('canvas');
      canvas.width = 500; canvas.height = 500;
      canvas.getContext('2d').fillStyle = '#a03050';
      canvas.getContext('2d').fillRect(0, 0, 500, 500);
      canvas.toBlob((blob) => {
        const file = new File([blob], 'second.png', { type: 'image/png' });
        const dt = new DataTransfer();
        dt.items.add(file);
        const input = document.getElementById('open-image-file');
        input.files = dt.files;
        input.dispatchEvent(new Event('change', { bubbles: true }));
        resolve(getComputedStyle(document.getElementById('open-image-crop-box')).display);
      });
    }));
    expect(result).toBe('none');
    // …and it comes back once the new picture actually lands.
    await expect(cropBox(page)).toBeVisible({ timeout: 10_000 });
  });


  test('editing the URL keeps the ticked Crop row over the picture it still shows', async ({ page }) => {
    await openModal(page);
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(cropRow(page)).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    await page.locator('#open-image-url').press('End');
    await page.locator('#open-image-url').type('?v=2');   // the text moves on…
    await expect(page.locator('#open-image-preview')).toBeVisible();   // …the picture stays
    await expect(cropRow(page)).toBeVisible();
    await expect(toggle(page)).toBeChecked();
  });

  test('a tab switched away from and back brings its crop back, rect and all', async ({ page }) => {
    await openModal(page);
    await page.setInputFiles('#open-image-file', pngFile(900, 1300));
    await expect(cropRow(page)).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    await page.locator('#oi-tab-blank').click();
    await expect(cropRow(page)).toBeHidden();
    await page.locator('#oi-tab-file').click();
    await expect(cropRow(page)).toBeVisible();
    await expect(toggle(page)).toBeChecked();
    await expect(cropBox(page)).toBeVisible();   // the rect, not just the tick
  });

  test('a tab switch never loses the picture, even with the URL retyped', async ({ page }) => {
    await openModal(page);
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(page.locator('#open-image-preview')).toBeVisible({ timeout: 10_000 });
    await page.locator('#open-image-url').press('End');
    await page.locator('#open-image-url').type('?v=2');       // the address moves on…
    await page.locator('#oi-tab-blank').click();
    await page.locator('#oi-tab-url').click();                // …away and back
    await expect(page.locator('#open-image-preview')).toBeVisible();
    await expect(page.locator('#open-image-preview-img')).toHaveJSProperty('complete', true);
  });

  test('the caption beside a checkbox toggles it, as its label should', async ({ page }) => {
    await openModal(page);
    await page.setInputFiles('#open-image-file', pngFile(900, 1300));
    await expect(cropRow(page)).toBeVisible({ timeout: 10_000 });
    await expect(toggle(page)).not.toBeChecked();
    await page.locator('label[for="open-image-crop-toggle"]').click();
    await expect(toggle(page)).toBeChecked();
    await page.locator('label[for="open-image-incognito"]').click();
    await expect(page.locator('#open-image-incognito')).toBeChecked();
  });
});

test.describe('Open Image crop rect persistence', () => {
  test('a dragged crop rect survives a tab switch, back to the same picture', async ({ page }) => {
    await openModal(page);
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(cropRow(page)).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    // Measured relative to the STAGE, so a layout shift elsewhere in the column never reads as a
    // divergence — only the crop rect's own position within it.
    const readRel = () => {
      const stage = document.getElementById('open-image-crop-stage').getBoundingClientRect();
      const b = document.getElementById('open-image-crop-box').getBoundingClientRect();
      return { x: b.x - stage.x, y: b.y - stage.y, w: b.width, h: b.height };
    };
    const before = await page.evaluate(readRel);
    // Drag well INTO the box first (its handles are near the edges and would resize
    // instead of move), then a real drag, small enough to stay inside a small preview.
    const box = await page.locator('#open-image-crop-box').boundingBox();
    const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
    await page.mouse.move(cx, cy);
    await page.mouse.down();
    await page.mouse.move(cx - 8, cy - 8, { steps: 4 });
    await page.mouse.up();
    const dragged = await page.evaluate(readRel);
    expect(Math.hypot(dragged.x - before.x, dragged.y - before.y)).toBeGreaterThan(3);

    // A differently-sized picture is what re-fires the decode-and-reset path; its own decode is
    // waited out explicitly so it cannot still be in flight and race the state under test.
    await page.setInputFiles('#open-image-file', pngFile(900, 700, 'other.png'));
    await expect(page.locator('#open-image-preview-img')).toHaveJSProperty('complete', true, { timeout: 10_000 });
    await page.waitForTimeout(200);
    await page.locator('#oi-tab-url').click();
    await expect(cropBox(page)).toBeVisible();
    await expect(page.locator('#open-image-preview-img')).toHaveJSProperty('complete', true, { timeout: 10_000 });
    await page.waitForTimeout(200);
    const restored = await page.evaluate(readRel);
    expect(Math.abs(restored.x - dragged.x)).toBeLessThan(2);
    expect(Math.abs(restored.y - dragged.y)).toBeLessThan(2);
  });
});

test.describe('Open Image video crop through an empty tab', () => {
  test('a URL video crop area survives a visit to an empty Local tab', async ({ page }) => {
    // cropState.iw/ih are shared by both tabs and zeroed by loadPreviewMedia on ANY tab visit, so
    // the "already ready" fast path must still re-decode to set them again.
    await gotoApp(page);
    await page.locator('#load-image-btn').click();
    await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    await page.locator('#oi-tab-file').click();   // empty — nothing chosen here
    await page.locator('#oi-tab-url').click();
    await expect(cropBox(page)).toBeVisible();
    await expect(toggle(page)).toBeChecked();
  });

  test('a URL crop area survives a visit to a LOCAL tab that actually loaded its own file', async ({ page }) => {
    // previewImg/previewVideo are shared between both tabs, so the fast path back to an already
    // loaded URL tab is only safe if nothing else has since loaded its own content into them.
    await openModal(page);
    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();
    const before = await page.evaluate(() => {
      const b = document.getElementById('open-image-crop-box').getBoundingClientRect();
      return { w: Math.round(b.width), h: Math.round(b.height) };
    });

    await page.locator('#oi-tab-file').click();
    await page.setInputFiles('#open-image-file', pngFile(1200, 400, 'wide.png'));
    await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    await page.locator('#oi-tab-url').click();
    await expect(cropBox(page)).toBeVisible();
    const after = await page.evaluate(() => {
      const stage = document.getElementById('open-image-crop-stage').getBoundingClientRect();
      const b = document.getElementById('open-image-crop-box').getBoundingClientRect();
      return { w: Math.round(b.width), h: Math.round(b.height),
               withinStage: b.width <= stage.width + 1 && b.height <= stage.height + 1 };
    });
    expect(after.withinStage).toBe(true);
    expect(after.w).toBe(before.w);
    expect(after.h).toBe(before.h);
  });

  test('a LOCAL crop area survives a visit to the URL tab that also loaded and cropped', async ({ page }) => {
    // Symmetric to "local clobbers url": the URL tab decoding its own picture into the shared
    // elements must not leave the Local tab's crop box mis-scaled or overflowing.
    await openModal(page);
    await page.setInputFiles('#open-image-file', pngFile(1018, 720, 'wide.png'));
    await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();
    const before = await page.evaluate(() => {
      const b = document.getElementById('open-image-crop-box').getBoundingClientRect();
      return { w: Math.round(b.width), h: Math.round(b.height) };
    });

    await page.locator('#oi-tab-url').click();
    await page.locator('#open-image-url').fill(PICTURE);
    await page.locator('#open-image-url-preview').click();
    await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
    await toggle(page).check();
    await expect(cropBox(page)).toBeVisible();

    await page.locator('#oi-tab-file').click();
    await expect(cropBox(page)).toBeVisible();
    const after = await page.evaluate(() => {
      const stage = document.getElementById('open-image-crop-stage').getBoundingClientRect();
      const b = document.getElementById('open-image-crop-box').getBoundingClientRect();
      return { w: Math.round(b.width), h: Math.round(b.height),
               withinStage: b.width <= stage.width + 1 && b.height <= stage.height + 1 };
    });
    expect(after.withinStage).toBe(true);
    expect(after.w).toBe(before.w);
    expect(after.h).toBe(before.h);
  });
});
