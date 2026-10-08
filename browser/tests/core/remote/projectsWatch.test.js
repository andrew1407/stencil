// js/core/remote/projectsWatch.js: a peer tab's save of the open project waits out a gesture and is
// then adopted, unless the gesture committed an edit of ours, which is saved after it and wins.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

installDom().register('notify-balloon', createStubElement('div', { notify() {} }));
const { onRemoteProjectsChange } = await import('../../../js/core/remote/projectsWatch.js');

const flush = () => new Promise((r) => setImmediate(r));
const appRig = () => {
  const app = {
    activeProjectId: 'p1', isDrawing: false, syncs: 0,
    history: { history: [{}], historyStep: 0 },
    storage: { syncActiveFromStorage: () => { app.syncs++; } }, updateProjectTitle() {},
  };
  return app;
};
const updated = { id: 'p1', action: 'updated' };

test('an idle editor adopts the peer save at once', async () => {
  const app = appRig();
  onRemoteProjectsChange(app, updated);
  await flush();
  assert.equal(app.syncs, 1);
});

test('a peer save mid-gesture is adopted when the gesture ends, once', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = appRig();
  app.isDrawing = true;
  onRemoteProjectsChange(app, updated);
  onRemoteProjectsChange(app, updated);
  await flush();
  t.mock.timers.tick(500); await flush();
  assert.equal(app.syncs, 0, 'nothing swaps under the pointer');
  app.isDrawing = false;
  t.mock.timers.tick(200); await flush();
  assert.equal(app.syncs, 1, 'deferred, not dropped, and collapsed to one');
});

test('a gesture that committed an edit keeps it: our later save wins', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = appRig();
  app.isDraggingPoint = true;
  onRemoteProjectsChange(app, updated);
  await flush();
  app.history.history.push({});
  app.history.historyStep = 1;
  app.isDraggingPoint = false;
  t.mock.timers.tick(200); await flush();
  assert.equal(app.syncs, 0);
});

test('a project switched away during the gesture is left alone', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = appRig();
  app.isDrawing = true;
  onRemoteProjectsChange(app, updated);
  await flush();
  app.isDrawing = false;
  app.activeProjectId = 'p2';
  t.mock.timers.tick(200); await flush();
  assert.equal(app.syncs, 0);
});
