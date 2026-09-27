// What re-lists the projects modal from outside it: a connections change, another tab's project
// or peer news, and the one-time open when the page starts alone with saved projects.
import { subscribe, EVENTS } from '../../../eventBus/appBus.js';
import { getProjectsBackend } from '../../../core/project/store/projectsBackend.js';

export function wireLiveRefresh({ app, store, mayRefresh, render, invalidateRemotes, setPeers,
  setIncognitoPeers, open }) {
  subscribe(EVENTS.connectionsChanged, () => {
    // The cached listing is stale; the next render re-fetches (never mid-drag or mid-removal).
    invalidateRemotes();
    if (mayRefresh()) render();
  });

  // Never mid-drag or mid-removal — the deferred render shows the recorded state. A named project is
  // read again from IndexedDB, then its row re-rendered: a peer's thumbnail is in no local mirror.
  app.tabs.onProjectsChanged((detail) => {
    if (mayRefresh()) render();
    if (detail?.id == null) return;
    Promise.resolve(getProjectsBackend()?.refresh?.(detail.id)).then(() => { if (mayRefresh()) render(); });
  });
  app.tabs.onPeers(ids => {
    setPeers(ids || []);
    if (mayRefresh()) render();
  });
  app.tabs.onIncognitoPeers(list => {
    setIncognitoPeers(list || []);
    if (mayRefresh()) render();
  });

  // The count the page OPENED with: one saved while the handshake settles is work in progress.
  const openedWithSaved = store.list().length > 0;
  app.tabs.whenReady().then(({ youAreOnly }) => {
    // open(null): the page opened it, not the toolbar icon, so it drops in from above.
    if (youAreOnly && openedWithSaved && !app.pendingOpenProjectId && !app.hasExternalLaunch) open(null);
  });
}
