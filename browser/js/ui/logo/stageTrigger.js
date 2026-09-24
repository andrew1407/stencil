// The one way into a logo show: the header mark's hold, a typed word and the console facade all
// arrive here. Desktop twin: the LogoStage hooks in app/MainWindowToolbarHeader.cpp.
import { notify } from '../../utils.js';
import { toggleWebcore, webcoreActive } from '../webcore/toggle.js';
import { closeOpenModal } from '../modal/registry.js';
import { OFF_TOAST } from '../webcore/rules.js';
import { PRESS_SLOP_PX } from '../tip/popover.js';
import { storedMotionMode } from '../motion/motionPrefs.js';
import { originOf } from '../motion.js';
import { waitForImage } from '../../core/image/loadFlow.js';
import { STAGE, HOLD_MS, TOAST_TEXT, effectOf, resolveShow, heartLine } from './stageRules.js';
import { openLogoStage, logoStageAllowed, closeLogoStage, currentLogoStage, markOrigin } from './stage.js';
import { trackPointer } from './pointer.js';

// The pink show is an edit, not a stage: a pink page if there is none, the pink tint, and the
// heart as one undoable step.
export const pinkVibe = async (app) => {
  if (!app.image) {
    const previous = app.image;
    await app.createBlankImage({ color: STAGE.pink.blank });
    await waitForImage(app, { previous });
  }
  if (!app.image) return false;
  app.settings.setImageFilter('custom');
  app.settings.setFilterColor(STAGE.pink.tint);
  const { width, height } = app.image;
  const drawn = app.export.installLayout(
    { imageWidth: width, imageHeight: height, lines: [heartLine(width, height)] },
    { mode: 'combine', history: true });
  app.zoomPan?.fitToWindow?.();   // the heart is the show — a page taller than the view hides it
  return drawn;
};

// Pink counts as on while its tint is the page's filter; webcore while the skin is stamped.
export const showActive = (name, app) => {
  const effect = effectOf(name);
  if (effect === 'webcore') return webcoreActive();
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
  if (effect === 'webcore') {
    if (!replace && !logoStageAllowed()) return false;
    return toggleWebcore(app, undefined, { scene }).then((on) => {
      notify(on ? TOAST_TEXT : OFF_TOAST, 'ok', { shine: true });
      return on;
    });
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
  else if (effectOf(name) === 'webcore') toggleWebcore(app);
  else if (effectOf(name) === 'pink') app.settings.setImageFilter('none');
  else closeLogoStage();
};

// The show the header mark's hold opens right now, given the accent and the motion mode.
export const heldShow = (app) => resolveShow(app?.accent, app?.customAccent, storedMotionMode());

// A press held on the mark for holdMs. The release must not reach the logo's own click, which
// would cycle the accent 220ms later.
export const wireLogoHold = (logo, app, { holdMs = HOLD_MS } = {}) => {
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
      const name = heldShow(app);
      if (!name) return;
      fired = true;
      wrap.addEventListener('click', swallowClick, { capture: true, once: true });
      activateShow(name, app, originOf(wrap));
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
};
