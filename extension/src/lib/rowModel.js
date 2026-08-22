// ── The list rows' display model (pure) ─────────────────────────────────────
// What a scanned image's row SHOWS — tooltip, initial thumbnail source, badge pills,
// outline colour — extracted from popup.js's renderRow so the row semantics are
// unit-testable; the DOM assembly stays with the panel.
import { formatOf, UNKNOWN_FORMAT } from './filters.js';
import { sourceOf } from './imageModel.js';
import { icon } from './icons.js';

// The name's tooltip: where the image came from + the row's gestures. An embedded
// data: URI is a huge base64 blob — show just its short mime prefix, never the whole
// thing (it would otherwise fill the screen, as the native title did).
export const rowTitle = (image) => {
  const kindLabel = { bg: 'Background image', video: 'Video', img: 'Image' }[image.kind] || 'Image';
  const refOf = (src) => src && src.startsWith('data:')
    ? src.slice(0, src.indexOf(',') + 1 || 32) + '…'   // e.g. "data:image/jpeg;base64,…"
    : src;
  const ref = image.kind === 'video' ? (image.videoUrl || '(in-page video)') : refOf(image.src);
  const hint = image.kind === 'video'
    ? (image.src ? 'Click: open current frame · Double-click: crop frame' : 'Use the ⋯ menu to open the video')
    : 'Click: open in editor · Double-click: quick crop';
  return `${ref}\n\n${kindLabel}\n${hint}`;
};

// The thumbnail's initial source, or '' when there is nothing safe to assign — a shared
// (server) row's bytes are Bearer-authed, and '' as a src would point the <img> at the
// page document. A frameless video gets the caller's play-glyph placeholder.
export const thumbInitialSrc = (image, playThumb) =>
  image.src || image.posterUrl || (image.kind === 'video' ? playThumb : '');

export const dimText = (image) => (image.w && image.h ? `${image.w}×${image.h}` : '');

// The row's badge pills, in order: kind, poster/meta provenance, format, and the
// opened flag. `html` entries carry a fixed icon from lib/icons.js — never page data.
export const rowBadges = (image, { opened = false } = {}) => {
  const badges = [{ cls: `badge ${image.kind}`, text: image.kind }];   // .bg/.video styled; .img neutral
  // A video poster lists as a normal image; tag it so it's distinct from the sibling
  // video (frame) row it was split from. `meta` is page furniture (favicon, og: preview…).
  if (image.poster) {
    badges.push({ cls: 'badge poster', text: 'poster', title: 'A video’s preview (poster) image — independent of its frames' });
  } else if (image.meta) {
    badges.push({ cls: 'badge meta', text: 'icon', title: 'An icon / metadata image — favicon, og:/twitter: preview, manifest icon, or preload' });
  }
  // A video shows its container format (from the media URL), not the frame's jpg;
  // in-page (blob) videos fall back to a plain "video" tag.
  badges.push({
    cls: 'badge fmt',
    text: image.kind === 'video' ? (formatOf(image.videoUrl) || 'video') : (formatOf(image.src) || UNKNOWN_FORMAT),
  });
  if (opened) {
    badges.push({
      cls: 'badge opened',
      html: icon('flag', { size: 12 }) + ' opened',
      title: 'Already opened in an editor — click to resume or add a copy',
    });
  }
  return badges;
};

// Pin status is conveyed by the row OUTLINE COLOUR alone (no text badge): GOLD
// ('shared') = stored on a connected server (a shared row, or a local pin whose image
// is also on a server, matched by source URL); GRAY ('pinned') = pinned locally only.
export const rowOutlineClass = (image, { showServerPins = true, sharedSources = null, pinned = false } = {}) => {
  const onServer = (showServerPins !== false)
    && (!!image.shared || !!(sharedSources && sharedSources.has(sourceOf(image))));
  if (onServer) return 'shared';
  return pinned ? 'pinned' : '';
};
