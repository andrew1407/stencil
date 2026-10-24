// Shared rig for the launchQuery*.test.js family: the facade rig booted on a URL, the notices
// and URL rewrites recorded, a decode that lands a picture (promoting a saved load the way
// beginLoad does), and the stand-ins for renames and a connection manager.
import { installFetchStub } from './fetchStub.js';
import { createMemoryStorage, installMemoryStorage } from './memoryStorage.js';
import { installImageDecode } from './projectTransferRig.js';
import { createStencil, makeApp, called, lastCall, blankCanvases } from './stencilApiRig.js';
import { parseLaunchQuery } from '../../js/core/launch/query.js';
import { applyLaunchQuery } from '../../js/console/launchQuery.js';

export { installFetchStub, called, lastCall, blankCanvases, applyLaunchQuery, parseLaunchQuery };

installMemoryStorage();
globalThis.sessionStorage = createMemoryStorage();

export const notices = [];
const getById = document.getElementById;
document.getElementById = (id) => (id === 'notify-balloon' ? { notify: (m, t) => notices.push([m, t]) } : getById(id));
export const replaced = [];
export const texts = () => notices.map(([m]) => m);

// A booted editor whose URL carried `search` and `hash`; `prepare` touches the app before the facade builds.
export const boot = (search, hash = '', over = {}, prepare = () => {}) => {
  notices.length = 0;
  replaced.length = 0;
  globalThis.location = { pathname: '/app', search, hash };
  globalThis.history = { replaceState: (...a) => replaced.push(a) };
  const app = makeApp(over);
  app.launchQuery = parseLaunchQuery(search, hash);
  app.pendingLaunchScript = '';
  app.accents.setThemeMode = (m) => app.calls.push(['setThemeMode', m]);
  prepare(app);
  const stencil = createStencil(app);
  return { app, stencil };
};

// The decode lands a picture; a saved (non-incognito) load is promoted the way beginLoad does.
// `settle` runs the real load bookkeeping too (the provenance, the name), as stencilApiLoad does.
export const decodeTo = (app, { settle = false } = {}) => installImageDecode((file, img) => {
  app.calls.push(['decoded', file.name]);
  app.image = { width: 4, height: 3 };
  if (!app.storage.incognito) {
    app.activeProjectId = 'p1';
    app._metas.push({ id: 'p1', name: file.name.replace(/\.\w+$/, '') });
  }
  if (settle) img.onload();
});

export const renames = { renameProject(id, name) { this.calls.push(['renameProject', id, name]); return true; } };

// A connection manager that knows (or not) every origin, records connects and lists `projects`.
export const connected = (known, projects = []) => (app) => {
  let has = known;
  app.connections = {
    has: () => has,
    connect: async (spec) => { has = true; app.calls.push(['connect', spec]); },
    remoteProjects: async () => projects,
  };
};
