// Unit tests for the copy/download toolbar options list (js/ui/exportOptionsMenu.js):
// row content per variant, and that the hotkey hint shown on each row comes from the
// LIVE hotkeys registry (platform-formatted, bordered keycaps) rather than a hardcoded
// guess — a hardcoded "Ctrl+C" next to the Download button's Original/Tint rows would be
// wrong (those have no shortcut at all; only the download button's own "current" does).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from './helpers/dom.js';

const doc = installDom({}, {
  window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
});

const { wireExportOptionsMenu } = await import('../js/ui/export/exportOptionsMenu.js');
const { hotkeys } = await import('../js/core/hotkeys.js');
const { formatCombo } = await import('../js/utils.js');
const { keysHtml } = await import('../js/ui/tip/tipContent.js');

// Keycap markup for a hotkey id, platform-formatted exactly like the code under test: this machine's own
// `navigator` may or may not report macOS, so the expectation tracks hotkeys.isMac.
const expectedKeys = (id) => keysHtml(formatCombo(hotkeys.get(id), hotkeys.isMac), hotkeys.isMac);

// Trigger a `button` that captures whatever gets inserted after it (the popover's <ul>)
// and reports as not disabled / not under the pointer.
const makeTrigger = () => {
  let menu = null;
  const el = doc.createElement('button');
  Object.assign(el, {
    insertAdjacentElement: (pos, node) => { menu = node; },
    matches: () => false,
    getBoundingClientRect: () => ({ left: 10, top: 10, right: 40, bottom: 40, width: 30, height: 30 }),
  });
  return { el, menuOf: () => menu };
};

const openViaDblclick = (el) => el.dispatch('dblclick', { preventDefault() {} });

test('copy trigger: current/original/filter rows, each with its own live hotkey', () => {
  const { el, menuOf } = makeTrigger();
  const runs = [];
  wireExportOptionsMenu(el, { image: {}, imageFilter: 'sepia', lines: [{ points: [] }] }, {
    run: (v) => runs.push(v),
    hotkeyIds: { current: 'copyImage', original: 'copyImageOriginal', tint: 'copyImageTint' },
  });
  openViaDblclick(el);
  const rows = menuOf().children;
  assert.equal(rows.length, 3, 'no split row for copy');
  const [current, original, tint] = rows;
  assert.match(current.innerHTML, /Current \(Tint \+ Lines\/Points\)/);
  assert.ok(current.innerHTML.includes(expectedKeys('copyImage')), 'live copyImage binding, bordered keycaps');
  assert.match(original.innerHTML, /Original \(No Tint, No Lines\/Points\)/);
  assert.ok(original.innerHTML.includes(expectedKeys('copyImageOriginal')));
  assert.match(tint.innerHTML, /Filter Only \(No Lines\/Points\)/);
  assert.ok(tint.innerHTML.includes(expectedKeys('copyImageTint')));

  current.dispatch('click');
  assert.deepEqual(runs, ['current']);
  assert.equal(menuOf().hidden, true, 'a row click closes the popover');
});

test('"Filter Only" is hidden with no filter applied — it would render byte-identical to "Original"', () => {
  const { el, menuOf } = makeTrigger();
  wireExportOptionsMenu(el, { image: {}, imageFilter: 'none', lines: [{ points: [] }] }, {
    run() {},
    hotkeyIds: { current: 'copyImage', original: 'copyImageOriginal', tint: 'copyImageTint' },
  });
  openViaDblclick(el);
  const rows = menuOf().children;
  assert.equal(rows.length, 2, 'just Current + Original with no filter active');
  assert.match(rows[0].innerHTML, /Current \(Tint \+ Lines\/Points\)/);
  assert.match(rows[1].innerHTML, /Original \(No Tint, No Lines\/Points\)/);
  assert.doesNotMatch(rows[1].innerHTML, /Filter Only/);
});

test('"Current" is hidden with no lines/points drawn — it would render byte-identical to Filter Only/Original', () => {
  const { el, menuOf } = makeTrigger();
  // A filter IS active, but with nothing drawn "Current" would still just be the
  // filtered image — indistinguishable from "Filter Only" — so it stays gone.
  wireExportOptionsMenu(el, { image: {}, imageFilter: 'sepia', lines: [] }, {
    run() {},
    hotkeyIds: { current: 'copyImage', original: 'copyImageOriginal', tint: 'copyImageTint' },
  });
  openViaDblclick(el);
  const rows = menuOf().children;
  assert.equal(rows.length, 2, 'just Original + Filter Only with nothing drawn');
  assert.match(rows[0].innerHTML, /Original \(No Tint, No Lines\/Points\)/);
  assert.match(rows[1].innerHTML, /Filter Only \(No Lines\/Points\)/);
  assert.doesNotMatch(rows[0].innerHTML + rows[1].innerHTML, /Current \(Tint/);
});

test('with neither a filter nor anything drawn, only "Original" remains', () => {
  const { el, menuOf } = makeTrigger();
  wireExportOptionsMenu(el, { image: {}, imageFilter: 'none', lines: [] }, { run() {} });
  openViaDblclick(el);
  const rows = menuOf().children;
  assert.equal(rows.length, 1);
  assert.match(rows[0].innerHTML, /Original \(No Tint, No Lines\/Points\)/);
});

test('download trigger: only "current" carries a hotkey — original/filter show none, hardcoded or borrowed from copy', () => {
  const { el, menuOf } = makeTrigger();
  wireExportOptionsMenu(el, { image: {}, imageFilter: 'bw', lines: [{ points: [] }] }, {
    run() {}, currentIcon: 'download',
    hotkeyIds: { current: 'saveImage' },
  });
  openViaDblclick(el);
  const [current, original, tint] = menuOf().children;
  assert.match(current.innerHTML, /ctx-hotkey/, '"current" (saveImage) has a live binding');
  assert.doesNotMatch(original.innerHTML, /ctx-hotkey/, 'no saveImage binding exists for "original"');
  assert.doesNotMatch(tint.innerHTML, /ctx-hotkey/, 'no saveImage binding exists for "filter"');
});

test('"With Compare" is a FOURTH row while a split compare view is active, alongside (not instead of) "Current" — and disappears once compare turns off', () => {
  // 'current' and 'split' are independent rows: 'current' is always there (tint + lines/points, reachable even
  // while comparing), 'split' only while comparing — and only then carries the shared hotkey chip (user report).
  const runsSplit = [];
  const comparing = makeTrigger();
  wireExportOptionsMenu(comparing.el, { image: {}, compareMode: 'vertical', imageFilter: 'sepia', lines: [{ points: [] }] }, {
    run: (v) => runsSplit.push(v),
    currentIcon: 'download',
    hotkeyIds: { current: 'saveImage', original: 'saveImageOriginal', tint: 'saveImageTint' },
  });
  openViaDblclick(comparing.el);
  const splitRows = comparing.menuOf().children;
  assert.equal(splitRows.length, 4, 'With Compare joins Current/Original/Filter, not replaces one');
  const [split, current, original, tint] = splitRows;
  assert.match(split.innerHTML, /With Compare/);
  assert.ok(split.innerHTML.includes(expectedKeys('saveImage')), 'the split row carries the shared hotkey chip');
  assert.match(current.innerHTML, /Current \(Tint \+ Lines\/Points\)/);
  assert.doesNotMatch(current.innerHTML, /ctx-hotkey/, 'Current has no hotkey of its own while split holds it');
  assert.ok(original.innerHTML.includes(expectedKeys('saveImageOriginal')), 'Original keeps its own combo regardless');
  assert.ok(tint.innerHTML.includes(expectedKeys('saveImageTint')), 'Filter Only keeps its own combo regardless');

  split.dispatch('click');
  assert.deepEqual(runsSplit, ['split']);
  current.dispatch('click');
  // Current still dispatches literally 'current' — clicking it never gives the split.
  assert.deepEqual(runsSplit, ['split', 'current']);

  const runsPlain = [];
  const notComparing = makeTrigger();
  wireExportOptionsMenu(notComparing.el, { image: {}, compareMode: 'none', lines: [{ points: [] }] }, {
    run: (v) => runsPlain.push(v),
    currentIcon: 'download',
    hotkeyIds: { current: 'saveImage' },
  });
  openViaDblclick(notComparing.el);
  const plainRows = notComparing.menuOf().children;
  assert.equal(plainRows.length, 2, 'With Compare is gone outside compare, and so is Filter Only with no filter active');
  assert.match(plainRows[0].innerHTML, /Current \(Tint \+ Lines\/Points\)/);
  assert.ok(plainRows[0].innerHTML.includes(expectedKeys('saveImage')), 'Current gets the hotkey back');
  plainRows[0].dispatch('click');
  assert.deepEqual(runsPlain, ['current']);
});

test('no image loaded → the popover does not open', () => {
  const { el, menuOf } = makeTrigger();
  wireExportOptionsMenu(el, { image: null }, { run() {} });
  openViaDblclick(el);
  assert.equal(menuOf().hidden, true);
  assert.equal(menuOf().children.length, 0);
});
