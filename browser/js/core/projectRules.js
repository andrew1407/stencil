// ── Project rules over the wasm core ────────────────────────────
// The browser's ProjectsStore stays its own class — it is localStorage-backed, while
// core/state/projectsStore.cpp is an in-memory registry — but the refresh presets and
// the expiry arithmetic are one shared rule that must not drift, and they read nothing
// but scalars. So they cross as plain functions rather than a handle, with epoch
// milliseconds as doubles (exact well past 2^53, no BigInt).

export const projectRuleExports = [
  'stencil_projects_periodMs', 'stencil_projects_addPeriod', 'stencil_projects_shouldPersist',
  'stencil_projects_isExpired', 'stencil_projects_isExpiringSoon',
];

export const buildProjectRules = (mod) => {
  const cPeriodMs = mod.cwrap('stencil_projects_periodMs', 'number', ['string']);
  const cAddPeriod = mod.cwrap('stencil_projects_addPeriod', 'number', ['number', 'string']);
  const cShouldPersist = mod.cwrap('stencil_projects_shouldPersist', 'number', ['number', 'number']);
  const cIsExpired = mod.cwrap('stencil_projects_isExpired', 'number', ['number', 'number']);
  const cIsExpiringSoon = mod.cwrap('stencil_projects_isExpiringSoon', 'number', ['number', 'number']);
  // A meta with no expiresAt (absent, null or 0) is "keep forever" on both sides.
  const expiryOf = (meta) => (meta && meta.expiresAt) || 0;

  return {
    projectPeriodMs: (period) => cPeriodMs(period ?? ''),
    projectAddPeriod: (from, period) => cAddPeriod(from, period ?? ''),
    projectShouldPersist: (activeId, temporary) => cShouldPersist(activeId != null ? 1 : 0, temporary ? 1 : 0) === 1,
    projectIsExpired: (meta, now) => cIsExpired(expiryOf(meta), now) === 1,
    projectIsExpiringSoon: (meta, now) => cIsExpiringSoon(expiryOf(meta), now) === 1,
  };
};
