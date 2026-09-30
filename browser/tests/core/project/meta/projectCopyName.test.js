// "Make a copy" naming — mirrored case for case by core/tests/state/ProjectsStore.test.cpp, but
// the 80 cap counts UTF-16 units here and bytes there, as each side's validateName does.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { copySuffixName } from '../../../../js/core/project/meta/projectNaming.js';

const metas = (...names) => names.map((name, i) => ({ id: `p${i}`, name }));

const CASES = [
  ['first copy takes the bare suffix', metas('photo'), 'photo', 'photo-copy'],
  ['a taken -copy moves to (1)', metas('photo', 'photo-copy'), 'photo', 'photo-copy(1)'],
  ['numbering fills the lowest gap', metas('photo-copy', 'photo-copy(1)', 'photo-copy(3)'), 'photo', 'photo-copy(2)'],
  ['copying a copy strips its suffix', metas('photo', 'photo-copy'), 'photo-copy', 'photo-copy(1)'],
  ['copying a numbered copy strips it too', metas('photo-copy', 'photo-copy(1)', 'photo-copy(2)'), 'photo-copy(2)', 'photo-copy(3)'],
  ['taken is trimmed and case-insensitive', metas('  PHOTO-COPY '), 'photo', 'photo-copy(1)'],
  ['the source name is trimmed', metas(), '  photo  ', 'photo-copy'],
  ['an empty name copies as Untitled', metas(), '   ', 'Untitled-copy'],
  ['a bare suffix copies as Untitled', metas(), '-copy(4)', 'Untitled-copy'],
  ['only the trailing suffix goes', metas(), 'a-copy b', 'a-copy b-copy'],
];

for (const [name, list, source, want] of CASES) {
  test(`copySuffixName: ${name}`, () => assert.equal(copySuffixName(list, source), want));
}

test('copySuffixName: a long base is cut so the whole name fits 80 characters', () => {
  const long = 'x'.repeat(100);
  const first = copySuffixName(metas(), long);
  assert.equal(first, 'x'.repeat(75) + '-copy');
  assert.equal(first.length, 80);
  const second = copySuffixName(metas(first), long);
  assert.equal(second, 'x'.repeat(72) + '-copy(1)');
  assert.equal(second.length, 80);
  // One UTF-16 unit here, so the é that C++ drops (two bytes across its cut) stays.
  assert.equal(copySuffixName(metas(), 'x'.repeat(74) + 'étail'), 'x'.repeat(74) + 'é-copy');
});

test('copySuffixName: a cut never splits a surrogate pair', () => {
  // The emoji's two units straddle the 75-unit cut, so both go (C++ backs off to a lead byte).
  assert.equal(copySuffixName(metas(), 'x'.repeat(74) + '😀tail'), 'x'.repeat(74) + '-copy');
});
