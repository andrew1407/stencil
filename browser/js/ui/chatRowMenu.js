// ── Right-click menu on transcript rows: the menu itself ────────
// ONE open menu app-wide (module state, like the thumb preview), and the one event that
// announces any chat popup so the surfaces can keep themselves open under it.
import { SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, menuPopOrigin, surfaceIn, surfaceOut } from './motion.js';
import { chatRowMenuItems, copyChatText, selectionCoversRow } from './chatRowMenuModel.js';
import { icon } from './icons.js';
import { notify } from '../utils.js';
import { publish, EVENTS } from '../bus/appBus.js';

// ONE open menu app-wide (module state, like the thumb preview): opening from the
// other surface, or re-opening on another row, replaces it.
let rowMenuEl = null;
let rowMenuClose = null;
export const chatRowMenuOpen = () => !!rowMenuEl;
const closeChatRowMenu = () => { rowMenuClose?.(); };
// ── Any chat popup, and the one event that announces it ─────────────────────
// The jump pills stand down while ANY chat popup is up. One event on both edges; a
// listener re-reads chatPopupOpen() rather than tracking its own state, so nothing can
// latch. Covers the row menu (body-level) and every composer "…" menu (in-panel).
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
  // Must not bubble to the ctx-menu's document mousedown closer — clicking an
  // item here is chat use, not a click "outside the menu".
  menu.addEventListener('mousedown', (e) => e.stopPropagation());
  const close = () => {
    if (rowMenuEl !== menu) return;
    rowMenuEl = null;
    rowMenuClose = null;
    // Back into the point it grew out of (js/ui/motion.js). Its own layer, so the node
    // still goes NOW — the menu is never left half-removed for the sake of an effect.
    surfaceOut(menu, { x, y }, { ms: SURFACE_MENU_OUT_MS });
    menu.remove();
    announcePopup();   // …and the chrome that stood down comes back
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
  // Cursor-anchored like the projects row menu: flip left/up near the edges.
  const mw = menu.offsetWidth;
  const mh = menu.offsetHeight;
  const left = Math.max(8, x + mw > window.innerWidth - 8 ? x - mw : x);
  const top = Math.max(8, y + mh > window.innerHeight - 8 ? y - mh : y);
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
  // The entry pop (animations.css menuPop) grows out of the open point — as dust when
  // motion.js can play it, and the plain pop is the fallback it leaves behind.
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
    // ANY scroll (the transcript's included) moves the anchor row out from under it.
    window.addEventListener('scroll', close, true);
    window.addEventListener('blur', close);
  }, 0);
};

// Touch has no hover, so no "…" trigger — instead a LONG-PRESS or a DOUBLE-TAP on a
// bubble opens the menu. Pure timing + distance recognizer (timers injectable) so the
// thresholds are unit-testable; wireChatRowMenu feeds it the raw touch events.
export const touchMenuGesture = (onOpen, {
  longPressMs = 500, moveTol = 10, doubleTapMs = 350,
  setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = (t) => clearTimeout(t),
} = {}) => {
  let timer = null, sx = 0, sy = 0, key = null, moved = false, fired = false;
  let lastTapKey = null, lastTapAt = -Infinity;
  const stop = () => { if (timer != null) { clearTimer(timer); timer = null; } };
  return {
    // Finger down on a row: arm the long-press at the touch point.
    start(k, x, y) {
      stop();
      key = k; sx = x; sy = y; moved = false; fired = false;
      timer = setTimer(() => { timer = null; fired = true; lastTapKey = null; onOpen(sx, sy); }, longPressMs);
    },
    // Drifting past the tolerance is a scroll, not a press — both gestures die.
    move(x, y) {
      if (moved || Math.hypot(x - sx, y - sy) <= moveTol) return;
      moved = true;
      stop();
    },
    // Lift. Returns true when THIS gesture opened the menu (long-press already
    // fired, or this tap completed a double-tap) — the caller suppresses the
    // native callout exactly then, never for ordinary taps.
    end(now = Date.now()) {
      const tap = timer != null && !moved;
      stop();
      if (!tap) { const opened = fired; fired = false; if (!opened) lastTapKey = null; return opened; }
      if (lastTapKey === key && now - lastTapAt <= doubleTapMs) {
        lastTapKey = null;
        onOpen(sx, sy);
        return true;
      }
      lastTapKey = key; lastTapAt = now;   // first tap: wait for a partner
      return false;
    },
    cancel() { stop(); moved = true; fired = false; lastTapKey = null; },
  };
};

// Wire one transcript: right-click on a settled .chat-msg row opens the menu at
// the pointer; the hover "…" trigger and the touch gestures open the same one.
// Hooks carry the surface's deltas (its composer, its send path):
//   onInsert(text)              append into this surface's composer and focus it
//   onResend(text, attachments) re-send a user turn with its original attachments
export const wireChatRowMenu = (transcript, hooks = {}) => {
  transcript.addEventListener('contextmenu', (e) => {
    const rowEl = e.target?.closest?.('.chat-msg');
    if (!rowEl || !transcript.contains(rowEl)) return;
    const row = rowEl._chatRow;
    if (!row || row.pending) return;
    if (selectionCoversRow(rowEl)) return;   // the native menu copies the selection
    e.preventDefault();
    e.stopPropagation();
    openChatRowMenu(row, e.clientX ?? 0, e.clientY ?? 0, hooks);
  });
  // The hover "…" trigger (renderChatLog appends it): same menu, anchored at the button.
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
  // Touch: long-press or double-tap on a settled bubble opens it at the touch point.
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
  // Suppress the native menu / selection callout ONLY when ours actually opened.
  transcript.addEventListener('touchend', (e) => { if (gesture.end()) e.preventDefault(); });
  transcript.addEventListener('touchcancel', () => gesture.cancel());
};
