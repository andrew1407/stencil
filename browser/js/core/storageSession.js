// The active-project session over ProjectsStore: attaching the store, the boot sweep, the
// refresh-on-open snap, promoting a temporary editor, and clearing the editor.
import { ProjectsStore, addPeriod, DEFAULT_PERIOD } from './projectsStore.js';
import { getProjectsBackend } from './projectsBackend.js';

// Payload keys ride an IndexedDB mirror, the registry stays in localStorage
// (projectsBackend.js); a failed async persist surfaces on the save-status line.
export const attachProjectsStore = (storage) => {
  const backend = getProjectsBackend();
  const store = new ProjectsStore(backend);
  if (backend && 'onWriteError' in backend) {
    backend.onWriteError = () =>
      storage.app.showSaveStatus('Save failed (browser storage error)', 'var(--danger)', 'x');
  }
  return store;
};

// Boot-time migration + expiry sweep. Does NOT auto-load a project.
export const restoreProjects = (storage) => {
  const now = Date.now();
  try {
    storage.store.migrateLegacy(now);
    storage.store.sweepExpired(now);
  } catch (e) {
    console.warn('Could not initialize project storage:', e);
  }
};

// "Refresh on open": expiresAt = now + its refresh period; keep-forever (0) is left alone.
export const autoRefreshOnOpen = (storage, id) => {
  const meta = storage.store.getMeta(id);
  if (!meta || !meta.autoRefresh || !meta.expiresAt) return;
  const period = meta.refreshPeriod || DEFAULT_PERIOD;
  storage.store.setExpiration(id, { expiresAt: addPeriod(Date.now(), period), refreshPeriod: period });
  storage.scheduleSyncBroadcast();
};

// A temp editor received its first image → a real project. The caller then calls save().
export const promoteTemporary = (storage) => {
  storage.activeId = storage.store.createId();
  storage.temporary = false;
  storage.app.activeProjectId = storage.activeId;
  return storage.activeId;
};

// Everything the emptied editor forgets; the canvas itself is the caller's to collapse.
export const clearEditorState = (app) => {
  app.image = null;
  app.originalImage = null;
  app.cropRect = null;
  app.imageDataUrl = null;
  app.imageBaseName = null;
  app.imageExt = null;
  app.imageSource = null;
  app.imageResource = null;
  app.lines = [];
  app.currentLine = null;
  app.selectedLineIdx = -1;
  app.coordLineIdx = -1;
  app.focusedPtIdx = -1;
  app.pendingLines = null;
  app.pendingImageSize = null;
  app.history.reset([], -1);
};
