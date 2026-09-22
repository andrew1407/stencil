// The open-image flow's flight anchors, in one place so the desktop mirrors one rule: a
// window or confirm about opening an image grows out of the CANVAS CENTRE, whatever gesture
// raised it, and an answer that opens an image pours back into the toolbar's Open control.

// Side of the small box a flight grows out of or pours into.
export const IMAGE_ANCHOR_PX = 40;

const boxAt = (cx, cy) => {
  const h = IMAGE_ANCHOR_PX / 2;
  return { left: cx - h, top: cy - h, right: cx + h, bottom: cy + h,
           width: IMAGE_ANCHOR_PX, height: IMAGE_ANCHOR_PX };
};

// The outcome's swap is still in the air when the flight is aimed — the half taking over is
// display:none or clipped by its slide — so the toolbar is held in that state for the measure.
const REVEAL_CLASS = 'reveal-group-transition';
const LOADED = { show: 'open-image-btn', hide: 'load-image-btn' };
const EMPTY = { show: 'load-image-btn', hide: 'image-actions' };

const settledRect = (el, state) => {
  if (!el?.getBoundingClientRect) return null;
  const held = [];
  const hold = (n) => { held.push([n, n.style.display, n.style.maxWidth, n.style.maxHeight]); };
  const lift = (from) => {
    for (let n = from; n && n !== document.body; n = n.parentElement) {
      if (!n.classList?.contains(REVEAL_CLASS) && n.style?.display !== 'none') continue;
      hold(n);
      if (n.style.display === 'none') n.style.display = '';
      n.style.maxWidth = 'none';
      n.style.maxHeight = 'none';
    }
  };
  const gone = byId(state.hide);
  if (gone) { hold(gone); gone.style.display = 'none'; }
  lift(byId(state.show));
  lift(el);
  const r = el.getBoundingClientRect();
  for (let i = held.length - 1; i >= 0; i--) {
    const [n, d, w, h] = held[i];
    n.style.display = d; n.style.maxWidth = w; n.style.maxHeight = h;
  }
  return r.width > 0 && r.height > 0 ? r : null;
};

const byId = (id) => document.getElementById(id);
const liveRect = (id) => {
  const r = byId(id)?.getBoundingClientRect?.();
  return r && r.width > 0 && r.height > 0 ? r : null;
};

// The centre of the canvas area. An empty editor still has the viewport, but before the page
// is laid out it measures nothing, so the window's own centre stands in.
export const canvasAnchorRect = () => {
  const r = liveRect('canvas-viewport');
  const cx = r ? r.left + r.width / 2 : (globalThis.window?.innerWidth || 0) / 2;
  const cy = r ? r.top + r.height / 2 : (globalThis.window?.innerHeight || 0) / 2;
  return boxAt(cx, cy);
};

// The toolbar Image section's Open control (ui/control/state.js): which half by the OUTCOME the
// flight carries, never by what measures now.
export const openImageAnchorRect = (imageOpen = true) => {
  const state = imageOpen ? LOADED : EMPTY;
  return settledRect(byId(state.show), state)
      || liveRect(state.show) || liveRect(state.hide) || canvasAnchorRect();
};

// Where a toolbar control sits once the editor is EMPTY: the same aim-before-the-sweep problem,
// for a control that stays put while the Image section shrinking under it moves the row.
export const emptiedControlRect = (id) =>
  settledRect(byId(id), EMPTY) || liveRect(id) || canvasAnchorRect();

// The two rules as one pair of opts for confirmModal: in from the canvas centre, back into
// the Open control when the answer opens an image and back to the canvas centre when not.
export const openImageConfirmAnchors = (opensImage = Boolean) => ({
  openAnchor: canvasAnchorRect(),
  closeAnchor: (answer) => (opensImage(answer) ? openImageAnchorRect() : canvasAnchorRect()),
});
