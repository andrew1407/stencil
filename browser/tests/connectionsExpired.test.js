// A session the server forgot (js/net/connectionManager.js): the expired state, its one
// warning and labelled Reconnect, and the adoption that spares a doomed request.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { ConnectionManager } from '../js/net/connectionManager.js';
import { COMPONENTS_CSS } from './helpers/css.js';
import { StubWS, deadSessionServer } from './helpers/connectionsRig.js';

test('a refused token lands in the expired state, not the unreachable one', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  const err = await mgr.connect({ url: 'http://a:1', token: 'stale' }).then(() => null, (e) => e);
  assert.ok(err, 'the connect still rejects — the caller decides what to say');
  assert.strictEqual(err.expired, true, 'and it is tagged as a dead SESSION');
  // Not live…
  assert.deepStrictEqual(mgr.urls, [], 'nothing may use a refused token');
  assert.strictEqual(mgr.has('http://a:1'), false);
  // …but remembered, with its URL, so the UI has a row to show and act on.
  assert.deepStrictEqual(mgr.expiredUrls, ['http://a:1']);
  assert.deepStrictEqual(mgr.knownUrls, ['http://a:1']);
  assert.strictEqual(mgr.isExpired('http://a:1'), true);
  assert.strictEqual(mgr.get('http://a:1').status, 'expired', 'its own dot state');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'stale', expired: true }],
    'the saved URL survives — re-authenticating must not mean retyping the address');
});

test('an expired session is never retried on its own, and a new token revives it', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  const after = server.state.calls.length;
  // Nothing in the manager touches it again by itself.
  assert.deepStrictEqual(await mgr.remoteProjects(), []);
  assert.strictEqual(server.state.calls.length, after, 'not one extra request');
  // The row's own Reconnect: a fresh mint first (all an open server needs)…
  await assert.rejects(() => mgr.reconnectOne('http://a:1', ''), /bad admin token/,
    'this server mints only for an admin token');
  assert.strictEqual(mgr.isExpired('http://a:1'), true, 'still expired, still shown');
  // …then the token the user pastes — the admin one mints a session, desktop parity.
  await mgr.reconnectOne('http://a:1', 'admin-token');
  assert.deepStrictEqual(mgr.urls, ['http://a:1']);
  assert.deepStrictEqual(mgr.expiredUrls, [], 'the expired row is gone once it is live');
  assert.strictEqual(mgr.get('http://a:1').status, 'connected');
  assert.strictEqual(mgr.get('http://a:1').token, 'issued-token');
});

test('a valid saved token still connects normally, and an UNREACHABLE server is not "expired"', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'issued-token' });
  assert.deepStrictEqual(mgr.urls, ['http://a:1']);
  assert.deepStrictEqual(mgr.expiredUrls, []);
  assert.strictEqual(mgr.get('http://a:1').status, 'connected');
  // A server that is simply down keeps the old behaviour — it may come back.
  const down = new ConnectionManager({
    fetchImpl: async () => { throw new TypeError('Failed to fetch'); }, WebSocketImpl: StubWS,
  });
  const err = await down.connect({ url: 'http://b:2', token: 't' }).then(() => null, (e) => e);
  assert.ok(err && !err.expired, 'not a credential problem');
  assert.deepStrictEqual(down.expiredUrls, [], 'and no expired row for it');
});

test('removing an expired row forgets it, exactly like disconnecting a live one', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  assert.strictEqual(mgr.reconnectable, true, 'reconnect-all can act on it');
  mgr.disconnect('http://a:1');
  assert.deepStrictEqual(mgr.knownUrls, [], 'the row is gone from the modal…');
  assert.deepStrictEqual(mgr.snapshot(), [], '…and from what gets saved');
  // disconnectAll clears them too.
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  mgr.disconnectAll();
  assert.deepStrictEqual(mgr.knownUrls, []);
});

test('boot reports an expired session as such — one warning, one clickable toast', () => {
  const src = readFileSync(new URL('../js/console/stencilApi.js', import.meta.url), 'utf8');
  const boot = src.slice(src.indexOf('if (firstInit && getAutoConnect())'), src.indexOf('let peers = []'));
  // Settled, so a dead token can never surface as an unhandled rejection…
  assert.ok(boot.includes('Promise.allSettled('), 'boot never leaves a rejection loose');
  // …counted apart from unreachable servers, because the cures differ.
  assert.ok(boot.includes('r.reason?.expired'));
  assert.match(boot, /Session expired on \$\{expired\.length\} saved server/);
  // A count says nothing about WHICH server to go check — one toast per address instead.
  assert.ok(boot.includes("notify(`Couldn't reach ${url}`, 'info')"));
  assert.ok(!/Couldn't reach \$\{unreachable\.length\}/.test(boot), 'not a count');
  // One diagnostic line PER CASE (a known-dead session adopted at boot, or one that
  // turns out dead now) — and both are warnings, never errors, never a raw rejection.
  assert.strictEqual(boot.split('console.').length - 1, 2, 'one line per case, no more');
  assert.strictEqual(boot.split('console.warn(').length - 1, 2);
  assert.ok(!/console\.(error|log)\(/.test(boot));
  // The toast is the way in: it opens Connections, where the row offers Reconnect.
  assert.ok(boot.includes("onClick: () => document.getElementById('connect-btn')?.click()"));
});

test('the connections modal shows expired rows with a labelled Reconnect', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const known = cm ? cm.knownUrls : []'), 'expired rows are listed too');
  assert.match(src, /expired: 'Session expired — reconnect to sign in again'/);
  assert.ok(src.includes("row.classList.add('connect-expired')"));
  // Mint first, then ask for a token — and the token may be the ADMIN one.
  assert.ok(src.includes("mgr().reconnectOne(url, expired ? '' : undefined)"));
  assert.ok(src.includes('isExpiredSession(err)'));
  assert.match(src, /admin token, which mints a fresh session/);
  assert.ok(src.includes("mgr().reconnectOne(url, String(token).trim())"));
  const css = COMPONENTS_CSS;
  assert.match(css, /\.conn-status-expired \{ background: #e0a800/, 'amber, not the red of a dead server');
  // The labelled button wears that amber as a FILL: every other row button is
  // accent-filled, so an amber outline on an accent face was unreadable.
  assert.match(css, /\.connect-row\.connect-expired \.connect-reconnect-one:not\(:disabled\) \{[^}]*background: #e0a800/,
    'the row’s Reconnect is amber-filled to match the row it fixes');
});

// A dead credential must not cost a doomed /projects call — and Chrome's own red 401 — on every
// single load.
test('a refused credential is remembered as refused, and adopted without a request', async () => {
  const server = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  // What gets persisted now carries the refusal…
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'stale', expired: true }]);
  // …and the next boot adopts it with ZERO requests, keeping the row and its action.
  const next = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  const before = server.state.calls.length;
  next.adoptExpired({ url: 'http://a:1', token: 'stale' });
  assert.strictEqual(server.state.calls.length, before, 'not one request');
  assert.deepStrictEqual(next.expiredUrls, ['http://a:1']);
  assert.strictEqual(next.get('http://a:1').status, 'expired');
  assert.strictEqual(next.reconnectable, true, 'and it is still actionable');
  // Adopting twice, or over a live connection, is a no-op.
  next.adoptExpired({ url: 'http://a:1', token: 'stale' });
  assert.deepStrictEqual(next.expiredUrls, ['http://a:1']);
  // Signing in again clears the flag, so it stops being persisted as dead — and the
  // credential's KIND is now known, so the next connect skips the doomed probe.
  await next.reconnectOne('http://a:1', 'admin-token');
  assert.deepStrictEqual(next.snapshot(), [{ url: 'http://a:1', token: 'admin-token', kind: 'admin' }]);
});

test('a live connection to the same url repairs the expired record', async () => {
  const { fetchImpl } = deadSessionServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'stale' }).catch(() => {});
  assert.deepStrictEqual(mgr.expiredUrls, ['http://a:1']);
  // The user connects that SAME server with a working credential (the connect form):
  // the dead record must not linger beside it — one url, one row.
  await mgr.connect({ url: 'http://a:1', token: 'issued-token' });
  assert.deepStrictEqual(mgr.expiredUrls, [], 'the expired record is repaired away');
  assert.deepStrictEqual(mgr.knownUrls, ['http://a:1'], 'and the modal shows ONE row');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'issued-token' }]);
});

test('boot adopts known-dead sessions instead of re-requesting them', () => {
  const src = readFileSync(new URL('../js/console/stencilApi.js', import.meta.url), 'utf8');
  const boot = src.slice(src.indexOf('if (firstInit && getAutoConnect())'), src.indexOf('let peers = []'));
  assert.ok(boot.includes('const dead = saved.filter((s) => s.expired)'));
  assert.ok(boot.includes('connMgr.adoptExpired(s)'), 'no request for a credential already refused');
  assert.ok(boot.includes('Promise.allSettled(live.map((s) => connMgr.connect(s)))'), 'the rest still connect');
  // The saved shape keeps the flag, or the next boot would forget and retry.
  const store = readFileSync(new URL('../js/net/connectionStore.js', import.meta.url), 'utf8');
  assert.match(store, /if \(s\.expired\) out\.expired = true;/);
  assert.match(store, /if \(s\.kind === 'admin'\) out\.kind = 'admin';/, 'the credential kind rides along too');
});

test('the Servers button says a session needs signing in again — in its tooltip, not a dot', () => {
  const src = readFileSync(new URL('../js/ui/connectModal.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const syncExpiredBadge = ()'));
  assert.match(src, /a saved session expired, reconnect to sign in again/, 'the tooltip says what to do');
  // Runs at wire time AND on every connections change, and covers the fullscreen clone.
  assert.ok(src.includes("document.querySelectorAll('#fs-controls-panel #connect-btn')"));
  const at = src.indexOf('syncExpiredBadge();');
  assert.ok(at > -1 && at < src.indexOf('subscribe(EVENTS.connectionsChanged'),
    'the tooltip is correct before any event fires');
  // No corner badge on the icon (desktop parity: its toolbar wears none).
  assert.ok(!src.includes('conn-needs-auth'), 'no badge class is set on the button');
  const css = COMPONENTS_CSS;
  assert.ok(!css.includes('conn-needs-auth'), 'no badge rule survives in the stylesheet');
});
