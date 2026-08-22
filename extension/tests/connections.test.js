// Tests for the extension's server-connection layer (src/lib/connections.js):
// the pure pin/connection transforms plus the REST + chrome.storage wrappers,
// driven with injected fetch + a storage mock.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  normalizeUrl, isLoopbackHost, sharedPinFromProject, sharedPinsFromProjects, mergePins,
  upsertConnection, dropConnection, connect, listProjects, collectSharedPins,
  addServer, removeServer, loadConnections, CONNECTIONS_KEY,
  pinTargetMode, connectionByUrl, projectRequestFromImage, fetchProjectImage,
  parseInviteUrl, isAdminConnection, filterConnections, reconnectServer,
} from '../src/lib/connections.js';

import { readFileSync } from 'node:fs';
import { installChromeStub } from './helpers/chromeStub.js';
import { createFilterTransition } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';

const installStorageMock = () => {
  const stub = installChromeStub();
  return { peek: stub.peek, reset: stub.reset };
};

// A mock server: routes the REST subset the extension uses.
const mockFetch = (opts = {}) => {
  const projects = opts.projects || [];
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  return async (url, init = {}) => {
    const u = new URL(url);
    const method = init.method || 'GET';
    if (u.pathname === '/auth/token' && method === 'POST') return json(200, { token: 'tk', expiresAt: 0 });
    if (u.pathname === '/projects' && method === 'GET') return json(200, { projects });
    if (u.pathname === '/projects' && method === 'POST') return json(201, { id: 'p_new_a', name: JSON.parse(init.body).name });
    return json(404, { code: 'notFound', message: 'no route' });
  };
};

test('normalizeUrl is secure by default: bare remote → https, loopback → http', () => {
  assert.equal(normalizeUrl('host:8090'), 'https://host:8090');
  assert.equal(normalizeUrl('localhost:8090'), 'http://localhost:8090');
  assert.equal(normalizeUrl('127.0.0.1:8090'), 'http://127.0.0.1:8090');
  assert.equal(normalizeUrl('http://h:1/projects'), 'http://h:1');
  assert.throws(() => normalizeUrl(''));
});

test('isLoopbackHost classifies hosts like the browser client', () => {
  assert.equal(isLoopbackHost('localhost'), true);
  assert.equal(isLoopbackHost('127.0.0.1'), true);
  assert.equal(isLoopbackHost('::1'), true);
  assert.equal(isLoopbackHost('example.com'), false);
});

test('sharedPinFromProject marks shared + carries serverUrl/projectId', () => {
  const pin = sharedPinFromProject({ id: 'p_a_b', name: 'Shot', resource: 'http://pg', updatedAt: 9 }, 'http://srv:1');
  assert.equal(pin.shared, true);
  assert.equal(pin.serverUrl, 'http://srv:1');
  assert.equal(pin.projectId, 'p_a_b');
  assert.equal(pin.site, 'http://srv:1');
  assert.equal(pin.name, 'Shot');
  assert.match(pin.source, /\/projects\/p_a_b\/files\/original$/);
});

test('sharedPinFromProject carries the project color (defaulting to "")', () => {
  const tinted = sharedPinFromProject({ id: 'p_c', name: 'Tinted', color: '#12ab34', hasImage: true }, 'http://srv:1');
  assert.equal(tinted.color, '#12ab34');
  // No color on the project → "" (the popup falls back to the neutral muted grey).
  const plain = sharedPinFromProject({ id: 'p_d', name: 'Plain' }, 'http://srv:1');
  assert.equal(plain.color, '');
});

test('sharedPinsFromProjects keeps only image projects', () => {
  const pins = sharedPinsFromProjects([
    { id: 'p1', name: 'A', hasImage: true },
    { id: 'p2', name: 'B', hasImage: false },
  ], 'http://s');
  assert.equal(pins.length, 1);
  assert.equal(pins[0].projectId, 'p1');
});

test('mergePins tags local shared:false, appends deduped shared', () => {
  const local = [{ site: 'http://a', source: 'x', name: 'L' }];
  const shared = [
    { serverUrl: 'http://s', projectId: 'p1', name: 'S1', shared: true },
    { serverUrl: 'http://s', projectId: 'p1', name: 'dup', shared: true },
    { serverUrl: 'http://s', projectId: 'p2', name: 'S2', shared: true },
  ];
  const merged = mergePins(local, shared);
  assert.equal(merged.length, 3); // 1 local + 2 distinct shared
  assert.equal(merged[0].shared, false);
  assert.equal(merged[1].projectId, 'p1');
  assert.equal(merged[2].projectId, 'p2');
});

test('upsertConnection / dropConnection key on url', () => {
  let list = upsertConnection([], { url: 'http://a', token: 't1' });
  list = upsertConnection(list, { url: 'http://b', token: 't2' });
  list = upsertConnection(list, { url: 'http://a', token: 't1b' }); // replace, float to front
  assert.deepEqual(list.map((c) => c.url), ['http://a', 'http://b']);
  assert.equal(list[0].token, 't1b');
  list = dropConnection(list, 'http://a');
  assert.deepEqual(list.map((c) => c.url), ['http://b']);
});

test('connect issues a token when none supplied', async () => {
  const conn = await connect('srv:8090', '', mockFetch());
  assert.equal(conn.url, 'https://srv:8090');
  assert.equal(conn.token, 'tk');
});

// ── Invite links: `<url>#token=<value>` (browser connectionManager parity) ──
test('parseInviteUrl splits the #token= fragment off the URL', () => {
  assert.deepEqual(parseInviteUrl('http://localhost:8090#token=abc123'),
    { url: 'http://localhost:8090', token: 'abc123' });
  assert.deepEqual(parseInviteUrl('srv:8090#token=a%2Bb'), { url: 'srv:8090', token: 'a+b' });
  // No fragment, a non-token fragment, or an empty token → pass through untouched.
  assert.deepEqual(parseInviteUrl('http://h:1'), { url: 'http://h:1', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#other=x'), { url: 'http://h:1#other=x', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#token='), { url: 'http://h:1#token=', token: '' });
});

// A fetch that only accepts one session token — enough to prove which token connect used.
const tokenGatedFetch = (good) => async (url, init = {}) => {
  const auth = ((init.headers || {}).Authorization || '').replace('Bearer ', '');
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  if (new URL(url).pathname === '/auth/token') return json(401, { message: 'admin required' });
  if (auth !== good) return json(401, { message: 'bad token' });
  return json(200, { projects: [] });
};

test('connect with an invite link strips the fragment and adopts the token as credential', async () => {
  const conn = await connect('http://a:1#token=sess-tok', '', tokenGatedFetch('sess-tok'));
  assert.equal(conn.url, 'http://a:1', 'the fragment never reaches the url');
  assert.equal(conn.token, 'sess-tok');
  assert.equal(conn.credential, 'sess-tok', 'the fragment token feeds the credential flow');
});

test('an explicitly supplied token wins over the invite fragment', async () => {
  const conn = await connect('http://a:1#token=bogus', 'real-tok', tokenGatedFetch('real-tok'));
  assert.equal(conn.token, 'real-tok');
  assert.equal(conn.credential, 'real-tok');
});

test('listProjects + collectSharedPins aggregate across servers', async () => {
  const f = (url, init) => {
    const host = new URL(url).host;
    const map = {
      'a:1': mockFetch({ projects: [{ id: 'pa', name: 'A', hasImage: true }] }),
      'b:2': mockFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] }),
    };
    return map[host](url, init);
  };
  const conns = [{ url: 'http://a:1', token: 't' }, { url: 'http://b:2', token: 't' }];
  const got = await listProjects(conns[0], f);
  assert.equal(got[0].id, 'pa');
  const shared = await collectSharedPins(conns, f);
  assert.deepEqual(shared.map((p) => p.projectId).sort(), ['pa', 'pb']);
});

test('collectSharedPins survives an unreachable server', async () => {
  const f = (url, init) => {
    if (new URL(url).host === 'down:0') throw new Error('refused');
    return mockFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] })(url, init);
  };
  const shared = await collectSharedPins([{ url: 'http://down:0', token: 't' }, { url: 'http://b:2', token: 't' }], f);
  assert.equal(shared.length, 1);
  assert.equal(shared[0].projectId, 'pb');
});

test('addServer / removeServer persist to chrome.storage', async () => {
  const mock = installStorageMock();
  const f = mockFetch();
  await addServer('srv:8090', '', f);
  let stored = (await loadConnections());
  assert.equal(stored.length, 1);
  assert.equal(stored[0].url, 'https://srv:8090');
  assert.equal(mock.peek()[CONNECTIONS_KEY][0].token, 'tk');

  const after = await removeServer('https://srv:8090');
  assert.equal(after.length, 0);
  mock.reset();
});

test('createProject posts and returns the new record', async () => {
  const rec = await createProjectShim();
  assert.equal(rec.id, 'p_new_a');
  assert.equal(rec.name, 'Pinned');
});

// helper kept here to exercise createProject through the mock server
import { createProject } from '../src/lib/connections.js';
async function createProjectShim() {
  return createProject({ url: 'http://s', token: 't' }, { name: 'Pinned', source: 'http://img' }, mockFetch());
}

// ── pin-target selection (pure) ──

test('pinTargetMode maps connection count to a routing mode', () => {
  assert.equal(pinTargetMode([]), 'none');
  assert.equal(pinTargetMode([{ url: 'http://a' }]), 'one');
  assert.equal(pinTargetMode([{ url: 'http://a' }, { url: 'http://b' }]), 'many');
  assert.equal(pinTargetMode(null), 'none');
});

test('connectionByUrl finds the matching connection (or null)', () => {
  const conns = [{ url: 'http://a', token: 't1' }, { url: 'http://b', token: 't2' }];
  assert.equal(connectionByUrl(conns, 'http://b').token, 't2');
  assert.equal(connectionByUrl(conns, 'http://z'), null);
  assert.equal(connectionByUrl(null, 'http://a'), null);
});

test('projectRequestFromImage maps name/source/resource with fallbacks', () => {
  // src + page resource fallback
  assert.deepEqual(
    projectRequestFromImage({ name: 'Logo', src: 'http://img/logo.png' }, 'http://page'),
    { name: 'Logo', source: 'http://img/logo.png', resource: 'http://page' });
  // explicit source/resource on the record win over the fallbacks
  assert.deepEqual(
    projectRequestFromImage({ name: 'S', source: 'http://s', resource: 'http://r' }, 'http://page'),
    { name: 'S', source: 'http://s', resource: 'http://r' });
  // empty record → Untitled + empty provenance
  assert.deepEqual(projectRequestFromImage(), { name: 'Untitled', source: '', resource: '' });
});

test('fetchProjectImage defaults to the original file with Bearer auth', async () => {
  let seen = null;
  const f = async (url, init) => {
    seen = { url, headers: init.headers, method: init.method };
    return { ok: true, status: 200, blob: async () => 'IMG_BYTES' };
  };
  const blob = await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', 'original', f);
  assert.equal(blob, 'IMG_BYTES');
  assert.equal(seen.method, 'GET');
  assert.equal(seen.url, 'http://srv:1/projects/p_a/files/original');
  assert.equal(seen.headers.Authorization, 'Bearer tok');
});

test('fetchProjectImage with kind omitted still hits the original file', async () => {
  let seen = null;
  const f = async (url) => {
    seen = url;
    return { ok: true, status: 200, blob: async () => 'BYTES' };
  };
  await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', undefined, f);
  assert.equal(seen, 'http://srv:1/projects/p_a/files/original');
});

test('fetchProjectImage can request the edited result variant', async () => {
  let seen = null;
  const f = async (url) => {
    seen = url;
    return { ok: true, status: 200, blob: async () => 'RESULT_BYTES' };
  };
  const blob = await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', 'result', f);
  assert.equal(blob, 'RESULT_BYTES');
  assert.equal(seen, 'http://srv:1/projects/p_a/files/result');
});

test('a stale session token self-heals: re-mint with the stored credential, retry once', async () => {
  // Server restarted: 'dead' is rejected; the credential 'adm' can mint 'fresh'.
  const calls = [];
  const f = async (url, init) => {
    const auth = (init.headers.Authorization || '').replace('Bearer ', '');
    calls.push(`${init.method} ${new URL(url).pathname} [${auth}]`);
    if (url.endsWith('/auth/token')) {
      if (auth !== 'adm') return { ok: false, status: 401, json: async () => ({ message: 'admin required' }) };
      return { ok: true, status: 200, json: async () => ({ token: 'fresh' }) };
    }
    if (auth === 'fresh') return { ok: true, status: 200, json: async () => ({ projects: [] }) };
    return { ok: false, status: 401, json: async () => ({ message: 'bad token' }) };
  };
  const conn = { url: 'http://srv:1', token: 'dead', credential: 'adm' };
  const out = await listProjects(conn, f);
  assert.deepEqual(out, []);
  assert.equal(conn.token, 'fresh');   // healed in place
  assert.deepEqual(calls, [
    'GET /projects [dead]',
    'POST /auth/token [adm]',
    'GET /projects [fresh]',
  ]);
});

// ── credentialKind: which connections hold an ADMIN credential ───────────────
// A server whose admin token can MINT session tokens but cannot list projects
// itself — the shape that proves the credential is an admin one.
const adminGatedFetch = (adminTok, sessionTok = 'sess') => async (url, init = {}) => {
  const auth = ((init.headers || {}).Authorization || '').replace('Bearer ', '');
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  if (new URL(url).pathname === '/auth/token')
    return auth === adminTok ? json(200, { token: sessionTok }) : json(401, { message: 'admin required' });
  return auth === sessionTok ? json(200, { projects: [] }) : json(401, { message: 'bad token' });
};

test('connect records credentialKind admin when the mint-then-validate path proves it', async () => {
  const conn = await connect('http://a:1', 'adm', adminGatedFetch('adm'));
  assert.equal(conn.credentialKind, 'admin');
  assert.equal(conn.credential, 'adm');
  assert.equal(conn.token, 'sess', 'the row runs on the minted session token');
});

test('connect leaves credentialKind empty for a session token and for anonymous', async () => {
  // A token that lists projects straight away is an ordinary session token.
  const session = await connect('http://a:1', 'sess-tok', tokenGatedFetch('sess-tok'));
  assert.equal(session.credentialKind, '');
  // No credential at all: the server issued the token itself.
  const anon = await connect('srv:8090', '', mockFetch());
  assert.equal(anon.credentialKind, '');
  assert.equal(anon.credential, '');
});

test('an admin credential proved mid-session is recorded on the connection', async () => {
  // Server restarted: 'dead' is refused, the stored credential re-mints and the retry works.
  const conn = { url: 'http://srv:1', token: 'dead', credential: 'adm' };
  await listProjects(conn, adminGatedFetch('adm', 'fresh'));
  assert.equal(conn.token, 'fresh');
  assert.equal(conn.credentialKind, 'admin');
});

test('upsertConnection persists credentialKind, defaulting to ""', () => {
  const list = upsertConnection([], { url: 'http://a', token: 't', credentialKind: 'admin' });
  assert.equal(list[0].credentialKind, 'admin');
  assert.equal(upsertConnection([], { url: 'http://b', token: 't' })[0].credentialKind, '');
  // Anything but the exact 'admin' marker is not admin.
  assert.equal(upsertConnection([], { url: 'http://c', token: 't', credentialKind: 'ADMIN' })[0].credentialKind, '');
});

test('addServer persists credentialKind and it survives the re-read', async () => {
  const mock = installStorageMock();
  await addServer('http://a:1', 'adm', adminGatedFetch('adm'));
  await addServer('http://b:2', 'sess-tok', tokenGatedFetch('sess-tok'));
  const stored = await loadConnections();
  assert.deepEqual(stored.map((c) => [c.url, c.credentialKind]),
    [['http://b:2', ''], ['http://a:1', 'admin']]);
  assert.equal(mock.peek()[CONNECTIONS_KEY][1].credentialKind, 'admin');
  mock.reset();
});

test('reconnectServer keeps a known admin kind: only the session token is stored', async () => {
  const mock = installStorageMock();
  await addServer('http://a:1', 'adm', adminGatedFetch('adm'));
  assert.equal((await loadConnections())[0].credentialKind, 'admin');
  // Re-validating the SESSION token can never re-prove the admin credential behind it.
  const after = await reconnectServer('http://a:1', adminGatedFetch('adm'));
  assert.equal(after[0].credentialKind, 'admin');
  assert.equal((await loadConnections())[0].credentialKind, 'admin');
  mock.reset();
});

test('isAdminConnection tolerates rows saved before the field existed', () => {
  assert.equal(isAdminConnection({ url: 'http://a', token: 't' }), false);
  assert.equal(isAdminConnection({ url: 'http://a', credentialKind: '' }), false);
  assert.equal(isAdminConnection({ url: 'http://a', credentialKind: 'admin' }), true);
  assert.equal(isAdminConnection(null), false);
});

test('filterConnections splits the list three ways', () => {
  const list = [
    { url: 'http://a', credentialKind: 'admin' },
    { url: 'http://b', credentialKind: '' },
    { url: 'http://c' },                          // legacy row: no field
  ];
  assert.deepEqual(filterConnections(list, 'all').map((c) => c.url), ['http://a', 'http://b', 'http://c']);
  assert.deepEqual(filterConnections(list, 'admin').map((c) => c.url), ['http://a']);
  assert.deepEqual(filterConnections(list, 'other').map((c) => c.url), ['http://b', 'http://c']);
  // Default + junk inputs behave like 'all' / an empty list.
  assert.equal(filterConnections(list).length, 3);
  assert.equal(filterConnections(list, 'nonsense').length, 3);
  assert.deepEqual(filterConnections(null, 'admin'), []);
});

// ── The options page wires the kind cue + filter ──
test('options page: golden admin outline and the three-way kind filter', () => {
  const html = readFileSync(new URL('../src/options/options.html', import.meta.url), 'utf8');
  const js = readFileSync(new URL('../src/options/options.js', import.meta.url), 'utf8');
  // Gold outline, the same #f5c518 cue a server-backed pin wears.
  assert.match(html, /\.pin-row\.conn-admin\s*\{[^}]*#f5c518/);
  assert.match(js, /conn-admin/);
  assert.match(js, /can mint session tokens/, 'the row explains what admin means');
  // Three .chk accent pills, one choice (radios), All checked by default.
  for (const v of ['all', 'admin', 'other'])
    assert.match(html, new RegExp(`<input type="radio" name="conn-kind" value="${v}"`));
  assert.match(html, /value="all" checked/);
  assert.match(js, /filterConnections\(all, connKind\(\)\)/, 'the render filters the list');
});

// ── The connection row is styled as a sibling of the pinned-image rows ───────
// Source-level pins: the row markup and the CSS it leans on live in two files with no
// DOM to assert against under `node --test`, so the contract is checked as text.
const optionsHtml = () => readFileSync(new URL('../src/options/options.html', import.meta.url), 'utf8');
// Just the connections half of options.js — the pins renderer above it has its own buttons.
const connectionsJs = () => {
  const js = readFileSync(new URL('../src/options/options.js', import.meta.url), 'utf8');
  return js.slice(js.indexOf('── Server connections'));
};

test('options page: removing a connection uses the destructive trash glyph', () => {
  const js = connectionsJs();
  assert.match(js, /remove\.innerHTML = icon\('trash'/, 'a delete uses the trash icon, not an x');
  assert.doesNotMatch(js, /remove\.innerHTML = icon\('x'/);
  assert.match(js, /remove\.className = 'pin-btn danger'/, 'it keeps the danger colour');
  assert.match(js, /remove\.title = 'Remove connection/);
});

test('options page: row action buttons read as enabled at rest, disabled only when disabled', () => {
  const html = optionsHtml();
  // Resting face + border are accent-tinted (not the flat panel/line that read as greyed).
  assert.match(html, /\.pin-btn \{[^}]*background:color-mix\(in srgb, var\(--btn-ink\) \d+%, var\(--panel\)\)/);
  assert.match(html, /\.pin-btn \{[^}]*color:var\(--btn-ink\)/);
  assert.match(html, /\.pin-btn \{[^}]*border:1px solid color-mix\(in srgb, var\(--btn-ink\)/);
  // Dark borrows the lighter accent shade, like the .chk pills in lib/theme.css.
  assert.match(html, /:root\[data-theme="dark"\] \{ --btn-ink:var\(--accent-2\)/);
  // Hover is the enhancement (full accent fill) and never fires on a disabled button.
  assert.match(html, /\.pin-btn:hover:not\(:disabled\) \{[^}]*background:var\(--accent\)/);
  assert.match(html, /\.pin-btn:disabled \{[^}]*opacity:\.45[^}]*color:var\(--muted\)/);
});

test('options page: the connection row is a .pin-row sibling with its own padding', () => {
  assert.match(connectionsJs(), /li\.className = 'pin-row conn-row'/);
  const html = optionsHtml();
  assert.match(html, /\.pin-row\.conn-row \{[^}]*padding:/, 'the thumb-less row sets its own padding');
  assert.match(html, /\.pin-row:hover \{[^}]*border-color:/, 'rows share one hover cue');
});

test('options page: the kind pills are compact and the connect row wraps as a unit', () => {
  const html = optionsHtml();
  assert.match(html, /<div class="row conn-bar">/);
  assert.match(html, /\.row\.conn-bar \{[^}]*flex-wrap:wrap/, 'the pills drop to their own line, not onto the buttons');
  // Popup format-pill sizing (popup.css .formats .chk), right-aligned on the row.
  assert.match(html, /\.conn-filters \.chk \{[^}]*font-size:11px/);
  assert.match(html, /\.conn-filters \{[^}]*margin-left:auto/);
});

// ── The kind filter animates the list both ways ──────────────────────────────
// The options list is rebuilt wholesale whenever the All / Admin / Non-admin pill
// changes, so what the user sees leave and arrive is decided by the keys of the two
// renders — connections are keyed by their url (the data-url the row already carries).
// The transition's own mechanics are pinned in tests/motion.test.js.

const CONNS = [
  { url: 'http://a', credentialKind: 'admin' },
  { url: 'http://b', credentialKind: '' },
  { url: 'http://c' },
];
const urlsFor = (mode) => filterConnections(CONNS, mode).map((c) => c.url);

test('switching the kind filter drops exactly the excluded rows, and brings them back', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, keyAttr: 'url', reduced: () => false });

  renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  const toAdmin = renderKeys(list, tr, urlsFor('admin'), { attr: 'url' });
  assert.deepEqual(toAdmin, { entered: [], left: ['http://b', 'http://c'] });
  assert.equal(tr.ghostCount, 2, 'the non-admin rows play out instead of blinking away');

  const backToAll = renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  assert.deepEqual(backToAll, { entered: ['http://b', 'http://c'], left: [] },
    're-admitted rows ENTER — the filter reads the same in both directions');
  assert.equal(tr.ghostCount, 0, 'the interrupted exits were dropped, not left under the new rows');
  assert.deepEqual(list.keys('url'), ['http://a', 'http://b', 'http://c']);
});

test('reduced motion: the kind filter still lands on exactly the right rows', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, keyAttr: 'url', reduced: () => true });
  renderKeys(list, tr, urlsFor('all'), { attr: 'url' });
  renderKeys(list, tr, urlsFor('other'), { attr: 'url' });
  assert.deepEqual(list.keys('url'), ['http://b', 'http://c']);
  assert.equal(tr.ghostCount, 0);
});

test('options page: the two lists animate filter changes, and a delete still scatters', () => {
  const js = readFileSync(new URL('../src/options/options.js', import.meta.url), 'utf8');
  // Both lists are wrapped: snapshot before the wipe, play after the rebuild.
  assert.match(js, /createFilterTransition\(\{ list: pinListEl \}\)/);
  assert.match(js, /createFilterTransition\(\{ list: connListEl, keyAttr: 'url' \}\)/);
  assert.match(js, /pinTransition\.begin\(\);\s*\n\s*pinListEl\.innerHTML = '';/);
  assert.match(js, /connTransition\.begin\(\);\s*\n\s*connListEl\.innerHTML = '';/);
  // A DELETE keeps the heavier effect and leaves the DOM, so it never also fades
  // out as if a filter had merely excluded it.
  assert.match(js, /leaveThenRemove\(li, \(\) => li\.remove\(\), scatterGridFor\(1\)\)/);
  // A freshly added connection is materialized by the add flow, not ramped in twice.
  assert.match(js, /connTransition\.end\(\{ skipEnter: materializingUrl/);
});
