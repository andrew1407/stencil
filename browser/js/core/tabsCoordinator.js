// ── TabsCoordinator: window-side cross-tab coordination ─────────
// Talks to the SharedWorker coordinator when available, else a BroadcastChannel roll-call,
// else single-tab assumptions. Never touches localStorage — only relays small control
// messages so the projects UI knows tab count, peers, and when another tab changed projects.
import { MSG } from '../worker/messages.js';
import { Emitter } from './emitter.js';

const CHANNEL_NAME = 'stencil_projects';
const READY_TIMEOUT_MS = 400;

export class TabsCoordinator {
  #worker = null;
  #port = null;
  #channel = null;
  #peerId = Math.random().toString(36).slice(2);
  #bus = new Emitter();   // channels: tabCount | peers | projectsChanged | accent | incognitoPeers

  #activeId = null;
  #incognito = null;           // this tab's incognito session ({ name, updatedAt }) or null
  #lastTabCount = { count: 1, youAreOnly: true };
  #readyResolve = null;
  #readyPromise = null;
  #resolvedReady = false;

  // BroadcastChannel roll-call bookkeeping
  #peerSeen = new Set();       // peer ids that answered the roll-call
  #peerActive = new Map();     // peerId -> activeId
  #peerIncognito = new Map();  // peerId -> { name, updatedAt } (OTHER tabs' incognito sessions)

  constructor() {
    this.#readyPromise = new Promise(resolve => { this.#readyResolve = resolve; });

    // Always resolve whenReady() even if no coordinator exists / answers.
    setTimeout(() => this.#resolveReady(), READY_TIMEOUT_MS);

    if (!this.#trySharedWorker()) this.#tryBroadcastChannel();
  }

  // ── subscriptions ─────────────────────────────────────────────
  onTabCount(cb) { return this.#bus.on('tabCount', cb); }
  onPeers(cb) { return this.#bus.on('peers', cb); }
  onProjectsChanged(cb) { return this.#bus.on('projectsChanged', cb); }
  onAccent(cb) { return this.#bus.on('accent', cb); }
  onIncognitoPeers(cb) { return this.#bus.on('incognitoPeers', cb); }

  whenReady() { return this.#readyPromise; }

  // ── outgoing ──────────────────────────────────────────────────
  reportActive(id) {
    this.#activeId = id ?? null;
    if (this.#port) return this.#post({ type: MSG.ACTIVE, activeId: this.#activeId });
    if (this.#channel) this.#channel.postMessage({ type: MSG.ACTIVE, peerId: this.#peerId, activeId: this.#activeId });
  }

  // Report this tab's incognito session (a small { name, updatedAt }) or null when it ends,
  // so other tabs can list "incognito open in another tab".
  reportIncognito(session) {
    this.#incognito = session || null;
    if (this.#port) return this.#post({ type: MSG.INCOGNITO, session: this.#incognito });
    if (this.#channel) this.#channel.postMessage({ type: MSG.INCOGNITO, peerId: this.#peerId, session: this.#incognito });
  }

  // Tell every other tab the main accent changed so they repaint live. The key
  // is the only payload — peers apply it themselves (and read localStorage on load).
  broadcastAccent(key) {
    if (this.#port) return this.#post({ type: MSG.ACCENT, key });
    if (this.#channel) this.#channel.postMessage({ type: MSG.ACCENT, peerId: this.#peerId, key });
  }

  projectsChanged(detail = {}) {
    // Nudge the Stencil extension's in-page editor bridge (present only when opened by the
    // extension) to re-read the registry and prune its opened-ledger. Detail-free — the bridge
    // reads localStorage itself, so no project data crosses — and a no-op when no one listens.
    try { window.dispatchEvent(new Event('stencil:registry-changed')); } catch { /* no DOM (e.g. worker) — the bridge nudge is best-effort */ }
    if (this.#port) return this.#post({ type: MSG.PROJECTS_CHANGED, ...detail });
    if (this.#channel) this.#channel.postMessage({ type: MSG.PROJECTS_CHANGED, peerId: this.#peerId, ...detail });
  }

  // ── SharedWorker path ─────────────────────────────────────────
  #trySharedWorker() {
    if (typeof SharedWorker === 'undefined') return false;
    try {
      this.#worker = new SharedWorker(
        new URL('../worker/projectsWorker.js', import.meta.url),
        { type: 'module' }
      );
      this.#port = this.#worker.port;
      this.#port.start();
      this.#port.onmessage = e => this.#onWorkerMessage(e.data || {});
      this.#post({ type: MSG.HELLO });
      window.addEventListener('beforeunload', () => this.#post({ type: MSG.BYE }));
      return true;
    } catch {
      this.#worker = null;
      this.#port = null;
      return false;
    }
  }

  #post(msg) {
    try {
      this.#port.postMessage(msg);
    } catch {
      /* worker port closed (shutting down) — coordination is best-effort */
    }
  }

  #onWorkerMessage(data) {
    if (data.type === MSG.TABCOUNT) {
      this.#lastTabCount = { count: data.count, youAreOnly: !!data.youAreOnly };
      this.#emitTabCount();
      this.#resolveReady();
      return;
    }
    if (data.type === MSG.PEERS) return this.#emitPeers(data.activeIds || []);
    if (data.type === MSG.INCOGNITOS) return this.#emitIncognitoPeers(data.sessions || []);
    if (data.type === MSG.PROJECTS_CHANGED) return this.#emitProjectsChanged(data);
    if (data.type === MSG.ACCENT) return this.#emitAccent(data.key);
  }

  // ── BroadcastChannel fallback ─────────────────────────────────
  #tryBroadcastChannel() {
    if (typeof BroadcastChannel === 'undefined') return false;
    try {
      this.#channel = new BroadcastChannel(CHANNEL_NAME);
    } catch {
      this.#channel = null;
      return false;
    }

    this.#peerSeen.add(this.#peerId);
    this.#channel.onmessage = e => this.#onChannelMessage(e.data || {});

    // Roll call: announce presence and ask who else is here. Peers reply with
    // HERE. After a short window we estimate count/youAreOnly best-effort.
    this.#channel.postMessage({ type: MSG.HELLO, peerId: this.#peerId, activeId: this.#activeId, incognito: this.#incognito });
    setTimeout(() => {
      const count = this.#peerSeen.size;
      this.#lastTabCount = { count, youAreOnly: count <= 1 };
      this.#emitTabCount();
      this.#emitPeersFromMap();
      this.#resolveReady();
    }, READY_TIMEOUT_MS - 50);

    window.addEventListener('beforeunload', () => {
      try {
        this.#channel.postMessage({ type: MSG.BYE, peerId: this.#peerId });
      } catch {
        /* channel already closed during unload — peers time us out anyway */
      }
    });
    return true;
  }

  #onChannelMessage(data) {
    const { type, peerId } = data;
    if (peerId === this.#peerId) return;
    if (type === MSG.HELLO) {
      this.#peerSeen.add(peerId);
      if (data.activeId != null) this.#peerActive.set(peerId, data.activeId);
      this.#setPeerIncognito(peerId, data.incognito);
      // Reply so the newcomer can count us, and share our active + incognito state.
      this.#channel.postMessage({ type: MSG.HERE, peerId: this.#peerId, activeId: this.#activeId, incognito: this.#incognito });
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.HERE) {
      this.#peerSeen.add(peerId);
      if (data.activeId != null) this.#peerActive.set(peerId, data.activeId);
      this.#setPeerIncognito(peerId, data.incognito);
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.ACTIVE) {
      this.#peerSeen.add(peerId);
      if (data.activeId == null) this.#peerActive.delete(peerId);
      else this.#peerActive.set(peerId, data.activeId);
      this.#emitPeersFromMap();
      return;
    }
    if (type === MSG.INCOGNITO) {
      this.#peerSeen.add(peerId);
      this.#setPeerIncognito(peerId, data.session);
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.PROJECTS_CHANGED) return this.#emitProjectsChanged(data);
    if (type === MSG.ACCENT) return this.#emitAccent(data.key);
    if (type === MSG.BYE) {
      this.#peerSeen.delete(peerId);
      this.#peerActive.delete(peerId);
      this.#peerIncognito.delete(peerId);
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
  }

  #setPeerIncognito(peerId, session) {
    if (session) this.#peerIncognito.set(peerId, session);
    else this.#peerIncognito.delete(peerId);
  }
  #emitIncognitoFromMap() {
    this.#emitIncognitoPeers(Array.from(this.#peerIncognito.values()));
  }

  #recountChannel() {
    const count = this.#peerSeen.size;
    this.#lastTabCount = { count, youAreOnly: count <= 1 };
    this.#emitTabCount();
    this.#emitPeersFromMap();
  }

  #emitPeersFromMap() {
    this.#emitPeers(Array.from(this.#peerActive.values()).filter(id => id != null));
  }

  // ── emit helpers (thin wrappers over the shared bus) ──────────
  #emitTabCount() { this.#bus.emit('tabCount', this.#lastTabCount); }
  #emitPeers(ids) { this.#bus.emit('peers', ids); }
  #emitProjectsChanged(detail = {}) { this.#bus.emit('projectsChanged', detail); }
  #emitAccent(key) { this.#bus.emit('accent', key); }
  #emitIncognitoPeers(sessions) { this.#bus.emit('incognitoPeers', sessions); }

  #resolveReady() {
    if (this.#resolvedReady) return;
    this.#resolvedReady = true;
    this.#readyResolve(this.#lastTabCount);
  }
}
