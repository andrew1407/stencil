// The DOM half of ZoomPan (core/pan.js). Every value is MEASURED live — a fixed inset
// guess leaves a page scrollbar.

// Below these the frame / the panel header would be clipped.
const MIN_VIEWPORT_H = 120;
const MIN_PANEL_H = 120;

// Bottom inset of EVERY ancestor up through <body>: counting only body leaves a page scrollbar.
const bottomInsetToPage = (el) => {
  let total = 0;
  for (let n = el; n && n !== document.documentElement; n = n.parentElement) {
    const c = getComputedStyle(n);
    total += (parseFloat(c.marginBottom) || 0);
    if (n !== el) total += (parseFloat(c.paddingBottom) || 0) + (parseFloat(c.borderBottomWidth) || 0);
  }
  return total;
};

// Where the top edge is LAID OUT, not painted: a rect reads a mid-flight transform too
// (appReveal slides the app 8px down), so every ancestor's translateY comes off.
const layoutTop = (el) => {
  let top = el.getBoundingClientRect().top;
  for (let n = el; n && n !== document.documentElement; n = n.parentElement) {
    const tf = getComputedStyle(n).transform;
    if (!tf || tf === 'none') continue;
    const m = /matrix\(([^)]+)\)/.exec(tf);
    if (m) top -= parseFloat(m[1].split(',')[5]) || 0;
  }
  return top;
};

// What sits under the viewport in its own column: a stretched flex column's bottom EDGE
// counts slack as occupied.
const belowInColumn = (vp) => {
  let total = 0;
  for (let n = vp.nextElementSibling; n; n = n.nextElementSibling) {
    const c = getComputedStyle(n);
    if (c.display === 'none') continue;
    total += n.getBoundingClientRect().height + (parseFloat(c.marginTop) || 0) + (parseFloat(c.marginBottom) || 0);
  }
  return total;
};

// Auto margins centre a canvas SMALLER than the frame (layout/canvasFrame.css), so its
// origin is not the scroll origin; 0 once the canvas overflows.
export const canvasOrigin = () => {
  const c = typeof document !== 'undefined' && document.getElementById('canvas-container');
  return { x: (c && c.offsetLeft) || 0, y: (c && c.offsetTop) || 0 };
};

// The viewport's own top edge to the window bottom, less what sits below it; fullscreen
// takes the window.
export const availContentHeight = () => {
  if (document.body.classList.contains('fullscreen-mode')) return window.innerHeight;
  const vp = document.getElementById('canvas-viewport');
  if (!vp) return Math.max(200, window.innerHeight - 140 - 96);
  const top = layoutTop(vp);
// The viewport's own column, NOT .container: that also encloses the coordinates panel.
  const shell = vp.closest('.canvas-section') || vp.parentElement;
  const below = shell ? belowInColumn(vp) + bottomInsetToPage(shell) : 96;
  return Math.max(MIN_VIEWPORT_H, Math.floor(window.innerHeight - top - below));
};

// clientWidth already excludes a vertical scrollbar; fullscreen takes the window width.
export const availContentWidth = () => {
  if (document.body.classList.contains('fullscreen-mode')) return window.innerWidth;
  const vp = document.getElementById('canvas-viewport');
  const w = vp ? vp.clientWidth : 0;
  return w > 0 ? w : Math.max(300, window.innerWidth - 420);
};

// Vertical border + padding of the border-box viewport: room the image cannot use.
export const viewportChromeY = () => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp) return 0;
  const cs = getComputedStyle(vp);
  return ['borderTopWidth', 'borderBottomWidth', 'paddingTop', 'paddingBottom']
    .reduce((n, k) => n + (parseFloat(cs[k]) || 0), 0);
};

// canvasOrigin() for a zoom level not on screen yet — the rule the auto margins follow.
export const originAt = (canvas, scale) => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp || !canvas) return { x: 0, y: 0 };
  return {
    x: Math.max(0, ((vp.clientWidth || 0) - canvas.width * scale) / 2),
    y: Math.max(0, ((vp.clientHeight || 0) - canvas.height * scale) / 2),
  };
};

// A max-height (not a height) lets `flex: 1 1 auto` fill the column and still stops a
// zoomed-in canvas stretching the page. Scale-independent; fullscreen owns its own box.
export const syncViewportHeight = () => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp || document.body.classList.contains('fullscreen-mode')) return;
  vp.style.maxHeight = availContentHeight() + 'px';
};

// Cap the coordinates panel to its real room so a long point list scrolls INSIDE it
// (#coord-body is overflow-y:auto). Off the LIVE top — a vh guess leaves a scrollbar.
export const syncCoordPanelHeight = () => {
  const panel = document.getElementById('coord-panel');
  if (!panel || document.body.classList.contains('fullscreen-mode')) return;
  const top = layoutTop(panel);
  const avail = Math.floor(window.innerHeight - top - bottomInsetToPage(panel));
  panel.style.maxHeight = Math.max(MIN_PANEL_H, avail) + 'px';
};
