// parseOpPlan's extraction tolerance (js/llm/plan.js): fences, prose, chat-only
// fallbacks, the plan-level count limits and sanitizeLabel.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, sanitizeLabel } from '../js/llm/plan/opPlan.js';
import { plan } from './helpers/opPlanRig.js';

// ── Extraction tolerance ──

test('parses a bare JSON object', () => {
  const p = parseOpPlan(plan({ actions: [{ op: 'rotate', dir: 'left' }] }));
  assert.strictEqual(p.reply, 'ok');
  assert.strictEqual(p.chatOnly, false);
  assert.deepStrictEqual(p.actions, [{ op: 'rotate', dir: 'left', times: 1 }]);
});

test('strips Markdown code fences before parsing', () => {
  const p = parseOpPlan('```json\n' + plan() + '\n```');
  assert.strictEqual(p.reply, 'ok');
  assert.strictEqual(p.chatOnly, false);
});

test('takes the first balanced JSON object amid prose', () => {
  const p = parseOpPlan('Sure! Here is the plan:\n' + plan() + '\nHope that helps.');
  assert.strictEqual(p.reply, 'ok');
});

test('braces inside strings do not break the balance scan', () => {
  const p = parseOpPlan(plan({ reply: 'curly {left" and } right' }));
  assert.strictEqual(p.reply, 'curly {left" and } right');
});

test('no JSON object at all → chat-only turn (raw text = reply, zero actions)', () => {
  const p = parseOpPlan('Just a plain answer, no JSON here.');
  assert.strictEqual(p.chatOnly, true);
  assert.strictEqual(p.reply, 'Just a plain answer, no JSON here.');
  assert.deepStrictEqual(p.actions, []);
  assert.deepStrictEqual(p.variants, []);
});

test('a brace blob that is not valid JSON also falls back to chat-only', () => {
  const p = parseOpPlan('{ this is not json }');
  assert.strictEqual(p.chatOnly, true);
});

test('version other than 1 (or absent) is accepted and ignored', () => {
  assert.strictEqual(parseOpPlan(plan({ version: 2 })).reply, 'ok');
  const noVersion = JSON.stringify({ reply: 'ok', actions: [] });
  assert.strictEqual(parseOpPlan(noVersion).reply, 'ok');
});

test('missing/empty reply is tolerated: "Done." + a warning, plan intact (§1)', () => {
  const p1 = parseOpPlan(JSON.stringify({ version: 1, actions: [{ op: 'rotate', dir: 'left' }] }));
  assert.equal(p1.reply, 'Done.');
  assert.equal(p1.actions.length, 1);
  assert.ok(p1.warnings.some((w) => /omitted its reply/.test(w)));
});

test('an EMPTY plan with no reply says nothing happened — never a bare "Done." (§1)', () => {
  // "Done." on a plan that carries no work reads as a success that never occurred.
  for (const text of [plan({ reply: '  ' }), JSON.stringify({ version: 1, actions: [] })]) {
    const p = parseOpPlan(text);
    assert.match(p.reply, /empty plan — nothing was changed/);
    assert.equal(p.actions.length, 0);
    assert.ok(!p.warnings.some((w) => /the plan still ran/.test(w)), 'no "it ran" claim');
  }
});

// ── Unknown op vs invalid known op ──
test('unknown op is dropped with a warning; the rest of the plan executes', () => {
  const p = parseOpPlan(plan({ actions: [{ op: 'resize', width: 100 }, { op: 'rotate', dir: 'left' }] }));
  assert.deepStrictEqual(p.actions, [{ op: 'rotate', dir: 'left', times: 1 }]);
  assert.deepStrictEqual(p.warnings, ['Skipped unknown operation "resize"']);
});

test('a known op with invalid params fails the WHOLE plan', () => {
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'rotate', dir: 'left' }, { op: 'filter', mode: 'nope' }] })), /Invalid filter/);
});

// ── Plan-level limits ──
test('action/variant count limits reject the plan', () => {
  const many = Array.from({ length: 17 }, () => ({ op: 'rotate', dir: 'left' }));
  assert.throws(() => parseOpPlan(plan({ actions: many })), /more than 16 actions/);
  assert.throws(() => parseOpPlan(plan({ variants: Array.from({ length: 9 }, () => ({ actions: [] })) })), /more than 8 variants/);
  assert.throws(() => parseOpPlan(plan({ variants: [{ actions: many }] })), /more than 16 actions/);
  // At the limits everything passes.
  const p = parseOpPlan(plan({
    actions: many.slice(0, 16),
    variants: Array.from({ length: 8 }, (_, i) => ({ label: `v${i}`, actions: many.slice(0, 16) })),
  }));
  assert.strictEqual(p.actions.length, 16);
  assert.strictEqual(p.variants.length, 8);
});

test('non-array actions/variants and non-object entries reject the plan', () => {
  assert.throws(() => parseOpPlan(plan({ actions: 'nope' })), /"actions" must be an array/);
  assert.throws(() => parseOpPlan(plan({ actions: ['nope'] })), /must be an object with an "op"/);
  assert.throws(() => parseOpPlan(plan({ variants: 'nope' })), /"variants" must be an array/);
  assert.throws(() => parseOpPlan(plan({ variants: [42] })), /variant must be an object/);
  assert.throws(() => parseOpPlan(plan({ variants: [{ label: 7 }] })), /"label" must be a string/);
});

test('variants get default labels and validated actions', () => {
  const p = parseOpPlan(plan({ variants: [{ actions: [{ op: 'rotate', dir: 'right' }] }] }));
  assert.strictEqual(p.variants[0].label, 'variant 1');
  assert.deepStrictEqual(p.variants[0].actions, [{ op: 'rotate', dir: 'right', times: 1 }]);
});

test('sanitizeLabel keeps labels short and filesystem-safe', () => {
  assert.strictEqual(sanitizeLabel('  Rotated / tinted!  '), 'Rotated tinted');
  assert.strictEqual(sanitizeLabel(''), 'variant');
  assert.strictEqual(sanitizeLabel(null), 'variant');
  assert.ok(sanitizeLabel('x'.repeat(100)).length <= 40);
});
