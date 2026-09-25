import { onWindowResize } from '../ui/canvas/frameSync.js';
// Writes the shared `--coord-panel-width` CSS var (clamped); dragging LEFT widens. A dragged
// width lives as long as the page and is stored nowhere: a reload comes back at the CSS
// default, as the desktop relaunches at its own.
// Under this the layout overflows rather than shrinks.
export const MIN_CANVAS_WIDTH = 320;

// Never past `maxFactor` of the window, the 640 ceiling, or MIN_CANVAS_WIDTH for the canvas.
export const clampPanelWidth = (w, winW, maxFactor = 0.7) =>
  Math.max(240, Math.min(640, Math.round(winW * maxFactor), Math.max(0, winW - MIN_CANVAS_WIDTH), w));

// The width the last drag chose, shared by both handles (the in-flow one and fullscreen's).
let dragged = NaN;

export const wirePanelResizer = (resizer, panel, { maxFactor = 0.7, onStart, onEnd, track = false } = {}) => {
  const clamp = (w) => clampPanelWidth(w, window.innerWidth, maxFactor);
  const setWidth = (w) => document.documentElement.style.setProperty('--coord-panel-width', clamp(w) + 'px');
// `track`: re-clamped against the live window on every resize, keeping the preference. A panel
// never dragged has NO preference and stays on the CSS default, which may span the window.
  const applyDragged = () => { if (Number.isFinite(dragged)) setWidth(dragged); };
  if (track) onWindowResize(applyDragged);
  let startX = 0, startW = 0, dragging = false;
  const onMove = (e) => { if (dragging) setWidth(startW + (startX - e.clientX)); };
  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    resizer.classList.remove('dragging');
    document.body.style.userSelect = '';
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
    dragged = Math.round(panel.getBoundingClientRect().width);
    onEnd?.();
  };
  resizer.addEventListener('mousedown', (e) => {
    e.preventDefault();
    dragging = true;
    startX = e.clientX;
    startW = panel.getBoundingClientRect().width;
    resizer.classList.add('dragging');
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
    onStart?.();
  });
  return { isDragging: () => dragging };
};
