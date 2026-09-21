// Per-op acceptance / rejection tables for the §2 core ops (js/llm/opPlan.js):
// crop, rotate, filter, layout, formula, page, blank and frame.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub } from './helpers/opPlanRig.js';

// ── Per-op acceptance / rejection tables ──

test('crop: token grammar and spec keys', () => {
  assert.deepStrictEqual(ok({ op: 'crop', spec: { x1: '10%', x2: '-10%', y1: '0', y2: '90%' } }).spec,
    { x1: '10%', x2: '-10%', y1: '0', y2: '90%' });
  ok({ op: 'crop', spec: { x1: '3cm' } });
  ok({ op: 'crop', spec: { y2: '-4.5in' } });
  ok({ op: 'crop', spec: { x1: '120px' } });
  bad({ op: 'crop', spec: {} });                        // at least one key
  bad({ op: 'crop', spec: { w: '10%' } });              // unknown spec key
  bad({ op: 'crop', spec: { x1: 10 } });                // token must be a string
  bad({ op: 'crop', spec: { x1: '10 %' } });            // malformed token
  bad({ op: 'crop', spec: { x1: '10km' } });            // unknown unit
  bad({ op: 'crop' });                                  // spec required
  bad({ op: 'crop', spec: { x1: '1' }, extra: true });  // unknown action field
});

test('crop: the aspect key — strict W:H, digits only, both positive', () => {
  assert.deepStrictEqual(ok({ op: 'crop', spec: { x1: '10%', aspect: '4:3' } }).spec,
    { x1: '10%', aspect: '4:3' });
  ok({ op: 'crop', spec: { aspect: '1:1' } });          // aspect alone counts as a key
  ok({ op: 'crop', spec: { aspect: '16:9' } });
  for (const aspect of ['0:3', '4:0', '-1:2', '4:-3', '3:4:5', 'a:b', '1.5:2', '4', '4:', ':3', '1e2:3', ''])
    bad({ op: 'crop', spec: { aspect } });              // malformed = whole plan fails
  bad({ op: 'crop', spec: { aspect: 43 } });            // must be a string
});

test('crop: action-level "aspect" beside "spec" folds into it (§3.2 tolerance)', () => {
  // Beside the spec, which lacks it → folded in, indistinguishable from in-spec.
  assert.deepStrictEqual(ok({ op: 'crop', spec: { x1: '10%' }, aspect: '4:3' }).spec,
    { x1: '10%', aspect: '4:3' });
  // Folded aspect alone satisfies the at-least-one-key rule, like the in-spec spelling.
  assert.deepStrictEqual(ok({ op: 'crop', spec: {}, aspect: '1:1' }).spec, { aspect: '1:1' });
  // The same value in both places is a harmless duplicate — the in-spec one stands.
  assert.deepStrictEqual(ok({ op: 'crop', spec: { aspect: '3:4' }, aspect: '3:4' }).spec,
    { aspect: '3:4' });
  // Conflicting duplicates = invalid params, the whole plan fails.
  bad({ op: 'crop', spec: { aspect: '3:4' }, aspect: '4:3' });
  bad({ op: 'crop', spec: { aspect: '3:4' }, aspect: 43 });
  // The action-level spelling gets the same strict W:H validation…
  bad({ op: 'crop', spec: { x1: '10%' }, aspect: '0:3' });
  bad({ op: 'crop', spec: { x1: '10%' }, aspect: '4' });
  bad({ op: 'crop', spec: { x1: '10%' }, aspect: 43 });
  // …and tolerance covers "beside spec", never "instead of spec".
  bad({ op: 'crop', aspect: '4:3' });
});

test('rotate: dir + times 1..3 (default 1)', () => {
  assert.deepStrictEqual(ok({ op: 'rotate', dir: 'right', times: 3 }), { op: 'rotate', dir: 'right', times: 3 });
  assert.strictEqual(ok({ op: 'rotate', dir: 'left' }).times, 1);
  bad({ op: 'rotate', dir: 'up' });
  bad({ op: 'rotate', dir: 'left', times: 0 });
  bad({ op: 'rotate', dir: 'left', times: 4 });
  bad({ op: 'rotate', dir: 'left', times: 1.5 });
});

test('filter: modes; tint required iff custom', () => {
  for (const mode of ['none', 'bw', 'sepia', 'invert', 'contour']) assert.deepStrictEqual(ok({ op: 'filter', mode }), { op: 'filter', mode });
  assert.deepStrictEqual(ok({ op: 'filter', mode: 'custom', tint: '#A1b2C3' }), { op: 'filter', mode: 'custom', tint: '#A1b2C3' });
  bad({ op: 'filter', mode: 'blur' });
  bad({ op: 'filter', mode: 'custom' });                 // tint required
  bad({ op: 'filter', mode: 'custom', tint: '#12345' }); // not 6 hex digits
  bad({ op: 'filter', mode: 'bw', tint: '#112233' });    // tint forbidden otherwise
});

test('layout: whitelisted per-line fields, finite points, ≤ 200 lines', () => {
  const line = { points: [{ x: 1, y: 2 }, { x: 3, y: 4 }], color: '#FFFF00', thickness: 2, pointSize: 4, style: 'dashed', locked: true, fillColor: 'transparent' };
  assert.deepStrictEqual(ok({ op: 'layout', lines: [line] }).lines[0], line);
  ok({ op: 'layout', lines: [{ points: [] }] });   // per-line defaults apply when omitted
  bad({ op: 'layout', lines: 'nope' });
  bad({ op: 'layout', lines: [{ points: [{ x: 1, y: 2, z: 3 }] }] });   // unknown point key
  bad({ op: 'layout', lines: [{ points: [{ x: Infinity, y: 0 }] }] });
  bad({ op: 'layout', lines: [{ points: [], style: 'wavy' }] });
  bad({ op: 'layout', lines: [{ points: [], evil: 1 }] });              // unknown line field
  bad({ op: 'layout', lines: Array.from({ length: 201 }, () => ({ points: [] })) });
  assert.strictEqual(parseOpPlan(plan({ actions: [{ op: 'layout', lines: Array.from({ length: 200 }, () => ({ points: [] })) }] })).actions[0].lines.length, 200);
});

test('formula: axis-matched single variable, restricted charset, ≤ 5000 chars', () => {
  assert.deepStrictEqual(ok({ op: 'formula', axis: 'x', expr: 'x*2+10' }), { op: 'formula', axis: 'x', expr: 'x*2+10' });
  ok({ op: 'formula', axis: 'y', expr: '(y - 3) ** 2 / 4' });
  bad({ op: 'formula', axis: 'z', expr: 'z' });
  bad({ op: 'formula', axis: 'x', expr: 'y*2' });        // wrong variable for the axis
  bad({ op: 'formula', axis: 'x', expr: 'x^2' });        // ^ not in the charset
  bad({ op: 'formula', axis: 'x', expr: 'x+'.repeat(2501) });   // > 5000 chars
});

test('formula: an empty expr clears that axis; `enabled` rides alone (§2)', () => {
  assert.deepStrictEqual(ok({ op: 'formula', axis: 'x', expr: '' }), { op: 'formula', axis: 'x', expr: '' });
  assert.deepStrictEqual(ok({ op: 'formula', enabled: false }), { op: 'formula', enabled: false });
  assert.deepStrictEqual(ok({ op: 'formula', enabled: true }), { op: 'formula', enabled: true });
  bad({ op: 'formula', enabled: 'off' });                       // must be a boolean
  bad({ op: 'formula', enabled: false, axis: 'x' });            // enabled rides ALONE
  bad({ op: 'formula', enabled: false, axis: 'x', expr: 'x' });
  bad({ op: 'formula' });                                       // one form or the other
  bad({ op: 'formula', axis: 'x' });                            // expr must be a string
});

test('executor: formula clear/disable route to their apply calls', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'formula', axis: 'x', expr: '' },
    { op: 'formula', axis: 'y', expr: '  ' },
    { op: 'formula', enabled: false },
  ] })), stub, {});
  assert.deepStrictEqual(calls, [
    // Clearing an axis never switches formulas ON for it…
    ['apply', { formulaX: '' }],
    ['apply', { formulaY: '' }],
    // …and enabled:false switches them OFF entirely, restoring identity.
    ['apply', { allowFormulas: false }],
  ]);
});

test('page / blank: lowercase ISO formats; blank colors', () => {
  assert.deepStrictEqual(ok({ op: 'page', format: 'a4' }), { op: 'page', format: 'a4' });
  ok({ op: 'page', format: 'b10' });
  ok({ op: 'page', format: 'c0' });
  bad({ op: 'page', format: 'A4' });                     // lowercase only
  bad({ op: 'page', format: 'a11' });
  bad({ op: 'page', format: 'd4' });
  assert.deepStrictEqual(ok({ op: 'blank', color: '#ffffff', format: 'a4' }), { op: 'blank', color: '#ffffff', format: 'a4' });
  ok({ op: 'blank', color: 'rebeccapurple' });
  bad({ op: 'blank', color: '#fff' });
  bad({ op: 'blank', color: 'not a color' });
  bad({ op: 'blank', color: '#ffffff', format: 'a99' });
});

test('frame: exactly one of index/indices; ints ≥ 0; ≤ 32 indices', () => {
  assert.deepStrictEqual(ok({ op: 'frame', index: 0 }), { op: 'frame', index: 0 });
  assert.deepStrictEqual(ok({ op: 'frame', indices: [0, 30, 60] }), { op: 'frame', indices: [0, 30, 60] });
  bad({ op: 'frame' });
  bad({ op: 'frame', index: 1, indices: [2] });
  bad({ op: 'frame', index: -1 });
  bad({ op: 'frame', index: 1.5 });
  bad({ op: 'frame', indices: [] });
  bad({ op: 'frame', indices: Array.from({ length: 33 }, (_, i) => i) });
});
