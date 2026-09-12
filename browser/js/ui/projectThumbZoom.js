import { placeNearCursor, onWindowResize } from '../utils.js';
import {
  surfaceIn, surfaceOut, settleSurface, rectCenter, TIP_DUST_IN_MS, TIP_DUST_OUT_MS,
} from './motion.js';

// Magnified hover preview for project-row thumbnails: one reused floating element and the
// Alt-doubling state every row shares.
export const createThumbZoom = () => {
  const PREVIEW_ZOOM = 1.67;
  // A glance, not a lightbox. Mirrored by the backstop in components/projects.css.
  const PREVIEW_MAX_VW = 0.25;
  const PREVIEW_MAX_VH = 0.20;
  let zoomEl = null;
  const ensureZoom = () => {
    if (zoomEl) return zoomEl;
    zoomEl = document.createElement('div');
    zoomEl.className = 'project-thumb-zoom';
    zoomEl.innerHTML = '<img alt="">';
    document.body.appendChild(zoomEl);
    return zoomEl;
  };
  // Sand on the shared short tip clock — a sweep across rows re-triggers it fast.
  const ZOOM_DUST_IN_MS = TIP_DUST_IN_MS;
  const ZOOM_DUST_OUT_MS = TIP_DUST_OUT_MS;
  let zoomPoint = null;
  const hideZoom = () => {
    if (zoomEl) {
      if (zoomEl.style.display !== 'none') surfaceOut(zoomEl, zoomPoint, { ms: ZOOM_DUST_OUT_MS });
      else settleSurface(zoomEl);
      zoomEl.style.display = 'none';
      zoomSize = null;
    }
    zoomPoint = null;
  };
  // Switching window never fires the row's mouseleave (chatView hideThumbPreview parity).
  window.addEventListener('blur', hideZoom);
  // Holding Alt doubles the glance (chat thumbs, extension, desktop too): factor 2 on the
  // zoom cap AND the viewport ceilings.
  let zoomSize = null;
  let zoomAlt = false;
  // A keyup is lost when a docked DevTools pane holds focus; blur always fires, so it un-sticks Alt.
  window.addEventListener('blur', () => { zoomAlt = false; });
  // The size change is a re-formation: replay the gather.
  const replayZoomDust = () => {
    if (!zoomEl || zoomEl.style.display === 'none' || !zoomPoint) return;
    surfaceIn(zoomEl, zoomPoint, { ms: ZOOM_DUST_IN_MS });
  };
  const applyZoomScale = () => {
    if (!zoomEl || !zoomSize || zoomEl.style.display === 'none') return;
    const f = zoomAlt ? 2 : 1;
    const scale = Math.min(PREVIEW_ZOOM * f,
      (window.innerWidth * PREVIEW_MAX_VW * f) / zoomSize.nw,
      (window.innerHeight * PREVIEW_MAX_VH * f) / zoomSize.nh);
    // The xl class doubles the CSS vw/vh caps too, or they clamp the width right back.
    zoomEl.classList.toggle('project-thumb-zoom-xl', zoomAlt);
    const img = zoomEl.querySelector('img');
    img.style.width = `${Math.round(zoomSize.nw * scale)}px`;
    img.style.height = `${Math.round(zoomSize.nh * scale)}px`;
  };
  window.addEventListener('keydown', e => { if (e.key === 'Alt') { zoomAlt = true; applyZoomScale(); replayZoomDust(); } });
  window.addEventListener('keyup', e => { if (e.key === 'Alt') { zoomAlt = false; applyZoomScale(); replayZoomDust(); } });
  // Re-crops the box into the window as it actually is; on show and resize, not per
  // mousemove (a style write before every measure forced a layout per move).
  const ZOOM_EDGE = 8;
  const applyZoomCaps = () => {
    if (!zoomEl) return;
    zoomEl.style.maxWidth = `${Math.max(0, window.innerWidth - ZOOM_EDGE * 2)}px`;
    zoomEl.style.maxHeight = `${Math.max(0, window.innerHeight - ZOOM_EDGE * 2)}px`;
  };
  onWindowResize(applyZoomCaps);
  // An overflowing height pins to the bottom edge rather than flipping above.
  const positionZoom = e => {
    if (!zoomEl) return;
    placeNearCursor(zoomEl, e.clientX, e.clientY, { edge: ZOOM_EDGE, clampY: true });
  };
  const enableThumbZoom = thumbEl => {
    thumbEl.addEventListener('mouseenter', e => {
      const img = thumbEl.querySelector('img');
      if (!img || !img.src) return;
      const z = ensureZoom();
      const zImg = z.querySelector('img');
      zImg.src = img.src;
      // One scale factor for both axes: independent max-width/max-height clamping squashes
      // a portrait thumb into a letterboxed landscape box.
      zoomSize = {
        nw: img.naturalWidth || img.width || 160,
        nh: img.naturalHeight || img.height || 160,
      };
      zoomAlt = e.altKey;
      zoomPoint = rectCenter(thumbEl);
      z.style.display = 'block';
      applyZoomCaps();
      applyZoomScale();
      positionZoom(e);
      surfaceIn(z, zoomPoint, { ms: ZOOM_DUST_IN_MS });
    });
    thumbEl.addEventListener('mousemove', positionZoom);
    thumbEl.addEventListener('mouseleave', hideZoom);
  };
  return { enableThumbZoom, hideZoom };
};
