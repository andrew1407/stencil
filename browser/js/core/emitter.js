// ── Emitter: minimal named-channel pub/sub ──────────────────────────────────
// Backs the app's in-memory subscription registries (TabsCoordinator channels,
// ServerConnection's events feed). DOM-free on purpose — TabsCoordinator runs
// where `window` may be absent, so cross-bridge `stencil:*` CustomEvents stay out.
export class Emitter {
  #channels = new Map();   // name -> Set<fn>

  // Subscribe to a channel; returns an unsubscribe function.
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
