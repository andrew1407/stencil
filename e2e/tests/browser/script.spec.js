// Browser .stc e2e: the script window (browser/js/ui/scriptModal.js) and the facade's
// stencil.execScript, driven with the SHARED fixture corpus
// (browser/js/config/script/fixtures/cases.txt) — the same scripts the core is proved on.
// The highlight layer and the diagnostics strip both come from the core's own token stream,
// so a span in the editor and a run of the script can never disagree about a line.
import { test, expect } from '@playwright/test';
import { gotoApp, expectModalOpen, settleModalAnimations } from '../../helpers/boot.js';
import { readFileSync, writeFileSync } from 'node:fs';
import { stcCase } from '../../helpers/stcCases.js';

const LAYOUT_URL = 'https://example.com/layout.json';

const blank = (page, size) => page.evaluate(async (s) => {
  await window.stencil.blank('#ffffff', { size: s });
  return window.stencil.imageSize;
}, size);

const openScriptWindow = async (page) => {
  await page.locator('#script-btn').click();
  await expectModalOpen(page, 'script-overlay');
  await settleModalAnimations(page, 'script-overlay');
};

// Type into the editor the way a person does — `fill` raises the input event the highlighter
// listens for, so the layer under the textarea repaints.
const typeScript = async (page, text) => {
  await page.locator('#script-editor').fill(text);
  await expect(page.locator('#script-editor')).toHaveValue(text);
};

const lineCount = (page) => page.evaluate(() => window.stencil.lines.length);

test('the window highlights the script and Run applies it to the open project', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  const size = await blank(page, { width: 400, height: 300 });
  // The corpus tour ends with a @layout over the network: answer it with one line that fits
  // this canvas exactly, so the install needs no size confirmation.
  await page.route(LAYOUT_URL, (route) => route.fulfill({
    contentType: 'application/json',
    body: JSON.stringify({
      imageWidth: size.width,
      imageHeight: size.height,
      lines: [{ points: [{ x: 10, y: 10 }, { x: 40, y: 40 }], color: '#ff0000' }],
    }),
  }));

  await openScriptWindow(page);
  await typeScript(page, stcCase('tour-sourceless').script);
  // Coloured from the core's tokens: the directives carry their own class.
  await expect(page.locator('#script-highlight span.stk-directive').first()).toBeVisible();
  await expect(page.locator('#script-highlight span.stk-error')).toHaveCount(0);

  await page.locator('#script-run').click();
  // A clean run closes the window; @rect, @line and the fetched layout leave three marks.
  await expect(page.locator('#script-overlay')).not.toHaveClass(/modal-open/);
  await expect.poll(() => lineCount(page)).toBe(3);
});

test('a bad directive is named by line and nothing runs', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  await blank(page, { width: 400, height: 300 });
  const { script, diagnostics } = stcCase('err-unknown-directive');
  const [bad] = diagnostics;

  await openScriptWindow(page);
  await typeScript(page, script);
  await page.locator('#script-run').click();

  const strip = page.locator('#script-diag');
  await expect(strip).toHaveText(new RegExp(`Line ${bad.line}:${bad.col}\\b`));
  await expect(strip).toContainText("unknown directive '@crp'");
  await expect(strip).toHaveClass(/script-diag-error/);
  // The squiggle lands on the offending token, the window stays open for the fix, and the
  // project is untouched: a script with an error runs no op at all.
  await expect(page.locator('#script-highlight span.stk-error').first()).toBeVisible();
  await expectModalOpen(page, 'script-overlay');
  expect(await lineCount(page)).toBe(0);
});

test('stencil.execScript runs a script straight through the facade', async ({ page }) => {
  await gotoApp(page);
  const filter = await page.evaluate(async () => {
    await window.stencil.blank('#ffffff', { size: { width: 120, height: 80 } });
    await window.stencil.execScript('@filter bw');
    return window.stencil.filter;
  });
  expect(filter).toBe('bw');
});

test('the hotkey opens the window, Upload fills it and Download writes it back out', async ({ page }, testInfo) => {
  await gotoApp(page, { motion: 'none' });
  await blank(page, { width: 400, height: 300 });

  // Alt+Shift+S is the registered opener (browser/js/config/hotkeysConfig.json).
  await page.keyboard.press('Alt+Shift+S');
  await expectModalOpen(page, 'script-overlay');
  await settleModalAnimations(page, 'script-overlay');

  const { script } = stcCase('minimal-source');
  const uploaded = testInfo.outputPath('uploaded.stc');
  writeFileSync(uploaded, script);
  await page.setInputFiles('#script-upload', uploaded);
  await expect(page.locator('#script-editor')).toHaveValue(script);

  const download = await Promise.all([
    page.waitForEvent('download'),
    page.locator('#script-download').click(),
  ]).then(([d]) => d);
  expect(download.suggestedFilename()).toBe('stencil.stc');
  const saved = testInfo.outputPath('downloaded.stc');
  await download.saveAs(saved);
  expect(readFileSync(saved, 'utf8')).toBe(script);
});

test('a .stc dropped with the window closed runs on the open project', async ({ page }) => {
  await gotoApp(page, { motion: 'none' });
  await blank(page, { width: 400, height: 300 });
  expect(await lineCount(page)).toBe(0);

  // The document-level drop handler routes by file name (js/ui/bindings/dropPaste.js).
  await page.evaluate((text) => {
    const data = new DataTransfer();
    data.items.add(new File([text], 'dropped.stc', { type: 'text/plain' }));
    document.dispatchEvent(new DragEvent('drop', { dataTransfer: data, bubbles: true, cancelable: true }));
  }, '@rect (10,10) (100,80)\n');

  await expect.poll(() => lineCount(page)).toBe(1);
  // It ran rather than opening the editor: the window stayed shut.
  await expect(page.locator('#script-overlay')).not.toHaveClass(/modal-open/);
});
