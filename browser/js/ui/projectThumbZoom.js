import { placeNearCursor, onWindowResize } from '../utils.js';
import {
  surfaceIn, surfaceOut, settleSurface, rectCenter, TIP_DUST_IN_MS, TIP_DUST_OUT_MS,
} from './motion.js';

// ── Magnified hover preview for project-row thumbnails ──────────
// Split out of projectsModal.js's wire(). One factory per modal: it owns the single
// reused floating element and the Alt-doubling state that every row shares.
export const createThumbZoom = () => {
  // Magnified hover preview: a fixed-position floating copy of a row's thumbnail that
  // follows the cursor, shown while hovering a thumb that holds a real image (not the
  // placeholder glyph). One reused element, shared by local + remote rows.
  const PREVIEW_ZOOM = 1.67;
  // A hover preview is a GLANCE, not a lightbox — it must leave the list underneath
  // readable. Mirrored by the max-width/max-height backstop in components.css.
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
  // ── The zoom is sand too (js/ui/motion.js surfaceIn/surfaceOut), on the shared
  // short tip clock — a sweep across rows re-triggers it fast. ──
  const ZOOM_DUST_IN_MS = TIP_DUST_IN_MS;
  const ZOOM_DUST_OUT_MS = TIP_DUST_OUT_MS;
  let zoomPoint = null;   // the thumbnail's own centre — the flight's origin/destination
  const hideZoom = () => {
    if (zoomEl) {
      if (zoomEl.style.display !== 'none') surfaceOut(zoomEl, zoomPoint, { ms: ZOOM_DUST_OUT_MS });
      else settleSurface(zoomEl);
      zoomEl.style.display = 'none';
      zoomSize = null;
    }
    zoomPoint = null;
  };
  // Switching window never fires the row's mouseleave — hide on blur, or the
  // zoom is still up when the user comes back (chatView hideThumbPreview parity).
  window.addEventListener('blur', hideZoom);
  // Holding Alt doubles the glance (every hover preview honours it — chat thumbs,
  // extension, desktop): factor 2 on the zoom cap AND the viewport ceilings.
  let zoomSize = null;   // {nw, nh} of the picture currently zoomed
  let zoomAlt = false;
  // A keyup can be lost off-window — Alt released while a docked DevTools pane (or any
  // other panel) holds keyboard focus never reaches this listener — which would leave
  // the NEXT hover's glance stuck doubled with no key actually held. blur is the one
  // signal that always fires when focus leaves, so it's the backstop that un-sticks it.
  window.addEventListener('blur', () => { zoomAlt = false; });
  // The size change IS a re-formation, not just a resize — replay the gather so
  // holding/releasing Alt reads as sand rather than a snap.
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
    // The xl class doubles the CSS vw/vh caps too — they would clamp the explicit
    // width right back to the un-Alt size otherwise.
    zoomEl.classList.toggle('project-thumb-zoom-xl', zoomAlt);
    const img = zoomEl.querySelector('img');
    img.style.width = `${Math.round(zoomSize.nw * scale)}px`;
    img.style.height = `${Math.round(zoomSize.nh * scale)}px`;
  };
  window.addEventListener('keydown', e => { if (e.key === 'Alt') { zoomAlt = true; applyZoomScale(); replayZoomDust(); } });
  window.addEventListener('keyup', e => { if (e.key === 'Alt') { zoomAlt = false; applyZoomScale(); replayZoomDust(); } });
  // A backstop under applyZoomScale's own vw/vh caps: those track the window at the
  // moment a hover STARTS, so the box is re-cropped into whatever the window actually
  // is — applied on show and on resize, not per mousemove (a style write before every
  // measure forced a layout per move).
  const ZOOM_EDGE = 8;
  const applyZoomCaps = () => {
    if (!zoomEl) return;
    zoomEl.style.maxWidth = `${Math.max(0, window.innerWidth - ZOOM_EDGE * 2)}px`;
    zoomEl.style.maxHeight = `${Math.max(0, window.innerHeight - ZOOM_EDGE * 2)}px`;
  };
  onWindowResize(applyZoomCaps);
  // Down-right of the cursor, flipped/clamped into the viewport (shared helper); an
  // overflowing height pins to the bottom edge rather than flipping above.
  const positionZoom = e => {
    if (!zoomEl) return;
    placeNearCursor(zoomEl, e.clientX, e.clientY, { edge: ZOOM_EDGE, clampY: true });
  };
  const enableThumbZoom = thumbEl => {
    thumbEl.addEventListener('mouseenter', e => {
      const img = thumbEl.querySelector('img');
      if (!img || !img.src) return;   // placeholder glyph — nothing to magnify
      const z = ensureZoom();
      const zImg = z.querySelector('img');
      zImg.src = img.src;
      // Sized here rather than left to the CSS caps: independent max-width/max-height
      // clamping squashes a portrait thumb into a letterboxed landscape box. One scale
      // factor keeps aspect, honours the viewport ceiling, never upscales past PREVIEW_ZOOM.
      zoomSize = {
        nw: img.naturalWidth || img.width || 160,
        nh: img.naturalHeight || img.height || 160,
      };
      zoomAlt = e.altKey;   // Alt already held on entry counts too
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
