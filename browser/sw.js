// ── Service worker: offline app shell + runtime cache ───────────
// Network-first on every same-origin GET, so the live file always wins and the cache
// is only the offline fallback — no version bumping needed for freshness. The name is
// fixed; activate still evicts any other (legacy) cache so old versions clean up.
const CACHE = 'stencil-v2';
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

  // A page is the same file whatever its query (`?open=<id>`): one entry per path, not per link.
  const key = req.mode === 'navigate' ? url.origin + url.pathname : req;
  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    try {
      const res = await fetch(req);                  // network-first: always try live
      // Cache complete, same-origin responses for offline (skip opaque/partial/errors).
      if (res && res.ok && res.type === 'basic') cache.put(key, res.clone()).then(() => prune(cache)).catch(() => {});
      return res;
    } catch {
      const cached = await cache.match(key);          // offline → fall back to cache
      if (cached) return cached;
      throw new Error('offline and not cached');
    }
  })());
});
