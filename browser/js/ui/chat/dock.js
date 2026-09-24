import { icon } from '../icons.js';
import { publish, EVENTS } from '../../eventBus/appBus.js';
import { trackPointer } from './view.js';
import {
  DOCKS, FLOAT_DEFAULT, DRAG_THRESHOLD_PX, DOCK_MIN_SIZE, DOCK_MAX_FRACTION,
  clampFloatRect, resizeFloatRect, dockZoneAt,
} from './geometry.js';

// Where the chat panel lives: dock edge or float rect, the gestures that change it, and
// the insets a docked panel takes out of the editor column. Session-only by design.
export function createChatDock(deps) {
  const {
    host, resizer, header, floatHandles, playDust, invalidatePillRects, phoneModal, onAdopt,
  } = deps;
// Every page load starts closed, docked left, at the default sizes.
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
// Announced as an event, not a ResizeObserver: the listeners resize the elements such an
// observer would watch. The notify stack reads these vars to dodge a docked chat.
  const updateNotifyInset = () => {
    invalidatePillRects();
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
// --chat-size does not inherit (panel.css), so it is written on each element that reads it.
  let chatSize = '';
  const applySize = () => {
    if (!chatSize) return;
    for (const el of [host, document.body, ...document.querySelectorAll('stencil-install, stencil-drop-overlay')])
      el.style.setProperty('--chat-size', chatSize);
  };
  const setDock = (mode) => {
    dock = mode;
    for (const d of DOCKS) host.classList.toggle(`chat-dock-${d}`, d === mode);
    host.removeAttribute('style');
    applySize();
    if (mode === 'float') applyFloatRect();
    for (const b of dockBtns) b.classList.toggle('chat-dock-btn-active', b.dataset.dock === mode);
// Re-forms out of the new edge; only while on screen.
    if (host.classList.contains('chat-open') && !host.classList.contains('chat-closing')) playDust(true);
    announceLayout();
  };
// A compact float opened by the popover gestures is transient: once it closes the prior
// layout comes back; only a layout the user chose sticks for the session.
  let compactPopover = false;
  let dockBeforeCompact = dock;
  let floatRectBeforeCompact = null;
// Adopting a layout resets the gesture machine, so an Alt glide cannot closeFromGlide this panel.
  const adoptLayout = () => { compactPopover = false; onAdopt?.(); };
  const restoreFromCompact = () => {
    if (!compactPopover) return;
    compactPopover = false;
    if (floatRectBeforeCompact) floatRect = clampRect(floatRectBeforeCompact);
    setDock(dockBeforeCompact);
  };
  for (const b of dockBtns) b.addEventListener('click', () => { adoptLayout(); setDock(b.dataset.dock); });
  const wireGestures = () => {
    const setSize = (px) => {
      const max = (dock === 'top' || dock === 'bottom' ? window.innerHeight : window.innerWidth) * DOCK_MAX_FRACTION;
      const clamped = Math.max(DOCK_MIN_SIZE, Math.min(Math.round(max), Math.round(px)));
      chatSize = clamped + 'px';
      applySize();
    };
// Window-level move/up (pointer capture as an assist) so the resize keeps tracking off
// the 16px handle. One starter for the docked-axis handle and the float-only handles.
    const beginResize = (handle, dir) => (e) => {
      e.preventDefault();
      e.stopPropagation();
      const startX = e.clientX, startY = e.clientY;
      const rect = host.getBoundingClientRect();
      const startW = rect.width, startH = rect.height;
      const start = { ...floatRect };
      adoptLayout();
      try { handle.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      handle.classList.add('dragging');
// CSS gates other handles' hover affordances on this class.
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

    let zonesEl = null;
    const showDockZones = () => {
      if (zonesEl) return;
      zonesEl = document.createElement('div');
      zonesEl.className = 'chat-dock-zones';
      for (const side of ['left', 'right', 'top', 'bottom']) {
        const z = document.createElement('div');
        z.className = `chat-dock-zone chat-dock-zone-${side}`;
        z.dataset.side = side;
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

// Header = drag handle. Floating moves the window; docked undocks into float at the
// pointer and continues the gesture. Releasing over an edge zone docks there.
    header.addEventListener('pointerdown', (e) => {
      if (e.target.closest('button')) return;
// Phones present the panel as an ordinary modal (components/chat/touch.css).
      if (phoneModal()) return;
      e.preventDefault();
      try { header.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      const downX = e.clientX, downY = e.clientY;
      let started = false;
      let offX = 0, offY = 0;
      const begin = (ev) => {
        started = true;
        adoptLayout();
        if (dock !== 'float') {
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
