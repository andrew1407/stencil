// "Open in…" deep links: `stencil://open?…` for the desktop app (server rows send a
// reference, never a token) and `t.me/<bot>?start=` for the Telegram bot. Ports of
// browser/js/core/launch/deepLink.js + openInModal.js's size guards; tests/openIn.test.js pins them.
import { isLoopbackHost, normalizeUrl } from '../connection/connections.js';

// Inline hand-offs ride the OS launch argv, which tolerates far less than an in-page URL.
export const INLINE_WARN_CHARS = 200_000;
export const INLINE_MAX_CHARS = 1_000_000;

// server+id wins over src on the receiving side; empty fields are omitted.
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

// The decoder re-normalizes, so the default scheme round-trips from just `host[:port]`.
const compressOrigin = (origin) => {
  const u = new URL(origin);
  const defaultScheme = isLoopbackHost(u.hostname) ? 'http:' : 'https:';
  return u.protocol === defaultScheme ? u.host : origin;
};

const toBase64 = (bin) => (typeof btoa === 'function'
  ? btoa(bin)
  : Buffer.from(bin, 'binary').toString('base64'));

// "1" + base64url("host[:port]|projectId"), unpadded; null when over the limit. The same
// codec lives in browser deepLink.js, desktop deepLink.cpp and the bot's DeepLinkCodec.cs.
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
