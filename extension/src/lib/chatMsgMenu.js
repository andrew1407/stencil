// ── Assistant transcript: per-message context menu (pure, unit-tested) ───────
// Right-clicking a chat bubble offers Copy message / Insert into prompt, and —
// on the user's OWN turns — Resend. Built with an injected document
// + icon renderer so composition and action dispatch stay testable without a real
// DOM (the lib/chatUi.js pattern); popup/assistant.js wires the real clipboard,
// selection and send loop. Message text is untrusted model output: labels/glyphs
// here are fixed strings, and the text itself only ever travels as DATA.
import { surfaceIn, surfaceOut, settleSurface } from './motion.js';

// The items a message of `role` offers, in order. Resend re-sends only the user's
// own turns — an assistant reply has nothing to send "again".
export const msgMenuItems = (role) => [
  { id: 'copy',   label: 'Copy message',       icon: 'copy' },
  { id: 'insert', label: 'Insert into prompt', icon: 'pencil' },
  ...(role === 'user' ? [{ id: 'resend', label: 'Resend', icon: 'send' }] : []),
];

// Which side of the bubble the hover "⋯" trigger sits on: the side facing the
// panel centre (user bubbles are right-aligned, assistant/error bubbles left-aligned).
export const msgMenuBtnSide = (role) => (role === 'user' ? 'left' : 'right');

// The hover-revealed "⋯" trigger a bubble carries — same menu as right-click.
// CSS hides it until the bubble is hovered; the caller wires the click to openFor.
export const createMsgMenuButton = ({ doc, renderIcon = () => '', role }) => {
  const btn = doc.createElement('button');
  btn.type = 'button';
  btn.className = `msg-menu-btn ${msgMenuBtnSide(role)}`;
  // A real control, so labelled — aria-hidden on a clickable button trips
  // Chrome's focused-descendant check. mousedown stays inert for selections.
  btn.setAttribute('aria-label', 'Message actions');
  btn.tabIndex = -1;
  btn.innerHTML = renderIcon('dots');   // fixed glyph, never user data
  btn.addEventListener('mousedown', (e) => e.preventDefault());
  return btn;
};

// "Insert into prompt": APPEND onto the composer's existing draft (its own line),
// never replacing what the user half-wrote, then hand focus back to the composer.
export const appendToPrompt = (inputEl, text) => {
  const cur = inputEl.value || '';
  inputEl.value = cur ? `${cur}\n${text}` : text;
  inputEl.focus();
};

// Same pull-back-inside-the-viewport clamping as the popup row menu (popup.js
// placeMenu): a point near the right/bottom edge slides the box in, never off.
export const clampMenuPosition = ({ x, y, size, viewport, margin = 6 }) => {
  const w = (size && size.width) || 0;
  const h = (size && size.height) || 0;
  if (x + w > viewport.width) x = Math.max(margin, viewport.width - w - margin);
  if (y + h > viewport.height) y = Math.max(margin, viewport.height - h - margin);
  return { left: Math.max(margin, x), top: Math.max(margin, y) };
};

// ── Yielding to the jump pills (assistant.js wires these to the hovered bubble) ──
// The pills win: a row's "…" that would sit under one rises clear of it, or hides
// where there is no room to. Same "shift, else hide" as the desktop
// (placeChatCardMore) and the browser (chatView.js).
export const MSG_MENU_JUMP_GAP = 6;   // clearance once lifted clear of the pills

// How far (px) `btn` must rise to clear every pill it currently overlaps — 0 when
// none of them touch it. Pure geometry over DOMRect-shaped objects.
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

// Whether lifting `btn` by `lift` still keeps the WHOLE button over its own bubble —
// a short bubble has nowhere to lift the trigger TO, and the caller hides it rather
// than park it over the neighbouring message. Pure.
export const msgMenuLiftFits = (bubble, btn, lift) => {
  if (!bubble || !btn || !(lift > 0)) return true;
  return btn.top - lift >= bubble.top;
};

// Where the open-pop animation grows FROM: the click point (x,y) expressed
// relative to the menu's final clamped left/top, held inside the menu box so a
// clamped-away menu still grows from its nearest edge. Shared by both menus.
export const menuTransformOrigin = ({ x, y, left, top, size }) => {
  const w = (size && size.width) || 0;
  const h = (size && size.height) || 0;
  const ox = Math.min(Math.max(x - left, 0), w);
  const oy = Math.min(Math.max(y - top, 0), h);
  return `${ox}px ${oy}px`;
};

/**
 * Build the floating menu element (`.action-menu` styling — the popup's own row
 * menu language). The caller appends `el` to its page and owns outside-click /
 * scroll dismissal via `close()`; Escape dismissal is owned here (open-scoped).
 * @param {object} args
 * @param {Document} args.doc - Document used for createElement.
 * @param {(name:string)=>string} [args.renderIcon] - Named glyph → svg markup (lib/icons.js).
 * @param {Record<string,(target:object)=>void>} args.actions - Handler per item id;
 *   each receives the `target` passed to openFor ({ role, text, el, attachments? }).
 * @returns {{el:object, openFor:Function, close:Function, isOpen:()=>boolean}}
 */
export const createMsgMenu = ({ doc, renderIcon = () => '', actions = {} }) => {
  let openOrigin = null;   // the point it grew out of, so the close pours it back there
  const el = doc.createElement('div');
  el.className = 'action-menu chat-msg-menu';
  el.hidden = true;
  // Pressing a menu item must not clear a selection the user right-clicked on:
  // Copy/Insert act on the whole message either way.
  el.addEventListener('mousedown', (e) => e.preventDefault());

  // Escape closes THIS menu only: capture-phase so it wins over the dialog /
  // popup behind it, attached on open and removed on close (never leaks).
  const onEscape = (e) => {
    if (e.key !== 'Escape') return;
    e.preventDefault();
    e.stopPropagation();
    close();
  };

  const close = () => {
    // Dusted back into the point it came from BEFORE it is hidden, while it can still be
    // measured — the motes are the menu leaving, and the close stays synchronous.
    if (!el.hidden) surfaceOut(el, openOrigin);
    else settleSurface(el);
    el.hidden = true;
    el.innerHTML = '';
    doc.removeEventListener?.('keydown', onEscape, true);
  };

  // Open for `target` at the pointer (viewport-clamped). Rebuilt per open, so the
  // items always match the clicked message's role.
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
    // The pop animation grows out of the click point (popup.css .action-menu) — and so
    // do its particles, which erupt from that same point and gather into the box.
    el.style.transformOrigin = menuTransformOrigin({ x, y, left, top, size });
    openOrigin = { x, y };
    surfaceIn(el, openOrigin);
    doc.addEventListener?.('keydown', onEscape, true);   // idempotent re-add on reopen
  };

  return { el, openFor, close, isOpen: () => !el.hidden };
};
