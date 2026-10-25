// A deploy's stamped sw.js (tools/swStamp.js): the whole build precached in one cache and served from
// it, a newer build waiting until the page asks, one deploy's cache at a time; and the stamp itself,
// every file a page of this build can load, hashed into its VERSION.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import vm from 'node:vm';
import { moduleGraph } from '../tools/graphManifest.js';
import { stampOf, stamped, htmlWithWasm, WASM_START, WASM_END } from '../tools/swStamp.js';

const SRC = readFileSync(new URL('../sw.js', import.meta.url), 'utf8');
const ORIGIN = 'https://app.test';
const PRECACHE = ['./', 'index.html', 'js/index.js', 'js/wasm/stencilCore.wasm'];

const boot = ({ missing = [] } = {}) => {
  const handlers = {};
  const caches = new Map();
  const cacheOf = (name) => {
    if (!caches.has(name)) {
      const store = new Map();
      const keyOf = (k) => (typeof k === 'string' ? k : k.url);
      caches.set(name, { store, put: async (k, r) => { store.set(keyOf(k), r); }, match: async (k) => store.get(keyOf(k)),
        keys: async () => [...store.keys()].map((url) => ({ url })), delete: async (k) => store.delete(String(k)) });
    }
    return caches.get(name);
  };
  const fetched = [];
  const state = { skipped: 0 };
  const ctx = {
    self: { addEventListener: (t, fn) => { handlers[t] = fn; }, location: { origin: ORIGIN, href: `${ORIGIN}/sw.js` },
      skipWaiting: () => { state.skipped++; }, clients: { claim: () => {} } },
    caches: { open: async (n) => cacheOf(n), keys: async () => [...caches.keys()], delete: async (n) => caches.delete(n) },
    URL, Promise, Set, Error, Map, setTimeout, clearTimeout,
    Request: class { constructor(url, init) { this.url = new URL(url, `${ORIGIN}/`).href; this.cache = init?.cache; } },
    fetch: async (req) => {
      fetched.push({ url: req.url, cache: req.cache });
      if (missing.some((m) => req.url.endsWith(m))) return { ok: false, status: 404 };
      return { ok: true, type: 'basic', body: req.url, clone() { return this; } };
    },
  };
  vm.runInNewContext(stamped(SRC, { version: 'v1', precache: PRECACHE }), ctx);
  const run = async (type, extra = {}) => {
    let done;
    handlers[type]({ waitUntil: (p) => { done = p; }, ...extra });
    return done;
  };
  const get = async (path, mode = 'cors') => {
    let out;
    handlers.fetch({ request: { method: 'GET', url: `${ORIGIN}${path}`, mode }, respondWith: (p) => { out = p; }, waitUntil: () => {} });
    return out;
  };
  return { caches, cacheOf, fetched, state, run, get, handlers };
};

test('a stamped install fetches the whole build past the HTTP cache into its own cache, and waits', async () => {
  const sw = boot();
  await sw.run('install');
  assert.deepEqual([...sw.cacheOf('stencil-v1').store.keys()].sort(),
    PRECACHE.map((u) => new URL(u, `${ORIGIN}/`).href).sort());
  assert.ok(sw.fetched.every((f) => f.cache === 'reload'));
  assert.equal(sw.state.skipped, 0, 'a page on the old build never gets a module of this one');
  sw.handlers.message({ data: { type: 'SKIP_WAITING' } });
  assert.equal(sw.state.skipped, 1, 'until the page asks');
});

test('a missing wasm is tolerated; any other missing file fails the install, so the old deploy stays', async () => {
  await boot({ missing: ['stencilCore.wasm'] }).run('install');
  await assert.rejects(boot({ missing: ['js/index.js'] }).run('install'), /precache: js\/index\.js failed/);
});

test('activating keeps this deploy\'s cache and the runtime one, and drops every other deploy', async () => {
  const sw = boot();
  for (const name of ['stencil-v0', 'stencil-v1', 'stencil-v2', 'other']) sw.cacheOf(name);
  await sw.run('activate');
  assert.deepEqual([...sw.caches.keys()].sort(), ['stencil-v1', 'stencil-v2']);
});

test('a build member is served from the deploy cache, a page by its path; anything else goes to the network', async () => {
  const sw = boot();
  await sw.run('install');
  sw.fetched.length = 0;
  assert.equal((await sw.get('/js/index.js')).body, `${ORIGIN}/js/index.js`);
  assert.equal((await sw.get('/?open=p1', 'navigate')).body, `${ORIGIN}/`);
  assert.deepEqual(sw.fetched, [], 'cache-first: no request for a precached file');
  await sw.get('/js/elsewhere.js');
  assert.deepEqual(sw.fetched.map((f) => f.url), [`${ORIGIN}/js/elsewhere.js`]);
});

test('the stamp lists every module a page can load, and any change to one moves VERSION', () => {
  const stamp = stampOf();
  const listed = new Set(stamp.precache);
  for (const m of [...moduleGraph().modules, 'js/worker/imageWorker.js', 'js/worker/projectsWorker.js', 'js/core/scriptHandles.js',
    'index.html', 'css/theme.css', 'js/prePaintTheme.js', '../common/icons/favicon.svg']) assert.ok(listed.has(m), m);
  assert.match(stamp.version, /^[0-9a-f]{16}$/);
  const root = mkdtempSync(join(tmpdir(), 'sw-stamp-'));
  try {
    mkdirSync(join(root, 'js'));
    writeFileSync(join(root, 'index.html'), '<link rel="stylesheet" href="a.css">');
    writeFileSync(join(root, 'manifest.webmanifest'), '{"icons":[]}');
    writeFileSync(join(root, 'a.css'), 'body{}');
    writeFileSync(join(root, 'js/index.js'), "import './b.js';\nconst w = new URL('./w.js', import.meta.url);\n");
    writeFileSync(join(root, 'js/b.js'), 'export {};');
    writeFileSync(join(root, 'js/w.js'), 'export {};');
    const first = stampOf(root);
    assert.deepEqual(first.precache, ['./', 'index.html', 'manifest.webmanifest', 'a.css', 'js/index.js', 'js/b.js', 'js/w.js']);
    writeFileSync(join(root, 'js/w.js'), 'export const x = 1;');
    assert.notEqual(stampOf(root).version, first.version);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('the wasm core\'s preload hints land in a deploy\'s page only when the core is built', () => {
  const tracked = readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  const block = (html) => html.slice(html.indexOf(WASM_START), html.indexOf(WASM_END));
  assert.doesNotMatch(block(tracked), /<link/, 'the tracked page preloads no wasm, built or not');
  assert.ok(tracked.indexOf(WASM_START) < tracked.indexOf('modulepreload:start'), 'the core starts before the graph');
  const root = mkdtempSync(join(tmpdir(), 'sw-wasm-'));
  try {
    assert.equal(htmlWithWasm(tracked, root), tracked, 'no wasm: the page is left alone');
    mkdirSync(join(root, 'js/wasm'), { recursive: true });
    writeFileSync(join(root, 'js/wasm/stencilCore.js'), '');
    writeFileSync(join(root, 'js/wasm/stencilCore.wasm'), '');
    const once = htmlWithWasm(tracked, root);
    assert.deepEqual([...block(once).matchAll(/<link rel="(\w+)" href="([^"]+)"/g)].map((m) => `${m[1]} ${m[2]}`), [
      'modulepreload js/wasm/stencilCore.js', 'modulepreload js/core/scriptHandles.js', 'preload js/wasm/stencilCore.wasm']);
    assert.match(block(once), /as="fetch" type="application\/wasm" crossorigin>/, 'the fetch the glue makes, same-origin');
    assert.equal(htmlWithWasm(once, root), once, 'stamping twice changes nothing');
    assert.equal(once.replace(block(once), ''), tracked.replace(block(tracked), ''), 'nothing outside the block moves');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
