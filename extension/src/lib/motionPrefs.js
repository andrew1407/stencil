// ── The motion mode (browser js/ui/motionPrefs.js twin) ─────────────────────
// The user's switch, kept by the pre-paint classic script lib/accent.js
// (window.StencilMotion), asked live so an options-page change reaches an open popup
// without a reload. Without the script only the OS preference speaks.
// Does the OS want motion at all? Never throws (no matchMedia outside a browser).
export const prefersReducedMotion = () => {
  try { return matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { return false; }
};
const motionPref = () => globalThis.StencilMotion || null;
export const motionMode = () => motionPref()?.get?.() ?? 'particles';
// The one gate every animation checks: nothing may move.
export const motionReduced = () => (motionPref() ? motionPref().reduced() : prefersReducedMotion());
// …and the one every PARTICLE flight checks on top of it. False in 'slide' leaves the
// surface's own CSS entrance in charge — the flight the particles normally replace.
export const dustEnabled = () => (motionPref() ? motionPref().particles() : !prefersReducedMotion());
// Which style the particles wear — 'dust' | 'water' | 'fire' — or null when none fly.
export const particleStyle = () => (motionPref() ? motionPref().style() : (prefersReducedMotion() ? null : 'dust'));
