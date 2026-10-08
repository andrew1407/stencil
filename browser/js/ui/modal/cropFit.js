// The crop window's size: the window fitted around its picture inside the viewport on every open,
// and the picture re-fitted to the window's free room on every resize after. Desktop twin:
// dialogs/crop/cropDialogParts.hpp cropWindowSize, CropDialog::fitToScreen and CropPreview's fit.
import { MIN_W, USER_SIZED_ATTR } from './resize.js';

export const VIEW_MARGIN = 20;   // the window keeps this much viewport on every side (resize.js MARGIN_PX)
export const STAGE_MIN = 120;    // the picture's room never shrinks under this, px a side
const PREVIEW_FLOOR = Object.freeze({ width: 760, height: 540 });   // desktop MIN_DISP_W/H

// The largest picture of aspect iw:ih inside roomW × roomH, a small one grown to it; `scale` is
// display px per image px, 0 with no picture or no room.
export const fitContain = (iw, ih, roomW, roomH) => {
  if (!(iw > 0 && ih > 0 && roomW > 0 && roomH > 0)) return { width: 0, height: 0, scale: 0 };
  const scale = Math.min(roomW / iw, roomH / ih);
  return { width: iw * scale, height: ih * scale, scale };
};

// The most a picture opens at: 96% of the viewport's width less 60px by 82% of its height less
// 180px, floored at 760×540 (desktop previewFitBox).
export const previewBox = (viewport) => ({
  width: Math.max(PREVIEW_FLOOR.width, Math.round(viewport.width * 0.96) - 60),
  height: Math.max(PREVIEW_FLOOR.height, Math.round(viewport.height * 0.82) - 180),
});

// The window's opening size around `chrome` (the window less its picture's room): the picture fitted
// into the preview box and what the viewport leaves, at least floorW wide, never past the viewport.
export const cropWindowSize = ({ iw, ih, chrome, viewport, floorW = 0 }) => {
  const room = { width: Math.max(0, viewport.width - 2 * VIEW_MARGIN),
                 height: Math.max(0, viewport.height - 2 * VIEW_MARGIN) };
  const box = previewBox(viewport);
  const fit = fitContain(iw, ih, Math.min(box.width, room.width - chrome.width),
                         Math.min(box.height, room.height - chrome.height));
  return {
    width: Math.min(room.width, Math.max(floorW, Math.ceil(fit.width + chrome.width))),
    height: Math.min(room.height, Math.ceil(fit.height + chrome.height)),
  };
};

// A closed window is display:none and measures nothing: lay it out unseen for one measurement.
const unseen = (overlay, measure) => {
  if (overlay.classList.contains('modal-open')) return measure();
  const { display, visibility } = overlay.style;
  overlay.style.visibility = 'hidden';
  overlay.style.display = 'flex';
  try { return measure(); } finally {
    overlay.style.display = display;
    overlay.style.visibility = visibility;
  }
};

// `fitWindow()` sizes `box` around the picture; `stage`, the picture itself, then follows every size
// change of `frame`, its free room, and hands the new scale to `onScale`.
export const wireCropFit = ({ overlay, box, frame, stage, footer }, dims, onScale) => {
  const fitStage = () => {
    const { iw, ih } = dims();
    const fit = fitContain(iw, ih, frame.clientWidth, frame.clientHeight);
    if (!fit.scale) return false;
    stage.style.width = `${fit.width}px`;
    stage.style.height = `${fit.height}px`;
    onScale(fit.scale);
    return true;
  };
  const chromeAt = (width) => {
    box.style.width = `${width}px`;
    return { width: box.offsetWidth - frame.clientWidth, height: box.offsetHeight - frame.clientHeight };
  };
  // `prepare` runs laid out, before anything is measured: whatever sizes the chrome belongs there.
  const fitWindow = (prepare) => unseen(overlay, () => {
    prepare?.();
    const viewport = { width: window.innerWidth, height: window.innerHeight };
    const { iw, ih } = dims();
    box.style.maxHeight = `calc(100vh - ${2 * VIEW_MARGIN}px)`;
    box.style.width = '';
    footer.style.width = 'max-content';
    // The shell's own width, and the footer on one line inside its borders: the narrowest it opens at.
    const floorW = Math.max(box.offsetWidth, footer.offsetWidth + box.offsetWidth - box.clientWidth);
    footer.style.width = '';
    box.style.height = `${Math.max(0, viewport.height - 2 * VIEW_MARGIN)}px`;
    // resize.js holds an edge drag above this: the narrowest window's chrome over STAGE_MIN.
    box.dataset.minH = String(Math.ceil(chromeAt(MIN_W).height + STAGE_MIN));
    const wide = chromeAt(viewport.width - 2 * VIEW_MARGIN);
    const width = cropWindowSize({ iw, ih, chrome: wide, viewport, floorW }).width;
    const chrome = { width: wide.width, height: chromeAt(width).height };
    box.style.height = `${cropWindowSize({ iw, ih, chrome, viewport, floorW }).height}px`;
    return fitStage();
  });

  // A size the user holds survives a viewport resize while the window still fits; past that it is
  // let go, and the window re-fits and re-centres as on open.
  const onViewport = () => {
    if (!overlay.classList.contains('modal-open')) return;
    if (box.dataset[USER_SIZED_ATTR]) {
      const r = box.getBoundingClientRect();
      if (r.left >= 0 && r.top >= 0 && r.right <= window.innerWidth && r.bottom <= window.innerHeight) return;
      delete box.dataset[USER_SIZED_ATTR];
      box.style.translate = '';
    }
    fitWindow();
  };

  if (typeof ResizeObserver === 'function') new ResizeObserver(() => { fitStage(); }).observe(frame);
  if (typeof window !== 'undefined' && window.addEventListener) window.addEventListener('resize', onViewport);
  return { fitWindow, fitStage, onViewport };
};
