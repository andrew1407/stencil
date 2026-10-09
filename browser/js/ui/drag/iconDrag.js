// Dragging a toolbar control: past the press slop a ghost of the control follows the pointer and the
// owner's hooks see the drag; released back over the control it came from, the drag is a cancel.
// The machine is pure (tests drive it); wireIconDrag is its DOM. Mouse and pen only — touch keeps
// the long-press popover. Desktop twin: desktop/src/support/drag/iconDrag.{hpp,cpp}.
import { pointInRect } from '../../utils.js';
import { createDragGhost } from '../canvas/dragGhost.js';
import { holdTips } from '../tip/tipHold.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

export const DRAG_SLOP_PX = constants.POPOVER.pressSlopPx;
export const GHOST_OPACITY = 0.85;
export const DRAGGING_CLASS = 'icon-dragging';
export const SOURCE_CLASS = 'icon-drag-source';
export const GHOST_CLASS = 'icon-drag-ghost';
export const TARGET_CLASS = 'icon-drop-target';
export const TARGET_OVER_CLASS = 'icon-drop-target-over';

let started = 0;
/** Drags started this session: a deferred open notes it and stands down if a drag began since. */
export const dragsStarted = () => started;

let live = 0;
let waiting = [];
/** Runs `fn` now, or once the drag in progress has dropped: a panel the drag left stays up meanwhile. */
export const afterIconDrag = (fn) => { if (live) waiting.push(fn); else fn(); };
const settle = () => {
  if (--live > 0) return;
  const run = waiting;
  waiting = [];
  setTimeout(() => { for (const fn of run) fn(); }, 0);
};

export const createIconDrag = ({ start, move, drop, cancel, originRect, targetAt = () => null,
                                 slop = DRAG_SLOP_PX } = {}) => {
  let press = null;
  let dragging = false;
  const at = (x, y) => ({ x, y, overOrigin: pointInRect(x, y, originRect()), target: targetAt(x, y) });
  return {
    get active() { return dragging; },
    press(x, y, event = null) { press = { x, y, event }; dragging = false; },
    // True while the pointer is dragging; the first move past the slop asks `start`, which may refuse.
    move(x, y) {
      if (!press) return false;
      if (!dragging) {
        if (Math.hypot(x - press.x, y - press.y) <= slop) return false;
        if (start?.({ x, y, from: { x: press.x, y: press.y }, event: press.event }) === false) {
          press = null;
          return false;
        }
        dragging = true;
        started += 1;
      }
      move?.(at(x, y));
      return true;
    },
    // True when this release ended a drag, so the click it would make is the drag's, not a press.
    release(x, y) {
      const was = dragging;
      press = null;
      dragging = false;
      if (!was) return false;
      const p = at(x, y);
      if (p.overOrigin) cancel?.();
      else drop?.(p);
      return true;
    },
    abort() {
      const was = dragging;
      press = null;
      dragging = false;
      if (was) cancel?.();
      return was;
    },
  };
};

// The release's click, and the dblclick it makes when a click came just before the drag.
const swallowNextClick = () => {
  const stop = (e) => { e.stopImmediatePropagation(); e.preventDefault(); };
  const off = () => {
    window.removeEventListener('click', stop, true);
    window.removeEventListener('dblclick', stop, true);
  };
  window.addEventListener('click', stop, true);
  window.addEventListener('dblclick', stop, true);
  setTimeout(off, 0);
};

// `hooks`: start / move / drop / cancel as the machine's, plus `ghost` (false: the owner draws its own),
// `ghostCentred` (the ghost rides centred on the pointer, not held where it was grabbed)
// and `enabled` (checked at each press; a disabled control is not dragged).
export const wireIconDrag = (el, hooks = {}) => {
  if (!el?.addEventListener) return null;
  const { ghost: wantGhost = true, ghostCentred = false, enabled = () => !el.disabled } = hooks;
  let pointerId = null;
  let ghost = null;
  let counted = false;
  let pressAt = null;
  let swallow = false;

  const finish = () => {
    ghost?.destroy();
    ghost = null;
    if (counted) { counted = false; settle(); }
    document.documentElement.classList.remove(DRAGGING_CLASS);
    el.classList.remove(SOURCE_CLASS);
    window.removeEventListener('keydown', onKey, true);
    holdTips(false);
  };
  const onKey = (e) => {
    if (e.key !== 'Escape' || !machine.active) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    finish();
    machine.abort();
  };
  const machine = createIconDrag({
    ...hooks,
    originRect: () => el.getBoundingClientRect(),
    targetAt: (x, y) => document.elementFromPoint?.(x, y) ?? null,
    // Held first: no tip or tip cloud may be up while the owner starts (a lens copies the page).
    start: (p) => {
      holdTips(true);
      if (hooks.start?.(p) === false) { holdTips(false); return false; }
      el.__stencilGestures?.dragged?.();
      live += 1;
      counted = true;
      try { el.setPointerCapture?.(pointerId); } catch { /* the pointer is already gone */ }
      const r = ghostCentred ? el.getBoundingClientRect() : null;
      const hold = r ? { x: r.left + r.width / 2, y: r.top + r.height / 2 } : pressAt;
      if (wantGhost) {
        ghost = createDragGhost(el, hold.x, hold.y, GHOST_OPACITY);
        ghost.el.style.boxShadow = '';   // the shining rim is css/animations/icon/drag.css's
        ghost.el.classList.add(GHOST_CLASS);
      }
      document.documentElement.classList.add(DRAGGING_CLASS);
      el.classList.add(SOURCE_CLASS);
      window.addEventListener('keydown', onKey, true);
      swallow = true;
      return true;
    },
    move: (p) => {
      ghost?.move(p.x, p.y);
      hooks.move?.(p);
    },
  });

  // Window listeners from the press on: a small icon loses the pointer before it passes the slop.
  const onMove = (e) => { if (e.pointerId === pointerId) machine.move(e.clientX, e.clientY); };
  const onUp = (e) => {
    if (e.pointerId !== pointerId) return;
    listen(false);
    pointerId = null;
    finish();
    machine.release(e.clientX, e.clientY);
    if (swallow) swallowNextClick();
    swallow = false;
  };
  const onLost = (e) => {
    if (e.pointerId !== pointerId) return;
    listen(false);
    pointerId = null;
    finish();
    machine.abort();
  };
  const listen = (on) => {
    const f = on ? 'addEventListener' : 'removeEventListener';
    window[f]('pointermove', onMove, true);
    window[f]('pointerup', onUp, true);
    window[f]('pointercancel', onLost, true);
  };
  el.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || !e.isPrimary || e.pointerType === 'touch' || !enabled()) return;
    pointerId = e.pointerId;
    pressAt = { x: e.clientX, y: e.clientY };
    swallow = false;
    machine.press(e.clientX, e.clientY, e);
    listen(true);
  });
  el.addEventListener('lostpointercapture', (e) => { if (machine.active) onLost(e); });
  return { get active() { return machine.active; }, abort: () => { finish(); machine.abort(); } };
};

// A release point as the small rect a window's opening flight starts from; the desktop's is this size too.
export const DROP_ANCHOR_PX = 24;
export const dropAnchor = (x, y, size = DROP_ANCHOR_PX) =>
  ({ left: x - size / 2, top: y - size / 2, right: x + size / 2, bottom: y + size / 2, width: size, height: size });

// A drop target's glow while a drag is live: `on` marks it as available, `over` as the one under the pointer.
export const markDropTarget = (el, on, over = false) => {
  if (!el?.classList) return;
  el.classList.toggle(TARGET_CLASS, !!on);
  el.classList.toggle(TARGET_OVER_CLASS, !!on && !!over);
};
