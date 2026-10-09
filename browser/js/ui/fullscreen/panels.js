import { FOLD_DUST_OUT_MS, SURFACE_IN_MS, dockAwayPoint, settleSurface, surfaceIn, surfaceOut } from '../motion.js';
import { EVENTS, subscribe } from '../../eventBus/appBus.js';
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

// A fixed box by its layout offsets: the client rect carries the entrance's stretch transform.
const layoutBox = (el) => {
  if (!el?.getBoundingClientRect) return null;
  if (globalThis.getComputedStyle?.(el)?.position !== 'fixed') return el.getBoundingClientRect();
  const left = el.offsetLeft, top = el.offsetTop;
  return { left, top, right: left + el.offsetWidth, width: el.offsetWidth, height: el.offsetHeight };
};

// A chat on its way out keeps its box until its dust settles; the panels give it up at once.
const dockedChat = (chat) =>
  (chat?.classList?.contains('chat-open') && !chat.classList.contains('chat-closing') ? chat : null);

// The canvas's box less a chat docked over its left or right side; null when either is unmeasured.
export const canvasSpan = (viewport, chat) => {
  const vp = layoutBox(viewport);
  if (!vp || !(vp.width > 0 && vp.height > 0)) return null;
  let { left, right } = vp;
  const c = layoutBox(dockedChat(chat));
  if (c && c.width > 0 && c.height > 0) {
    if (c.left <= left + 1 && c.right < right) left = Math.max(left, c.right);
    else if (c.right >= right - 1 && c.left > left) right = Math.min(right, c.left);
  }
  return { left, right, top: vp.top, width: right - left, height: vp.height };
};

// The strip and the selection overlay under it take the canvas's own columns, as the desktop's
// tool rows stop at a docked chat. Written in px even when they are the window's: a width the
// stylesheet leaves auto cannot ease to the next one.
const placeAcross = (el, span) => {
  if (!el) return;
  const winW = globalThis.window?.innerWidth;
  const placed = !!(span && winW);
  const left = placed ? Math.max(0, Math.round(span.left)) : 0;
  const right = placed ? Math.min(winW, Math.round(span.right)) : 0;
  el.style.left = placed ? `${left}px` : '';
  el.style.width = placed ? `${right - left}px` : '';
};

// The list and its handle keep their stylesheet offsets from the right, shifted by a chat docked
// there (--fs-dock-right, components/fullscreen.css); nothing is set while no chat is in the way.
const placeAtRight = (el, span) => {
  if (!el) return;
  const winW = globalThis.window?.innerWidth;
  const off = span && winW ? Math.max(0, winW - Math.round(span.right)) : 0;
  if (off > 0) el.style.setProperty('--fs-dock-right', `${off}px`);
  else el.style.removeProperty('--fs-dock-right');
};

// The top band also spans a shown selection overlay, which sits below the strip. Both bands lie
// along the canvas's own edges: a docked chat beside it reveals nothing (desktop FullscreenController).
export const syncFsTriggers = () => {
  const el = (id) => document.getElementById(id);
  const band = (panel) => (panel?.classList.contains(VISIBLE_CLASS) ? REVEALED_TRIGGER_PX : FS_TRIGGER_PX);
  const fsSel = el('fs-selection-panel');
  const sel = fsSel && fsSel.style.display !== 'none' ? fsSel.getBoundingClientRect().bottom : 0;
  const span = canvasSpan(el('canvas-viewport'), document.querySelector?.('stencil-chat-panel'));
  // Placed only in fullscreen: outside it the viewport's columns are the page's, not the window's.
  const placed = document.body?.classList?.contains('fullscreen-mode') ? span : null;
  placeAcross(el('fs-controls-panel'), placed);
  placeAcross(fsSel, placed);
  placeAtRight(el('fs-points-panel'), placed);
  placeAtRight(el('fs-panel-resizer'), placed);
  // …nor does the points list revealed over the canvas's right side.
  const list = el('fs-points-panel');
  const topRight = span && list?.classList.contains(VISIBLE_CLASS)
    ? Math.min(span.right, list.offsetLeft - HANDLE_PX) : span?.right;
  const top = el('fs-top-trigger');
  if (top) {
    top.style.height = Math.max(band(el('fs-controls-panel')), sel) + 'px';
    top.style.left = span ? `${Math.round(span.left)}px` : '';
    top.style.width = span ? `${Math.round(Math.max(0, topRight - span.left))}px` : '';
    top.style.right = span ? 'auto' : '';
  }
  const right = el('fs-right-trigger');
  if (right) {
    right.style.width = band(el('fs-points-panel')) + 'px';
    right.style.right = span ? `${Math.round(window.innerWidth - span.right)}px` : '';
    right.style.top = span ? `${Math.round(span.top)}px` : '';
    right.style.height = span ? `${Math.round(span.height)}px` : '';
    right.style.bottom = span ? 'auto' : '';
  }
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
  // The bands and the panels follow the canvas as a chat opens, closes, docks or resizes beside
  // it, and take their places on the way into fullscreen, ahead of any reveal.
  const chat = document.querySelector?.('stencil-chat-panel');
  if (typeof ResizeObserver !== 'undefined') {
    const ro = new ResizeObserver(() => syncFsTriggers());
    for (const n of [document.getElementById?.('canvas-viewport'), chat]) if (n) ro.observe(n);
  }
  // A close is announced only once its dust has settled; the class says it is leaving now.
  if (chat && typeof MutationObserver !== 'undefined')
    new MutationObserver(() => syncFsTriggers()).observe(chat, { attributes: true, attributeFilter: ['class'] });
  subscribe(EVENTS.chatLayoutChanged, () => syncFsTriggers());
  subscribe(EVENTS.fullscreenChanged, () => (typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame(syncFsTriggers) : syncFsTriggers()));

  return {
    showControlsPanel: controls.show, hideControlsPanel: controls.hide,
    showPointsPanel: points.show, hidePointsPanel: points.hide,
    pauseControlsHide: controls.pause, pausePointsHide: points.pause, reset,
  };
};
