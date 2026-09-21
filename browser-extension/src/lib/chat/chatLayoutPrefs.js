// ── Chat message side placement ("Swap message sides") ──────────────────────
// PORT of browser/js/ui/chatLayoutPrefs.js (the extension can't import across
// subprojects) — keep the two rule-for-rule. Deliberately NOT persisted, so each freshly
// opened popup / side panel / DevTools panel starts at the default.
let side = 'normal';

export const CHAT_SIDE_NORMAL = 'normal';
export const CHAT_SIDE_SWAPPED = 'swapped';
// The class the swap rules key off (.chat-swapped, on a transcript container):
// css/components/chat/tails.css here, popup.css in the extension.
export const CHAT_SWAPPED_CLASS = 'chat-swapped';

export const chatSide = () => side;

export const setChatSide = (next) => {
  side = next === CHAT_SIDE_SWAPPED ? CHAT_SIDE_SWAPPED : CHAT_SIDE_NORMAL;
};

// Flips the preference for the rest of this tab's session, returning the NEW side.
export const toggleChatSide = () => {
  const next = chatSide() === CHAT_SIDE_SWAPPED ? CHAT_SIDE_NORMAL : CHAT_SIDE_SWAPPED;
  setChatSide(next);
  return next;
};

// Stamps (or clears) the class the CSS swap rules need. `side` defaults to whatever is set, so a
// caller can call this on mount with no argument.
export const applyChatSide = (transcriptEl, side = chatSide()) => {
  transcriptEl?.classList?.toggle(CHAT_SWAPPED_CLASS, side === CHAT_SIDE_SWAPPED);
};
