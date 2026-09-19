// The Open Image dialog's crop size READ-OUT ("900 x 1300 px · Portrait"): it slides into
// its place in the preview column and back out, so nothing under it is snapped by its
// height. Before this it was a display: none toggle — one 25px jump, gap included.
// Desktop twin: OpenImageDialog::slideCropDims, on the same clock.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';
import { pngFile } from '../../helpers/png.js';

// Motion is ON: the slide is what is under test.
async function openWithPicture(page) {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  // AFTER the shell is open: onOpen clears every field, the file input included.
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
  await page.setInputFiles('#open-image-file', pngFile(900, 1300));
  await expect(page.locator('#open-image-crop-row')).toBeVisible({ timeout: 10_000 });
  await page.waitForTimeout(600);   // the picture's own arrival, out of the way
}

// Click the toggle and sample the read-out's height across the flight, in the page.
const toggleAndSample = (page) => page.evaluate(async () => {
  const dims = document.getElementById('open-image-crop-dims');
  const out = [];
  document.getElementById('open-image-crop-toggle').click();
  for (let i = 0; i < 12; i++) {
    await new Promise((r) => setTimeout(r, 45));
    out.push(dims.offsetHeight);
  }
  return out;
});

test.describe('Open Image crop read-out', () => {
  test('the read-out slides in and out instead of appearing', async ({ page }) => {
    await openWithPicture(page);
    const inward = await toggleAndSample(page);
    const full = Math.max(...inward);
    expect(full).toBeGreaterThan(8);                     // it really arrived
    // The middle of the flight is what a display toggle cannot produce: heights strictly
    // between nothing and the whole line.
    expect(inward.filter((h) => h > 0 && h < full).length).toBeGreaterThanOrEqual(2);
    expect(inward[inward.length - 1]).toBe(full);        // …and it lands open

    const outward = await toggleAndSample(page);
    expect(outward.filter((h) => h > 0 && h < full).length).toBeGreaterThanOrEqual(2);
    expect(outward[outward.length - 1]).toBe(0);         // …and closed
    await expect(page.locator('#open-image-crop-dims')).toBeHidden();
  });

  // The Album/Portrait button must not display:none before its own cloud fires (a reflow with no
  // visible cause), nor stay opaque under the falling cloud for the whole flight.
  test('the Album/Portrait button reserves its space before showing, and fades WITH its own cloud', async ({ page }) => {
    await openWithPicture(page);
    const btn = page.locator('#open-image-crop-orientation');

    const shown = await page.evaluate(async () => {
      const el = document.getElementById('open-image-crop-orientation');
      const out = [];
      document.getElementById('open-image-crop-toggle').click();
      for (let i = 0; i < 4; i++) {
        await new Promise((r) => setTimeout(r, 20));
        const cs = getComputedStyle(el);
        out.push({ w: el.offsetWidth, opacity: cs.opacity });
      }
      return out;
    });
    // Space is reserved from the FIRST sampled frame — no width jump once the cloud starts.
    expect(shown.every((s) => s.w === shown[0].w)).toBe(true);
    expect(shown[0].w).toBeGreaterThan(0);
    // …and invisible the whole time the cloud is still forming.
    expect(shown.every((s) => Number(s.opacity) === 0)).toBe(true);
    await expect(btn).toHaveCSS('opacity', '1', { timeout: 2000 });

    const hidden = await page.evaluate(async () => {
      const el = document.getElementById('open-image-crop-orientation');
      const out = [];
      document.getElementById('open-image-crop-toggle').click();
      for (let i = 0; i < 4; i++) {
        await new Promise((r) => setTimeout(r, 20));
        out.push(Number(getComputedStyle(el).opacity));
      }
      return out;
    });
    // Faded out almost at once — never left fully opaque under its own falling cloud.
    expect(hidden[hidden.length - 1]).toBeLessThan(0.5);
  });
});
