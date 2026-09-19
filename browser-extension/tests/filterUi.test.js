// src/lib/filterUi.js — the format pill model, the f-* controls it reads, and the round-trip it
// persists to chrome.storage (with the cross-surface echo it must skip).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { COMMON_FORMATS, FILTERS_KEY, formatListFor, formatPillsHtml, createFilterUi } from '../src/lib/filterUi.js';
import { UNKNOWN_FORMAT, VIDEO_FORMATS } from '../src/lib/filters.js';
import { installChromeStub } from './helpers/chromeStub.js';
import { stubDom } from './helpers/filterUiDom.js';

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
