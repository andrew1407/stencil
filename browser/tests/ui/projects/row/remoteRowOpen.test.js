// A server row's open gesture (js/ui/projects/row/remoteRow.js), local rows' own: a double click opens it
// at once, where it once only asked twice (user report); a single click still asks first.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mountProjectsModal } from '../../../helpers/projectsModalRig.js';

const SERVER = 'https://srv.example';
const flush = async () => { for (let i = 0; i < 20; i++) await Promise.resolve(); };

const mount = async (t) => {
  const asked = [];
  const calls = [];
  const app = {
    confirm: async (message) => { asked.push(message); return true; },
    connections: { urls: [SERVER], has: () => true, get: () => null,
      remoteProjects: async () => [{ id: 'r1', name: 'Shared', serverUrl: SERVER, hasImage: true, imageW: 4, imageH: 4,
        createdAt: 1, updatedAt: 1, version: 1 }] },
    projectTransfer: new Proxy({}, { get: (_, name) => async (...args) => { calls.push([name, ...args]); } }),
  };
  const m = await mountProjectsModal(t, { metas: [{ id: 'a', name: 'Alpha', updatedAt: 2 }], mode: 'none', app });
  m.change('projects-filter', 'all');
  await flush();
  m.change('projects-filter', 'all');
  const row = m.rows().find((r) => r.classList.contains('project-remote'));
  assert.ok(row, 'the server row is listed');
  return { m, row, asked, calls };
};
const opened = (calls) => calls.filter(([name]) => name === 'openRemoteProject').length;

test('a double click on a server row opens it at once, unasked', async (t) => {
  const { m, row, asked, calls } = await mount(t);
  row.dispatch('click', { detail: 1 });
  row.dispatch('click', { detail: 2 });
  row.dispatch('dblclick', {});
  await flush();
  for (const { fn } of m.timers.splice(0)) fn();
  await flush();
  assert.equal(opened(calls), 1, 'opened once');
  assert.deepEqual(asked, [], 'no confirmation');
});

test('a single click on a server row still asks before opening', async (t) => {
  const { m, row, asked, calls } = await mount(t);
  row.dispatch('click', { detail: 1 });
  await flush();
  assert.equal(opened(calls), 0, 'nothing opens inside the double-click window');
  for (const { fn } of m.timers.splice(0)) fn();
  await flush();
  assert.equal(asked.length, 1, 'asked once');
  assert.equal(opened(calls), 1, 'then opened');
});
