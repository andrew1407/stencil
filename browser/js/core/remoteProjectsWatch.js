// Another tab changed the project set: what the editor does when the project it is showing
// is removed, cleared, closed, or saved again by a peer.
import { notify } from '../utils.js';
import { getProjectsBackend } from './projectsBackend.js';
import { PROJECT_ACTION } from '../worker/messages.js';

// Safe to swap content under the user.
const isIdle = (app) =>
  !app.isDrawing && !app.isPanning && !app.isDraggingPoint &&
  !app.isDraggingSegment && !app.isDraggingLine &&
  !app.isZoomRectDragging && !app.isRectDrawDragging;

// Full teardown, not storage.newTemporary(): a bare reset leaves remoteLink pointing at a
// project that no longer exists.
const tearDown = (app, message) => {
  app.newEditor();
  app.updateButtons();
  notify(message, 'info');
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
    refreshed.then(() => {
      if (isIdle(app)) app.storage.syncActiveFromStorage();
// A colour change lives in the registry meta, not the payload syncActiveFromStorage reloads.
      app.updateProjectTitle();
    });
  }
};
