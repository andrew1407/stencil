// The peer bookkeeping behind tabsCoordinator.js's BroadcastChannel transport; DOM-free and
// transport-free.
export class PeerRoster {
  #seen = new Set();           // peer ids that answered the roll-call (this tab included)
  #active = new Map();         // peerId -> activeId
  #incognito = new Map();      // peerId -> { name, updatedAt } (OTHER tabs' incognito sessions)

  see(peerId) { this.#seen.add(peerId); }

  forget(peerId) {
    this.#seen.delete(peerId);
    this.#active.delete(peerId);
    this.#incognito.delete(peerId);
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
