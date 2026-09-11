import { fetchAsDataUrl } from '../lib/stencil.js';
import { editableSrc } from '../lib/imageModel.js';
import { createHoverPreview } from '../lib/hoverPreview.js';
import { previewEl, previewImg, THUMB_PX } from './panelDom.js';
import { rowResource } from './model.js';

// ── Hover preview ──
// The shared magnifier card (lib/hoverPreview.js): debounce, stale-fetch token, tiny-source
// memo and placement live there; this wires the panel's DOM and fetch path in.
export const preview = createHoverPreview({
  previewEl, previewImg, thumbPx: THUMB_PX,
  fetchDataUrl: (src, pageUrl) => fetchAsDataUrl(src, { pageUrl }),
  getSrc: editableSrc,
  getPageUrl: rowResource,
});
// The source → data-URL cache is shared: the row thumbnails' recovery path and the
// shared rows' hand-off reuse bytes a hover already fetched (and vice versa).
export const previewCache = preview.cache;
export const bindPreview = preview.bind;
export const bindDataUrlPreview = preview.bindDataUrl;
export const hidePreview = preview.hide;
// A drag suppresses the source element's mouseleave — clear the card on any drag activity.
for (const type of ['dragstart', 'dragend', 'drop']) document.addEventListener(type, hidePreview, true);
