import { isLoopbackHost, normalizeUrl } from '../net/connectionManager.js';

// Project deep links: a new tab carries the target id in `?open=<id>`, which the booting
// app consumes once and strips from the URL. Pure string helpers.

export const OPEN_PARAM = 'open';

// The id from a location.search string, or null when absent/empty.
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

// `base` is the page URL without a query string.
export const buildOpenProjectUrl = (base, id) =>
  `${base}?${OPEN_PARAM}=${encodeURIComponent(id)}`;

// The payload rides in the `#stencil=` fragment (consumed by applyExternalLaunch), not the
// query, so it stays off server logs and out of history; it is also the only vehicle for
// incognito launches, which are never persisted.
export const buildExternalLaunchUrl = (base, payload) =>
  `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;

// Cross-front-end "Open in…" links: the desktop's `stencil://` scheme and the Telegram
// bot's `/start <payload>`, from the same identity the fragment uses — a server reference
// ({ url, id, version? }, never a token) or inline image + layout.

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

// Telegram caps `?start=` payloads at 64 chars from [A-Za-z0-9_-].
export const TELEGRAM_START_LIMIT = 64;

// Drop the scheme when it is what normalizeUrl infers for the bare host (https remote,
// http loopback) — the decoder re-normalizes, so it round-trips from `host[:port]`.
const compressOrigin = (origin) => {
  const u = new URL(origin);
  const defaultScheme = isLoopbackHost(u.hostname) ? 'http:' : 'https:';
  return u.protocol === defaultScheme ? u.host : origin;
};

const toBase64 = (bin) => (typeof btoa === 'function'
  ? btoa(bin)
  : Buffer.from(bin, 'binary').toString('base64'));

// "1" + base64url("host[:port]|projectId"), padding stripped; null past Telegram's limit.
// The identical codec lives in desktop/src/app/deepLink.cpp and bot
// Application/Links/DeepLinkCodec.cs — shared golden vectors in each suite.
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

// Telegram never linkifies custom schemes, so `stencil://` rides launch.html, which forwards.
export const buildDesktopBounceUrl = (browserBase, stencilUrl) =>
  `${String(browserBase || '').replace(/\/+$/, '')}/launch.html#stencil-desktop=${encodeURIComponent(stencilUrl)}`;

// chars ≈ bytes for base64: the server's 32 MiB MaxBodyBytes.
export const LAUNCH_DATA_URL_MAX = 32 * 1024 * 1024;

// Null for junk, else { kind: 'server'|'dataUrl'|'src', ...normalized }. Precedence:
// server > dataUrl > src (the server's copy is canonical).
export const normalizeLaunchPayload = (payload) => {
  if (!payload || typeof payload !== 'object') return null;
  const str = (v) => (typeof v === 'string' && v ? v : null);
// Fresh plain object without __proto__/constructor/prototype: a downstream
// Object.assign of them would pollute Object.prototype.
  const obj = (v) => {
    if (!v || typeof v !== 'object') return null;
    const clean = {};
    for (const k of Object.keys(v)) {
      if (k !== '__proto__' && k !== 'constructor' && k !== 'prototype') clean[k] = v[k];
    }
    return clean;
  };
  const common = {
    name: str(payload.name),
    crop: obj(payload.crop),
// The full uncropped frame instead of the page-aspect auto-crop; ignored with an explicit `crop`.
    noCrop: !!payload.noCrop,
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
// Only a real data: URL may ride this slot (the receiver fetch()es it); remote images use `src`.
  const dataUrl = str(payload.dataUrl);
  if (dataUrl && dataUrl.length > LAUNCH_DATA_URL_MAX) return null;
  if (dataUrl && /^data:/i.test(dataUrl)) return { kind: 'dataUrl', dataUrl, ...common };
  const src = str(payload.src);
  if (src && /^https?:/i.test(src)) return { kind: 'src', src, ...common };
  return null;
};
