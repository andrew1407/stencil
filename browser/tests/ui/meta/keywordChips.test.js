// The keywords field's list rules (js/ui/keywordChips.js), DOM-free. Desktop twin:
// KeywordChips::normalize / ::parse / ::addTo, pinned by the same cases in
// keywordChips.headless.cpp.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { addKeywords, normalizeKeyword, parseKeywords } from '../../../js/ui/meta/keywordChips.js';

test('a keyword is whatever was typed, however many words — never split', () => {
  assert.equal(normalizeKeyword('kitchen remodel'), 'kitchen remodel');
  assert.equal(normalizeKeyword('  Field   Notes  '), 'field notes');   // trimmed, collapsed
  assert.equal(normalizeKeyword('Maps'), 'maps');
  assert.equal(normalizeKeyword('   '), '');
  assert.equal(normalizeKeyword(null), '');
  // A comma is part of the keyword now; it is not a separator.
  assert.equal(normalizeKeyword('a, b'), 'a, b');
});

test('parse cleans a stored list and drops repeats, in order', () => {
  assert.deepEqual(parseKeywords(['Maps', 'kitchen remodel', 'maps']), ['maps', 'kitchen remodel']);
  assert.deepEqual(parseKeywords([' ', '', null]), []);
  assert.deepEqual(parseKeywords([]), []);
});

test('a new keyword lands at the front', () => {
  assert.deepEqual(addKeywords(['b', 'c'], 'a'), ['a', 'b', 'c']);
  assert.deepEqual(addKeywords([], 'kitchen remodel'), ['kitchen remodel']);
});

test('a keyword already held moves to the front instead of doubling', () => {
  assert.deepEqual(addKeywords(['a', 'b', 'c'], 'c'), ['c', 'a', 'b']);
  assert.deepEqual(addKeywords(['a', 'b', 'c'], 'C'), ['c', 'a', 'b']);
  assert.deepEqual(addKeywords(['kitchen remodel', 'x'], 'Kitchen  Remodel'), ['kitchen remodel', 'x']);
  // Already first: the list is unchanged, never a duplicate.
  assert.deepEqual(addKeywords(['a', 'b'], 'a'), ['a', 'b']);
});

test('adding nothing leaves the list alone', () => {
  assert.deepEqual(addKeywords(['a', 'b'], '   '), ['a', 'b']);
  assert.deepEqual(addKeywords(['a', 'b'], ''), ['a', 'b']);
});

test('the source list is never mutated', () => {
  const list = ['a', 'b'];
  addKeywords(list, 'c');
  assert.deepEqual(list, ['a', 'b']);
});
