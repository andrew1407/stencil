// Shared rig for the "Make a copy" specs: a REAL ProjectsStore over memory storage under a
// ProjectTransferController whose tabs, host, chat persistence and server are recording stubs.
import { ProjectsStore } from '../../js/core/project/store/projectsStore.js';
import { ProjectTransferController } from '../../js/core/project/transferController.js';
import { createMemoryStorage } from './memoryStorage.js';

export const IMG = 'data:image/png;base64,iVBORw0KGgo=';
export const THUMB = 'data:image/jpeg;base64,/9j/4AAQ';
export const LAYOUT = Object.freeze({
  imageWidth: 40, imageHeight: 30, lines: [{ points: [{ x: 1, y: 2 }, { x: 3, y: 4 }] }],
  cropRect: { x: 1, y: 1, w: 40, h: 30 }, rotationQuarters: 1, imageFilter: 'sepia', filterColor: '#7c3aed',
  pageSize: 'A4', formulaX: 'x', allowFormulas: true, imageBaseName: 'cat', imageExt: 'png',
  imageSource: 'https://img.example/cat.png', imageResource: 'https://page.example/',
});
export const META = Object.freeze({
  id: 'p1', name: 'Cat', color: '#112233', description: 'a cat', keywords: ['pet'],
  expiresAt: 0, refreshPeriod: 'month', autoRefresh: false, hasImage: true, imageW: 40, imageH: 30,
  source: 'https://img.example/cat.png', resource: 'https://page.example/', thumbnail: THUMB,
});

export const makeServer = (url = 'https://srv.example', names = []) => {
  const conn = {
    url, calls: [],
    listProjects: async () => names.map((name, i) => ({ id: `s${i}`, name })),
    createProject: async (body) => { conn.calls.push(['createProject', body]); return { id: 'r9', version: 1 }; },
    putFile: async (id, kind, bytes, meta) => { conn.calls.push(['putFile', id, kind, meta, bytes]); },
    getProject: async (id) => ({ project: { id, version: 2 } }),
    updateProject: async (id, body) => { conn.calls.push(['updateProject', id, body]); return { id, version: 3 }; },
  };
  return conn;
};

export const makeCopyRig = ({ metas = [{ ...META }], live = null, active = null, conn = makeServer() } = {}) => {
  const calls = [];
  const store = new ProjectsStore(createMemoryStorage());
  for (const m of metas) store.upsert({ ...m }, { image: IMG, layout: { ...LAYOUT } });
  const storage = { store, temporary: false, incognito: false, imageReady: Promise.resolve(),
    save: () => calls.push(['save']), loadProject: (id) => { calls.push(['loadProject', id]); return true; } };
  const host = {
    activeProjectId: active, remoteLink: null,
    chatPersistence: { projectCopied: async (from, to) => { calls.push(['chat', from, to]); return true; }, projectOpened() {} },
    updateProjectTitle() {}, updateIncognitoUI: () => calls.push(['incognitoUI']),
    newEditor: () => calls.push(['newEditor']),
    loadImageFromFile: (file, opts) => calls.push(['load', file.name, opts]),
    liveProject: () => live,
  };
  const tabs = { projectsChanged: (d) => calls.push(['changed', d]), reportActive() {} };
  const remoteSync = { fetchRemoteOriginal: async () => new Blob([new Uint8Array([1])], { type: 'image/png' }) };
  const ctrl = new ProjectTransferController({
    storage, tabs, remoteSync, host,
    getConnections: () => ({ get: (u) => (u === conn.url ? conn : null) }),
  });
  ctrl.openProjectInNewTab = (id, win) => calls.push(['newtab', id, win]);
  ctrl.openLaunchInNewTab = (payload, win) => calls.push(['launch', payload, win]);
  ctrl.openRemoteProject = async (m) => calls.push(['openRemote', m]);
  ctrl.openRemoteProjectInNewTab = (m, win) => calls.push(['remoteNewtab', m, win]);
  return { ctrl, calls, store, storage, host, conn };
};
