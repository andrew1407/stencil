// ── The header logo's SPRING-LOADED drag menu ───────────────────────────────
// Extracted from popup.js. Hover a dragged <img>/<video> over the Stencil mark and a
// flat drop-aware menu springs open under the drag; each item is itself a drop target,
// and releasing anywhere else does nothing. The items are drop-ONLY (no click handler),
// and a once-gate guarantees exactly one action per release; a stray click on the
// just-dropped item (or the logo under it) is swallowed. The shared #action-menu
// element and its placement come from the owner (lib/actionMenu.js).
import { createOnceGate } from './onceGate.js';
import { createDragArmer, dragMenuActions } from './dropEntry.js';
import { classifyDrop } from './chatDrop.js';
import { icon } from './icons.js';

export const LOGO_DROP_HINT = 'Drag an image or video here for quick actions';
// Grace period after leaving the logo — long enough to travel to the menu.
export const LOGO_GRACE_MS = 450;

// A dragged FILE's MIME type IS readable mid-drag (unlike its data), so a video file
// gets the video-guarded menu straight away; unknown payloads get the optimistic menu.
export const fileEntryHint = (dt) => {
  const items = dt && dt.items ? [...dt.items] : [];
  const f = items.find((i) => i.kind === 'file' && /^(image|video)\//i.test(i.type || ''));
  if (!f) return null;
  return /^video\//i.test(f.type)
    ? { kind: 'video', src: '', videoUrl: 'file:pending' }
    : { kind: 'img', src: 'file:pending' };
};

/**
 * @param {object} deps
 * @param {HTMLElement|null} deps.logoEl - The header mark (null on a page without one).
 * @param {HTMLElement} deps.menuEl - The shared #action-menu element.
 * @param {(x: number, y: number) => void} deps.placeMenu - actionMenu's placement.
 * @param {() => void} deps.closeSharedMenu - actionMenu's close (the row menu may be open).
 * @param {(e: DragEvent) => string|null} deps.dragKind - Payload classification for a
 *   live drag ('internal' = one of our own rows).
 * @param {() => object|null} deps.getDraggingRow - The row being dragged out of our own
 *   list (a dragover can read the payload's TYPES but never its data).
 * @param {(id: string, payload: object) => void} deps.onAction - Runs the chosen action
 *   on the classified drop payload.
 * @param {number} deps.springMs - Dwell before the menu springs open.
 * @param {number} [deps.graceMs]
 * @param {Document} [deps.doc]
 */
export const createLogoDragMenu = ({
  logoEl, menuEl, placeMenu, closeSharedMenu, dragKind, getDraggingRow, onAction,
  springMs, graceMs = LOGO_GRACE_MS, doc = document,
}) => {
  const brandZone = (node) => !!(node && node.closest && node.closest('header .logo, header h1'));
  const setLogoOver = (on) => logoEl && logoEl.classList.toggle('drop-over', !!on);

  // "A compatible drag is live" → the logo pulses (CSS keyframes). Armed from anywhere
  // on the surface, dropped on drop/dragend/window-leave.
  const arm = createDragArmer({
    setArmed: (on) => logoEl && logoEl.classList.toggle('drag-armed', on),
  });

  if (logoEl) {
    // data-title, never an SVG <title> child or a title attribute — both raise Chrome's
    // own popup on top of ours (lib/tip.js). The logo names itself with aria-label.
    logoEl.dataset.title = LOGO_DROP_HINT;
    const h1 = doc.querySelector('header h1');
    if (h1 && h1.dataset && !h1.dataset.title) h1.dataset.title = LOGO_DROP_HINT;
  }

  // One action per release, no matter how many ways the release reaches us…
  const dispatch = createOnceGate();
  // …and a stray click on the just-dropped item (or the logo under it) is swallowed.
  doc.addEventListener('click', (e) => {
    if (!dispatch.suppressed()) return;
    if (!menuEl.contains(e.target) && !brandZone(e.target)) return;
    e.preventDefault();
    e.stopPropagation();
  }, true);

  let open = false;
  let springTimer = null;
  let graceTimer = null;
  const clearSpring = () => { if (springTimer) { clearTimeout(springTimer); springTimer = null; } };
  const clearGrace = () => { if (graceTimer) { clearTimeout(graceTimer); graceTimer = null; } };

  const close = () => {
    clearSpring();
    clearGrace();
    if (!open) return;
    open = false;
    closeSharedMenu();
  };

  // Travelling across the menu's own padding (between items) must not start the grace
  // countdown. Registered ONCE, not per open.
  menuEl.addEventListener('dragover', (e) => {
    if (!open) return;
    e.preventDefault();
    clearGrace();
  });

  const openMenu = (entryHint) => {
    if (open) return;
    const actions = dragMenuActions(entryHint);
    if (!actions.length) return;
    closeSharedMenu();
    menuEl.innerHTML = '';
    for (const a of actions) {
      const b = doc.createElement('button');
      b.type = 'button';
      b.className = 'drag-item';
      b.dataset.action = a.id;
      b.innerHTML = `<span class="ic">${icon(a.icon, { size: 15 })}</span>${a.label}`;
      // Each item IS a drop target: hovering highlights it, releasing runs it.
      const over = (e) => {
        e.preventDefault();
        e.stopPropagation();
        try { e.dataTransfer.dropEffect = 'copy'; } catch { /* locked dataTransfer */ }
        clearGrace();
        b.classList.add('over');
      };
      b.addEventListener('dragenter', over);
      b.addEventListener('dragover', over);
      b.addEventListener('dragleave', () => b.classList.remove('over'));
      // Drop-ONLY: no click handler, so a release can't reach the action down a second
      // path; the gate closes that door for good (a synthesized click, a re-dispatch).
      b.addEventListener('drop', (e) => {
        e.preventDefault();
        e.stopPropagation();                       // this drop is the item's, not the document's
        if (!dispatch.allow()) return;
        const payload = classifyDrop(e.dataTransfer);
        close();                                   // close BEFORE dispatching
        setLogoOver(false);
        arm.end();                                 // this drop never reaches the document
        if (payload) onAction(a.id, payload);
      });
      menuEl.appendChild(b);
    }
    open = true;
    menuEl.hidden = false;
    const r = logoEl.getBoundingClientRect();
    placeMenu(r.left, r.bottom + 6);
  };

  for (const type of ['dragenter', 'dragover']) {
    doc.addEventListener(type, (e) => {
      const inMenu = open && menuEl.contains(e.target);
      if (!brandZone(e.target) && !inMenu) return;
      if (!dragKind(e)) return;
      e.preventDefault();                          // required to allow the drop
      try { e.dataTransfer.dropEffect = 'copy'; } catch { /* locked dataTransfer */ }
      clearGrace();
      if (inMenu) return;
      setLogoOver(true);
      // A dragover can only read the payload's TYPES, never its data, so the menu is
      // built from what we do know: our own dragged row, or a dragged FILE's MIME type.
      if (!open && !springTimer) {
        const hint = dragKind(e) === 'internal' ? getDraggingRow() : fileEntryHint(e.dataTransfer);
        springTimer = setTimeout(() => { springTimer = null; openMenu(hint); }, springMs);
      }
    });
  }

  // Leaving the logo (or the menu) starts a short grace countdown rather than closing
  // at once, so the pointer can travel from the mark to the item it is aiming for.
  const graceOnDragLeave = (e) => {
    if (brandZone(e.relatedTarget) || (open && menuEl.contains(e.relatedTarget))) return;
    if (!brandZone(e.target) && !(open && menuEl.contains(e.target))) return;
    setLogoOver(false);
    clearSpring();
    if (!open || graceTimer) return;
    graceTimer = setTimeout(() => { graceTimer = null; close(); }, graceMs);
  };

  // The release / end-of-drag paths the owner wires to its document listeners.
  const release = () => { setLogoOver(false); arm.end(); close(); };

  return {
    graceOnDragLeave,
    release,
    /** Escape: stop advertising and close (the drop cue clears on the drag's own end). */
    dismiss: () => { arm.end(); close(); },
    armUpdate: (types) => arm.update(types),
    armEnd: () => arm.end(),
    isOpen: () => open,
  };
};
