// §2 undo/redo and the page/blank centimetre dims (js/llm/plan.js): history steps are
// top-level only, and custom cm sizes drive the setters at 96 dpi.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../../../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from '../../helpers/opPlanRig.js';

// ── §2 undo/redo: the surface's own edit history, top-level only ──

test('undo/redo: steps 1..20 (default 1); anything else rejects', () => {
  assert.deepStrictEqual(ok({ op: 'undo' }), { op: 'undo', steps: 1 });
  assert.deepStrictEqual(ok({ op: 'redo' }), { op: 'redo', steps: 1 });
  assert.deepStrictEqual(ok({ op: 'undo', steps: 20 }), { op: 'undo', steps: 20 });
  bad({ op: 'undo', steps: 0 });
  bad({ op: 'undo', steps: 21 });
  bad({ op: 'redo', steps: 1.5 });
  bad({ op: 'redo', steps: '2' });
  bad({ op: 'undo', all: true });                        // unknown field
});

test('undo/redo in a variant or preview drop it (history-invisible sandboxes)', () => {
  for (const op of ['undo', 'redo']) {
    const v = dropsWithWarning(plan({ variants: [{ label: 'v', actions: [{ op }] }] }),
      /Dropped variant 1 \("v"\).*steps the live edit history.*not allowed inside variants/);
    assert.strictEqual(v.variants.length, 0);
    dropsWithWarning(plan({
      ask: { question: 'Q', options: [{ label: 'A', actions: [{ op }] }, { label: 'B' }] },
    }), /Dropped the preview for ask option 1 \("A"\).*steps the live edit history/);
  }
});

test('executor: undo/redo step the facade history once per step', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'undo', steps: 3 }, { op: 'redo' },
  ] })), stub, {});
  assert.deepStrictEqual(calls, [['undo'], ['undo'], ['undo'], ['redo']]);
});

test('executor: undo resets the §1 coordinate re-mapping (it can revert a crop)', async () => {
  const { stub, calls } = makeStub();                    // 640x480
  await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'crop', spec: { x1: '100px' } },
    { op: 'undo' },
    { op: 'layout', lines: [{ points: [{ x: 5, y: 7 }] }] },
  ] })), stub, {});
  const [, lines] = calls.find(([op]) => op === 'setLines');
  assert.deepStrictEqual(lines[0].points[0], { x: 5, y: 7 });
});

// ── §2 page/blank custom centimetre dims ──

test('page: custom width+height in cm — exactly one form, range 0.1..500', () => {
  assert.deepStrictEqual(ok({ op: 'page', width: 20, height: 30 }), { op: 'page', width: 20, height: 30 });
  ok({ op: 'page', width: 0.1, height: 500 });
  bad({ op: 'page' });                                   // one form required
  bad({ op: 'page', format: 'a4', width: 20, height: 30 });   // not both forms
  bad({ op: 'page', width: 20 });                        // dims ride together
  bad({ op: 'page', height: 30 });
  bad({ op: 'page', width: 0.05, height: 30 });          // below 0.1cm
  bad({ op: 'page', width: 20, height: 501 });           // above 500cm
  bad({ op: 'page', width: '20', height: 30 });          // numbers only
});

test('executor: custom page dims drive the cm setters then the custom page format', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'page', width: 20, height: 30 }] })), stub, {});
  assert.deepStrictEqual(calls, [
    ['pageWidth', 20], ['pageHeight', 30], ['apply', { page: 'custom' }],
  ]);
});

test('blank: optional cm dims ride together (0.1..500) and override the format', () => {
  assert.deepStrictEqual(ok({ op: 'blank', color: '#ffffff', width: 10, height: 15 }),
    { op: 'blank', color: '#ffffff', width: 10, height: 15 });
  ok({ op: 'blank', color: '#ffffff', format: 'a4', width: 10, height: 15 });   // dims may ride WITH a format (they win)
  bad({ op: 'blank', color: '#ffffff', width: 10 });     // both or neither
  bad({ op: 'blank', color: '#ffffff', height: 10 });
  bad({ op: 'blank', color: '#ffffff', width: 0, height: 10 });
  bad({ op: 'blank', color: '#ffffff', width: 10, height: 501 });
});

test('executor: blank cm dims become the 96-dpi pixel size opts (core defaultBlankSizePx)', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'blank', color: '#dbeafe', width: 2.54, height: 5.08 }] })), stub, {});
  // 2.54cm = 1in = 96px at the same dpi a page-format blank renders at.
  assert.deepStrictEqual(calls, [['blank', '#dbeafe', { size: { width: 96, height: 192 } }]]);
});
