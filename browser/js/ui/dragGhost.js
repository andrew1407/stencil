// Drag ghost for the reorderable lists. Ours, not `setDragImage`'s: a native drag image
// mis-applies the grab offset on HiDPI, and mouse + touch (touchDrag.js) share this one.
const GHOST_Z = '100005';
const GHOST_OPACITY = 0.6;

// Decoded at module load: an undecoded drag image makes Chrome fall back to its own snapshot.
const BLANK_DRAG_IMAGE = typeof Image === 'undefined' ? null : new Image();
if (BLANK_DRAG_IMAGE) {
  BLANK_DRAG_IMAGE.src = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==';
}

// `grabX/grabY` is where the pointer took hold of the row; that pixel stays under the pointer.
export function createDragGhost(row, grabX, grabY, opacity = GHOST_OPACITY) {
  const rect = row.getBoundingClientRect();
  const dx = grabX - rect.left;
  const dy = grabY - rect.top;
  const el = row.cloneNode(true);
// A copy of a row must not answer queries meant for the real list (ids, drag keys, the
// source's dragging class) and is hidden from assistive tech.
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
// Scale about the grab point, so the pixel under the pointer stays under it.
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

// Mouse (HTML5 DnD): suppress the browser's drag image and run our own, tracking the
// cursor through `dragover`. Returns a teardown for a drag ended early.
export function setTranslucentDragImage(e, row, opacity = GHOST_OPACITY) {
  let ghost = null;
  try {
    if (BLANK_DRAG_IMAGE) e.dataTransfer.setDragImage(BLANK_DRAG_IMAGE, 0, 0);
    ghost = createDragGhost(row, e.clientX, e.clientY, opacity);
  } catch {
    return () => {};   // no dataTransfer / no clone → the browser's own ghost, as before
  }
// `drag` (source) and `dragover` (target) fire at different moments and some browsers
// report (0,0) on one of them; whichever reports a real position moves the ghost.
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
