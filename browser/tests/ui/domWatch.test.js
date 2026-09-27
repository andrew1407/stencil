// The numeric-field upgrade and the accessible-name pass share ONE page-wide observer: each
// registration reuses it, added elements reach every handler, tip changes reach the tip ones.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const observers = [];
globalThis.MutationObserver = class {
  constructor(cb) { this.cb = cb; this.opts = null; observers.push(this); }
  observe(root, opts) { this.root = root; this.opts = opts; }
};
globalThis.document = { body: { tag: 'body' } };
const { onElementAdded, onTipChanged } = await import('../../js/ui/domWatch.js');

test('two passes, one observer over the body, childList + tip attributes', () => {
  const seen = { added: [], tips: [] };
  onElementAdded((el) => seen.added.push(['a', el.id]));
  onElementAdded((el) => seen.added.push(['b', el.id]));
  onTipChanged((el) => seen.tips.push(el.id));
  assert.equal(observers.length, 1, 'registrations reuse the observer');
  const [obs] = observers;
  assert.equal(obs.root, document.body);
  assert.deepEqual(obs.opts, { childList: true, subtree: true, attributes: true, attributeFilter: ['data-title', 'data-tip'] });

  const el = (id) => ({ id, nodeType: 1 });
  obs.cb([
    { type: 'childList', addedNodes: [el('x'), { nodeType: 3 }] },
    { type: 'attributes', target: el('t'), addedNodes: [] },
  ]);
  assert.deepEqual(seen.added, [['a', 'x'], ['b', 'x']], 'every handler, registration order, elements only');
  assert.deepEqual(seen.tips, ['t']);
});
