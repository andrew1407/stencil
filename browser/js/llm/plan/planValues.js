// ── Shared op-plan value guards + resolvers ─────────────────────
// The small checks the validator, the executors and the plan runner all share.
import { LIMITS } from './planSchema.js';

export const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
export const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;

// Variant labels name files/projects — keep them short and filesystem-safe.
export const sanitizeLabel = (label) => String(label == null ? '' : label)
  .trim().replace(/[^\w \-]+/g, '').replace(/\s+/g, ' ').slice(0, 40).trim() || 'variant';

// §10 connect/disconnect: resolve `server` against a list the USER owns — exact URL, else a
// UNIQUE host match. The model can NEVER introduce a host, and plans never carry tokens.
export const resolveServer = (server, entries, what) => {
  const want = String(server ?? '').trim();
  const urlOf = (e) => (typeof e === 'string' ? e : e.url);
  const exact = entries.find((e) => urlOf(e) === want);
  if (exact != null) return exact;
  const w = want.toLowerCase();
  const matches = entries.filter((e) => {
    try {
      const u = new URL(urlOf(e));
      return u.host.toLowerCase() === w || u.hostname.toLowerCase() === w;
    } catch { return false; }
  });
  if (matches.length === 1) return matches[0];
  throw new Error(matches.length
    ? `Unknown server "${server}" — that host matches several ${what}; use the full URL`
    : `Unknown server "${server}" — not among your ${what}`);
};
