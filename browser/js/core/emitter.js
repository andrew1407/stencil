// Minimal named-channel pub/sub. DOM-free on purpose: TabsCoordinator runs where `window`
// may be absent, so cross-bridge `stencil:*` CustomEvents stay out.
export class Emitter {
  #channels = new Map();

// Returns an unsubscribe function.
  on(name, cb) {
    let set = this.#channels.get(name);
    if (!set) this.#channels.set(name, set = new Set());
    set.add(cb);
    return () => set.delete(cb);
  }

  emit(name, ...args) {
    const set = this.#channels.get(name);
    if (!set) return;
    for (const cb of set) {
      try {
        cb(...args);
      } catch {
        // isolate a throwing subscriber so the rest still fire
      }
    }
  }

  clear() { this.#channels.clear(); }
}
