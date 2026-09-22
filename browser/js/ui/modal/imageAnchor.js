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

const liveRect = (id) => {
  const r = document.getElementById(id)?.getBoundingClientRect?.();
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

// The toolbar Image section's Open control. It is a pair that swaps on whether an image is
// open (ui/control/state.js), so take whichever half is showing at the moment of the flight.
export const openImageAnchorRect = () =>
  liveRect('open-image-btn') || liveRect('load-image-btn') || canvasAnchorRect();

// The two rules as one pair of opts for confirmModal: in from the canvas centre, back into
// the Open control when the answer opens an image and back to the canvas centre when not.
export const openImageConfirmAnchors = (opensImage = Boolean) => ({
  openAnchor: canvasAnchorRect(),
  closeAnchor: (answer) => (opensImage(answer) ? openImageAnchorRect() : canvasAnchorRect()),
});
