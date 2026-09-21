// §2.1 multi-image ops (js/llm/opPlan.js): `image` switches to a turn attachment,
// `save` persists, and both are top-level only.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { plan, makeStub, dropsWithWarning, askPlan } from './helpers/opPlanRig.js';

// ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──

test('image/save: validated shapes, and both are top-level only', () => {
  const p = parseOpPlan(plan({ actions: [
    { op: 'image', index: 2 }, { op: 'save', name: 'portrait 1' }, { op: 'save' },
  ] }));
  assert.deepStrictEqual(p.actions, [
    { op: 'image', index: 2 }, { op: 'save', name: 'portrait 1' }, { op: 'save' },
  ]);
  // index is 1-based: 0, negatives and non-integers are not an attachment
  for (const index of [0, -1, 1.5, '1']) {
    assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'image', index }] })), /index/);
  }
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'save', name: 'x'.repeat(121) }] })), /120/);
  // §2.1 "path" is valid everywhere (string ≤ 1024, never a URL); the browser
  // executor notes+skips it. Empty/whitespace is the same as no path at all.
  const withPath = parseOpPlan(plan({ actions: [{ op: 'save', path: ' ~/Downloads ' }] }));
  assert.deepStrictEqual(withPath.actions, [{ op: 'save', path: '~/Downloads' }]);
  assert.deepStrictEqual(
    parseOpPlan(plan({ actions: [{ op: 'save', path: '  ' }] })).actions, [{ op: 'save' }]);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'save', path: 'x'.repeat(1025) }] })), /1024/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'save', path: 'https://x.example/out.png' }] })), /not a URL/);
  // …and neither may hide inside a variant or an ask preview (they escape the branch):
  // §1 drops that variant / that option's picture, naming it.
  for (const action of [{ op: 'image', index: 1 }, { op: 'save' }]) {
    const v = dropsWithWarning(plan({ variants: [{ label: 'v', actions: [action] }] }),
      /Dropped variant 1 \("v"\).*top-level/);
    assert.strictEqual(v.variants.length, 0);
  }
  const a = askPlan({ question: 'Q', options: [{ label: 'A', actions: [{ op: 'save' }] }, { label: 'B' }] });
  assert.ok(a.warnings.some((w) => /Dropped the preview for ask option 1 \("A"\).*top-level/.test(w)), JSON.stringify(a.warnings));
  assert.strictEqual(a.ask.options[0].actions, undefined);
});

test('executor: image loads that attachment and save persists — once per image', async () => {
  const { stub } = makeStub();
  const loaded = [];
  const saved = [];
  const p = parseOpPlan(plan({ actions: [
    { op: 'image', index: 1 }, { op: 'filter', mode: 'bw' }, { op: 'save', name: 'one' },
    { op: 'image', index: 2 }, { op: 'save', name: 'two' },
  ] }));
  const { warnings } = await executeOpPlan(p, stub, {
    loadAttachment: async (i) => { loaded.push(i); },
    saveProject: async (name) => { saved.push(name); },
  });
  assert.deepStrictEqual(loaded, [1, 2]);
  assert.deepStrictEqual(saved, ['one', 'two']);
  assert.deepStrictEqual(warnings, []);
});

test('executor: an image index the turn cannot satisfy costs that action, not the plan', async () => {
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({ actions: [
    { op: 'image', index: 3 }, { op: 'filter', mode: 'sepia' },
  ] }));
  const { warnings } = await executeOpPlan(p, stub, {
    loadAttachment: async () => { throw new Error('this message attached 2 image(s)'); },
  });
  assert.ok(warnings.some((w) => w.includes('attached image 3') && w.includes('2 image(s)')));
  assert.ok(calls.some(([op, arg]) => op === 'apply' && arg.filter === 'sepia'));   // the rest ran
});

test('executor: save with nothing loaded is skipped with a warning', async () => {
  const { stub } = makeStub({ imageSize: undefined });
  const saved = [];
  const p = parseOpPlan(plan({ actions: [{ op: 'save', name: 'x' }] }));
  const { warnings } = await executeOpPlan(p, stub, { saveProject: async (n) => { saved.push(n); } });
  assert.deepStrictEqual(saved, []);
  assert.ok(warnings.some((w) => w.includes('no working image')));
});

test('executor: a save "path" costs the destination, not the save', async () => {
  const { stub } = makeStub();
  const saved = [];
  const p = parseOpPlan(plan({ actions: [{ op: 'save', name: 'x', path: '~/Downloads' }] }));
  const { warnings } = await executeOpPlan(p, stub, { saveProject: async (n) => { saved.push(n); } });
  assert.deepStrictEqual(saved, ['x']);
  assert.ok(warnings.some((w) => w.includes('Saved to the usual place')), JSON.stringify(warnings));
});

test('executor: switching image resets the §1 coordinate re-mapping', async () => {
  const { stub, calls } = makeStub();
  // A crop moves the origin; the attachment that follows is a FRESH frame, so the
  // layout after it must land on the points as written — not shifted by that crop.
  const p = parseOpPlan(plan({ actions: [
    { op: 'crop', spec: { x1: '10%' } },
    { op: 'image', index: 1 },
    { op: 'layout', lines: [{ points: [{ x: 5, y: 7 }] }] },
  ] }));
  await executeOpPlan(p, stub, { loadAttachment: async () => {} });
  const [, lines] = calls.find(([op]) => op === 'setLines');
  assert.deepStrictEqual(lines[0].points[0], { x: 5, y: 7 });
});
