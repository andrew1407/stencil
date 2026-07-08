// Shared drag-image helper for the reorderable modal lists (projects + connections).
//
// Chrome captures the native drag image at dragstart *before* the synchronous `.dragging`
// opacity class takes effect, so that class only dims the row left behind — not the ghost that
// follows the cursor. To make the in-flight ghost translucent (matching the desktop QDrag
// pixmap), we clone the row, dim the clone, and hand it to setDragImage. The off-screen clone is
// removed on the next tick, once the browser has snapshotted it.
export function setTranslucentDragImage(e, row, opacity = 0.4) {
  try {
    const ghost = row.cloneNode(true);
    ghost.style.opacity = String(opacity);
    // position:absolute + a modest negative top is the canonical setDragImage recipe: Chrome
    // still rasterizes the (rendered, off-screen) element, but reliably — unlike a very large
    // offset, which some builds skip and then fall back to the opaque native ghost.
    ghost.style.position = 'absolute';
    ghost.style.top = '-1000px';
    ghost.style.left = '0';
    ghost.style.width = `${row.offsetWidth}px`;
    ghost.style.pointerEvents = 'none';
    ghost.style.margin = '0';
    document.body.appendChild(ghost);
    const rect = row.getBoundingClientRect();
    e.dataTransfer.setDragImage(ghost, e.clientX - rect.left, e.clientY - rect.top);
    setTimeout(() => ghost.remove(), 0);
  } catch { /* setDragImage unsupported → fall back to the native ghost */ }
}
