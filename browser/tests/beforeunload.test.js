// Unit test for DrawingApp.hasEditingSession (js/core/drawingApp.js), the synchronous
// predicate that drives the beforeunload leave-guard wired in index.js. We invoke the real
// method via `.call(fakeThis)` with a minimal `this` — no DOM/class instance needed — and
// pin exactly when leaving the tab should prompt: an image is loaded, OR the user has drawn
// something (history.canUndo). An empty, untouched editor must NOT prompt.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// drawingApp.js reaches for a few globals at import time; supply the same minimal stubs the
// other DrawingApp unit tests use so the module graph loads under `node --test`.
globalThis.document = { getElementById: () => null };
globalThis.location = { hash: '', pathname: '/app', search: '' };
globalThis.history = { replaceState: () => {} };

const { DrawingApp } = await import('../js/core/drawingApp.js');
const hasEditingSession = DrawingApp.prototype.hasEditingSession;

const noUndo = { canUndo: () => false };
const withUndo = { canUndo: () => true };

test('empty editor (no image, nothing drawn) does not warn on leave', () => {
  assert.equal(hasEditingSession.call({ image: null, history: noUndo }), false);
});

test('a loaded image warns on leave', () => {
  assert.equal(hasEditingSession.call({ image: {}, history: noUndo }), true);
});

test('an undoable drawing warns on leave even without an image', () => {
  assert.equal(hasEditingSession.call({ image: null, history: withUndo }), true);
});

test('missing/partial history never throws (returns false)', () => {
  assert.equal(hasEditingSession.call({ image: null, history: undefined }), false);
  assert.equal(hasEditingSession.call({ image: null, history: {} }), false);
});
