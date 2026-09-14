import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement } from './helpers/dom.js';

// The toolbar's hairlines are painted by .ctrl-section::before inside the column gap, so
// they take no width: a section that begins a wrapped row drops the one in front of it
// (js/ui/toolbarSeparators.js) and nothing about that choice can move the wrap.
const GAP = 21;

// A wrapping row of sections only — the real toolbar's flow. Each section is `secW` wide
// and items are GAP apart; whatever the marking says, the widths never change.
const flowRow = (count, { cap = 350, secW = 100 } = {}) => {
  const rows = new Map();
  const els = [];
  const layout = () => {
    rows.clear();
    let x = 0;
    let row = 0;
    for (const el of els) {
      const need = x === 0 ? secW : GAP + secW;
      if (x > 0 && x + need > cap) { row += 1; x = secW; } else { x += need; }
      rows.set(el, row);
    }
  };
  for (let i = 0; i < count; i++) {
    const sec = createStubElement('div');
    sec.classList.add('ctrl-section');
    sec.getBoundingClientRect = () => { layout(); return { top: rows.get(sec) * 60, height: 40 }; };
    els.push(sec);
  }
  els.forEach((el, i) => { el.previousElementSibling = els[i - 1] || null; el.nextElementSibling = els[i + 1] || null; });
  const root = { querySelectorAll: (sel) => (sel === '.ctrl-section' ? els : []) };
  return { root, els, rowOf: (el) => { layout(); return rows.get(el); } };
};

// Sections at fixed rows, for the cases a flow model cannot pose exactly.
const fixedRow = (tops) => {
  const els = tops.map((t) => {
    const sec = createStubElement('div', { getBoundingClientRect: () => ({ top: t, height: 40 }) });
    sec.classList.add('ctrl-section');
    return sec;
  });
  return { els, root: { querySelectorAll: (sel) => (sel === '.ctrl-section' ? els : []) } };
};

test('every pair of sections sharing a row keeps its hairline', async () => {
  const dom = installDom();
  try {
    const { syncWrappedSeparators, WRAPPED_SEP_CLASS } = await import('../js/ui/toolbar.js');
    // The reported bug: Formula and Data side by side with the line between them gone,
    // because hiding it was what pulled Data up beside Formula in the first place.
    for (const cap of [230, 260, 350, 480, 620, 1000]) {
      const { root, els, rowOf } = flowRow(7, { cap, secW: 100 });
      syncWrappedSeparators(root);
      for (let i = 1; i < els.length; i++) {
        const same = rowOf(els[i]) === rowOf(els[i - 1]);
        assert.equal(els[i].classList.contains(WRAPPED_SEP_CLASS), !same,
          `cap ${cap}: a hairline is dropped exactly when the row broke in front of it`);
      }
      // Nothing is left in front of a row: the shove this function exists to stop.
      assert.ok(els[0].classList.contains(WRAPPED_SEP_CLASS), 'the first section heads a row');
    }
  } finally { dom.restore(); }
});

test('one pass is a fixed point: a second call at the same geometry changes nothing', async () => {
  const dom = installDom();
  try {
    const { syncWrappedSeparators, WRAPPED_SEP_CLASS } = await import('../js/ui/toolbar.js');
    const { root, els } = fixedRow([0, 0, 60, 60, 120]);   // rows: A B | C D | E
    const marks = () => els.map((s) => s.classList.contains(WRAPPED_SEP_CLASS));
    syncWrappedSeparators(root);
    assert.deepStrictEqual(marks(), [true, false, true, false, true]);
    syncWrappedSeparators(root);
    assert.deepStrictEqual(marks(), [true, false, true, false, true]);
    // Wide again: one row → only the leading section is bare.
    const wide = fixedRow([0, 0, 0, 0, 0]);
    syncWrappedSeparators(wide.root);
    assert.deepStrictEqual(wide.els.map((s) => s.classList.contains(WRAPPED_SEP_CLASS)),
      [true, false, false, false, false]);
    // A root with nothing to sync is not an error (the fullscreen clone before it fills).
    syncWrappedSeparators(null);
    syncWrappedSeparators({});
  } finally { dom.restore(); }
});

test('the hairline is painted out of flow, so marking it cannot move the wrap', () => {
  const css = readFileSync(new URL('../css/layout/controlRows.css', import.meta.url), 'utf8');
  const toolbar = readFileSync(new URL('../js/ui/toolbar.js', import.meta.url), 'utf8');
  assert.match(css, /\.ctrl-section \+ \.ctrl-section::before \{[^}]*position: absolute;/);
  assert.match(css, /\.ctrl-section\.ctrl-sep-wrapped::before \{ content: none; \}/);
  // No separator ELEMENT anywhere: one between two sections would put its own width back
  // into the wrap it is deciding, and the answer would oscillate again.
  assert.ok(!toolbar.includes('ctrl-sep'), 'the toolbar emits no separator element');
  assert.ok(!/^\.ctrl-sep \{/m.test(css), 'no separator element is styled');
});
