// What a scanned image's row shows; the DOM assembly stays with the panel.
import { formatOf, UNKNOWN_FORMAT } from './filters.js';
import { sourceOf } from './imageModel.js';
import { icon } from './icons.js';

// A data: URI shows only its mime prefix — the whole blob would fill the screen.
export const rowTitle = (image) => {
  const kindLabel = { bg: 'Background image', video: 'Video', img: 'Image' }[image.kind] || 'Image';
  const refOf = (src) => src && src.startsWith('data:')
    ? src.slice(0, src.indexOf(',') + 1 || 32) + '…'
    : src;
  const ref = image.kind === 'video' ? (image.videoUrl || '(in-page video)') : refOf(image.src);
  const hint = image.kind === 'video'
    ? (image.src ? 'Click: open current frame · Double-click: crop frame' : 'Use the ⋯ menu to open the video')
    : 'Click: open in editor · Double-click: quick crop';
  return `${ref}\n\n${kindLabel}\n${hint}`;
};

// '' when nothing is safe to assign: a shared row's bytes are Bearer-authed, and an
// empty src would point the <img> at the page document.
export const thumbInitialSrc = (image, playThumb) =>
  image.src || image.posterUrl || (image.kind === 'video' ? playThumb : '');

export const dimText = (image) => (image.w && image.h ? `${image.w}×${image.h}` : '');

// `html` entries carry a fixed icon from lib/icons.js — never page data.
export const rowBadges = (image, { opened = false } = {}) => {
  const badges = [{ cls: `badge ${image.kind}`, text: image.kind }];
  if (image.poster) {
    badges.push({ cls: 'badge poster', text: 'poster', title: 'A video’s preview (poster) image — independent of its frames' });
  } else if (image.meta) {
    badges.push({ cls: 'badge meta', text: 'icon', title: 'An icon / metadata image — favicon, og:/twitter: preview, manifest icon, or preload' });
  }
  // A video shows its container format, not the frame's jpg.
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

// Pin status is the outline colour alone: 'shared' (gold) = on a connected server,
// matched by source URL; 'pinned' (gray) = local only.
export const rowOutlineClass = (image, { showServerPins = true, sharedSources = null, pinned = false } = {}) => {
  const onServer = (showServerPins !== false)
    && (!!image.shared || !!(sharedSources && sharedSources.has(sourceOf(image))));
  if (onServer) return 'shared';
  return pinned ? 'pinned' : '';
};
