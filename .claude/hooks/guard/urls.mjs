// URL rules for the guard: WebFetch, WebSearch and page navigation — a local origin passes, a
// secret path denies, a raw IP or an exfil-shaped query asks.

import { allow, ask, deny } from './verdict.mjs';

export function isLocalHost(hostport, ctx) {
  let h = String(hostport).trim().replace(/^\[/, '').replace(/\].*$/, '').replace(/:\d+$/, '').toLowerCase();
  if (!h) return true; // relative URL — same origin
  if (/^127\./.test(h) || h === '::1' || h === '::' || /^0\.0\.0\.0$/.test(h)) return true;
  return ctx.allowedOrigins.map((o) => String(o).toLowerCase()).includes(h);
}

// Secret tokens for URL/query text — deliberately tighter than commandTouchesSecret:
// no bare `credentials`/`gcloud` words, so ordinary doc lookups don't trip it.
const URL_SECRET_TOKEN = /\.env(\.(?!(example|sample|template|dist)\b)[\w-]+)?(?![.\w])|\bid_rsa\b|\bid_ed25519\b|\bid_dsa\b|\bid_ecdsa\b|\.ssh\//i;

const BASE64ISH_BLOB = /[A-Za-z0-9+/=_-]{200,}/; // a long unbroken encodable run

// Returns a decision for a URL an outbound request will hit, or null for a normal page load.
// Local/user-allowlisted origins are always fine — `#stencil=` fragments are huge on localhost.
function urlExfilDecision(rawUrl, ctx) {
  const s = String(rawUrl || '');
  let u;
  try {
    u = new URL(s);
  } catch {
    return null; // relative or non-URL — nothing to judge here
  }
  if (isLocalHost(u.hostname, ctx)) return allow();
  if (URL_SECRET_TOKEN.test(s)) return deny('URL mentions a secret/credential path');
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(u.hostname) || u.hostname.startsWith('[')) {
    return ask('request to a raw IP address (not a named host)');
  }
  const tail = (u.search || '') + (u.hash || '');
  if (tail.length > 512 || BASE64ISH_BLOB.test(tail)) {
    return ask('exfil-shaped URL: very long or base64-looking query/fragment');
  }
  return null;
}

export function webFetchDecision(input, ctx) {
  return urlExfilDecision(input && input.url, ctx) || allow();
}

export function webSearchDecision(input) {
  const q = String((input && input.query) || '');
  if (URL_SECRET_TOKEN.test(q)) return ask('search query mentions a secret/credential path');
  if (BASE64ISH_BLOB.test(q) || q.length > 1000) {
    return ask('exfil-shaped search query: very long or base64-looking blob');
  }
  return allow();
}

export function navigationDecision(input, ctx) {
  return urlExfilDecision(input && input.url, ctx) || allow();
}
