// §10 view-only settings (js/llm/plan.js): compare and zoom, the accent preset path,
// the extended lineStyle fields and copy's "what".
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub } from './helpers/opPlanRig.js';

// ── §10 compare / zoom: view-only editor settings ──

test('compare: modes; split 0.02..0.98 only with a split mode', () => {
  assert.deepStrictEqual(ok({ op: 'compare', mode: 'none' }), { op: 'compare', mode: 'none' });
  assert.deepStrictEqual(ok({ op: 'compare', mode: 'original' }), { op: 'compare', mode: 'original' });
  assert.deepStrictEqual(ok({ op: 'compare', mode: 'vertical', split: 0.5 }), { op: 'compare', mode: 'vertical', split: 0.5 });
  assert.deepStrictEqual(ok({ op: 'compare', mode: 'horizontal', split: 0.02 }), { op: 'compare', mode: 'horizontal', split: 0.02 });
  ok({ op: 'compare', mode: 'vertical' });               // split optional
  bad({ op: 'compare', mode: 'sideways' });
  bad({ op: 'compare' });
  bad({ op: 'compare', mode: 'none', split: 0.5 });      // split needs a split mode
  bad({ op: 'compare', mode: 'original', split: 0.5 });
  bad({ op: 'compare', mode: 'vertical', split: 0.01 }); // below the divider clamp
  bad({ op: 'compare', mode: 'vertical', split: 0.99 });
  bad({ op: 'compare', mode: 'vertical', split: '0.5' });
});

test('zoom: exactly one of percent (5..3200) / fit:true', () => {
  assert.deepStrictEqual(ok({ op: 'zoom', percent: 150 }), { op: 'zoom', percent: 150 });
  assert.deepStrictEqual(ok({ op: 'zoom', fit: true }), { op: 'zoom', fit: true });
  ok({ op: 'zoom', percent: 5 });
  ok({ op: 'zoom', percent: 3200 });
  bad({ op: 'zoom' });
  bad({ op: 'zoom', percent: 150, fit: true });
  bad({ op: 'zoom', fit: false });                       // fit must be true
  bad({ op: 'zoom', percent: 4 });
  bad({ op: 'zoom', percent: 3201 });
  bad({ op: 'zoom', percent: '150' });
});

test('executor: compare and zoom drive the view controls, never the picture', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'compare', mode: 'vertical', split: 0.3 },
    { op: 'zoom', percent: 150 },
    { op: 'zoom', fit: true },
    { op: 'compare', mode: 'none' },
  ] })), stub, {});
  assert.deepStrictEqual(calls, [
    ['compareMode', 'vertical'], ['compareSplit', 0.3],
    ['zoomLevel', 150],
    ['zoomFit'],
    ['compareMode', 'none'],
  ]);
});

// ── §10 accent preset / lineStyle extensions / copy what ──

test('accent: exactly one of color / preset; presets are normalized lowercase', () => {
  assert.deepStrictEqual(ok({ op: 'accent', preset: 'green' }), { op: 'accent', preset: 'green' });
  assert.deepStrictEqual(ok({ op: 'accent', preset: ' Violet ' }), { op: 'accent', preset: 'violet' });
  bad({ op: 'accent' });
  bad({ op: 'accent', color: '#7c3aed', preset: 'green' });
  bad({ op: 'accent', preset: '  ' });
  bad({ op: 'accent', preset: 42 });
});

test('executor: a preset persists via the facade accent path; unknown presets note + skip', async () => {
  const { stub, calls } = makeStub();
  const out = await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'accent', preset: 'green' },
    { op: 'accent', preset: 'cyan' },       // not a preset → the facade throw becomes a note
    { op: 'accent', color: '#00ffff' },     // a raw hex keeps the custom-accent path
  ] })), stub, {});
  assert.deepStrictEqual(calls, [['mainTheme', 'green'], ['mainTheme', '#00ffff']]);
  assert.ok(out.warnings.some((w) => w.startsWith('accent: Unknown theme "cyan"')));
});

test('lineStyle: pointColor ("" = follow stroke), drawMode, fillColor', () => {
  assert.deepStrictEqual(
    ok({ op: 'lineStyle', pointColor: '#112233', drawMode: 'rect', fillColor: 'transparent' }),
    { op: 'lineStyle', pointColor: '#112233', drawMode: 'rect', fillColor: 'transparent' });
  assert.deepStrictEqual(ok({ op: 'lineStyle', pointColor: '' }), { op: 'lineStyle', pointColor: '' });
  assert.deepStrictEqual(ok({ op: 'lineStyle', fillColor: '#a1b2c3' }), { op: 'lineStyle', fillColor: '#a1b2c3' });
  bad({ op: 'lineStyle', pointColor: 'red' });           // hex or "" only
  bad({ op: 'lineStyle', drawMode: 'circle' });
  bad({ op: 'lineStyle', fillColor: 'none' });           // hex or "transparent" only
});

test('executor: the extended lineStyle fields batch through the same apply call', async () => {
  const { stub, calls } = makeStub();
  await executeOpPlan(parseOpPlan(plan({ actions: [
    { op: 'lineStyle', color: '#00ff00', pointColor: '', drawMode: 'rect', fillColor: '#a1b2c3' },
  ] })), stub, {});
  assert.deepStrictEqual(calls, [
    ['apply', { lineColor: '#00ff00', pointColor: '', drawMode: 'rect', fillColor: '#a1b2c3' }],
  ]);
});

test('copy: what "image" (default) or "layout"; layout needs drawn lines', async () => {
  assert.deepStrictEqual(ok({ op: 'copy', what: 'layout' }), { op: 'copy', what: 'layout' });
  assert.deepStrictEqual(ok({ op: 'copy', what: 'image' }), { op: 'copy' });
  bad({ op: 'copy', what: 'json' });

  // With lines drawn → the facade's layout-copy path (no injected capability).
  const withLines = makeStub({ lines: [{ points: [{ x: 1, y: 1 }] }] });
  const p = parseOpPlan(plan({ actions: [{ op: 'copy', what: 'layout' }] }));
  await executeOpPlan(p, withLines.stub, {});
  assert.deepStrictEqual(withLines.calls, [['copyLayout']]);

  // No lines → skipped with a note, never a failed plan.
  const bare = makeStub();
  const out = await executeOpPlan(p, bare.stub, {});
  assert.deepStrictEqual(bare.calls, []);
  assert.deepStrictEqual(out.warnings, ['Skipped copy — no drawn lines to copy']);

  // The injected outcome-promise capability wins over the fire-and-forget facade path.
  const injected = makeStub({ lines: [{ points: [{ x: 1, y: 1 }] }] });
  let wrote = 0;
  await executeOpPlan(p, injected.stub, { copyLayoutRendered: async () => { wrote++; } });
  assert.strictEqual(wrote, 1);
  assert.deepStrictEqual(injected.calls, []);

  // A blocked clipboard becomes a warning, not a failed plan.
  const blocked = makeStub({ lines: [{ points: [{ x: 1, y: 1 }] }] });
  const denied = await executeOpPlan(p, blocked.stub, { copyLayoutRendered: async () => { throw new Error('denied'); } });
  assert.ok(denied.warnings.some((w) => w.includes('Copy to clipboard failed — denied')));
});
