// The §10 expansion end-to-end (js/llm/controller.js): compare/zoom/undo, the project
// capabilities and copy's layout write, each note surfacing instead of failing the turn.
import { test } from 'node:test';
import assert from 'node:assert';
import { makeClient, makeController } from '../../helpers/chatControllerRig.js';

// ── The §10 expansion, wired end-to-end through a chat turn ──────────────────

test('compare/zoom/undo settings plan dispatches through the facade view controls', async () => {
  const viewPlan = JSON.stringify({
    version: 1, reply: 'view set', actions: [
      { op: 'compare', mode: 'vertical', split: 0.4 },
      { op: 'zoom', percent: 150 },
      { op: 'undo', steps: 2 },
      { op: 'zoom', fit: true },
    ],
  });
  const { controller, calls } = makeController(makeClient(viewPlan));
  const entry = await controller.send('compare side by side at 40%, zoom in, undo twice, then fit');
  assert.strictEqual(entry.reply, 'view set');
  assert.deepStrictEqual(calls, [
    ['compareMode', 'vertical'], ['compareSplit', 0.4],
    ['zoomLevel', 150],
    ['undo'], ['undo'],
    ['zoomFit'],
  ]);
});

test('renameProject routes through the injected capability; the store\'s refusal is a note', async () => {
  const renamePlan = JSON.stringify({
    version: 1, reply: 'renamed', actions: [{ op: 'renameProject', name: 'Taken' }],
  });
  const renamed = [];
  const { controller } = makeController(makeClient(renamePlan), {
    renameActiveProject: async (n) => { renamed.push(n); return `a project named "${n}" already exists`; },
  });
  const out = await controller.send('rename this project to Taken');
  assert.deepStrictEqual(renamed, ['Taken']);
  assert.ok(out.warnings.some((w) => w.includes('renameProject: a project named "Taken" already exists')));
});

test('blankColor on a non-blank project skips with the capability\'s note', async () => {
  const recolorPlan = JSON.stringify({
    version: 1, reply: 'recoloured', actions: [{ op: 'blankColor', color: '#dbeafe' }],
  });
  const { controller } = makeController(makeClient(recolorPlan), {
    setBlankColor: async () => 'only a blank project has a recolourable background',
  });
  const out = await controller.send('make the background light blue');
  assert.ok(out.warnings.some((w) => w.includes('blankColor: only a blank project has a recolourable background')));
});

test('incognito on a loaded editor: the facade throw becomes a note, not a failed turn', async () => {
  const incognitoPlan = JSON.stringify({
    version: 1, reply: 'ok', actions: [{ op: 'incognito', on: true }],
  });
  const { controller } = makeController(makeClient(incognitoPlan));
  const out = await controller.send('go incognito');
  assert.strictEqual(out.reply, 'ok');
  assert.ok(out.warnings.some((w) => w.includes('incognito: Incognito can only be enabled on a blank editor')));
});

test('openProject routes through the injected capability, notes included', async () => {
  const openPlan = JSON.stringify({
    version: 1, reply: 'opened', actions: [{ op: 'openProject', name: 'Cat' }],
  });
  const opened = [];
  const { controller } = makeController(makeClient(openPlan), {
    openProjectNamed: async (n) => { opened.push(n); return 'open canceled'; },
  });
  const out = await controller.send('open my Cat project');
  assert.deepStrictEqual(opened, ['Cat']);
  assert.ok(out.warnings.some((w) => w.includes('openProject: open canceled')));
});

test('copy what:"layout" awaits the injected outcome promise; a blocked write is a warning', async () => {
  const copyPlan = JSON.stringify({
    version: 1, reply: 'copied', actions: [{ op: 'copy', what: 'layout' }],
  });
  let wrote = 0;
  const okRun = makeController(makeClient(copyPlan), { copyLayoutRendered: async () => { wrote++; } });
  okRun.stencil.lines = [{ points: [{ x: 1, y: 1 }] }];
  const good = await okRun.controller.send('copy the layout json');
  assert.strictEqual(wrote, 1);
  assert.deepStrictEqual(good.warnings, []);

  const denied = makeController(makeClient(copyPlan), {
    copyLayoutRendered: async () => { throw new Error('Write permission denied'); },
  });
  denied.stencil.lines = [{ points: [{ x: 1, y: 1 }] }];
  const out = await denied.controller.send('copy the layout json');
  assert.ok(out.warnings.some((w) => w.includes('Copy to clipboard failed — Write permission denied')));
});
