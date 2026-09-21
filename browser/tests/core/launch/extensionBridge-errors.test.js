// The extension bridge's refusals and message hygiene (js/core/extensionBridge.js): an unknown
// request type, foreign frames, id correlation, no window at all, and crop.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { makeApp, setUp, request, flush, wireExtensionBridge } from '../../helpers/extensionBridgeRig.js';

test('an unknown request type is answered with an error, not silence', async () => {
  const { win } = setUp();
  request(win, 'ext-16', 'delete-everything', {});
  await flush();

  assert.deepEqual(win.replies[0], {
    source: 'stencil-ext-res', id: 'ext-16', ok: false, error: 'unknown request',
  });
});

test('messages from another frame — or without our tag — are ignored entirely', async () => {
  const { win, app } = setUp();
  // Same shape, different source: an iframe or an opener impersonating the bridge.
  win.deliver({ source: 'stencil-ext-req', id: 'x', request: 'state', payload: {} }, { name: 'other-frame' });
  // Our window, but someone else's message bus traffic.
  win.deliver({ source: 'stencil-page-api', message: { type: 'stencil-page-open' } });
  win.deliver({ source: 'stencil-ext-req', request: 'state' });   // no id to correlate → not ours
  win.deliver(null);
  await flush();

  assert.deepEqual(win.replies, []);
  assert.equal(app.imports.length, 0);
});

test('replies are id-correlated, so overlapping requests never cross', async () => {
  const { win } = setUp();
  request(win, 'a', 'state', { thumbnail: false });
  request(win, 'b', 'switch', { projectId: 'p2' });
  request(win, 'c', 'state', { thumbnail: false });
  await flush();

  assert.deepEqual(win.replies.map((r) => r.id), ['a', 'b', 'c']);
  assert.equal(win.replies[0].result.projectId, 'p1');
  assert.equal(win.replies[2].result.projectId, 'p2');   // the switch in between is visible
});

test('wiring without a window is a no-op (Node / a page with no message bus)', () => {
  assert.doesNotThrow(() => wireExtensionBridge(makeApp(), null));
});

test('crop opens the editor\'s own crop dialog (toolbar button path); a blank editor refuses', async () => {
  const clicks = [];
  globalThis.document.getElementById = (id) => (id === 'crop-image' ? { click: () => clicks.push(id) } : null);
  try {
    const { win } = setUp();
    request(win, 'crop-1', 'crop', {});
    await flush();
    const res = win.replies[0];
    assert.equal(res.id, 'crop-1');
    assert.equal(res.ok, true);
    assert.deepEqual(clicks, ['crop-image']);

    const blank = setUp({ image: null });
    request(blank.win, 'crop-2', 'crop', {});
    await flush();
    assert.equal(blank.win.replies[0].ok, false, 'nothing to crop on a blank editor');
    assert.deepEqual(clicks, ['crop-image'], 'the button was not clicked again');
  } finally {
    delete globalThis.document.getElementById;
  }
});
