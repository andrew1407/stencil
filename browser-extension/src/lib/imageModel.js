// Pure predicates over the popup's row shape { kind, src, videoUrl, posterUrl, name, w, h, … }.
import { filenameFromUrl } from './imageData.js';

// A video's provenance is its media URL — the still is an opaque frame.
export const sourceOf = (image) => (image.kind === 'video' ? (image.videoUrl || '') : (image.src || ''));

// Mirrors a scanned <img> poster row.
export const posterImage = (video) => ({
  kind: 'img',
  src: video.posterUrl,
  poster: true,
  name: filenameFromUrl(video.posterUrl, 'poster'),
  w: 0, h: 0,
});

// The poster stands in when no frame was captured (an unplayed video's frame 0 is often black).
export const editableSrc = (image) => image.src || image.posterUrl || '';

export const pinnable = (image) => !!sourceOf(image);

// Shared pins obey only the text search; kind / format / size filters do not apply to a
// remote project. An invalid regex matches nothing, as in passesFilters.
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

export const hostLabel = (origin) => {
  try {
    return new URL(origin).host;
  } catch {
    return origin || '(server)';
  }
};
