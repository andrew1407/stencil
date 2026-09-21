// executeOpPlan over the facade stub (js/llm/opPlan.js): the op-to-facade mapping and
// the §1 coordinate re-mapping that puts plan points where the model saw them.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { rotateLinePointsQuarter } from '../js/core/parse/cropGeometry.js';
import { plan, makeStub, drawnBy } from './helpers/opPlanRig.js';

test('executor maps every op onto the facade', async () => {
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({
    actions: [
      // layout first: after a crop/rotate its points are deliberately re-mapped
      // (§1) — that behavior has its own tests below.
      { op: 'layout', lines: [{ points: [{ x: 1, y: 2 }] }] },
      { op: 'crop', spec: { x1: '10%' } },
      { op: 'rotate', dir: 'left', times: 2 },
      { op: 'rotate', dir: 'right' },
      { op: 'filter', mode: 'custom', tint: '#112233' },
      { op: 'formula', axis: 'y', expr: 'y+1' },
      { op: 'page', format: 'a3' },
      { op: 'blank', color: '#ffffff', format: 'a4' },
    ],
  }));
  const out = await executeOpPlan(p, stub, {});
  assert.deepStrictEqual(calls, [
    ['setLines', [{ points: [{ x: 1, y: 2 }] }], undefined],
    ['crop', { x1: '10%' }],
    ['rotateLeft'], ['rotateLeft'],
    ['rotateRight'],
    ['apply', { filter: 'custom', filterColor: '#112233' }],
    ['apply', { allowFormulas: true, formulaY: 'y+1' }],
    ['apply', { page: 'a3' }],
    ['apply', { page: 'a4' }], ['blank', '#ffffff'],
  ]);
  assert.deepStrictEqual(out, { results: [], warnings: [] });
});

test('a plan crop with aspect hands the whole spec to the facade intact', async () => {
  // The facade's crop path resolves aspect itself (core cropSpec) — the executor
  // must forward the key untouched, never resolve or strip it.
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'crop', spec: { x1: '10%', aspect: '4:3' } }] }));
  await executeOpPlan(p, stub, {});
  assert.deepStrictEqual(calls, [['crop', { x1: '10%', aspect: '4:3' }]]);
});

test('non-custom filter and x-formula map to their plain apply calls', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'filter', mode: 'sepia' }, { op: 'formula', axis: 'x', expr: 'x*2' }] })), stub, {});
  assert.deepStrictEqual(calls, [
    ['apply', { filter: 'sepia' }],
    ['apply', { allowFormulas: true, formulaX: 'x*2' }],
  ]);
});

// The common case: the model redraws an outline it just placed. Through the paste setter
// that pops Combine / Replace / Cancel, which no assistant turn can answer.
test('layout replaces existing lines silently — never through the prompting paste path', async () => {
  const { stub, calls } = makeStub({ lines: [{ points: [{ x: 9, y: 9 }] }] });
  const fresh = [{ points: [{ x: 1, y: 2 }] }, { points: [{ x: 3, y: 4 }] }];
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'layout', lines: fresh }] })), stub, {});
  assert.deepStrictEqual(calls, [['setLines', fresh, undefined]]);
  assert.ok(!calls.some(([name]) => name === 'layout'), 'the paste setter is never touched');
  assert.deepStrictEqual(stub.lines, fresh, 'the old lines are gone, no confirmation needed');
});

// "Drop the outlines" is the same op with an empty list — and always has lines present.
test('layout with an empty list clears the lines', async () => {
  const { stub, calls } = makeStub({ lines: [{ points: [{ x: 9, y: 9 }] }] });
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'layout', lines: [] }] })), stub, {});
  assert.deepStrictEqual(calls, [['setLines', [], undefined]]);
  assert.deepStrictEqual(stub.lines, []);
});

// installLayout refuses silently without an image; the op must not report lines it never took.
test('layout without a working image fails the turn instead of silently doing nothing', async () => {
  const { stub } = makeStub({ imageSize: undefined });
  await assert.rejects(
    () => executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'layout', lines: [{ points: [] }] }] })), stub, {}),
    /working image/);
});

// ── §1 coordinate re-mapping: plan coords are in the frame the model SAW ──

test('crop then layout: later points shift by the resolved crop origin', async () => {
  const { stub, calls } = makeStub();                    // 640x480
  await executeOpPlan(parseOpPlan(plan({
    actions: [
      { op: 'crop', spec: { x1: '100px' } },
      { op: 'layout', lines: [{ points: [{ x: 150, y: 50 }] }] },
    ],
  })), stub, {});
  assert.deepStrictEqual(drawnBy(calls), [[{ points: [{ x: 50, y: 50 }] }]]);
});

test('rotate then layout maps points exactly like the editor rotates existing ones', async () => {
  // The oracle is the real rotate semantics (cropGeometry rotateLinePointsQuarter): a plan point
  // written pre-rotate must land on the pixel an already-drawn point would.
  const P = { x: 150, y: 50 };
  for (const [dir, times] of [['right', 1], ['left', 1], ['right', 2], ['left', 3]]) {
    const oracle = [{ points: [{ ...P }] }];
    let box = { w: 640, h: 480 };
    for (let i = 0; i < times; i++) {
      rotateLinePointsQuarter(oracle, box.w, box.h, dir === 'right');
      box = { w: box.h, h: box.w };
    }
    const { stub, calls } = makeStub();                  // 640x480
    await executeOpPlan(parseOpPlan(plan({
      actions: [
        { op: 'rotate', dir, times },
        { op: 'layout', lines: [{ points: [{ ...P }] }] },
      ],
    })), stub, {});
    assert.deepStrictEqual(drawnBy(calls), [[{ points: oracle[0].points }]], `${dir} x${times}`);
  }
});

test('crop and rotate compose in execution order', async () => {
  const { stub, calls } = makeStub();                    // 640x480
  await executeOpPlan(parseOpPlan(plan({
    actions: [
      { op: 'crop', spec: { x1: '100px' } },             // → 540x480, origin +100 in x
      { op: 'rotate', dir: 'right' },                    // 540x480 → 480x540
      { op: 'layout', lines: [{ points: [{ x: 150, y: 50 }] }] },
    ],
  })), stub, {});
  // (150,50) −crop→ (50,50) −right (H=480)→ (480−50, 50) = (430,50).
  assert.deepStrictEqual(drawnBy(calls), [[{ points: [{ x: 430, y: 50 }] }]]);
});

test('layout points clamp into the working image even at identity — and only then', async () => {
  const { stub, calls } = makeStub();                    // 640x480, no crop/rotate
  const lines = [{
    points: [{ x: -5, y: 700 }, { x: 10000, y: -3 }, { x: 12.5, y: 0 }, { x: 640, y: 480 }],
    color: '#00FF00', thickness: 3, style: 'dashed', fillColor: 'transparent',
  }];
  const p = parseOpPlan(plan({ actions: [{ op: 'layout', lines }] }));
  await executeOpPlan(p, stub, {});
  assert.deepStrictEqual(drawnBy(calls), [[{
    points: [{ x: 0, y: 480 }, { x: 640, y: 0 }, { x: 12.5, y: 0 }, { x: 640, y: 480 }],
    color: '#00FF00', thickness: 3, style: 'dashed', fillColor: 'transparent',
  }]], 'out-of-frame points pin to the edge; in-bounds ones (edges included) pass through untouched');
  // The validated plan itself is never mutated by execution.
  assert.deepStrictEqual(p.actions[0].lines[0].points[0], { x: -5, y: 700 });
});

test('ops that replace the working image reset the re-mapping to identity', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({
    actions: [
      { op: 'crop', spec: { x1: '100px' } },
      { op: 'blank', color: '#ffffff' },                 // new frame — the crop shift is void
      { op: 'layout', lines: [{ points: [{ x: 150, y: 50 }] }] },
    ],
  })), stub, {});
  assert.deepStrictEqual(drawnBy(calls), [[{ points: [{ x: 150, y: 50 }] }]]);
});

test('variant actions re-map starting from the post-actions state', async () => {
  const { stub, calls } = makeStub();                    // 640x480
  const exportImage = async () => 'data:image/png;base64,SNAP';
  await executeOpPlan(parseOpPlan(plan({
    actions: [{ op: 'crop', spec: { x1: '100px' } }],    // → 540x480
    variants: [
      { label: 'plain', actions: [{ op: 'layout', lines: [{ points: [{ x: 150, y: 50 }] }] }] },
      { label: 'turned', actions: [
        { op: 'rotate', dir: 'right' },
        { op: 'layout', lines: [{ points: [{ x: 150, y: 50 }] }] },
      ] },
    ],
  })), stub, { exportImage });
  // Both variants inherit the top-level crop's shift; the second composes its own rotate.
  assert.deepStrictEqual(drawnBy(calls), [
    [{ points: [{ x: 50, y: 50 }] }],
    [{ points: [{ x: 430, y: 50 }] }],
  ]);
});
