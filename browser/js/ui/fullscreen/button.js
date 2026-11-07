// The fullscreen toggle's gestures: click → the OS fullscreen ('screen'), double-click → the
// in-window layer ('window'), right-click or a held press → a menu of both (the held one press-drag-release). Desktop twin:
// desktop/src/app/view/FullscreenButton.cpp. Timers are injected for DOM-free tests.
import { icon } from '../icons.js';
import { keysHtml } from '../tip/keys.js';
import { hotkeys } from '../../core/settings/hotkeys.js';
import { formatCombo, composeControlTitle } from '../../utils.js';
import { showMenu, hideMenu, DRAG_PICK_ROW_CLASS } from '../control/dropdownMenu.js';
import { DOUBLE_CLICK_MS, LONG_PRESS_MS, PRESS_SLOP_PX } from '../tip/popover.js';

export const FS_KINDS = Object.freeze(['screen', 'window']);
const ROWS = Object.freeze({
  screen: { label: 'Full Screen', exit: 'Exit Full Screen', icon: 'monitor', hk: 'fullscreen' },
  window: { label: 'Fullscreen in Window', exit: 'Exit Fullscreen in Window', icon: 'maximize', hk: 'fullscreenWindow' },
});

// One row of the button's menu and of the canvas menu's Fullscreen flyout; `mode` is the live
// kind (null = off), and the row of the live kind is the way out.
export const fsRowFace = (kind, mode) => {
  const r = ROWS[kind];
  return mode === kind ? { label: r.exit, icon: 'minimize', hk: r.hk } : { label: r.label, icon: r.icon, hk: r.hk };
};

// Clicks are counted here rather than read off `dblclick`: the fullscreen strip's copy of the
// button relays plain clicks only (clones.js), and two of them must still read as a double.
export const createFsButtonGesture = ({
  single, double, openMenu, delay = DOUBLE_CLICK_MS, holdMs = LONG_PRESS_MS, slop = PRESS_SLOP_PX,
  setTimer = setTimeout, clearTimer = clearTimeout,
} = {}) => {
  let clickTimer = null, holdTimer = null, press = null, shouldSwallowClick = false;
  const cancelClick = () => { if (clickTimer !== null) { clearTimer(clickTimer); clickTimer = null; } };
  const cancelHold = () => { if (holdTimer !== null) { clearTimer(holdTimer); holdTimer = null; } };
  return {
    click() {
      if (shouldSwallowClick) { shouldSwallowClick = false; return; }
      if (clickTimer !== null) { cancelClick(); double(); return; }
      clickTimer = setTimer(() => { clickTimer = null; single(); }, delay);
    },
    contextmenu() { cancelClick(); cancelHold(); shouldSwallowClick = true; openMenu(false); },
    pressStart({ x = 0, y = 0, button = 0 } = {}) {
      shouldSwallowClick = false;
      cancelHold();
      if (button !== 0) return;
      press = { x, y };
      holdTimer = setTimer(() => { holdTimer = null; shouldSwallowClick = true; cancelClick(); openMenu(true); }, holdMs);
    },
    pressMove({ x = 0, y = 0 } = {}) {
      if (press && (Math.abs(x - press.x) > slop || Math.abs(y - press.y) > slop)) cancelHold();
    },
    pressEnd() { cancelHold(); press = null; },
    dispose() { cancelClick(); cancelHold(); },
  };
};

const comboOf = (id) => {
  const c = hotkeys.get(id);
  return c ? formatCombo(c, hotkeys.isMac) : '';
};

// The tooltip names all three gestures; the heading's keycap is the live kind's (or Full Screen's).
export const fsButtonTitle = (mode, combo = comboOf) => {
  const win = combo('fullscreenWindow');
  const dbl = mode ? '' : `\nDouble-click — in the window${win ? `, ${win}` : ''}`;
  return `${mode ? 'Exit fullscreen' : 'Full screen'}${dbl}\nRight-click or hold — both kinds`;
};

// Tooltip only: re-run on hover too, so a rebound Fullscreen-in-Window combo shows at once.
const syncFsTip = (btn, mode) => {
  btn.dataset.title = fsButtonTitle(mode);
  btn.dataset.hkTitle = ROWS[mode || 'screen'].hk;
  const tip = composeControlTitle(btn, hotkeys.isMac, (id) => hotkeys.get(id));
  if (btn.dataset.tip !== tip) btn.dataset.tip = tip;
};

export const syncFsButton = (btn, mode) => {
  if (!btn) return;
  btn.innerHTML = icon(mode ? 'minimize' : 'maximize');
  syncFsTip(btn, mode);
  btn.classList.toggle('active', !!mode);
};

const rowHtml = (face) => {
  const c = comboOf(face.hk);
  return `<span class="ctx-icon">${icon(face.icon)}</span><span class="ctx-label">${face.label}</span>`
    + (c ? `<span class="ctx-hotkey">${keysHtml(c, hotkeys.isMac)}</span>` : '');
};

// A list the hold opened answers that press's release: the row under it is picked, anywhere else
// closes. Read off the rows' boxes, not hit-testing: the list is still pouring in when it may come.
const heldRelease = (menu, { close, pick }) => {
  const rowAt = (e) => {
    for (const li of menu.children) {
      const r = li.getBoundingClientRect?.();
      if (r && e.clientX >= r.left && e.clientX < r.right && e.clientY >= r.top && e.clientY < r.bottom) return li;
    }
    return null;
  };
  const markRow = (row) => { for (const li of menu.children) li.classList.toggle(DRAG_PICK_ROW_CLASS, li === row); };
  const onMove = (e) => markRow(rowAt(e));
  const onUp = (e) => {
    const row = rowAt(e);
    close();
    if (row) pick(row);
  };
  const listen = (on) => {
    const fn = on ? 'addEventListener' : 'removeEventListener';
    document[fn]('pointermove', onMove, true);
    document[fn]('pointerup', onUp, true);
    document[fn]('pointercancel', close, true);
  };
  return { arm: () => listen(true), disarm: () => { listen(false); markRow(null); } };
};

// `getMode()` is the live kind, `choose(kind)` toggles it (layer.js app.toggleFullscreen).
export const wireFullscreenButton = (btn, { getMode, choose }) => {
  if (!btn || typeof document === 'undefined') return null;
  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu fs-kind-menu';
  menu.setAttribute('role', 'menu');
  menu.hidden = true;
  let anchor = btn;
  const onDocDown = (e) => { if (!menu.contains(e.target) && !anchor.contains(e.target)) close(); };
  const onKey = (e) => {
    if (e.key !== 'Escape') return;
    e.stopPropagation();   // the menu takes this Escape, not the fullscreen it sits over
    close();
  };
  const held = heldRelease(menu, { close: () => close(), pick: (row) => choose(row.dataset.fsKind) });
  const close = () => {
    held.disarm();
    if (menu.hidden) return;
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onKey, true);
    hideMenu(menu);
  };
  const open = (at = btn) => {
    anchor = at;
    menu.innerHTML = '';
    for (const kind of FS_KINDS) {
      const li = document.createElement('li');
      li.className = 'accent-dd-opt';
      li.setAttribute('role', 'menuitem');
      li.dataset.fsKind = kind;
      li.innerHTML = rowHtml(fsRowFace(kind, getMode()));
      li.addEventListener('click', () => { close(); choose(kind); });
      menu.appendChild(li);
    }
    showMenu(menu, at);
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onKey, true);
  };
  const g = createFsButtonGesture({
    single: () => choose(getMode() || 'screen'),
    double: () => choose(getMode() || 'window'),
    openMenu: (wasHeld) => {
      open(anchor);
      if (wasHeld) held.arm();
    },
  });
  // The strip's copy opens the menu at itself; its clicks already reach `btn` (clones.js).
  const wireOpeners = (el) => {
    el.addEventListener('contextmenu', (e) => { e.preventDefault(); anchor = el; g.contextmenu(); });
    el.addEventListener('pointerdown', (e) => { anchor = el; g.pressStart({ x: e.clientX, y: e.clientY, button: e.button }); });
    el.addEventListener('pointermove', (e) => g.pressMove({ x: e.clientX, y: e.clientY }));
    el.addEventListener('pointerup', () => g.pressEnd());
    el.addEventListener('pointercancel', () => g.pressEnd());
  };
  btn.addEventListener('click', () => g.click());
  btn.addEventListener('pointerenter', () => syncFsTip(btn, getMode()));
  btn.addEventListener('dblclick', (e) => e.preventDefault());
  wireOpeners(btn);
  // The strip's copy shares the button's id; a fresh copy each entry, so no listener stacks.
  const wireCopyIn = (root) => {
    const copy = [...(root?.querySelectorAll?.('button') ?? [])].find((b) => b.id === btn.id);
    if (copy) wireOpeners(copy);
  };
  return { wireCopyIn, close, isOpen: () => !menu.hidden };
};
