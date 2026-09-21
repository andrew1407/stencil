import { fetchAsDataUrl } from '../../lib/stencil.js';
import { editableSrc } from '../../lib/image/imageModel.js';
import { createHoverPreview } from '../../lib/highlight/hoverPreview.js';
import { previewEl, previewImg, THUMB_PX } from '../panelDom.js';
import { rowResource } from '../list/model.js';

export const preview = createHoverPreview({
  previewEl, previewImg, thumbPx: THUMB_PX,
  fetchDataUrl: (src, pageUrl) => fetchAsDataUrl(src, { pageUrl }),
  getSrc: editableSrc,
  getPageUrl: rowResource,
});
// Shared with the thumbnails' recovery path and the shared rows' hand-off.
export const previewCache = preview.cache;
export const bindPreview = preview.bind;
export const bindDataUrlPreview = preview.bindDataUrl;
export const hidePreview = preview.hide;
// A drag suppresses the source element's mouseleave.
for (const type of ['dragstart', 'dragend', 'drop']) document.addEventListener(type, hidePreview, true);
