// Browser e2e for the VS Code hand-off: a `.stc` rides the SAME `#stencil=` fragment the
// Chrome extension uses, as a top-level `script` the shared codec ignores, and the app runs
// it against the picture the fragment brought (browser/js/core/launchController.js +
// browser/js/index.js). The script also lands in the one shared script buffer, so the window
// shows the source that acted when the user opens it — never by itself.
import { test, expect } from '@playwright/test';
import { gotoApp, expectModalOpen } from '../../helpers/boot.js';

const fragment = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));

const IMAGE_URL = 'https://cdn.example/handed-over.png';

// A real 8×8 PNG: big enough that a crop and a line resolve to something.
const PNG_8x8 = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFUlEQVR42mNk+M+ADzDhkxxVMKoAAI' +
  'tEAxWVc3AAAAAASUVORK5CYII=', 'base64');

const serveImage = (page) => page.route(IMAGE_URL, (route) =>
  route.fulfill({ contentType: 'image/png', body: PNG_8x8 }));

test('a handed-over script runs against the handed-over image', async ({ page }) => {
  await serveImage(page);
  await gotoApp(page, {
    motion: 'none',
    hash: fragment({ src: IMAGE_URL, name: 'handed-over.png', script: '@filter sepia\n' }),
  });

  // The picture arrived…
  await expect.poll(() => page.evaluate(() => !!window.stencil.imageSize)).toBe(true);
  // …and the script ran ON it, not ahead of it.
  await expect.poll(() => page.evaluate(() => window.stencil.filter)).toBe('sepia');
  // The fragment is consumed once: a reload must not re-run it.
  expect(await page.evaluate(() => location.hash)).toBe('');
});

test('the script window opens on the source that ran', async ({ page }) => {
  await serveImage(page);
  const script = '@crop 25%\n@filter bw\n';
  await gotoApp(page, { motion: 'none', hash: fragment({ src: IMAGE_URL, script }) });
  await expect.poll(() => page.evaluate(() => window.stencil.filter)).toBe('bw');

  await page.locator('#script-btn').click();
  await expectModalOpen(page, 'script-overlay');
  await expect(page.locator('#script-editor')).toHaveValue(script);
});

test('a script-only hand-off brings its own picture with @source', async ({ page }) => {
  await serveImage(page);
  await gotoApp(page, {
    motion: 'none',
    hash: fragment({ script: `@source ${IMAGE_URL}:\n  @filter invert\n` }),
  });

  await expect.poll(() => page.evaluate(() => !!window.stencil.imageSize)).toBe(true);
  await expect.poll(() => page.evaluate(() => window.stencil.filter)).toBe('invert');
});

test('a local @source is refused by the platform, in the app the user is looking at', async ({ page }) => {
  await gotoApp(page, { motion: 'none', hash: fragment({ script: '@source ./cat.png:\n  @filter bw\n' }) });

  // The browser has no filesystem: runScript says so on the line it happened. The window
  // stays shut — the script came from someone else's editor — and still holds the source.
  const toast = page.locator('#notify-balloon .notify-toast', { hasText: '@source needs a URL in the browser' });
  await expect(toast).toBeVisible({ timeout: 5000 });
  await expect(page.locator('#script-overlay')).toBeHidden();

  await page.locator('#script-btn').click();
  await expectModalOpen(page, 'script-overlay');
  await expect(page.locator('#script-editor')).toHaveValue('@source ./cat.png:\n  @filter bw\n');
});
