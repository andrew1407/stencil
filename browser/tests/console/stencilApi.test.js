// window.stencil (js/console/stencilApi.js): the read-only guard, the chainable editor
// actions, settings flattening, and the tooltip/layout/viewport/blank wrappers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  createStencil, validateLayout, makeApp, called, lastCall, viewport,
} from '../helpers/stencilApiRig.js';

// ── Read-only guard ────────────────────────────────────────────────────────────
test('guard: reassigning a method, defining, or deleting a member throws; real setters write through', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.throws(() => { stencil.undo = 0; }, /read-only/);
  assert.throws(() => { stencil.getProjectByName = 0; }, /read-only/);
  assert.throws(() => { delete stencil.lines; }, /cannot be deleted/);
  assert.throws(() => { Object.defineProperty(stencil, 'x', { value: 1 }); }, /read-only/);

  // A legit setter (a flattened setting) writes through to the app.
  stencil.lineColor = '#ABC';
  assert.deepEqual(lastCall(app, 'setColor'), ['setColor', '#aabbcc']); // #abc → #aabbcc via toHexColor
});

// ── Chainable editor actions ────────────────────────────────────────────────────
test('editor actions route to app.* and return the facade for chaining', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.rotateLeft(), stencil);
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', -1]);
  assert.equal(stencil.rotateRight(), stencil);
  assert.deepEqual(lastCall(app, 'rotateImage'), ['rotateImage', 1]);

  // A whole chain hits each underlying method once, in order.
  stencil.undo().redo().clearLines().startDrawing().stopDrawing()
    .downloadImage().copyImage().copyLayout().downloadLayout().newEditor().zoomFit();
  for (const m of ['undo', 'redo', 'clearAllLines', 'startDrawingMode', 'stopDrawingMode',
    'saveImage', 'copyImageToClipboard', 'copyLayoutToClipboard', 'downloadJSON', 'newEditor', 'fitToWindow'])
    assert.equal(called(app, m).length, 1, `${m} called once`);
});

test('drawing get/set mirrors the start/stop buttons and honours the loaded-image guard', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // No image → enabling drawing is a no-op (matches the toolbar guard).
  stencil.drawing = true;
  assert.equal(called(app, 'startDrawingMode').length, 0);

  app.image = { width: 10, height: 10 };
  stencil.drawing = true;
  assert.equal(called(app, 'startDrawingMode').length, 1);

  app.isDrawing = true;
  stencil.drawing = false;
  assert.equal(called(app, 'stopDrawingMode').length, 1);
});

// ── Settings flattening ─────────────────────────────────────────────────────────
test('settings flatten onto the facade and onto stencil.settings, both driving app setters', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.thickness, 2);          // reads app.thickness
  stencil.thickness = 4;
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 4]);

  stencil.settings.pageSize = 'a3';
  assert.deepEqual(lastCall(app, 'setPageSize'), ['setPageSize', 'a3']);

  stencil.darkTheme = false;
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'light']);
  stencil.darkTheme = true;
  assert.deepEqual(lastCall(app, 'setTheme'), ['setTheme', 'dark']);
});


// ── Tooltip / imageSize / layout / coordinate conversion / viewport / fullscreen ──
test('tooltip sections, imageSize, and layout get/set route to the app', () => {
  const app = makeApp({ image: { width: 800, height: 600 } });
  const stencil = createStencil(app);

  assert.deepEqual(stencil.imageSize, { width: 800, height: 600 });

  stencil.tooltip.enabled = false;
  assert.deepEqual(lastCall(app, 'setTooltipOption'), ['setTooltipOption', 'enabled', false]);
  assert.equal(stencil.tooltip.page, true);   // reads app.tooltipShowPage

  stencil.layout = { foo: 1 };
  assert.deepEqual(lastCall(app, 'applyPastedLayout'), ['applyPastedLayout', { foo: 1 }]);
});

test('setLines installs lines silently — no paste prompt, no toast, optional undo', () => {
  // `stencil.layout = …` is the clipboard-paste path (it can raise "Replace layout?" and
  // always toasts). setLines is the programmatic one: same install, none of the UI.
  const app = makeApp({ image: { width: 800, height: 600 } });
  const stencil = createStencil(app);
  const lines = [{ points: [{ x: 1, y: 2 }], color: '#ff0000' }];

  stencil.setLines(lines, { history: false });
  // The current image dims ride along so validateLayout sees a matching size.
  assert.deepEqual(lastCall(app, 'installLayout'),
    ['installLayout', { imageWidth: 800, imageHeight: 600, lines }, { history: false }]);
  // It never reaches the paste path, so nothing can prompt or toast.
  assert.equal(app.calls.filter((c) => c[0] === 'applyPastedLayout').length, 0);

  // Defaults keep the change undoable; a non-array is treated as "no lines".
  stencil.setLines(lines);
  assert.deepEqual(lastCall(app, 'installLayout')[2], {});
  stencil.setLines(null);
  assert.deepEqual(lastCall(app, 'installLayout')[1].lines, []);
  // Chainable like the rest of the facade.
  assert.equal(stencil.setLines([]), stencil);
});

test('px2Page / page2Px convert via the app mapping', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.deepEqual(stencil.px2Page({ x: 100, y: 200 }), { x: 10, y: 20 });
  // page2Px: (cm / pageDim) * canvasPx → (5/20)*200=50 , (15/30)*300=150
  assert.deepEqual(stencil.page2Px({ x: 5, y: 15 }), { x: 50, y: 150 });
});

test('move pans the viewport; fullscreen get/set toggles via the app', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.move({ x: 10, y: -4 }), stencil);
  assert.equal(viewport.scrollLeft, 10);
  assert.equal(viewport.scrollTop, -4);

  assert.equal(stencil.fullscreen, false);
  stencil.fullscreen = true;
  assert.equal(called(app, 'toggleFullscreen').length, 1);
  assert.equal(stencil.fullscreen, true);
  stencil.fullscreen = true;                       // already on → no extra toggle
  assert.equal(called(app, 'toggleFullscreen').length, 1);
});

test('blank() creates a solid image via the app and resolves to the facade', async () => {
  const app = makeApp();
  const stencil = createStencil(app);

  const ret = await stencil.blank('red', { size: { width: 800, height: 600 } });
  assert.equal(ret, stencil);
  assert.deepEqual(lastCall(app, 'createBlankImage'), ['createBlankImage', { color: 'red', width: 800, height: 600 }]);
  assert.deepEqual(stencil.imageSize, { width: 800, height: 600 });
});

// createBlankImage decodes asynchronously, so the picture lands frames after the promise settles:
// imageLoadFlow.waitForImage takes `previous` so a chained crop never gets the outgoing image.
test('blank() over an existing image waits for the swap, not for "an image exists"', async () => {
  const app = makeApp({ image: { width: 111, height: 222 } });
  app.createBlankImage = (opts) => {
    setTimeout(() => { app.image = { width: opts.width, height: opts.height }; }, 30);
    return Promise.resolve();
  };
  const stencil = createStencil(app);

  await stencil.blank('red', { size: { width: 800, height: 600 } });
  assert.deepEqual(stencil.imageSize, { width: 800, height: 600 });
});
