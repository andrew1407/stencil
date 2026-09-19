// §10 project management (js/llm/opPlan.js): removeProject (named and current),
// clearProjects, renameProject and projectColor over their guarded flows.
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../js/llm/opPlan.js';
import { plan, ok, bad, makeStub, dropsWithWarning } from './helpers/opPlanRig.js';

// ── §10 project management: removeProject / clearProjects ──

test('removeProject/clearProjects: shapes, and both are editor-settings scoped', () => {
  const p = parseOpPlan(plan({ actions: [
    { op: 'removeProject', name: 'portrait 1' }, { op: 'clearProjects' },
  ] }));
  assert.deepStrictEqual(p.actions, [
    { op: 'removeProject', name: 'portrait 1' }, { op: 'clearProjects' },
  ]);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'removeProject' }] })), /name/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'removeProject', name: '  ' }] })), /name/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'clearProjects', name: 'x' }] })), /unknown/i);
  for (const op of ['removeProject', 'clearProjects']) {
    const v = dropsWithWarning(plan({
      variants: [{ label: 'v', actions: [op === 'removeProject' ? { op, name: 'x' } : { op }] }],
    }), /Dropped variant 1 \("v"\).*not allowed inside variants/);
    assert.strictEqual(v.variants.length, 0);
  }
});

test('executor: project ops run the injected guarded flows; their notes surface', async () => {
  const { stub } = makeStub();
  const calls = [];
  const p = parseOpPlan(plan({ actions: [
    { op: 'removeProject', name: 'a' }, { op: 'clearProjects' },
  ] }));
  const { warnings } = await executeOpPlan(p, stub, {
    removeProjectNamed: async (n) => { calls.push(['remove', n]); return 'removal canceled'; },
    clearLocalProjects: async () => { calls.push(['clear']); return null; },
  });
  assert.deepStrictEqual(calls, [['remove', 'a'], ['clear']]);
  assert.ok(warnings.some((w) => w.includes('removal canceled')));
});

// ── §10 removeProject.current / renameProject / projectColor ──

test('removeProject: exactly one of name / current:true', () => {
  assert.deepStrictEqual(ok({ op: 'removeProject', current: true }), { op: 'removeProject', current: true });
  bad({ op: 'removeProject' });
  bad({ op: 'removeProject', name: 'x', current: true });
  bad({ op: 'removeProject', current: false });          // current must be true
  bad({ op: 'removeProject', current: 1 });
});

test('executor: removeProject current resolves the ACTIVE project\'s name; none → note', async () => {
  const removed = [];
  const p = parseOpPlan(plan({ actions: [{ op: 'removeProject', current: true }] }));

  const active = makeStub({ current: { name: 'Portrait 1', incognito: false } });
  await executeOpPlan(p, active.stub, { removeProjectNamed: async (n) => { removed.push(n); return null; } });
  assert.deepStrictEqual(removed, ['Portrait 1']);

  // No active saved project (or an incognito editor) → note + skip, never a failed plan.
  for (const current of [undefined, { name: 'Incognito (unsaved)', incognito: true }]) {
    const bare = makeStub({ current });
    const out = await executeOpPlan(p, bare.stub, { removeProjectNamed: async (n) => { removed.push(n); return null; } });
    assert.ok(out.warnings.some((w) => w.includes('no active saved project')));
  }
  assert.deepStrictEqual(removed, ['Portrait 1']);       // the skips never reached the remover
});

// §10: with nothing saved but an image on screen, "remove this project" means the thing
// the user is looking at — refusing on the technicality was the complaint.
test('executor: removeProject current falls back to the clear flow on an unsaved editor', async () => {
  const p = parseOpPlan(plan({ actions: [{ op: 'removeProject', current: true }] }));
  const removed = [];
  const remover = async (n) => { removed.push(n); return null; };

  for (const current of [undefined, { name: 'Incognito (unsaved)', incognito: true }]) {
    let cleared = 0;
    const { stub } = makeStub({ current });                 // imageSize is set: a picture IS open
    const out = await executeOpPlan(p, stub, {
      removeProjectNamed: remover,
      clearWorkingImage: async () => { cleared++; return null; },
    });
    assert.strictEqual(cleared, 1, 'the clear flow ran');
    assert.deepStrictEqual(out.warnings, []);
    assert.ok(!out.warnings.some((w) => w.includes('no active saved project')));
  }
  assert.deepStrictEqual(removed, [], 'the remover was never called for an unsaved editor');

  // A declined confirm is a note, never a failed plan (the removeProject rule).
  const { stub } = makeStub({ current: undefined });
  const declined = await executeOpPlan(p, stub, {
    removeProjectNamed: remover,
    clearWorkingImage: async () => 'removal canceled',
  });
  assert.ok(declined.warnings.some((w) => w.includes('removeProject: removal canceled')));

  // A SAVED project still takes the ordinary remove path — unchanged.
  const saved = makeStub({ current: { name: 'Portrait 1', incognito: false } });
  let cleared = 0;
  await executeOpPlan(p, saved.stub, {
    removeProjectNamed: remover,
    clearWorkingImage: async () => { cleared++; return null; },
  });
  assert.deepStrictEqual(removed, ['Portrait 1']);
  assert.strictEqual(cleared, 0);

  // Nothing saved AND nothing on screen → the old note stands (there is nothing to remove).
  const empty = makeStub({ current: undefined, imageSize: undefined, lines: [] });
  const out = await executeOpPlan(p, empty.stub, {
    removeProjectNamed: remover,
    clearWorkingImage: async () => { cleared++; return null; },
  });
  assert.strictEqual(cleared, 0);
  assert.ok(out.warnings.some((w) => w.includes('no active saved project')));
});

test('renameProject: name 1..80; notes surface from the injected capability', async () => {
  assert.deepStrictEqual(ok({ op: 'renameProject', name: ' New name ' }), { op: 'renameProject', name: 'New name' });
  bad({ op: 'renameProject' });
  bad({ op: 'renameProject', name: '  ' });
  bad({ op: 'renameProject', name: 'x'.repeat(81) });

  const { stub } = makeStub();
  const p = parseOpPlan(plan({ actions: [{ op: 'renameProject', name: 'Taken' }] }));
  const out = await executeOpPlan(p, stub, {
    renameActiveProject: async (n) => `a project named "${n}" already exists`,
  });
  assert.ok(out.warnings.some((w) => w.includes('renameProject: a project named "Taken" already exists')));
  await assert.rejects(() => executeOpPlan(p, stub, {}), /cannot manage projects/);
});

test('projectColor: #rrggbb or "" (clear); no active project → the facade throw is a note', async () => {
  assert.deepStrictEqual(ok({ op: 'projectColor', color: '#ec4899' }), { op: 'projectColor', color: '#ec4899' });
  assert.deepStrictEqual(ok({ op: 'projectColor', color: '' }), { op: 'projectColor', color: '' });
  bad({ op: 'projectColor' });
  bad({ op: 'projectColor', color: 'pink' });
  bad({ op: 'projectColor', color: '#ec489' });

  const active = makeStub({ current: { name: 'P', incognito: false } });
  await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'projectColor', color: '#ec4899' }] })), active.stub, {});
  assert.deepStrictEqual(active.calls, [['projectColor', '#ec4899']]);

  const bare = makeStub();
  const out = await executeOpPlan(parseOpPlan(plan({ actions: [{ op: 'projectColor', color: '' }] })), bare.stub, {});
  assert.deepStrictEqual(bare.calls, []);
  assert.ok(out.warnings.some((w) => w.includes('projectColor: No active project to colour')));
});
