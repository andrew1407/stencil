// Right-click menu on transcript rows: the menu itself.
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, menuPopOrigin, surfaceIn, surfaceOut } from './motion.js';
import { chatRowMenuItems, copyChatText, selectionCoversRow } from './chatRowMenuModel.js';
import { icon } from './icons.js';
import { notify } from '../utils.js';
import { publish, EVENTS } from '../bus/appBus.js';

// One open menu app-wide: opening from the other surface or another row replaces it.
let rowMenuEl = null;
let rowMenuClose = null;
export const chatRowMenuOpen = () => !!rowMenuEl;
const closeChatRowMenu = () => { rowMenuClose?.(); };
// The jump pills stand down while any chat popup is up: one event on both edges, and a
// listener re-reads chatPopupOpen() rather than tracking its own state.
export const CHAT_POPUP_EVENT = EVENTS.chatPopup;
export const openComposerMenus = new Set();
export const chatPopupOpen = () => !!rowMenuEl || openComposerMenus.size > 0;
export const announcePopup = () => {
  publish(CHAT_POPUP_EVENT);
};

const openChatRowMenu = (row, x, y, hooks) => {
  closeChatRowMenu();
  const menu = document.createElement('div');
  menu.className = 'chat-row-menu';
// Must not bubble to the ctx-menu's document mousedown closer.
  menu.addEventListener('mousedown', (e) => e.stopPropagation());
  const close = () => {
    if (rowMenuEl !== menu) return;
    rowMenuEl = null;
    rowMenuClose = null;
// Back into the point it grew out of; its own layer, so the node still goes now.
    surfaceOut(menu, { x, y }, { ms: SURFACE_MENU_OUT_MS });
    menu.remove();
    announcePopup();
    document.removeEventListener('pointerdown', onDown, true);
    document.removeEventListener('keydown', onKey, true);
    window.removeEventListener('scroll', close, true);
    window.removeEventListener('blur', close);
  };
  const onDown = (e) => { if (!menu.contains(e.target)) close(); };
  const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
  const act = {
    copy: async () => {
      if (!(await copyChatText(row.text))) notify('Could not copy the message', 'fail');
    },
    insert: () => hooks.onInsert?.(row.text),
    resend: () => hooks.onResend?.(row.text, row.attachments || []),
  };
  for (const it of chatRowMenuItems(row)) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chat-row-menu-item';
    b.innerHTML = icon(it.icon, { size: 15 });
    const label = document.createElement('span');
    label.textContent = it.label;
    b.appendChild(label);
    b.addEventListener('click', (e) => { e.stopPropagation(); close(); act[it.id](); });
    menu.appendChild(b);
  }
  document.body.appendChild(menu);
  const mw = menu.offsetWidth;
  const mh = menu.offsetHeight;
  const left = Math.max(8, x + mw > window.innerWidth - 8 ? x - mw : x);
  const top = Math.max(8, y + mh > window.innerHeight - 8 ? y - mh : y);
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
// The entry pop (animations/overlays.css menuPop) grows out of the open point; the plain
// pop is the fallback when motion.js cannot play dust.
  menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
  surfaceIn(menu, { x, y }, { ms: SURFACE_MENU_IN_MS });
  rowMenuEl = menu;
  rowMenuClose = close;
  announcePopup();
// Wired a tick late, or the opening right-click's own events would close it.
  setTimeout(() => {
    if (rowMenuEl !== menu) return;
    document.addEventListener('pointerdown', onDown, true);
    document.addEventListener('keydown', onKey, true);
    window.addEventListener('scroll', close, true);
    window.addEventListener('blur', close);
  }, 0);
};

// Touch has no hover: a long-press or a double-tap on a bubble opens the menu. Pure
// recognizer, timers injectable.
export const touchMenuGesture = (onOpen, {
  longPressMs = 500, moveTol = 10, doubleTapMs = 350,
  setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = (t) => clearTimeout(t),
} = {}) => {
  let timer = null, sx = 0, sy = 0, key = null, moved = false, fired = false;
  let lastTapKey = null, lastTapAt = -Infinity;
  const stop = () => { if (timer != null) { clearTimer(timer); timer = null; } };
  return {
    start(k, x, y) {
      stop();
      key = k; sx = x; sy = y; moved = false; fired = false;
      timer = setTimer(() => { timer = null; fired = true; lastTapKey = null; onOpen(sx, sy); }, longPressMs);
    },
    move(x, y) {
      if (moved || Math.hypot(x - sx, y - sy) <= moveTol) return;
      moved = true;
      stop();
    },
// Returns true when this gesture opened the menu — the caller suppresses the native
// callout exactly then.
    end(now = Date.now()) {
      const tap = timer != null && !moved;
      stop();
      if (!tap) { const opened = fired; fired = false; if (!opened) lastTapKey = null; return opened; }
      if (lastTapKey === key && now - lastTapAt <= doubleTapMs) {
        lastTapKey = null;
        onOpen(sx, sy);
        return true;
      }
      lastTapKey = key; lastTapAt = now;
      return false;
    },
    cancel() { stop(); moved = true; fired = false; lastTapKey = null; },
  };
};

// Right-click, the hover "…" trigger and the touch gestures open the same menu.
// onInsert(text) appends into this surface's composer; onResend(text, attachments)
// re-sends a user turn with its original attachments.
export const wireChatRowMenu = (transcript, hooks = {}) => {
  transcript.addEventListener('contextmenu', (e) => {
    const rowEl = e.target?.closest?.('.chat-msg');
    if (!rowEl || !transcript.contains(rowEl)) return;
    const row = rowEl._chatRow;
    if (!row || row.pending) return;
    if (selectionCoversRow(rowEl)) return;
    e.preventDefault();
    e.stopPropagation();
    openChatRowMenu(row, e.clientX ?? 0, e.clientY ?? 0, hooks);
  });
  transcript.addEventListener('click', (e) => {
    const btn = e.target?.closest?.('.chat-row-menu-btn');
    if (!btn || !transcript.contains(btn)) return;
    const rowEl = btn.closest('.chat-msg');
    const row = rowEl?._chatRow;
    if (!row || row.pending) return;
    e.preventDefault();
    e.stopPropagation();
    const r = btn.getBoundingClientRect?.();
    openChatRowMenu(row, r ? r.left : 0, r ? r.bottom + 4 : 0, hooks);
  });
  let touchRow = null;
  const gesture = touchMenuGesture((x, y) => {
    if (touchRow) openChatRowMenu(touchRow, x, y, hooks);
  });
  transcript.addEventListener('touchstart', (e) => {
    const t = e.touches?.[0];
    const rowEl = e.target?.closest?.('.chat-msg');
    const row = rowEl?._chatRow;
    if (!t || e.touches.length > 1 || !row || row.pending || !transcript.contains(rowEl)) {
      gesture.cancel();
      touchRow = null;
      return;
    }
    touchRow = row;
    gesture.start(rowEl, t.clientX, t.clientY);
  }, { passive: true });
  transcript.addEventListener('touchmove', (e) => {
    const t = e.touches?.[0];
    if (t) gesture.move(t.clientX, t.clientY);
  }, { passive: true });
  transcript.addEventListener('touchend', (e) => { if (gesture.end()) e.preventDefault(); });
  transcript.addEventListener('touchcancel', () => gesture.cancel());
};
