// Dragging a connection row: reorder by dropping on another row, disconnect by dropping outside
// the modal. Both the HTML5 and the touch path (ui/touchDrag.js) end in the same two outcomes.
import { setTranslucentDragImage } from '../dragGhost.js';
import { makeTouchDraggable } from '../touchDrag.js';

export function createConnectRowDrag({ list, overlay, mgr, render, confirmDisconnect }) {
  // didReorder: an in-list drop already reordered (dragend must not also drag-out remove).
  let draggingUrl = null;
  let didReorder = false;
  let dragActive = false;

  const orderForDrop = (targetUrl, before) => {
    const cur = mgr().urls.filter((u) => u !== draggingUrl);
    let idx = cur.indexOf(targetUrl);
    if (idx < 0) idx = cur.length - 1;
    cur.splice(before ? idx : idx + 1, 0, draggingUrl);
    return cur;
  };
  const clearDropCues = () => list.querySelectorAll('.connect-drop-before,.connect-drop-after')
    .forEach((el) => el.classList.remove('connect-drop-before', 'connect-drop-after'));
  // Dropped clear of the card → the same confirm the row's own trash button asks.
  const droppedOutside = (x, y) => {
    const card = overlay.querySelector('.app-modal');
    const box = card && card.getBoundingClientRect();
    return !!box && (x < box.left || x > box.right || y < box.top || y > box.bottom);
  };

  // Drags starting on an input/button are suppressed.
  const attach = (row, url) => {
    row.draggable = true;
    row.addEventListener('dragstart', (e) => {
      if (e.target.closest('input,button')) { e.preventDefault(); return; }
      draggingUrl = url; didReorder = false; dragActive = true;
      row.classList.add('connect-dragging');
      setTranslucentDragImage(e, row);
      // An internal reorder marker, not the url in text/plain (that popped the image-drop overlay).
      try { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('application/x-stencil-reorder', 'connection'); } catch { /* older DnD */ }
    });
    row.addEventListener('dragover', (e) => {
      if (!draggingUrl || draggingUrl === url) return;
      e.preventDefault();
      try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ }
      const r = row.getBoundingClientRect();
      const before = e.clientY < r.top + r.height / 2;
      clearDropCues();
      row.classList.add(before ? 'connect-drop-before' : 'connect-drop-after');
    });
    row.addEventListener('dragleave', () => row.classList.remove('connect-drop-before', 'connect-drop-after'));
    row.addEventListener('drop', (e) => {
      if (!draggingUrl || draggingUrl === url) return;
      e.preventDefault();
      e.stopPropagation();
      const r = row.getBoundingClientRect();
      const before = e.clientY < r.top + r.height / 2;
      mgr().reorder(orderForDrop(url, before));
      didReorder = true;
    });
    row.addEventListener('dragend', async (e) => {
      const dragged = draggingUrl;
      draggingUrl = null; dragActive = false;
      row.classList.remove('connect-dragging');
      clearDropCues();
      if (didReorder) { didReorder = false; render(); return; }
      if (droppedOutside(e.clientX, e.clientY) && dragged) await confirmDisconnect(dragged);
      else render();
    });
    makeTouchDraggable(row, {
      canStart: (e) => !e.target.closest('input,button'),
      onStart: () => { draggingUrl = url; didReorder = false; dragActive = true; row.classList.add('connect-dragging'); },
      onMove: (x, y) => {
        clearDropCues();
        const target = document.elementFromPoint(x, y)?.closest('.connect-row');
        if (target && target.dataset.url && target.dataset.url !== draggingUrl) {
          const r = target.getBoundingClientRect();
          target.classList.add(y < r.top + r.height / 2 ? 'connect-drop-before' : 'connect-drop-after');
        }
      },
      onDrop: async (x, y) => {
        const dragged = draggingUrl;
        const target = document.elementFromPoint(x, y)?.closest('.connect-row');
        clearDropCues();
        row.classList.remove('connect-dragging');
        if (target && target.dataset.url && target.dataset.url !== dragged) {
          const r = target.getBoundingClientRect();
          const order = orderForDrop(target.dataset.url, y < r.top + r.height / 2);
          draggingUrl = null; dragActive = false;
          mgr().reorder(order);
          render();
          return;
        }
        draggingUrl = null; dragActive = false;
        if (droppedOutside(x, y) && dragged) await confirmDisconnect(dragged);
        else render();
      },
      onCancel: () => { draggingUrl = null; dragActive = false; row.classList.remove('connect-dragging'); clearDropCues(); render(); },
    });
  };

  return { attach, isDragging: () => dragActive };
}
