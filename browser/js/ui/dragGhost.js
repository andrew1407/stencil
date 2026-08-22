// Shared drag ghost for the reorderable modal lists — the translucent copy of the row
// that follows the pointer. OURS, not `setDragImage`'s: the browser rasterizes a native
// drag image at device scale and mis-applies the grab offset on HiDPI; a positioned
// element stays exact, and mouse + touch (touchDrag.js) show the SAME ghost.
const GHOST_Z = '100005';
const GHOST_OPACITY = 0.6;

// A 1×1 transparent PNG stands in for the native drag image. Decoded at module load so it
// is ready at dragstart — an undecoded image makes Chrome fall back to its own snapshot.
const BLANK_DRAG_IMAGE = typeof Image === 'undefined' ? null : new Image();
if (BLANK_DRAG_IMAGE) {
  BLANK_DRAG_IMAGE.src = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==';
}

// A ghost that follows a point. `grabX/grabY` is where the pointer took hold of the row, so
// that same pixel stays under the pointer for the whole drag.
export function createDragGhost(row, grabX, grabY, opacity = GHOST_OPACITY) {
  const rect = row.getBoundingClientRect();
  const dx = grabX - rect.left;
  const dy = grabY - rect.top;
  const el = row.cloneNode(true);
  // A copy of a row is not a row: strip what would make it answer queries meant for the
  // real list (duplicate ids, the drag keys the drop hit-test reads, the source's own
  // "I am being dragged" dimming class) and hide it from assistive tech.
  el.setAttribute('data-drag-ghost', '');
  el.setAttribute('aria-hidden', 'true');
  el.classList.remove(...[...el.classList].filter((c) => c.endsWith('-dragging')));
  for (const node of [el, ...el.querySelectorAll('*')]) {
    node.removeAttribute('id');
    node.removeAttribute('data-id');
    node.removeAttribute('data-drag-key');
  }
  Object.assign(el.style, {
    position: 'fixed', left: `${rect.left}px`, top: `${rect.top}px`, width: `${rect.width}px`,
    margin: '0', opacity: String(opacity), pointerEvents: 'none', zIndex: GHOST_Z,
    // Grow the lift about the grab point, so the pixel under the pointer stays under it —
    // scaling about the default centre slides the ghost by half the growth.
    transformOrigin: `${dx}px ${dy}px`,
    transform: 'scale(1.02)', boxShadow: '0 8px 24px rgba(0,0,0,0.4)',
  });
  document.body.appendChild(el);
  return {
    el,
    move(x, y) { el.style.left = `${x - dx}px`; el.style.top = `${y - dy}px`; },
    destroy() { el.remove(); },
  };
}

// Mouse (HTML5 DnD): suppress the browser's drag image and run our own for the drag's
// lifetime, tracking the cursor through `dragover`. Returns a teardown for callers that
// end a drag early; dragend/drop clean up on their own.
export function setTranslucentDragImage(e, row, opacity = GHOST_OPACITY) {
  let ghost = null;
  try {
    if (BLANK_DRAG_IMAGE) e.dataTransfer.setDragImage(BLANK_DRAG_IMAGE, 0, 0);
    ghost = createDragGhost(row, e.clientX, e.clientY, opacity);
  } catch {
    return () => {};   // no dataTransfer / no clone → the browser's own ghost, as before
  }
  // Two sources, because neither alone keeps up: `drag` fires on the source element and
  // `dragover` on whatever is under the cursor, at different moments in the drag loop, and
  // some browsers report (0,0) on one of them. Take whichever reports a real position — the
  // ghost then tracks within a frame instead of visibly trailing the cursor.
  const onOver = (ev) => { if (ev.clientX || ev.clientY) ghost.move(ev.clientX, ev.clientY); };
  const stop = () => {
    ghost.destroy();
    row.removeEventListener('drag', onOver, true);
    document.removeEventListener('dragover', onOver, true);
    document.removeEventListener('dragend', stop, true);
    document.removeEventListener('drop', stop, true);
  };
  row.addEventListener('drag', onOver, true);
  document.addEventListener('dragover', onOver, true);
  document.addEventListener('dragend', stop, true);
  document.addEventListener('drop', stop, true);
  return stop;
}
