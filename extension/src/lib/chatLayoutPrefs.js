// ── Chat message side placement ("Swap message sides") ──────────────────────
// A UI-only display preference — which side user/assistant/error bubbles draw on,
// and which corner their tail points from. Browser app parity: js/ui/chatLayoutPrefs.js
// is the same module. Deliberately NOT persisted (user report: it should never carry
// over a reload or a reopened tab/panel) — it lives in a plain module-level variable, so
// each freshly opened popup/side panel/DevTools panel starts at the default, scoped to
// that one instance's own session. There used to be a watchChatSide() that kept the
// three pages in step live via a `storage` event — dropped along with the persistence
// it depended on; each page's own click toggle is now the only way to change it there.
// Default ('normal'): user right, assistant/error left — exactly today's layout.
let side = 'normal';

export const CHAT_SIDE_NORMAL = 'normal';
export const CHAT_SIDE_SWAPPED = 'swapped';
// The class popup.css keys the swap rules off (#sec-assistant .chat-transcript).
export const CHAT_SWAPPED_CLASS = 'chat-swapped';

export const loadChatSide = () => side;

export const saveChatSide = (next) => {
  side = next === CHAT_SIDE_SWAPPED ? CHAT_SIDE_SWAPPED : CHAT_SIDE_NORMAL;
};

// Flips the preference for the rest of this page's session, returning the NEW side.
export const toggleChatSide = () => {
  const next = loadChatSide() === CHAT_SIDE_SWAPPED ? CHAT_SIDE_NORMAL : CHAT_SIDE_SWAPPED;
  saveChatSide(next);
  return next;
};

// Stamps (or clears) the class a transcript element needs for the CSS swap rules.
export const applyChatSide = (transcriptEl, side = loadChatSide()) => {
  transcriptEl?.classList?.toggle(CHAT_SWAPPED_CLASS, side === CHAT_SIDE_SWAPPED);
};
