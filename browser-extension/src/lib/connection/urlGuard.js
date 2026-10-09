// SSRF guard for page-derived fetch URLs: <all_urls> lets fetch() reach any address on
// the user's network, and the URLs come from arbitrary pages. The host check is lexical, judged by
// the shared address table (addressRanges.js) — MV3 has no resolve-time hook for DNS rebinding —
// and guardedFetch adds the redirect refusal and the byte cap.

import { inClass, isBlockedAddress, parseAddress } from './addressRanges.js';
import { MAX_FETCH_BYTES, cancelBody, readCapped } from './cappedBody.js';

export { MAX_FETCH_BYTES, readCapped };

// The URL parser has already canonicalised octal/hex/decimal IPv4 spellings into dotted-quad.
const isMetadataHost = (host) => {
  const bytes = parseAddress(host);
  return !!bytes && inClass(bytes, 'cloudMetadata');
};

// http(s) only (data:/blob: pass); a localhost name or an address the table's `fetch` policy blocks
// is refused. `allowLoopback`: USER-typed URLs only. `allowSameHostAs`: TRUSTED page URL, never the metadata IP.
export const isAllowedImageUrl = (url, { allowLoopback = false, allowSameHostAs = '' } = {}) => {
  let u;
  try { u = new URL(String(url)); } catch { return false; }
  if (u.protocol === 'data:' || u.protocol === 'blob:') return true;
  if (u.protocol !== 'http:' && u.protocol !== 'https:') return false;
  // One trailing dot is the same host to the resolver (`localhost.` is loopback).
  const host = u.hostname.toLowerCase().replace(/\.$/, '');
  if (allowSameHostAs && !isMetadataHost(host)) {
    try {
      const p = new URL(String(allowSameHostAs));
      if ((p.protocol === 'http:' || p.protocol === 'https:') && p.hostname.toLowerCase().replace(/\.$/, '') === host) return true;
    } catch { /* no usable page context — stay strict */ }
  }
  if (host === 'localhost' || host.endsWith('.localhost')) return allowLoopback;
  const bytes = parseAddress(host);
  if (bytes) return !isBlockedAddress(bytes, 'fetch', { allowLoopback });
  if (host.includes(':') || host.startsWith('[')) return false;   // unparseable IPv6 literal
  return true;   // a public name — private DNS answers are invisible lexically (MV3)
};

// ms; common/config/constants.json NETWORK.fetchTimeoutMs, the browser app's NET_TIMEOUT_MS.
export const FETCH_TIMEOUT_MS = 30_000;

// The caller's signal raced against the deadline, so a stalled host cannot pin the worker.
export const withDeadline = (signal, ms = FETCH_TIMEOUT_MS) => {
  const S = globalThis.AbortSignal;
  if (typeof S?.timeout !== 'function') return signal;
  if (!signal) return S.timeout(ms);
  return typeof S.any === 'function' ? S.any([signal, S.timeout(ms)]) : signal;
};

export const BLOCKED_ADDRESS = 'blocked private or internal address';
export const REDIRECT_REFUSED = 'that URL redirects elsewhere';
const REDIRECT_STATUSES = new Set([301, 302, 303, 307, 308]);

// A redirect is refused, as the CLI, pystencil and desktop refuse it: a public first hop must not
// bounce to an internal host, and a browser hides a manual redirect's Location, so no hop re-checks.
export const guardedFetch = async (url, guard = {}, init = {}) => {
  if (!isAllowedImageUrl(url, guard)) throw new Error(BLOCKED_ADDRESS);
  const resp = await fetch(url, { ...init, signal: withDeadline(init.signal), redirect: 'manual' });
  if (resp.type === 'opaqueredirect' || REDIRECT_STATUSES.has(resp.status)) {
    await cancelBody(resp);
    // The https twin is its own request to the same host, so the guard's verdict still holds.
    if (/^http:/i.test(url)) return guardedFetch(`https:${url.slice(5)}`, guard, init);
    throw new Error(REDIRECT_REFUSED);
  }
  return resp;
};

export const readBlobCapped = async (resp, max = MAX_FETCH_BYTES) =>
  new Blob([await readCapped(resp, max)], { type: resp.headers?.get?.('content-type') || '' });
