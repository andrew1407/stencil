import { test } from 'node:test';
import assert from 'node:assert/strict';
import { matchEntries, trackableSource, reconcileLedger, originOf } from '../src/lib/ledger.js';

const e = (over = {}) => ({ source: '', resource: '', name: '', count: 1, t: 0, ...over });

// Editor that handed off `src`; old enough (t far in the past) to be past the grace window.
const led = (src, editorUrl = 'http://localhost:8080/', t = 0) =>
  e({ source: src, name: 'i.png', editorUrl, t });
const NOW = 10 * 60 * 1000; // 10 min — well past RECONCILE_GRACE_MS for t:0 entries

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

test('originOf: extracts origin, empty on garbage', () => {
  assert.equal(originOf('http://localhost:8080/#x'), 'http://localhost:8080');
  assert.equal(originOf('https://a.com/i.png?q=1'), 'https://a.com');
  assert.equal(originOf('not a url'), '');
  assert.equal(originOf(null), '');
});

test('reconcileLedger: drops entries whose project was deleted (same editor)', () => {
  const entries = [led('https://a.com/keep.png'), led('https://a.com/gone.png')];
  const projects = [{ source: 'https://a.com/keep.png' }];
  const out = reconcileLedger(entries, projects, 'http://localhost:8080', NOW);
  assert.equal(out.length, 1);
  assert.equal(out[0].source, 'https://a.com/keep.png');
});

test('reconcileLedger: leaves entries for a DIFFERENT editor untouched', () => {
  const entries = [led('https://a.com/x.png', 'https://editor.example.com/')];
  // Reporting editor is localhost with no projects; the prod-editor entry stays.
  const out = reconcileLedger(entries, [], 'http://localhost:8080', NOW);
  assert.equal(out.length, 1);
});

test('reconcileLedger: keeps fresh entries (editor may not have saved yet)', () => {
  const fresh = led('https://a.com/just-opened.png', 'http://localhost:8080/', NOW); // t == now
  const out = reconcileLedger([fresh], [], 'http://localhost:8080', NOW);
  assert.equal(out.length, 1); // within grace window → not pruned despite no project
});

test('reconcileLedger: re-derives count from the number of live projects for the source', () => {
  const entries = [led('https://a.com/x.png')]; // count 1
  // Two live projects (an original + an "add a copy") share this source.
  const projects = [{ source: 'https://a.com/x.png' }, { source: 'https://a.com/x.png' }];
  const out = reconcileLedger(entries, projects, 'http://localhost:8080', NOW);
  assert.equal(out.length, 1);
  assert.equal(out[0].count, 2); // tracks reality, not a monotonic handoff tally
});

test('reconcileLedger: count moves back down when copies are removed', () => {
  const entries = [e({ source: 'https://a.com/x.png', name: 'i.png', editorUrl: 'http://localhost:8080/', t: 0, count: 5 })];
  const out = reconcileLedger(entries, [{ source: 'https://a.com/x.png' }], 'http://localhost:8080', NOW);
  assert.equal(out[0].count, 1); // 5 stale opens → 1 live project
});

test('reconcileLedger: empty registry prunes all stale same-editor entries', () => {
  const entries = [led('https://a.com/a.png'), led('https://a.com/b.png')];
  const out = reconcileLedger(entries, [], 'http://localhost:8080', NOW);
  assert.equal(out.length, 0);
});

test('reconcileLedger: tolerates bad input and never reorders survivors', () => {
  assert.deepEqual(reconcileLedger(null, [], 'http://localhost:8080', NOW), []);
  const entries = [led('https://a.com/1.png'), led('https://a.com/2.png')];
  const projects = [{ source: 'https://a.com/2.png' }, { source: 'https://a.com/1.png' }];
  const out = reconcileLedger(entries, projects, 'http://localhost:8080', NOW);
  assert.deepEqual(out.map(x => x.source), ['https://a.com/1.png', 'https://a.com/2.png']);
});
