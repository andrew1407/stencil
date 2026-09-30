// A video's crop area belongs to its tab too: a URL video's crop survives a visit to an empty
// Local tab or one that loaded its own file, and a Local crop survives the URL tab's own crop.
// Desktop twin: OpenImageDialog::stalePreview / applyMode.
import { test, expect } from '@playwright/test';
import { gotoApp, SITE_URL } from '../../../helpers/boot.js';
import { pngFile } from '../../../helpers/png.js';
import { cropBox, openModal, toggle } from '../../../helpers/openImage.js';

const PICTURE = `${SITE_URL}__e2e__/preview.png`;

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
