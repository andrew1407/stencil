// ── Per-message context menu: Copy / Insert into prompt / Resend ────────────
// Right-click on a transcript bubble, or the hover-revealed "⋯" each one carries. Same
// .action-menu language as the popup's row menu; Resend only on the user's own turns
// (their attachments requeue too).
import { icon } from '../../lib/icons.js';
import { createMsgMenu, createMsgMenuButton, appendToPrompt } from '../../lib/chat/msgMenu.js';

export const wireMsgMenu = ({ transcriptEl, inputEl, msgMeta, send, state }) => {
  const msgTargetOf = (el) => {
    const meta = msgMeta.get(el) || { text: el.textContent || '' };
    return {
      el,
      role: el.classList.contains('user') ? 'user' : 'assistant',
      text: meta.text,
      resendText: meta.resendText != null ? meta.resendText : meta.text,
      attachments: meta.attachments || [],
    };
  };
  const msgMenu = createMsgMenu({
    doc: document,
    renderIcon: (name) => icon(name, { size: 15 }),
    actions: {
      copy: (t) => { navigator.clipboard?.writeText(t.text)?.catch?.(() => {}); },
      insert: (t) => appendToPrompt(inputEl, t.text),
      resend: (t) => { if (!state.busy) send(t.resendText, t.attachments); },
    },
  });
  document.body.appendChild(msgMenu.el);
  // Both affordances — right-click and the hover "⋯" — open through here.
  let openedBy = null;   // the ⋯ that opened the menu; null when a right-click did
  const openMenuAt = (el, x, y) => msgMenu.openFor(msgTargetOf(el), {
    x, y, viewport: { width: window.innerWidth, height: window.innerHeight },
  });
  transcriptEl.addEventListener('contextmenu', (e) => {
    const el = e.target?.closest?.('.msg');
    if (!el || el.classList.contains('note') || el.classList.contains('typing-row')) return;
    if (el.closest('.disintegrate-host')) return;   // a leaving clone, not a live message
    // Only OUR default is suppressed; any selection the right-click landed on
    // stays as it is (the menu's own mousedown is inert — see msgMenu.js).
    e.preventDefault();
    openedBy = null;
    openMenuAt(el, e.clientX, e.clientY);
  });
  // The hover-revealed "⋯" each bubble carries (addMsg): the SAME menu, anchored
  // just under the button (clampMenuPosition pulls it back inside the viewport).
  state.addMsgMenuBtn = (el) => {
    const btn = createMsgMenuButton({
      doc: document, renderIcon: (n) => icon(n, { size: 13 }),
      role: el.classList.contains('user') ? 'user' : 'assistant',
    });
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      // A second press on the ⋯ that opened it shuts it (browser chatRowMenu.js rowMenuTrigger).
      if (msgMenu.isOpen() && openedBy === btn) { openedBy = null; msgMenu.close(); return; }
      const r = btn.getBoundingClientRect();
      openMenuAt(el, r.left, r.bottom + 2);
      openedBy = btn;
    });
    el.appendChild(btn);
  };
  document.addEventListener('pointerdown', (e) => {
    if (msgMenu.isOpen() && !msgMenu.el.contains(e.target) && !openedBy?.contains(e.target)) msgMenu.close();
  });
  // Escape dismissal lives in the menu itself (msgMenu.js, open-scoped).
  transcriptEl.addEventListener('scroll', () => msgMenu.close());
};
