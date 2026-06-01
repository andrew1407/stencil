import { test } from 'node:test';
import assert from 'node:assert';
import { rowMatches } from '../js/ui/base.js';

test('rowMatches: empty query matches any text', () => {
    assert.strictEqual(rowMatches('anything at all', ''), true);
});

test('rowMatches: whitespace-only query trims to empty and matches', () => {
    assert.strictEqual(rowMatches('Toggle Points', '   '), true);
});

test('rowMatches: case-insensitive substring match', () => {
    assert.strictEqual(rowMatches('Toggle Points', 'points'), true);
});

test('rowMatches: non-substring returns false', () => {
    assert.strictEqual(rowMatches('Toggle Points', 'lines'), false);
});

test('rowMatches: query with surrounding spaces is trimmed before compare', () => {
    assert.strictEqual(rowMatches('Toggle Points', ' tog '), true);
});

test('rowMatches: mixed-case/numeric text with lowercase query', () => {
    assert.strictEqual(rowMatches('Zoom In (Ctrl+2)', 'ctrl+2'), true);
});

test('rowMatches: nullish text never throws and only matches empty query', () => {
    assert.strictEqual(rowMatches(undefined, ''), true);
    assert.strictEqual(rowMatches(undefined, 'x'), false);
});
