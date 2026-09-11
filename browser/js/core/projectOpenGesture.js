// Pure row-open gesture logic lifted out of ui/projectsModal.js — no DOM here.
import { isTouchLike } from '../utils.js';

// ── Opening a project row: gesture → intent ─────────────────────────────────
// MOUSE   click            → confirm, this tab      dblclick        → open now, this tab
//         ⌘/Ctrl+click     → confirm, NEW TAB       ⌘/Ctrl+dblclick → open now, NEW TAB
//         Enter / Space    → confirm, this tab (⌘/Ctrl held → new tab), like a single click
// TOUCH   tap              → confirm, this tab      new tab → the row's ⋯ menu
//
// Touch deliberately has NO hold gesture: press-and-hold is the list's drag-to-reorder
// pickup (touchDrag.js), so the ⋯ menu is the touch route to a new tab. Movement always
// wins: past the slop the gesture belongs to the drag/scroll and any pending open is
// dropped. Pure — unit-tested.
export const DOUBLE_CLICK_MS = 250;    // single-click actions wait this long for a dblclick
export const DRAG_SLOP_PX = 10;        // travel that means "this is a drag/scroll, not a tap"
export const rowOpenIntent = ({ type, ctrlKey, metaKey } = {}) => {
  if (type === 'tap') return { confirm: true, target: 'here' };           // touch only
  const target = (ctrlKey || metaKey) ? 'newtab' : 'here';
  return { confirm: type !== 'dblclick', target };
};

// The gesture machine behind a row. The crux is the DEFERRED single click: a plain click
// waits one double-click interval and a dblclick cancels it, so the confirmation modal
// never flashes open and shut. Timers and the touch test are injected for DOM-free tests.
export const createOpenGesture = ({
  run,
  touch = () => isTouchLike(),
  delay = DOUBLE_CLICK_MS,
  slop = DRAG_SLOP_PX,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let clickTimer = null;
  let press = null;          // { x, y, moved } while a pointer is down on the row
  let swallowNextClick = false;
  const cancelClick = () => { if (clickTimer !== null) { clearTimer(clickTimer); clickTimer = null; } };
  return {
    click(e = {}) {
      cancelClick();
      // The click synthesized after a long press (or a drag) must not also open.
      if (swallowNextClick) { swallowNextClick = false; return; }
      if (touch()) { run(rowOpenIntent({ type: 'tap' })); return; }
      clickTimer = setTimer(() => {
        clickTimer = null;
        run(rowOpenIntent({ type: 'click', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
      }, delay);
    },
    dblclick(e = {}) {
      cancelClick();                     // …the pending single click never happens
      if (touch()) return;               // touch has no double-click gesture
      run(rowOpenIntent({ type: 'dblclick', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
    },
    key(e = {}) {
      cancelClick();
      run(rowOpenIntent({ type: 'key', ctrlKey: e.ctrlKey, metaKey: e.metaKey }));
    },
    // ── Pointer travel: MOVEMENT WINS ──
    // Past the slop the gesture belongs to the drag/scroll: the pending open is dropped
    // and the click that may follow the release is swallowed. Nothing here ever OPENS
    // anything — the hold is the list's reorder pickup, not ours.
    pressStart(pt = {}) {
      swallowNextClick = false;
      press = { x: pt.x || 0, y: pt.y || 0, moved: false };
    },
    pressMove(pt = {}) {
      if (!press || press.moved) return false;
      if (Math.abs((pt.x || 0) - press.x) <= slop && Math.abs((pt.y || 0) - press.y) <= slop) return false;
      press.moved = true;
      cancelClick();                     // a deferred single click never survives a drag
      swallowNextClick = true;           // …nor does the click a drop may synthesize
      return true;
    },
    pressEnd() { const moved = !!press && press.moved; press = null; return moved; },
    // Drag pickup (HTML5 dragstart on mouse, the touch engine's onStart on finger).
    dragStart() { cancelClick(); swallowNextClick = true; press = null; },
    cancel() { cancelClick(); press = null; swallowNextClick = false; },
    get pendingClick() { return clickTimer !== null; },
    get dragging() { return !!press && press.moved; },
  };
};

// Whether an out-of-band change (server event, peers echo, another tab) may rebuild the
// list RIGHT NOW: never mid-drag (the rebuild destroys the dragged row) and never while a
// removal wipe plays — those wait for beginRemoval's settle render. Pure — unit-tested.
export const canRefreshList = ({ open = true, dragging = false, removing = false } = {}) =>
  !!open && !dragging && !removing;
