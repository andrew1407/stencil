// The one browser fetch guard (ARCHITECTURE.md): a request is refused unless its scheme can
// be fetched, and never leaves without a deadline. data:/blob: carry local bytes — the
// Open-Image URL field and every dataUrl chat hands back — so they pass; file: does not.
import { NET_TIMEOUT_MS, timeoutSignal } from './abortable.js';

export const FETCHABLE_SCHEMES = ['http:', 'https:', 'data:', 'blob:'];

const schemeOf = (url) => {
  const m = /^\s*([a-z][a-z0-9+.-]*):/i.exec(String(url ?? ''));
  return m ? `${m[1].toLowerCase()}:` : '';
};

export const isFetchable = (url) => FETCHABLE_SCHEMES.includes(schemeOf(url));

export const guardedFetch = (url, init = {}) => {
  if (!isFetchable(url)) {
    const scheme = schemeOf(url) || 'no scheme';
    return Promise.reject(new Error(`Refused to fetch "${String(url)}" (${scheme}) — http(s) only`));
  }
  const impl = globalThis.fetch;
  if (typeof impl !== 'function') return Promise.reject(new Error('no fetch implementation available'));
  return impl(url, { signal: timeoutSignal(NET_TIMEOUT_MS), ...init });
};
