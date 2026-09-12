// window.stencil.chat — the console facade's assistant half (js/console/assistantApi.js).
// Every member is a thin delegation to the panel's own scripting surface (app.chat), so
// drive it with a recording stub and assert what reaches the panel: no source-text pins.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const { createStencil } = await import('../js/console/stencilApi.js');

// `connections` pre-set keeps createStencil off the auto-connect boot path; the rest is
// the minimum the facade touches before a chat member is read.
const makeApp = (chat = null) => ({
  connections: {},
  tabs: { onPeers() {} },
  storage: { incognito: false, store: { list: () => [] } },
  lines: [],
  chat,
});

test('chat.history returns the panel\'s settled transcript', () => {
  const rows = [{ role: 'user', text: 'hi' }, { role: 'assistant', text: 'there' }];
  const stencil = createStencil(makeApp({ history: () => rows }));
  assert.deepEqual(stencil.chat.history, rows);
});

test('chat.abort forwards the Stop button\'s path and reports whether a turn was running', () => {
  let called = 0;
  const stencil = createStencil(makeApp({ abort: () => { called++; return true; } }));
  assert.equal(stencil.chat.abort(), true);
  assert.equal(called, 1);
});

test('chat.clear runs the panel\'s own reset and returns the facade for chaining', () => {
  let cleared = 0;
  const app = makeApp({ clear: () => { cleared++; } });
  const stencil = createStencil(app);
  assert.equal(stencil.chat.clear(), stencil);
  assert.equal(cleared, 1);
});

test('every chat member refuses before the panel wires', () => {
  const stencil = createStencil(makeApp(null));
  for (const run of [() => stencil.chat.history, () => stencil.chat.abort(), () => stencil.chat.clear()]) {
    assert.throws(run, /Chat panel not ready/);
  }
});
