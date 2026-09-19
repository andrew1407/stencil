// Shared in-memory `localStorage` stand-in for the browser test suites, implementing everything the
// storage-backed modules touch. Two ways in: createMemoryStorage(), a storage OBJECT to inject (the stores take
// theirs as a constructor argument, so nothing global is touched), and installMemoryStorage(), the same object
// on globalThis.localStorage, whose .restore() puts the previous value back. `_map` is exposed deliberately:
// several suites assert on the raw stored strings to prove a payload never reached localStorage.

/**
 * A faithful subset of the Storage interface, backed by a Map.
 * @param {Record<string,string>} init - Entries to pre-seed.
 * @param {{throwOnSet?: boolean}} opts - `throwOnSet` makes setItem raise a
 *   QuotaExceededError, the failure real browsers hit once the origin is full.
 *   It is a plain property, so a test can flip it partway through.
 */
export const createMemoryStorage = (init = {}, opts = {}) => {
  const map = new Map(Object.entries(init));
  return {
    _map: map,
    throwOnSet: opts.throwOnSet || false,

    getItem(key) {
      return map.has(key) ? map.get(key) : null;
    },
    setItem(key, value) {
      if (this.throwOnSet) {
        // Real browsers throw a DOMException whose *name* is what callers branch on.
        const err = new Error('quota');
        err.name = 'QuotaExceededError';
        throw err;
      }
      // Storage stringifies on write; keeping that faithful catches code that
      // round-trips a number or an object and expects its type back.
      map.set(key, String(value));
    },
    removeItem(key) {
      map.delete(key);
    },
    clear() {
      map.clear();
    },
    // Enumeration helper the projects store uses to sweep its keys.
    keys() {
      return Array.from(map.keys());
    },
    // The indexed accessors of the real Storage interface.
    key(i) {
      return Array.from(map.keys())[i] ?? null;
    },
    get length() {
      return map.size;
    },
  };
};

/**
 * Install a memory storage as globalThis.localStorage.
 *
 * Node runs each test FILE in its own process, so a module-level install cannot leak
 * across suites and needs no teardown. Restore matters only when a single file wants
 * the global present for some tests and absent for others.
 *
 * @returns the storage, with an extra `restore()` that undoes the install.
 */
export const installMemoryStorage = (init = {}, opts = {}) => {
  const storage = createMemoryStorage(init, opts);
  const had = Object.prototype.hasOwnProperty.call(globalThis, 'localStorage');
  const previous = had ? Object.getOwnPropertyDescriptor(globalThis, 'localStorage') : null;

  globalThis.localStorage = storage;

  storage.restore = () => {
    if (previous) Object.defineProperty(globalThis, 'localStorage', previous);
    else delete globalThis.localStorage;
  };
  return storage;
};
