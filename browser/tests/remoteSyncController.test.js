// Unit tests for RemoteSyncController (js/core/remoteSyncController.js) — the live co-edit
// engine extracted out of DrawingApp. The debounce/conflict paths are timing+network heavy;
// here we pin the deterministic, side-effect-observable parts: the three server-layout
// adoption helpers (they restore filter/page/formula state into the app), the renderResultBytes
// no-image guard, and that scheduleRemoteSync is a clean no-op for a local-only session.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// DOM is only touched for UI mirroring; return null / empty so the guards short-circuit.
globalThis.document = {
  getElementById: () => null,
  querySelectorAll: () => [],
};

const { RemoteSyncController } = await import('../js/core/remoteSyncController.js');

const makeApp = (over = {}) => {
  const rec = { syncFormulaUI: [], showFormulaError: [] };
  return {
    rec,
    remoteLink: null,
    image: null,
    imageFilter: 'none', filterColor: '#7c3aed', filterDirty: true,
    pageSize: 'A3', customPageWidth: 21, customPageHeight: 29.7, unit: 'cm',
    allowFormulas: false, formulaX: '', formulaY: '',
    settings: {
      syncFormulaUI: (v) => rec.syncFormulaUI.push(v),
      showFormulaError: (v) => rec.showFormulaError.push(v),
    },
    ...over,
  };
};

test('scheduleRemoteSync: local-only session (no remoteLink) is a clean no-op', () => {
  const app = makeApp({ remoteLink: null });
  // Must not throw and must not touch getSyncToServer/timers (short-circuits on remoteLink).
  assert.doesNotThrow(() => new RemoteSyncController(app).scheduleRemoteSync());
});

test('adoptServerFilter: restores filter + tint and clears the dirty flag', () => {
  const app = makeApp({ imageFilter: 'none', filterDirty: true });
  new RemoteSyncController(app).adoptServerFilter({ imageFilter: 'custom', filterColor: '#010203' });
  assert.equal(app.imageFilter, 'custom');
  assert.equal(app.filterColor, '#010203');
  assert.equal(app.filterDirty, false);   // the server's filter is now ours
});

test('adoptServerFilter: legacy blackAndWhite maps to bw; missing layout is a no-op', () => {
  const app = makeApp({ imageFilter: 'sepia' });
  new RemoteSyncController(app).adoptServerFilter({ blackAndWhite: true });
  assert.equal(app.imageFilter, 'bw');
  const app2 = makeApp({ imageFilter: 'sepia' });
  new RemoteSyncController(app2).adoptServerFilter(null);
  assert.equal(app2.imageFilter, 'sepia'); // unchanged
});

test('adoptServerPageFormat: restores a named format + custom dims', () => {
  const app = makeApp();
  new RemoteSyncController(app).adoptServerPageFormat({ pageSize: 'a4', customPageWidth: 10, customPageHeight: 20 });
  assert.equal(app.pageSize, 'A4');
  assert.equal(app.customPageWidth, 10);
  assert.equal(app.customPageHeight, 20);
});

test('adoptServerFormulas: restores expressions + drives the formula UI', () => {
  const app = makeApp();
  new RemoteSyncController(app).adoptServerFormulas({ allowFormulas: true, formulaX: 'x*2', formulaY: 'y+1' });
  assert.equal(app.allowFormulas, true);
  assert.equal(app.formulaX, 'x*2');
  assert.equal(app.formulaY, 'y+1');
  assert.deepEqual(app.rec.syncFormulaUI, [true]);
  assert.deepEqual(app.rec.showFormulaError, [false]);
});

test('renderResultBytes: no image → null', async () => {
  const bytes = await new RemoteSyncController(makeApp({ image: null })).renderResultBytes();
  assert.equal(bytes, null);
});
