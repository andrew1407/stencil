// Two app-wide motion switches (Visuals modal / stencil.drawingAnimations, stencil.motionMode):
// `drawing` is the canvas stroke motion (core/strokeFx.js); `mode` is how the interface moves —
// 'particles' | 'water' | 'fire' (dustCloud.js styleFrame), 'slide' (each surface's own CSS
// entrance), 'none'. prefers-reduced-motion: reduce reads as 'none' whatever is stored.
// App-wide under its own localStorage key; mirrored onto <html data-motion> (animations/motionModes.css).

import { publish, EVENTS } from '../bus/appBus.js';

export const MOTION_STORAGE_KEY = 'drawingApp_motion';
// Fired after every change, so an open dialog can restate its controls.
export const MOTION_EVENT = EVENTS.motionChanged;
const MOTION_ATTR = 'data-motion';

const MOTION_PARTICLES = 'particles';
const MOTION_WATER = 'water';
const MOTION_FIRE = 'fire';
const MOTION_SLIDE = 'slide';
const MOTION_NONE = 'none';
export const MOTION_MODES = Object.freeze([MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE, MOTION_SLIDE, MOTION_NONE]);
// The style (dustCloud.js PARTICLE_STYLES) each particle mode paints in.
export const PARTICLE_MODES = Object.freeze([MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE]);
const PARTICLE_STYLE_OF = { [MOTION_PARTICLES]: 'dust', [MOTION_WATER]: 'water', [MOTION_FIRE]: 'fire' };
export const DEFAULT_MOTION_MODE = MOTION_PARTICLES;
export const DEFAULT_DRAWING_ANIMATIONS = true;

// One list for the desktop combo (dialogs/settingsDialog.cpp) and the extension's options
// page (src/lib/accent.js StencilMotion) to mirror.
export const MOTION_MODE_LABELS = Object.freeze([
  [MOTION_PARTICLES, 'Dust'],
  [MOTION_WATER, 'Water'],
  [MOTION_FIRE, 'Fire'],
  [MOTION_SLIDE, 'Sliding'],
  [MOTION_NONE, 'None'],
]);

const ls = () => (typeof localStorage !== 'undefined' ? localStorage : null);

export const normalizeMotionMode = (v) => {
  const s = String(v ?? '').trim().toLowerCase();
  return MOTION_MODES.includes(s) ? s : DEFAULT_MOTION_MODE;
};

const defaultMotionPrefs = () => ({
  mode: DEFAULT_MOTION_MODE, drawing: DEFAULT_DRAWING_ANIMATIONS,
});

// Bad/missing data degrades to defaults.
const readMotionPrefs = () => {
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

// Asked at call time so a mid-session change is followed.
export const prefersReducedMotion = () =>
  typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;

// The one gate every animation checks: nothing may move.
export const motionReduced = () => prefs.mode === MOTION_NONE || prefersReducedMotion();

// False in 'slide' leaves the surface's own CSS entrance in charge.
export const dustEnabled = () => PARTICLE_MODES.includes(prefs.mode) && !prefersReducedMotion();

// 'dust' | 'water' | 'fire', or null when none fly.
export const particleStyle = () => (dustEnabled() ? PARTICLE_STYLE_OF[prefs.mode] : null);

// The canvas stroke motion, which the user can turn off on its own.
export const drawMotionEnabled = () => prefs.drawing && !motionReduced();

// Mirrors what prePaintTheme.js writes before first paint.
export const applyMotionAttr = (root = typeof document !== 'undefined' ? document.documentElement : null) => {
  root?.setAttribute?.(MOTION_ATTR, prefs.mode);
};

// Persist, restamp <html>, announce. An unknown mode falls back to the default (the console
// facade throws before it gets here).
export const setMotionPrefs = (patch = {}) => {
  const next = { ...prefs };
  if (patch.mode !== undefined) next.mode = normalizeMotionMode(patch.mode);
  if (patch.drawing !== undefined) next.drawing = !!patch.drawing;
  prefs = next;
  try { ls()?.setItem(MOTION_STORAGE_KEY, JSON.stringify(prefs)); } catch { /* storage blocked — this session still honours it */ }
  applyMotionAttr();
  publish(MOTION_EVENT, motionPrefs());
  return motionPrefs();
};

// Tests only: forget what was loaded and read the store again.
export const reloadMotionPrefs = () => { prefs = readMotionPrefs(); return motionPrefs(); };
