import { test } from 'node:test';
import assert from 'node:assert/strict';
import { matchEntries, trackableSource } from '../src/lib/ledger.js';

const e = (over = {}) => ({ source: '', resource: '', name: '', count: 1, t: 0, ...over });

test('trackableSource: only http(s) URLs are tracked', () => {
  assert.equal(trackableSource('https://a.com/i.png'), true);
  assert.equal(trackableSource('http://a.com/i.png'), true);
  assert.equal(trackableSource('data:image/png;base64,AA'), false);
  assert.equal(trackableSource('blob:https://a.com/x'), false);
  assert.equal(trackableSource(''), false);
  assert.equal(trackableSource(null), false);
});

test('matchEntries: prefers exact source URL', () => {
  const entries = [
    e({ source: 'https://a.com/i.png', name: 'i.png', resource: 'https://a.com/p1' }),
    e({ source: 'https://a.com/i.png', name: 'i.png', resource: 'https://a.com/p2' }),
    e({ source: 'https://b.com/j.png', name: 'j.png' }),
  ];
  const m = matchEntries(entries, 'https://a.com/i.png', 'whatever.png');
  assert.equal(m.length, 2);
  assert.ok(m.every(x => x.source === 'https://a.com/i.png'));
});

test('matchEntries: falls back to name only when no source given', () => {
  const entries = [
    e({ source: '', name: 'photo.png' }),
    e({ source: 'https://a.com/photo.png', name: 'photo.png' }),
  ];
  // No source → match sourceless entries by name (avoids false-matching real URLs).
  const byName = matchEntries(entries, '', 'photo.png');
  assert.equal(byName.length, 1);
  assert.equal(byName[0].source, '');
  // Empty source AND empty name → no matches.
  assert.equal(matchEntries(entries, '', '').length, 0);
});

test('matchEntries: tolerates non-array input', () => {
  assert.deepEqual(matchEntries(null, 'https://a.com/i.png', 'i'), []);
  assert.deepEqual(matchEntries(undefined, '', ''), []);
});
