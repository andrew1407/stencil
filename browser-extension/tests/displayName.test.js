// Tests for shortName() (src/lib/displayName.js) — the extension's port of
// browser/js/utils.js `shortName`. These mirror browser/tests/shortName.test.js case
// for case: the two must agree, or the same image reads with a different name in the
// popup than in the editor it hands off to.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { shortName, NAME_DISPLAY_CHARS } from '../src/lib/displayName.js';

test('the limit matches the browser helper', () => {
  assert.equal(NAME_DISPLAY_CHARS, 28);
});

test('leaves a name at or under the limit untouched', () => {
  assert.equal(shortName('portrait'), 'portrait');
  const exact = 'x'.repeat(NAME_DISPLAY_CHARS);
  assert.equal(shortName(exact), exact);
});

test('middle-ellipsises a longer name to exactly the limit', () => {
  const slug = 'MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_';
  const out = shortName(slug);
  assert.equal(out.length, NAME_DISPLAY_CHARS);
  assert.equal(out, 'MV5BODg3MzYwMj…NzAzNw@@._V1_');
});

test('keeps the tail, so names differing only at the end stay distinguishable', () => {
  const base = 'MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_';
  assert.notEqual(shortName(`${base}.jpg`), shortName(`${base}-copy.jpg`));
});

test('splits the budget head-heavy on an odd remainder', () => {
  assert.equal(shortName('abcdefghijklmnop', 8), 'abcd…nop');
});

test('degrades safely at tiny limits and on non-string input', () => {
  assert.equal(shortName('abcdef', 1), '…');
  assert.equal(shortName('abcdef', 2), 'a…');
  assert.equal(shortName(null), '');
  assert.equal(shortName(undefined), '');
});
