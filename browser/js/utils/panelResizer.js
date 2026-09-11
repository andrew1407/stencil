import { onWindowResize } from '../ui/frameSync.js';
// ── Coordinates-panel drag resizer ──────────────────────────────
// Resizes the coordinates panel by writing the shared `--coord-panel-width` CSS var
// (clamped); used by both the normal and fullscreen panels. Dragging LEFT widens.
// `onStart`/`onEnd` hook a drag (fullscreen pauses its auto-hide).
// The width is kept for THIS tab only (sessionStorage): a reload keeps it, a reopened
// app starts at the default again — the desktop relaunches at its default too.
const COORD_PANEL_WIDTH_KEY = 'drawingApp_coordPanelWidth';
// Never squeeze the canvas below this; under it the layout overflows rather than shrinks.
export const MIN_CANVAS_WIDTH = 320;

// Widest the panel may be in a window of `winW`: never past `maxFactor` of it, never past
// the 640 ceiling, never leaving less than MIN_CANVAS_WIDTH for the canvas. Pure → testable.
export const clampPanelWidth = (w, winW, maxFactor = 0.7) =>
  Math.max(240, Math.min(640, Math.round(winW * maxFactor), Math.max(0, winW - MIN_CANVAS_WIDTH), w));

export const wirePanelResizer = (resizer, panel, { maxFactor = 0.7, onStart, onEnd, restore = false } = {}) => {
  const clamp = (w) => clampPanelWidth(w, window.innerWidth, maxFactor);
  const setWidth = (w) => document.documentElement.style.setProperty('--coord-panel-width', clamp(w) + 'px');
  // The width survives a reload, so a panel dragged wide in a big window can come back
  // into a small one — and a window can be narrowed after the fact. Re-clamp on both,
  // against the live window, keeping the preference for when there is room for it again.
  // A panel the user never dragged has NO preference: it stays on the CSS default
  // (--coord-panel-default) rather than being pinned to whatever it measures right now.
  // Measuring it was the bug: below the stacking breakpoint the panel spans the window
  // under the canvas, and each resize fed that full width back as the "preference",
  // ratcheting an untouched panel up to its cap by the time the window was wide again.
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
