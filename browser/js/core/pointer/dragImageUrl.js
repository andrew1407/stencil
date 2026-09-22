import { timeoutSignal } from '../../net/abortable.js';
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

// A drag's text/html is SERIALISED markup, so its src arrives escaped ('?a=1&amp;b=2').
const HTML_ENTITIES = Object.freeze({ amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", '#39': "'" });
const unescapeHtml = (s) => String(s).replace(/&(amp|lt|gt|quot|apos|#39);/g, (_, e) => HTML_ENTITIES[e]);

// Only breaks the tie below — a false negative just falls back to source order.
const IMAGE_URL = /\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)(?:[?#]|$)/i;
export const looksLikeImageUrl = (u) =>
  IMAGE_URL.test(String(u || '')) || /^(data:image\/|blob:)/i.test(String(u || ''));

// github.com answers its own /raw/ and /blob/ paths with a redirect whose
// Access-Control-Allow-Origin is empty, so the CDN target has to be named outright.
const HOST_TWINS = Object.freeze([
  Object.freeze({
    from: /^https:\/\/github\.com\/([^/?#]+\/[^/?#]+)\/(?:raw|blob)\/(.+)$/i,
    to: (m) => `https://raw.githubusercontent.com/${m[1]}/${m[2]}`,
  }),
]);

// Each twin is spliced in right AFTER the url it came from, so the original is still tried
// first and a wrong guess costs nothing.
const withHostTwins = (urls) => urls.flatMap((u) => {
  const twins = [];
  for (const rule of HOST_TWINS) {
    const m = rule.from.exec(u);
    const twin = m ? rule.to(m) : '';
    if (twin && twin !== u) twins.push(twin);
  }
  return [u, ...twins];
});

const hostOf = (url) => { try { return new URL(String(url)).host; } catch { return String(url); } };

export const extractDraggedImageUrls = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

// '#'-prefixed lines are comments.
  const uriList = get('text/uri-list');
  const fromList = absolutize(
    uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#')));
  const m = get('text/html').match(/<img[^>]+src\s*=\s*["']([^"']+)["']/i);
  const fromHtml = m ? absolutize(unescapeHtml(m[1])) : '';
// Only when it is itself an http(s) URL.
  const text = get('text/plain').trim();
  const fromText = /^https?:\/\//i.test(text) ? text : '';
// Firefox-shaped sources carry "URL\ntitle" and nothing else.
  const fromMoz = absolutize(get('text/x-moz-url').split('\n')[0].trim());

// Whichever NAMES an image wins (a gallery's uri-list holds the full size, its <img> the
// thumbnail); then the <img> src, which is an image even when its url spells no extension.
  const candidates = [fromList, fromHtml, fromText, fromMoz].filter(Boolean);
  const named = candidates.filter(looksLikeImageUrl);
  return [...new Set(withHostTwins([...named, ...(fromHtml ? [fromHtml] : []), ...candidates]))];
};

// The first candidate, for callers that only need to know a drag carries one.
export const extractDraggedImageUrl = (read) => extractDraggedImageUrls(read)[0] || '';

// CORS-limited; the thrown messages are what the callers surface. `accept` bounds the
// MIME types (the chat also takes video).
export const fetchDraggedMediaFile = async (url, { accept = /^image\// } = {}) => {
  const resp = await fetch(url, { mode: 'cors', signal: timeoutSignal() }).catch(() => {
    throw new Error(`${hostOf(url)} refused the request (CORS or an unreachable host)`);
  });
  if (!resp.ok) throw new Error(`${hostOf(url)} answered HTTP ${resp.status}`);
  const blob = await resp.blob();
  if (!accept.test(blob.type || '')) throw new Error(`that URL is ${blob.type || 'not an image'}`);
  return new File([blob], fileNameForUrl(url, blob.type), { type: blob.type });
};

// Tries the candidates in order and keeps the first that answers with an accepted type. A
// guess here is cheap to get wrong: an extensionless CDN url and a page link look alike.
export const fetchFirstDraggedMediaFile = async (urls, opts = {}) => {
  const list = (Array.isArray(urls) ? urls : [urls]).filter(Boolean);
  if (!list.length) throw new Error('no image URL in that drag');
  const walk = [...new Set(withHostTwins(list))];
  let firstErr = null;
  for (const url of walk) {
    try { return await fetchDraggedMediaFile(url, opts); }
    catch (err) { firstErr = firstErr || err; }
  }
// Every candidate was tried, so the count separates "this host blocks downloads" from
// "the app picked the wrong url".
  const tried = walk.length > 1 ? `all ${walk.length} URLs in that drag` : 'the only URL in that drag';
  throw new Error(`${tried} failed; ${firstErr.message}`);
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
