// The transcript's per-message context menu. Message text is untrusted model output:
// labels/glyphs here are fixed strings, and the text itself only ever travels as DATA.
import { surfaceIn, surfaceOut, settleSurface } from '../motion.js';

// Resend re-sends only the user's own turns.
export const msgMenuItems = (role) => [
  { id: 'copy',   label: 'Copy message',       icon: 'copy' },
  { id: 'insert', label: 'Insert into prompt', icon: 'pencil' },
  ...(role === 'user' ? [{ id: 'resend', label: 'Resend', icon: 'send' }] : []),
];

// The "⋯" trigger sits on the side facing the panel centre.
export const msgMenuBtnSide = (role) => (role === 'user' ? 'left' : 'right');

export const createMsgMenuButton = ({ doc, renderIcon = () => '', role }) => {
  const btn = doc.createElement('button');
  btn.type = 'button';
  btn.className = `msg-menu-btn ${msgMenuBtnSide(role)}`;
  // Labelled, not aria-hidden: a hidden clickable button trips Chrome's
  // focused-descendant check. mousedown stays inert so selections survive.
  btn.setAttribute('aria-label', 'Message actions');
  btn.tabIndex = -1;
  btn.innerHTML = renderIcon('dots');   // fixed glyph, never user data
  btn.addEventListener('mousedown', (e) => e.preventDefault());
  return btn;
};

// APPENDS on its own line, never replacing a half-written draft.
export const appendToPrompt = (inputEl, text) => {
  const cur = inputEl.value || '';
  inputEl.value = cur ? `${cur}\n${text}` : text;
  inputEl.focus();
};

export const clampMenuPosition = ({ x, y, size, viewport, margin = 6 }) => {
  const w = (size && size.width) || 0;
  const h = (size && size.height) || 0;
  if (x + w > viewport.width) x = Math.max(margin, viewport.width - w - margin);
  if (y + h > viewport.height) y = Math.max(margin, viewport.height - h - margin);
  return { left: Math.max(margin, x), top: Math.max(margin, y) };
};

// The jump pills win: a "…" that would sit under one rises clear of it, or hides where
// there is no room to — the desktop's placeChatCardMore and the browser's chatView.js.
export const MSG_MENU_JUMP_GAP = 6;   // px clearance once lifted clear of the pills

// px `btn` must rise to clear every pill it overlaps; 0 when none touch it.
export const msgMenuLiftPx = (btn, pills = [], gap = MSG_MENU_JUMP_GAP) => {
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

// A short bubble has nowhere to lift the trigger TO; the caller hides it instead.
export const msgMenuLiftFits = (bubble, btn, lift) => {
  if (!bubble || !btn || !(lift > 0)) return true;
  return btn.top - lift >= bubble.top;
};

// The click point relative to the clamped menu box, held inside it so a clamped-away
// menu still grows from its nearest edge.
export const menuTransformOrigin = ({ x, y, left, top, size }) => {
  const w = (size && size.width) || 0;
  const h = (size && size.height) || 0;
  const ox = Math.min(Math.max(x - left, 0), w);
  const oy = Math.min(Math.max(y - top, 0), h);
  return `${ox}px ${oy}px`;
};

// The caller appends `el` and owns outside-click / scroll dismissal; Escape is owned
// here. `actions` is a handler per item id, each receiving openFor's `target`.
export const createMsgMenu = ({ doc, renderIcon = () => '', actions = {} }) => {
  let openOrigin = null;
  const el = doc.createElement('div');
  el.className = 'action-menu chat-msg-menu';
  el.hidden = true;
  // A press must not clear a selection the user right-clicked on.
  el.addEventListener('mousedown', (e) => e.preventDefault());

  // Capture-phase so it wins over the dialog behind it; armed only while open.
  const onEscape = (e) => {
    if (e.key !== 'Escape') return;
    e.preventDefault();
    e.stopPropagation();
    close();
  };

  const close = () => {
    // Dusted out BEFORE it is hidden, while it can still be measured.
    if (!el.hidden) surfaceOut(el, openOrigin);
    else settleSurface(el);
    el.hidden = true;
    el.innerHTML = '';
    doc.removeEventListener?.('keydown', onEscape, true);
  };

  const openFor = (target, { x, y, viewport }) => {
    el.innerHTML = '';
    for (const item of msgMenuItems(target.role)) {
      const btn = doc.createElement('button');
      btn.type = 'button';
      const ic = doc.createElement('span');
      ic.className = 'ic';
      ic.innerHTML = renderIcon(item.icon);   // fixed glyph, never user data
      btn.append(ic, doc.createTextNode(item.label));
      btn.addEventListener('click', () => { close(); actions[item.id]?.(target); });
      el.appendChild(btn);
    }
    el.hidden = false;
    const size = { width: el.offsetWidth || 0, height: el.offsetHeight || 0 };
    const { left, top } = clampMenuPosition({ x, y, size, viewport });
    el.style.left = `${left}px`;
    el.style.top = `${top}px`;
    el.style.transformOrigin = menuTransformOrigin({ x, y, left, top, size });
    openOrigin = { x, y };
    surfaceIn(el, openOrigin);
    doc.addEventListener?.('keydown', onEscape, true);
  };

  return { el, openFor, close, isOpen: () => !el.hidden };
};
