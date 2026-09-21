// The Open-Image dialog's crop overlay: the rect, its box and shade, the read-out, the
// Album/Portrait face, and the drag. Split out of openImageModal.js, which owns the tabs
// and the media instead. Shaped like the extension's createCropStage: the caller keeps the
// state object and this draws it, so nothing here has to know which tab it belongs to.
// Desktop twin: OpenImageDialog's CropPreview.
import { cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, swapCropOrientation }
  from '../../core/cropGeometry.js';
import { tweenRect } from '../motion/rectTween.js';
import { icon } from '../icons.js';
import { pinWidestFace } from '../motion.js';

// All rect math in ORIGINAL-image pixels (the natural pixels of the imported still);
// `scale` is the only screen-space number, and `iw`/`ih` the picture the rect belongs to.
export const freshCropState = () => ({
  rect: { x: 0, y: 0, width: 0, height: 0 }, album: false, aspect: 1, scale: 1, iw: 0, ih: 0,
});

// A rect worth opening with: anything smaller is "no crop fitted yet", not a 0×0 crop.
export const hasCropRect = (state) => state.rect.width >= 1 && state.rect.height >= 1;

/**
 * @param {object} args - `state` (see freshCropState), `els` = {box, shade, dims, orient},
 *   `pageDims()` the page the aspect is locked to, `media()` the element the rect is drawn
 *   ON (for a video that is the player itself), `onDrag()` after every drag frame.
 */
export const createCropOverlay = ({ state, els, pageDims, media, onDrag }) => {
  const { box, shade, dims, orient } = els;

  // The orientation flip's own rect flight; any plain render settles it (rectTween.js).
  let flight = null;
  const settle = () => { if (flight) { flight(); flight = null; } };

  const paint = (r) => {
    box.style.left = (r.x * state.scale) + 'px';
    box.style.top = (r.y * state.scale) + 'px';
    box.style.width = (r.width * state.scale) + 'px';
    box.style.height = (r.height * state.scale) + 'px';
    shade.style.left = box.style.left;
    shade.style.top = box.style.top;
    shade.style.width = box.style.width;
    shade.style.height = box.style.height;
  };

  // render runs per drag frame, so rewrite the glyph only when the face changes — it rebuilds the
  // SVG and drops any turn. Pinned to the wider face (desktop: OpenImageDialog takes the max).
  let shownAlbum = null;
  const renderOrientFace = () => {
    if (shownAlbum === state.album) return;
    shownAlbum = state.album;
    pinWidestFace(orient, [
      icon('swap', { size: 14 }) + '<span>Album</span>',
      icon('swap', { size: 14 }) + '<span>Portrait</span>',
    ]);
    orient.innerHTML = icon('swap', { size: 14 }) + `<span>${state.album ? 'Album' : 'Portrait'}</span>`;
  };

  const render = () => {
    settle();
    box.style.display = 'block';
    shade.style.display = 'block';
    paint(state.rect);
    dims.textContent = `${Math.round(state.rect.width)} × ${Math.round(state.rect.height)} px · ${state.album ? 'Album (landscape)' : 'Portrait'}`;
    renderOrientFace();
  };

  const hide = () => {
    settle();
    box.style.display = 'none';
    shade.style.display = 'none';
  };

  const computeScale = () => {
    const r = media().getBoundingClientRect();
    state.scale = state.iw > 0 && r.width > 0 ? r.width / state.iw : 1;
  };

  // swapCropOrientation carries the user's framing across the flip (no rect yet falls back to
  // centeredCrop). `fly` eases the box from its old shape (desktop twin: CropPreview::setAlbum).
  const recenter = (fly = false) => {
    const from = { ...state.rect };
    state.aspect = cropAspect(pageDims().width, pageDims().height, state.album);
    state.rect = swapCropOrientation(state.rect, state.aspect, state.iw, state.ih);
    render();
    if (fly && from.width >= 1) flight = tweenRect(from, state.rect, paint);
  };

  // Unlike the orientation flip, an arbitrary new aspect has no reciprocal to carry the old box
  // across, so it resets to the same fresh default a first Crop tick gets.
  const fitToPage = () => {
    if (!state.iw || !state.ih) return false;
    state.aspect = cropAspect(pageDims().width, pageDims().height, state.album);
    state.rect = centeredCrop(state.iw, state.ih, state.aspect);
    render();
    return true;
  };

  // Move / corner-resize over the preview (mirrors cropModal).
  const toImage = (clientX, clientY) => {
    const r = media().getBoundingClientRect();
    return { x: (clientX - r.left) / state.scale, y: (clientY - r.top) / state.scale };
  };
  let drag = null;   // { kind: 'move'|'resize', corner, startImg, startRect }
  const onMove = (e) => {
    if (!drag) return;
    const cur = toImage(e.clientX, e.clientY);
    state.rect = drag.kind === 'move'
      ? moveCropClamped(drag.startRect, cur.x - drag.startImg.x, cur.y - drag.startImg.y, state.iw, state.ih)
      : resizeCropFromCorner(drag.startRect, drag.corner, cur.x, cur.y, state.aspect, state.iw, state.ih);
    onDrag?.();
    render();
  };
  const onUp = () => {
    drag = null;
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
  };
  const onDown = (e, kind, corner) => {
    e.preventDefault();
    e.stopPropagation();
    drag = { kind, corner, startImg: toImage(e.clientX, e.clientY), startRect: { ...state.rect } };
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  };
  box.addEventListener('mousedown', (e) => onDown(e, 'move'));
  box.querySelectorAll('.crop-handle').forEach((h) =>
    h.addEventListener('mousedown', (e) => onDown(e, 'resize', parseInt(h.dataset.corner, 10))));

  return { render, hide, recenter, fitToPage, computeScale, settle };
};
