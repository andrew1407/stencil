// Clear All Local on the real projects modal (js/ui/projects/list/actions.js): only the local rows
// play out; a server row stays where it is, never scattering and arriving again (user report).
import test from 'node:test';
import assert from 'node:assert/strict';
import { mountProjectsModal } from '../../../helpers/projectsModalRig.js';
import { localLeavers } from '../../../../js/ui/projects/list/actions.js';

const METAS = [{ id: 'a', name: 'Alpha', updatedAt: 2 }, { id: 'b', name: 'Beta', updatedAt: 1 }];
const SERVER = 'https://srv.example';
const flush = async () => { for (let i = 0; i < 20; i++) await Promise.resolve(); };

test('Clear All Local plays out the local rows and leaves the server row in place', async (t) => {
  const calls = [];
  const app = {
    confirm: async () => true,
    connections: { urls: [SERVER], has: () => true, get: () => null,
      remoteProjects: async () => [{ id: 'r1', name: 'Shared', serverUrl: SERVER, hasImage: true, imageW: 4, imageH: 4,
        createdAt: 1, updatedAt: 1, version: 1 }] },
    projectTransfer: new Proxy({}, { get: (_, name) => (...args) => calls.push([name, ...args]) }),
  };
  const m = await mountProjectsModal(t, { metas: METAS, mode: 'none', app });
  m.change('projects-filter', 'all');
  await flush();
  m.change('projects-filter', 'all');
  assert.ok(m.rows().some((r) => r.classList.contains('project-remote')), 'the server row is listed');
  assert.equal(m.rows().filter((r) => !r.classList.contains('project-remote')).length, 2);

  const { rows, keys } = localLeavers(m.list, m.keys());
  assert.deepEqual(rows.map((r) => r.dataset.filterKey), ['local:a', 'local:b'], 'only the local rows play out');
  assert.deepEqual(keys, ['local:a', 'local:b'], 'only the local keys are held as leaving');

  m.$('projects-clear-all').dispatch('click');
  await flush();
  assert.ok(calls.some(([name]) => name === 'clearAllProjects'), 'the local projects were cleared');
  assert.ok(m.rows().some((r) => r.classList.contains('project-remote')), 'the server row is still listed');
});
