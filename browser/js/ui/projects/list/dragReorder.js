import { notify, shortName } from '../../../utils.js';
import { leaveThenRemove, rowLeaveDust, ITEM_DUST_MS } from '../../motion.js';
import { clearedToast } from '../../../core/project/transferController.js';
import { setTranslucentDragImage } from '../../canvas/dragGhost.js';
import { makeTouchDraggable } from '../../canvas/touchDrag.js';
import { createDropZones } from '../window/projectDropZones.js';
import { reconcileManualOrder } from '../window/projectSort.js';
import { createDragMenu } from './dragMenu.js';
import { createDragClose } from './dragClose.js';

// Dragging a project row: reorder (persisted as the session's manual order), the drag-out zones
// and the header's ⋯ menu and ✕. Mouse uses HTML5 DnD, touch the pointer engine — one set of paths.
export function createDragReorder(deps) {
  const {
    list, overlay, app, close, render, sortMode, setSortMode, sortItems, buildItems,
    loadOrder, saveOrder, confirmOpen, openRemote, invalidateRemotes,
    beginRemoval, retireKey, localKey, remoteKey, rowById, header,
  } = deps;
  // A drop rewrites the persisted key order and switches the sort to 'manual'. Seeded from the
  // full current ordering (ignoring the filter) so every project keeps a slot; new ids go last.
  let dragKey = null;
  let didReorder = false;
  let dragActive = false;
  let didZone = false;   // a drag-out zone action ran on the accepted drop (skip dragend render)
  // Row key -> { meta, isRemote } for the current render, so a drop on a drag-out zone can
  // resolve the dragged project without re-parsing the key (server urls contain ':').
  const keyMeta = new Map();
  const clearRowDropCues = () => list.querySelectorAll('.project-drop-before,.project-drop-after')
    .forEach((el) => el.classList.remove('project-drop-before', 'project-drop-after'));
  const persistManualDrop = (draggedKey, targetKey, before) => {
    const mode = sortMode();
    const full = sortItems(buildItems({ applySearch: false }), mode === 'manual' ? 'name' : mode).map((i) => i.key);
    const base = mode === 'manual' ? loadOrder() : [];
    saveOrder(reconcileManualOrder(full, base, draggedKey, targetKey, before));
    setSortMode('manual');
  };

  // Zones are PURELY VISUAL (pointer-events:none) — the action is decided from the pointer's
  // RELEASE position (zoneForPoint). Every zone confirms.
  const { showZones, hideZones, zoneForPoint, highlightZone } = createDropZones(overlay);
  let lastX = 0;
  let lastY = 0;
  // The header's targets: the held row's menu behind a ⋯, and the ✕ for the project open here.
  // A ⋯ item run by the release settles the list itself: no dragend render after it.
  const moreMenu = createDragMenu(header);
  const closeDrop = createDragClose({ app, closeBtn: header.closeBtn, settle: () => render() });
  const headerAt = (x, y) => [moreMenu.track(x, y), closeDrop.track(x, y)].some(Boolean);
  const holdHeader = (row, key) => { moreMenu.begin(row._menuItems); closeDrop.begin(keyMeta.get(key)); };

  // preventDefault over a zone so the cursor reads as droppable and the drop is ACCEPTED, which
  // suppresses the browser's snap-back-to-source animation.
  const onDocDragOver = (e) => {
    if (!dragActive) return;
    lastX = e.clientX; lastY = e.clientY;
    const onHeader = headerAt(lastX, lastY);
    const zone = onHeader ? null : zoneForPoint(lastX, lastY);
    highlightZone(zone);
    // dropEffect MUST stay compatible with effectAllowed ('move', set in dragstart): a 'copy'
    // effect makes the browser REJECT the drop (no drop event, no action). Keep every zone 'move'.
    if (onHeader || zone) { e.preventDefault(); try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ } }
  };
  document.addEventListener('dragover', onDocDragOver);
  // A pointer arriving on the ⋯ or a menu row fires dragenter first, the dragovers only on later ticks.
  document.addEventListener('dragenter', onDocDragOver);
  // Run the zone action on the accepted DROP (not dragend), so there's no snap-back glitch and
  // the action fires immediately. A reorder (drop on a row, stopPropagation) never reaches here.
  const onDocDrop = (e) => {
    if (!dragActive) return;
    if (moreMenu.drop(e.clientX, e.clientY)) { e.preventDefault(); return; }
    if (closeDrop.drop(e.clientX, e.clientY)) { e.preventDefault(); didZone = true; return; }
    const zone = zoneForPoint(e.clientX, e.clientY);
    if (!zone) return;   // over the dialog → row drop / nothing handles it
    e.preventDefault();
    didZone = true;
    performZoneAction(dragKey, zone);
  };
  document.addEventListener('drop', onDocDrop);
  const endDrag = () => {
    dragActive = false; dragKey = null; didReorder = false; didZone = false;
    hideZones(); clearRowDropCues(); moreMenu.end(); closeDrop.end();
    list.querySelectorAll('.project-dragging').forEach((el) => el.classList.remove('project-dragging'));
  };

  // Run the drag-out action for the dropped row (resolved via keyMeta), mirroring the ⋯-menu
  // equivalents so both paths behave identically.
  const performZoneAction = async (key, action) => {
    const info = keyMeta.get(key);
    if (!info) { render(); return; }
    const meta = info.meta;
    if (!info.isRemote) {
      const id = meta.id;
      if (action === 'here') { if (await confirmOpen(meta.name)) { app.projectTransfer.switchToProject(id); close(); } else render(); }
      else if (action === 'newtab') { if (await confirmOpen(meta.name, true)) app.projectTransfer.openProjectInNewTab(id); render(); }
      else if (action === 'remove') {
        const serverLinked = meta.remoteId && meta.address;
        const note = serverLinked
          ? `Remove the local copy of "${shortName(meta.name || 'Untitled')}"? It stays on the server ${meta.address}.`
          : `Remove project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`;
        if (!(await app.confirm(note, { title: 'Remove project', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
        const settle = beginRemoval();
        const revive = retireKey(localKey(id));
        await leaveThenRemove(rowById(id), () => {}, rowLeaveDust(1, 0, ITEM_DUST_MS));
        app.projectTransfer.removeProject(id);
        await settle();
        revive();
        notify(clearedToast(1), 'ok');
      }
      return;
    }
    // Server (remote) row.
    if (action === 'here') { if (!(await confirmOpen(meta.name))) { render(); return; } try { await openRemote(meta); close(); } catch (err) { notify(`Could not open server project — ${err.message}`, 'fail'); render(); } }
    else if (action === 'newtab') { if (await confirmOpen(meta.name, true)) app.projectTransfer.openRemoteProjectInNewTab(meta); render(); }
    else if (action === 'remove') {
      if (!(await app.confirm(`Delete server project "${shortName(meta.name || 'Untitled')}"? This cannot be undone.`, { title: 'Delete server project', danger: true, confirmLabel: 'Yes', confirmIcon: 'trash', cancelLabel: 'No' }))) { render(); return; }
      const conn = app.connections && app.connections.get(meta.serverUrl);
      if (!conn) { notify('Not connected to that server', 'fail'); return; }
      try {
        const settle = beginRemoval();
        const revive = retireKey(remoteKey(meta));
        await leaveThenRemove(rowById(meta.id), () => {}, rowLeaveDust(1, 0, ITEM_DUST_MS));
        await conn.deleteProject(meta.id); invalidateRemotes(); await settle();
        revive();
        notify(clearedToast(1), 'ok');
      }
      catch (err) { notify(`Could not delete — ${err.message}`, 'fail'); }
    }
  };

  const attachRowDrag = (row, key) => {
    row.draggable = true;
    row.dataset.dragKey = key;   // lets the touch path hit-test the drop target via elementFromPoint
    row.addEventListener('dragstart', (e) => {
      // Don't hijack clicks on interactive children (checkbox, ⋯ menu, rename input).
      if (e.target.closest('input,button,select,.project-name-edit')) { e.preventDefault(); return; }
      dragKey = key; didReorder = false; dragActive = true;
      row.classList.add('project-dragging');
      setTranslucentDragImage(e, row);  // translucent cursor-following ghost
      showZones();
      holdHeader(row, key);
      // Mark this as an internal reorder drag so the image-drop overlay ignores it.
      try { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('application/x-stencil-reorder', 'project'); } catch { /* older DnD */ }
    });
    row.addEventListener('dragover', (e) => {
      if (!dragKey || dragKey === key) return;
      e.preventDefault();
      try { e.dataTransfer.dropEffect = 'move'; } catch { /* noop */ }
      const r = row.getBoundingClientRect();
      clearRowDropCues();
      row.classList.add(e.clientY < r.top + r.height / 2 ? 'project-drop-before' : 'project-drop-after');
    });
    row.addEventListener('dragleave', () => row.classList.remove('project-drop-before', 'project-drop-after'));
    row.addEventListener('drop', (e) => {
      if (!dragKey || dragKey === key) return;
      e.preventDefault(); e.stopPropagation();
      const r = row.getBoundingClientRect();
      persistManualDrop(dragKey, key, e.clientY < r.top + r.height / 2);
      didReorder = true;
    });
    row.addEventListener('dragend', () => {
      const acted = didZone || moreMenu.applied;   // a drop action or a ⋯ item settles the list itself
      endDrag();               // resets flags + hides zones (the source row may be detached)
      if (!acted) render();    // reflect a reorder, or clean up after a no-op release
    });

    // Touch/pen: HTML5 DnD never fires on touch, so drive the SAME reorder + zone logic through
    // the pointer engine (long-press to pick up; swipe to scroll). Mouse ignores this path.
    makeTouchDraggable(row, {
      canStart: (e) => !e.target.closest('input,button,select,.project-name-edit'),
      // The pickup is a DRAG, never an open: drop any pending click intent (the engine
      // also swallows the click after a real drag — this covers the pickup itself).
      onStart: () => { row._openGesture?.dragStart(); dragKey = key; didReorder = false; didZone = false; dragActive = true; row.classList.add('project-dragging'); showZones(); holdHeader(row, key); },
      onMove: (x, y) => {
        lastX = x; lastY = y;
        clearRowDropCues();
        const onHeader = headerAt(x, y);
        const zone = onHeader ? null : zoneForPoint(x, y);
        highlightZone(zone);
        if (!zone && !onHeader) {
          const target = document.elementFromPoint(x, y)?.closest('.project-row');
          if (target && target.dataset.dragKey && target.dataset.dragKey !== dragKey) {
            const r = target.getBoundingClientRect();
            target.classList.add(y < r.top + r.height / 2 ? 'project-drop-before' : 'project-drop-after');
          }
        }
      },
      onDrop: (x, y) => {
        if (moreMenu.drop(x, y)) { const quiet = moreMenu.applied; endDrag(); if (!quiet) render(); return; }
        if (closeDrop.drop(x, y)) { endDrag(); return; }
        const zone = zoneForPoint(x, y);
        if (zone) { didZone = true; performZoneAction(dragKey, zone); endDrag(); return; }
        const target = document.elementFromPoint(x, y)?.closest('.project-row');
        if (target && target.dataset.dragKey && target.dataset.dragKey !== dragKey) {
          const r = target.getBoundingClientRect();
          persistManualDrop(dragKey, target.dataset.dragKey, y < r.top + r.height / 2);
        }
        endDrag();
        render();
      },
      onCancel: () => { endDrag(); render(); },
    });
  };

  return { attachRowDrag, keyMeta, isDragging: () => dragActive };
}
