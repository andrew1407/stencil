// Browser e2e for the VS Code hand-off: a `.stc` rides the SAME `#stencil=` fragment the
// Chrome extension uses, as a top-level `script` the shared codec ignores. Whatever `scriptMode`
// the sender wrote, it opens in the Script window over the picture the fragment brought
// (browser/js/core/launch/controller.js + browser/js/index.js) and runs only on Run.
import { test, expect } from '@playwright/test';
import { gotoApp, expectModalOpen } from '../../../helpers/boot.js';

const fragment = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));

const IMAGE_URL = 'https://cdn.example/handed-over.png';

// A real 8×8 PNG: big enough that a crop and a line resolve to something.
const PNG_8x8 = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFUlEQVR42mNk+M+ADzDhkxxVMKoAAI' +
  'tEAxWVc3AAAAAASUVORK5CYII=', 'base64');

const serveImage = (page) => page.route(IMAGE_URL, (route) =>
  route.fulfill({ contentType: 'image/png', body: PNG_8x8 }));

for (const scriptMode of ['run', 'open', undefined]) {
  test(`a "${scriptMode ?? 'no mode'}" hand-off opens the script in its window and runs nothing`, async ({ page }) => {
    await serveImage(page);
    const script = '@filter sepia\n';
    await gotoApp(page, { motion: 'none', hash: fragment({ src: IMAGE_URL, script, scriptMode }) });

    await expectModalOpen(page, 'script-overlay');
    await expect(page.locator('#script-editor')).toHaveValue(script);
    await expect.poll(() => page.evaluate(() => !!window.stencil.imageSize)).toBe(true);
    expect(await page.evaluate(() => window.stencil.filter)).not.toBe('sepia');
    // The fragment is consumed once: a reload must not bring it back.
    expect(await page.evaluate(() => location.hash)).toBe('');

    await page.locator('#script-run').click();
    await expect.poll(() => page.evaluate(() => window.stencil.filter)).toBe('sepia');
  });
}

test('a script-only hand-off brings its own picture with @source, on Run', async ({ page }) => {
  await serveImage(page);
  await gotoApp(page, {
    motion: 'none',
    hash: fragment({ script: `@source ${IMAGE_URL}:\n  @filter invert\n` }),
  });

  await expectModalOpen(page, 'script-overlay');
  expect(await page.evaluate(() => !!window.stencil.imageSize)).toBe(false);
  await page.locator('#script-run').click();
  await expect.poll(() => page.evaluate(() => !!window.stencil.imageSize)).toBe(true);
  await expect.poll(() => page.evaluate(() => window.stencil.filter)).toBe('invert');
});

test('a local @source is refused by the platform when the user runs it', async ({ page }) => {
  await gotoApp(page, { motion: 'none', hash: fragment({ script: '@source ./cat.png:\n  @filter bw\n' }) });

  await expectModalOpen(page, 'script-overlay');
  await expect(page.locator('#script-editor')).toHaveValue('@source ./cat.png:\n  @filter bw\n');
  await page.locator('#script-run').click();
  const toast = page.locator('#notify-balloon .notify-toast', { hasText: '@source needs a URL in the browser' });
  await expect(toast).toBeVisible({ timeout: 5000 });
});
