// ── The chat empty state + typing dots ──────────────────────────
// ONE set of suggestion chips for every chat surface, so the panel and the context-menu
// flyout can never drift apart.
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { escapeHtml } from './base.js';
import { icon } from './icons.js';

// ── The empty state: ONE set of suggestion chips for every chat surface ─────
// Clicking a chip prefills that surface's input, so the panel and the context-menu flyout
// can never drift apart. One string per chip (config/uiStrings.json): what's written on
// the button is exactly what lands in the input — no separate longer prompt.
export const CHAT_SUGGESTIONS = UI_STRINGS.chat.suggestions.map((s) => ({ prompt: s, label: s }));
// The chips as markup, for the two static templates (so the first paint already has
// them, before any JS runs).
export const chatSuggestionsHtml = () => CHAT_SUGGESTIONS
  .map((s) => `<button type="button" class="chat-suggest" data-prompt="${escapeHtml(s.prompt)}">${escapeHtml(s.label)}</button>`)
  .join('\n                ');
// Three bouncing dots for an in-flight turn — the same look in the extension panel and
// the desktop dock. Stops moving under prefers-reduced-motion (see animations.css).
export const typingDots = () => {
  const wrap = document.createElement('span');
  wrap.className = 'chat-typing';
  wrap.setAttribute('role', 'status');
  wrap.setAttribute('aria-label', 'Assistant is answering');
  for (let i = 0; i < 3; i++) wrap.appendChild(document.createElement('i'));
  return wrap;
};

// The cue shown over the COMPOSER while a drag hovers it: an animated icon beside the
// label. It lives in the composer (not over the whole panel) because that is exactly
// where a drop attaches — the transcript above belongs to the canvas's own drop.
const CHAT_DROP_CUE = 'Drop to attach';
export const chatDropCueHtml = (id = 'chat-drop-cue') =>
  `<div class="chat-drop-cue" id="${id}" aria-hidden="true">`
  + `<span class="chat-drop-cue-icon">${icon('image', { size: 16 })}</span>`
  + `<span>${escapeHtml(CHAT_DROP_CUE)}</span></div>`;

export const chatEmptyState = () => {
  const wrap = document.createElement('div');
  wrap.className = 'chat-empty';
  for (const s of CHAT_SUGGESTIONS) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chat-suggest';
    b.dataset.prompt = s.prompt;
    b.textContent = s.label;
    wrap.appendChild(b);
  }
  return wrap;
};
