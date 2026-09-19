// Right-click menu on transcript rows: the pure model (chatRowMenu.js paints it).
import { icon } from './icons.js';

// Styled as the projects modal's row menu (components/projects.css aliases .chat-row-menu).
// renderChatLog stamps each row element with its log row (el._chatRow); user rows add Resend.
export const chatRowMenuItems = (row) => {
  if (!row || row.pending) return [];
  const items = [
    { id: 'copy', label: 'Copy message', icon: 'copy' },
    { id: 'insert', label: 'Insert into prompt', icon: 'pencil' },
  ];
  if (row.role === 'user') items.push({ id: 'resend', label: 'Resend', icon: 'send' });
  return items;
};

// The hover "…" trigger sits on the bubble corner facing the panel centre.
export const chatRowMenuButton = (row) => {
  if (!chatRowMenuItems(row).length) return null;
  const b = document.createElement('button');
  b.type = 'button';
  b.className = `chat-row-menu-btn chat-row-menu-btn-${row.role === 'user' ? 'left' : 'right'}`;
// Labelled, not aria-hidden (Chrome rejects hiding a focused element); mousedown is inert
// to keep text selections.
  b.setAttribute('aria-label', 'Message actions');
  b.tabIndex = -1;
  b.innerHTML = icon('more', { size: 13 });
  b.addEventListener('mousedown', (e) => e.preventDefault());
  return b;
};

// The jump pills float where a cut row parks its "…" and win the paint order, so the
// trigger lifts clear of them, or hides (desktop placeChatCardMore's "shift, else hide").
export const CHAT_ROW_MENU_JUMP_GAP = 6;

// How far `btn` must rise to clear every pill it overlaps; 0 when none touch it.
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

// A short bubble has nowhere to lift the trigger to; the caller hides it instead.
export const rowMenuLiftFits = (row, btn, lift) => {
  if (!row || !btn || !(lift > 0)) return true;
  return btn.top - lift >= row.top;
};

// The async API first, the hidden-textarea execCommand fallback; the caller owns the failure toast.
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

// A right-click on text already selected in this row keeps the native menu.
export const selectionCoversRow = (rowEl, win = typeof window === 'undefined' ? null : window) => {
  const sel = win?.getSelection?.();
  if (!sel || sel.isCollapsed || !String(sel).trim()) return false;
  try { return sel.containsNode(rowEl, true); } catch { return false; }
};
