// The panel's accordion: one toggle per section, registered by id, so programmatic opens
// take the SAME path a header click does — aria-expanded, chevron and hooks included.
// `beforeToggle` runs before the class flips; `onUserToggle` fires only for the USER's
// own toggles (cancels a fold-back a live drag had queued).
export const createCollapsibleSections = ({ doc = document, beforeToggle = () => {}, onUserToggle = () => {} } = {}) => {
  const toggles = new Map();
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

  // A HIDDEN section is absent, not collapsed — a drag spring never tries to unfold it.
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
    setHook: (id, fn) => hooks.set(id, fn),
    // A peek counts as the assistant's first expand.
    runHook: (id, collapsed) => hooks.get(id)?.(collapsed),
  };
};
