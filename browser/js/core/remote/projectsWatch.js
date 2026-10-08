// Another tab changed the project set: what the editor does when the project it is showing
// is removed, cleared, closed, or saved again by a peer.
import { notify } from '../../utils.js';
import { getProjectsBackend } from '../project/store/projectsBackend.js';
import { PROJECT_ACTION } from '../../worker/messages.js';
import { canvasGestureActive } from '../pointer/gesture.js';
import { newEditor } from '../launch/openFlow.js';

// Safe to swap content under the user.
const isIdle = (app) => !app.isDrawing && !canvasGestureActive(app);

// Full teardown, not storage.newTemporary(): a bare reset leaves remoteLink pointing at a
// project that no longer exists.
const tearDown = (app, message) => {
  newEditor(app);
  app.updateButtons();
  notify(message, 'info');
};

// ms between idle checks while a peer's save of the open project waits out a gesture.
const DEFER_MS = 120;
const deferred = new WeakSet();
const top = (app) => app.history?.history?.[app.history.historyStep];

// A gesture defers the peer's save, never drops it. An edit the gesture committed meanwhile is
// saved after it and wins (cross-tab last-writer-wins); otherwise the peer's save is adopted.
const adoptPeerSave = (app, id, since = null) => {
  if (id !== app.activeProjectId) return;
  if (!isIdle(app)) {
    if (deferred.has(app)) return;
    deferred.add(app);
    const from = since ?? { step: top(app) };
    setTimeout(() => {
      deferred.delete(app);
      Promise.resolve(getProjectsBackend()?.refresh?.(id)).then(() => adoptPeerSave(app, id, from));
    }, DEFER_MS);
    return;
  }
  if (!since || top(app) === since.step) app.storage.syncActiveFromStorage();
// A colour change lives in the registry meta, not the payload syncActiveFromStorage reloads.
  app.updateProjectTitle();
};

export const onRemoteProjectsChange = (app, detail) => {
  const { id, action } = detail;
// Payloads live in a per-tab IndexedDB mirror (projectsBackend.js): pull the changed one in
// now so the sync below reads the peer's bytes, not this tab's stale copy.
  const refreshed = Promise.resolve(id != null ? getProjectsBackend()?.refresh?.(id) : null);
  if (action === PROJECT_ACTION.REMOVED && id === app.activeProjectId) {
    tearDown(app, 'This project was removed in another tab');
    return;
  }
  if (action === PROJECT_ACTION.CLEARED && app.activeProjectId != null) {
    tearDown(app, 'All projects were cleared in another tab');
    return;
  }
  if (action === PROJECT_ACTION.CLOSE && id === app.activeProjectId) {
    tearDown(app, 'This project was closed from another tab');
    return;
  }
  if (action === PROJECT_ACTION.UPDATED && id === app.activeProjectId) {
    refreshed.then(() => adoptPeerSave(app, id));
  }
};
