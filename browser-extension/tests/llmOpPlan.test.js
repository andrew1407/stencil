// The shared §1 extraction/validation mechanics under the extension profile (src/llm/opPlan.js):
// chat-only degradation, dropped variants, focus/open/attach, and the action caps.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LIMITS, attachOnly } from '../src/llm/op/opPlan.js';
import { parse, plan } from './helpers/opPlanHarness.js';

// ── Shared §1 extraction mechanics ──
test('no JSON object at all → chat-only turn (raw text = reply, no actions)', () => {
  const p = parse('Just words, no JSON here.');
  assert.equal(p.chatOnly, true);
  assert.equal(p.reply, 'Just words, no JSON here.');
  assert.deepEqual(p.actions, []);
});

test('markdown fences are stripped; the first balanced object wins', () => {
  const p = parse('```json\n{"version":1,"reply":"hi","actions":[{"op":"focus","image":2}],"variants":[]}\n```');
  assert.equal(p.chatOnly, false);
  assert.equal(p.reply, 'hi');
  assert.deepEqual(p.actions, [{ op: 'focus', image: 2 }]);
});

test('a { … } that is not valid JSON degrades to chat-only', () => {
  const p = parse('look: {not json at all}');
  assert.equal(p.chatOnly, true);
  assert.equal(p.reply, 'look: {not json at all}');
});

test('missing/empty reply is tolerated: "Done." + a warning, plan intact (§1)', () => {
  const p1 = parse({ version: 1, actions: [{ op: 'focus', image: 1 }] });
  assert.equal(p1.reply, 'Done.');
  assert.equal(p1.actions.length, 1);
  assert.ok(p1.warnings.some((w) => /omitted its reply/.test(w)));
});

test('an EMPTY plan with no reply says nothing happened — never a bare "Done." (§1)', () => {
  const p = parse({ version: 1, reply: '   ', actions: [] });
  assert.match(p.reply, /empty plan — nothing was changed/);
  assert.ok(!p.warnings.some((w) => /the plan still ran/.test(w)), 'no "it ran" claim');
});

test('version other than 1 (or absent) is accepted but ignored', () => {
  assert.equal(parse({ version: 7, reply: 'ok', actions: [] }).reply, 'ok');
  assert.equal(parse({ reply: 'ok' }).reply, 'ok');
});

// ── Variants are unused here (§8) but drop with a warning, never fail the plan (§1) ──
test('empty/absent variants pass; a non-array "variants" is still a plan error', () => {
  assert.throws(() => parse(plan([], { variants: 'nope' })), /"variants" must be an array/);
  assert.equal(parse(plan([])).reply, 'ok');
  assert.equal(parse({ version: 1, reply: 'ok' }).chatOnly, false);
  assert.deepEqual(parse(plan([])).warnings, []);
});

test('non-empty variants are dropped with a warning; the rest of the plan still runs (§1)', () => {
  const p = parse(plan([{ op: 'focus', image: 2 }], {
    variants: [{ label: 'rotated', actions: [{ op: 'rotate', dir: 'left' }] },
               { label: 'settings', actions: [{ op: 'theme', mode: 'dark' }] }],
  }));
  assert.deepEqual(p.actions, [{ op: 'focus', image: 2 }]);
  assert.equal(p.reply, 'ok');
  assert.ok(p.warnings.some((w) => /Skipped 2 variants — the extension doesn't edit images/.test(w)));
});

test('a plan that was ONLY variants still answers normally — reply + warning, not an error', () => {
  const p = parse(plan([], { variants: [{ label: 'x', actions: [{ op: 'crop', spec: { x1: '10%' } }] }] }));
  assert.equal(p.chatOnly, false);
  assert.equal(p.reply, 'ok');
  assert.deepEqual(p.actions, []);
  assert.equal(p.warnings.length, 1);
  assert.match(p.warnings[0], /Skipped 1 variant —/);   // singular, no "s"
});

// Whatever a variant carries — a settings op, a top-level-only op, a malformed one, or
// more than the §1 cap — the drop is the same: it is never validated, never fatal.
test('variant contents are never validated: junk inside one costs the variant, not the plan', () => {
  for (const variants of [
    [{ label: 'bad', actions: [{ op: 'crop', spec: { nope: 'x' } }] }],
    [{ label: 'top-level only', actions: [{ op: 'save' }, { op: 'undo' }] }],
    [{ actions: 'not an array' }], ['not even an object'],
    Array.from({ length: LIMITS.variants + 4 }, (_, i) => ({ label: `v${i}` })),
  ]) {
    const p = parse(plan([{ op: 'focus', image: 1 }], { variants }));
    assert.deepEqual(p.actions, [{ op: 'focus', image: 1 }]);
    assert.ok(p.warnings.some((w) => /Skipped \d+ variants? —/.test(w)));
  }
});

// ── focus ──
test('focus validates its index against the listing length', () => {
  assert.deepEqual(parse(plan([{ op: 'focus', image: 0 }])).actions, [{ op: 'focus', image: 0 }]);
  assert.deepEqual(parse(plan([{ op: 'focus', image: 9 }])).actions, [{ op: 'focus', image: 9 }]);
  assert.throws(() => parse(plan([{ op: 'focus', image: 10 }])), /out of range/);
  assert.throws(() => parse(plan([{ op: 'focus', image: -1 }])), /integer >= 0/);
  assert.throws(() => parse(plan([{ op: 'focus', image: 1.5 }])), /integer/);
  assert.throws(() => parse(plan([{ op: 'focus', image: '2' }])), /integer/);
  assert.throws(() => parse(plan([{ op: 'focus', image: 0 }]), 0), /out of range/);   // empty listing
});

test('unknown fields on a known op fail the plan', () => {
  assert.throws(() => parse(plan([{ op: 'focus', image: 0, extra: true }])), /unknown field "extra"/);
  assert.throws(() => parse(plan([{ op: 'attach', image: 0, zoom: 2 }])), /unknown field "zoom"/);
});

// ── open ──
test('open validates index, optional incognito, and translates its §2 sub-actions', () => {
  const p = parse(plan([{
    op: 'open', image: 3, incognito: true,
    actions: [
      { op: 'crop', spec: { x1: '10%', x2: '-10%' } },
      { op: 'rotate', dir: 'left' },
      { op: 'filter', mode: 'custom', tint: '#12ab34' },
      { op: 'layout', lines: [{ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color: '#FFFF00' }] },
      { op: 'page', format: 'a4' },
    ],
  }]));
  assert.equal(p.actions.length, 1);
  const a = p.actions[0];
  assert.equal(a.op, 'open');
  assert.equal(a.image, 3);
  assert.equal(a.incognito, true);
  assert.deepEqual(a.actions.map((x) => x.op), ['crop', 'rotate', 'filter', 'layout', 'page']);
  assert.deepEqual(a.actions[1], { op: 'rotate', dir: 'left', times: 1 });   // times defaults to 1
});

test('open without actions/incognito normalizes to an empty action list', () => {
  const p = parse(plan([{ op: 'open', image: 1 }]));
  assert.deepEqual(p.actions, [{ op: 'open', image: 1, actions: [] }]);
});

test('open.actions rejects §2 ops outside crop/rotate/filter/layout/page', () => {
  for (const op of [{ op: 'formula', axis: 'x', expr: 'x*2' }, { op: 'blank', color: '#ffffff' }, { op: 'frame', index: 0 }]) {
    assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [op] }])), /not allowed in open\.actions/);
  }
});

test('open.actions: an unknown op drops with a warning (forward compatibility)', () => {
  const p = parse(plan([{ op: 'open', image: 0, actions: [{ op: 'sharpen' }, { op: 'page', format: 'b5' }] }]));
  assert.deepEqual(p.actions[0].actions, [{ op: 'page', format: 'b5' }]);
  assert.match(p.warnings[0], /unknown operation "sharpen"/);
});

test('open.actions: invalid params on a known core op fail the plan (§2 rules)', () => {
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'crop', spec: { x1: '10furlongs' } }] }])), /crop token/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'crop', spec: {} }] }])), /at least one/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'rotate', dir: 'up' }] }])), /"dir"/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'filter', mode: 'custom' }] }])), /"tint" is required/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'filter', mode: 'bw', tint: '#000000' }] }])), /only applies with "mode"/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'page', format: 'A4' }] }])), /lowercase/);
  assert.throws(() => parse(plan([{ op: 'open', image: 0, actions: [{ op: 'layout', lines: [{ points: [{ x: 'a', y: 0 }] }] }] }])), /must be a number/);
});

test('open: bad incognito or a bad index fail the plan', () => {
  assert.throws(() => parse(plan([{ op: 'open', image: 0, incognito: 'yes' }])), /"incognito"/);
  assert.throws(() => parse(plan([{ op: 'open', image: 99 }])), /out of range/);
});

// ── attach ──
test('attach takes exactly one of image / images and normalizes to indices', () => {
  assert.deepEqual(parse(plan([{ op: 'attach', image: 2 }])).actions, [{ op: 'attach', indices: [2] }]);
  assert.deepEqual(parse(plan([{ op: 'attach', images: [0, 4, 9] }])).actions, [{ op: 'attach', indices: [0, 4, 9] }]);
  assert.throws(() => parse(plan([{ op: 'attach' }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'attach', image: 1, images: [2] }])), /exactly one/);
  assert.throws(() => parse(plan([{ op: 'attach', images: [] }])), /non-empty/);
});

test('attach is capped at 8 indices, each bounded by the listing', () => {
  assert.equal(parse(plan([{ op: 'attach', images: [0, 1, 2, 3, 4, 5, 6, 7] }])).actions[0].indices.length, 8);
  assert.throws(() => parse(plan([{ op: 'attach', images: [0, 1, 2, 3, 4, 5, 6, 7, 8] }])), /more than 8/);
  assert.throws(() => parse(plan([{ op: 'attach', images: [0, 10] }])), /out of range/);
});

// ── top-level op filtering (§8) ──
test('§2 core ops at the top level are dropped with a warning, unknown ops with the generic one', () => {
  const p = parse(plan([
    { op: 'crop', spec: { x1: '10%' } },
    { op: 'frame', index: 0 },
    { op: 'focus', image: 1 },
    { op: 'resize', width: 100 },
  ]));
  assert.deepEqual(p.actions, [{ op: 'focus', image: 1 }]);
  assert.equal(p.warnings.length, 3);
  assert.match(p.warnings[0], /core operation "crop".*inside an "open" action/);
  assert.match(p.warnings[1], /core operation "frame"/);
  assert.match(p.warnings[2], /unknown operation "resize"/);
});

test('a dropped core op does not need valid §2 params (it is skipped before validation)', () => {
  const p = parse(plan([{ op: 'rotate', dir: 'sideways', times: 99 }]));
  assert.deepEqual(p.actions, []);
  assert.match(p.warnings[0], /core operation "rotate"/);
});

// ── limits & malformed shells ──
test('more than 16 actions fail the plan', () => {
  const many = Array.from({ length: 17 }, () => ({ op: 'focus', image: 0 }));
  assert.throws(() => parse(plan(many)), /more than 16 actions/);
});

test('malformed actions shells fail the plan', () => {
  assert.throws(() => parse(plan('x')), /"actions" must be an array/);
  assert.throws(() => parse(plan([42])), /object with an "op"/);
  assert.throws(() => parse(plan([{ image: 1 }])), /object with an "op"/);
});

// ── attachOnly (auto-continuation trigger) ──
test('attachOnly is true only when every action is an attach (and there is at least one)', () => {
  assert.equal(attachOnly(parse(plan([{ op: 'attach', image: 1 }]))), true);
  assert.equal(attachOnly(parse(plan([{ op: 'attach', images: [1, 2] }, { op: 'attach', image: 3 }]))), true);
  assert.equal(attachOnly(parse(plan([{ op: 'attach', image: 1 }, { op: 'focus', image: 2 }]))), false);
  assert.equal(attachOnly(parse(plan([]))), false);
  assert.equal(attachOnly(parse('chat only')), false);
});
