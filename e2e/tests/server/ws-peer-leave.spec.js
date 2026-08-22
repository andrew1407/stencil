// Peer-leave on a half-open WS peer — closes the suite's long-standing known gap.
// The server now runs a WS keepalive (server/internal/transport/ws.go): every
// accepted connection is pinged each 30s (wsPingInterval) and hard-closed if the
// pong doesn't arrive within 10s (wsPongTimeout), so a dead peer is detected
// within ~40s. The closed conn unblocks the hub's pending Read, the member is
// unregistered, and the session publishes a `peer-leave` frame to the remaining
// members (server/internal/hub/session.go).
//
// Signal choice: that protocol-level `peer-leave` frame at the surviving client
// is asserted directly — it is the strongest observable (the same one real
// clients use for presence), so no indirect proxy (REST delete guard /
// re-join peer counts) is needed.
//
// Half-open simulation: pausing B's underlying TCP socket stops it reading, so
// it never answers pings — no close frame, no FIN/RST, the connection just goes
// silent. (Destroying the socket would emit FIN/RST and exercise the ordinary
// close-detection path instead of the keepalive.) The `ws` lib answers pings
// automatically only while the socket is being read, so a paused socket is a
// faithful dead peer.
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
