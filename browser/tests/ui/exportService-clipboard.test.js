// Clipboard copy variants and the pasted-layout prompt/install paths of ExportService
// (js/core/service.js). Split from exportService.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ExportService, makeApp, notifications, reset, lastNote } from '../helpers/exportServiceRig.js';
// ── copyImageToClipboard: variant labels + the split-compare special case ──
const fakeOffscreen = () => ({ toBlob: (cb) => cb(new Blob()) });
const withFakeClipboard = async (fn) => {
  // Node's own `navigator` global is a getter-only accessor — redefine it, don't assign.
  const origNav = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
  const origCI = globalThis.ClipboardItem;
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true, value: { clipboard: { write: () => Promise.resolve() } },
  });
  globalThis.ClipboardItem = function (parts) { this.parts = parts; };
  try { return await fn(); } finally {
    if (origNav) Object.defineProperty(globalThis, 'navigator', origNav); else delete globalThis.navigator;
    globalThis.ClipboardItem = origCI;
  }
};

test('copyImageToClipboard: "current" ignores compare mode — always the plain edited frame, never auto-upgraded to split', async () => {
  // The PRIMARY-gesture decision (split vs current) belongs to the CALLER, so this method never substitutes
  // one for the other: an explicit 'current' request stays literal even while comparing.
  reset();
  const app = makeApp({ image: {}, compareMode: 'horizontal' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard('current'));
  assert.deepEqual(seen, ['current']);
  assert.deepEqual(lastNote(), ['Image copied to clipboard', 'ok']);
});

test('copyImageToClipboard: "split" is its own explicit variant, valid only while comparing', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'horizontal' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard('split'));
  assert.deepEqual(seen, ['split']);
  assert.deepEqual(lastNote(), ['Split image copied to clipboard', 'ok']);
});

test('copyImageToClipboard: "original"/"tint" ignore compare mode — always the plain render', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'vertical' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(async () => {
    await svc.copyImageToClipboard('original');
    await svc.copyImageToClipboard('tint');
  });
  assert.deepEqual(seen, ['original', 'tint']);
  assert.deepEqual(notifications.map(n => n[0]), ['Original image copied to clipboard', 'Tinted image copied to clipboard']);
});

test('copyImageToClipboard: "current" with no split compare active is the plain render, default label', async () => {
  reset();
  const app = makeApp({ image: {}, compareMode: 'none' });
  const svc = new ExportService(app);
  const seen = [];
  svc.renderExportCanvas = (v) => { seen.push(v); return fakeOffscreen(); };
  await withFakeClipboard(() => svc.copyImageToClipboard());
  assert.deepEqual(seen, ['current']);
  assert.deepEqual(lastNote(), ['Image copied to clipboard', 'ok']);
});

test('applyPastedLayout: no image → "Load an image first", no mutation', async () => {
  reset();
  const app = makeApp({ image: null });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [] }] });
  assert.deepEqual(lastNote(), ['Load an image first', 'fail']);
  assert.equal(app.record.saveHistory, 0);
  assert.equal(app.record.redraw, 0);
});

test('applyPastedLayout: valid payload installs lines + routes through app methods', async () => {
  reset();
  const pasted = [{ points: [{ x: 1, y: 2 }], color: '#fff' }];
  const app = makeApp({ image: {}, lines: [] });
  // Match canvas dims so neither the replace nor the dim-mismatch confirm fires.
  await new ExportService(app).applyPastedLayout({ imageWidth: 100, imageHeight: 80, lines: pasted });
  // validateLayout accepted → lines installed and every shared method fired once.
  assert.equal(app.lines.length, 1);
  assert.equal(app.record.saveHistory, 1);
  assert.equal(app.record.redraw, 1);
  assert.equal(app.record.updateButtons, 1);
  assert.equal(app.record.coordUpdate.length, 1);
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard', 'ok']);
});

test('applyPastedLayout over existing lines: Cancel → canceled, no mutation', async () => {
  reset();
  // Existing lines + a valid payload raises the Combine/Replace/Cancel prompt; back out.
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => null });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.deepEqual(lastNote(), ['Layout paste canceled', 'info']);
  assert.equal(app.record.saveHistory, 0);
});

test('the layout prompt gives each real answer its own glyph, not a generic tick', async () => {
  reset();
  let opts = null;
  const app = makeApp({
    image: {}, lines: [{ points: [{ x: 0, y: 0 }] }],
    askAlt: async (_msg, o) => { opts = o; return null; },
  });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(opts.confirmLabel, 'Replace');
  assert.equal(opts.altLabel, 'Combine');
  // Replace swaps one layout for the other; Combine stacks them. A check mark would
  // say nothing about either, and a bare word beside two icon buttons reads as odd.
  assert.equal(opts.confirmIcon, 'swap');
  assert.equal(opts.altIcon, 'layers');
  const { ICONS } = await import('../../js/ui/icons.js');
  assert.ok(ICONS[opts.confirmIcon], 'confirmIcon names a real glyph');
  assert.ok(ICONS[opts.altIcon], 'altIcon names a real glyph');
});

test('applyPastedLayout over existing lines: Replace drops the old ones', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => 'confirm' });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(app.lines.length, 1);
  assert.deepEqual(app.lines[0].points, [{ x: 5, y: 5 }]);
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard', 'ok']);
});

test('applyPastedLayout over existing lines: Combine keeps them and adds the new on top', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }], askAlt: async () => 'alt' });
  await new ExportService(app).applyPastedLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] });
  assert.equal(app.lines.length, 2, 'the existing line survives');
  assert.deepEqual(app.lines[0].points, [{ x: 0, y: 0 }]);   // old first…
  assert.deepEqual(app.lines[1].points, [{ x: 5, y: 5 }]);   // …new on top
  assert.deepEqual(lastNote(), ['Layout pasted from clipboard (combined)', 'ok']);
});

test('installLayout: silent, and mode:"combine" appends instead of replacing', async () => {
  reset();
  const app = makeApp({ image: {}, lines: [{ points: [{ x: 0, y: 0 }] }] });
  const svc = new ExportService(app);
  // No prompt is consulted and nothing is announced — this is the programmatic path.
  app.askAlt = () => { throw new Error('installLayout must never prompt'); };
  assert.equal(svc.installLayout({ lines: [{ points: [{ x: 5, y: 5 }] }] }, { mode: 'combine' }), true);
  assert.equal(app.lines.length, 2);
  assert.equal(lastNote(), undefined, 'no toast');
  // Default mode replaces.
  svc.installLayout({ lines: [{ points: [{ x: 9, y: 9 }] }] });
  assert.equal(app.lines.length, 1);
  // history:false keeps it out of undo.
  const before = app.record.saveHistory;
  svc.installLayout({ lines: [{ points: [{ x: 1, y: 1 }] }] }, { history: false });
  assert.equal(app.record.saveHistory, before);
});
