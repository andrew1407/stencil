// ── Service worker: offline app shell + runtime cache ───────────
// Unstamped (the tracked file, every dev server) it is network-first: the live file always wins and
// the cache is the offline fallback. A deploy stamps VERSION + PRECACHE (tools/swStamp.js): that
// build is then served cache-first from one cache, and a newer one waits until the page asks.
// stamp:start
const VERSION = '';
const PRECACHE = [];
// stamp:end
const CACHE = 'stencil-v2';
const DEPLOY = `stencil-${VERSION}`;
// Runtime entries kept: over twice the app's own files (about 620), so the offline app is never
// trimmed; past it the entry fetched longest ago goes first, and the shell is never pruned.
const MAX_ENTRIES = 1500;

// Critical shell: enough to boot the app offline. The rest of the module graph
// (ui/, core/, config/, the optional wasm) is filled in at runtime on first use.
const SHELL = [
  './',
  './index.html',
  './manifest.webmanifest',
  '../common/icons/favicon.svg',
  '../common/icons/icon-maskable.svg',
  './css/theme.css',
  './css/layout/scrollbars.css',
  './css/layout/shell.css',
  './css/layout/icons.css',
  './css/layout/shimmer.css',
  './css/layout/buttonStates.css',
  './css/layout/canvas/frame.css',
  './css/layout/zoomControls.css',
  './css/layout/selectionPanel.css',
  './css/layout/canvas/cursor.css',
  './css/layout/infoLine.css',
  './css/layout/controlRows.css',
  './css/layout/topbar.css',
  './css/layout/coord/panel.css',
  './css/layout/coord/rows.css',
  './css/components/controls.css',
  './css/components/fullscreen.css',
  './css/components/contextMenu/contextMenu.css',
  './css/components/ctx/assistant.css',
  './css/components/notifications.css',
  './css/components/logoStage.css',
  './css/components/settings/settings.css',
  './css/components/chat/layering.css',
  './css/components/modalShell.css',
  './css/components/openImage.css',
  './css/components/accentPicker.css',
  './css/components/blankImage.css',
  './css/components/hosts.css',
  './css/components/projects/projects.css',
  './css/components/connect/connections.css',
  './css/components/incognito.css',
  './css/components/tooltip.css',
  './css/components/projects/expiration.css',
  './css/components/chat/panel.css',
  './css/components/chat/tails.css',
  './css/components/chat/narrow.css',
  './css/components/chat/touch.css',
  './css/animations/keyframes.css',
  './css/animations/controls.css',
  './css/animations/icon/hover.css',
  './css/animations/voice.css',
  './css/animations/overlay/overlays.css',
  './css/animations/responsive.css',
  './css/animations/reducedMotion.css',
  './css/animations/themeSwap.css',
  './css/animations/collapse.css',
  './css/animations/reveal/reveal.css',
  './css/animations/dust.css',
  './css/animations/motionModes.css',
  './css/webcore/tokens.css',
  './css/webcore/chrome.css',
  './css/webcore/fields.css',
  './css/webcore/canvas.css',
  './css/webcore/tables.css',
  './css/webcore/windows.css',
  './css/webcore/menus.css',
  './css/webcore/notices.css',
  './css/webcore/chat.css',
  './css/webcore/icons.css',
  './js/index.js',
];

const shellUrls = () => new Set(SHELL.map(u => new URL(u, self.location.href).href));

// A put moves its key to the end of cache.keys(), so the front is the least recently fetched.
const prune = async (cache) => {
  const keys = await cache.keys();
  if (keys.length <= MAX_ENTRIES) return;
  const shell = shellUrls();
  const old = keys.filter(k => !shell.has(k.url)).slice(0, keys.length - MAX_ENTRIES);
  await Promise.all(old.map(k => cache.delete(k)));
};

// One trailing prune per burst: keys() is O(n), so one per put made a cold boot O(n²). A stream
// that never pauses still prunes every PRUNE_EVERY puts.
const PRUNE_DELAY_MS = 1000;
const PRUNE_EVERY = 200;
let pruneTimer = null;
let puts = 0;
let settle = null;
let burst = null;
const schedulePrune = (cache) => {
  clearTimeout(pruneTimer);
  if (!burst) burst = new Promise((resolve) => { settle = resolve; });
  const done = burst;
  const run = () => {
    const resolve = settle;
    burst = null;
    puts = 0;
    prune(cache).catch(() => {}).then(resolve);
  };
  if (++puts >= PRUNE_EVERY) run();
  else pruneTimer = setTimeout(run, PRUNE_DELAY_MS);
  return done;
};

// The wasm pair is the one optional member: the JS fallback runs without it.
const OPTIONAL = /\/wasm\/stencilCore\.(js|wasm)$/;
const PRECACHED = new Set(PRECACHE.map(u => new URL(u, self.location.href).href));

// A stamped build is fetched whole, bypassing the HTTP cache; any required file missing fails the
// install, so the deploy already serving stays in charge. It then WAITS: a page running the old
// build never gets a module of the new one; the page's update prompt sends SKIP_WAITING.
const precache = async () => {
  const cache = await caches.open(DEPLOY);
  await Promise.all(PRECACHE.map(async (u) => {
    const res = await fetch(new Request(u, { cache: 'reload' })).catch(() => null);
    if (res && res.ok) return cache.put(new URL(u, self.location.href).href, res);
    if (!OPTIONAL.test(u)) throw new Error(`precache: ${u} failed`);
  }));
};

self.addEventListener('install', e => {
  e.waitUntil(VERSION ? precache() : caches.open(CACHE)
    // Tolerate a missing optional asset rather than failing the whole install.
    .then(c => Promise.all(SHELL.map(url => c.add(url).catch(() => {}))))
    .then(() => self.skipWaiting()));
});

self.addEventListener('message', (e) => {
  if (e.data?.type === 'SKIP_WAITING') self.skipWaiting();
});

// One deploy, one cache: the deploy's own and the runtime cache stay, every other goes.
self.addEventListener('activate', e => {
  const keep = VERSION ? [DEPLOY, CACHE] : [CACHE];
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => !keep.includes(k)).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;                 // never cache mutations
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;  // leave cross-origin to the network

  // A page is the same file whatever its query (`?open=<id>`): one entry per path, not per link.
  const key = req.mode === 'navigate' ? url.origin + url.pathname : req;
  if (VERSION && PRECACHED.has(req.mode === 'navigate' ? key : url.href)) {
    e.respondWith(caches.open(DEPLOY).then(c => c.match(key)).then(hit => hit || fetch(req)));
    return;
  }
  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    try {
      const res = await fetch(req);                  // network-first: always try live
      // Cache complete, same-origin responses for offline (skip opaque/partial/errors).
      if (res && res.ok && res.type === 'basic') {
        e.waitUntil(cache.put(key, res.clone()).then(() => schedulePrune(cache)).catch(() => {}));
      }
      return res;
    } catch {
      const cached = await cache.match(key);          // offline → fall back to cache
      if (cached) return cached;
      throw new Error('offline and not cached');
    }
  })());
});
