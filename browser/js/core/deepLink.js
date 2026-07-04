import { isLoopbackHost, normalizeUrl } from '../net/connectionManager.js';

// ── Project deep links ──────────────────────────────────────────
// A saved project can open in a NEW browser tab from the projects modal. The new tab carries
// the target id in a query param (`?open=<id>`), which the booting app consumes once and
// strips from the URL. Pure string helpers (no DOM) for direct unit-testing.

export const OPEN_PARAM = 'open';

// Read the requested project id from a location.search string (e.g.
// "?open=p_1&x=2"). Returns the id, or null when the param is absent/empty.
export const readOpenProjectId = (search = '') => {
  let params;
  try {
    params = new URLSearchParams(search || '');
  } catch {
    return null;
  }
  const id = params.get(OPEN_PARAM);
  return id ? id : null;
};

// Build the URL that opens `id` in a fresh tab. `base` is the page URL without a
// query string (e.g. location.origin + location.pathname); `id` is appended as
// the open param, URL-encoded.
export const buildOpenProjectUrl = (base, id) =>
  `${base}?${OPEN_PARAM}=${encodeURIComponent(id)}`;

// Build a URL that hands a full image off to a fresh tab via the `#stencil=<JSON>`
// fragment consumed by DrawingApp.applyExternalLaunch (shape
// { dataUrl, name?, incognito?, ... }). The payload rides in the fragment (not the
// query) so it stays off server logs and out of history after the receiver strips it.
// This is the only vehicle that works for incognito launches, since those are never
// persisted and so cannot be referenced by `?open=<id>`.
export const buildExternalLaunchUrl = (base, payload) =>
  `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;

// ── Cross-front-end "Open in…" links ────────────────────────────
// The desktop app registers the `stencil://` OS scheme (macOS/Linux); the Telegram bot
// listens on `/start <payload>` deep links. These builders produce both link kinds from
// the same project identity the fragment payload uses: either a server reference
// ({ url, id, version? }, never a token — the receiver connects like a fresh client) or
// inline image + layout for local/incognito sessions.

// Build a `stencil://open?…` URL for the desktop app. Recognized fields:
// server+id[+version] (open a server project), src (path/URL/data: image),
// layout (object or JSON string, applied after the image loads), frame, incognito.
// server+id wins over src on the receiving side; empty/absent fields are omitted.
export const buildStencilSchemeUrl = ({ scheme = 'stencil', server, id, version, src, layout, frame, incognito } = {}) => {
  const params = [];
  const add = (k, v) => params.push(`${k}=${encodeURIComponent(v)}`);
  if (server && id) {
    add('server', server);
    add('id', id);
    if (version) add('version', String(version));
  } else if (src) {
    add('src', src);
    if (layout) add('layout', typeof layout === 'string' ? layout : JSON.stringify(layout));
    if (frame != null) add('frame', String(frame));
  }
  if (incognito) add('incognito', '1');
  return `${scheme}://open?${params.join('&')}`;
};

// Telegram caps `?start=` payloads at 64 chars from the charset [A-Za-z0-9_-].
export const TELEGRAM_START_LIMIT = 64;

// Drop the scheme from a normalized origin when it matches what normalizeUrl would
// infer for the bare host (https for remote hosts, http for loopback) — the decoder
// re-normalizes, so the default scheme round-trips from just `host[:port]`.
const compressOrigin = (origin) => {
  const u = new URL(origin);
  const defaultScheme = isLoopbackHost(u.hostname) ? 'http:' : 'https:';
  return u.protocol === defaultScheme ? u.host : origin;
};

const toBase64 = (bin) => (typeof btoa === 'function'
  ? btoa(bin)
  : Buffer.from(bin, 'binary').toString('base64'));

// Encode (server origin, project id) into a t.me start payload:
// "1" (version marker) + base64url("host[:port]|projectId"), padding stripped.
// Returns null when the result would exceed Telegram's 64-char limit — callers must
// then fall back to showing copyable `/connect <url>` + `/fetch <id>` commands.
// The identical codec exists in desktop/src/app/deepLink.cpp and
// bot Application/Links/DeepLinkCodec.cs — keep the three in sync (shared golden
// vectors in each suite's tests).
export const encodeTelegramStartPayload = (serverUrl, projectId) => {
  const plain = `${compressOrigin(normalizeUrl(serverUrl))}|${projectId}`;
  const bytes = new TextEncoder().encode(plain);
  let bin = '';
  for (const b of bytes) bin += String.fromCharCode(b);
  const payload = '1' + toBase64(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  return payload.length <= TELEGRAM_START_LIMIT ? payload : null;
};

export const buildTelegramLink = (botUsername, payload) =>
  `https://t.me/${botUsername}?start=${payload}`;

// Telegram never linkifies custom schemes, so `stencil://` links sent through chat ride
// inside launch.html (served next to the app), which forwards to the scheme URL.
export const buildDesktopBounceUrl = (browserBase, stencilUrl) =>
  `${String(browserBase || '').replace(/\/+$/, '')}/launch.html#stencil-desktop=${encodeURIComponent(stencilUrl)}`;

// Validate + classify an inbound `#stencil=` payload. Returns null for junk, else
// { kind: 'server'|'dataUrl'|'src', ...normalized fields }. Precedence when several
// image sources are present: server > dataUrl > src (the server's copy is canonical).
// Pure so the fragment schema is unit-testable without a DOM; applyExternalLaunch
// consumes the result.
export const normalizeLaunchPayload = (payload) => {
  if (!payload || typeof payload !== 'object') return null;
  const str = (v) => (typeof v === 'string' && v ? v : null);
  const obj = (v) => (v && typeof v === 'object' ? v : null);
  const common = {
    name: str(payload.name),
    crop: obj(payload.crop),
    page: obj(payload.page),
    source: str(payload.source),
    resource: str(payload.resource),
    open: str(payload.open),
    incognito: !!payload.incognito,
    layout: obj(payload.layout),
  };
  const server = obj(payload.server);
  if (server && str(server.url) && str(server.id)) {
    const version = Number(server.version);
    return {
      kind: 'server',
      server: { url: server.url, id: server.id, version: Number.isFinite(version) ? version : 0 },
      ...common,
    };
  }
  // Only a real data: URL may ride the dataUrl slot (the receiver fetch()es it) — a remote
  // image belongs in `src`, scheme-checked below.
  const dataUrl = str(payload.dataUrl);
  if (dataUrl && /^data:/i.test(dataUrl)) return { kind: 'dataUrl', dataUrl, ...common };
  const src = str(payload.src);
  if (src && /^https?:/i.test(src)) return { kind: 'src', src, ...common };
  return null;
};
