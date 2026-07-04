// Deeper browser-editor logic — the integration seams the smoke test doesn't reach:
// real pointer drawing on the canvas, the undo/redo history stack, crop with length
// tokens + the px↔page coordinate mapping, bulk settings apply, and save→reload
// persistence. All driven against the served app in a real browser.
import { test, expect } from '@playwright/test';
import { gotoApp, APP_URL } from '../../helpers/boot.js';

test('real pointer drawing builds a line on the canvas', async ({ page }) => {
  await gotoApp(page);
  await page.evaluate(async () => { await window.stencil.blank('#ffffff', { size: { width: 400, height: 300 } }); });

  // Enter draw mode and click distinct canvas points — the same canvasClick path the UI uses.
  await page.evaluate(() => window.stencil.startDrawing());
  const canvas = page.locator('#canvas');
  await canvas.click({ position: { x: 60, y: 50 } });
  await canvas.click({ position: { x: 160, y: 80 } });
  await canvas.click({ position: { x: 110, y: 150 } });
  await page.evaluate(() => window.stencil.stopDrawing());

  const lines = await page.evaluate(() => window.stencil.lines.length);
  const pts = await page.evaluate(() => window.stencil.lines[0]?.points.length ?? 0);
  expect(lines).toBe(1);
  expect(pts).toBeGreaterThanOrEqual(2);
});

test('undo / redo walk the history stack', async ({ page }) => {
  await gotoApp(page);
  const size = await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 400, height: 300 } });
    return window.stencil.imageSize;
  });

  await page.evaluate((s) => {
    window.stencil.layout = { imageWidth: s.width, imageHeight: s.height, lines: [{ points: [{ x: 10, y: 10 }, { x: 90, y: 60 }] }] };
  }, size);
  expect(await page.evaluate(() => window.stencil.lines.length)).toBe(1);

  await page.evaluate(() => window.stencil.undo());
  expect(await page.evaluate(() => window.stencil.lines.length)).toBe(0);

  await page.evaluate(() => window.stencil.redo());
  expect(await page.evaluate(() => window.stencil.lines.length)).toBe(1);
});

test('crop by percentage tokens + px↔page round-trip', async ({ page }) => {
  await gotoApp(page);
  const size = await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 400, height: 300 } });
    return window.stencil.imageSize;
  });

  // px → page → px is a linear round-trip (no formulas active).
  const roundTrip = await page.evaluate(() => {
    const p = window.stencil.px2Page({ x: 100, y: 50 });
    return window.stencil.page2Px(p);
  });
  expect(Math.abs(roundTrip.x - 100)).toBeLessThan(0.01);
  expect(Math.abs(roundTrip.y - 50)).toBeLessThan(0.01);

  // Crop to the middle 50% of the width via percentage edges.
  const cropped = await page.evaluate(() => {
    window.stencil.crop({ x1: '25%', x2: '75%' });
    return window.stencil.imageSize;
  });
  expect(Math.abs(cropped.width - Math.round(size.width * 0.5))).toBeLessThanOrEqual(2);
});

test('bulk apply() routes settings through the core', async ({ page }) => {
  await gotoApp(page);
  await page.evaluate(() => window.stencil.apply({ lineColor: '#123456', thickness: 5, filter: 'sepia', pageSize: 'a4' }));
  const s = await page.evaluate(() => ({
    lineColor: window.stencil.settings.lineColor,
    thickness: window.stencil.settings.thickness,
    filter: window.stencil.settings.filter,
    pageSize: String(window.stencil.settings.pageSize).toLowerCase(),
  }));
  expect(s.lineColor.toLowerCase()).toBe('#123456');
  expect(s.thickness).toBe(5);
  expect(s.filter).toBe('sepia');
  expect(s.pageSize).toBe('a4');
});

test('save → reload persists the project to storage', async ({ page }) => {
  // NOTE: a plain goto (not gotoApp) — Playwright gives each test a fresh context, so
  // localStorage starts empty, and we must NOT clear it on the reload below.
  await page.goto(APP_URL);
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });

  const name = 'persist-' + Date.now();
  await page.evaluate(async (nm) => {
    await window.stencil.blank('#ffffff', { size: { width: 320, height: 240 } });
    window.stencil.layout = { imageWidth: 320, imageHeight: 240, lines: [{ points: [{ x: 5, y: 5 }, { x: 50, y: 50 }] }] };
    if (window.stencil.current) window.stencil.current.name = nm;
    await window.stencil.save();
  }, name);

  await page.reload();
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });

  const found = await page.evaluate((nm) => !!window.stencil.getProjectByName(nm), name);
  expect(found, 'the saved project should survive a reload').toBeTruthy();
});
