// The shown accent (js/ui/accent/shownAccent.js): a hover preview's data-accent or a custom inline
// --accent is what a session skin follows, reported once per real change and never after unwatch.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

const { shownAccentHex, watchShownAccent } = await import('../../../js/ui/accent/shownAccent.js');

test('the preset under data-accent, a custom inline --accent over it, one call per move', () => {
  const doc = installDom({});
  const root = doc.documentElement;
  let fire = null, disconnected = false;
  globalThis.MutationObserver = class {
    constructor(fn) { fire = fn; }
    observe() {}
    disconnect() { disconnected = true; }
  };
  root.setAttribute('data-accent', 'blue');
  assert.equal(shownAccentHex(doc), '#2563eb');
  const seen = [];
  const unwatch = watchShownAccent(doc, (hex) => seen.push(hex));
  root.setAttribute('data-accent', 'orange');
  fire();
  fire();
  root.style.setProperty('--accent', '#123456');
  fire();
  assert.deepEqual(seen, ['#ea580c', '#123456'], 'a repeat with nothing moved is not reported');
  unwatch();
  assert.equal(disconnected, true);
  delete globalThis.MutationObserver;
  assert.doesNotThrow(() => watchShownAccent(doc, () => {})(), 'no observer, no watch');
});
