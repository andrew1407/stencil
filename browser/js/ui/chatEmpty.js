// The chat empty state + typing dots.
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { escapeHtml } from './base.js';
import { icon } from './icons.js';

// One set of suggestion chips for every chat surface; what is written on the chip is
// exactly what lands in the input (config/uiStrings.json).
export const CHAT_SUGGESTIONS = UI_STRINGS.chat.suggestions.map((s) => ({ prompt: s, label: s }));
// For the two static templates, so the first paint already has them.
export const chatSuggestionsHtml = () => CHAT_SUGGESTIONS
  .map((s) => `<button type="button" class="chat-suggest" data-prompt="${escapeHtml(s.prompt)}">${escapeHtml(s.label)}</button>`)
  .join('\n                ');
// Same look in the extension panel and the desktop dock; still under prefers-reduced-motion.
export const typingDots = () => {
  const wrap = document.createElement('span');
  wrap.className = 'chat-typing';
  wrap.setAttribute('role', 'status');
  wrap.setAttribute('aria-label', 'Assistant is answering');
  for (let i = 0; i < 3; i++) wrap.appendChild(document.createElement('i'));
  return wrap;
};

// Shown over the composer while a drag hovers it, since that is where a drop attaches.
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
