import { FOLD_DUST_OUT_MS, SURFACE_IN_MS, dockAwayPoint, settleSurface, surfaceIn, surfaceOut } from './motion.js';
// The two slide-in panels and their auto-hide timers; the hover wiring and the resizer drag
// pause them through `pauseControlsHide` / `pausePointsHide`. Each comes and goes as dust past
// its own edge, the fold the toolbar rows and the points panel play (toolbar.js, mainContent.js).

const HIDE_GRACE_MS = 400;
// The list gathers on the coord panel's clock (mainContent.js), the strip on the toolbar's.
export const POINTS_DUST_IN_MS = 460;
// The drag handle's strip at the list's left edge (#fs-panel-resizer): dusted with the list.
const HANDLE_PX = 10;
// The reveal band, the desktop's PANEL_REVEAL_PX. A revealed panel is its own keep-zone, so its
// band shrinks into the panel's padding and never covers a control.
export const FS_TRIGGER_PX = 28;
const REVEALED_TRIGGER_PX = 8;
// The selection overlay's own top slide (components/fullscreen.css).
const SELECTION_TOP_MS = 260;
const VISIBLE_CLASS = 'fs-panel-visible';

// The top band also spans a shown selection overlay, which sits below the strip.
export const syncFsTriggers = () => {
  const el = (id) => document.getElementById(id);
  const band = (panel) => (panel?.classList.contains(VISIBLE_CLASS) ? REVEALED_TRIGGER_PX : FS_TRIGGER_PX);
  const fsSel = el('fs-selection-panel');
  const sel = fsSel && fsSel.style.display !== 'none' ? fsSel.getBoundingClientRect().bottom : 0;
  const top = el('fs-top-trigger');
  if (top) top.style.height = Math.max(band(el('fs-controls-panel')), sel) + 'px';
  const right = el('fs-right-trigger');
  if (right) right.style.width = band(el('fs-points-panel')) + 'px';
};

// The overlay rides under the revealed strip and at the top edge without it.
const placeSelectionOverlay = (fsControlsPanel, ctrlsVisible) => {
  const fsSel = document.getElementById('fs-selection-panel');
  if (!fsSel || fsSel.style.display === 'none') return;
  // offsetHeight, not getBoundingClientRect: the transform must not affect the measure
  fsSel.style.top = ctrlsVisible ? fsControlsPanel.offsetHeight + 'px' : '0px';
  requestAnimationFrame(syncFsTriggers);
};

// The box a fixed panel holds once revealed: the offsets, never the client rect, which carries
// the slide's transform. The list's box takes in its drag handle, so the motes cover that too.
const revealedBox = (panel, dock) => {
  const box = { left: panel.offsetLeft, top: panel.offsetTop, width: panel.offsetWidth, height: panel.offsetHeight };
  if (dock === 'right') { box.left -= HANDLE_PX; box.width += HANDLE_PX; }
  return box.width >= 8 && box.height >= 8 ? box : null;
};

// True while the motes fly; false hands the reveal back to the CSS slide.
export const panelDust = (panel, dock, hiding, inMs = SURFACE_IN_MS) => {
  const box = revealedBox(panel, dock);
  const away = box && dockAwayPoint(box, dock);
  return (hiding ? surfaceOut : surfaceIn)(panel, away || null, { box, ms: hiding ? FOLD_DUST_OUT_MS : inMs });
};

// One panel's reveal, auto-hide and flight, closed over its own timer and shown flag. `before`
// runs on each edge ahead of the dust, `after` once the hidden state has landed.
const makeFsPanel = ({ panel, dock, inMs, dust, before = () => {}, after = () => {} }) => {
  let hideTimer = null;
  let shown = false;

  const show = () => {
    clearTimeout(hideTimer);
    before(true);   // before the class, so the CSS transitions start together
    panel.classList.add(VISIBLE_CLASS);
    if (!shown) dust(panel, dock, false, inMs);
    shown = true;
    syncFsTriggers();
  };
  const hide = () => {
    clearTimeout(hideTimer);
    hideTimer = setTimeout(() => {
      before(false);
      // The panel keeps its box for the whole out-flight; the class goes with the last motes.
      const played = shown && dust(panel, dock, true);
      shown = false;
      const drop = () => { panel.classList.remove(VISIBLE_CLASS); syncFsTriggers(); after(); };
      if (played) hideTimer = setTimeout(drop, FOLD_DUST_OUT_MS);
      else drop();
    }, HIDE_GRACE_MS);
  };
  // A cursor back on a panel still dissolving brings it back, as the slide used to reverse.
  const pause = () => { if (shown) clearTimeout(hideTimer); else show(); };
  // Leaving fullscreen: whatever is in the air goes with the mode (desktop: stopDustClouds).
  const settle = () => {
    clearTimeout(hideTimer);
    shown = false;
    settleSurface(panel);
    panel.classList.remove(VISIBLE_CLASS);
  };
  return { show, hide, pause, settle };
};

export const createFsPanels = ({ fsControlsPanel, fsPointsPanel, showPoints, dust = panelDust }) => {
  const controls = makeFsPanel({
    panel: fsControlsPanel, dock: 'top', inMs: SURFACE_IN_MS, dust,
    before: (revealing) => placeSelectionOverlay(fsControlsPanel, revealing),
    after: () => setTimeout(syncFsTriggers, SELECTION_TOP_MS),   // …again once the overlay has landed
  });
  const points = makeFsPanel({
    panel: fsPointsPanel, dock: 'right', inMs: POINTS_DUST_IN_MS, dust,
    before: (revealing) => { if (revealing) showPoints(); },
  });
  const reset = () => { controls.settle(); points.settle(); syncFsTriggers(); };

  return {
    showControlsPanel: controls.show, hideControlsPanel: controls.hide,
    showPointsPanel: points.show, hidePointsPanel: points.hide,
    pauseControlsHide: controls.pause, pausePointsHide: points.pause, reset,
  };
};
