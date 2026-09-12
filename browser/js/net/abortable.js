// A signal that aborts a fetch after `ms` (the runtime's own AbortError/TimeoutError);
// undefined where the platform has neither API.
export const NET_TIMEOUT_MS = 30_000;

export const timeoutSignal = (ms = NET_TIMEOUT_MS) => {
  const S = globalThis.AbortSignal;
  if (S && typeof S.timeout === 'function') return S.timeout(ms);
  if (typeof AbortController === 'undefined') return undefined;
  const c = new AbortController();
  setTimeout(() => c.abort(), ms);
  return c.signal;
};
