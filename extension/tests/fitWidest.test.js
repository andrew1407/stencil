// The filter rows' width contract. Two halves:
//
//   1. lib/fitWidest.js — a dropdown whose label swaps must not resize and shove the row,
//      so it is pinned to the WIDEST of its own options, measured at the real font. Node
//      has no layout engine and this repo adds no deps, so the stub below carries exactly
//      the surface the module touches, with a fake type metric standing in for the font.
//   2. The pages must not go back to guessing that width as a px floor, and a control a
//      page HIDES must take no width at all (the shared .chk / .accent-dd both set
//      `display`, which out-specifies the UA's [hidden] rule — an empty box in the row).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { pinToWidestOption } from '../src/lib/fitWidest.js';

// ── Stub DOM ────────────────────────────────────────────────────────────────
// A custom-select trigger: the label span reports a width derived from its text (7px a
// character, the way a real font would), and setting select.value re-syncs that label —
// exactly what customSelect.js's wrapped setter does.
const CHAR_PX = 7;

const makeSelect = (labels, { widthOf = (t) => t.length * CHAR_PX } = {}) => {
  const name = {
    text: '',
    styleAttr: '',
    style: { cssText: '', minWidth: '' },
    // A style write goes to the same place the attribute does, so restore/read agree.
    getAttribute: () => name.styleAttr,
    setAttribute: (_, v) => { name.styleAttr = v; name.style.cssText = v; },
    get offsetWidth() { return widthOf(name.text); },
  };
  const options = labels.map(([value, textContent]) => ({ value, textContent }));
  const select = {
    options,
    _value: options[0].value,
    get value() { return select._value; },
    set value(v) {
      select._value = v;
      const opt = options.find((o) => o.value === v);
      name.text = opt ? opt.textContent : '';
    },
    closest: (sel) => (sel === '.accent-dd' ? wrap : null),
  };
  const wrap = { querySelector: (sel) => (sel === '.accent-dd-name' ? name : null) };
  select.value = options[0].value;          // seed the label, as enhanceSelect's sync does
  return { select, name };
};

const MODES = [['common', 'Name + keywords'], ['names', 'Names only'], ['keywords', 'Keywords only']];

test('the pin is the WIDEST option, not the selected one', () => {
  const { select, name } = makeSelect(MODES);
  select.value = 'names';                   // the NARROWEST label is showing
  const pinned = pinToWidestOption(select);
  assert.equal(pinned, 'Name + keywords'.length * CHAR_PX);
  assert.equal(name.style.minWidth, `${'Name + keywords'.length * CHAR_PX}px`);
});

test('the width is stable across every label swap', () => {
  const { select, name } = makeSelect(MODES);
  const pinned = pinToWidestOption(select);
  // What the control ends up at: its own label, floored by the pin.
  const widthNow = () => Math.max(parseFloat(name.style.minWidth), name.offsetWidth);
  for (const [value] of MODES) {
    select.value = value;
    assert.equal(widthNow(), pinned, `${value} resized the control`);
  }
});

test('the pin is the widest label and not a pixel more', () => {
  // The point of measuring: no dead space left over once the widest label is showing.
  const { select, name } = makeSelect(MODES);
  const pinned = pinToWidestOption(select);
  select.value = 'common';
  assert.equal(name.offsetWidth, pinned, 'the widest label exactly fills the pinned width');
});

test('it measures at the real font, so a bigger one pins wider', () => {
  // A hard-coded px floor cannot do this: another font size, a zoom level or a translated
  // label moves the answer, and the guess stays put.
  const small = makeSelect(MODES);
  const large = makeSelect(MODES, { widthOf: (t) => t.length * CHAR_PX * 2 });
  assert.equal(pinToWidestOption(large.select), pinToWidestOption(small.select) * 2);
});

test('a longer (translated) label pins wider, without reordering the options', () => {
  const { select } = makeSelect([
    ['common', 'Name + keywords'],
    ['names', 'Nur Namen'],
    ['keywords', 'Schlüsselwörter und Namen'],   // the widest is no longer the first
  ]);
  assert.equal(pinToWidestOption(select), 'Schlüsselwörter und Namen'.length * CHAR_PX);
});

test('the walk leaves the selection and the inline style exactly as it found them', () => {
  const { select, name } = makeSelect(MODES);
  name.setAttribute('style', 'color: red;');
  select.value = 'keywords';
  pinToWidestOption(select);
  assert.equal(select.value, 'keywords', 'the user\'s choice survives the measurement');
  assert.match(name.styleAttr, /color: red;/, 'and so does whatever style was there');
  assert.doesNotMatch(name.styleAttr, /visibility:\s*hidden/, 'the measuring style is undone');
});

test('an un-enhanced select is a no-op, not a crash', () => {
  assert.equal(pinToWidestOption(null), 0);
  assert.equal(pinToWidestOption({ options: [], closest: () => null }), 0);
});

// ── The pages keep their side of it ─────────────────────────────────────────

test('the options page pins the search-mode list and floors nothing by hand', () => {
  const html = readFileSync(new URL('../src/options/options.html', import.meta.url), 'utf8');
  const js = readFileSync(new URL('../src/options/options.js', import.meta.url), 'utf8');
  assert.match(js, /pinToWidestOption\(pinSearchModeEl\)/,
    'the fixed three-label list is pinned to its widest label');
  // The filter row's controls size to the label they show. A px floor here is a guess —
  // it was 170px, ~60px of dead space around "All sites (n)" and "Any storage".
  const row = /\.pin-filters select \{([^}]*)\}/.exec(html);
  const dd = /\.pin-filters \.accent-dd \{([^}]*)\}/.exec(html);
  assert.ok(row && dd, 'the pin-filters sizing rules are still there');
  for (const [what, rule] of [['select', row[1]], ['.accent-dd', dd[1]]])
    assert.doesNotMatch(rule, /min-width/, `.pin-filters ${what} must not re-floor its width`);
  // …and the search box sizes to its typing room instead of claiming the whole row: the
  // page's form fields are `width:100%`, which it inherits unless it says otherwise.
  const search = /\.pin-filters input\[type="search"\] \{([^}]*)\}/.exec(html);
  assert.ok(search, 'the pin-filters search rule is still there');
  assert.match(search[1], /width:auto/, 'the search box must not take the form-field full width');
});

test('a hidden chip or dropdown takes no width', () => {
  // Both components set `display` themselves, which out-specifies the UA's [hidden] rule:
  // without these the popup's "server pins" pill and its per-server list still paint an
  // (empty) box in the filter row with no server connected.
  const css = readFileSync(new URL('../src/lib/theme.css', import.meta.url), 'utf8');
  assert.match(css, /\.chk\[hidden\] \{ display: none; \}/);
  assert.match(css, /\.accent-dd\[hidden\] \{ display: none; \}/);
  // The wrapper only ever gets `hidden` because customSelect mirrors the select's.
  const cs = readFileSync(new URL('../src/lib/customSelect.js', import.meta.url), 'utf8');
  assert.match(cs, /wrap\.hidden = selectEl\.hidden/);
});
