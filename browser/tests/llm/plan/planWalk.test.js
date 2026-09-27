// The generic op-plan walk (js/llm/plan/parser.js walkPlan): one result document per reply, the
// structured codes a surface re-words from, and the registry-driven surface differences (forbidden
// hardFail, surfaceRules, capabilities). core/opplan is its twin; generated/normalized.json pins both.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSchema } from '../../../js/llm/plan/opSchema.js';
import { walkPlan } from '../../../js/llm/plan/parser.js';
import REGISTRY from '../../../js/config/llm/opRegistry.json' with { type: 'json' };
import { knownColor } from '../../../tools/opPlanGoldens.mjs';

const schema = (surface, opts = {}) => createSchema(REGISTRY, surface, { knownColor, ...opts });
const walk = (surface, plan, opts) => walkPlan(schema(surface, opts), typeof plan === 'string' ? plan : JSON.stringify(plan));
const plan = (...actions) => ({ reply: 'ok', actions });

test('a result always carries the same keys in the same order', () => {
  for (const text of ['hello', '{"reply":"x"}', '{"reply":"x","actions":{}}']) {
    assert.deepEqual(Object.keys(walk('browser', text)), ['status', 'reply', 'actions', 'variants', 'ask', 'warnings', 'error']);
  }
});

test('a forbidden op fails the plan only where the registry says hardFail', () => {
  const p = plan({ op: 'llm' });
  assert.equal(walk('browser', p).warnings[0].code, 'W_UNKNOWN_OP');
  const cli = walk('cli', p);
  assert.equal(cli.status, 'invalid');
  assert.deepEqual(cli.error, { code: 'E_FORBIDDEN', op: 'llm', detail: 'the "llm" op is never model-drivable',
    message: 'Invalid plan: the "llm" op is never model-drivable' });
});

test('surface rules run on the normalized action, after the key checks', () => {
  const bogus = walk('desktop', plan({ op: 'blank', color: 'notacolor' }));
  assert.equal(bogus.error.message, 'Invalid blank action: "color" must be #rrggbb or a CSS colour name');
  assert.equal(walk('browser', plan({ op: 'blank', color: 'notacolor' })).status, 'valid');
  assert.equal(walk('desktop', plan({ op: 'blank', color: 'Transparent' })).status, 'valid');
  assert.equal(walk('cli', plan({ op: 'crop', spec: { album: true } })).error.detail,
    'spec needs at least one of x1/x2/y1/y2/aspect');
  assert.equal(walk('cli', plan({ op: 'openFile', path: ' notes.txt ' })).error.detail,
    '"notes.txt" is not an image, video, .json layout or .stencil project');
  assert.equal(walk('cli', plan({ op: 'openFile', path: 'a.dir/photo.PNG' })).status, 'valid');
  assert.equal(walk('cli', plan({ op: 'openFile', path: 'a.png/photo' })).status, 'invalid');
});

test('an unwired capability turns its op into an unknown one', () => {
  const r = walk('cli', plan({ op: 'save' }, { op: 'rotate', dir: 'left' }), { capabilities: ['loadAttachment'] });
  assert.deepEqual(r.warnings.map((w) => w.code), ['W_UNKNOWN_OP']);
  assert.deepEqual(r.actions, [{ op: 'rotate', dir: 'left', times: 1 }]);
});

test('envelope failures carry the rule and where it struck', () => {
  assert.deepEqual(walk('cli', { reply: 'x', actions: [{ op: 'rotate', dir: 'left' }, 5] }).error,
    { code: 'E_PLAN', rule: 'notAction', scope: 'actions', item: 1, isObject: false,
      detail: 'every action in "actions" must be an object with an "op"',
      message: 'Invalid plan: every action in "actions" must be an object with an "op"' });
  const many = walk('cli', { reply: 'x', variants: [{ actions: Array(17).fill({ op: 'rotate', dir: 'left' }) }] }).error;
  assert.deepEqual([many.rule, many.scope, many.index, many.max], ['tooMany', 'variant', 1, 16]);
  assert.equal(walk('cli', { reply: 'x', variants: [{ label: 7 }] }).error.rule, 'variantLabel');
  assert.equal(walk('cli', { reply: 'x', ask: 'q' }).error.rule, 'ask');
});

test('a dropped variant or preview is a structured warning; the plan runs', () => {
  const r = walk('cli', {
    reply: 'x',
    variants: [{ actions: [{ op: 'clear' }] }, { label: 'ok', actions: [{ op: 'filter', mode: 'bw' }] }],
    ask: { question: 'Q', options: [{ label: 'A', actions: [{ op: 'undo' }] }, { label: 'B' }] },
  });
  assert.equal(r.status, 'valid');
  assert.deepEqual(r.variants.map((v) => v.label), ['ok']);
  assert.deepEqual(r.warnings.map(({ code, op, index, label }) => [code, op, index, label]),
    [['W_VARIANT_DROPPED', 'clear', 1, null], ['W_PREVIEW_DROPPED', 'undo', 1, 'A']]);
});

test('the §1 JSON caps fail an over-deep plan, never one that is not JSON', () => {
  const deep = `{"reply":"x","z":${'['.repeat(64)}${']'.repeat(64)}}`;
  assert.equal(walk('browser', deep).error.code, 'E_JSON_LIMIT');
  assert.equal(walk('browser', `{"reply":"x","z":${'['.repeat(63)}${']'.repeat(63)}}`).status, 'valid');
  assert.equal(walk('browser', `{"reply":"x","z":${'['.repeat(64)}1 2${']'.repeat(64)}}`).status, 'chatOnly');
});

test('prototype names are data, never a lookup', () => {
  const r = walk('browser', '{"reply":"x","actions":[{"op":"toString"},{"op":"__proto__"},{"op":"constructor"}]}');
  assert.deepEqual(r.warnings.map((w) => w.op), ['toString', '__proto__', 'constructor']);
  assert.equal(walk('browser', '{"reply":"x","actions":[{"op":"rotate","dir":"left","__proto__":{}}]}').error.detail,
    'unknown field "__proto__"');
  assert.equal(walk('browser', plan({ op: 'formula', axis: 'toString', expr: 'x' })).error.detail,
    '"axis" must be one of "x", "y"');
});
