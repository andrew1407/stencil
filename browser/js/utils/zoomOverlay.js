// The DOM writers ZoomPan (core/pan.js) drives; #zoom-rect-overlay serves both the
// zoom-to-rect marquee and the rectangle being drawn.

export const updateZoomRectOverlay = (s, en) => {
  const overlay = document.getElementById('zoom-rect-overlay');
  if (!overlay || !s || !en) return;
  const x1 = Math.min(s.cssX, en.cssX);
  const y1 = Math.min(s.cssY, en.cssY);
  const w = Math.abs(en.cssX - s.cssX);
  const h = Math.abs(en.cssY - s.cssY);
  overlay.style.left = x1 + 'px';
  overlay.style.top = y1 + 'px';
  overlay.style.width = w + 'px';
  overlay.style.height = h + 'px';
  overlay.style.display = 'block';
};

export const hideZoomRectOverlay = () => {
  const overlay = document.getElementById('zoom-rect-overlay');
  if (overlay) overlay.style.display = 'none';
};

export const updateRectDrawOverlay = (s, en) => {
  const overlay = document.getElementById('zoom-rect-overlay');
  if (!overlay || !s || !en) return;
  overlay.style.left = Math.min(s.cssX, en.cssX) + 'px';
  overlay.style.top = Math.min(s.cssY, en.cssY) + 'px';
  overlay.style.width = Math.abs(en.cssX - s.cssX) + 'px';
  overlay.style.height = Math.abs(en.cssY - s.cssY) + 'px';
  overlay.style.display = 'block';
};

// Every zoom-percent input (the original plus fullscreen clones), except the one being edited.
export const setZoomInputValue = (percent) => {
  const inputs = document.querySelectorAll('[id="zoom-input"]');
  inputs.forEach(el => {
    if (document.activeElement === el) return;
    el.value = percent;
  });
};
