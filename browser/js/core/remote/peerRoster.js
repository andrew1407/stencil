// The peer bookkeeping behind tabsCoordinator.js's BroadcastChannel transport; DOM-free and
// transport-free.
export class PeerRoster {
  #seen = new Set();           // peer ids that answered the roll-call (this tab included)
  #active = new Map();         // peerId -> activeId
  #incognito = new Map();      // peerId -> { name, updatedAt } (OTHER tabs' incognito sessions)
  #missed = new Map();         // peerId -> pings gone unanswered in a row

  // True when the peer was not counted: a new tab, or one back after it was forgotten.
  see(peerId) {
    const fresh = !this.#seen.has(peerId);
    this.#seen.add(peerId);
    this.#missed.set(peerId, 0);
    return fresh;
  }

  forget(peerId) {
    this.#seen.delete(peerId);
    this.#active.delete(peerId);
    this.#incognito.delete(peerId);
    this.#missed.delete(peerId);
  }

  // One ping round: every peer but `self` silent for more than `misses` rounds (a crashed tab
  // never says BYE) is forgotten. True when the count changed.
  sweep(self, misses) {
    let changed = false;
    for (const id of this.#seen) {
      if (id === self) continue;
      const n = (this.#missed.get(id) ?? 0) + 1;
      if (n > misses) { this.forget(id); changed = true; } else this.#missed.set(id, n);
    }
    return changed;
  }

  // A null/undefined value clears the entry — "nothing open" and "no incognito session".
  setActive(peerId, activeId) {
    if (activeId == null) this.#active.delete(peerId);
    else this.#active.set(peerId, activeId);
  }

  setIncognito(peerId, session) {
    if (session) this.#incognito.set(peerId, session);
    else this.#incognito.delete(peerId);
  }

  tabCount() {
    const count = this.#seen.size;
    return { count, youAreOnly: count <= 1 };
  }

  activeIds() { return Array.from(this.#active.values()).filter((id) => id != null); }

  incognitoSessions() { return Array.from(this.#incognito.values()); }
}
