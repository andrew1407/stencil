import { timeoutSignal } from '../net/abortable.js';
// Pure helper: pull an image URL out of a cross-page drag's payloads. An <img> dragged
// from another website lands NOT as a File but as a URL in text/uri-list, text/html
// (an <img src>), or text/plain; `read(type)` returns the drag's string for that MIME
// (''/throwing tolerated). Returns the first image-looking URL, or ''. DOM-free.
// absolutize: '//host/path' (protocol-relative) is fixable — https is the only sane
// scheme for a cross-origin drag; '/path' (root-relative) has an unknowable origin and
// is refused outright rather than fetched against the wrong host.
const absolutize = (url) => {
  const u = String(url || '').trim();
  if (!u) return '';
  if (/^(https?:|data:|blob:)/i.test(u)) return u;
  if (u.startsWith('//')) return 'https:' + u;
  return '';   // relative, or a scheme we can't fetch — not a usable drag source
};

// Does this URL name an image file? Used to break the tie below, so it only has to
// be right about the common cases — a false negative just falls back to source order.
const IMAGE_URL = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i;
export const looksLikeImageUrl = (u) =>
  IMAGE_URL.test(String(u || '')) || /^(data:image\/|blob:)/i.test(String(u || ''));

export const extractDraggedImageUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

  // uri-list: newline-separated; '#'-prefixed lines are comments.
  const uriList = get('text/uri-list');
  const fromList = absolutize(
    uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#')));
  // html: the first <img src="…">.
  const m = get('text/html').match(/<img[^>]+src\s*=\s*["']([^"']+)["']/i);
  const fromHtml = m ? absolutize(m[1]) : '';
  // plain text: only when it's itself an http(s) URL.
  const text = get('text/plain').trim();
  const fromText = /^https?:\/\//i.test(text) ? text : '';

  // Source order is the tiebreak, NOT the rule: dragging a LINKED image (Wikipedia wraps
  // article images in an <a>) puts the link target in uri-list and the real image only in
  // text/html's <img src>. Whichever payload names an image wins; else fall back to order.
  const candidates = [fromList, fromHtml, fromText].filter(Boolean);
  return candidates.find(looksLikeImageUrl) || candidates[0] || '';
};

// Fetch a dragged URL into a File, so a cross-page drag flows through the very same
// path as a dropped file. CORS-limited (like the URL tab) — the thrown messages are
// what the callers surface. `accept` bounds the MIME types: the canvas takes images,
// the chat also takes video (its attachments sample frames from one).
export const fetchDraggedMediaFile = async (url, { accept = /^image\// } = {}) => {
  const resp = await fetch(url, { mode: 'cors', signal: timeoutSignal() }).catch(() => {
    throw new Error('the request was blocked (CORS or an unreachable host)');
  });
  if (!resp.ok) throw new Error(`the server answered HTTP ${resp.status}`);
  const blob = await resp.blob();
  if (!accept.test(blob.type || '')) throw new Error(`that URL is ${blob.type || 'not an image'}`);
  return new File([blob], fileNameForUrl(url, blob.type), { type: blob.type });
};

// A readable filename for a fetched drag. http(s) URLs name the file; data:/blob: URLs
// have no name at all — their "last path segment" is the entire base64 payload — so
// those get a plain name with the MIME's extension instead.
export const fileNameForUrl = (url, mime = '') => {
  const u = String(url || '');
  const ext = (mime.split('/')[1] || '').split(';')[0].replace('jpeg', 'jpg');
  const fallback = `image${ext ? `.${ext}` : ''}`;
  if (/^(data|blob):/i.test(u)) return fallback;
  const last = decodeURIComponent((u.split('/').pop() || '').split(/[?#]/)[0] || '');
  // A path ending in a slash (or an opaque id) is no better than the fallback. An
  // extensionless segment with a known MIME is an endpoint name, not a filename — prefer
  // the fallback; without a MIME the segment is still the only name there is, keep it.
  return last && last.length <= 80 && (last.includes('.') || !ext) ? last : fallback;
};

// Media (image/video) Files carried by a DataTransfer / clipboardData. Reads the
// items SYNCHRONOUSLY (clipboardData invalidates after an await), falling back to
// `.files` when items are absent. Shared by the global paste/drop wiring
// (controlsBinder) and the chat panel's attach-on-paste/drop. DOM-free, testable.
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
