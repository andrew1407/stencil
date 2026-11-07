// The DOM half of ZoomPan (core/zoom/pan.js). Every value is MEASURED live — a fixed inset
// guess leaves a page scrollbar.
import { isStacked } from './dom.js';

// Below these the frame / the panel header would be clipped.
const MIN_VIEWPORT_H = 120;
const MIN_PANEL_H = 120;
// Stacked (the panel under the canvas, the page scrolling), the frame and the panel each take at least this
// share of the window, whatever the toolbar's height: a workspace, and room for the "+ Blank image" card.
const STACKED_SHARE = 0.5;
const stackedFloor = (min) => Math.max(min, Math.floor(window.innerHeight * STACKED_SHARE));

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

// The scrolling frame around the canvas; null before the shell is built.
export const canvasViewport = () => (typeof document !== 'undefined' && document.getElementById('canvas-viewport')) || null;

// Auto margins centre a canvas SMALLER than the frame (layout/canvas/frame.css), so its
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
  const stacked = isStacked();
  // Stacked, from the frame's place in the DOCUMENT: the page scrolls, and the scroll offset would feed back.
  const top = layoutTop(vp) + (stacked ? window.scrollY || 0 : 0);
// The viewport's own column, NOT .container: that also encloses the coordinates panel.
  const shell = vp.closest('.canvas-section') || vp.parentElement;
  const below = shell ? belowInColumn(vp) + bottomInsetToPage(shell) : 96;
  return Math.max(stacked ? stackedFloor(MIN_VIEWPORT_H) : MIN_VIEWPORT_H, Math.floor(window.innerHeight - top - below));
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
  const room = availContentHeight() + 'px';
  vp.style.maxHeight = room;
  // Stacked, the column no longer fills the page, so the frame holds its room as a floor too: one height with
  // or without a picture, so no flight measures a frame that then moves under it.
  if (isStacked()) vp.style.minHeight = room;
  else if (vp.style.minHeight) vp.style.minHeight = '';
};

// The part of the page the screen shows, in client px: a window hanging past the screen's edge hides that strip,
// and a popover kept to the whole viewport opened into it (user report). The whole viewport when nothing hangs over.
const MIN_ON_SCREEN_PX = 120;   // a sliver this narrow is no room to place in: the whole viewport instead
export const onScreenViewport = (win = window) => {
  const s = win.screen ?? {};
  const availLeft = s.availLeft ?? 0, availTop = s.availTop ?? 0;
  const availRight = availLeft + (s.availWidth ?? Infinity), availBottom = availTop + (s.availHeight ?? Infinity);
  const x = win.screenX ?? 0, y = win.screenY ?? 0;
  const left = Math.max(0, Math.min(win.innerWidth, availLeft - x));
  const right = Math.max(0, Math.min(win.innerWidth, x + (win.outerWidth || win.innerWidth) - availRight));
  const bottom = Math.max(0, Math.min(win.innerHeight, y + (win.outerHeight || win.innerHeight) - availBottom));
  const width = win.innerWidth - left - right, height = win.innerHeight - bottom;
  return width >= MIN_ON_SCREEN_PX && height >= MIN_ON_SCREEN_PX ? { left, top: 0, width, height } : { left: 0, top: 0, width: win.innerWidth, height: win.innerHeight };
};

// The coordinates panel's room off its LIVE top (a vh guess leaves a scrollbar): its cap, so a long list scrolls
// inside it, and the column a fold of it spans to the bottom, as the desktop's dock (animations/collapse.css).
export const syncCoordPanelHeight = () => {
  const panel = document.getElementById('coord-panel');
  if (!panel || document.body.classList.contains('fullscreen-mode')) return;
  const top = layoutTop(panel);
  const avail = Math.floor(window.innerHeight - top - bottomInsetToPage(panel));
  // Stacked, the panel sits under the frame, below the fold as often as not: its share of the window, scrolling inside.
  const room = Math.max(MIN_PANEL_H, isStacked() ? stackedFloor(MIN_PANEL_H) : avail) + 'px';
  panel.style.maxHeight = room;
  panel.style.setProperty('--coord-panel-room', room);
};
