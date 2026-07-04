// Server live-edit smoke: black-box the running binary's WebSocket + raw-TCP protocol
// (server/internal/hub). Scripts the hello→subscribe→welcome→edit/save handshake with
// two clients and asserts edit fan-out, the save ACK, and the raw-TCP path.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, stackEnabled } from '../../helpers/serverApi.js';
import { dialWS, dialTCP, join, T } from '../../helpers/wire.js';

test.describe('server live-edit protocol', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('WS: hello → subscribe → welcome, edit fan-out, save ACK', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'ws-smoke' });

    const a = await dialWS();
    const b = await dialWS();
    try {
      const welcomeA = await join(a, { token, projectId: project.id, clientId: 'A' });
      expect(welcomeA.type).toBe(T.welcome);
      await join(b, { token, projectId: project.id, clientId: 'B' });

      // A's edit relays to B, stamped with the origin client id.
      a.send({ type: T.edit, version: 0, op: 'addLine', payload: { x: 1 } });
      const relayed = await b.readUntil(T.edit);
      expect(relayed.op).toBe('addLine');
      expect(relayed.fromClientId).toBe('A');

      // A saves at version 0 → ACK at version 1; B sees the broadcast.
      a.send({ type: T.save, version: 0, layout: { lines: [1] } });
      const ack = await a.readUntil(T.synced);
      expect(ack.version).toBe(1);
      const bSynced = await b.readUntil(T.synced);
      expect(bSynced.version).toBe(1);
    } finally {
      a.close();
      b.close();
    }
  });

  test('WS: bad token is rejected as unauthorized', async ({ request }) => {
    const project = await createProject(request, await issueToken(request), { name: 'ws-auth' });
    const c = await dialWS();
    try {
      c.send({ type: T.hello, token: 'not-a-real-token', projectId: project.id });
      const err = await c.readUntil(T.error);
      expect(err.code).toBe('unauthorized');
    } finally {
      c.close();
    }
  });

  test('TCP: NDJSON hello → subscribe → welcome', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'tcp-smoke' });

    const c = await dialTCP();
    try {
      const welcome = await join(c, { token, projectId: project.id, clientId: 'T' });
      expect(welcome.type).toBe(T.welcome);
      expect(welcome.project?.id).toBe(project.id);
    } finally {
      c.close();
    }
  });
});
