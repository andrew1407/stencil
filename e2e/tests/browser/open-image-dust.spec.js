// The Open Image dialog's preview arrival is a dust canvas over the media
// (browser/js/ui/openImageModal.js dustOver → motion/canvasDustStage.js). It is NOT a
// `.disintegrate-host`, so the shell's sweepDust never reached it: switching tab or closing
// the window mid-flight left the motes playing over whatever arrived next, with the veil
// they stood in for still down. Desktop twin: OpenImageDialog::cancelPreviewDust.
import { test, expect } from '@playwright/test';
import { gotoApp } from '../../helpers/boot.js';
import { pngFile } from '../../helpers/png.js';

const FILE = pngFile(320, 240, 'dust.png');
const CLOUDS = '#open-image-modal-overlay canvas.canvas-dust';

// Motion is deliberately ON: the cloud is the thing under test.
async function openWithPicture(page) {
  await gotoApp(page);
  await page.locator('#load-image-btn').click();
  await expect(page.locator('#open-image-modal-overlay')).toHaveClass(/modal-open/);
  await page.setInputFiles('#open-image-file', FILE);
  await expect(page.locator(CLOUDS).first()).toBeAttached({ timeout: 10_000 });   // mid-flight
}

// The press AND the read in one task: a cloud takes itself down when its own span runs
// out, so a retrying assertion cannot tell a cancel from simply waiting it out.
const pressAndRead = (page, id, sel) => page.evaluate(([btn, cloudSel]) => {
  const n = () => document.querySelectorAll(cloudSel).length;
  const before = n();
  document.getElementById(btn).click();
  return { before, after: n(),
           veiled: document.getElementById('open-image-preview-img').style.opacity === '0' };
}, [id, sel]);

test.describe('Open Image preview dust', () => {
  test('leaving the tab takes the arrival cloud with it, and lifts its veil', async ({ page }) => {
    await openWithPicture(page);
    const got = await pressAndRead(page, 'oi-tab-url', CLOUDS);
    expect(got.before).toBeGreaterThan(0);
    expect(got.after).toBe(0);
    expect(got.veiled).toBe(false);
  });

  test('closing the window takes it too', async ({ page }) => {
    await openWithPicture(page);
    const got = await pressAndRead(page, 'open-image-cancel', CLOUDS);
    expect(got.before).toBeGreaterThan(0);
    expect(got.after).toBe(0);
    expect(got.veiled).toBe(false);
  });
});
