// ── The composer's "…" overflow: attach / clear / settings ──────────────────
// Browser + desktop parity: the items reuse the existing handlers, only the affordance
// moved. Both edges of the menu fly, like every other surface here.
import { applyChatSide, toggleChatSide } from '../../lib/chatLayoutPrefs.js';
import { dismissTip } from '../../lib/controlTooltip.js';
import { surfaceIn, surfaceOut, centerOf, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../../lib/motion.js';

export const wireComposerMenu = ({ transcriptEl, gearTip, queueFiles, state }) => {
  const moreBtn = document.getElementById('chat-more-btn');
  const moreMenu = document.getElementById('chat-more-menu');
  const attachInput = document.getElementById('chat-attach-input');
  // Played while the menu is still up — `hidden` is display:none — and the cloud is a copy on
  // <body>, so the end state never waits.
  const setMoreOpen = (on) => {
    if (on === !moreMenu.hidden) return;
    if (on) moreMenu.hidden = false;
    if (on) surfaceIn(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_IN_MS });
    else surfaceOut(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_OUT_MS });
    if (!on) moreMenu.hidden = true;
    moreBtn.setAttribute('aria-expanded', String(on));
  };
  const closeMore = () => setMoreOpen(false);
  moreBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    const opening = moreMenu.hidden;
    setMoreOpen(opening);
    if (opening) { dismissTip(); gearTip.hide(); }   // their tiers would sit over the menu
  });
  // The trigger's rich status tooltip (browser chatPanel.js statusHost parity):
  // hover/focus reveal it, one click's focus suppressed (chatStatusTip.js wire).
  gearTip.wire(moreBtn);
  for (const item of moreMenu.querySelectorAll('.chat-more-item'))
    item.addEventListener('click', closeMore);
  document.addEventListener('pointerdown', (e) => {
    if (!moreMenu.hidden && !moreMenu.contains(e.target) && e.target !== moreBtn) closeMore();
  });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeMore(); });
  // Add image → the hidden picker; its files ride the same queue as a drop.
  document.getElementById('chat-attach-btn').addEventListener('click', () => {
    if (!state.busy) attachInput.click();
  });
  attachInput.addEventListener('change', async () => {
    const files = [...(attachInput.files || [])];
    attachInput.value = '';   // re-picking the same file must fire again
    if (files.length) await queueFiles(files);
  });
  // Settings → the extension's own Options page (the popup's gear path).
  document.getElementById('chat-open-options').addEventListener('click', () => {
    document.getElementById('open-options')?.click();
  });
  // Scoped to THIS page's session (chatLayoutPrefs.js) and not persisted, so a fresh popup, side
  // panel or DevTools panel always starts at the default.
  applyChatSide(transcriptEl);
  document.getElementById('chat-swap-sides').addEventListener('click', () => {
    applyChatSide(transcriptEl, toggleChatSide());
  });
};
