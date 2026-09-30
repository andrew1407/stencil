// A signal that aborts a fetch after `ms` (the runtime's own AbortError/TimeoutError);
// undefined where the platform has neither API.
import constants from '../../../common/config/constants.json' with { type: 'json' };

export const NET_TIMEOUT_MS = constants.NETWORK.fetchTimeoutMs;

export const timeoutSignal = (ms = NET_TIMEOUT_MS) => {
  const S = globalThis.AbortSignal;
  if (S && typeof S.timeout === 'function') return S.timeout(ms);
  if (typeof AbortController === 'undefined') return undefined;
  const c = new AbortController();
  setTimeout(() => c.abort(), ms);
  return c.signal;
};
