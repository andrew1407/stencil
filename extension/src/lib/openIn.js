// ── "Open in…" cross-front-end deep links (extension) ───────────────────────
// A popup/panel row can hand its image off to another Stencil front-end, mirroring the
// browser app's toolbar "Open In…" menu:
//   • Desktop app — a `stencil://open?…` OS-scheme link the desktop app registers. A page
//     image rides INLINE (src=data:…, plus an optional layout); a server row sends only a
//     server reference (the desktop connects like a fresh client — no token in the link).
//   • Telegram bot — a `t.me/<bot>?start=<payload>` deep link carrying a (server, project id)
//     reference in a 64-char start payload. Server rows only: a start payload can't carry
//     image bytes, and an unsaved image has no id.
//
// These are PORTS of browser/js/core/deepLink.js (buildStencilSchemeUrl,
// encodeTelegramStartPayload, buildTelegramLink) + the size guards from
// browser/js/ui/openInModal.js. The extension can't import from browser/ (separate
// subproject), so they're duplicated + unit-tested against the browser's golden vectors
// (tests/openIn.test.js) so the links stay byte-compatible. Keep the two in sync.
import { isLoopbackHost, normalizeUrl } from './connections.js';

// Inline hand-offs ride the OS launch machinery (LaunchServices / xdg-open argv), which
// tolerates far less than an in-page URL. Warn on large embedded images; refuse absurd ones.
export const INLINE_WARN_CHARS = 200_000;
export const INLINE_MAX_CHARS = 1_000_000;

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
// Returns null when the result would exceed Telegram's 64-char limit. The identical codec
// exists in browser/js/core/deepLink.js, desktop/src/app/deepLink.cpp and the bot's
// DeepLinkCodec.cs — keep them in sync (shared golden vectors in each suite's tests).
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
