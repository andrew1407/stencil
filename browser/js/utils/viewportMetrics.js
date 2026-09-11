// ── Viewport metrics: what the canvas frame and coord panel measure ──
// The DOM half of ZoomPan (core/zoomPan.js), so the zoom logic there holds no queries of
// its own. Every value is MEASURED live — a fixed inset guess leaves a page scrollbar.

// Floors: below these the frame / the panel header would be clipped, and on a window
// that short something has to give anyway.
const MIN_VIEWPORT_H = 120;
const MIN_PANEL_H = 120;

// Bottom padding + border + margin of EVERY ancestor up through <body>: counting only
// body over-reports the room and leaves a permanent page scrollbar.
const bottomInsetToPage = (el) => {
  let total = 0;
  for (let n = el; n && n !== document.documentElement; n = n.parentElement) {
    const c = getComputedStyle(n);
    total += (parseFloat(c.marginBottom) || 0);
    if (n !== el) total += (parseFloat(c.paddingBottom) || 0) + (parseFloat(c.borderBottomWidth) || 0);
  }
  return total;
};

// Where `el`'s top edge is LAID OUT, not painted: a bounding rect reads a mid-flight
// transform too (appReveal slides the app 8px down), so every ancestor's translateY comes off.
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

// What sits under the viewport in its own column (status line + drop hint), summed from the
// siblings: the column is a stretched flex box, so its bottom EDGE counts slack as occupied.
const belowInColumn = (vp) => {
  let total = 0;
  for (let n = vp.nextElementSibling; n; n = n.nextElementSibling) {
    const c = getComputedStyle(n);
    if (c.display === 'none') continue;
    total += n.getBoundingClientRect().height + (parseFloat(c.marginTop) || 0) + (parseFloat(c.marginBottom) || 0);
  }
  return total;
};

// Where the image starts inside the viewport's scroll content. Auto margins centre a canvas
// SMALLER than the frame (layout.css), so its origin is not the scroll origin — every
// viewport→image conversion subtracts this or a focal zoom lands a viewport away. It is 0
// once the canvas overflows, which is the only time a scroll offset can be non-zero.
export const canvasOrigin = () => {
  const c = typeof document !== 'undefined' && document.getElementById('canvas-container');
  return { x: (c && c.offsetLeft) || 0, y: (c && c.offsetTop) || 0 };
};

// Room for the canvas viewport: its own top edge to the window bottom, less what sits
// below it. Measured live so it tracks the real toolbar height; fullscreen takes the window.
export const availContentHeight = () => {
  if (document.body.classList.contains('fullscreen-mode')) return window.innerHeight;
  const vp = document.getElementById('canvas-viewport');
  if (!vp) return Math.max(200, window.innerHeight - 140 - 96);
  const top = layoutTop(vp);
  // Off the viewport's own column, NOT .container: that also encloses the coordinates
  // panel, which grows with the point list.
  const shell = vp.closest('.canvas-section') || vp.parentElement;
  // Everything between the shell's bottom edge and the page bottom counts too.
  const below = shell ? belowInColumn(vp) + bottomInsetToPage(shell) : 96;
  // The floor stays below ordinary window heights so the true fit wins.
  return Math.max(MIN_VIEWPORT_H, Math.floor(window.innerHeight - top - below));
};

// Measured off the viewport for the same reason as the height. clientWidth already
// excludes a vertical scrollbar; fullscreen takes the window width.
export const availContentWidth = () => {
  if (document.body.classList.contains('fullscreen-mode')) return window.innerWidth;
  const vp = document.getElementById('canvas-viewport');
  const w = vp ? vp.clientWidth : 0;
  return w > 0 ? w : Math.max(300, window.innerWidth - 420);
};

// Vertical border + padding of the border-box viewport: room availContentHeight() counts
// but the image cannot use. Counted once here, not as a magic constant at each call site.
export const viewportChromeY = () => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp) return 0;
  const cs = getComputedStyle(vp);
  return ['borderTopWidth', 'borderBottomWidth', 'paddingTop', 'paddingBottom']
    .reduce((n, k) => n + (parseFloat(cs[k]) || 0), 0);
};

// canvasOrigin() for a zoom level that is not on screen yet: half the free space, or
// nothing once the image outgrows the frame — the rule the auto margins follow.
export const originAt = (canvas, scale) => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp || !canvas) return { x: 0, y: 0 };
  return {
    x: Math.max(0, ((vp.clientWidth || 0) - canvas.width * scale) / 2),
    y: Math.max(0, ((vp.clientHeight || 0) - canvas.height * scale) / 2),
  };
};

// The frame ALWAYS takes the whole available height and centres the picture with auto
// margins; hugging the image collapsed it to a strip when zoomed out. A max-height (not a
// height) lets `flex: 1 1 auto` fill the column, so it follows a toolbar fold smoothly and
// still stops a zoomed-in canvas stretching the page. Scale-independent — never per step.
// No-op in fullscreen, where the layer owns the box.
export const syncViewportHeight = () => {
  const vp = document.getElementById('canvas-viewport');
  if (!vp || document.body.classList.contains('fullscreen-mode')) return;
  vp.style.maxHeight = availContentHeight() + 'px';
};

// Cap the coordinates panel to its real room so a long point list scrolls INSIDE it
// (#coord-body is overflow-y:auto). Off the panel's LIVE top — a vh guess leaves a scrollbar.
export const syncCoordPanelHeight = () => {
  const panel = document.getElementById('coord-panel');
  if (!panel || document.body.classList.contains('fullscreen-mode')) return;
  const top = layoutTop(panel);
  const avail = Math.floor(window.innerHeight - top - bottomInsetToPage(panel));
  panel.style.maxHeight = Math.max(MIN_PANEL_H, avail) + 'px';
};
