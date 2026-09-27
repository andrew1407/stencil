// The anthropic session-key rows of the assistant settings (llm-providers.md §5): a password
// field whose text is held for this tab only when the dialog saves, the held key's local expiry,
// and Forget. The field is never filled back from storage. Extension twin: options/llm.js.
import { readSessionKey, setSessionKey, forgetSessionKey } from '../../llm/sessionKey.js';

export const SESSION_STORAGE_BLOCKED_TEXT = 'This browser refuses session storage here, so the key cannot be kept.';

const clock = (ms) => new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

// "…until 21:40", with the weekday once the expiry falls on another day. Pure.
export const sessionKeyStatusText = (held, now = Date.now()) => {
  if (!held) return 'No key for this session.';
  const at = new Date(held.expiresAt);
  const day = at.toDateString() === new Date(now).toDateString() ? '' : `${at.toLocaleDateString([], { weekday: 'short' })} `;
  return `Key kept for this tab until ${day}${clock(held.expiresAt)}.`;
};

// `host` scopes every lookup to the dialog; without its rows the whole thing is inert.
export const wireSessionKeyRows = (host, { onInput, onChange, onForget } = {}) => {
  const at = (id) => host?.querySelector?.(`#${id}`) || null;
  const rows = ['chat-session-key-row', 'chat-session-key-status-row', 'chat-session-key-note'].map(at);
  const field = at('chat-session-key');
  const status = at('chat-session-key-status');
  const forget = at('chat-session-key-forget');
  const typed = () => (field?.value || '').trim();
  const render = (text) => {
    const held = readSessionKey();
    if (status) status.textContent = text || sessionKeyStatusText(held);
    if (forget) forget.disabled = !held;
  };
  field?.addEventListener('input', () => onInput?.());
  field?.addEventListener('change', () => onChange?.());
  forget?.addEventListener('click', () => {
    forgetSessionKey();
    if (field) field.value = '';
    render();
    onForget?.();
  });
  return {
    // Anthropic only; every showing starts from an empty field.
    show(on) {
      for (const el of rows) if (el) el.style.display = on ? '' : 'none';
      if (field) field.value = '';
      render();
    },
    // What a probe or a model list sends now: the typed key, else the held one.
    requestKey: () => typed() || readSessionKey()?.key || '',
    // On Save: a typed key is held from now for the TTL; an empty field keeps the held one.
    commit() {
      const key = typed();
      if (field) field.value = '';
      if (key && !setSessionKey(key)) { render(SESSION_STORAGE_BLOCKED_TEXT); return false; }
      render();
      return true;
    },
  };
};
