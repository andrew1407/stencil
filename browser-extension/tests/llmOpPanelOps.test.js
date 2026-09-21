// The §8/§10 panel-op half of the extension profile (src/llm/opPlan.js): pin/unpin, rescan,
// open.mode, scanTab, theme/accent/filter, clearChat, openUrl — and what gathers vs acts.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FORBIDDEN_OPS, LIMITS, continuationOnly } from '../src/llm/op/opPlan.js';
import { bulletOf, parse, parseT, plan } from './helpers/opPlanHarness.js';

// ── §8 pin + scanTab (tab-aware ops) ──

test('the §8 widening rides the registry bullets: open.mode and the filter toggles', () => {
  assert.match(bulletOf('open'), /"mode":"resume"/);
  assert.match(bulletOf('filter'), /"markOpened"/);
});

test('pin: single index or bounded index array, mirroring attach', () => {
  assert.deepEqual(parse(plan([{ op: 'pin', image: 3 }])).actions, [{ op: 'pin', indices: [3] }]);
  assert.deepEqual(parse(plan([{ op: 'pin', images: [0, 2] }])).actions, [{ op: 'pin', indices: [0, 2] }]);
  assert.throws(() => parse(plan([{ op: 'pin', image: 1, images: [2] }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'pin' }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'pin', images: [] }])), /non-empty/);
  assert.throws(() => parse(plan([{ op: 'pin', image: 99 }])), /out of range/);
  assert.throws(() => parse(plan([{ op: 'pin', images: [0, 1, 2, 3, 4, 5, 6, 7, 8] }])), /more than 8/);
  assert.throws(() => parse(plan([{ op: 'pin', image: 0, extra: 1 }])), /unknown field/);
  assert.equal(LIMITS.pinIndices, 8);
});

// ── §8 panel-op widening: unpin / rescan / open.mode / accent / filter toggles ──
test('unpin: the missing half of pin — same index shape, bounds, cap and strict keys', () => {
  assert.deepEqual(parse(plan([{ op: 'unpin', image: 3 }])).actions, [{ op: 'unpin', indices: [3] }]);
  assert.deepEqual(parse(plan([{ op: 'unpin', images: [0, 2] }])).actions, [{ op: 'unpin', indices: [0, 2] }]);
  assert.throws(() => parse(plan([{ op: 'unpin', image: 1, images: [2] }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'unpin' }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'unpin', images: [] }])), /non-empty/);
  assert.throws(() => parse(plan([{ op: 'unpin', image: 99 }])), /out of range/);
  assert.throws(() => parse(plan([{ op: 'unpin', image: 1.5 }])), /integer/);
  assert.throws(() => parse(plan([{ op: 'unpin', images: [0, 1, 2, 3, 4, 5, 6, 7, 8] }])), /more than 8/);
  assert.throws(() => parse(plan([{ op: 'unpin', image: 0, extra: 1 }])), /unknown field/);
});

test('rescan: a bare {"op":"rescan"} — any field at all fails the plan', () => {
  assert.deepEqual(parse(plan([{ op: 'rescan' }])).actions, [{ op: 'rescan' }]);
  assert.deepEqual(parse(plan([{ op: 'rescan' }]), 0).actions, [{ op: 'rescan' }]);   // valid on an empty listing
  assert.throws(() => parse(plan([{ op: 'rescan', tab: 0 }])), /unknown field "tab"/);
  assert.throws(() => parse(plan([{ op: 'rescan', image: 1 }])), /unknown field/);
});

test('open.mode: "resume" / "copy" carried on the validated action, junk fails', () => {
  assert.deepEqual(parse(plan([{ op: 'open', image: 1, mode: 'resume' }])).actions,
    [{ op: 'open', image: 1, actions: [], mode: 'resume' }]);
  assert.deepEqual(parse(plan([{ op: 'open', image: 1, mode: 'copy' }])).actions,
    [{ op: 'open', image: 1, actions: [], mode: 'copy' }]);
  // Absent mode stays absent (today's fresh-open default).
  assert.deepEqual(parse(plan([{ op: 'open', image: 1 }])).actions, [{ op: 'open', image: 1, actions: [] }]);
  assert.throws(() => parse(plan([{ op: 'open', image: 1, mode: 'again' }])), /"mode" must be one of/);
  assert.throws(() => parse(plan([{ op: 'open', image: 1, mode: true }])), /"mode"/);
});

test('accent: §10\'s shape — exactly one of a #rrggbb color or a preset name', () => {
  assert.deepEqual(parse(plan([{ op: 'accent', color: '#7c3aed' }])).actions, [{ op: 'accent', color: '#7c3aed' }]);
  assert.deepEqual(parse(plan([{ op: 'accent', preset: 'green' }])).actions, [{ op: 'accent', preset: 'green' }]);
  assert.throws(() => parse(plan([{ op: 'accent' }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'accent', color: '#123456', preset: 'green' }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'accent', color: 'purple' }])), /#rrggbb/);
  assert.throws(() => parse(plan([{ op: 'accent', color: '#12ab3' }])), /#rrggbb/);
  assert.throws(() => parse(plan([{ op: 'accent', color: '#12ab3g' }])), /#rrggbb/);
  assert.throws(() => parse(plan([{ op: 'accent', preset: '   ' }])), /non-empty string/);
  assert.throws(() => parse(plan([{ op: 'accent', preset: 'x'.repeat(41) }])), /longer than 40/);
  assert.throws(() => parse(plan([{ op: 'accent', color: '#123456', mode: 'dark' }])), /unknown field/);
});

test('filter: the three list toggles are strict booleans', () => {
  assert.deepEqual(parse(plan([{ op: 'filter', markOpened: true, openedFirst: false, showPinned: true }])).actions,
    [{ op: 'filter', markOpened: true, openedFirst: false, showPinned: true }]);
  // One toggle alone satisfies the at-least-one-field rule.
  assert.deepEqual(parse(plan([{ op: 'filter', showPinned: false }])).actions,
    [{ op: 'filter', showPinned: false }]);
  assert.throws(() => parse(plan([{ op: 'filter', markOpened: 'yes' }])), /"markOpened" must be a boolean/);
  assert.throws(() => parse(plan([{ op: 'filter', openedFirst: 1 }])), /"openedFirst" must be a boolean/);
  assert.throws(() => parse(plan([{ op: 'filter', showPinned: null }])), /needs at least one/);
});

test('scanTab: integer index bounded by the tabs listing', () => {
  assert.deepEqual(parseT(plan([{ op: 'scanTab', tab: 2 }])).actions, [{ op: 'scanTab', tab: 2 }]);
  assert.throws(() => parseT(plan([{ op: 'scanTab', tab: 5 }])), /out of range/);
  assert.throws(() => parseT(plan([{ op: 'scanTab', tab: -1 }])), /integer >= 0/);
  assert.throws(() => parseT(plan([{ op: 'scanTab', tab: 'x' }])), /must be an integer/);
  assert.throws(() => parseT(plan([{ op: 'scanTab', tab: 0, url: 'https://x' }])), /unknown field/);
  // With no tabs listed there is nothing to scan — the plan fails (model mis-step).
  assert.throws(() => parseT(plan([{ op: 'scanTab', tab: 0 }]), { tabsLength: 0 }), /no other open tabs/);
});

test('continuationOnly: attach, scanTab and rescan gather context; anything else acts', () => {
  assert.equal(continuationOnly(parseT(plan([{ op: 'attach', image: 1 }]))), true);
  assert.equal(continuationOnly(parseT(plan([{ op: 'scanTab', tab: 0 }]))), true);
  assert.equal(continuationOnly(parseT(plan([{ op: 'rescan' }]))), true);
  assert.equal(continuationOnly(parseT(plan([{ op: 'scanTab', tab: 0 }, { op: 'attach', image: 1 }]))), true);
  assert.equal(continuationOnly(parseT(plan([{ op: 'rescan' }, { op: 'attach', image: 1 }]))), true);
  assert.equal(continuationOnly(parseT(plan([{ op: 'scanTab', tab: 0 }, { op: 'pin', image: 1 }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'rescan' }, { op: 'unpin', image: 1 }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'focus', image: 1 }]))), false);
  assert.equal(continuationOnly(parseT(plan([]))), false);
});

// ── §8 panel settings: the assistant working the surface's own controls ──
test('theme: the three modes Options offers, and nothing else', () => {
  for (const mode of ['light', 'dark', 'system']) {
    assert.deepEqual(parse(plan([{ op: 'theme', mode }])).actions, [{ op: 'theme', mode }]);
  }
  assert.throws(() => parse(plan([{ op: 'theme', mode: 'blue' }])), /must be one of/);
  assert.throws(() => parse(plan([{ op: 'theme' }])), /is required/);
  assert.throws(() => parse(plan([{ op: 'theme', mode: 'dark', extra: 1 }])), /unknown field/);
});

test('filter: every field optional, at least one required, each one bounded', () => {
  assert.deepEqual(parse(plan([{ op: 'filter', search: 'cat' }])).actions,
    [{ op: 'filter', search: 'cat' }]);
  // Formats are upper-cased for the panel's own pill labels; kinds are de-duped.
  assert.deepEqual(parse(plan([{ op: 'filter', formats: ['png', 'JPG', 'png'], kinds: ['images', 'images', 'video'] }])).actions,
    [{ op: 'filter', kinds: ['images', 'video'], formats: ['PNG', 'JPG', 'PNG'] }]);
  // 0 is a legal bound — it CLEARS that limit (the executor empties the input).
  assert.deepEqual(parse(plan([{ op: 'filter', minWidth: 200, maxWidth: 0 }])).actions,
    [{ op: 'filter', minWidth: 200, maxWidth: 0 }]);
  assert.deepEqual(parse(plan([{ op: 'filter', regex: true, search: '^ic' }])).actions,
    [{ op: 'filter', search: '^ic', regex: true }]);
  assert.throws(() => parse(plan([{ op: 'filter' }])), /needs at least one/);
  assert.throws(() => parse(plan([{ op: 'filter', kinds: ['pdfs'] }])), /"kinds\[0\]" must be one of/);
  assert.throws(() => parse(plan([{ op: 'filter', kinds: 'images' }])), /must be an array/);
  assert.throws(() => parse(plan([{ op: 'filter', regex: 'yes' }])), /must be a boolean/);
  assert.throws(() => parse(plan([{ op: 'filter', minWidth: -1 }])), /integer 0\.\.100000/);
  assert.throws(() => parse(plan([{ op: 'filter', minWidth: 1.5 }])), /must be an integer/);
  assert.throws(() => parse(plan([{ op: 'filter', maxHeight: LIMITS.filterSize + 1 }])), /integer 0\.\.100000/);
  assert.throws(() => parse(plan([{ op: 'filter', search: 'x'.repeat(LIMITS.filterSearch + 1) }])), /longer than/);
  assert.throws(() => parse(plan([{ op: 'filter', formats: new Array(LIMITS.filterFormats + 1).fill('PNG') }])), /more than/);
  assert.throws(() => parse(plan([{ op: 'filter', search: 'a', sort: 'name' }])), /unknown field/);
});

// ── §10 clearChat, carried into this profile as a panel op ──
test('clearChat: a bare {"op":"clearChat"} — any field at all fails the plan', () => {
  assert.deepEqual(parse(plan([{ op: 'clearChat' }])).actions, [{ op: 'clearChat' }]);
  assert.deepEqual(parse(plan([{ op: 'clearChat' }]), 0).actions, [{ op: 'clearChat' }]);   // no listing needed
  assert.throws(() => parse(plan([{ op: 'clearChat', now: true }])), /unknown field "now"/);
  assert.throws(() => parse(plan([{ op: 'clearChat', image: 0 }])), /unknown field/);
});

test('clearChat never gathers, and stays out of open.actions (unknown there)', () => {
  assert.equal(continuationOnly(parseT(plan([{ op: 'clearChat' }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'attach', image: 1 }, { op: 'clearChat' }]))), false);
  const p = parse(plan([{ op: 'open', image: 0, actions: [{ op: 'clearChat' }] }]));
  assert.deepEqual(p.actions[0].actions, []);
  assert.match(p.warnings[0], /unknown operation "clearChat" inside "open"/);
});

test('clearChat is drivable; the persistence/consent TOGGLES stay forbidden', () => {
  assert.ok(!FORBIDDEN_OPS.has('clearChat'));
  for (const name of ['chatPersist', 'chatConsent', 'persistChat', 'shareTabs']) {
    assert.ok(FORBIDDEN_OPS.has(name), name);
  }
});

test('panel-settings ops ACT — they never gather context for a continuation', () => {
  assert.equal(continuationOnly(parseT(plan([{ op: 'theme', mode: 'dark' }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'filter', search: 'cat' }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'accent', color: '#16a34a' }]))), false);
  assert.equal(continuationOnly(parseT(plan([{ op: 'attach', image: 1 }, { op: 'theme', mode: 'dark' }]))), false);
});

// ── §8 openUrl (user-echoed URLs only; the executor guards the echo) ──
test('openUrl: http(s) url + optional incognito; junk fails', () => {
  assert.deepEqual(parse(plan([{ op: 'openUrl', url: 'https://a.example/cat.jpg' }])).actions,
    [{ op: 'openUrl', url: 'https://a.example/cat.jpg' }]);
  assert.deepEqual(parse(plan([{ op: 'openUrl', url: 'http://a.example/x.png', incognito: true }])).actions,
    [{ op: 'openUrl', url: 'http://a.example/x.png', incognito: true }]);
  assert.throws(() => parse(plan([{ op: 'openUrl' }])), /is required/);
  assert.throws(() => parse(plan([{ op: 'openUrl', url: 'ftp://a.example/x' }])), /http\(s\) URL/);
  assert.throws(() => parse(plan([{ op: 'openUrl', url: 'https://a.example/x', incognito: 'yes' }])), /boolean/);
  assert.throws(() => parse(plan([{ op: 'openUrl', url: 'https://a.example/x', tab: 1 }])), /unknown field/);
});
