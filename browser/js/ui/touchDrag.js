// Pointer-based drag for the reorderable modal lists (projects + connections): HTML5
// DnD never fires on TOUCH, so this layer engages only for touch/pen. Long-press picks
// a row up (a quick swipe just scrolls); the caller decides the drop target via
// callbacks; the shared ghost (dragGhost.js) follows the finger, and the trailing
// synthesized click is swallowed after a real drag.
// Rows must set `touch-action: pan-y` (CSS) so the list still scrolls — but once a row
// IS picked up, only a non-passive `touchmove` preventDefault stops the compositor
// claiming the vertical gesture (`pointermove` preventDefault does not, and a
// mid-gesture `touch-action` change is ignored). Registered while the finger is DOWN
// so the moves stay cancelable; it cancels only once dragging.
import { createDragGhost } from './dragGhost.js';

export function makeTouchDraggable(row, opts) {
  const {
    canStart,             // (e) => bool — false ignores this pointerdown (interactive child)
    onStart,              // () => void — drag began (after long-press)
    onMove,               // (clientX, clientY) => void
    onDrop,               // (clientX, clientY) => void
    onCancel,             // () => void — pointercancel while dragging
    longPressMs = 280,
    moveCancelPx = 12,    // finger travel (pre-drag) that reads as a scroll → abandon
  } = opts;

  let sx = 0, sy = 0, pid = null, timer = null, dragging = false, ghost = null;

  // Registered for the whole finger-down sequence so the browser keeps the moves cancelable,
  // but only cancels them once the row has been picked up — a plain swipe still scrolls.
  const holdGesture = (e) => { if (dragging && e.cancelable) e.preventDefault(); };

  const cleanup = () => {
    if (ghost) { ghost.destroy(); ghost = null; }
    if (pid != null) { try { row.releasePointerCapture(pid); } catch { /* not captured */ } }
    document.removeEventListener('touchmove', holdGesture, { capture: true });
    row.style.touchAction = '';
    clearTimeout(timer); timer = null; dragging = false; pid = null;
  };

  const onDown = (e) => {
    if (e.pointerType === 'mouse') return;            // mouse → native HTML5 DnD path
    if (canStart && !canStart(e)) return;             // don't hijack checkbox / ⋯ / rename
    sx = e.clientX; sy = e.clientY; pid = e.pointerId;
    document.addEventListener('touchmove', holdGesture, { capture: true, passive: false });
    timer = setTimeout(() => {
      dragging = true;
      row.style.touchAction = 'none';                 // belt-and-braces: UAs that re-read it
      try { row.setPointerCapture(pid); } catch { /* older UA */ }
      ghost = createDragGhost(row, sx, sy);
      onStart && onStart();
    }, longPressMs);
  };
  const onMoveEv = (e) => {
    if (e.pointerId !== pid) return;
    if (!dragging) {                                   // still deciding hold-vs-swipe
      if (Math.hypot(e.clientX - sx, e.clientY - sy) > moveCancelPx) cleanup();   // a scroll, not a pickup
      return;
    }
    e.preventDefault();                                // text selection etc. (scroll: holdGesture)
    ghost.move(e.clientX, e.clientY);
    onMove && onMove(e.clientX, e.clientY);
  };
  const finish = (e, cancelled) => {
    if (e.pointerId !== pid) return;
    const wasDragging = dragging;
    const x = e.clientX, y = e.clientY;
    cleanup();
    if (!wasDragging) return;
    // Swallow the click the browser synthesizes after this pointer sequence so a drag-drop
    // doesn't also trigger the row's tap-to-open. It has to be caught on the DOCUMENT: the
    // drop re-renders the list, so by the time the click lands `row` is detached and a
    // listener on it would never see it (a hold-and-release would open the project).
    const swallow = (ev) => { ev.stopPropagation(); ev.preventDefault(); };
    document.addEventListener('click', swallow, { capture: true, once: true });
    setTimeout(() => document.removeEventListener('click', swallow, true), 500);
    if (cancelled) onCancel && onCancel(); else onDrop && onDrop(x, y);
  };

  row.addEventListener('pointerdown', onDown);
  row.addEventListener('pointermove', onMoveEv, { passive: false });
  row.addEventListener('pointerup', (e) => finish(e, false));
  row.addEventListener('pointercancel', (e) => finish(e, true));
}
