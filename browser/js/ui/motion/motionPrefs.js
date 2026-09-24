// Two app-wide motion switches (Visuals modal / stencil.drawingAnimations, stencil.motionMode):
// `drawing` is the canvas stroke motion (core/strokeFx.js); `mode` is how the interface moves —
// 'particles' | 'water' | 'fire' (cloud.js styleFrame), 'slide' (each surface's own CSS
// entrance), 'none'. prefers-reduced-motion: reduce reads as 'none' whatever is stored.
// App-wide under its own localStorage key; mirrored onto <html data-motion> (animations/motionModes.css).

import { publish, EVENTS } from '../../eventBus/appBus.js';

export const MOTION_STORAGE_KEY = 'drawingApp_motion';
// Fired after every change, so an open dialog can restate its controls.
export const MOTION_EVENT = EVENTS.motionChanged;
const MOTION_ATTR = 'data-motion';
const BACKDROP_ATTR = 'data-modal-backdrop';

const MOTION_PARTICLES = 'particles';
// On by default: a window that dims what it covers is the shape both surfaces ship with.
export const DEFAULT_MODAL_BACKDROP = true;
const MOTION_WATER = 'water';
const MOTION_FIRE = 'fire';
const MOTION_SLIDE = 'slide';
const MOTION_NONE = 'none';
export const MOTION_MODES = Object.freeze([MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE, MOTION_SLIDE, MOTION_NONE]);
// The style (cloud.js PARTICLE_STYLES) each particle mode paints in.
export const PARTICLE_MODES = Object.freeze([MOTION_PARTICLES, MOTION_WATER, MOTION_FIRE]);
const PARTICLE_STYLE_OF = { [MOTION_PARTICLES]: 'dust', [MOTION_WATER]: 'water', [MOTION_FIRE]: 'fire' };
export const DEFAULT_MOTION_MODE = MOTION_PARTICLES;
export const DEFAULT_DRAWING_ANIMATIONS = true;

// One list for the desktop combo (dialogs/SettingsDialog.cpp) and the extension's options
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
  backdrop: DEFAULT_MODAL_BACKDROP,
});

const mergePatch = (base, patch = {}) => {
  const next = { ...base };
  if (patch.mode !== undefined) next.mode = normalizeMotionMode(patch.mode);
  if (patch.drawing !== undefined) next.drawing = !!patch.drawing;
  if (patch.backdrop !== undefined) next.backdrop = !!patch.backdrop;
  return next;
};

// Bad/missing data degrades to defaults.
const readMotionPrefs = () => {
  const out = defaultMotionPrefs();
  try {
    const raw = ls()?.getItem(MOTION_STORAGE_KEY);
    if (!raw) return out;
    const saved = JSON.parse(raw);
    if (saved && typeof saved === 'object') return mergePatch(out, saved);
  } catch { /* storage blocked or the blob is junk — the defaults stand */ }
  return out;
};

let prefs = readMotionPrefs();
// A session layer over the stored prefs (the webcore skin): read by everything, written nowhere.
let override = null;
const effective = () => (override ? { ...prefs, ...override } : prefs);

export const motionPrefs = () => ({ ...effective() });
export const motionMode = () => effective().mode;
export const drawingAnimations = () => effective().drawing;
// Whether an open window blurs and darkens what is behind it. Desktop twin:
// support/motionPrefs.hpp modalBackdrop().
export const modalBackdrop = () => effective().backdrop;
export const motionOverridden = () => override !== null;
export const storedMotionMode = () => prefs.mode;
export const showMotionStyle = () => PARTICLE_STYLE_OF[effective().mode] || 'dust';
export const showDustAllowed = () => PARTICLE_MODES.includes(effective().mode);   // slide/none fly none

// Asked at call time so a mid-session change is followed.
export const prefersReducedMotion = () =>
  typeof matchMedia !== 'undefined' && matchMedia('(prefers-reduced-motion: reduce)').matches;

// The one gate every animation checks: nothing may move.
export const motionReduced = () => effective().mode === MOTION_NONE || prefersReducedMotion();

// False in 'slide' leaves the surface's own CSS entrance in charge.
export const dustEnabled = () => PARTICLE_MODES.includes(effective().mode) && !prefersReducedMotion();

// 'dust' | 'water' | 'fire', or null when none fly.
export const particleStyle = () => (dustEnabled() ? PARTICLE_STYLE_OF[effective().mode] : null);

// The canvas stroke motion, which the user can turn off on its own.
export const drawMotionEnabled = () => effective().drawing && !motionReduced();

// Mirrors what prePaintTheme.js writes before first paint.
export const applyMotionAttr = (root = typeof document !== 'undefined' ? document.documentElement : null) => {
  const now = effective();
  root?.setAttribute?.(MOTION_ATTR, now.mode);
  // A plain attribute, so the blur is a CSS rule rather than a per-overlay inline style.
  root?.toggleAttribute?.(BACKDROP_ATTR, now.backdrop);
};

const announce = () => {
  applyMotionAttr();
  publish(MOTION_EVENT, motionPrefs());
  return motionPrefs();
};

// Persist, restamp <html>, announce. An unknown mode falls back to the default (the console
// facade throws before it gets here). A user's choice ends any session override.
export const setMotionPrefs = (patch = {}) => {
  override = null;
  prefs = mergePatch(prefs, patch);
  try { ls()?.setItem(MOTION_STORAGE_KEY, JSON.stringify(prefs)); } catch { /* storage blocked — this session still honours it */ }
  return announce();
};

// Lay a session-only patch over the stored prefs (null lifts it); the store is never touched.
export const setMotionOverride = (patch) => {
  override = patch ? mergePatch(effective(), patch) : null;
  return announce();
};

// Tests only: forget what was loaded and read the store again.
export const reloadMotionPrefs = () => { override = null; prefs = readMotionPrefs(); return motionPrefs(); };
