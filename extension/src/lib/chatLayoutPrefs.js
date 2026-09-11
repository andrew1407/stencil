// ── Chat message side placement ("Swap message sides") ──────────────────────
// Which side user/assistant/error bubbles draw on. Browser app parity:
// js/ui/chatLayoutPrefs.js is the same module. Deliberately NOT persisted — a plain
// module-level variable, so each freshly opened popup/side panel/DevTools panel starts
// at the default, scoped to that one instance's session.
// Default ('normal'): user right, assistant/error left.
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
