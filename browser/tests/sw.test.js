// browser/sw.js run in a vm against a stub Cache Storage: network-first with the cache as the offline
// fallback, a page cached once per path whatever its query, and the runtime cache bounded at
// MAX_ENTRIES with the least recently fetched entries going first and the shell never pruned.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const SRC = readFileSync(new URL('../sw.js', import.meta.url), 'utf8');
const ORIGIN = 'https://app.test';
const MAX = Number(/const MAX_ENTRIES = (\d+);/.exec(SRC)[1]);
const urlOf = (k) => (typeof k === 'string' ? k : k.url);

// Insertion-ordered like the real Cache: a put removes the old entry and appends the new one.
const stubCaches = () => {
  const store = new Map();
  const cache = {
    put: async (k, res) => { store.delete(urlOf(k)); store.set(urlOf(k), res); },
    match: async (k) => store.get(urlOf(k)),
    keys: async () => [...store.keys()].map((url) => ({ url })),
    delete: async (k) => store.delete(urlOf(k)),
    add: async (u) => { store.set(new URL(u, `${ORIGIN}/`).href, { body: u }); },
  };
  return { store, caches: { open: async () => cache, keys: async () => ['stencil-v2'], delete: async () => true } };
};

const boot = () => {
  const handlers = {};
  const { store, caches } = stubCaches();
  const net = { down: false, served: [] };
  const ctx = {
    self: { addEventListener: (t, fn) => { handlers[t] = fn; }, location: { origin: ORIGIN, href: `${ORIGIN}/` },
      skipWaiting: () => {}, clients: { claim: () => {} } },
    caches, URL, Promise, Set, Error,
    fetch: async (req) => {
      if (net.down) throw new TypeError('offline');
      net.served.push(req.url);
      return { ok: true, type: 'basic', body: req.url, clone() { return this; } };
    },
  };
  vm.runInNewContext(SRC, ctx);
  const get = async (path, mode = 'cors') => {
    let out;
    handlers.fetch({ request: { method: 'GET', url: `${ORIGIN}${path}`, mode }, respondWith: (p) => { out = p; } });
    const res = await out;
    for (let i = 0; i < 4; i++) await new Promise((r) => setImmediate(r));
    return res;
  };
  const install = async () => {
    let done;
    handlers.install({ waitUntil: (p) => { done = p; } });
    await done;
  };
  return { store, net, get, install };
};

test('network first, and the cached copy offline', async () => {
  const sw = boot();
  assert.equal((await sw.get('/js/a.js')).body, `${ORIGIN}/js/a.js`);
  sw.net.down = true;
  assert.equal((await sw.get('/js/a.js')).body, `${ORIGIN}/js/a.js`);
  await assert.rejects(sw.get('/js/never.js'), /offline and not cached/);
});

test('a page is one entry per path: every ?open= link reuses it, online and offline', async () => {
  const sw = boot();
  await sw.get('/index.html?open=p1', 'navigate');
  await sw.get('/index.html?open=p2', 'navigate');
  assert.deepEqual([...sw.store.keys()], [`${ORIGIN}/index.html`]);
  sw.net.down = true;
  assert.ok(await sw.get('/index.html?open=p3', 'navigate'), 'a link never opened before still opens offline');
  assert.equal(sw.store.size, 1);
});

test('a module keeps one entry per URL: a query is its own response', async () => {
  const sw = boot();
  await sw.get('/projects?cursor=a');
  await sw.get('/projects?cursor=b');
  assert.equal(sw.store.size, 2);
});

test('the cache stops at MAX_ENTRIES: the least recently fetched goes first, the shell never', async () => {
  const sw = boot();
  await sw.install();
  const shell = sw.store.size;
  assert.ok(shell > 50 && MAX >= 2 * 620, `the cap (${MAX}) clears the app's own files`);
  for (let i = 0; i < MAX - shell; i++) await sw.get(`/js/m${i}.js`);
  await sw.get('/js/m0.js');
  await sw.get('/js/extra.js');
  assert.equal(sw.store.size, MAX);
  assert.ok(!sw.store.has(`${ORIGIN}/js/m1.js`), 'the oldest runtime entry left');
  assert.ok(sw.store.has(`${ORIGIN}/js/m0.js`), 'a re-fetched entry moved to the back');
  assert.ok(sw.store.has(`${ORIGIN}/index.html`) && sw.store.has(`${ORIGIN}/js/index.js`), 'the shell stays');
});
