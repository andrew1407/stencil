// The variant sandbox and its neighbours (js/llm/plan.js): clear, copy, one result
// per variant, the clean restore snapshot and the frame fan-out.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from './helpers/opPlanRig.js';

// "Remove the image" must actually remove it. Without this op a model reaches for
// `blank`, which swaps in a white page and reports success it never achieved.
test('clear drops the working image through the editor\'s own reset', async () => {
  const { stub, calls } = makeStub({ lines: [{ points: [{ x: 1, y: 1 }] }] });
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'clear' }] })), stub, {});
  assert.deepStrictEqual(calls, [['newEditor']]);
});

test('clear takes no fields, and its variant is dropped like the other §10 ops', () => {
  bad({ op: 'clear', color: '#ffffff' });
  // A variant exists to produce an image, so it cannot clear one — but §1 drops THAT
  // variant, naming it, instead of throwing away the whole turn.
  const p = dropsWithWarning(plan({
    actions: [{ op: 'filter', mode: 'bw' }],
    variants: [{ label: 'v', actions: [{ op: 'clear' }] }, { label: 'ok', actions: [{ op: 'rotate', dir: 'left' }] }],
  }), /Dropped variant 1 \("v"\).*not allowed inside variants/);
  assert.deepStrictEqual(p.actions, [{ op: 'filter', mode: 'bw' }]);
  assert.deepStrictEqual(p.variants.map((v) => v.label), ['ok']);
});

// §10 copy: the toolbar's copy-image control as an op — no fields, editor-scoped.
test('copy routes through the facade\'s copy-image path (the DATA-section button)', async () => {
  const { stub, calls } = makeStub();
  const out = await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'copy' }] })), stub, {});
  assert.deepStrictEqual(calls, [['copyImage']]);
  assert.deepStrictEqual(out, { results: [], warnings: [] });
});

test('copy takes no fields at all — any extra field fails the plan', () => {
  assert.deepStrictEqual(ok({ op: 'copy' }), { op: 'copy' });
  bad({ op: 'copy', format: 'png' });
  bad({ op: 'copy', image: 1 });
});

test('copy without a working image warns and skips — never a failed plan', async () => {
  const { stub, calls } = makeStub({ imageSize: undefined });
  const out = await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'copy' }] })), stub, {});
  assert.deepStrictEqual(calls, [], 'the clipboard path is never touched');
  assert.deepStrictEqual(out.warnings, ['Skipped copy — no working image to copy']);
});

test('copy in an ask-option preview costs that option its picture, not the card', () => {
  const p = dropsWithWarning(plan({
    ask: { question: 'Copy?', options: [
      { label: 'yes', actions: [{ op: 'copy' }] },
      { label: 'no', actions: [{ op: 'rotate', dir: 'left' }] },
    ] },
  }), /Dropped the preview for ask option 1 \("yes"\).*editor-settings op "copy" is not allowed inside variants/);
  // §11.2: the option is still offered, just pictureless.
  assert.deepStrictEqual(p.ask.options.map((o) => [o.label, !!o.actions]), [['yes', false], ['no', true]]);
});

test('variants: one result per variant, each branching from the post-actions state', async () => {
  const { stub, calls } = makeStub();
  let n = 0;
  const exportImage = async () => `data:image/png;base64,SNAP${n++}`;
  const p = parseOpPlan(plan({
    actions: [{ op: 'rotate', dir: 'left' }],
    variants: [
      { label: 'tinted', actions: [{ op: 'filter', mode: 'sepia' }] },
      { label: 'cropped', actions: [{ op: 'crop', spec: { x1: '10%' } }] },
    ],
  }));
  const out = await executeOpPlan(p, stub, { exportImage });
  assert.equal(out.results.length, 2);
  assert.deepStrictEqual(out.results.map((r) => r.label), ['tinted', 'cropped']);
  // The top-level action ran once, and each variant's own op ran once.
  assert.deepStrictEqual(calls.filter((c) => c[0] === 'rotateLeft').length, 1);
  assert.deepStrictEqual(calls.filter((c) => c[0] === 'crop').length, 2);   // the variant's + the restore's
  assert.equal(calls.filter((c) => c[0] === 'load').length, 0);
});

test('variants do NOT leak their editor state into the working image', async () => {
  // The whole point of the sandbox: a variants-only plan must leave the editor exactly
  // as it found it — filter, page, formulas AND the user's lines.
  const startLines = [{ points: [{ x: 1, y: 2 }], color: '#00FF00', style: 'dashed' }];
  const { stub } = makeStub({
    filter: 'sepia', filterColor: '#112233', pageSize: 'A4',
    allowFormulas: true, formulaX: 'x*2', lines: startLines,
  });
  const exportImage = async () => 'data:image/png;base64,SNAP';
  const p = parseOpPlan(plan({
    actions: [],
    variants: [
      { label: 'bw', actions: [{ op: 'filter', mode: 'bw' }] },
      { label: 'a3', actions: [{ op: 'page', format: 'a3' }] },
      { label: 'cropped', actions: [{ op: 'crop', spec: { x1: '10%' } }] },
      { label: 'lay', actions: [{ op: 'layout', lines: [{ points: [{ x: 9, y: 9 }] }] }] },
    ],
  }));
  const out = await executeOpPlan(p, stub, { exportImage });
  assert.equal(out.results.length, 4);
  assert.equal(stub.filter, 'sepia', 'the last variant\'s filter must not stick');
  assert.equal(stub.pageSize, 'A4', 'the page format must not stick');
  assert.equal(stub.formulaX, 'x*2');
  assert.deepStrictEqual(stub.lines, startLines, 'the user\'s lines must survive');
});

test('the variant snapshot carries neither the filter nor the lines', async () => {
  // exportImage() renders filters and annotations into the pixels, so a RESTORE snapshot is taken
  // with them off; only `blank`/`frame` variants take one.
  const { stub } = makeStub({ filter: 'sepia', showPoints: true, showLines: true });
  const seen = [];
  const exportImage = async () => {
    seen.push({ filter: stub.filter, showPoints: stub.showPoints, showLines: stub.showLines });
    return 'data:image/png;base64,SNAP';
  };
  const p = parseOpPlan(plan({ variants: [{ label: 'v', actions: [{ op: 'blank', color: '#ffffff' }] }] }));
  await executeOpPlan(p, stub, { exportImage });
  assert.deepStrictEqual(seen[0], { filter: 'none', showPoints: false, showLines: false },
    'the restore snapshot must be taken clean');
  // …and the editor gets its own settings back straight after.
  assert.equal(stub.filter, 'sepia');
  assert.equal(stub.showPoints, true);
  assert.equal(stub.showLines, true);
});

test('variants without an exportImage capability fail cleanly', async () => {
  const { stub } = makeStub();
  const p = parseOpPlan(plan({ variants: [{ actions: [] }] }));
  await assert.rejects(() => executeOpPlan(p, stub, {}), /export/);
});

test('frame ops error without a video input, run through loadFrame with one', async () => {
  const { stub } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'frame', index: 5 }] }));
  await assert.rejects(() => executeOpPlan(p, stub, {}), /video/);

  const loaded = [];
  await executeOpPlan(p, stub, { loadFrame: async (i) => loaded.push(i) });
  assert.deepStrictEqual(loaded, [5]);

  // Multiple indices → one exported result per frame.
  let n = 0;
  const multi = parseOpPlan(plan({ actions: [{ op: 'frame', indices: [0, 30] }] }));
  const out = await executeOpPlan(multi, stub, { loadFrame: async (i) => loaded.push(i), exportImage: async () => `f${n++}` });
  assert.deepStrictEqual(loaded, [5, 0, 30]);
  assert.deepStrictEqual(out.results, [{ label: 'frame0', dataUrl: 'f0' }, { label: 'frame30', dataUrl: 'f1' }]);
});

test('executor passes parser warnings through', async () => {
  const { stub } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'sharpen' }] }));
  const out = await executeOpPlan(p, stub, {});
  assert.deepStrictEqual(out.warnings, ['Skipped unknown operation "sharpen"']);
});
