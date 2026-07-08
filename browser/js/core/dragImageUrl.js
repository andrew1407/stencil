// Pure helper: pull an image URL out of a cross-page drag's payloads (feature #5).
// An <img> dragged from another website lands NOT as a File but as a URL in the drag's
// text/uri-list, text/html (an <img src>), or text/plain. `read(type)` returns the drag's
// string for that MIME type (''/throwing tolerated). Returns the first image-looking URL, or ''.
// Kept DOM-free so it's unit-testable (tests/dragImageUrl.test.js).
export const extractDraggedImageUrl = (read) => {
  const get = (t) => { try { return read(t) || ''; } catch { return ''; } };

  // uri-list: newline-separated; '#'-prefixed lines are comments.
  const uriList = get('text/uri-list');
  if (uriList) {
    const line = uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#'));
    if (line) return line;
  }
  // html: the first <img src="…">.
  const html = get('text/html');
  const m = html && html.match(/<img[^>]+src\s*=\s*["']([^"']+)["']/i);
  if (m) return m[1];
  // plain text: only when it's itself an http(s) URL.
  const text = get('text/plain').trim();
  if (/^https?:\/\//i.test(text)) return text;
  return '';
};
