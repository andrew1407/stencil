// ── Chat message side placement ("Swap message sides") ──────────────────────
// A UI-only preference: which side user/assistant/error bubbles draw on and which corner their
// tail points from. Deliberately NOT persisted (user report: it must not carry over a reload), so
// it is a module-level variable scoped to one tab's session; adjustable through
// window.stencil.chat.swapSides. Default 'normal': user right, assistant/error left.
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
