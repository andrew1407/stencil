// Portable .stencil project files in the REAL browser app, driven through window.stencil
// (browser/js/console/stencilApi.js). Opening the shared cross-surface fixture
// (fixtures/project.stencil — the SAME file the CLI e2e renders) must decode its embedded
// image, adopt its layout (crop/rotation/lines), and apply its opt-in theme. Then a
// save → re-open round-trip through the facade preserves the project.
import { test, expect } from '@playwright/test';
import path from 'node:path';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { gotoApp } from '../../helpers/boot.js';

const FIXTURE = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../fixtures/project.stencil',
);

test('opens a .stencil project through the facade (image + layout + theme)', async ({ page }) => {
  await gotoApp(page);
  const text = readFileSync(FIXTURE, 'utf8');

  await page.evaluate((t) => window.stencil.openProjectFile(t), text);
  // openProjectFile loads the image asynchronously (FileReader); wait for the adopted line.
  await page.waitForFunction(() => window.stencil.lines && window.stencil.lines.length === 1);

  // The embedded layout's quarter-turn + full-frame crop → the 8x4 original renders 4x8,
  // exactly as the CLI e2e renders the same fixture (cross-surface parity).
  const size = await page.evaluate(() => window.stencil.imageSize);
  expect(size).toEqual({ width: 4, height: 8 });

  // The file opted into a theme (dark + violet), so opening it applies it.
  const theme = await page.evaluate(() => ({
    mode: document.documentElement.getAttribute('data-theme'),
    accent: document.documentElement.getAttribute('data-accent'),
  }));
  expect(theme).toEqual({ mode: 'dark', accent: 'violet' });
});

test('save → re-open round-trips a project through the facade', async ({ page }) => {
  await gotoApp(page);

  // Build a project in the app: a 40x24 blank with one line.
  const before = await page.evaluate(async () => {
    await window.stencil.blank('#3060c0', { size: { width: 40, height: 24 } });
    const s = window.stencil.imageSize;
    window.stencil.layout = {
      imageWidth: s.width, imageHeight: s.height,
      lines: [{ points: [{ x: 5, y: 5 }, { x: 30, y: 18 }], color: '#ff0000' }],
    };
    return { size: s, lines: window.stencil.lines.length };
  });
  expect(before.lines).toBe(1);

  // Capture the .stencil bytes the browser would save via the download-blob fallback
  // (force it by hiding the File System Access picker for this page), then re-open them.
  const download = await Promise.all([
    page.waitForEvent('download'),
    page.evaluate(() => {
      // Prefer the deterministic download path over a native save-file picker in CI.
      try { delete window.showSaveFilePicker; } catch { window.showSaveFilePicker = undefined; }
      return window.stencil.saveProjectFile();
    }),
  ]).then(([d]) => d);
  const stream = await download.createReadStream();
  let text = '';
  for await (const chunk of stream) text += chunk;
  expect(JSON.parse(text).format).toBe('stencil-project');

  await page.evaluate((t) => window.stencil.openProjectFile(t), text);
  await page.waitForFunction(() => window.stencil.lines && window.stencil.lines.length === 1);
  const after = await page.evaluate(() => window.stencil.imageSize);
  expect(after).toEqual(before.size);   // image + layout survived the round-trip
});
