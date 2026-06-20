// ── projectsWorker: SharedWorker tab coordinator (PURE — no storage) ──
// Pure message router between tabs; NEVER touches localStorage (SharedWorker is
// window-less) — persistence stays window-side. State = connected ports + each
// port's active project id, used to report tab count, broadcast peer-open project
// ids, and relay "projects changed" re-render pings.
import { MSG } from './messages.js';

// port -> { activeId }
const ports = new Map();

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
    /* port closed (tab gone) — it'll be pruned on its next BYE / failed ping */
  }
};
const broadcast = (msg, except = null) => {
  for (const port of ports.keys()) {
    if (port === except) continue;
    post(port, msg);
  }
};

const drop = port => {
  if (!ports.has(port)) return;
  ports.delete(port);
  broadcast(tabcountMsg());
  broadcast(peersMsg());
};

self.onconnect = e => {
  const port = e.ports[0];
  ports.set(port, { activeId: null });
  port.start();

  port.onmessage = ev => {
    const data = ev.data || {};
    const type = data.type;
    if (type === MSG.HELLO) {
      // Tell the newcomer the current count, then refresh everyone.
      post(port, tabcountMsg());
      broadcast(tabcountMsg());
      post(port, peersMsg());
      return;
    }
    if (type === MSG.ACTIVE) {
      const state = ports.get(port);
      if (state) state.activeId = data.activeId ?? null;
      broadcast(peersMsg());
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
