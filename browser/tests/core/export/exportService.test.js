// ExportService (js/core/service.js): the empty-state guards, the export canvas per
// variant and the saveImage filenames. The rig lives in helpers/exportServiceRig.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ExportService, makeApp, reset, lastNote } from '../../helpers/exportServiceRig.js';
test('saveImage: no image → fail notify, no work', () => {
  reset();
  new ExportService(makeApp()).saveImage();
  assert.deepEqual(lastNote(), ['No image loaded', 'fail']);
});

test('downloadJSON: no lines → fail notify', () => {
  reset();
  new ExportService(makeApp({ lines: [] })).downloadJSON();
  assert.deepEqual(lastNote(), ['No lines to export', 'fail']);
});

test('copyImageToClipboard: no image → fail notify', () => {
  reset();
  new ExportService(makeApp()).copyImageToClipboard();
  assert.deepEqual(lastNote(), ['No image to copy', 'fail']);
});

test('copyLayoutToClipboard: no lines → fail notify', () => {
  reset();
  new ExportService(makeApp({ lines: [] })).copyLayoutToClipboard();
  assert.deepEqual(lastNote(), ['No layout to copy', 'fail']);
});

test('saveImage("split"): outside a split compare view → fail notify, no work', () => {
  reset();
  new ExportService(makeApp({ image: {}, compareMode: 'none' })).saveImage('split');
  assert.deepEqual(lastNote(), ['Turn on split compare to download with the splitter', 'fail']);
});

// The stub canvas's getContext() returns null (helpers/dom.js), which suits 'current'/'tint'/the split
// render — they reach the ctx only through app.renderer's mocked methods; only 'original' touches it.
const fakeCtx = () => ({
  filter: 'none', drawImage() {}, save() {}, restore() {}, beginPath() {}, rect() {}, clip() {},
});
// Fakes the two elements saveImage/copyImageToClipboard create: 'canvas' (a 2D ctx stand-in plus
// toBlob/toDataURL) and 'a'. Returns fn's result plus every <a> made, so its .download can be read.
const withFakeCanvas = (fn) => {
  const orig = document.createElement;
  const ctx = fakeCtx();
  const links = [];
  document.createElement = (tag) => {
    if (tag === 'canvas') {
      return { width: 0, height: 0, getContext: () => ctx, toDataURL: () => 'data:image/png;base64,x', toBlob: (cb) => cb(new Blob()) };
    }
    if (tag === 'a') { const a = { click() {} }; links.push(a); return a; }
    return orig(tag);
  };
  try { return { result: fn(ctx), links }; } finally { document.createElement = orig; }
};

test('renderExportCanvas: "current" draws the filter + the visible lines, not points', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, showPoints: false, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: {
      drawImageWithFilter: () => calls.push('filter'),
      drawLine: (line, sel) => calls.push(['line', sel]),
      drawPoint: () => calls.push('point'),
    },
  });
  const off = new ExportService(app).renderExportCanvas('current');
  assert.equal(off.width, app.canvas.width);
  assert.deepEqual(calls, ['filter', ['line', false]]);
});

test('renderExportCanvas: "tint" draws the filter but never annotations', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, showPoints: true, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: { drawImageWithFilter: () => calls.push('filter'), drawLine: () => calls.push('line'), drawPoint: () => calls.push('point') },
  });
  new ExportService(app).renderExportCanvas('tint');
  assert.deepEqual(calls, ['filter']);
});

test('renderExportCanvas: "original" draws the raw image only — no filter, no annotations', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: { drawImageWithFilter: () => calls.push('filter'), drawLine: () => calls.push('line') },
  });
  withFakeCanvas(() => new ExportService(app).renderExportCanvas('original'));
  assert.deepEqual(calls, []);
});

test('renderExportCanvas("split"): draws the filtered+annotated frame, then a CLEAN split — no divider, no knob', () => {
  const calls = [];
  const app = makeApp({
    image: {}, showLines: true, compareMode: 'vertical', lines: [{ points: [{ x: 0, y: 0 }], color: '#000' }],
    renderer: {
      drawImageWithFilter: () => calls.push('filter'),
      drawLine: () => calls.push('line'),
      drawCompareSplit: (mode, opts) => calls.push(['split', mode, opts]),
    },
  });
  new ExportService(app).renderExportCanvas('split');
  assert.deepEqual(calls, ['filter', 'line', ['split', 'vertical', { withDivider: false }]]);
});

test('renderExportCanvas("split"): "horizontal" compare mode passes through as-is', () => {
  const calls = [];
  const app = makeApp({
    image: {}, compareMode: 'horizontal',
    renderer: { drawImageWithFilter() {}, drawCompareSplit: (mode) => calls.push(mode) },
  });
  new ExportService(app).renderExportCanvas('split');
  assert.deepEqual(calls, ['horizontal']);
});

test('saveImage: each variant downloads with its own filename suffix', () => {
  const app = makeApp({
    image: {}, imageBaseName: 'pic', imageExt: 'png',
    renderer: { drawImageWithFilter() {}, drawLine() {}, drawPoint() {} },
  });
  const svc = new ExportService(app);
  const { links } = withFakeCanvas(() => {
    svc.saveImage('current'); svc.saveImage('original'); svc.saveImage('tint');
  });
  assert.deepEqual(links.map(l => l.download), ['pic-drawing.png', 'pic-drawing-original.png', 'pic-drawing-tint.png']);
});

test('saveImage("split"): downloads a CLEAN split composite while a split compare view is active — no divider/knob baked in', () => {
  const calls = [];
  const app = makeApp({
    image: {}, compareMode: 'vertical', imageBaseName: 'pic', imageExt: 'png',
    renderer: { drawImageWithFilter() {}, drawCompareSplit: (mode, opts) => calls.push([mode, opts]) },
  });
  const { links } = withFakeCanvas(() => new ExportService(app).saveImage('split'));
  assert.deepEqual(links.map(l => l.download), ['pic-drawing-split.png']);
  assert.deepEqual(calls, [['vertical', { withDivider: false }]]);
});
