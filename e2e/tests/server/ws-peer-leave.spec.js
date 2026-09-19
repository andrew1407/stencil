// Peer-leave on a half-open WS peer. The server pings every accepted connection each 30s
// (wsPingInterval) and hard-closes it if no pong arrives within 10s (wsPongTimeout), so the hub
// unregisters the member and publishes a `peer-leave` frame to the rest within ~40s, which is
// what this asserts. Half-open is simulated by PAUSING B's TCP socket: it then answers no pings
// and emits no FIN/RST, where destroying it would exercise ordinary close detection instead.
import { test, expect } from '@playwright/test';
import { issueToken, createProject, stackEnabled } from '../../helpers/serverApi.js';
import { dialWS, join, T } from '../../helpers/wire.js';

test.describe('WS keepalive peer reaping', () => {
  test.skip(!stackEnabled, 'requires the backing stack (E2E_STACK=1)');

  test('a half-open peer is reaped and peer-leave reaches the survivor', async ({ request }) => {
    // The compose server runs the default 30s ping + 10s pong-timeout cadence,
    // so the reap can take up to ~40s; allow generous margin over that.
    test.setTimeout(90_000);

    const token = await issueToken(request);
    const project = await createProject(request, token, { name: 'reap' });

    const a = await dialWS();
    const b = await dialWS();
    try {
      await join(a, { token, projectId: project.id, clientId: 'A' });
      await join(b, { token, projectId: project.id, clientId: 'B' });
      await a.readWhere(T.peerJoin, (m) => m.clientId === 'B');

      // B goes half-open: silent at the WS level, TCP connection still standing.
      b._raw._socket.pause();

      // The keepalive reaps B and the hub announces it to A within ping
      // interval (30s) + pong timeout (10s) + margin.
      const leave = await a.readWhere(T.peerLeave, (m) => m.clientId === 'B', 75_000);
      expect(leave.clientId).toBe('B');
    } finally {
      try { b.terminate(); } catch { /* already reaped */ }
      a.close();
    }
  });
});
