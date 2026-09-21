// Spring-loaded drop targets: a COLLAPSED section can't take a drop, so the one the
// pointer dwells on unfolds. Every DOM action is injected and goes through the SAME
// toggler the header click uses.

export const ASSISTANT_SECTION = 'sec-assistant';
export const SEARCH_SECTION = 'sec-search';

// WHICH sections a drag can spring is a property of the surface, not of the kind.
export const DRAG_KINDS = Object.freeze(['internal', 'files', 'url', 'external']);

// Dwell before a collapsed section unfolds, so sweeping across a header doesn't pop it.
export const SPRING_DWELL_MS = 300;

// A drag that left the window may come back; the fold-back waits this long.
export const RESTORE_DELAY_MS = 400;

// The section a drag over `sectionId` should spring open, or ''.
export const sectionForDragPoint = (kind, sectionId, { collapsed = {}, sections = [] } = {}) => {
  if (!DRAG_KINDS.includes(kind)) return '';
  if (!sectionId || !sections.includes(sectionId)) return '';
  return collapsed[sectionId] === true ? sectionId : '';
};

// `sections` are this surface's drop targets; `expand`/`collapse` MUST go through the
// shared toggler.
export const createDragSectionOpener = ({
  sections = [ASSISTANT_SECTION],
  isCollapsed,
  expand,
  collapse,
  onOpen,
  dwellMs = SPRING_DWELL_MS,
  timer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  // Sections THIS drag sprang open and still owes a fold-back.
  const auto = new Set();

  // Deferred fold-back, with a generation guard against a timer that fires anyway.
  let pending = null;
  let restoreGen = 0;

  let armedId = '';
  let armTimer = null;
  let armGen = 0;

  const cancelPending = () => {
    if (pending !== null) { clearTimer(pending); pending = null; }
    restoreGen++;
  };

  const disarm = () => {
    if (armTimer !== null) { clearTimer(armTimer); armTimer = null; }
    armedId = '';
    armGen++;
  };

  const restore = () => {
    cancelPending();
    disarm();
    for (const id of auto) collapse(id);
    auto.clear();
  };

  return {
    pendingRestore: () => [...auto],
    armedSection: () => armedId,

    // dragover fires continuously: re-reporting the SAME section keeps the running dwell
    // (restarting it would never elapse).
    pointerOver(kind, sectionId) {
      cancelPending();
      const want = sectionForDragPoint(kind, sectionId, {
        collapsed: sectionId ? { [sectionId]: !!isCollapsed(sectionId) } : {},
        sections,
      });
      if (!want) { if (armedId) disarm(); return; }
      if (want === armedId) return;
      disarm();
      armedId = want;
      const gen = armGen;
      armTimer = timer(() => {
        if (gen !== armGen) return;
        armTimer = null;
        armedId = '';
        if (!isCollapsed(want)) return;
        expand(want);
        auto.add(want);
        if (onOpen) onOpen(want);
      }, dwellMs);
    },

    // A section toggled by hand or dropped into is the user's now.
    manualToggle(id) { auto.delete(id); },

    dropIn(id) { auto.delete(id); },

    end() { restore(); },

    // The drag left the window: the pending dwell dies now, the fold-back waits.
    scheduleEnd(delay = RESTORE_DELAY_MS) {
      disarm();
      if (!auto.size || pending !== null) return;
      const gen = restoreGen;
      pending = timer(() => {
        if (gen !== restoreGen) return;
        pending = null;
        restore();
      }, delay);
    },
  };
};
