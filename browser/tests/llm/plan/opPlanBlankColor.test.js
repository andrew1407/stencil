// §10 blankColor / openProject / incognito (js/llm/plan.js): each capability's note
// path, and the variant-drop table every settings op obeys.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../../../js/llm/plan/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from '../../helpers/opPlanRig.js';

// ── §10 blankColor / openProject / incognito ──

test('blankColor: hex or CSS name; non-blank projects note + skip via the capability', async () => {
  assert.deepStrictEqual(ok({ op: 'blankColor', color: '#dbeafe' }), { op: 'blankColor', color: '#dbeafe' });
  ok({ op: 'blankColor', color: 'lavender' });
  bad({ op: 'blankColor' });
  bad({ op: 'blankColor', color: '#dbe' });
  bad({ op: 'blankColor', color: 'not a color' });

  const { stub } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'blankColor', color: '#dbeafe' }] }));
  const painted = [];
  const done = await executeOpPlan(p, stub, { setBlankColor: async (c) => { painted.push(c); return null; } });
  assert.deepStrictEqual(painted, ['#dbeafe']);
  assert.deepStrictEqual(done.warnings, []);

  // A non-blank project comes back as the capability's note, never a failed plan.
  const out = await executeOpPlan(p, stub, {
    setBlankColor: async () => 'only a blank project has a recolourable background',
  });
  assert.ok(out.warnings.some((w) => w.includes('blankColor: only a blank project')));
  await assert.rejects(() => executeOpPlan(p, stub, {}), /cannot recolour/);
});

test('openProject: name 1..120; notes surface; a fresh project resets the §1 re-mapping', async () => {
  assert.deepStrictEqual(ok({ op: 'openProject', name: ' Cat ' }), { op: 'openProject', name: 'Cat' });
  bad({ op: 'openProject' });
  bad({ op: 'openProject', name: '' });
  bad({ op: 'openProject', name: 'x'.repeat(121) });

  const opened = [];
  const { stub, calls } = makeStub();
  const p = parseOpPlan(plan({ actions: [
    { op: 'crop', spec: { x1: '100px' } },
    { op: 'openProject', name: 'Cat' },
    { op: 'layout', lines: [{ points: [{ x: 5, y: 7 }] }] },
  ] }));
  await executeOpPlan(p, stub, { openProjectNamed: async (n) => { opened.push(n); return null; } });
  assert.deepStrictEqual(opened, ['Cat']);
  // The opened project is a fresh frame — the crop's shift must not leak into it.
  const [, lines] = calls.find(([op]) => op === 'setLines');
  assert.deepStrictEqual(lines[0].points[0], { x: 5, y: 7 });

  const declined = await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'openProject', name: 'Cat' }] })),
    makeStub().stub, { openProjectNamed: async () => 'open canceled' });
  assert.ok(declined.warnings.some((w) => w.includes('openProject: open canceled')));
});

test('incognito: boolean on; the facade\'s non-blank throw becomes a note + skip', async () => {
  assert.deepStrictEqual(ok({ op: 'incognito', on: true }), { op: 'incognito', on: true });
  assert.deepStrictEqual(ok({ op: 'incognito', on: false }), { op: 'incognito', on: false });
  bad({ op: 'incognito' });
  bad({ op: 'incognito', on: 'yes' });

  // A blank editor toggles; the loaded one throws → note, the plan survives.
  const blank = makeStub({ imageSize: undefined });
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'incognito', on: true }] })), blank.stub, {});
  assert.deepStrictEqual(blank.calls, [['incognitoSet', true]]);

  const loaded = makeStub();
  const out = await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'incognito', on: true }] })), loaded.stub, {});
  assert.deepStrictEqual(loaded.calls, []);
  assert.ok(out.warnings.some((w) => w.includes('incognito: Incognito can only be enabled on a blank editor')));
});

test('every new §10 op drops its variant like the rest of the settings profile', () => {
  for (const action of [
    { op: 'compare', mode: 'none' },
    { op: 'zoom', fit: true },
    { op: 'renameProject', name: 'x' },
    { op: 'projectColor', color: '' },
    { op: 'blankColor', color: '#dbeafe' },
    { op: 'openProject', name: 'x' },
    { op: 'incognito', on: true },
  ]) {
    const p = dropsWithWarning(plan({ variants: [{ actions: [action] }] }),
      new RegExp(`Dropped variant 1 .*editor-settings op "${action.op}" is not allowed inside variants`));
    assert.strictEqual(p.variants.length, 0);
  }
});
