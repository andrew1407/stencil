// The rows' shared magnifier card: debounced, token-guarded against a slow fetch, and
// memoising thumbnail-sized sources so they never flash an empty card twice. It forms
// from motes out of the row it previews (lib/motion.js surfaceIn/surfaceOut).
import {
  surfaceIn, surfaceOut, settleSurface, centerOf, TIP_DUST_IN_MS, TIP_DUST_OUT_MS,
} from '../motion.js';

const GAP = 12;

// Right of the row, flipping left / clamping up on overflow.
export const previewPosition = ({ anchor, size, viewport }) => {
  let left = anchor.right + GAP;
  if (left + size.width > viewport.width) left = Math.max(GAP, anchor.left - size.width - GAP);
  let top = anchor.top;
  if (top + size.height > viewport.height) top = Math.max(GAP, viewport.height - size.height - GAP);
  return { left, top };
};

// Unmeasured images (w/h = 0) still get a preview.
export const previewWorthwhile = (image, thumbPx) =>
  !(image.w > 0 && image.h > 0 && image.w <= thumbPx && image.h <= thumbPx);

// `fetchDataUrl` is the host-permission fetch: a bare <img src> cannot load a
// hotlink-protected source.
export const createHoverPreview = ({
  previewEl, previewImg, thumbPx, fetchDataUrl, getSrc,
  getPageUrl = () => '', win = window, debounceMs = 100,
}) => {
  const cache = new Map();   // source → data URL; the row thumbnails' recovery path reuses it
  const tiny = new Set();    // sources whose bytes measured no bigger than the row thumbnail
  let srcKey = '';
  let showTimer = null;
  let anchor = null;
  let token = 0;             // drops a stale async fetch when the pointer moves on
  let dustPoint = null;      // the row's own centre — the card's dust origin/destination

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

  // The cloud is photographed while the card is still the box on screen, not after.
  const dustHide = () => {
    if (!previewEl.hidden && dustPoint) {
      if (!surfaceOut(previewEl, dustPoint, { ms: TIP_DUST_OUT_MS })) settleSurface(previewEl);
    } else settleSurface(previewEl);
    previewEl.hidden = true;
    dustPoint = null;
  };

  // Returns whether this was a fresh show — the only edge that flies.
  const revealAt = (el) => {
    const wasHidden = previewEl.hidden;
    dustPoint = centerOf(el);
    previewEl.hidden = false;
    position(el);
    return wasHidden;
  };
  const dustIn = () => {
    if (!surfaceIn(previewEl, dustPoint, { ms: TIP_DUST_IN_MS })) settleSurface(previewEl);
  };

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

  // A drag started from a thumbnail suppresses its mouseleave, so the owner also wires
  // this to drag start/end/drop.
  const hide = () => {
    clearTimeout(showTimer);
    token++;
    anchor = null;
    dustHide();
  };

  // For a source that is already bytes: show `small` at once, then swap in `bigger()`.
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
      dustIn();
    }));
    el.addEventListener('mouseleave', hide);
  };

  const bind = (el, image) => {
    el.addEventListener('mouseenter', () => schedule(async () => {
      const ps = getSrc(image);
      if (!ps || tiny.has(ps) || !previewWorthwhile(image, thumbPx)) return;
      const t = ++token;
      anchor = el;
      srcKey = ps;
      let src = ps;
      try {
        src = await resolveSrc(ps, getPageUrl(image));
      } catch {
        src = ps;   // fall back to a direct load; the error handler hides a void box
      }
      if (t !== token) return;
      previewImg.src = src;
      // Decode before showing: undecodable / thumbnail-sized bytes show no card.
      try { await previewImg.decode(); } catch { tiny.add(ps); return; }
      if (t !== token) return;
      const nw = previewImg.naturalWidth;
      const nh = previewImg.naturalHeight;
      if (nw > 0 && nh > 0 && nw <= thumbPx && nh <= thumbPx) { tiny.add(ps); return; }
      const wasHidden = revealAt(el);
      // A 0×0-intrinsic SVG renders collapsed — no bare padding pill.
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
