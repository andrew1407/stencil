// ── Motion preferences ──────────────────────────────────────────────────────
// Two app-wide switches over everything that moves, set in the Visuals modal and
// through the console facade (stencil.drawingAnimations / stencil.motionMode):
//
//   • drawing — the canvas stroke motion: a vertex flying to where it was put, its
//     landing pop, ripple and spark (core/strokeFx.js). On by default.
//   • mode    — how the INTERFACE moves:
//       'particles' (default) every surface forms out of dust and comes apart into it
//                   — windows, menus, marks, chat entries, the canvas, the voice mic;
//       'water'     the same particles as drops: sagging, swaying, shimmering;
//       'fire'      …as embers: lifting, wavering, flickering (dustCloud.js styleFrame);
//       'slide'     no particles anywhere: each surface plays its own plain entrance
//                   instead (the grow-from-the-icon / rise / fade CSS it already has,
//                   which is exactly what the dust normally stands in for);
//       'none'      nothing moves — the same end state prefers-reduced-motion gives.
//
// The OS preference still wins on its own: prefers-reduced-motion: reduce reads as
// 'none' whatever is stored, so an accessibility setting is never overridden by ours.
//
// App-wide, not per project (the same flavour as the theme/accent keys), so the value
// lives under its own localStorage key rather than in the project layout blob. Every
// read is guarded: private/disabled storage throws, and the defaults are then correct.
// The mode is also mirrored onto <html data-motion> for the CSS half (animations.css).

export const MOTION_STORAGE_KEY = 'drawingApp_motion';
// Fired on <window> after every change, so an open dialog can restate its controls.
export const MOTION_EVENT = 'stencil:motion-changed';
export const MOTION_ATTR = 'data-motion';

export const MOTION_PARTICLES = 'particles';
export const MOTION_WATER = 'water';
export const MOTION_FIRE = 'fire';
export const MOTION_SLIDE = 'slide';
export const MOTION_NONE = 'none';
export const MOTION_MODES = [MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE, MOTION_SLIDE, MOTION_NONE];
// The three modes that fly particles, and the style (dustCloud.js PARTICLE_STYLES) each
// paints them in.
export const PARTICLE_MODES = [MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE];
export const PARTICLE_STYLE_OF = { [MOTION_PARTICLES]: 'dust', [MOTION_WATER]: 'water', [MOTION_FIRE]: 'fire' };
export const DEFAULT_MOTION_MODE = MOTION_PARTICLES;
export const DEFAULT_DRAWING_ANIMATIONS = true;

// Human labels for the one dropdown that offers them (visualsModal.js) — kept here so
// the desktop's combo (dialogs/settingsDialog.cpp) and the extension's options page
// (src/lib/accent.js StencilMotion) have one list to mirror.
export const MOTION_MODE_LABELS = [
  [MOTION_PARTICLES, 'Dust'],
  [MOTION_WATER, 'Water'],
  [MOTION_FIRE, 'Fire'],
  [MOTION_SLIDE, 'Sliding'],
  [MOTION_NONE, 'None'],
];

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

export const normalizeMotionMode = (v) => {
  const s = String(v ?? '').trim().toLowerCase();
  return MOTION_MODES.includes(s) ? s : DEFAULT_MOTION_MODE;
};

export const defaultMotionPrefs = () => ({
  mode: DEFAULT_MOTION_MODE, drawing: DEFAULT_DRAWING_ANIMATIONS,
});

// Saved overrides merged over the defaults. Bad/missing data degrades to defaults.
export const readMotionPrefs = () => {
  const out = defaultMotionPrefs();
  try {
    const raw = ls()?.getItem(MOTION_STORAGE_KEY);
    if (!raw) return out;
    const saved = JSON.parse(raw);
    if (saved && typeof saved === 'object') {
      out.mode = normalizeMotionMode(saved.mode);
      if (saved.drawing !== undefined) out.drawing = !!saved.drawing;
    }
  } catch { /* storage blocked or the blob is junk — the defaults stand */ }
  return out;
};

let prefs = readMotionPrefs();

export const motionPrefs = () => ({ ...prefs });
export const motionMode = () => prefs.mode;
export const drawingAnimations = () => prefs.drawing;

// The OS half, asked at call time so a mid-session change is followed.
export const prefersReducedMotion = () =>
  typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;

// The one gate every animation checks: nothing may move.
export const motionReduced = () => prefs.mode === MOTION_NONE || prefersReducedMotion();

// …and the one every PARTICLE flight checks on top of it. False in 'slide' leaves the
// surface's own CSS entrance in charge — the flight the particles normally replace.
export const dustEnabled = () => PARTICLE_MODES.includes(prefs.mode) && !prefersReducedMotion();

// Which style the particles wear — 'dust' | 'water' | 'fire' — or null when none fly.
// Read by every cloud builder alongside dustEnabled().
export const particleStyle = () => (dustEnabled() ? PARTICLE_STYLE_OF[prefs.mode] : null);

// The canvas stroke motion, which the user can turn off on its own.
export const drawMotionEnabled = () => prefs.drawing && !motionReduced();

// Stamp the mode on <html> for the CSS half. Mirrors what prePaintTheme.js writes
// before first paint, so a later change lands on the same attribute.
export const applyMotionAttr = (root = typeof document !== 'undefined' ? document.documentElement : null) => {
  root?.setAttribute?.(MOTION_ATTR, prefs.mode);
};

// Write one or both preferences: persist, restamp <html>, and announce it. Returns
// the new preferences. An unknown mode falls back to the default (the console facade
// throws before it gets here — see stencilApi.js).
export const setMotionPrefs = (patch = {}) => {
  const next = { ...prefs };
  if (patch.mode !== undefined) next.mode = normalizeMotionMode(patch.mode);
  if (patch.drawing !== undefined) next.drawing = !!patch.drawing;
  prefs = next;
  try { ls()?.setItem(MOTION_STORAGE_KEY, JSON.stringify(prefs)); } catch { /* storage blocked — this session still honours it */ }
  applyMotionAttr();
  try { window.dispatchEvent(new CustomEvent(MOTION_EVENT, { detail: motionPrefs() })); } catch { /* no DOM — best-effort UI nudge */ }
  return motionPrefs();
};

// Tests only: forget what was loaded and read the store again.
export const reloadMotionPrefs = () => { prefs = readMotionPrefs(); return motionPrefs(); };
