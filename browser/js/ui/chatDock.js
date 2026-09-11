import { icon } from './icons.js';
import { publish, EVENTS } from '../bus/appBus.js';
import { trackPointer } from './chatView.js';
import {
  DOCKS, FLOAT_DEFAULT, DRAG_THRESHOLD_PX, DOCK_MIN_SIZE, DOCK_MAX_FRACTION,
  clampFloatRect, resizeFloatRect, dockZoneAt,
} from './chatGeometry.js';

// Where the chat panel LIVES: its dock edge or float rect, the buttons and gestures that
// change it (dock buttons, header drag, edge resize, the transient compact popover), and
// the insets a docked panel takes out of the editor column. Session-only by design.
export function createChatDock(deps) {
  const {
    host, resizer, header, floatHandles, playDust, invalidatePillRects, phoneModal, onAdopt,
  } = deps;
  // ── Docking / float placement. Layout is SESSION-ONLY by design: every page
  // load starts closed, docked left, at the default sizes. ──
  let dock = 'left';
  const clampRect = (r) => clampFloatRect(r, window.innerWidth, window.innerHeight);
  let floatRect = clampRect(FLOAT_DEFAULT);

  const applyFloatRect = () => {
    host.style.left = floatRect.x + 'px';
    host.style.top = floatRect.y + 'px';
    host.style.width = floatRect.w + 'px';
    host.style.height = floatRect.h + 'px';
    host.style.right = 'auto';
    host.style.bottom = 'auto';
  };
  const dockBtns = [...host.querySelectorAll('.chat-dock-btn')];
  // Docking top/bottom takes real height from the editor column, so the viewport and
  // coordinates panel re-measure against what is left. Announced as an event, not a
  // ResizeObserver: the listeners resize the very elements such an observer would
  // watch. Toasts dodge a docked chat: the notify stack reads these vars.
  const updateNotifyInset = () => {
    invalidatePillRects();   // runs on every panel resize/dock change — the pills moved
    const open = host.classList.contains('chat-open');
    const r = open ? host.getBoundingClientRect() : { width: 0, height: 0 };
    const left = open && host.classList.contains('chat-dock-left') ? Math.round(r.width) : 0;
    const bottom = open && host.classList.contains('chat-dock-bottom') ? Math.round(r.height) : 0;
    document.body.style.setProperty('--chat-inset-left', `${left}px`);
    document.body.style.setProperty('--chat-inset-bottom', `${bottom}px`);
  };
  if (typeof ResizeObserver !== 'undefined') new ResizeObserver(updateNotifyInset).observe(host);
  const announceLayout = () => {
    updateNotifyInset();
    publish(EVENTS.chatLayoutChanged);
  };
  const setDock = (mode) => {
    dock = mode;
    for (const d of DOCKS) host.classList.toggle(`chat-dock-${d}`, d === mode);
    host.removeAttribute('style');   // clear any float inline rect
    if (mode === 'float') applyFloatRect();
    for (const b of dockBtns) b.classList.toggle('chat-dock-btn-active', b.dataset.dock === mode);
    // The dock swap used to restart the matching slide; it re-forms out of the NEW
    // edge instead. Only while on screen — a closed panel has nothing to measure.
    if (host.classList.contains('chat-open') && !host.classList.contains('chat-closing')) playDust(true);
    announceLayout();
  };
  // A compact float opened by the popover gestures is TRANSIENT: once it closes, the
  // prior layout comes back — only a layout the user chose (dock button, header
  // drag, edge resize) sticks for the session.
  let compactPopover = false;
  let dockBeforeCompact = dock;
  let floatRectBeforeCompact = null;
  // Adopting a layout also resets the gesture machine: its popover is gone,
  // so an Alt glide over other icons must not closeFromGlide THIS panel.
  const adoptLayout = () => { compactPopover = false; onAdopt?.(); };
  const restoreFromCompact = () => {
    if (!compactPopover) return;
    compactPopover = false;
    if (floatRectBeforeCompact) floatRect = clampRect(floatRectBeforeCompact);
    setDock(dockBeforeCompact);
  };
  for (const b of dockBtns) b.addEventListener('click', () => { adoptLayout(); setDock(b.dataset.dock); });
  // The gestures need the panel in the DOM and the dust clock wired, so the caller arms
  // them once wire() has both.
  const wireGestures = () => {
    // ── Docked-size resizer (wirePanelResizer-style, but axis-aware + float corner) ──
    const setSize = (px) => {
      const max = (dock === 'top' || dock === 'bottom' ? window.innerHeight : window.innerWidth) * DOCK_MAX_FRACTION;
      const clamped = Math.max(DOCK_MIN_SIZE, Math.min(Math.round(max), Math.round(px)));
      document.documentElement.style.setProperty('--chat-size', clamped + 'px');
    };
    // Window-level move/up listeners (pointer capture as an assist) so the resize keeps
    // tracking off the 16px handle. One gesture starter for every resize surface: the
    // docked-axis handle (doubles as the float SE grip) and the float-only handles.
    const beginResize = (handle, dir) => (e) => {
      e.preventDefault();
      e.stopPropagation();
      const startX = e.clientX, startY = e.clientY;
      const rect = host.getBoundingClientRect();
      const startW = rect.width, startH = rect.height;
      const start = { ...floatRect };
      adoptLayout();   // resizing the float = the user chose this shape
      try { handle.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      handle.classList.add('dragging');
      // While a gesture is live, other handles' hover affordances are suppressed
      // (CSS gates on this class) so only the dragged handle shows as active.
      host.classList.add('chat-gesturing');
      trackPointer((ev) => {
        if (dock === 'float') {
          floatRect = resizeFloatRect(start, dir, ev.clientX - startX, ev.clientY - startY, window.innerWidth, window.innerHeight);
          applyFloatRect();
        } else if (dock === 'left') setSize(startW + (ev.clientX - startX));
        else if (dock === 'right') setSize(startW + (startX - ev.clientX));
        else if (dock === 'top') setSize(startH + (ev.clientY - startY));
        else setSize(startH + (startY - ev.clientY));
      }, () => {
        handle.classList.remove('dragging');
        host.classList.remove('chat-gesturing');
      });
    };
    resizer.addEventListener('pointerdown', beginResize(resizer, 'se'));
    for (const fh of floatHandles()) fh.addEventListener('pointerdown', beginResize(fh, fh.dataset.dir));

    // ── Edge drop zones, shown only WHILE a header drag is live (no permanent DOM) ──
    let zonesEl = null;
    const showDockZones = () => {
      if (zonesEl) return;
      zonesEl = document.createElement('div');
      zonesEl.className = 'chat-dock-zones';
      for (const side of ['left', 'right', 'top', 'bottom']) {
        const z = document.createElement('div');
        z.className = `chat-dock-zone chat-dock-zone-${side}`;
        z.dataset.side = side;
        // Animated chevron pointing INTO the edge — the "dock here" affordance.
        const arrow = document.createElement('span');
        arrow.className = 'chat-zone-arrow';
        arrow.innerHTML = icon(`chevron-${side === 'top' ? 'up' : side === 'bottom' ? 'down' : side}`, { size: 18 });
        z.appendChild(arrow);
        zonesEl.appendChild(z);
      }
      document.body.appendChild(zonesEl);
    };
    const highlightDockZone = (side) => {
      if (!zonesEl) return;
      for (const z of zonesEl.children) z.classList.toggle('chat-dock-zone-active', z.dataset.side === side);
    };
    const hideDockZones = () => { zonesEl?.remove(); zonesEl = null; };

    // ── Header = drag handle (everywhere except the buttons). Floating: moves the
    // window. Docked: undocks into float AT THE POINTER and continues the same
    // gesture. Releasing over an edge zone docks there; elsewhere keeps floating.
    header.addEventListener('pointerdown', (e) => {
      if (e.target.closest('button')) return;
      // Phones present the panel as an ordinary modal (components/chat/touch.css) — no
      // dragging, no undocking, no dock zones.
      if (phoneModal()) return;
      e.preventDefault();
      try { header.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      const downX = e.clientX, downY = e.clientY;
      let started = false;
      let offX = 0, offY = 0;
      const begin = (ev) => {
        started = true;
        adoptLayout();   // header drag = the user chose where this panel lives
        if (dock !== 'float') {
          // Undock: keep the stored float size, grabbed by the header at the pointer.
          floatRect = clampRect({ ...floatRect, x: ev.clientX - Math.min(floatRect.w / 2, 140), y: ev.clientY - 14 });
          setDock('float');
        }
        offX = ev.clientX - floatRect.x;
        offY = ev.clientY - floatRect.y;
        header.classList.add('chat-dragging');
        host.classList.add('chat-gesturing');
        showDockZones();
      };
      trackPointer((ev) => {
        if (!started) {
          if (Math.abs(ev.clientX - downX) < DRAG_THRESHOLD_PX && Math.abs(ev.clientY - downY) < DRAG_THRESHOLD_PX) return;
          begin(ev);
        }
        floatRect = clampRect({ ...floatRect, x: ev.clientX - offX, y: ev.clientY - offY });
        applyFloatRect();
        highlightDockZone(dockZoneAt(ev.clientX, ev.clientY, window.innerWidth, window.innerHeight));
      }, (ev) => {
        header.classList.remove('chat-dragging');
        host.classList.remove('chat-gesturing');
        hideDockZones();
        if (!started) return;
        const zone = ev.type === 'pointercancel' ? null
          : dockZoneAt(ev.clientX, ev.clientY, window.innerWidth, window.innerHeight);
        if (zone) setDock(zone);
      });
    });
  };

  return {
    mode: () => dock,
    setDock, wireGestures, announceLayout, adoptLayout, restoreFromCompact,
    rect: () => floatRect,
    clampRect,
    isCompact: () => compactPopover,
    enterCompact: (r) => {
      if (!compactPopover) { dockBeforeCompact = dock; floatRectBeforeCompact = { ...floatRect }; }
      compactPopover = true;
      floatRect = r;
      setDock('float');
    },
  };
}
