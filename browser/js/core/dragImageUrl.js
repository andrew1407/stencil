import { timeoutSignal } from '../net/abortable.js';
// An <img> dragged from another site lands as a URL in text/uri-list, text/html or
// text/plain, never as a File; `read(type)` returns the drag's string for that MIME.
// '//host' is fixable (https); a root-relative '/path' has an unknowable origin and is refused.
const absolutize = (url) => {
  const u = String(url || '').trim();
  if (!u) return '';
  if (/^(https?:|data:|blob:)/i.test(u)) return u;
  if (u.startsWith('//')) return 'https:' + u;
  return '';
};

// Only breaks the tie below — a false negative just falls back to source order.
const IMAGE_URL = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i;
export const looksLikeImageUrl = (u) =>
  IMAGE_URL.test(String(u || '')) || /^(data:image\/|blob:)/i.test(String(u || ''));

export const extractDraggedImageUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

// '#'-prefixed lines are comments.
  const uriList = get('text/uri-list');
  const fromList = absolutize(
    uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#')));
  const m = get('text/html').match(/<img[^>]+src\s*=\s*["']([^"']+)["']/i);
  const fromHtml = m ? absolutize(m[1]) : '';
// Only when it is itself an http(s) URL.
  const text = get('text/plain').trim();
  const fromText = /^https?:\/\//i.test(text) ? text : '';

// Source order is the tiebreak, not the rule: a LINKED image puts the link target in
// uri-list and the real image only in text/html's <img src>, so whichever names an image wins.
  const candidates = [fromList, fromHtml, fromText].filter(Boolean);
  return candidates.find(looksLikeImageUrl) || candidates[0] || '';
};

// CORS-limited; the thrown messages are what the callers surface. `accept` bounds the
// MIME types (the chat also takes video).
export const fetchDraggedMediaFile = async (url, { accept = /^image\// } = {}) => {
  const resp = await fetch(url, { mode: 'cors', signal: timeoutSignal() }).catch(() => {
    throw new Error('the request was blocked (CORS or an unreachable host)');
  });
  if (!resp.ok) throw new Error(`the server answered HTTP ${resp.status}`);
  const blob = await resp.blob();
  if (!accept.test(blob.type || '')) throw new Error(`that URL is ${blob.type || 'not an image'}`);
  return new File([blob], fileNameForUrl(url, blob.type), { type: blob.type });
};

// data:/blob: URLs have no name (their last segment is the whole payload), so those get a
// plain name with the MIME's extension.
export const fileNameForUrl = (url, mime = '') => {
  const u = String(url || '');
  const ext = (mime.split('/')[1] || '').split(';')[0].replace('jpeg', 'jpg');
  const fallback = `image${ext ? `.${ext}` : ''}`;
  if (/^(data|blob):/i.test(u)) return fallback;
  const last = decodeURIComponent((u.split('/').pop() || '').split(/[?#]/)[0] || '');
// An extensionless segment with a known MIME is an endpoint name, not a filename; without
// a MIME the segment is still the only name there is.
  return last && last.length <= 80 && (last.includes('.') || !ext) ? last : fallback;
};

// Reads the items SYNCHRONOUSLY (clipboardData invalidates after an await), falling back
// to `.files`. DOM-free.
export const mediaFilesFromData = (dt) => {
  const isMedia = (type) => /^(image|video)\//.test(type || '');
  const out = [];
  if (!dt) return out;
  if (dt.items) {
    for (const item of dt.items) {
      if (item.kind === 'file' && isMedia(item.type)) {
        const f = item.getAsFile && item.getAsFile();
        if (f) out.push(f);
      }
    }
  }
  if (!out.length && dt.files) {
    for (const f of dt.files) if (isMedia(f.type)) out.push(f);
  }
  return out;
};
