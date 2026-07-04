// Server live-session depth: the global /events feed (created/updated/deleted), peer
// join/leave lifecycle, and ephemeral cursor/presence relay — the parts of
// server/internal/hub the smoke handshake doesn't reach.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, listProjects, bearer, SERVER_URL, stackEnabled } from '../../helpers/serverApi.js';
import { dialWS, join, T } from '../../helpers/wire.js';

test.describe('server events + session lifecycle', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('global /events feed reports created / updated / deleted', async ({ request }) => {
    const token = await issueToken(request);

    // An events client is a hello with an EMPTY projectId (selects the global feed).
    const ev = await dialWS();
    try {
      ev.send({ type: T.hello, token });                     // no projectId, no subscribe
      await new Promise((r) => setTimeout(r, 200));          // let the feed subscription settle

      const project = await createProject(request, token, { name: 'evt' });
      const created = await ev.readUntil(T.projectEvent);
      expect(created.event).toBe('created');
      expect(created.project?.id).toBe(project.id);

      // A REST update publishes 'updated'.
      const cur = (await listProjects(request, token)).find((p) => p.id === project.id);
      await request.put(`${SERVER_URL}/projects/${project.id}`, {
        headers: bearer(token), data: { name: 'evt2', version: cur.version },
      });
      const updated = await ev.readUntil(T.projectEvent);
      expect(updated.event).toBe('updated');
      expect(updated.project?.id).toBe(project.id);

      await request.delete(`${SERVER_URL}/projects/${project.id}`, { headers: bearer(token) });
      const deleted = await ev.readUntil(T.projectEvent);
      expect(deleted.event).toBe('deleted');
      expect(deleted.project?.id).toBe(project.id);
    } finally {
      ev.close();
    }
  });

  test('peer-join fires when a peer joins the session', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'peers' });

    const a = await dialWS();
    const b = await dialWS();
    try {
      await join(a, { token, projectId: project.id, clientId: 'A' });
      // B joining is announced to A (the server also emits a self peer-join, so match on B).
      await join(b, { token, projectId: project.id, clientId: 'B' });
      const joinEv = await a.readWhere(T.peerJoin, (m) => m.clientId === 'B');
      expect(joinEv.clientId).toBe('B');
    } finally {
      a.close();
      b.close();
    }
    // NOTE: peer-LEAVE is intentionally not asserted here. The server has no WS
    // keepalive/read-deadline, so a dropped peer (graceful close frame OR abrupt TCP
    // reset) is not detected promptly — peer-leave doesn't arrive within a bounded
    // window, so it can't be asserted reliably in e2e. Flagged as a real server gap.
  });

  test('cursor is relayed to peers, stamped with the origin client', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'cursor' });

    const a = await dialWS();
    const b = await dialWS();
    try {
      await join(a, { token, projectId: project.id, clientId: 'A' });
      await join(b, { token, projectId: project.id, clientId: 'B' });

      a.send({ type: T.cursor, x: 12, y: 34 });
      const seen = await b.readUntil(T.cursor);
      expect(seen.fromClientId).toBe('A');
      expect([seen.x, seen.y]).toEqual([12, 34]);
    } finally {
      a.close();
      b.close();
    }
  });
});
