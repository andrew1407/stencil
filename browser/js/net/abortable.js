// ── Bounded fetches ─────────────────────────────────────────────
// Outside the LLM client every fetch ran unbounded: a hung or black-holed host held the
// promise — and whatever UI awaited it — open for the life of the tab. A signal from here
// aborts the request after `ms`, surfacing as the runtime's own AbortError/TimeoutError.
// Undefined where the platform has neither API (the fetch then behaves as it always did).
export const NET_TIMEOUT_MS = 30000;

export const timeoutSignal = (ms = NET_TIMEOUT_MS) => {
  const S = globalThis.AbortSignal;
  if (S && typeof S.timeout === 'function') return S.timeout(ms);
  if (typeof AbortController === 'undefined') return undefined;
  const c = new AbortController();
  setTimeout(() => c.abort(), ms);
  return c.signal;
};
