// The menu that springs open under a drag hovering the header mark: every item is a
// drop-only target, and a once-gate guarantees exactly one action per release.
import { createOnceGate } from './onceGate.js';
import { createDragArmer, dragMenuActions } from './dropEntry.js';
import { classifyDrop } from './chatDrop.js';
import { icon } from './icons.js';

export const LOGO_DROP_HINT = 'Drag an image or video here for quick actions';
// Long enough to travel from the mark to the menu.
const LOGO_GRACE_MS = 450;

// A dragged file's MIME type is readable mid-drag (its data is not).
export const fileEntryHint = (dt) => {
  const items = dt && dt.items ? [...dt.items] : [];
  const f = items.find((i) => i.kind === 'file' && /^(image|video)\//i.test(i.type || ''));
  if (!f) return null;
  return /^video\//i.test(f.type)
    ? { kind: 'video', src: '', videoUrl: 'file:pending' }
    : { kind: 'img', src: 'file:pending' };
};

// `menuEl`/`placeMenu`/`closeSharedMenu` are lib/actionMenu.js's shared #action-menu;
// `springMs` is the dwell before the menu opens.
export const createLogoDragMenu = ({
  logoEl, menuEl, placeMenu, closeSharedMenu, dragKind, getDraggingRow, onAction,
  springMs, graceMs = LOGO_GRACE_MS, doc = document,
}) => {
  const brandZone = (node) => !!(node && node.closest && node.closest('header .logo, header h1'));
  const setLogoOver = (on) => logoEl && logoEl.classList.toggle('drop-over', !!on);

  const arm = createDragArmer({
    setArmed: (on) => logoEl && logoEl.classList.toggle('drag-armed', on),
  });

  if (logoEl) {
    // data-title, never a title attribute: that raises Chrome's own popup over ours (lib/tip.js).
    logoEl.dataset.title = LOGO_DROP_HINT;
    const h1 = doc.querySelector('header h1');
    if (h1 && h1.dataset && !h1.dataset.title) h1.dataset.title = LOGO_DROP_HINT;
  }

  const dispatch = createOnceGate();
  // A stray click on the just-dropped item (or the logo under it) is swallowed.
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

  // Travelling across the menu's own padding must not start the grace countdown.
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
      // No click handler, so a release cannot reach the action down a second path.
      b.addEventListener('drop', (e) => {
        e.preventDefault();
        e.stopPropagation();                       // this drop is the item's, not the document's
        if (!dispatch.allow()) return;
        const payload = classifyDrop(e.dataTransfer);
        close();
        setLogoOver(false);
        arm.end();                                 // this drop never reaches the document
        if (payload) onAction(a.id, payload);
      });
      menuEl.appendChild(b);
    }
    open = true;
    menuEl.hidden = false;
    const r = logoEl.getBoundingClientRect();
    // The menu grows out of (and its particles fly from) the mark, not its own corner.
    placeMenu(r.left, r.bottom + 6, { x: r.left + r.width / 2, y: r.top + r.height / 2 });
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
      // A dragover exposes only the payload's types: build from our own row or the file's MIME.
      if (!open && !springTimer) {
        const hint = dragKind(e) === 'internal' ? getDraggingRow() : fileEntryHint(e.dataTransfer);
        springTimer = setTimeout(() => { springTimer = null; openMenu(hint); }, springMs);
      }
    });
  }

  const graceOnDragLeave = (e) => {
    if (brandZone(e.relatedTarget) || (open && menuEl.contains(e.relatedTarget))) return;
    if (!brandZone(e.target) && !(open && menuEl.contains(e.target))) return;
    setLogoOver(false);
    clearSpring();
    if (!open || graceTimer) return;
    graceTimer = setTimeout(() => { graceTimer = null; close(); }, graceMs);
  };

  const release = () => { setLogoOver(false); arm.end(); close(); };

  return {
    graceOnDragLeave,
    release,
    dismiss: () => { arm.end(); close(); },
    armUpdate: (types) => arm.update(types),
    armEnd: () => arm.end(),
    isOpen: () => open,
  };
};
