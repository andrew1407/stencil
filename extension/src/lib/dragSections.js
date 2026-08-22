// ── Spring-loaded drop targets: opening the collapsed section under the pointer ─
// A COLLAPSED section can't accept a drop — its body is `display: none` — so dragging
// an image at a folded Assistant / Found-resources section was a dead end. This is the
// classic spring-loaded-folder answer: while a drag is live, the section the POINTER
// dwells on unfolds, and only that one. Nothing opens up front, and a section the drag
// never visited is never touched.
//
// Pure decision + bookkeeping only: every DOM action (is it collapsed? expand it,
// collapse it, scroll to it) is INJECTED by popup.js, which routes expansion through
// the SAME toggler the section header click uses — so aria-expanded, the chevron, the
// assistant's lazy boot and the body.search-collapsed coupling all stay in sync.
// `node --test` drives the whole thing with stubs.

export const ASSISTANT_SECTION = 'sec-assistant';
export const SEARCH_SECTION = 'sec-search';

// Drag payloads that may spring anything open (popup.js classifies them): our own list
// rows, plus the external image/video payload kinds chatDrop.js accepts. WHICH sections
// a drag can spring is a property of the surface, not of the kind — an internal row
// drag springs the chat, and on the persistent surfaces (where the list itself is a
// drag-to-pin target) the results list too.
export const DRAG_KINDS = ['internal', 'files', 'url', 'external'];

// How long the pointer must dwell on a collapsed section before it unfolds, so merely
// sweeping across a header on the way somewhere else doesn't pop it open.
export const SPRING_DWELL_MS = 300;

// A drag that left the window may just be passing over the page and come back, so the
// fold-back is deferred by this much and cancelled if the drag returns — collapsing
// under a live drag is the jank we're avoiding.
export const RESTORE_DELAY_MS = 400;

/**
 * The section a drag hovering `sectionId` should spring open, or '' for none. This is
 * the whole decision: "which section is under the pointer, and can it take a drop?"
 * @param {string} kind - Drag payload kind (DRAG_KINDS); anything else springs nothing.
 * @param {string} sectionId - The section under the pointer ('' when over none).
 * @param {object} ctx
 * @param {Record<string, boolean>} ctx.collapsed - Collapsed state by section id.
 * @param {string[]} ctx.sections - Sections that are drop targets on this surface.
 * @returns {string} the section id to spring open, or ''.
 */
export const sectionForDragPoint = (kind, sectionId, { collapsed = {}, sections = [] } = {}) => {
  if (!DRAG_KINDS.includes(kind)) return '';
  if (!sectionId || !sections.includes(sectionId)) return '';
  return collapsed[sectionId] === true ? sectionId : '';
};

/**
 * Build the spring-open controller.
 * @param {object} io
 * @param {string[]} io.sections   - Sections that are drop targets on this surface
 *   (the popup has no drag-to-pin list, so it passes the chat alone).
 * @param {Function} io.isCollapsed- (id) => boolean
 * @param {Function} io.expand     - (id) => void  — MUST go through the shared toggler
 * @param {Function} io.collapse   - (id) => void  — likewise
 * @param {Function} [io.onOpen]   - (id) => void  — e.g. scroll it into view
 * @param {number}   [io.dwellMs]  - Hover dwell before springing (SPRING_DWELL_MS)
 * @param {Function} [io.timer]    - setTimeout seam
 * @param {Function} [io.clearTimer] - clearTimeout seam
 */
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
  // Sections THIS drag sprang open and is still responsible for folding back.
  const auto = new Set();

  // Deferred fold-back (the drag left the window), with a staleness guard so a timer
  // callback that still fires (a stub clock in tests, a clearTimeout that lost the
  // race) can tell it belongs to a drag that already came back.
  let pending = null;
  let restoreGen = 0;

  // The dwell currently being timed, and its own staleness guard.
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
    // Test/inspection seams.
    pendingRestore: () => [...auto],
    armedSection: () => armedId,

    /**
     * The pointer moved during a drag (dragenter/dragover), over `sectionId` ('' when
     * over no section). That section springs open after the dwell; moving off it — or
     * onto another one — cancels the pending expand immediately.
     * Idempotent: dragover fires continuously, so re-reporting the SAME section keeps
     * the running dwell instead of restarting it (it would otherwise never elapse).
     */
    pointerOver(kind, sectionId) {
      cancelPending();   // the drag is back inside the surface — cancel any fold-back
      const want = sectionForDragPoint(kind, sectionId, {
        collapsed: sectionId ? { [sectionId]: !!isCollapsed(sectionId) } : {},
        sections,
      });
      if (!want) { if (armedId) disarm(); return; }
      if (want === armedId) return;   // already dwelling here
      disarm();
      armedId = want;
      const gen = armGen;
      armTimer = timer(() => {
        if (gen !== armGen) return;   // the pointer moved on — stale dwell
        armTimer = null;
        armedId = '';
        if (!isCollapsed(want)) return;
        expand(want);
        auto.add(want);
        if (onOpen) onOpen(want);
      }, dwellMs);
    },

    // The user toggled a section by hand mid-drag — it's theirs now, hands off.
    manualToggle(id) { auto.delete(id); },

    // A drop landed IN this section: they're clearly using it, so it stays open.
    dropIn(id) { auto.delete(id); },

    // The drag ended (dragend, or a drop anywhere): fold back whatever is left.
    end() { restore(); },

    // The drag left the window: fold back, but only after RESTORE_DELAY_MS, so a drag
    // that merely crosses the page and returns doesn't flap the layout. Any pending
    // dwell dies immediately — the pointer is gone.
    scheduleEnd(delay = RESTORE_DELAY_MS) {
      disarm();
      if (!auto.size || pending !== null) return;
      const gen = restoreGen;
      pending = timer(() => {
        if (gen !== restoreGen) return;   // the drag returned — this timer is stale
        pending = null;
        restore();
      }, delay);
    },
  };
};
