import { blobToDataUrl } from '../lib/stencil.js';
import { loadConnections, collectSharedPins, connectionByUrl, fetchProjectImage } from '../lib/connections.js';
import { hostLabel } from '../lib/imageModel.js';
import { pollClock } from '../lib/pollClock.js';
import { state } from './model.js';
import { previewCache } from './preview.js';
import { applyFilters } from './filters.js';
import { escapeHtml } from '../lib/escapeHtml.js';

// A shared-pin record as a row. No `src`: the download is Bearer-authed, so a bare <img>
// cannot load it; measured:true keeps the size observer off it.
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
  color: pin.color || '',
  opened: [],
  pinned: false,
});

const sharedConn = (image) => connectionByUrl(state.connections, image.serverUrl);

// The entry is the PROMISE, so a re-render joins the fetch in flight; a rejection is
// dropped so the next render retries.
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

// The thumbnail shows the edited `result`; the hand-off below needs the untouched original.
export const sharedThumbUrl = (image) => sharedFetch(image, 'thumb', async () => {
  const blob = await sharedImageBlob(image, 'result').catch(() => sharedImageBlob(image, 'original'));
  return blobToDataUrl(blob);
});

export const sharedDataUrl = (image) => sharedFetch(image, 'original', async () => {
  const dataUrl = await blobToDataUrl(await sharedImageBlob(image, 'original'));
  if (image.source) previewCache.set(image.source, dataUrl);
  return dataUrl;
});

export const resolveSharedThumb = async (image, thumb) => {
  try {
    thumb.src = await sharedThumbUrl(image);
    thumb.style.visibility = 'visible';
  } catch {
    thumb.style.visibility = 'hidden';
  }
};

// An unreachable server is skipped (collectSharedPins swallows its error).
export const loadShared = async () => {
  state.connections = await loadConnections();
  const pins = await collectSharedPins(state.connections);
  state.shared = pins.map(sharedToImage);
  // Bytes stay cached while their row does.
  const live = new Set(state.shared.map(sharedRowKey));
  for (const key of sharedBytes.keys()) {
    if (!live.has(key.slice(0, key.lastIndexOf('\n')))) sharedBytes.delete(key);
  }
  // A LOCAL pin of an image that is also on a server takes the golden outline too.
  state.sharedSources = new Set(pins.map((p) => p.origin).filter(Boolean));
  state.serverByOrigin = new Map();
  for (const p of pins) {
    if (!p.origin) continue;
    if (!state.serverByOrigin.has(p.origin)) state.serverByOrigin.set(p.origin, new Set());
    state.serverByOrigin.get(p.origin).add(p.serverUrl);
  }
  state.serverHosts = state.connections.map((c) => c.url);
  syncServerFilterUI();
};

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
        + state.serverHosts.map((u) => `<option value="${escapeHtml(u)}">${escapeHtml(hostLabel(u))}</option>`).join('');
      sel.value = [...sel.options].some((o) => o.value === prev) ? prev : 'all';
    }
  }
};

// Rides the panel's shared clock (lib/pollClock.js), not a second timer on the same tick.
const pollShared = async () => {
  await loadShared();
  applyFilters();
};
export const startSharedPolling = () => { if (state.connections.length) pollClock.add(pollShared); };
export const stopSharedPolling = () => pollClock.remove(pollShared);
window.addEventListener('pagehide', stopSharedPolling);
