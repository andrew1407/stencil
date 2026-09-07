import { test } from 'node:test';
import assert from 'node:assert';
import { installDom, createStubElement } from './helpers/dom.js';

// The toolbar's section hairlines hide when the wrapping row breaks between their two
// sections (js/ui/toolbar.js syncWrappedSeparators) — and are shown again first, so a
// second pass at the same geometry lands on the same answer.
const row = (tops) => {
  // sections and separators alternate: S sep S sep S …; `tops` are the sections' rows
  const els = [];
  tops.forEach((t, i) => {
    if (i) { const sep = createStubElement('div'); sep.classList.add('ctrl-sep'); els.push(sep); }
    els.push(createStubElement('div', { getBoundingClientRect: () => ({ top: t, left: 0, right: 0, bottom: t + 40, width: 100, height: 40 }) }));
  });
  els.forEach((el, i) => { el.previousElementSibling = els[i - 1] || null; el.nextElementSibling = els[i + 1] || null; });
  const root = { querySelectorAll: (sel) => (sel === '.ctrl-sep' ? els.filter((e) => e.classList.contains('ctrl-sep')) : []) };
  return { root, seps: els.filter((e) => e.classList.contains('ctrl-sep')) };
};

// …and a REFLOWING row, which is what the toolbar really is: hiding a hairline takes its
// width out of the row, and the section after it can then come back up beside its
// neighbour — where its own hairlines are wanted again. Sections are `secW` wide, a shown
// separator costs `sepW`, and each item that does not fit starts a new row.
const flowRow = (count, { cap = 350, secW = 100, sepW = 22 } = {}) => {
  const els = [];
  const isSep = (el) => el.classList.contains('ctrl-sep');
  const rows = new Map();
  const layout = () => {
    rows.clear();
    let x = 0;
    let row = 0;
    for (const el of els) {
      const w = isSep(el) ? (el.classList.contains('ctrl-sep-wrapped') ? 0 : sepW) : secW;
      if (x > 0 && x + w > cap) { row += 1; x = 0; }
      rows.set(el, row);
      x += w;
    }
  };
  for (let i = 0; i < count; i++) {
    if (i) {
      const sep = createStubElement('div');
      sep.classList.add('ctrl-sep');
      sep.getBoundingClientRect = () => { layout(); return { top: rows.get(sep) * 60, height: 40 }; };
      els.push(sep);
    }
    const sec = createStubElement('div');
    sec.getBoundingClientRect = () => { layout(); return { top: rows.get(sec) * 60, height: 40 }; };
    els.push(sec);
  }
  els.forEach((el, i) => { el.previousElementSibling = els[i - 1] || null; el.nextElementSibling = els[i + 1] || null; });
  const root = { querySelectorAll: (sel) => (sel === '.ctrl-sep' ? els.filter(isSep) : []) };
  return { root, els, seps: els.filter(isSep), rowOf: (el) => { layout(); return rows.get(el); } };
};

test('the hairlines settle against the layout they themselves produce', async () => {
  const dom = installDom();
  try {
    const { syncWrappedSeparators, WRAPPED_SEP_CLASS } = await import('../js/ui/toolbar.js');
    // Six sections over three rows: hiding the hairline that wrapped to the head of row
    // two pulls that row's own tail back up beside it — and the line between THOSE two is
    // then wanted again. Measuring once, against the all-shown layout, leaves it hidden:
    // the reported bug (Zoom | Page | Data on one row with the lines between them gone).
    const { root, seps, els, rowOf } = flowRow(6, { cap: 350, secW: 100, sepW: 22 });
    syncWrappedSeparators(root);
    // Whatever it decided, every answer must describe the layout it ended in: a hidden
    // hairline straddles two rows, a shown one has both its neighbours on the same row.
    for (const sep of seps) {
      const same = rowOf(sep.previousElementSibling) === rowOf(sep.nextElementSibling);
      assert.equal(sep.classList.contains(WRAPPED_SEP_CLASS), !same,
        'a hairline is hidden exactly when its two sections are on different rows');
    }
    // Nothing is left in front of a row: the shove this function exists to stop.
    for (const sep of seps)
      if (!sep.classList.contains(WRAPPED_SEP_CLASS))
        assert.equal(rowOf(sep), rowOf(sep.previousElementSibling), 'no hairline heads a row');
    assert.ok(els.length > 0);
  } finally { dom.restore(); }
});

test('a separator between two sections on different rows is hidden; on one row it stays', async () => {
  const dom = installDom();
  try {
    const { syncWrappedSeparators, WRAPPED_SEP_CLASS } = await import('../js/ui/toolbar.js');
    const { root, seps } = row([0, 0, 60, 60, 120]);   // rows: A B | C D | E
    syncWrappedSeparators(root);
    assert.deepStrictEqual(seps.map((s) => s.classList.contains(WRAPPED_SEP_CLASS)), [false, true, false, true]);
    // Same geometry again: identical answer (every separator is reset before measuring).
    syncWrappedSeparators(root);
    assert.deepStrictEqual(seps.map((s) => s.classList.contains(WRAPPED_SEP_CLASS)), [false, true, false, true]);
    // Wide again: all on one row → nothing hidden.
    const wide = row([0, 0, 0, 0, 0]);
    syncWrappedSeparators(wide.root);
    assert.ok(wide.seps.every((s) => !s.classList.contains(WRAPPED_SEP_CLASS)));
  } finally { dom.restore(); }
});
