// ── Collapsible filter sections (the panel's accordion) ─────────────────────
// Extracted from popup.js. Click (or Enter/Space) a .section-head to hide/show its
// section's body; clicks on controls inside the header act on the control instead. One
// toggle per section, registered by id, so programmatic opens (the drag spring, the
// editor-mode picker) take the SAME path a header click does — aria-expanded, the
// chevron, and the per-section hooks (assistant lazy boot, search fold) included.

/**
 * Wire every .section-head in `doc`.
 *
 * @param {object} deps
 * @param {Document} [deps.doc]
 * @param {(section: Element) => void} [deps.beforeToggle] - Runs before the class flips
 *   (the peek returns a borrowed body home).
 * @param {(id: string) => void} [deps.onUserToggle] - A toggle the USER performed hands
 *   the section back to them (cancels a fold-back a live drag had queued).
 */
export const createCollapsibleSections = ({ doc = document, beforeToggle = () => {}, onUserToggle = () => {} } = {}) => {
  const toggles = new Map();
  // Per-section toggle side-effects, keyed by id and registered where their owner
  // lives — the toggler itself stays section-agnostic.
  const hooks = new Map();

  for (const head of doc.querySelectorAll('.section-head')) {
    const section = head.closest('.fsection');
    const toggle = () => {
      beforeToggle(section);
      const collapsed = section.classList.toggle('collapsed');
      head.setAttribute('aria-expanded', String(!collapsed));
      hooks.get(section.id)?.(collapsed);
    };
    if (section && section.id) toggles.set(section.id, toggle);
    const userToggle = () => { toggle(); if (section && section.id) onUserToggle(section.id); };
    head.addEventListener('click', (e) => { if (!e.target.closest('button, input, select, a, label')) userToggle(); });
    head.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); userToggle(); } });
  }

  // A HIDDEN section (the assistant with the provider off) is absent, not collapsed —
  // so a drag spring never tries to unfold it.
  const isCollapsed = (id) => {
    const el = doc.getElementById(id);
    return !!el && !el.hidden && el.classList.contains('collapsed');
  };
  const setCollapsed = (id, want) => {
    if (toggles.has(id) && isCollapsed(id) !== want) toggles.get(id)();
  };

  return {
    isCollapsed,
    setCollapsed,
    /** Register the section's toggle side-effect (assistant boot, search fold). */
    setHook: (id, fn) => hooks.set(id, fn),
    /** Run a section's hook directly (a peek counts as the assistant's first expand). */
    runHook: (id, collapsed) => hooks.get(id)?.(collapsed),
  };
};
