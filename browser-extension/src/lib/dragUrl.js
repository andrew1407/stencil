import { formatOf, VIDEO_FORMATS } from './filters.js';

// A dragged page <img>/<video> lands NOT a File but a URL in text/uri-list, text/html or
// text/plain. `read(type)` may return '' or throw. Returns the first URL, or ''.
export const extractDraggedUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

  // text/html FIRST: an image wrapped in a link puts the LINK's href in text/uri-list
  // but the real <img> src in text/html.
  const html = get('text/html');
  const m = html && html.match(/<(?:img|source|video)[^>]+src\s*=\s*["']([^"']+)["']/i);
  if (m) return m[1];
  const uriList = get('text/uri-list');
  if (uriList) {
    const line = uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#'));
    if (line) return line;
  }
  const text = get('text/plain').trim();
  if (/^https?:\/\//i.test(text) || text.startsWith('data:')) return text;
  // `text/x-moz-url` is "URL\ntitle" — some drag sources populate only this.
  const moz = get('text/x-moz-url').split('\n')[0].trim();
  if (/^https?:\/\//i.test(moz) || moz.startsWith('data:')) return moz;
  return '';
};

// The drag gives no element tag, so the URL's extension is the only kind signal.
export const guessKindFromUrl = (url) =>
  VIDEO_FORMATS.includes(formatOf(String(url || ''))) ? 'video' : 'image';
