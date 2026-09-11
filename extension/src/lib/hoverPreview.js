// ── The floating hover preview (the rows' magnifier card) ───────────────────
// One shared card per surface: debounced so a sweep across rows doesn't strobe cards,
// token-guarded so a pointer that moves on beats a slow fetch, and a tiny-source memo so
// a thumbnail-sized image never flashes an empty card twice. Extracted from popup.js.
//
// ── The card is sand too (lib/motion.js surfaceIn/surfaceOut) ───────────────
// It forms from motes streaming out of the row it previews and pours back into it. Its
// own short clock (as in lib/controlTooltip.js): a sweep across rows re-triggers fast, so
// a flight must end before the next begins — and an UPGRADE (the small source swapped for
// the fetched one) replays the gather rather than snapping.
import {
  surfaceIn, surfaceOut, settleSurface, centerOf, TIP_DUST_IN_MS, TIP_DUST_OUT_MS,
} from './motion.js';

const GAP = 12;

/** Card placement beside a hovered row: right of it, flipping left / clamping up on overflow. */
export const previewPosition = ({ anchor, size, viewport }) => {
  let left = anchor.right + GAP;
  if (left + size.width > viewport.width) left = Math.max(GAP, anchor.left - size.width - GAP);
  let top = anchor.top;
  if (top + size.height > viewport.height) top = Math.max(GAP, viewport.height - size.height - GAP);
  return { left, top };
};

// Skip the preview when the image is no bigger than its row thumbnail (nothing larger
// to reveal). Unmeasured images (w/h = 0) still get a preview.
export const previewWorthwhile = (image, thumbPx) =>
  !(image.w > 0 && image.h > 0 && image.w <= thumbPx && image.h <= thumbPx);

/**
 * Wire the shared preview card. `thumbPx` is the row-thumbnail size a preview must beat;
 * `fetchDataUrl` is the host-permission fetch, since a bare <img src> can't load a
 * hotlink-protected source; `getSrc` returns an image's previewable source ('' = none).
 */
export const createHoverPreview = ({
  previewEl, previewImg, thumbPx, fetchDataUrl, getSrc,
  getPageUrl = () => '', win = window, debounceMs = 100,
}) => {
  // source → data URL, so re-hovering is instant and each source is fetched at most
  // once. Exposed: the row thumbnails' recovery path and shared rows reuse it.
  const cache = new Map();
  // Sources whose bytes measured no bigger than the row thumbnail — re-hovering them
  // skips the preview outright instead of flashing an empty card again.
  const tiny = new Set();
  let srcKey = '';        // the source behind the currently loading preview
  let showTimer = null;
  let anchor = null;      // the row the card is anchored to (repositioned on late size)
  let token = 0;          // drops a stale async fetch when the pointer moves on
  let dustPoint = null;   // the row's own centre — the card's dust origin/destination

  // The preview only fires once the pointer SETTLES on a row for a beat.
  const schedule = (fn) => {
    clearTimeout(showTimer);
    showTimer = setTimeout(fn, debounceMs);
  };

  const position = (el) => {
    const p = previewPosition({
      anchor: el.getBoundingClientRect(),
      size: { width: previewEl.offsetWidth || 270, height: previewEl.offsetHeight || 270 },
      viewport: { width: win.innerWidth, height: win.innerHeight },
    });
    previewEl.style.left = `${p.left}px`;
    previewEl.style.top = `${p.top}px`;
  };

  const resolveSrc = async (src, pageUrl) => {
    if (src.startsWith('data:')) return src;
    if (cache.has(src)) return cache.get(src);
    const dataUrl = await fetchDataUrl(src, pageUrl);
    cache.set(src, dataUrl);
    return dataUrl;
  };

  // Dust the card out and hide it — the cloud is what it leaves behind, so it is
  // photographed while still the box on screen, not after. A card that was never
  // shown (the tiny/undecodable bail-outs below) just goes, nothing to leave behind.
  const dustHide = () => {
    if (!previewEl.hidden && dustPoint) {
      if (!surfaceOut(previewEl, dustPoint, { ms: TIP_DUST_OUT_MS })) settleSurface(previewEl);
    } else settleSurface(previewEl);
    previewEl.hidden = true;
    dustPoint = null;
  };

  // Reveal the card beside `el` — the row's centre becomes the dust origin/destination.
  // Returns whether this was a fresh show (the only edge that flies — see dustIn).
  const revealAt = (el) => {
    const wasHidden = previewEl.hidden;
    dustPoint = centerOf(el);
    previewEl.hidden = false;
    position(el);
    return wasHidden;
  };
  // Gather the card from the dust point, settling instantly when the flight declines.
  const dustIn = () => {
    if (!surfaceIn(previewEl, dustPoint, { ms: TIP_DUST_IN_MS })) settleSurface(previewEl);
  };

  // Reposition once the real dimensions are known; hide when the bytes turn out no
  // bigger than the row thumbnail (nothing to reveal) or won't decode.
  previewImg.addEventListener('load', () => {
    if (previewImg.naturalWidth > 0 && previewImg.naturalWidth <= thumbPx &&
        previewImg.naturalHeight > 0 && previewImg.naturalHeight <= thumbPx) {
      if (srcKey) tiny.add(srcKey);
      dustHide();
      return;
    }
    if (anchor) position(anchor);
  });
  previewImg.addEventListener('error', dustHide);

  // Hide the card. A drag started from a thumbnail suppresses its mouseleave — which
  // would pin the card over the panel for the whole drag — so the owner also wires this
  // to any drag start/end/drop on the surface.
  const hide = () => {
    clearTimeout(showTimer);   // a pending debounced show must not fire late
    token++;                   // cancel any in-flight fetch for this row
    anchor = null;
    dustHide();
  };

  // The magnifier for a source that is ALREADY bytes (an editor row's canvas capture):
  // show `small` at once, then swap in whatever `bigger()` resolves. A pointer that
  // moves on first (the token) wins over a late upgrade.
  const bindDataUrl = (el, small, bigger) => {
    el.addEventListener('mouseenter', () => schedule(async () => {
      const t = ++token;
      anchor = el;
      srcKey = '';   // canvas captures are transient — never memo them as tiny
      previewImg.src = small;
      if (revealAt(el)) dustIn();
      if (typeof bigger !== 'function') return;
      let big = '';
      try { big = await bigger(); } catch { big = ''; }
      if (!big || t !== token) return;
      previewImg.src = big;
      position(el);
      // The upgrade is a genuine content swap — replay the gather rather than snap.
      dustIn();
    }));
    el.addEventListener('mouseleave', hide);
  };

  const bind = (el, image) => {
    el.addEventListener('mouseenter', () => schedule(async () => {
      const ps = getSrc(image);
      if (!ps || tiny.has(ps) || !previewWorthwhile(image, thumbPx)) return;   // nothing to preview
      const t = ++token;
      anchor = el;
      srcKey = ps;
      let src = ps;
      try {
        src = await resolveSrc(ps, getPageUrl(image));
      } catch {
        src = ps;   // fall back to a direct load; the error handler hides a void box
      }
      if (t !== token) return;   // pointer already moved on
      previewImg.src = src;
      // Decode BEFORE showing — undecodable / thumbnail-sized bytes show no card.
      try { await previewImg.decode(); } catch { tiny.add(ps); return; }
      if (t !== token) return;
      const nw = previewImg.naturalWidth;
      const nh = previewImg.naturalHeight;
      if (nw > 0 && nh > 0 && nw <= thumbPx && nh <= thumbPx) { tiny.add(ps); return; }
      const wasHidden = revealAt(el);
      // A 0×0-intrinsic SVG can render collapsed — hide the bare padding pill.
      const box = previewImg.getBoundingClientRect();
      if (box.width < 24 || box.height < 24) {
        previewEl.hidden = true;
        dustPoint = null;
        tiny.add(ps);
        return;
      }
      if (wasHidden) dustIn();
    }));
    el.addEventListener('mouseleave', hide);
  };

  return { bind, bindDataUrl, hide, cache };
};
