// Closing the project open here (js/ui/projects/closeProject.js): the one question the ✕ drop
// and the closeProject hotkey ask — not a danger, naming the project and that it stays saved —
// and the toast instead of a question when nothing is open.
import test from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

const doc = installDom();
const toasts = [];
doc.register('notify-balloon', createStubElement('div', { notify: (msg, type) => toasts.push([msg, type]) }));
const { confirmCloseProject, closeProjectMessage } = await import('../../../js/ui/projects/closeProject.js');

const appWith = ({ active = 'p1', answer = true, during = () => {} } = {}) => {
  const asked = [];
  const app = {
    activeProjectId: active,
    closed: [],
    storage: { store: { getMeta: (id) => (id === 'p1' ? { id, name: 'Harbour plan' } : null) } },
    confirm: async (message, opts) => { asked.push({ message, opts }); during(app); return answer; },
    closeProject(id) { app.closed.push(id); return app; },
  };
  return { app, asked };
};

test('the open project is named, said to stay saved, and closed here once the answer is yes', async () => {
  toasts.length = 0;
  const anchor = createStubElement('button');
  const { app, asked } = appWith();
  assert.equal(await confirmCloseProject(app, { closeAnchor: anchor }), true);
  assert.equal(asked.length, 1);
  assert.equal(asked[0].message, closeProjectMessage('Harbour plan'));
  assert.match(asked[0].message, /"Harbour plan"/);
  assert.match(asked[0].message, /stays saved in Projects/);
  assert.equal(asked[0].opts.title, 'Close project?');
  assert.equal(asked[0].opts.confirmLabel, 'Close');
  assert.ok(!asked[0].opts.danger, 'nothing is removed, so it is no danger');
  assert.equal(asked[0].opts.closeAnchor, anchor, 'the answer pours back where it was asked from');
  assert.deepEqual(app.closed, ['p1']);
  assert.deepEqual(toasts, [['Closed "Harbour plan"', 'ok']]);
});

test('a No, or another project opened while asking, closes nothing', async () => {
  const no = appWith({ answer: false });
  assert.equal(await confirmCloseProject(no.app), false);
  assert.deepEqual(no.app.closed, []);
  const moved = appWith({ during: (app) => { app.activeProjectId = 'p2'; } });
  assert.equal(await confirmCloseProject(moved.app), false);
  assert.deepEqual(moved.app.closed, [], 'p2 was never asked about');
});

test('with nothing open a toast says so and nothing is asked', async () => {
  toasts.length = 0;
  const { app, asked } = appWith({ active: null });
  assert.equal(await confirmCloseProject(app), false);
  assert.equal(asked.length, 0);
  assert.deepEqual(app.closed, []);
  assert.deepEqual(toasts, [['No project is open', 'info']]);
});

test('ask: false — the ✕ drop — closes at once with the same notice', async () => {
  toasts.length = 0;
  const { app, asked } = appWith();
  assert.equal(await confirmCloseProject(app, { ask: false }), true);
  assert.equal(asked.length, 0);
  assert.deepEqual(app.closed, ['p1']);
  assert.deepEqual(toasts, [['Closed "Harbour plan"', 'ok']]);
});
