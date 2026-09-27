// A session the server forgot (js/net/connectionManager.js): the expired state, its one
// warning and labelled Reconnect, and the adoption that spares a doomed request.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../../../js/net/connectionManager.js';
import { createStencil } from '../../../js/console/stencilApi.js';
import { StencilConnectModal } from '../../../js/ui/connect/modal.js';
import { EVENTS } from '../../../js/eventBus/appBus.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { StubWS, deadSessionServer, facadeApp } from '../../helpers/connectionsRig.js';
import { conn, openModal, rows, find, hasClass, sleep } from '../../helpers/connectModalRig.js';

// One first boot of the facade over the saved set: a's token is refused now, b and d are down,
// and c was refused on an earlier visit.
const SAVED = [{ url: 'http://a:1', token: 'stale' }, { url: 'http://b:2', token: '' },
  { url: 'http://c:3', token: 'old', expired: true }, { url: 'http://d:4', token: '' }];
const boot = async (t) => {
  const store = { drawingApp_servers: JSON.stringify(SAVED) };
  const doc = installDom({}, { localStorage: { getItem: (k) => store[k] ?? null, setItem: (k, v) => { store[k] = v; } } });
  t.after(doc.restore);
  const [notes, opened, hosts, server] = [[], [], [], deadSessionServer()];
  doc.register('notify-balloon', createStubElement('div', { notify: (...a) => notes.push(a) }));
  doc.register('connect-btn', createStubElement('button', { click: () => opened.push('connect-btn') }));
  t.mock.property(globalThis, 'WebSocket', StubWS);
  t.mock.property(globalThis, 'fetch', (url, init) => {
    const { host } = new URL(url);
    hosts.push(host);
    return ['b:2', 'd:4'].includes(host) ? Promise.reject(new TypeError('failed to fetch')) : server.fetchImpl(url, init);
  });
  const said = Object.fromEntries(['warn', 'error', 'log'].map((m) => [m, t.mock.method(console, m, () => {})]));
  const app = facadeApp(server);
  delete app.connections;
  createStencil(app);
  await sleep(20);
  // Node's own process warnings (hotkeys.js reads its localStorage at import) are not the app's.
  const own = (m) => said[m].mock.calls.map((c) => c.arguments[0]).filter((a) => !/ExperimentalWarning/.test(a));
  return { mgr: app.connections, notes, opened, hosts, said: own };
};

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

test('boot reports an expired session as such — one warning, one clickable toast', async (t) => {
  const { notes, opened, said } = await boot(t);
  // Settled, so a dead token can never surface as an unhandled rejection, and counted apart from
  // unreachable servers, because the cures differ.
  const [msg, type, opts] = notes.find(([m]) => /^Session expired/.test(m)) ?? [];
  assert.strictEqual(msg, 'Session expired on 1 saved server — reconnect');
  assert.strictEqual(type, 'fail');
  // A count says nothing about WHICH server to go check — one toast per address instead.
  assert.deepStrictEqual(notes.filter(([m]) => /reach/.test(m)),
    [["Couldn't reach http://b:2", 'info', undefined], ["Couldn't reach http://d:4", 'info', undefined]]);
  // One diagnostic line PER CASE (a known-dead session adopted at boot, or one that
  // turns out dead now) — and both are warnings, never errors, never a raw rejection.
  assert.strictEqual(said('warn').length, 2, 'one line per case, no more');
  assert.deepStrictEqual([...said('error'), ...said('log')], []);
  // The toast is the way in: it opens Connections, where the row offers Reconnect.
  opts.onClick();
  assert.deepStrictEqual(opened, ['connect-btn']);
});

test('the connections modal shows expired rows with a labelled Reconnect', async () => {
  const [live, dead] = ['http://a:1', 'http://b:2'];
  const calls = [];
  let asked = '';
  const { list, notes } = openModal([conn(live, ''), { ...conn(dead, ''), status: 'expired', connected: false }], {
    mgrExtra: { urls: [live], expiredUrls: [dead], knownUrls: [live, dead],
      reconnectOne: async (u, tok) => {
        calls.push([u, tok]);
        if (tok === '') throw Object.assign(new Error('refused'), { expired: true });
      } },
    appExtra: { prompt: async (msg) => { asked = msg; return '  admin-token  '; } },
  });
  const row = rows(list).find((r) => r.dataset.url === dead);
  assert.ok(row, 'expired rows are listed too');
  assert.ok(hasClass(row, 'connect-expired'));
  assert.match(find(row, 'connect-url').innerHTML, /data-title="Session expired — reconnect to sign in again"/);
  find(rows(list)[0], 'connect-reconnect-one').dispatch('click');
  find(row, 'connect-reconnect-one').dispatch('click');
  await sleep(20);
  // Mint first, then ask for a token — and the token may be the ADMIN one.
  assert.deepStrictEqual(calls, [[live, undefined], [dead, ''], [dead, 'admin-token']]);
  assert.match(asked, /admin token, which mints a fresh session/);
  assert.deepStrictEqual(notes.at(-1), [`Reconnected to ${dead}`, 'ok']);
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

test('boot adopts known-dead sessions instead of re-requesting them', async (t) => {
  const { mgr, hosts } = await boot(t);
  assert.ok(!hosts.includes('c:3'), 'no request for a credential already refused');
  assert.strictEqual(mgr.get('http://c:3').status, 'expired', 'its row is adopted as it was left');
  assert.deepStrictEqual([...new Set(hosts)].sort(), ['a:1', 'b:2', 'd:4'], 'the rest still connect');
});

test('the Servers button says a session needs signing in again — in its tooltip, not a dot', (t) => {
  const win = createStubElement('window');
  const doc = installDom({ autoCreateById: true }, { window: win, matchMedia: () => ({ matches: true }) });
  t.after(doc.restore);
  const [btn, clone] = [doc.register('connect-btn', createStubElement('button')), createStubElement('button')];
  doc.querySelectorAll = (sel) => (sel === '#fs-controls-panel #connect-btn' ? [clone] : []);
  const mgr = { knownUrls: [], urls: [], expiredUrls: ['http://a:1'], reconnectable: true, get: () => null };
  new StencilConnectModal().wire({ connections: mgr });
  // Runs at wire time AND on every connections change, and covers the fullscreen clone.
  for (const el of [btn, clone])
    assert.match(el.dataset.title, /a saved session expired, reconnect to sign in again/, 'correct before any event');
  mgr.expiredUrls = [];
  win.dispatch(EVENTS.connectionsChanged);
  for (const el of [btn, clone]) assert.strictEqual(el.dataset.title, 'Servers — connect to share & co-edit projects');
  // No corner badge on the icon (desktop parity: its toolbar wears none).
  assert.deepStrictEqual([btn, clone].map((el) => el.className), ['', ''], 'no badge class is set on the button');
  const css = COMPONENTS_CSS;
  assert.ok(!css.includes('conn-needs-auth'), 'no badge rule survives in the stylesheet');
});
