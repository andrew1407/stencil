// ── imageModel: pure predicates over a scanned-image record ─────────────────
// DOM-free / chrome-free helpers extracted from popup/popup.js so they can be unit-tested
// under `node --test` (the popup controller itself is DOM/chrome-bound and untestable there).
// An "image" here is the popup's row shape: { kind, src, videoUrl, posterUrl, name, w, h, ... }.
import { filenameFromUrl } from './stencil.js';

// The image's own URL for provenance: its media URL for a video (the still is an opaque
// frame), else the image/background src. Empty/data: sources aren't tracked upstream.
export const sourceOf = (image) => (image.kind === 'video' ? (image.videoUrl || '') : (image.src || ''));

// A video's poster as a standalone image item, so the action menu can open / crop / download
// the poster (preview cover) directly. Mirrors a scanned <img> poster row.
export const posterImage = (video) => ({
  kind: 'img',
  src: video.posterUrl,
  poster: true,
  name: filenameFromUrl(video.posterUrl, 'poster'),
  w: 0, h: 0,
});

// The image to actually open / crop / preview: the scanned still, falling back to a video's
// poster when no frame was captured (an unplayed video's frame 0 is often black).
export const editableSrc = (image) => image.src || image.posterUrl || '';

// An image/video can be pinned when it has an openable source URL (img/background src, or a
// video's media URL) — the same thing "open in new tab" needs.
export const pinnable = (image) => !!sourceOf(image);

// Shared pins only obey the text search (name / server source) — the page-image kind / format /
// size filters don't apply to a remote project. No search = always shown. With regex=true the
// search is a case-insensitive RegExp (invalid → matches nothing), matching passesFilters.
export const sharedMatchesSearch = (image, search, regex = false) => {
  if (!search) return true;
  const fields = [image.name, image.source];
  if (regex) {
    let re;
    try { re = new RegExp(search, 'i'); } catch { return false; }
    return fields.some(v => re.test(v || ''));
  }
  const q = search.toLowerCase();
  return fields.some(v => (v || '').toLowerCase().includes(q));
};

// A connected server's short label for the "server pins" filter select: its URL host, falling
// back to the raw origin (or a generic placeholder) when it isn't a parseable URL.
export const hostLabel = (origin) => {
  try {
    return new URL(origin).host;
  } catch {
    return origin || '(server)';
  }
};
