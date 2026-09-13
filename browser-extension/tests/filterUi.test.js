// Tests for src/lib/filterUi.js — the filter controls extracted from popup.js: the
// format-pill model, reading the controls, and the persist/restore/mirror machinery.
// Storage rides tests/helpers/chromeStub.js; the DOM is the usual stub document.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { COMMON_FORMATS, FILTERS_KEY, formatListFor, formatPillsHtml, createFilterUi } from '../src/lib/filterUi.js';
import { UNKNOWN_FORMAT, VIDEO_FORMATS, passesFilters } from '../src/lib/filters.js';
import { diffListKeys, createFilterTransition, FILTER_OUT_CLASS } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';
import { readFileSync } from 'node:fs';
import { installChromeStub } from './helpers/chromeStub.js';

// ── Pure: the pill model ──

test('formatListFor: common + video + page extras + etc last, present marked', () => {
  const { formats, present } = formatListFor([
    { kind: 'img', src: 'http://x/a.png' },
    { kind: 'img', src: 'http://x/b.jxl' },     // an extra the commons don\'t list
    { kind: 'img', src: 'blob:opaque' },        // undetectable → etc present
  ]);
  assert.deepEqual(formats.slice(0, COMMON_FORMATS.length), COMMON_FORMATS);
  for (const v of VIDEO_FORMATS) assert.ok(formats.includes(v));
  assert.ok(formats.includes('jxl'), 'a page-only format is offered');
  assert.equal(formats[formats.length - 1], UNKNOWN_FORMAT, 'etc is always last');
  assert.ok(present.has('png') && present.has('jxl') && present.has(UNKNOWN_FORMAT));
  assert.ok(!present.has('gif'));
});

test('formatListFor without undetectable items still offers etc, marked absent', () => {
  const { formats, present } = formatListFor([{ kind: 'img', src: 'http://x/a.png' }]);
  assert.equal(formats[formats.length - 1], UNKNOWN_FORMAT);
  assert.ok(!present.has(UNKNOWN_FORMAT));
});

test('formatPillsHtml: all pills start checked; absent ones are dimmed and titled', () => {
  const html = formatPillsHtml(['png', 'gif'], new Set(['png']));
  assert.match(html, /<label class="chk"><input type="checkbox" value="png" checked>PNG<\/label>/);
  assert.match(html, /<label class="chk absent" data-title="Not present on this page"><input type="checkbox" value="gif" checked>GIF<\/label>/);
});

// ── Stub DOM: the f-* controls + a pill box that parses its own innerHTML ──
const control = (value = '', checked = false) => ({ value, checked, textContent: '' });
const stubDom = () => {
  const els = {
    'f-search': control(' cat '), 'f-regex': control('', true),
    'f-minw': control('10'), 'f-maxw': control(''), 'f-minh': control('abc'), 'f-maxh': control('200'),
    'f-img': control('', true), 'f-bg': control('', false), 'f-video': control('', true),
    'f-poster': control('', false), 'f-meta': control('', true),
    'f-fmt-toggle': control(),
  };
  const box = {
    inputs: [],
    set innerHTML(html) {
      this.inputs = [...html.matchAll(/value="([^"]+)"/g)].map((m) => ({
        value: m[1], checked: true, handlers: [],
        addEventListener: function (t, fn) { if (t === 'change') this.handlers.push(fn); },
      }));
    },
    get innerHTML() { return ''; },
    querySelectorAll: function () { return this.inputs; },
  };
  els['f-formats'] = box;
  return { doc: { getElementById: (id) => els[id] || null }, els, box };
};

// ── Reading ──

test('read() maps every control, trims the search, and parses sizes as numbers', () => {
  const { doc, box } = stubDom();
  const ui = createFilterUi({ doc });
  box.innerHTML = formatPillsHtml(['png', 'gif'], new Set(['png']));
  box.inputs[1].checked = false;
  const f = ui.read();
  assert.deepEqual(f, {
    search: 'cat', regex: true, formats: ['png'],
    minW: 10, maxW: null, minH: null, maxH: 200,
    includeImg: true, includeBg: false, includeVideo: true, includePosters: false, includeMeta: true,
  });
});

// ── Pills + toggle label ──

test('populateFormats builds the pills, wires onChange, and syncs the toggle label', () => {
  const { doc, els, box } = stubDom();
  let changes = 0;
  const ui = createFilterUi({ doc, onChange: () => changes++ });
  ui.populateFormats([{ kind: 'img', src: 'http://x/a.png' }]);
  assert.ok(box.inputs.length > 0);
  assert.ok(box.inputs.every((i) => i.checked), 'all pills start checked (= no filtering)');
  assert.equal(els['f-fmt-toggle'].textContent, 'Deselect all');

  // A pill toggle runs the owner's applyFilters and flips the label.
  box.inputs[0].checked = false;
  box.inputs[0].handlers[0]();
  assert.equal(changes, 1);
  assert.equal(els['f-fmt-toggle'].textContent, 'Select all');
});

// ── Persistence round-trip ──

test('save stores the OFF formats; load + restore + populate bring the state back', async () => {
  const stub = installChromeStub();
  try {
    const { doc, box } = stubDom();
    const ui = createFilterUi({ doc });
    ui.populateFormats([{ kind: 'img', src: 'http://x/a.png' }]);
    const png = box.inputs.find((i) => i.value === 'png');
    png.checked = false;
    ui.save();
    await new Promise((r) => setTimeout(r, 0));
    const saved = (await chrome.storage.local.get(FILTERS_KEY))[FILTERS_KEY];
    assert.deepEqual(saved.disabledFormats, ['png'], 'the OFF ones are stored (new formats default on)');
    assert.equal(saved.search, 'cat');
    assert.equal(saved.minW, 10);

    // A fresh surface: load, restore the static controls, rebuild the pills.
    const fresh = stubDom();
    fresh.els['f-search'].value = '';
    fresh.els['f-regex'].checked = false;
    const ui2 = createFilterUi({ doc: fresh.doc });
    await ui2.load();
    ui2.restoreStatic();
    assert.equal(fresh.els['f-search'].value, 'cat');
    assert.equal(fresh.els['f-regex'].checked, true);
    ui2.populateFormats([{ kind: 'img', src: 'http://x/a.png' }]);
    const png2 = fresh.box.inputs.find((i) => i.value === 'png');
    assert.equal(png2.checked, false, 'the persisted OFF format survives the rebuild');
    assert.ok(fresh.box.inputs.filter((i) => i !== png2).every((i) => i.checked));
  } finally { stub.restore(); }
});

test('a torn-down storage never throws: load nulls, save is best-effort', async () => {
  const stub = installChromeStub({ storageThrows: true });
  try {
    const { doc, box } = stubDom();
    const ui = createFilterUi({ doc });
    await ui.load();
    box.innerHTML = formatPillsHtml(['png'], new Set(['png']));
    ui.save();   // must not throw
  } finally { stub.restore(); }
});

// ── Cross-surface mirroring ──

test('acceptExternal skips the echo of our own write but adopts another surface\'s', async () => {
  const stub = installChromeStub();
  try {
    const { doc, els, box } = stubDom();
    const ui = createFilterUi({ doc });
    ui.populateFormats([{ kind: 'img', src: 'http://x/a.png' }]);
    ui.save();
    await new Promise((r) => setTimeout(r, 0));
    const echo = JSON.parse(JSON.stringify((await chrome.storage.local.get(FILTERS_KEY))[FILTERS_KEY]));
    assert.equal(ui.acceptExternal(echo), false, 'our own write mirrors back as a no-op');

    const other = { ...echo, search: 'dog', disabledFormats: ['gif'] };
    assert.equal(ui.acceptExternal(other), true);
    assert.equal(els['f-search'].value, 'dog');
    const gif = box.inputs.find((i) => i.value === 'gif');
    assert.equal(gif.checked, false);
    assert.equal(ui.acceptExternal(null), false);
  } finally { stub.restore(); }
});

// ── What the popup's list actually animates when a filter changes ────────────
// The scanned-image list is rebuilt wholesale on every pill/search change, so what
// enters and leaves is decided purely by the keys of the two renders. These pin that
// decision (and the visible set after a burst of changes) without a browser; the
// transition's own mechanics live in tests/motion.test.js.

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
