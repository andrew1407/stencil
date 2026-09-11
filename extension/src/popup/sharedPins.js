import { blobToDataUrl } from '../lib/stencil.js';
import { loadConnections, collectSharedPins, connectionByUrl, fetchProjectImage } from '../lib/connections.js';
import { hostLabel } from '../lib/imageModel.js';
import { pollClock } from '../lib/pollClock.js';
import { state } from './model.js';
import { previewCache } from './preview.js';
import { applyFilters } from './filters.js';

// ── Shared pins (connected collaboration servers) ───────────────────────────
// A server project renders alongside the page's own images with a golden outline; its
// thumbnail / editor hand-off are fetched over the server's Bearer-authed endpoint.
// Map a shared-pin record (connections.js) to the popup's image shape. No `src` (the
// download is authed — a bare <img> can't load it); the bytes come via fetchProjectImage,
// and measured:true keeps the size observer off a row with no probe-able URL.
export const sharedToImage = (pin) => ({
  kind: 'img',
  src: '',
  name: pin.name,
  w: 0,
  h: 0,
  measured: true,
  shared: true,
  serverUrl: pin.serverUrl,
  projectId: pin.projectId,
  source: pin.source,
  resource: pin.resource || '',
  // Project's custom accent colour ("#rrggbb", or "" = default) for painting the row name.
  color: pin.color || '',
  opened: [],
  pinned: false,
});

// The connection that owns a shared row (by its server origin), or null.
const sharedConn = (image) => connectionByUrl(state.connections, image.serverUrl);

// Authed bytes per shared row, keyed server+project+kind. The entry is the PROMISE, so a
// re-render or a poll refresh joins the fetch in flight instead of opening a second authed
// round-trip; a rejection is dropped so the next render retries.
const sharedBytes = new Map();
const sharedRowKey = (image) => `${image.serverUrl}\n${image.projectId}`;
const sharedFetch = (image, kind, run) => {
  const key = `${sharedRowKey(image)}\n${kind}`;
  if (!sharedBytes.has(key)) sharedBytes.set(key, run().catch((err) => { sharedBytes.delete(key); throw err; }));
  return sharedBytes.get(key);
};

const sharedImageBlob = (image, kind) => {
  const conn = sharedConn(image);
  if (!conn) return Promise.reject(new Error('no connection for shared pin'));
  return fetchProjectImage(conn, image.projectId, kind);
};

// Thumbnail bytes (authed) as a data URL: the edited `result`, falling back to the
// `original` — unlike the hand-off below, which needs the untouched original.
export const sharedThumbUrl = (image) => sharedFetch(image, 'thumb', async () => {
  const blob = await sharedImageBlob(image, 'result').catch(() => sharedImageBlob(image, 'original'));
  return blobToDataUrl(blob);
});

// Original (unedited) bytes (authed) for the editor / crop hand-off, so the editor re-opens
// the raw image and re-applies the saved filter + lines. Seeded into the preview cache too.
export const sharedDataUrl = (image) => sharedFetch(image, 'original', async () => {
  const dataUrl = await blobToDataUrl(await sharedImageBlob(image, 'original'));
  if (image.source) previewCache.set(image.source, dataUrl);
  return dataUrl;
});

// Set a shared row's thumbnail from its authed EDITED-result data URL; hide the <img> if
// the fetch fails (unreachable server, or a project with no stored bytes yet).
export const resolveSharedThumb = async (image, thumb) => {
  try {
    thumb.src = await sharedThumbUrl(image);
    thumb.style.visibility = 'visible';
  } catch {
    thumb.style.visibility = 'hidden';
  }
};

// Pull the current shared pins from every connection into state.shared. Best-effort:
// an unreachable server is skipped (collectSharedPins swallows its error).
export const loadShared = async () => {
  state.connections = await loadConnections();
  const pins = await collectSharedPins(state.connections);
  state.shared = pins.map(sharedToImage);
  // Bytes stay cached while their row does; a row that went away drops its entries.
  const live = new Set(state.shared.map(sharedRowKey));
  for (const key of sharedBytes.keys()) {
    if (!live.has(key.slice(0, key.lastIndexOf('\n')))) sharedBytes.delete(key);
  }
  // The set of ORIGINAL source URLs that exist on a server, so a LOCAL pin of the same
  // image also shows the golden "on a server" outline (not just the separate shared rows).
  state.sharedSources = new Set(pins.map((p) => p.origin).filter(Boolean));
  // origin URL -> Set(serverUrl) + the connected hosts, for the "server pins" filter.
  state.serverByOrigin = new Map();
  for (const p of pins) {
    if (!p.origin) continue;
    if (!state.serverByOrigin.has(p.origin)) state.serverByOrigin.set(p.origin, new Set());
    state.serverByOrigin.get(p.origin).add(p.serverUrl);
  }
  state.serverHosts = state.connections.map((c) => c.url);
  syncServerFilterUI();
};

// Show/populate the popup's "server pins" checkbox + per-server select (only when at
// least one server is connected). Mirrors the same filter on the options page.
export const syncServerFilterUI = () => {
  const has = (state.serverHosts || []).length > 0;
  const wrap = document.getElementById('f-server-pins-wrap');
  const showServer = document.getElementById('f-server-pins');
  const sel = document.getElementById('f-server-store');
  if (wrap) wrap.hidden = !has;
  if (sel) {
    sel.hidden = !has || !(showServer && showServer.checked);
    if (has) {
      const prev = sel.value || 'all';
      sel.innerHTML = '<option value="all">Any server</option>'
        + state.serverHosts.map((u) => `<option value="${u}">${hostLabel(u)}</option>`).join('');
      sel.value = [...sel.options].some((o) => o.value === prev) ? prev : 'all';
    }
  }
};

// Poll-while-open: refresh shared pins so server-side changes show without a manual rescan.
// Rides the panel's shared clock (lib/pollClock.js), not a second timer on the same tick.
const pollShared = async () => {
  await loadShared();
  applyFilters();
};
export const startSharedPolling = () => { if (state.connections.length) pollClock.add(pollShared); };
export const stopSharedPolling = () => pollClock.remove(pollShared);
window.addEventListener('pagehide', stopSharedPolling);
