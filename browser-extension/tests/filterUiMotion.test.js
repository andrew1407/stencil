// The scanned-image list is rebuilt wholesale on every pill/search change, so what enters and
// leaves is decided purely by the keys of the two renders. Mechanics: tests/motion.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createFilterUi } from '../src/lib/highlight/filterUi.js';
import { passesFilters } from '../src/lib/highlight/filters.js';
import { diffListKeys, createFilterTransition, FILTER_OUT_CLASS } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';
import { stubDom } from './helpers/filterUiDom.js';

// The stub's controls carry a scenario of their own — start from "nothing filtered".
const openFilters = (els) => {
  for (const id of ['f-search', 'f-minw', 'f-maxw', 'f-minh', 'f-maxh']) els[id].value = '';
  els['f-regex'].checked = false;
  for (const id of ['f-img', 'f-bg', 'f-video', 'f-poster', 'f-meta']) els[id].checked = true;
};

const ITEMS = [
  { kind: 'img', src: 'http://x/cat.png', name: 'cat.png' },
  { kind: 'img', src: 'http://x/dog.gif', name: 'dog.gif' },
  { kind: 'bg', src: 'http://x/hero.png', name: 'hero.png' },
];
const visibleKeys = (ui) => ITEMS.filter((it) => passesFilters(it, ui.read())).map((it) => it.src);

test('narrowing the search: only the excluded rows leave, nothing else moves', () => {
  const { doc, els } = stubDom();
  const ui = createFilterUi({ doc });
  openFilters(els);
  ui.populateFormats(ITEMS);

  const before = visibleKeys(ui);
  assert.deepEqual(before, ITEMS.map((it) => it.src));
  els['f-search'].value = 'o';                       // cat is out, dog + hero stay
  const diff = diffListKeys(before, visibleKeys(ui));
  assert.deepEqual(diff, { entered: [], left: ['http://x/cat.png'] });
});

test('a format pill off drops exactly its rows; back on brings exactly them back', () => {
  const { doc, els, box } = stubDom();
  const ui = createFilterUi({ doc });
  openFilters(els);
  ui.populateFormats(ITEMS);
  const all = visibleKeys(ui);

  const png = box.inputs.find((i) => i.value === 'png');
  png.checked = false;
  const off = visibleKeys(ui);
  assert.deepEqual(diffListKeys(all, off),
    { entered: [], left: ['http://x/cat.png', 'http://x/hero.png'] });

  png.checked = true;
  assert.deepEqual(diffListKeys(off, visibleKeys(ui)),
    { entered: ['http://x/cat.png', 'http://x/hero.png'], left: [] },
    'a re-admitted row ENTERS — the list is symmetric, not one-way');
});

test('a kind toggle and the search compose: the set is right after a burst of changes', () => {
  const { doc, els } = stubDom();
  const ui = createFilterUi({ doc });
  openFilters(els);
  ui.populateFormats(ITEMS);

  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false });
  renderKeys(list, tr, visibleKeys(ui));

  // Fast typing, then a background-images toggle, then the search cleared again.
  for (const q of ['d', 'do', 'dog', 'dogx', '']) {
    els['f-search'].value = q;
    renderKeys(list, tr, visibleKeys(ui));
  }
  els['f-bg'].checked = false;
  renderKeys(list, tr, visibleKeys(ui));

  const settled = list.children.filter((li) => !li.classList.contains(FILTER_OUT_CLASS));
  assert.deepEqual(settled.map((li) => li.dataset.key), ['http://x/cat.png', 'http://x/dog.gif'],
    'the background image is the only one gone, however fast the filters changed');
  assert.equal(tr.ghostCount, 1, 'exactly one row is on its way out — no leaked animations');
});

test('the popup wires the transition around its rebuild and keys every row', () => {
  // The rebuild and the row it keys live in separate modules of the panel.
  const js = ['filters.js', 'row.js'].map((f) => readFileSync(new URL(`../src/popup/${f}`, import.meta.url), 'utf8')).join('\n');
  assert.match(js, /filterTransition\.begin\(\);\s*\n\s*listEl\.innerHTML = '';/,
    'the snapshot is taken before the wipe, or nothing can play out');
  assert.match(js, /filterTransition\.end\(\);/);
  // Every branch of applyFilters (both empty states) must reach end(), or a ghost is stranded.
  const body = js.slice(js.indexOf('const applyFilters = () => {'));
  assert.equal((body.slice(0, body.indexOf('\n};')).match(/return;/g) || []).length, 0,
    'applyFilters no longer returns early past the transition');
  assert.match(js, /li\.dataset\.key = rowKey\(image\)/);
  // A filter drop is the LIGHT effect; the destructive scatter is not reused for it.
  assert.match(js, /filterLeave\(li,/, 'a row the measurement disqualifies fades, it is not destroyed');
  assert.doesNotMatch(js, /disintegrate\(/, 'no particles for a row a filter merely excluded');
});
