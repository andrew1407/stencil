// Browser-app smoke: drive the real app in a real browser through window.stencil
// (browser/js/console/stencilApi.js) and the #stencil= deep-link path
// (DrawingApp.applyExternalLaunch). No collaboration server needed.
import { test, expect } from '@playwright/test';
import { gotoApp, expectModalOpen, PNG_DATA_URL } from '../../helpers/boot.js';

test('blank → draw → rotate → crop through the facade', async ({ page }) => {
  await gotoApp(page);

  // Blank canvas: the facade resolves once the image is in place. (Height may be
  // derived from the page proportion, so read back the actual dimensions.)
  const size = await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 400, height: 300 } });
    return window.stencil.imageSize;
  });
  expect(size.width).toBe(400);
  expect(size.height).toBeGreaterThan(0);

  // Inject a line via a layout payload (matching dims → no confirm dialog), then read it back.
  const lineCount = await page.evaluate((s) => {
    window.stencil.layout = {
      imageWidth: s.width, imageHeight: s.height,
      lines: [{ points: [{ x: 20, y: 20 }, { x: 120, y: 90 }], color: '#ff0000' }],
    };
    return window.stencil.lines.length;
  }, size);
  expect(lineCount).toBe(1);

  // Rotate a quarter turn: width/height swap.
  const rotated = await page.evaluate(() => { window.stencil.rotateRight(); return window.stencil.imageSize; });
  expect(rotated).toEqual({ width: size.height, height: size.width });

  // Crop the left 50%: width shrinks, height kept.
  const cropped = await page.evaluate(() => {
    window.stencil.crop({ x1: 0, x2: '50%' });
    return window.stencil.imageSize;
  });
  expect(cropped.width).toBeLessThan(rotated.width);
  expect(cropped.width).toBeGreaterThan(0);
});

test('projects modal opens', async ({ page }) => {
  await gotoApp(page);
  await page.locator('#projects-btn').click();
  await expectModalOpen(page, 'projects-modal-overlay');
});

test('#stencil= deep link loads the handed-off image', async ({ page }) => {
  const payload = encodeURIComponent(JSON.stringify({ dataUrl: PNG_DATA_URL, name: 'handoff.png' }));
  await gotoApp(page, { hash: `#stencil=${payload}` });

  // applyExternalLaunch decodes async; poll the facade until the project reports the image.
  await page.waitForFunction(() => !!window.stencil.current?.imageName, null, { timeout: 10_000 });
  const name = await page.evaluate(() => window.stencil.current.imageName);
  expect(name).toContain('handoff');

  // The fragment must be stripped from the URL after consumption.
  expect(new URL(page.url()).hash).toBe('');
});
