// Browser js/ui/prefs.js twin. window.StencilMotion (lib/accent.js) is asked live so
// an options-page change reaches an open popup; without it only the OS preference speaks.
export const prefersReducedMotion = () => {
  try { return matchMedia('(prefers-reduced-motion: reduce)').matches; } catch { return false; }
};
const motionPref = () => globalThis.StencilMotion || null;
export const motionMode = () => motionPref()?.get?.() ?? 'particles';
// The one gate every animation checks: nothing may move.
export const motionReduced = () => (motionPref() ? motionPref().reduced() : prefersReducedMotion());
// False in 'slide' leaves the surface's own CSS entrance in charge.
export const dustEnabled = () => (motionPref() ? motionPref().particles() : !prefersReducedMotion());
// null when no particles fly.
export const particleStyle = () => (motionPref() ? motionPref().style() : (prefersReducedMotion() ? null : 'dust'));
