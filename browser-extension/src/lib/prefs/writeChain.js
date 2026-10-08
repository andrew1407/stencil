// The one serialization rail for chrome.storage keys more than one context writes (pins, the
// opened ledger): a promise chain per key, and the relay that sends an extension page's writes
// to the worker so every context's writes share the worker's chain.
import { MSG } from '../messages.js';

// chrome.storage has no atomic read-modify-write: each write reads inside its turn.
export const writeChain = () => {
  let tail = Promise.resolve();
  return (fn) => {
    const run = tail.then(fn);
    tail = run.catch(() => {});   // a rejected write must not wedge the queue
    return run;
  };
};

let send = null;

export const routeStoreWrites = (fn = (m) => chrome.runtime.sendMessage(m)) => { send = fn; };

// No answering worker falls back to this document's own chain.
export const viaWorker = async (store, op, args, local) => {
  if (send) {
    try {
      const reply = await send({ type: MSG.STORE_WRITE, store, op, args });
      if (reply && reply.ok) return reply.value;
    } catch { /* no receiver */ }
  }
  return local(...args);
};
