// A signal that aborts a fetch after `ms`, as the runtime's own TimeoutError.
import constants from '../../../common/config/constants.json' with { type: 'json' };

export const NET_TIMEOUT_MS = constants.NETWORK.fetchTimeoutMs;

export const timeoutSignal = (ms = NET_TIMEOUT_MS) => AbortSignal.timeout(ms);
