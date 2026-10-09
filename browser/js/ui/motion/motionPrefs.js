// Two app-wide motion switches (Visuals modal / stencil.drawingAnimations, stencil.motionMode):
// `drawing` is the canvas stroke motion (core/line/strokeFx.js); `mode` is how the interface moves —
// 'particles' | 'water' | 'fire' (cloud.js styleFrame), 'slide' (each surface's own CSS
// entrance), 'none'. prefers-reduced-motion: reduce reads as 'none' whatever is stored.
// App-wide under its own localStorage key; mirrored onto <html data-motion> (animations/motionModes.css).

import { publish, EVENTS } from '../../eventBus/appBus.js';
import MOTION from '../../../../common/config/motion.json' with { type: 'json' };

export const MOTION_STORAGE_KEY = 'drawingApp_motion';
// Fired after every change, so an open dialog can restate its controls.
export const MOTION_EVENT = EVENTS.motionChanged;
const MOTION_ATTR = 'data-motion';
const BACKDROP_ATTR = 'data-modal-backdrop';
const MULTI_WINDOW_ATTR = 'data-multi-window';

const MOTION_PARTICLES = 'particles';
// Off by default: one window at a time, dimming and blurring what it covers; on, windows stay open
// side by side over a sharp page.
export const DEFAULT_MULTI_WINDOW = false;
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
// The Visuals "Animation speed": every interface flight runs DEFAULT / speed times its tuned length.
export const MOTION_SPEED = Object.freeze({ ...MOTION.speed });
export const DEFAULT_MOTION_SPEED = MOTION_SPEED.default;

// One list for the desktop combo (dialogs/settings/SettingsDialog.cpp) and the extension's options
// page (src/lib/accent/accent.js StencilMotion) to mirror.
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

// A number outside min–max is clamped; anything else reads as the default. Two decimals, the step.
export const clampMotionSpeed = (v) => {
  const n = typeof v === 'string' && v.trim() !== '' ? Number(v) : v;
  if (typeof n !== 'number' || !Number.isFinite(n)) return DEFAULT_MOTION_SPEED;
  return Math.round(Math.min(MOTION_SPEED.max, Math.max(MOTION_SPEED.min, n)) * 100) / 100;
};

const defaultMotionPrefs = () => ({
  mode: DEFAULT_MOTION_MODE, drawing: DEFAULT_DRAWING_ANIMATIONS,
  multiWindow: DEFAULT_MULTI_WINDOW, speed: DEFAULT_MOTION_SPEED,
});

const mergePatch = (base, patch = {}) => {
  const next = { ...base };
  if (patch.mode !== undefined) next.mode = normalizeMotionMode(patch.mode);
  if (patch.drawing !== undefined) next.drawing = !!patch.drawing;
  if (patch.multiWindow !== undefined) next.multiWindow = !!patch.multiWindow;
  if (patch.speed !== undefined) next.speed = clampMotionSpeed(patch.speed);
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
// Many windows at once, none dimming or blocking the app. Desktop twin: support/motionPrefs.hpp multiWindow().
export const multiWindow = () => effective().multiWindow;
export const motionOverridden = () => override !== null;
export const motionSpeed = () => effective().speed;
// The duration multiplier: exactly 1 at the default, so nothing tuned moves until the user asks.
export const motionScale = () => DEFAULT_MOTION_SPEED / effective().speed;
export const motionMs = (ms) => {
  const k = motionScale();
  return k === 1 ? ms : Math.round(ms * k);
};
// A clock for one rAF flight: its milliseconds pass at the speed in force when it started, so a
// change mid-flight neither freezes the motes nor ends them at once.
export const motionClock = (real = () => globalThis.performance?.now?.() ?? Date.now()) => {
  const k = motionScale();
  const t0 = real();
  return () => t0 + (real() - t0) / k;
};
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
  root?.toggleAttribute?.(BACKDROP_ATTR, !now.multiWindow);
  root?.toggleAttribute?.(MULTI_WINDOW_ATTR, now.multiWindow);
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
