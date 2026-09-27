// lib/numericWatch.js: the fields on the page upgrade at once, and every one a later render adds
// — itself a numeric input, or anywhere inside an added subtree — upgrades as it appears.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const field = () => {
  const attrs = { step: '1' };
  return {
    nodeType: 1, type: 'number', value: '3', dataset: {},
    getAttribute: (k) => attrs[k] ?? null,
    addEventListener() {},
    matches: (sel) => sel === 'input[type="number"]',
  };
};
const holder = (...inputs) => ({
  nodeType: 1, matches: () => false, querySelectorAll: () => inputs,
});

const onPage = field();
globalThis.document = { body: { tag: 'body' }, querySelectorAll: () => [onPage] };
let observer = null;
globalThis.MutationObserver = class {
  constructor(fn) { this.fn = fn; observer = this; }
  observe(root, opts) { this.root = root; this.opts = opts; }
};

const { watchNumericInputs } = await import('../../src/lib/numericWatch.js');

test('the page upgrades now, and whatever is added later upgrades as it lands', () => {
  const obs = watchNumericInputs();
  assert.equal(obs, observer);
  assert.equal(onPage.type, 'text', 'a field already on the page takes an expression');
  assert.deepEqual([observer.root, observer.opts], [document.body, { childList: true, subtree: true }]);
  const direct = field();
  const nested = field();
  observer.fn([{ addedNodes: [direct, { nodeType: 3 }, holder(nested)] }]);
  assert.equal(direct.type, 'text', 'an added numeric input');
  assert.equal(nested.type, 'text', 'one inside an added subtree');
  assert.equal(direct.dataset.numericEnhanced, '1');
});
