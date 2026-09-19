// A configurable `globalThis.chrome` stub for `node --test` — one place for the mock
// six suites used to hand-roll. It covers exactly the surface those suites exercise:
// storage.local get/set over an in-memory store, storage.sync's callback-or-promise
// get, storage.onChanged, and runtime.sendMessage/onMessage. Nothing speculative —
// tabs/action/contextMenus/scripting stay out until a suite actually drives them.

// Answer a chrome.storage get for a string key, an array of keys, or null (everything),
// returning only the keys actually present — the shape the hand-rolled mocks pinned.
const pick = (store, key) => {
  if (key == null) return { ...store };
  const keys = Array.isArray(key) ? key : [key];
  const out = {};
  for (const k of keys) if (k in store) out[k] = store[k];
  return out;
};

/**
 * Install the stub on `globalThis.chrome`, remembering what was there before.
 *
 * @param {object} [opts]
 * @param {Record<string,any>} [opts.local]  - Pre-seeded chrome.storage.local contents.
 * @param {Record<string,any>} [opts.sync]   - Pre-seeded chrome.storage.sync contents
 *   (sync.get merges these over the caller's defaults, as real chrome does).
 * @param {boolean} [opts.storageThrows]     - Every storage.local access throws, the way
 *   a torn-down extension context behaves.
 * @param {(msg: any) => any} [opts.respond] - What runtime.sendMessage returns, verbatim
 *   (a Promise models an answering receiver; undefined models no receiver at all).
 *   Default: Promise<undefined>, the MV3 no-listener resolution.
 */
export const installChromeStub = ({
  local = {},
  sync = {},
  storageThrows = false,
  respond,
} = {}) => {
  const prev = { had: 'chrome' in globalThis, value: globalThis.chrome };
  let localStore = { ...local };
  let syncStore = { ...sync };
  const sets = [];               // every storage.local.set payload, in call order
  const sent = [];               // every runtime.sendMessage payload, in call order
  const runtimeListeners = [];   // runtime.onMessage listeners, for driving SW messages
  const changedListeners = [];   // storage.onChanged listeners

  globalThis.chrome = {
    storage: {
      local: {
        get: async (key) => {
          if (storageThrows) throw new Error('no storage');
          return pick(localStore, key);
        },
        // Recorded synchronously but landing only after a microtask — the real-latency gap in which a
        // concurrent read-modify-write can race.
        set: async (obj) => {
          if (storageThrows) throw new Error('no storage');
          sets.push(obj);
          await Promise.resolve();
          Object.assign(localStore, obj);
        },
      },
      sync: {
        // Real chrome answers get(defaults) as a promise AND get(defaults, cb) via the
        // callback; the bridges use both shapes.
        get: (defaults, cb) => {
          const isDefaults = defaults && typeof defaults === 'object' && !Array.isArray(defaults);
          const out = isDefaults
            ? { ...defaults, ...pick(syncStore, Object.keys(defaults)) }
            : pick(syncStore, defaults ?? null);
          if (typeof cb === 'function') { cb(out); return; }
          return Promise.resolve(out);
        },
      },
      onChanged: { addListener: (fn) => changedListeners.push(fn) },
    },
    runtime: {
      sendMessage: (m) => {
        sent.push(m);
        return respond ? respond(m) : Promise.resolve();
      },
      onMessage: { addListener: (fn) => runtimeListeners.push(fn) },
    },
  };

  return {
    sent,
    sets,
    runtimeListeners,
    changedListeners,
    /** Raw storage.local contents. */
    peek: () => localStore,
    /** Per-test reset: empty the store, drop the recordings, keep the stub installed. */
    reset: () => {
      localStore = {};
      syncStore = { ...sync };
      sets.length = 0;
      sent.length = 0;
      runtimeListeners.length = 0;
      changedListeners.length = 0;
    },
    /** Put back whatever `globalThis.chrome` held before install (or remove it). */
    restore: () => {
      if (prev.had) globalThis.chrome = prev.value;
      else delete globalThis.chrome;
    },
  };
};
