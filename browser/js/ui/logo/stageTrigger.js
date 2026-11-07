// The one way into a logo show: the header mark's hold, a typed word and the console facade all
// arrive here. Desktop twin: the LogoStage hooks in app/toolbar/ToolbarBuilderHeader.cpp.
import { notify } from '../../utils.js';
import { webcoreSkin } from '../webcore/toggle.js';
import { aquaSkin } from '../aqua/toggle.js';
import { closeOpenModal } from '../modal/registry.js';
import { PRESS_SLOP_PX } from '../tip/popover.js';
import { storedMotionMode } from '../motion/motionPrefs.js';
import { originOf } from '../motion/surface/themeSwapPlay.js';
import { waitForImage } from '../../core/image/loadFlow.js';
import { STAGE, HOLD_MS, TOAST_TEXT, SKINS, effectOf, resolveShow, heartLine } from './stageRules.js';
import { openLogoStage, logoStageAllowed, closeLogoStage, currentLogoStage, markOrigin } from './stage.js';
import { trackPointer } from './pointer.js';
import { createBlankImage } from '../../core/image/blankImage.js';

// The pink show is an edit, not a stage: a pink page if there is none, then the pink tint and the
// heart as one undoable step (the heart's step carries the tint).
export const pinkVibe = async (app) => {
  if (!app.image) {
    const previous = app.image;
    await createBlankImage(app, { color: STAGE.pink.blank });
    await waitForImage(app, { previous });
  }
  if (!app.image) return false;
  app.settings.setFilter({ filter: 'custom', filterColor: STAGE.pink.tint }, { history: false });
  const { width, height } = app.image;
  const drawn = app.export.installLayout(
    { imageWidth: width, imageHeight: height, lines: [heartLine(width, height)] },
    { mode: 'combine', history: true });
  app.zoomPan?.fitToWindow?.();   // the heart is the show — a page taller than the view hides it
  return drawn;
};

// The session skins by effect, one at a time: turning one on first turns the other off through its own path.
const SKIN = Object.freeze({ webcore: webcoreSkin, aqua: aquaSkin });
const toggleSkin = async (effect, app, { scene = true, toast = true } = {}) => {
  const skin = SKIN[effect];
  if (!skin.active()) {
    for (const other of Object.values(SKIN)) if (other !== skin && other.active()) await other.toggle(app, undefined, { scene: false });
  }
  const on = await skin.toggle(app, undefined, { scene });
  if (toast) notify(on ? TOAST_TEXT : skin.offToast, 'ok', { shine: true });
  return on;
};

// Pink counts as on while its tint is the page's filter; webcore while the skin is stamped.
export const showActive = (name, app) => {
  const effect = effectOf(name);
  if (SKINS.has(effect)) return SKIN[effect].active();
  if (effect === 'pink') return !!app?.image && app.imageFilter === 'custom'
    && String(app.filterColor).toLowerCase() === STAGE.pink.tint;
  return !!effect && currentLogoStage()?.name === name;
};

// An asked-for show wins: the stage up, every open window and fullscreen give way to it.
export const clearWay = (app, doc = globalThis.document) => {
  closeLogoStage();
  closeOpenModal();
  for (const overlay of doc?.querySelectorAll?.('.app-modal-overlay.modal-open') ?? []) {
    overlay.querySelector?.('.app-modal-close')?.click?.();
  }
  if (doc?.body?.classList?.contains?.('fullscreen-mode')) app?.toggleFullscreen?.();
};

// Every activation. The hold is gated to the bare window; `replace` (a call, a typed word) clears
// the way instead, and never restarts the show already up. `scene: false` = the skin alone.
export const activateShow = (name, app, origin = null, { replace = false, scene = true } = {}) => {
  const effect = effectOf(name);
  if (!effect) return false;
  if (SKINS.has(effect)) {
    if (!replace && !logoStageAllowed()) return false;
    return toggleSkin(effect, app, { scene });
  }
  if (replace) {
    if (showActive(name, app)) return false;
    clearWay(app);
  }
  if (!logoStageAllowed()) return false;
  if (effect === 'pink') {
    const done = pinkVibe(app);
    notify(TOAST_TEXT, 'ok', { shine: true });
    return done;
  }
  if (!openLogoStage(name, { app, origin })) return false;
  notify(TOAST_TEXT, 'ok', { shine: true });
  return true;
};

// The `<name>Mode` switch: assigning the state it is in does nothing.
export const setShow = (name, app, on) => {
  if (!!on === showActive(name, app)) return;
  if (on) activateShow(name, app, markOrigin(), { replace: true, scene: false });
  else if (SKINS.has(effectOf(name))) toggleSkin(effectOf(name), app, { toast: false });
  else if (effectOf(name) === 'pink') app.settings.setImageFilter('none');
  else closeLogoStage();
};

// The show the header mark's hold opens right now, given the accent and the motion mode.
const faceOf = (doc = globalThis.document) =>
  (doc?.documentElement?.getAttribute?.('data-theme') === 'dark' ? 'dark' : 'light');
export const heldShow = (app) => resolveShow(app?.accent, app?.customAccent, storedMotionMode(), faceOf());

// A press held on the mark for holdMs. The release must not reach the logo's own click, which
// would cycle the accent 220ms later. The handle drops a waiting hold, and says if this press fired.
// `onHold` replaces the show the accent would open (the CLI stage's logo leaves the mode).
export const wireLogoHold = (logo, app, { holdMs = HOLD_MS, onHold = null } = {}) => {
  if (!logo || !app) return;
  trackPointer(logo.ownerDocument);
  const wrap = logo.closest?.('.app-logo-wrap') || logo;
  let timer = null, from = null, fired = false;
  const cancel = () => { if (timer) clearTimeout(timer); timer = null; from = null; };
  const swallowClick = (e) => { e.preventDefault?.(); e.stopImmediatePropagation?.(); };
  wrap.addEventListener('pointerdown', (e) => {
    if (e.button || e.altKey || e.ctrlKey || e.metaKey || e.shiftKey) return;
    fired = false;
    from = { x: e.clientX ?? 0, y: e.clientY ?? 0 };
    timer = setTimeout(() => {
      timer = null;
      const name = onHold ? null : heldShow(app);
      if (!name && !onHold) return;
      fired = true;
      wrap.addEventListener('click', swallowClick, { capture: true, once: true });
      if (onHold) onHold(); else activateShow(name, app, originOf(wrap));
    }, holdMs);
  });
  wrap.addEventListener('pointermove', (e) => {
    if (!from) return;
    if (Math.hypot((e.clientX ?? 0) - from.x, (e.clientY ?? 0) - from.y) > PRESS_SLOP_PX) cancel();
  });
  for (const type of ['pointerup', 'pointerleave', 'pointercancel']) {
    wrap.addEventListener(type, () => {
      cancel();
      // A hold that opened a show leaves the swallow armed for exactly one click.
      if (!fired) wrap.removeEventListener('click', swallowClick, { capture: true });
    });
  }
  return { cancel, get fired() { return fired; } };
};
