// ── The anthropic session key (llm-providers.md §5) ─────────────────────
// The user's own Anthropic key, held in THIS tab's sessionStorage as { key, expiresAt } for at most
// providers.json sessionKey.ttlMinutes: a reload keeps it, a new tab starts without it, and it never
// reaches localStorage, a file, a URL or a log. Twin: browser-extension/src/llm/sessionKey.js.
import PROVIDERS_ASSET from '../config/llm/providers.json' with { type: 'json' };

export const SESSION_KEY_ITEM = 'stencil_llm_session_key';
export const SESSION_KEY_TTL_MS = PROVIDERS_ASSET.providers.anthropic.sessionKey.ttlMinutes * 60_000;

// Absent under Node, and a blocked origin throws on the mere property read.
const storage = () => {
  try { return globalThis.sessionStorage ?? null; } catch { return null; }
};

export const forgetSessionKey = () => {
  try { storage()?.removeItem(SESSION_KEY_ITEM); } catch { /* blocked — nothing was held */ }
};

// The held { key, expiresAt }, or null; an expired or malformed record is dropped on the way.
export const readSessionKey = (now = Date.now()) => {
  let raw = null;
  try { raw = storage()?.getItem(SESSION_KEY_ITEM) ?? null; } catch { return null; }
  if (raw === null) return null;
  let rec = null;
  try { rec = JSON.parse(raw); } catch { /* malformed — dropped below */ }
  if (typeof rec?.key === 'string' && rec.key && Number.isFinite(rec.expiresAt) && rec.expiresAt > now) {
    return { key: rec.key, expiresAt: rec.expiresAt };
  }
  forgetSessionKey();
  return null;
};

export const sessionKey = (now = Date.now()) => readSessionKey(now)?.key || '';

// Holds `key` for the TTL from `now` and returns its expiry; an empty key forgets. 0 = not held.
export const setSessionKey = (key, now = Date.now()) => {
  const k = String(key ?? '').trim();
  if (!k) { forgetSessionKey(); return 0; }
  const s = storage();
  if (!s) return 0;
  const expiresAt = now + SESSION_KEY_TTL_MS;
  try { s.setItem(SESSION_KEY_ITEM, JSON.stringify({ key: k, expiresAt })); return expiresAt; } catch { return 0; }
};
