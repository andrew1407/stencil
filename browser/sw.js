// ── Service worker: offline app shell + runtime cache ───────────
// No build step / generated manifest. Precaches the critical shell on install, then
// serves every same-origin GET stale-while-revalidate (cache answers instantly,
// background fetch refreshes for next time). Bump VERSION to force a clean
// re-precache and evict the old cache on activate.
const VERSION = 'v3';
const CACHE = `stencil-${VERSION}`;

// Critical shell: enough to boot the app offline. The rest of the module graph
// (ui/, core/, config/, the optional wasm) is filled in at runtime on first use.
const SHELL = [
  './',
  './index.html',
  './manifest.webmanifest',
  './favicon.svg',
  './icon-maskable.svg',
  './css/theme.css',
  './css/layout.css',
  './css/components.css',
  './css/animations.css',
  './js/index.js',
];

self.addEventListener('install', e => {
  e.waitUntil(
    caches.open(CACHE)
      // Tolerate a missing optional asset rather than failing the whole install.
      .then(c => Promise.all(SHELL.map(url => c.add(url).catch(() => {}))))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;                 // never cache mutations
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;  // leave cross-origin to the network

  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const cached = await cache.match(req);
    const network = fetch(req)
      .then(res => {
        // Only store complete, same-origin responses (skip opaque/partial/errors).
        if (res && res.ok && res.type === 'basic') cache.put(req, res.clone());
        return res;
      })
      .catch(() => cached);                          // offline → fall back to cache
    return cached || network;                        // cache-first, refresh behind
  })());
});
