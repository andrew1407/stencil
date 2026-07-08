import { formatOf, VIDEO_FORMATS } from './filters.js';

// Pure helper: pull an image/video URL out of a drag's payloads (feature #6). Dragging a page
// <img>/<video> onto the side panel's list lands NOT a File but a URL in the drag's
// text/uri-list, text/html (an <img>/<source> src), or text/plain. `read(type)` returns the
// drag's string for that MIME type (''/throwing tolerated). Returns the first URL, or ''.
// DOM-free so it's unit-testable (tests/dragUrl.test.js).
export const extractDraggedUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

  const uriList = get('text/uri-list');
  if (uriList) {
    const line = uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#'));
    if (line) return line;
  }
  const html = get('text/html');
  const m = html && html.match(/<(?:img|source|video)[^>]+src\s*=\s*["']([^"']+)["']/i);
  if (m) return m[1];
  const text = get('text/plain').trim();
  if (/^https?:\/\//i.test(text) || text.startsWith('data:')) return text;
  return '';
};

// Guess a pin kind from a URL's extension (video containers → 'video', else 'image'). The drag
// gives no element tag, so the extension is the best signal available for a dropped URL.
export const guessKindFromUrl = (url) =>
  VIDEO_FORMATS.includes(formatOf(String(url || ''))) ? 'video' : 'image';
