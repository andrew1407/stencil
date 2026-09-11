import { createDragSectionOpener, ASSISTANT_SECTION, SEARCH_SECTION, SPRING_DWELL_MS } from '../lib/dragSections.js';
import { createLogoDragMenu } from '../lib/logoDragMenu.js';
import { entryFromDrop, dragActionAllowed, dragPayloadKind } from '../lib/dropEntry.js';
import { shortName } from '../lib/displayName.js';
import { sourceOf } from '../lib/imageModel.js';
import { listEl, menuEl, statusEl, run, IS_SIDE_PANEL, IS_DEVTOOLS } from './panelDom.js';
import { state } from './model.js';
import { openHere, openCrop } from './openActions.js';
import { placeMenu, closeMenu } from './rowMenu.js';
import { getDraggingRow } from './gestures.js';
import { sections } from './sections.js';

// ── Spring-loaded drop targets: a COLLAPSED section can't accept a drop ──────
// While a drag is live, the section the POINTER dwells on unfolds — only that one — and
// folds back if the drag ends elsewhere (lib/dragSections.js owns the rules). A HIDDEN
// section (assistant with provider off) is absent, never sprung.
export const dragSections = createDragSectionOpener({
  sections: (IS_SIDE_PANEL || IS_DEVTOOLS) ? [ASSISTANT_SECTION, SEARCH_SECTION] : [ASSISTANT_SECTION],
  // Read through an arrow: ./sections.js is the other half of this pair, so its
  // controller can still be in flight while this one is built.
  isCollapsed: (id) => sections.isCollapsed(id),
  expand: (id) => sections.setCollapsed(id, false),
  collapse: (id) => sections.setCollapsed(id, true),
  // Only scroll if the freshly unfolded body isn't fully visible — the pointer is
  // already ON this section, so the layout must move as little as possible under it.
  onOpen: (id) => document.getElementById(id)?.scrollIntoView({ block: 'nearest' }),
});

// What is being dragged: our own list rows carry the x-stencil-drag type; anything
// else must look like an image/video payload (the same kinds chatDrop.js classifies).
const dragKind = (e) => dragPayloadKind(e.dataTransfer && e.dataTransfer.types);
// The collapsible section under the pointer — for a collapsed one that's its header
// row, which is all of it that's left on screen.
const sectionUnder = (node) => (node && node.closest ? (node.closest('.fsection')?.id || '') : '');
for (const type of ['dragenter', 'dragover']) {
  document.addEventListener(type, (e) => {
    // Any compatible drag anywhere over the surface advertises the logo as a target
    // (it pulses) — the point is to say "you can drop here" BEFORE the pointer arrives.
    logoMenu.armUpdate(e.dataTransfer && e.dataTransfer.types);
    const kind = dragKind(e);
    if (kind) dragSections.pointerOver(kind, sectionUnder(e.target));
  }, true);
}
// A drop INSIDE an auto-opened section keeps it open (capture phase, so it is recorded
// before the section's own drop handler and before the document-level `end()` below).
document.getElementById(ASSISTANT_SECTION)?.addEventListener('drop', () => dragSections.dropIn(ASSISTANT_SECTION), true);
listEl.addEventListener('drop', () => dragSections.dropIn(SEARCH_SECTION), true);
// ── Header logo: a SPRING-LOADED drag menu for media dragged off the PAGE ────
// The whole mechanism — spring dwell, grace timer, drop-only items, the once-per-release
// gate — lives in lib/logoDragMenu.js; this wires its document-level end-of-drag paths.

// Perform one drag-menu action on the released payload. The entry is normalised at
// DROP time (the only moment the payload is readable), so the optimistic menu is
// re-checked here: an action that turns out not to apply says so instead of throwing.
const runDragMenuAction = (id, payload) => {
  const entry = entryFromDrop(payload, { items: state.all, objectUrl: (f) => URL.createObjectURL(f) });
  if (!entry) { statusEl.textContent = 'Couldn’t read an image or video from that drop.'; return; }
  if (!dragActionAllowed(entry, id)) {
    statusEl.textContent = `“${shortName(entry.name)}” has no image to ${id === 'crop' ? 'crop' : 'open in the editor'} — try “Open in new tab”.`;
    return;
  }
  if (id === 'newtab') { chrome.tabs.create({ url: sourceOf(entry) }); return; }
  if (id === 'crop') { run(() => openCrop(entry)); return; }
  // "Open in editor" is the row menu's own default: the in-page modal ("Here") — or, in
  // editor mode, an import into the editor tab the panel is standing on.
  run(() => openHere(entry, id === 'incognito'));
};

export const logoMenu = createLogoDragMenu({
  logoEl: document.querySelector('header .logo'),
  menuEl,
  placeMenu,
  closeSharedMenu: closeMenu,
  dragKind,
  getDraggingRow,
  onAction: runDragMenuAction,
  springMs: SPRING_DWELL_MS,   // dwell before the menu springs open (matches the section spring)
});

// ONE document dragleave listener runs the three end-of-drag branches in order; each
// keeps its own guard and they touch disjoint state.
document.addEventListener('dragleave', (e) => {
  logoMenu.graceOnDragLeave(e);
  if (!e.relatedTarget) {
    // The drag left the window: stop advertising at once (the pointer is gone), but
    // only SCHEDULE the section fold-back in case it comes back.
    logoMenu.armEnd();
    dragSections.scheduleEnd();
  } else {
    // Leaving a section (into a sibling) must also cancel a dwell that hasn't sprung
    // yet — pointerOver('') does that; the drag is still live.
    const kind = dragKind(e);
    if (kind && sectionUnder(e.relatedTarget) !== sectionUnder(e.target)) {
      dragSections.pointerOver(kind, sectionUnder(e.relatedTarget));
    }
  }
});
// Released anywhere but on an item (the item's own handler stops that drop), the drag
// ending, or Escape: close and do nothing; a drop also folds the drag-out sections back.
document.addEventListener('drop', () => { logoMenu.release(); dragSections.end(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') logoMenu.dismiss(); });
document.addEventListener('dragend', () => { logoMenu.release(); dragSections.end(); });
