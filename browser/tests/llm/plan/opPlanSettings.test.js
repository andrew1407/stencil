// §10 editor-settings ops (js/llm/plan.js): theme, accent, lineStyle, units, view,
// connect/disconnect shapes, and the §1 leniency that drops a misplaced variant.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../../../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from '../../helpers/opPlanRig.js';

// ── §10 editor-settings ops (browser editor profile) ──
test('theme / units / view: accept + reject tables', () => {
  assert.deepStrictEqual(ok({ op: 'theme', mode: 'dark' }), { op: 'theme', mode: 'dark' });
  assert.deepStrictEqual(ok({ op: 'theme', mode: 'light' }), { op: 'theme', mode: 'light' });
  bad({ op: 'theme', mode: 'blue' });
  bad({ op: 'theme' });
  bad({ op: 'theme', mode: 'dark', extra: 1 });          // unknown action field
  assert.deepStrictEqual(ok({ op: 'units', value: 'in' }), { op: 'units', value: 'in' });
  assert.deepStrictEqual(ok({ op: 'units', value: 'cm' }), { op: 'units', value: 'cm' });
  bad({ op: 'units', value: 'px' });
  bad({ op: 'units' });
  assert.deepStrictEqual(ok({ op: 'view', points: true, lines: false }), { op: 'view', points: true, lines: false });
  assert.deepStrictEqual(ok({ op: 'view', points: false }), { op: 'view', points: false });
  bad({ op: 'view' });                                   // at least one field
  bad({ op: 'view', points: 'yes' });
  bad({ op: 'view', hidden: true });                     // unknown field
});

test('accent: strict #rrggbb only', () => {
  assert.deepStrictEqual(ok({ op: 'accent', color: '#7c3aed' }), { op: 'accent', color: '#7c3aed' });
  bad({ op: 'accent', color: 'purple' });                // no CSS names for the accent
  bad({ op: 'accent', color: '#7c3ae' });
  bad({ op: 'accent' });
});

test('lineStyle: any subset of fields; toolbar ranges 1..20 / 1..30', () => {
  assert.deepStrictEqual(
    ok({ op: 'lineStyle', color: '#00ff00', thickness: 3, pointSize: 6, style: 'dashed' }),
    { op: 'lineStyle', color: '#00ff00', thickness: 3, pointSize: 6, style: 'dashed' });
  assert.deepStrictEqual(ok({ op: 'lineStyle', thickness: 1 }), { op: 'lineStyle', thickness: 1 });
  assert.deepStrictEqual(ok({ op: 'lineStyle', thickness: 20 }), { op: 'lineStyle', thickness: 20 });
  assert.deepStrictEqual(ok({ op: 'lineStyle', pointSize: 30 }), { op: 'lineStyle', pointSize: 30 });
  ok({ op: 'lineStyle', color: 'aqua' });                // CSS names allowed (like blank)
  bad({ op: 'lineStyle' });                              // at least one field
  bad({ op: 'lineStyle', thickness: 0 });
  bad({ op: 'lineStyle', thickness: 21 });
  bad({ op: 'lineStyle', thickness: 2.5 });
  bad({ op: 'lineStyle', pointSize: 0 });
  bad({ op: 'lineStyle', pointSize: 31 });
  bad({ op: 'lineStyle', style: 'wavy' });
  bad({ op: 'lineStyle', color: 'not a color' });
});

test('connect / disconnect: non-empty server string, nothing else (plans never carry tokens)', () => {
  assert.deepStrictEqual(ok({ op: 'connect', server: 'http://srv:8090' }), { op: 'connect', server: 'http://srv:8090' });
  assert.deepStrictEqual(ok({ op: 'disconnect', server: 'srv' }), { op: 'disconnect', server: 'srv' });
  bad({ op: 'connect' });
  bad({ op: 'connect', server: '  ' });
  bad({ op: 'connect', server: 'x', token: 't' });       // token field rejected outright
  bad({ op: 'disconnect', server: 42 });
});

// The user's report: the model put `clear` in a variant and the WHOLE turn died with
// "Could not read the assistant's plan". §1 now drops the variant and runs the rest.
test('§1 leniency: a plan of nothing but a misplaced variant still replies normally', () => {
  const p = dropsWithWarning(plan({ reply: 'Here you go.', variants: [{ label: 'reset', actions: [{ op: 'clear' }] }] }),
    /Dropped variant 1 \("reset"\)/);
  assert.strictEqual(p.chatOnly, false);          // a normal reply card, never an error card
  assert.strictEqual(p.reply, 'Here you go.');
  assert.deepStrictEqual(p.actions, []);
  assert.deepStrictEqual(p.variants, []);
});

test('§1 leniency is scope-only — bad params inside a variant still fail the plan', () => {
  // A KNOWN op with invalid params is not a misplacement: strictness is unchanged.
  assert.throws(() => parseOpPlan(plan({ variants: [{ label: 'v', actions: [{ op: 'filter', mode: 'plaid' }] }] })),
    /"mode" must be one of/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'filter', mode: 'plaid' }] })), /"mode" must be one of/);
  // …and an unknown op still drops just that action, with its own warning.
  const p = dropsWithWarning(plan({ variants: [{ label: 'v', actions: [{ op: 'teleport' }, { op: 'rotate', dir: 'left' }] }] }),
    /Skipped unknown operation "teleport"/);
  assert.deepStrictEqual(p.variants.map((v) => v.actions.length), [1]);
});

test('a misplaced variant never stops the top-level actions or its well-formed siblings', async () => {
  const p = dropsWithWarning(plan({
    actions: [{ op: 'filter', mode: 'bw' }],
    variants: [
      { label: 'sepia', actions: [{ op: 'filter', mode: 'sepia' }] },
      { label: 'reset', actions: [{ op: 'clear' }] },
      { label: 'turned', actions: [{ op: 'rotate', dir: 'left' }] },
    ],
  }), /Dropped variant 2 \("reset"\)/);
  assert.deepStrictEqual(p.variants.map((v) => v.label), ['sepia', 'turned']);
  const { stub, calls } = makeStub();
  const { results, warnings } = await executeOpPlan(p, stub, { exportImage: async () => 'data:image/png;base64,AAA' });
  assert.deepStrictEqual(results.map((r) => r.label), ['sepia', 'turned']);
  assert.ok(calls.some(([op, arg]) => op === 'apply' && arg.filter === 'bw'));
  assert.ok(warnings.some((w) => /Dropped variant 2/.test(w)));
});

test('an editor-settings op inside a variant drops THAT variant, not the plan', () => {
  for (const action of [
    { op: 'theme', mode: 'dark' },
    { op: 'accent', color: '#112233' },
    { op: 'lineStyle', thickness: 2 },
    { op: 'units', value: 'cm' },
    { op: 'view', points: true },
    { op: 'connect', server: 'http://srv:8090' },
    { op: 'disconnect', server: 'http://srv:8090' },
    { op: 'copy' },
  ]) {
    const p = dropsWithWarning(plan({ variants: [{ actions: [action] }] }),
      new RegExp(`Dropped variant 1 .*editor-settings op "${action.op}" is not allowed inside variants`));
    assert.strictEqual(p.variants.length, 0, `${action.op}'s variant survived`);
  }
  // Same ops remain valid in the TOP-LEVEL actions of a plan that also has variants.
  const p = parseOpPlan(plan({
    actions: [{ op: 'theme', mode: 'light' }],
    variants: [{ actions: [{ op: 'rotate', dir: 'left' }] }],
  }));
  assert.strictEqual(p.actions[0].op, 'theme');
  assert.strictEqual(p.variants.length, 1);
});
