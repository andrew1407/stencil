import { onWindowResize } from '../ui/frameSync.js';
// Writes the shared `--coord-panel-width` CSS var (clamped); dragging LEFT widens. Kept for
// THIS tab only (sessionStorage) — the desktop relaunches at its default too.
const COORD_PANEL_WIDTH_KEY = 'drawingApp_coordPanelWidth';
// Under this the layout overflows rather than shrinks.
export const MIN_CANVAS_WIDTH = 320;

// Never past `maxFactor` of the window, the 640 ceiling, or MIN_CANVAS_WIDTH for the canvas.
export const clampPanelWidth = (w, winW, maxFactor = 0.7) =>
  Math.max(240, Math.min(640, Math.round(winW * maxFactor), Math.max(0, winW - MIN_CANVAS_WIDTH), w));

export const wirePanelResizer = (resizer, panel, { maxFactor = 0.7, onStart, onEnd, restore = false } = {}) => {
  const clamp = (w) => clampPanelWidth(w, window.innerWidth, maxFactor);
  const setWidth = (w) => document.documentElement.style.setProperty('--coord-panel-width', clamp(w) + 'px');
// Re-clamped against the live window on restore and resize, keeping the preference. A
// panel the user never dragged has NO preference and stays on the CSS default: below the
// stacking breakpoint it spans the window, and measuring it fed that width back as one.
  const applyStored = () => {
    let saved = NaN;
    try { saved = parseInt(sessionStorage.getItem(COORD_PANEL_WIDTH_KEY), 10); } catch { /* storage blocked */ }
    if (Number.isFinite(saved)) setWidth(saved);
    else document.documentElement.style.removeProperty('--coord-panel-width');
  };
  if (restore) { applyStored(); onWindowResize(applyStored); }
  let startX = 0, startW = 0, dragging = false;
  const onMove = (e) => { if (dragging) setWidth(startW + (startX - e.clientX)); };
  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    resizer.classList.remove('dragging');
    document.body.style.userSelect = '';
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
    try { sessionStorage.setItem(COORD_PANEL_WIDTH_KEY, String(Math.round(panel.getBoundingClientRect().width))); } catch { /* storage blocked */ }
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
