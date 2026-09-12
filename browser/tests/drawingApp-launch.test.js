// Security/behaviour regression tests for DrawingApp.applyExternalLaunch (js/core/drawingApp.js).
// The extension hands images to the editor via the URL FRAGMENT (#stencil=<encoded JSON>);
// applyExternalLaunch consumes it. Two invariants matter here and must not silently break:
//   1. The fragment is stripped from the URL immediately (history.replaceState) so it never
//      reaches the server on a reload — "the fragment never reaches the server".
//   2. A malformed/truncated payload is caught (no throw) and reported via a fail notify.
// We also pin the dataUrl (data:) vs src (https:) fetch dispatch.
//
// The tail of the file covers its sibling entry point, importExternalImage — the extension
// bridge importing a hand-off into a tab that is ALREADY editing something. Both share the
// same import tail, so what's pinned there is what differs: 'new' must start a new project
// (flush + reset first) instead of overwriting the one on screen, and a replace must leave
// the target project's page format alone.
//
// applyExternalLaunch is an instance method that reaches into many collaborators AND uses
// private methods (#setExternalPage/#stripExt/#applyServerLaunch). Rather than build a whole
// DrawingApp, we invoke the real method via `.call(mockApp)` with a minimal stub `this` and
// stubbed globals (location/history/fetch/document). We deliberately drive only the payload
// shapes that DON'T reach a private method: no `page` (would hit #setExternalPage), no
// `server` kind (#applyServerLaunch), no `open:'resume'` and no source-on-a-persistent-editor
// (both hit #stripExt). Those paths need a real class instance and are out of scope for a unit
// test; what we lock in is the fragment-strip, the malformed-JSON guard, and the fetch routing.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from './helpers/dom.js';
import { installFetchStub } from './helpers/fetchStub.js';
import { COMPONENTS_CSS } from './helpers/css.js';

// notify() (utils.js) posts to a #notify-balloon element if present; expose one so we can spy.
const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };

// Mutable stub globals the method reads/writes. Reset per test via resetGlobals().
let replaceStateCalls = [];
let fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });

installDom({}, {
  location: { hash: '', pathname: '/app', search: '' },
  history: { replaceState: (...args) => replaceStateCalls.push(args) },
}).register('notify-balloon', balloon);
// The stub delegates so a test can swap `fetchImpl` partway; calls land on fetchStub.calls.
const fetchStub = installFetchStub((...args) => fetchImpl(...args));
const fetchCalls = fetchStub.calls;

const { DrawingApp } = await import('../js/core/drawingApp.js');

const resetGlobals = () => {
  notifications.length = 0;
  replaceStateCalls = [];
  fetchStub.reset();
  fetchImpl = () => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) });
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
};

// The minimum `this` applyExternalLaunch touches on the non-private paths we drive.
const makeMock = (over = {}) => {
  const loaded = [];
  return {
    loaded,
    storage: { incognito: false, store: {} },
    loadImageFromFile: (...args) => loaded.push(args),
    updateIncognitoUI() {},
    ...over,
  };
};

// Encode a payload the way the extension does: #stencil=<encodeURIComponent(JSON)>.
const fragmentFor = (payload) => '#stencil=' + encodeURIComponent(JSON.stringify(payload));
const run = (mock) => DrawingApp.prototype.applyExternalLaunch.call(mock);

test('no #stencil= fragment → the method is a no-op (no URL rewrite, no fetch)', () => {
  resetGlobals();
  globalThis.location.hash = '#something-else';
  run(makeMock());
  assert.equal(replaceStateCalls.length, 0);
  assert.equal(fetchCalls.length, 0);
});

test('a valid fragment is stripped from the URL immediately (never reaches the server)', async () => {
  resetGlobals();
  globalThis.location = { hash: fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'a.png' }), pathname: '/editor', search: '?q=1' };
  globalThis.history = { replaceState: (...args) => replaceStateCalls.push(args) };
  const mock = makeMock();
  run(mock);

  // history.replaceState(null, '', pathname + search) — the #stencil fragment is dropped.
  assert.equal(replaceStateCalls.length, 1);
  const [state, title, url] = replaceStateCalls[0];
  assert.equal(state, null);
  assert.equal(title, '');
  assert.equal(url, '/editor?q=1');
  assert.ok(!url.includes('#stencil'), 'stripped URL carries no fragment');
});

test('a malformed/truncated #stencil= payload is caught (no throw) and reported as a fail', () => {
  resetGlobals();
  // Truncated JSON — decodeURIComponent succeeds, JSON.parse throws inside the method.
  globalThis.location.hash = '#stencil=' + encodeURIComponent('{"dataUrl":"data:image/png;base64,AAA');
  const mock = makeMock();

  assert.doesNotThrow(() => run(mock));
  // The fragment is still stripped (strip happens before the parse).
  assert.equal(replaceStateCalls.length, 1);
  // Reported via a fail notify, and nothing was fetched/loaded.
  assert.deepEqual(notifications, [['Stencil: could not read the shared image', 'fail']]);
  assert.equal(fetchCalls.length, 0);
  assert.equal(mock.loaded.length, 0);
});

test('a data: payload fetches without CORS mode and loads the decoded image', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'shared.png' });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));   // let the fetch().then() microtasks flush

  assert.equal(fetchCalls.length, 1);
  const [url, opts] = fetchCalls[0];
  assert.equal(url, 'data:image/png;base64,AAAA');
  assert.ok(opts.signal && opts.mode === undefined, 'data: URLs get no CORS mode — and every fetch is bounded');
  assert.equal(mock.loaded.length, 1);           // loadImageFromFile(file, opts) was reached
  const [file] = mock.loaded[0];
  assert.equal(file.name, 'shared.png');
});

test('an https src: payload fetches with { mode: "cors" } and loads the image', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/i.png', name: 'i.png' });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(fetchCalls.length, 1);
  const [url, opts] = fetchCalls[0];
  assert.equal(url, 'https://cdn.example/i.png');
  assert.ok(opts.signal && opts.mode === 'cors', 'remote image → cross-origin fetch, bounded like the rest');
  assert.equal(mock.loaded.length, 1);
});

test('a crop in the payload flows through to loadImageFromFile opts (open-image "new tab" + crop)', async () => {
  resetGlobals();
  const crop = { x: 10, y: 20, width: 100, height: 140 };
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'c.png', crop });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(mock.loaded.length, 1);
  const [, opts] = mock.loaded[0];
  assert.deepEqual(opts.crop, crop);   // the inline-crop rect rides the fragment to the loader
});

test('noCrop in the payload flows to loadImageFromFile opts (open-image "new tab", Crop off → whole frame)', async () => {
  resetGlobals();
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'n.png', noCrop: true });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(mock.loaded.length, 1);
  const [, opts] = mock.loaded[0];
  assert.equal(opts.noCrop, true);     // Crop-off imports the full frame, not the default auto-crop
  assert.equal(opts.crop, undefined);
});

test('an explicit crop wins over noCrop when both are present', async () => {
  resetGlobals();
  const crop = { x: 1, y: 2, width: 30, height: 40 };
  globalThis.location.hash = fragmentFor({ dataUrl: 'data:image/png;base64,AAAA', name: 'b.png', crop, noCrop: true });
  const mock = makeMock();
  run(mock);
  await new Promise((r) => setTimeout(r, 0));

  const [, opts] = mock.loaded[0];
  assert.deepEqual(opts.crop, crop);
  assert.equal(opts.noCrop, undefined);   // crop present ⇒ noCrop is not forwarded
});

test('a failed fetch is caught and reported as a fail (no throw escapes)', async () => {
  resetGlobals();
  fetchImpl = () => Promise.resolve({ ok: false, status: 404, blob: async () => ({ type: 'image/png' }) });
  globalThis.location.hash = fragmentFor({ src: 'https://cdn.example/missing.png', name: 'm.png' });
  const mock = makeMock();
  assert.doesNotThrow(() => run(mock));
  await new Promise((r) => setTimeout(r, 0));

  assert.equal(mock.loaded.length, 0);
  assert.deepEqual(notifications.at(-1), ['Stencil: failed to load the shared image', 'fail']);
});

// ── importExternalImage: the extension bridge importing into a LIVE editor ──────────────
// A stub `this` for importExternalImage, standing in a tab that already holds a SAVED project
// with annotations over it. The storage seam behaves like the real collaborators: save()
// flushes the active project into the store, newTemporary() resets to a blank editor (and
// clears incognito, which is why the flag has to be re-applied after it).
const makeEditorMock = (over = {}) => {
  const mock = {
    stored: new Map(),
    loaded: [],
    replaced: [],
    incognitoUiCalls: 0,
    activeProjectId: 'p1',
    lines: [{ points: [{ x: 1, y: 2 }] }],
    storage: {
      incognito: false,
      temporary: false,
      store: {},
      save: () => { if (mock.activeProjectId != null) mock.stored.set(mock.activeProjectId, mock.lines); },
      newTemporary: () => {
        mock.activeProjectId = null;
        mock.lines = [];
        mock.storage.temporary = true;
        mock.storage.incognito = false;
      },
    },
    newEditor: () => mock.storage.newTemporary(),
    updateIncognitoUI: () => { mock.incognitoUiCalls++; },
    loadImageFromFile: (...args) => mock.loaded.push(args),
    replaceProjectImage: (...args) => mock.replaced.push(args),
    ...over,
  };
  return mock;
};

// The bridge hands importExternalImage a normalizeLaunchPayload result, not a raw payload.
const importInto = (mock, launch, mode) =>
  DrawingApp.prototype.importExternalImage.call(mock, launch, { mode });

test('an import into an occupied editor starts a NEW project — the one on screen is flushed, not overwritten', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  const previousLines = mock.lines;
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'shot.png' }, 'new');

  // The project that was on screen is persisted with its annotations intact…
  assert.deepEqual(mock.stored.get('p1'), previousLines);
  // …and the image lands in a blank editor, so the loader promotes it into its OWN project
  // rather than swapping the raster of 'p1' (which would drop those lines).
  assert.equal(mock.activeProjectId, null);
  assert.equal(mock.storage.temporary, true);
  assert.equal(mock.loaded.length, 1);
  assert.equal(mock.replaced.length, 0);
});

test('an incognito session survives the reset (newTemporary clears the flag) and is never saved', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  mock.storage.incognito = true;
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'i.png' }, 'new');

  assert.equal(mock.storage.incognito, true, 'still incognito after the fresh editor');
  assert.equal(mock.incognitoUiCalls, 1);
  assert.equal(mock.stored.size, 0, 'an incognito session is never flushed to the store');
  assert.equal(mock.loaded.length, 1);
});

test('a replace swaps the ACTIVE project in place — no flush, no reset, crop forwarded', async () => {
  resetGlobals();
  const crop = { x: 4, y: 8, width: 60, height: 90 };
  const mock = makeEditorMock();
  // A `page` in the hand-off would reach the PRIVATE #setExternalPage, which throws on this
  // stub `this` (brand check) — so this call completing at all is how we pin that a replace
  // leaves the target project's page format alone.
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'r.png', page: { size: 'A4' }, crop }, 'replace-keep');

  assert.equal(mock.activeProjectId, 'p1');         // same project, same identity
  assert.equal(mock.stored.size, 0);                // nothing flushed, nothing reset
  assert.equal(mock.loaded.length, 0);
  assert.equal(mock.replaced.length, 1);
  const [, opts] = mock.replaced[0];
  assert.equal(opts.keepAnnotations, true);
  assert.deepEqual(opts.crop, crop);                // the rect describes the NEW raster
});

test('a "replace" drops the annotations, still in place', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'r.png' }, 'replace');

  assert.equal(mock.replaced.length, 1);
  assert.equal(mock.replaced[0][1].keepAnnotations, false);
  assert.equal(mock.activeProjectId, 'p1');
});

// ── createBlankImage: a riding filter must not repaint a fresh blank ──
// A blank's colour IS the page: with a leftover 'bw' filter, a red blank renders
// flat gray (Rec. 709 luma of pure red = 54). Creation resets the filter to 'none'
// BEFORE the fill is generated. The reset line runs ahead of the private
// #blankFillBlob call, which throws on a stub `this` (brand check) — same trick as
// the replace test above: we assert the prefix behaviour, catching the TypeError.
test('createBlankImage resets a riding image filter to none first', () => {
  const filterSets = [];
  const mock = {
    imageFilter: 'bw',
    settings: { setImageFilter: f => filterSets.push(f) },
    storage: { incognito: false },
    connections: {},
    pageSize: 'A4',
    customPageWidth: 21, customPageHeight: 29.7,
  };
  assert.throws(() => DrawingApp.prototype.createBlankImage.call(
    mock, { color: '#ff0000', width: 40, height: 30 }), TypeError);
  assert.deepEqual(filterSets, ['none'], 'filter reset to none before the fill');
});

test('createBlankImage leaves an already-clean filter alone', () => {
  const filterSets = [];
  const mock = {
    imageFilter: 'none',
    settings: { setImageFilter: f => filterSets.push(f) },
    storage: { incognito: false },
    connections: {},
    pageSize: 'A4',
    customPageWidth: 21, customPageHeight: 29.7,
  };
  assert.throws(() => DrawingApp.prototype.createBlankImage.call(
    mock, { color: '#00ff00', width: 40, height: 30 }), TypeError);
  assert.deepEqual(filterSets, [], 'no redundant setImageFilter call');
});

// ── The incognito tag on the info line (updateInfo) ──────────────────────────
// The mode used to read ONLY in the toolbar "?" bubble, which appears with an image —
// so an empty incognito editor announced it nowhere at all (user report). The line now
// carries its own tag element, and data-size keeps the bubble reading the size alone.
const infoLine = () => {
  const el = { dataset: {}, children: [], _text: '', className: '', innerHTML: '' };
  el.setAttribute = () => {};
  el.appendChild = (c) => { el.children.push(c); return c; };
  el.append = (...cs) => { for (const c of cs) el.appendChild(c); };
  Object.defineProperty(el, 'textContent', {
    get: () => el._text + el.children.map((c) => c.textContent).join(''),
    set: (v) => { el._text = String(v); el.children.length = 0; },
  });
  return el;
};
const runUpdateInfo = (over) => {
  const info = infoLine();
  const prev = globalThis.document;
  globalThis.document = {
    getElementById: (id) => (id === 'image-info' ? info : null),
    createElement: () => infoLine(),
  };
  try {
    DrawingApp.prototype.updateInfo.call({
      image: null, canvas: { width: 0, height: 0 },
      activeIsBlank: () => false, storage: { incognito: false }, ...over,
    });
  } finally { globalThis.document = prev; }
  return info;
};

test('the info line states incognito with no image loaded, and nothing when it is off', () => {
  const on = runUpdateInfo({ storage: { incognito: true } });
  assert.equal(on.dataset.size, 'No image loaded. Upload an image to start.');
  // A muted divider then the tag — the empty editor gets the same pair as a loaded one.
  assert.equal(on.children.length, 2, 'the empty editor still shows the mode');
  assert.equal(on.children[0].className, 'info-divider');
  assert.equal(on.children[0].textContent, '|');
  assert.equal(on.children[1].className, 'info-incognito');
  // The app's own glyph, at 13px, stroked in currentColor so it takes the accent.
  assert.match(on.children[1].innerHTML, /class="ic ic-incognito"/);
  assert.match(on.children[1].innerHTML, /width="13" height="13"/);
  assert.match(on.children[1].innerHTML, /stroke="currentColor"/);
  assert.ok(!/🕶/.test(on.children[1].innerHTML), 'no emoji — it renders differently per platform');
  assert.match(on.children[1].innerHTML, /Incognito — not saved/);
  // …and the size stays separable, so the "?" bubble never doubles the line.
  assert.ok(!on.dataset.size.includes('Incognito'));

  const off = runUpdateInfo({});
  assert.equal(off.children.length, 0, 'no tag — and so no dangling divider — when it is off');
  assert.equal(off.textContent, 'No image loaded. Upload an image to start.');

  // With an image the tag rides beside the size, which data-size still holds alone.
  const withImage = runUpdateInfo({
    image: {}, canvas: { width: 800, height: 600 }, storage: { incognito: true },
  });
  assert.equal(withImage.dataset.size, 'Image Size: 800 × 600 px');
  assert.equal(withImage.children.length, 2, 'divider + tag beside the image facts');
});

// ── The coordinate readout must track the cursor in EVERY mode ───────────────
// Reported: hovering the image showed no coordinates at all while a horizontal
// comparison was on. Nothing overlays the canvas (the incognito frame is pointer-
// transparent and the split is drawn INTO the canvas, measured with elementsFromPoint) —
// canvasMouseMove simply returned on the compare branches before updating the strip.
const hoverMock = (over = {}) => {
  const readout = [];
  return {
    readout,
    image: {}, isPanning: false, isDraggingPoint: false,
    compareHoldOriginal: false, isDraggingCompareSplit: false, compareMode: 'horizontal',
    canvas: { style: {} },
    input: { holdEngaged: false },
    tooltipMgr: { hide() {}, applyHover() {} },
    // The compare gate the tooltip consults (drawingApp.compareShowsPoint).
    compareShowsPoint: () => true,
    isDrawing: false, drawMode: 'line', isZoomRectDragging: false,
    hoverPt: null, hoverLineIdx: -1, coordLineIdx: 0, hoveredPtIdx: -1,
    findNearestSegmentWithIdx: () => null,
    canvasCoords: () => ({ x: 12, y: 34 }),
    updateCoordStatus: (...a) => readout.push(a),
    nearCompareDivider: () => false,
    compareReadOnly: () => true,
    findNearestPointWithIdx: () => null,
    findLineAt: () => -1,
    coordTable: { applyRowHighlight() {} },
    renderer: { redraw() {} },
    applyLinesListHover() {},
    ...over,
  };
};

test('hovering the canvas updates the coords readout in ALL four states', () => {
  const ev = (x) => ({ clientX: x, clientY: 120, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false });
  // Desktop parity (CanvasWidget::mouseMoveEvent): the readout must CHANGE between two
  // cursor positions in every state — plain, incognito, and both split compares. The two
  // compare states froze it at the last pixel hovered before compare came on.
  const states = [
    ['plain', { compareMode: 'none', compareReadOnly: () => false }],
    ['incognito', { compareMode: 'none', compareReadOnly: () => false, incognito: true }],
    ['compare vertical', { compareMode: 'vertical', compareReadOnly: () => true }],
    ['compare horizontal', { compareMode: 'horizontal', compareReadOnly: () => true }],
  ];
  for (const [name, over] of states) {
    let n = 0;
    const m = hoverMock({ ...over, canvasCoords: () => ({ x: 10 + (n += 1) * 5, y: 34 }) });
    DrawingApp.prototype.canvasMouseMove.call(m, ev(100));
    DrawingApp.prototype.canvasMouseMove.call(m, ev(140));
    assert.deepEqual(m.readout, [[15, 34], [20, 34]], `${name}: the readout follows the cursor`);
  }
  // Over the draggable divider it still reports — and the handle keeps its resize cursor.
  const div = hoverMock({ nearCompareDivider: () => true, compareReadOnly: () => false });
  DrawingApp.prototype.canvasMouseMove.call(div, ev(100));
  assert.deepEqual(div.readout, [[12, 34]]);
  assert.equal(div.canvas.style.cursor, 'row-resize', 'the handle stays draggable');
  // Compare is still read-only for EDITING — but the coordinate tooltip is DISPLAY, so
  // the hover decision now runs (its own visibility gate decides what to show).
  const cmp = hoverMock();
  const asked = [];
  cmp.tooltipMgr = { hide() {}, applyHover: (...a) => asked.push(a.slice(0, 4)) };
  DrawingApp.prototype.canvasMouseMove.call(cmp, ev(100));
  assert.deepEqual(asked, [[100, 120, 12, 34]], 'the tooltip is offered the hovered point');
  assert.equal(cmp.canvas.style.cursor, 'default', 'while the edit cursor stays suppressed');
  // Over the DIVIDER nothing is offered — dragging it must not pop a point tooltip.
  const onDiv = hoverMock({ nearCompareDivider: () => true, compareReadOnly: () => false });
  let divHidden = 0;
  onDiv.tooltipMgr = { hide: () => { divHidden += 1; },
    applyHover: () => assert.fail('the divider must not pop a tooltip') };
  DrawingApp.prototype.canvasMouseMove.call(onDiv, ev(100));
  assert.equal(divHidden, 1);
  // No image at all → the idle hint, as before (no coordinates to report).
  const empty = hoverMock({ image: null });
  DrawingApp.prototype.canvasMouseMove.call(empty, ev(100));
  assert.deepEqual(empty.readout, [[]]);
});

// ── No tooltip while the mouse is down doing something else ──────────────────
// Panning, dragging, sweeping a zoom/rect-draw box, or an armed/active press-and-hold
// (InputController#holdDraw) all move the mouse without the tooltip caring — and any
// tooltip already up must be dropped, not left lingering.
test('a drag or hold in progress (any kind) never offers the tooltip a hover — and drops one already up', () => {
  const ev = (x) => ({ clientX: x, clientY: 120, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false });
  const cases = [
    ['isPanning', { isPanning: true }],
    ['isDraggingPoint', { isDraggingPoint: true }],
    ['isDraggingSegment', { isDraggingSegment: true }],
    ['isDraggingLine', { isDraggingLine: true }],
    ['isZoomRectDragging', { isZoomRectDragging: true }],
    ['isRectDrawDragging', { isRectDrawDragging: true }],
    ['input.holdEngaged (armed or drawing)', { input: { holdEngaged: true } }],
  ];
  for (const [name, over] of cases) {
    const m = hoverMock({ compareMode: 'none', compareReadOnly: () => false, ...over });
    let hidden = 0;
    m.tooltipMgr = { hide: () => { hidden += 1; },
      applyHover: () => assert.fail(`${name}: a drag/hold in progress must not pop a tooltip`) };
    DrawingApp.prototype.canvasMouseMove.call(m, ev(100));
    assert.equal(hidden, 1, `${name}: any tooltip already up is dropped`);
    assert.deepEqual(m.readout, [], `${name}: the coord readout is not touched mid-gesture either`);
  }
});

test('the on-canvas overlays never eat the pointer', () => {
  const css = COMPONENTS_CSS;
  const frame = css.slice(css.indexOf('.incognito-frame {'), css.indexOf('}', css.indexOf('.incognito-frame {')));
  assert.match(frame, /pointer-events: none/, 'the incognito frame is pointer-transparent');
  // pointer-events inherits, so the four dashed edges inside it must not re-enable it.
  const edge = css.slice(css.indexOf('.ig-edge {'), css.indexOf('}', css.indexOf('.ig-edge {')));
  assert.ok(!/pointer-events/.test(edge), 'the edges inherit the frame\'s transparency');
  // The comparison has no overlay element to eat anything — the split is rendered
  // into the canvas itself, and its divider is a cursor + a pointer handler.
  const markup = readFileSync(new URL('../js/ui/mainContent.js', import.meta.url), 'utf8');
  assert.ok(!/compare-(overlay|divider|handle)/.test(markup), 'the split stays a canvas render');
  const zoomRect = markup.slice(markup.indexOf('id="zoom-rect-overlay"'));
  assert.match(zoomRect.slice(0, zoomRect.indexOf('>')), /pointer-events:none/, 'and the zoom rect too');
});
