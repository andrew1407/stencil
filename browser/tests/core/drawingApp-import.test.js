// DrawingApp.importExternalImage and createBlankImage (js/core/drawingApp.js): a new project vs
// an in-place replace, and the filter reset a blank's colour needs. Split from drawingApp-launch.
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

const resetGlobals = () => {
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState: () => {} };
};

// The stub's storage seam behaves like the real collaborators: save() flushes the active project
// into the store, newTemporary() resets to a blank editor and clears incognito.
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
  // A `page` in the hand-off would reach the private #setExternalPage, which throws on this stub
  // `this`, so the call completing at all is the pin that a replace leaves the format alone.
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
