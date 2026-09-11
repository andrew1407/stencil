// ── Right-click menu on transcript rows: the pure model ─────────
// What the menu offers for a row, where its lift lands, and the clipboard write — all
// decided without touching the menu DOM (chatRowMenu.js paints it).
import { icon } from './icons.js';

// ── Right-click menu on transcript rows ─────────────────────────────────────
// One floating menu for any settled row, shared by the panel and the context-menu
// flyout, styled as the projects modal's row menu (components.css aliases
// .chat-row-menu). renderChatLog stamps each row element with its log row
// (el._chatRow, refreshed per repaint), so the menu always reads the CURRENT row.
// The items, data-driven and pure: every settled row gets Copy / Insert into
// prompt; USER rows add Resend (same text + original attachments).
export const chatRowMenuItems = (row) => {
  if (!row || row.pending) return [];
  const items = [
    { id: 'copy', label: 'Copy message', icon: 'copy' },
    { id: 'insert', label: 'Insert into prompt', icon: 'pencil' },
  ];
  if (row.role === 'user') items.push({ id: 'resend', label: 'Resend', icon: 'send' });
  return items;
};

// The hover "…" trigger a settled row wears (renderChatLog appends it): opens the
// same menu as right-click. It sits on the bubble corner facing the panel centre —
// user bubbles are right-aligned so bottom-LEFT, assistant ones bottom-RIGHT.
export const chatRowMenuButton = (row) => {
  if (!chatRowMenuItems(row).length) return null;
  const b = document.createElement('button');
  b.type = 'button';
  b.className = `chat-row-menu-btn chat-row-menu-btn-${row.role === 'user' ? 'left' : 'right'}`;
  // A real control, so labelled — not aria-hidden (a click focuses it and Chrome
  // rejects hiding a focused element). mousedown is inert to keep text selections.
  b.setAttribute('aria-label', 'Message actions');
  b.tabIndex = -1;
  b.innerHTML = icon('more', { size: 13 });
  b.addEventListener('mousedown', (e) => e.preventDefault());
  return b;
};

// The jump pills float exactly where a cut row parks its "…" and win the paint order,
// so the trigger lifts clear of THEM — or hides where a short bubble leaves nowhere to
// lift to. Desktop placeChatCardMore's "shift, else hide". Pure geometry, unit-tested.
export const CHAT_ROW_MENU_JUMP_GAP = 6;   // clearance once lifted clear of the pills

// How far (px) `btn` must rise to clear every pill it currently overlaps — 0 when none
// of them touch it.
export const rowMenuLiftPx = (btn, pills = [], gap = CHAT_ROW_MENU_JUMP_GAP) => {
  if (!btn || !(btn.width > 0)) return 0;
  let lift = 0;
  for (const p of pills) {
    if (!p || !(p.width > 0 && p.height > 0)) continue;
    const overlapsX = btn.left < p.right && btn.right > p.left;
    const overlapsY = btn.bottom > p.top && btn.top < p.bottom;
    if (overlapsX && overlapsY) lift = Math.max(lift, Math.ceil(btn.bottom - p.top) + gap);
  }
  return lift;
};

// Whether lifting `btn` by `lift` still keeps the WHOLE button inside `row` — a short
// bubble has nowhere to lift the trigger TO, and the caller hides it rather than park
// it over the neighbouring message.
export const rowMenuLiftFits = (row, btn, lift) => {
  if (!row || !btn || !(lift > 0)) return true;
  return btn.top - lift >= row.top;
};

// Copy `text` to the clipboard: the async API first, the hidden-textarea
// execCommand fallback where it is missing or refused. Resolves true on success —
// the caller owns the failure toast.
export const copyChatText = async (text, doc = document,
  nav = typeof navigator === 'undefined' ? null : navigator) => {
  try {
    if (nav?.clipboard?.writeText) { await nav.clipboard.writeText(text); return true; }
  } catch { /* fall through to execCommand */ }
  try {
    const ta = doc.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    doc.body.appendChild(ta);
    ta.select();
    const ok = !!doc.execCommand?.('copy');
    ta.remove();
    return ok;
  } catch { return false; }
};

// A right-click on text the user ALREADY selected in this row keeps the NATIVE
// menu — its Copy acts on exactly that selection, which the custom menu can't.
export const selectionCoversRow = (rowEl, win = typeof window === 'undefined' ? null : window) => {
  const sel = win?.getSelection?.();
  if (!sel || sel.isCollapsed || !String(sel).trim()) return false;
  try { return sel.containsNode(rowEl, true); } catch { return false; }
};
