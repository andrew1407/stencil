// ── projectsWorker: SharedWorker tab coordinator (PURE — no storage) ──
// This worker is a pure message router between browser tabs. It NEVER touches
// localStorage (a SharedWorker is window-less and has no DOM); all persistence
// stays window-side in ProjectsStore/Storage. Its only state is the set of
// connected ports and each port's currently-active project id, so it can:
//   • report the live tab count (and whether a tab is the only one), and
//   • broadcast which project ids are open in other tabs (peers), and
//   • relay "projects changed" pings so other tabs re-render their lists.
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

const post = (port, msg) => { try { port.postMessage(msg); } catch {} };
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
    if (type === MSG.BYE) return drop(port);
  };

  // Fired when a tab navigates away/closes (browser support varies).
  port.onmessageerror = () => drop(port);
};
