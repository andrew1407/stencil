// Server WS robustness: the live-edit endpoint (server/internal/hub) must degrade
// cleanly under hostile/garbage input — a bad token is rejected with `unauthorized`,
// a project is a shared workspace so any valid token may join it, and a malformed
// non-JSON frame mid-session is dropped on the floor without tearing down the sender
// or its peers.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, stackEnabled } from '../../helpers/serverApi.js';
import { dialWS, join, T } from '../../helpers/wire.js';

test.describe('server WS robustness', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('a bad token is rejected; any valid token may join a shared project', async ({ request }) => {
    const tokenA = await issueToken(request, 'client-A');
    const tokenB = await issueToken(request, 'client-B'); // a distinct, valid session
    const project = await createProject(request, tokenA, { name: 'ws-shared' });

    // An invalid token is turned away with an `unauthorized` error frame.
    const bad = await dialWS();
    try {
      bad.send({ type: T.hello, token: 'not-a-real-token', projectId: project.id, clientId: 'X' });
      const err = await bad.readUntil(T.error);
      expect(err.code).toBe('unauthorized');
    } finally {
      bad.close();
    }

    // A different valid token joins A's project cleanly (shared workspace → welcome).
    const c = await dialWS();
    try {
      const welcome = await join(c, { token: tokenB, projectId: project.id, clientId: 'B' });
      expect(welcome.project.id).toBe(project.id);
    } finally {
      c.close();
    }
  });

  test('a malformed (non-JSON) frame mid-session is ignored, not fatal to the session', async ({ request }) => {
    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'ws-garbage' });

    const a = await dialWS();
    const b = await dialWS();
    try {
      await join(a, { token, projectId: project.id, clientId: 'A' });
      await join(b, { token, projectId: project.id, clientId: 'B' });

      // B fires a raw, un-parseable text frame straight down the socket. The server
      // json.Unmarshal fails and it `continue`s — B must stay connected, not be closed.
      b._raw.send('{ this is definitely not valid json ]]]');

      // The session is unharmed: A's edit still fans out to B, stamped with A's id.
      a.send({ type: T.edit, version: 0, op: 'addLine', payload: { x: 42 } });
      const relayed = await b.readUntil(T.edit);
      expect(relayed.op).toBe('addLine');
      expect(relayed.fromClientId).toBe('A');

      // …and B — the offender — is itself still live: its own edit fans out to A.
      b.send({ type: T.edit, version: 0, op: 'addRect', payload: { y: 7 } });
      const backToA = await a.readUntil(T.edit);
      expect(backToA.op).toBe('addRect');
      expect(backToA.fromClientId).toBe('B');
    } finally {
      a.close();
      b.close();
    }
  });
});
