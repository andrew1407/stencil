// ── Chat message side placement ("Swap message sides") ──────────────────────
// A UI-only display preference — which side user/assistant/error bubbles draw on,
// and which corner their tail points from. Independent of the LLM provider config
// (llmSettings.js) and of any one conversation. Deliberately NOT persisted (user
// report: it should never carry over a reload or a reopened tab) — it lives in a
// plain module-level variable, so a fresh page load always starts at the default
// and the setting is scoped to that one tab's own session. Still adjustable at
// runtime through window.stencil.chat.swapSides (stencilApi.js).
// Default ('normal'): user right, assistant/error left — exactly today's layout.
let side = 'normal';

export const CHAT_SIDE_NORMAL = 'normal';
export const CHAT_SIDE_SWAPPED = 'swapped';
// The class the swap rules key off (.chat-swapped, on a transcript container):
// css/components/chat/tails.css here, popup.css in the extension — both surfaces stamp it
// on their own transcript element from the one shared preference below.
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

// Stamps (or clears) the class a transcript element needs for the CSS swap rules.
// `side` defaults to whatever is currently set, so a caller can just call this on
// mount with no argument.
export const applyChatSide = (transcriptEl, side = chatSide()) => {
  transcriptEl?.classList?.toggle(CHAT_SWAPPED_CLASS, side === CHAT_SIDE_SWAPPED);
};
