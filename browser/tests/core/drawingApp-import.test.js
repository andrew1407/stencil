// DrawingApp.importExternalImage (core/launch/controller.js) and createBlankImage
// (core/image/blankImage.js): a new project vs an in-place replace, down to the real load and
// settle, and the filter reset a blank's colour needs. Split from drawingApp-launch.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';
import { installFetchStub } from '../helpers/fetchStub.js';

installDom({}, {
  location: { hash: '', pathname: '/app', search: '' },
  history: { replaceState: () => {} },
});
installFetchStub(() => Promise.resolve({ ok: true, blob: async () => ({ type: 'image/png' }) }));

const { DrawingApp } = await import('../../js/core/drawingApp.js');
const { createBlankImage } = await import('../../js/core/image/blankImage.js');

// The decode boundary: the load reads its file and the picture decodes at once, so the real
// loadImageFromFile and settleLoadedImage run against the mock; each read lands in its log.
let log = [];
globalThis.FileReader = class {
  readAsDataURL(file) {
    log.push('read');
    queueMicrotask(() => this.onload?.({ target: { result: 'data:image/png;base64,AAAA' } }));
  }
};
globalThis.Image = class {
  width = 40; height = 30;
  set src(v) { this.url = v; queueMicrotask(() => this.onload?.()); }
  get src() { return this.url; }
};
const settled = () => new Promise((r) => setTimeout(r, 0));
const reads = () => log.filter((e) => e === 'read').length;

const resetGlobals = () => {
  log = [];
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: () => {} };
};

// The stub's storage seam behaves like the real collaborators: save() flushes the active project
// into the store (never in incognito), newTemporary() resets to a blank editor and clears
// incognito, and promoting a temporary editor makes a NEW project. Every step lands in `log`.
const makeEditorMock = (over = {}) => {
  const mock = {
    stored: new Map(),
    incognitoUiCalls: 0,
    activeProjectId: 'p1',
    lines: [{ points: [{ x: 1, y: 2 }] }],
    canvas: { width: 40, height: 30 },
    storage: {
      incognito: false,
      temporary: false,
      store: {},
      save: () => {
        log.push('save');
        if (!mock.storage.incognito && mock.activeProjectId != null) mock.stored.set(mock.activeProjectId, mock.lines);
      },
      newTemporary: () => {
        log.push('reset');
        mock.activeProjectId = null;
        mock.lines = [];
        mock.storage.temporary = true;
        mock.storage.incognito = false;
      },
      promoteTemporaryToProject: () => { log.push('promote'); mock.activeProjectId = 'p2'; mock.storage.temporary = false; },
    },
    imageModel: {
      roundRect: (r) => ({ ...r }),
      defaultCropRect: () => ({ x: 5, y: 0, width: 30, height: 30 }),
      rebuildCroppedImage() {},
    },
    history: { reset() {} },
    zoomPan: { fitToWindow() {}, syncViewportHeight() {} },
    coordTable: { update() {} },
    renderer: { redraw() {}, layers: () => [] },
    tabs: { reportActive() {}, reportIncognito() {} },
    updateIncognitoUI: () => { mock.incognitoUiCalls++; },
    updateInfo() {}, updateButtons() {}, updateCoordStatus() {}, updateProjectTitle() {},
    ...over,
  };
  return mock;
};

// The bridge hands importExternalImage a normalizeLaunchPayload result, not a raw payload.
const importInto = async (mock, launch, mode) => {
  await DrawingApp.prototype.importExternalImage.call(mock, launch, { mode });
  await settled();
};

test('an import into an occupied editor starts a NEW project — the one on screen is flushed, not overwritten', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  const previousLines = mock.lines;
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'shot.png' }, 'new');

  // The project that was on screen is persisted with its annotations intact…
  assert.deepEqual(mock.stored.get('p1'), previousLines);
  // …and the image lands in a blank editor, so the loader promotes it into its OWN project
  // rather than swapping the raster of 'p1' (which would drop those lines).
  assert.deepEqual(log.slice(0, 4), ['save', 'reset', 'promote', 'read'], 'flushed, reset, then a new project loads');
  assert.equal(mock.activeProjectId, 'p2');
  assert.equal(reads(), 1);
  assert.deepEqual(mock.stored.get('p1'), previousLines, 'p1 keeps its lines after the load settles');
});

test('an incognito session survives the reset (newTemporary clears the flag) and is never saved', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  mock.storage.incognito = true;
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'i.png' }, 'new');

  assert.equal(mock.storage.incognito, true, 'still incognito after the fresh editor');
  assert.equal(mock.incognitoUiCalls, 1);
  assert.equal(mock.stored.size, 0, 'an incognito session is never flushed to the store');
  assert.equal(log.includes('promote'), false, 'and never promoted to a project');
  assert.equal(reads(), 1);
});

test('a replace swaps the ACTIVE project in place — no flush, no reset, crop forwarded', async () => {
  resetGlobals();
  const crop = { x: 4, y: 8, width: 60, height: 90 };
  const mock = makeEditorMock();
  const kept = mock.lines;
  // A `page` in the hand-off would reach the private #setExternalPage, which throws on this stub
  // `this`, so the call completing at all is the pin that a replace leaves the format alone.
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'r.png', page: { size: 'A4' }, crop }, 'replace-keep');

  assert.equal(mock.activeProjectId, 'p1');         // same project, same identity
  // Nothing flushed, nothing reset, nothing promoted: the only save is the settled replace's own.
  assert.deepEqual(log, ['read', 'save']);
  assert.equal(mock.lines, kept, 'replace-keep keeps the annotations');
  assert.deepEqual(mock.cropRect, crop);            // the rect describes the NEW raster
});

test('a "replace" drops the annotations, still in place', async () => {
  resetGlobals();
  const mock = makeEditorMock();
  await importInto(mock, { kind: 'dataUrl', dataUrl: 'data:image/png;base64,AAAA', name: 'r.png' }, 'replace');

  assert.deepEqual(log, ['read', 'save'], 'loaded in place, no flush or reset');
  assert.deepEqual(mock.lines, [], 'the annotations are dropped');
  assert.equal(mock.activeProjectId, 'p1');
});

// A blank's colour IS the page, so creation resets the filter to 'none' BEFORE the fill is
// generated: under a leftover 'bw', a red blank renders flat gray (Rec. 709 luma of red = 54).
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
  assert.throws(() => createBlankImage(mock, { color: '#ff0000', width: 40, height: 30 }), TypeError);
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
  assert.throws(() => createBlankImage(mock, { color: '#00ff00', width: 40, height: 30 }), TypeError);
  assert.deepEqual(filterSets, [], 'no redundant setImageFilter call');
});
