// ── projectsWorker: SharedWorker tab coordinator (PURE — no storage) ──
// Pure message router between tabs; NEVER touches localStorage (SharedWorker is
// window-less) — persistence stays window-side. State = connected ports + each
// port's active project id, used to report tab count, broadcast peer-open project
// ids, and relay "projects changed" re-render pings.
import { MSG } from './messages.js';

// A crashed tab never says BYE: a port silent for MISSES pings in a row is dropped (~30 s).
const PING_MS = 10_000;
const MISSES = 3;

// port -> { activeId, incognito, missed }   (incognito = { name, updatedAt } or null)
const ports = new Map();
let pinger = null;

const tabcountMsg = () => ({
  type: MSG.TABCOUNT,
  count: ports.size,
  youAreOnly: ports.size === 1,
});

const peersMsg = () => ({
  type: MSG.PEERS,
  activeIds: Array.from(ports.values())
    .map(s => s.activeId)
    .filter(id => id != null),
});

const post = (port, msg) => {
  try {
    port.postMessage(msg);
  } catch {
    /* port closed (tab gone) — the sweep drops it once its pings go unanswered */
  }
};
const broadcast = (msg, except = null) => {
  for (const port of ports.keys()) {
    if (port === except) continue;
    post(port, msg);
  }
};

// Each port is told the OTHER ports' incognito sessions (so a tab never lists its own).
const sendIncognitos = () => {
  for (const port of ports.keys()) {
    const sessions = [];
    for (const [p, s] of ports) if (p !== port && s.incognito) sessions.push(s.incognito);
    post(port, { type: MSG.INCOGNITOS, sessions });
  }
};

const drop = port => {
  if (!ports.has(port)) return;
  ports.delete(port);
  if (ports.size === 0) { clearInterval(pinger); pinger = null; }
  broadcast(tabcountMsg());
  broadcast(peersMsg());
  sendIncognitos();
};

const sweep = () => {
  for (const [port, state] of ports) {
    if (++state.missed > MISSES) drop(port);
    else post(port, { type: MSG.PING });
  }
};

const join = (port, data) => {
  ports.set(port, { activeId: data.activeId ?? null, incognito: data.incognito || null, missed: 0 });
  pinger ??= setInterval(sweep, PING_MS);
  post(port, tabcountMsg());
  broadcast(tabcountMsg());
  broadcast(peersMsg());
  sendIncognitos();
};

self.onconnect = e => {
  const port = e.ports[0];
  port.start();

  port.onmessage = ev => {
    const data = ev.data || {};
    const type = data.type;
    const known = ports.get(port);
    if (known) known.missed = 0;
    // HELLO from a tab back from the bfcache, PONG from one thawed after its port was dropped:
    // either rejoins with the state it carries. Any other word from a dropped port asks for that.
    if (type === MSG.HELLO || type === MSG.PONG) {
      if (!known) return join(port, data);
      if (type === MSG.PONG) return;
      post(port, tabcountMsg());
      broadcast(tabcountMsg());
      broadcast(peersMsg());
      sendIncognitos();
      return;
    }
    if (!known && type !== MSG.BYE) post(port, { type: MSG.PING });
    if (type === MSG.ACTIVE) {
      const state = ports.get(port);
      if (state) state.activeId = data.activeId ?? null;
      broadcast(peersMsg());
      return;
    }
    if (type === MSG.INCOGNITO) {
      const state = ports.get(port);
      if (state) state.incognito = data.session || null;
      sendIncognitos();
      return;
    }
    // Relay (with its id/action detail) to OTHER tabs so they re-render their
    // lists and, if they hold the same project, sync their editor.
    if (type === MSG.PROJECTS_CHANGED) return broadcast(data, port);
    // Relay an accent change so every other tab repaints its UI live.
    if (type === MSG.ACCENT) return broadcast(data, port);
    if (type === MSG.BYE) return drop(port);
  };

  // Fired when a tab navigates away/closes (browser support varies).
  port.onmessageerror = () => drop(port);
};
