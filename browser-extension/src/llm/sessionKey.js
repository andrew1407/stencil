// ── The anthropic session key (llm-providers.md §5) ─────────────────────
// The user's own Anthropic key in chrome.storage.session as { key, expiresAt } for at most
// providers.json sessionKey.ttlMinutes: gone when the browser closes or the extension reloads, never
// in storage.local, a file or a URL, and readable by trusted contexts only. Browser twin over
// sessionStorage: browser/js/llm/sessionKey.js. Every chrome.* access is guarded for `node --test`.
import PROVIDERS_ASSET from '../config/providers.json' with { type: 'json' };

export const SESSION_KEY_ITEM = 'stencil_llm_session_key';
export const SESSION_KEY_TTL_MS = PROVIDERS_ASSET.providers.anthropic.sessionKey.ttlMinutes * 60_000;

const area = () => globalThis.chrome?.storage?.session;

// Session storage already defaults to trusted contexts; the worker states it so no content script can read the key.
export const lockSessionKeyArea = async () => {
  try { await area()?.setAccessLevel?.({ accessLevel: 'TRUSTED_CONTEXTS' }); } catch { /* the default stands */ }
};

export const forgetSessionKey = async () => {
  try { await area()?.remove(SESSION_KEY_ITEM); } catch { /* unavailable — nothing was held */ }
};

// The held { key, expiresAt }, or null; an expired or malformed record is dropped on the way.
export const readSessionKey = async (now = Date.now()) => {
  let rec;
  try { rec = (await area()?.get(SESSION_KEY_ITEM))?.[SESSION_KEY_ITEM]; } catch { return null; }
  if (rec == null) return null;
  if (typeof rec.key === 'string' && rec.key && Number.isFinite(rec.expiresAt) && rec.expiresAt > now) {
    return { key: rec.key, expiresAt: rec.expiresAt };
  }
  await forgetSessionKey();
  return null;
};

export const sessionKey = async (now = Date.now()) => (await readSessionKey(now))?.key || '';

// Holds `key` for the TTL from `now` and resolves its expiry; an empty key forgets. 0 = not held.
export const setSessionKey = async (key, now = Date.now()) => {
  const k = String(key ?? '').trim();
  if (!k) { await forgetSessionKey(); return 0; }
  const a = area();
  if (!a) return 0;
  const expiresAt = now + SESSION_KEY_TTL_MS;
  try { await a.set({ [SESSION_KEY_ITEM]: { key: k, expiresAt } }); return expiresAt; } catch { return 0; }
};
