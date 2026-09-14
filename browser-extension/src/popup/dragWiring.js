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

// Spring-loaded drop targets: a collapsed section cannot accept a drop, so the one the
// pointer dwells on unfolds (lib/dragSections.js owns the rules).
export const dragSections = createDragSectionOpener({
  sections: (IS_SIDE_PANEL || IS_DEVTOOLS) ? [ASSISTANT_SECTION, SEARCH_SECTION] : [ASSISTANT_SECTION],
  // Read through an arrow: ./sections.js's controller can still be in flight here.
  isCollapsed: (id) => sections.isCollapsed(id),
  expand: (id) => sections.setCollapsed(id, false),
  collapse: (id) => sections.setCollapsed(id, true),
  // The pointer is already ON this section, so the layout must move as little as possible.
  onOpen: (id) => document.getElementById(id)?.scrollIntoView({ block: 'nearest' }),
});

const dragKind = (e) => dragPayloadKind(e.dataTransfer && e.dataTransfer.types);
const sectionUnder = (node) => (node && node.closest ? (node.closest('.fsection')?.id || '') : '');
for (const type of ['dragenter', 'dragover']) {
  document.addEventListener(type, (e) => {
    // The logo advertises itself as a target BEFORE the pointer arrives.
    logoMenu.armUpdate(e.dataTransfer && e.dataTransfer.types);
    const kind = dragKind(e);
    if (kind) dragSections.pointerOver(kind, sectionUnder(e.target));
  }, true);
}
// Capture phase: a drop inside an auto-opened section is recorded before `end()` below.
document.getElementById(ASSISTANT_SECTION)?.addEventListener('drop', () => dragSections.dropIn(ASSISTANT_SECTION), true);
listEl.addEventListener('drop', () => dragSections.dropIn(SEARCH_SECTION), true);

// The payload is readable only at DROP time, so the optimistic menu is re-checked here.
const runDragMenuAction = (id, payload) => {
  const entry = entryFromDrop(payload, { items: state.all, objectUrl: (f) => URL.createObjectURL(f) });
  if (!entry) { statusEl.textContent = 'Couldn’t read an image or video from that drop.'; return; }
  if (!dragActionAllowed(entry, id)) {
    statusEl.textContent = `“${shortName(entry.name)}” has no image to ${id === 'crop' ? 'crop' : 'open in the editor'} — try “Open in new tab”.`;
    return;
  }
  if (id === 'newtab') { chrome.tabs.create({ url: sourceOf(entry) }); return; }
  if (id === 'crop') { run(() => openCrop(entry)); return; }
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
  springMs: SPRING_DWELL_MS,   // matches the section spring
});

document.addEventListener('dragleave', (e) => {
  logoMenu.graceOnDragLeave(e);
  if (!e.relatedTarget) {
    // The drag left the window: only SCHEDULE the fold-back in case it comes back.
    logoMenu.armEnd();
    dragSections.scheduleEnd();
  } else {
    // Leaving a section into a sibling cancels a dwell that has not sprung yet.
    const kind = dragKind(e);
    if (kind && sectionUnder(e.relatedTarget) !== sectionUnder(e.target)) {
      dragSections.pointerOver(kind, sectionUnder(e.relatedTarget));
    }
  }
});
// An item's own handler stops its drop before these fire.
document.addEventListener('drop', () => { logoMenu.release(); dragSections.end(); });
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') logoMenu.dismiss(); });
document.addEventListener('dragend', () => { logoMenu.release(); dragSections.end(); });
