// shortName() (js/utils.js) — the display-shortening applied to project and image names before they go into a
// dialog sentence or a toast, so a CDN-basename name cannot wrap across three lines and blow out a confirm.
// Parity: src/lib/displayName.js and desktop/src/support/displayName.hpp are ports of this —
// same limit, same head/tail split, and their tests mirror these cases.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { shortName, NAME_DISPLAY_CHARS } from '../../js/utils.js';

test('leaves a name at or under the limit untouched', () => {
  assert.equal(shortName('portrait'), 'portrait');
  const exact = 'x'.repeat(NAME_DISPLAY_CHARS);
  assert.equal(shortName(exact), exact, 'exactly at the limit must not be shortened');
});

test('middle-ellipsises a longer name to exactly the limit', () => {
  const slug = 'MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_';
  const out = shortName(slug);
  assert.equal(out.length, NAME_DISPLAY_CHARS);
  assert.ok(out.includes('…'), 'must carry the ellipsis');
  assert.ok(slug.startsWith(out.split('…')[0]), 'head must come from the start');
  assert.ok(slug.endsWith(out.split('…')[1]), 'tail must come from the end');
});

// The tail is the whole reason for a MIDDLE ellipsis: it holds the extension and the
// "-copy" suffix that distinguishes two otherwise identical names.
test('keeps the tail, so names differing only at the end stay distinguishable', () => {
  const base = 'MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_';
  assert.notEqual(shortName(`${base}.jpg`), shortName(`${base}-copy.jpg`));
});

test('splits the budget head-heavy on an odd remainder', () => {
  // limit 8 → keep 7 → head 4, tail 3
  assert.equal(shortName('abcdefghijklmnop', 8), 'abcd…nop');
});

test('honours an explicit limit', () => {
  // limit 5 → keep 4 → head 2, tail 2 (an even remainder splits evenly)
  assert.equal(shortName('abcdefghij', 5), 'ab…ij');
});

test('degrades safely at tiny limits and on non-string input', () => {
  assert.equal(shortName('abcdef', 1), '…');       // keep 0 → ellipsis only
  assert.equal(shortName('abcdef', 2), 'a…');      // keep 1 → head only, no tail
  assert.equal(shortName(null), '');
  assert.equal(shortName(undefined), '');
});
