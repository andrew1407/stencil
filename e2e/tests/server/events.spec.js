// The server's live feed, black-boxing the running binary over both transports: a hello with no
// projectId opens the global /events feed (created/updated/deleted), and a hello that names a
// project, or carries a bad token, is refused with an error frame and closed.
import { setTimeout as sleep } from 'node:timers/promises';
import { test, expect } from '@playwright/test';
import { issueToken, createProject, listProjects, bearer, SERVER_URL, stackEnabled } from '../../helpers/server/api.js';
import { dialWS, dialTCP, T } from '../../helpers/server/wire.js';

// Drives one feed client through a project's lifecycle and asserts the three events, in order.
async function expectLifecycle(request, token, ev) {
  ev.send({ type: T.hello, token });                     // no projectId: the global feed
  await sleep(200);          // let the feed subscription settle

  const project = await createProject(request, token, { name: 'evt' });
  const created = await ev.readWhere(T.projectEvent, (m) => m.project?.id === project.id);
  expect(created.event).toBe('created');
  expect(created.project.layout).toBeUndefined();

  const cur = (await listProjects(request, token)).find((p) => p.id === project.id);
  await request.put(`${SERVER_URL}/projects/${project.id}`, {
    headers: bearer(token), data: { name: 'evt2', version: cur.version },
  });
  const updated = await ev.readWhere(T.projectEvent, (m) => m.project?.id === project.id);
  expect(updated.event).toBe('updated');
  expect(updated.project.name).toBe('evt2');

  await request.delete(`${SERVER_URL}/projects/${project.id}`, { headers: bearer(token) });
  const deleted = await ev.readWhere(T.projectEvent, (m) => m.project?.id === project.id);
  expect(deleted.event).toBe('deleted');
}

test.describe('server events feed', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('WS: the global /events feed reports created / updated / deleted', async ({ request }) => {
    const token = await issueToken(request);
    const ev = await dialWS();
    try {
      await expectLifecycle(request, token, ev);
    } finally {
      ev.close();
    }
  });

  test('TCP: the same feed over NDJSON', async ({ request }) => {
    const token = await issueToken(request);
    const ev = await dialTCP();
    try {
      await expectLifecycle(request, token, ev);
    } finally {
      ev.close();
    }
  });

  test('a bad token is refused as unauthorized and the socket closed', async ({ request }) => {
    await issueToken(request); // the server is up and issuing
    const c = await dialWS();
    try {
      c.send({ type: T.hello, token: 'not-a-real-token' });
      const err = await c.readUntil(T.error);
      expect(err.code).toBe('unauthorized');
      await c.closed();
    } finally {
      c.close();
    }
  });

  test('a hello naming a project is refused: no session is served', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'no-session' });
    const c = await dialWS();
    try {
      c.send({ type: T.hello, token, projectId: project.id, clientId: 'A' });
      const err = await c.readUntil(T.error);
      expect(err.code).toBe('badRequest');
      expect(err.message).toContain('projectId');
      await c.closed();
    } finally {
      c.close();
      await request.delete(`${SERVER_URL}/projects/${project.id}`, { headers: bearer(token) });
    }
  });
});
