import { formatOf, VIDEO_FORMATS } from './filters.js';

// Pure helper: pull an image/video URL out of a drag's payloads (feature #6). Dragging a page
// <img>/<video> onto the side panel's list lands NOT a File but a URL in the drag's
// text/uri-list, text/html (an <img>/<source> src), or text/plain. `read(type)` returns the
// drag's string for that MIME type (''/throwing tolerated). Returns the first URL, or ''.
// DOM-free so it's unit-testable (tests/dragUrl.test.js).
export const extractDraggedUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

  // Prefer the actual media element in text/html FIRST: dragging an image wrapped in a
  // link (e.g. a Wikipedia thumbnail — <a href="/wiki/File:…"><img src="…500px…"></a>)
  // puts the LINK's href in text/uri-list but the real <img> src in text/html. We want
  // to pin the image, not the page it links to.
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

// Guess a pin kind from a URL's extension (video containers → 'video', else 'image'). The drag
// gives no element tag, so the extension is the best signal available for a dropped URL.
export const guessKindFromUrl = (url) =>
  VIDEO_FORMATS.includes(formatOf(String(url || ''))) ? 'video' : 'image';
