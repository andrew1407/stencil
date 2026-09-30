// §10 copyProject (js/llm/plan/projectExecutors.js): its shape, the defaults it runs with, the
// capability it reaches, and the real capability's notes (js/llm/adapters/project.js).
import { test } from 'node:test';
import assert from 'node:assert';
import { parseOpPlan, executeOpPlan } from '../../../js/llm/plan/opPlan.js';
import { projectAdapters } from '../../../js/llm/adapters/project.js';
import { plan, makeStub, dropsWithWarning } from '../../helpers/opPlanRig.js';

test('copyProject: what is required; open, incognito and local are optional; incognito needs an open', () => {
  const p = parseOpPlan(plan({ actions: [{ op: 'copyProject', what: 'layout', open: 'here', incognito: true }] }));
  assert.deepStrictEqual(p.actions, [{ op: 'copyProject', what: 'layout', open: 'here', incognito: true }]);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'copyProject' }] })), /what/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'copyProject', what: 'lines' }] })), /what/);
  assert.throws(() => parseOpPlan(plan({ actions: [{ op: 'copyProject', what: 'image', incognito: true }] })), /incognito/);
  const v = dropsWithWarning(plan({ variants: [{ label: 'v', actions: [{ op: 'copyProject', what: 'image' }] }] }),
    /Dropped variant 1 \("v"\).*not allowed inside variants/);
  assert.strictEqual(v.variants.length, 0);
});

test('executor: copyProject hands the capability the full request; its note surfaces', async () => {
  const { stub } = makeStub();
  const asked = [];
  const p = parseOpPlan(plan({ actions: [{ op: 'copyProject', what: 'project' }] }));
  const { warnings } = await executeOpPlan(p, stub, {
    copyActiveProject: async (req) => { asked.push(req); return 'reduced'; },
  });
  assert.deepStrictEqual(asked, [{ what: 'project', open: 'none', incognito: false, local: false }]);
  assert.ok(warnings.some((w) => w === 'copyProject: reduced'));
  await assert.rejects(executeOpPlan(p, stub, {}), /cannot copy projects/);
});

const appWith = (over = {}) => {
  const calls = [];
  const app = {
    image: {}, remoteLink: null,
    projectTransfer: { copyProject: async (call) => { calls.push(call); return 'n1'; } },
    ...over,
  };
  return { app, calls, caps: projectAdapters(app) };
};

test('copyActiveProject: copies the live editor; nothing to copy or a failure is a note', async () => {
  const { caps, calls } = appWith();
  assert.strictEqual(await caps.copyActiveProject({ what: 'layout', open: 'newtab' }), null);
  assert.deepStrictEqual(calls, [{ id: null, what: 'layout', open: 'newtab' }]);
  assert.strictEqual(await appWith({ image: null }).caps.copyActiveProject({ what: 'image' }), 'there is no image to copy');
  const failing = appWith({ projectTransfer: { copyProject: async () => { throw new Error('quota'); } } });
  assert.strictEqual(await failing.caps.copyActiveProject({ what: 'image' }), 'could not make the copy — quota');
});

test('copyActiveProject: an incognito request on a server copy is made, and says what it dropped', async () => {
  const { caps, calls } = appWith({ remoteLink: { address: 'https://srv', remoteId: 'r1' } });
  const note = await caps.copyActiveProject({ what: 'image', open: 'here', incognito: true });
  assert.strictEqual(note, 'a server copy cannot be incognito — made it on the server');
  assert.strictEqual(calls.length, 1);
});
