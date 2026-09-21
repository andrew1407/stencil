// Cross-tab coordination: the SharedWorker coordinator when available, else a
// BroadcastChannel roll-call, else single-tab assumptions. Never touches localStorage.
import { MSG } from '../worker/messages.js';
import { Emitter } from './emitter.js';
import { PeerRoster } from './remote/peerRoster.js';
import EVENTS from '../config/events.json' with { type: 'json' };

const CHANNEL_NAME = 'stencil_projects';
const READY_TIMEOUT_MS = 400;

export class TabsCoordinator {
  #worker = null;
  #port = null;
  #channel = null;
  #peerId = Math.random().toString(36).slice(2);
  #bus = new Emitter();

  #activeId = null;
  #incognito = null;
  #lastTabCount = { count: 1, youAreOnly: true };
  #readyResolve = null;
  #readyPromise = null;
  #resolvedReady = false;

  #peers = new PeerRoster();

  constructor() {
    this.#readyPromise = new Promise(resolve => { this.#readyResolve = resolve; });

    // whenReady() resolves even if no coordinator exists / answers.
    setTimeout(() => this.#resolveReady(), READY_TIMEOUT_MS);

    if (!this.#trySharedWorker()) this.#tryBroadcastChannel();
  }

  onTabCount(cb) { return this.#bus.on('tabCount', cb); }
  onPeers(cb) { return this.#bus.on('peers', cb); }
  onProjectsChanged(cb) { return this.#bus.on('projectsChanged', cb); }
  onAccent(cb) { return this.#bus.on('accent', cb); }
  onIncognitoPeers(cb) { return this.#bus.on('incognitoPeers', cb); }

  whenReady() { return this.#readyPromise; }

  reportActive(id) {
    this.#activeId = id ?? null;
    if (this.#port) return this.#post({ type: MSG.ACTIVE, activeId: this.#activeId });
    if (this.#channel) this.#channel.postMessage({ type: MSG.ACTIVE, peerId: this.#peerId, activeId: this.#activeId });
  }

  // A small { name, updatedAt }, or null when the session ends.
  reportIncognito(session) {
    this.#incognito = session || null;
    if (this.#port) return this.#post({ type: MSG.INCOGNITO, session: this.#incognito });
    if (this.#channel) this.#channel.postMessage({ type: MSG.INCOGNITO, peerId: this.#peerId, session: this.#incognito });
  }

  // The key is the only payload — peers apply it themselves.
  broadcastAccent(key) {
    if (this.#port) return this.#post({ type: MSG.ACCENT, key });
    if (this.#channel) this.#channel.postMessage({ type: MSG.ACCENT, peerId: this.#peerId, key });
  }

  projectsChanged(detail = {}) {
    // Nudge the extension's editor bridge to re-read the registry; detail-free, it reads localStorage itself.
    try { window.dispatchEvent(new Event(EVENTS.registryChanged)); } catch { /* no DOM (e.g. worker) — the bridge nudge is best-effort */ }
    if (this.#port) return this.#post({ type: MSG.PROJECTS_CHANGED, ...detail });
    if (this.#channel) this.#channel.postMessage({ type: MSG.PROJECTS_CHANGED, peerId: this.#peerId, ...detail });
  }

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
      /* worker port closed — coordination is best-effort */
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

  #tryBroadcastChannel() {
    if (typeof BroadcastChannel === 'undefined') return false;
    try {
      this.#channel = new BroadcastChannel(CHANNEL_NAME);
    } catch {
      this.#channel = null;
      return false;
    }

    this.#peers.see(this.#peerId);
    this.#channel.onmessage = e => this.#onChannelMessage(e.data || {});

    // Roll call: peers reply with HERE; after a short window count/youAreOnly is estimated.
    this.#channel.postMessage({ type: MSG.HELLO, peerId: this.#peerId, activeId: this.#activeId, incognito: this.#incognito });
    setTimeout(() => {
      this.#lastTabCount = this.#peers.tabCount();
      this.#emitTabCount();
      this.#emitPeersFromMap();
      this.#resolveReady();
    }, READY_TIMEOUT_MS - 50);

    window.addEventListener('beforeunload', () => {
      try {
        this.#channel.postMessage({ type: MSG.BYE, peerId: this.#peerId });
      } catch {
        /* channel already closed — peers time us out anyway */
      }
    });
    return true;
  }

  #onChannelMessage(data) {
    const { type, peerId } = data;
    if (peerId === this.#peerId) return;
    if (type === MSG.HELLO) {
      this.#peers.see(peerId);
      if (data.activeId != null) this.#peers.setActive(peerId, data.activeId);
      this.#peers.setIncognito(peerId, data.incognito);
      this.#channel.postMessage({ type: MSG.HERE, peerId: this.#peerId, activeId: this.#activeId, incognito: this.#incognito });
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.HERE) {
      this.#peers.see(peerId);
      if (data.activeId != null) this.#peers.setActive(peerId, data.activeId);
      this.#peers.setIncognito(peerId, data.incognito);
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.ACTIVE) {
      this.#peers.see(peerId);
      this.#peers.setActive(peerId, data.activeId);
      this.#emitPeersFromMap();
      return;
    }
    if (type === MSG.INCOGNITO) {
      this.#peers.see(peerId);
      this.#peers.setIncognito(peerId, data.session);
      this.#emitIncognitoFromMap();
      return;
    }
    if (type === MSG.PROJECTS_CHANGED) return this.#emitProjectsChanged(data);
    if (type === MSG.ACCENT) return this.#emitAccent(data.key);
    if (type === MSG.BYE) {
      this.#peers.forget(peerId);
      this.#recountChannel();
      this.#emitIncognitoFromMap();
      return;
    }
  }

  #emitIncognitoFromMap() { this.#emitIncognitoPeers(this.#peers.incognitoSessions()); }

  #recountChannel() {
    this.#lastTabCount = this.#peers.tabCount();
    this.#emitTabCount();
    this.#emitPeersFromMap();
  }

  #emitPeersFromMap() { this.#emitPeers(this.#peers.activeIds()); }

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
