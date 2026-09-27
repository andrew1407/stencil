// Shared rig for the projectTransferController specs: a recording server connection and a
// projects-store / storage / tabs / host stand-in, plus the FileReader and Image decode Node lacks.
import { ProjectTransferController } from '../../js/core/project/transferController.js';

export const FAKE_DATA_URL = 'data:image/png;base64,ZmFrZQ==';
// `reads` lists every blob handed to it, in order; onload gets the event the load flow reads.
export class FakeFileReader {
  static reads = [];
  readAsDataURL(blob) {
    FakeFileReader.reads.push(blob);
    this.result = FAKE_DATA_URL;
    queueMicrotask(() => this.onload && this.onload({ target: this }));
  }
}

// The decode boundary of the real load flow (core/image/loadFlow.js): the file is read, and the
// original "decodes" by `onDecode(file, img)` — which stands in for settle.js (its own specs),
// or calls `img.onload()` to run it.
export const installImageDecode = (onDecode) => {
  const saved = { FileReader: globalThis.FileReader, Image: globalThis.Image };
  FakeFileReader.reads = [];
  globalThis.FileReader = FakeFileReader;
  globalThis.Image = class {
    set src(v) { this._src = v; queueMicrotask(() => onDecode(FakeFileReader.reads.at(-1), this)); }
    get src() { return this._src; }
  };
  return {
    reads: () => FakeFileReader.reads.map((f) => f?.name),
    restore: () => Object.assign(globalThis, saved),
  };
};

// A recording server connection covering the REST surface the transfer flows touch.
export const makeConn = (url = 'https://srv.example', over = {}) => {
  const conn = {
    url,
    calls: [],
    createProject: async (fields) => { conn.calls.push(['createProject', fields]); return { id: 'r1', version: 1 }; },
    putFile: async (id, kind, bytes, meta) => { conn.calls.push(['putFile', id, kind, bytes, meta]); },
    updateProject: async (id, fields) => { conn.calls.push(['updateProject', id, fields]); return { id, version: (fields.version || 0) + 1 }; },
    getProject: async (id) => { conn.calls.push(['getProject', id]); return { project: { id, name: 'Remote', version: 7, source: '', color: '#112233' }, layout: { imageWidth: 4, imageHeight: 3, lines: [] } }; },
    deleteProject: async (id) => { conn.calls.push(['deleteProject', id]); },
    ...over,
  };
  return conn;
};

// A recording projects-store + storage + tabs + host rig with just what the flows read.
export const makeRig = ({ conn, meta = {}, payload = {} } = {}) => {
  const calls = [];
  const store = {
    get: (id) => ({ id, payload }),
    getMeta: () => ({ id: 'p1', name: 'Local', ...meta }),
    upsert: (m, p) => { calls.push(['upsert', m, p]); },
    createId: () => 'new-local',
    remove: (id) => { calls.push(['remove', id]); },
    list: () => [],
  };
  const storage = {
    temporary: false,
    incognito: false,
    store,
    save: () => { calls.push(['storage.save']); },
    newTemporary: () => { calls.push(['storage.newTemporary']); },
    loadProject: (id) => { calls.push(['storage.loadProject', id]); return true; },
  };
  const tabs = {
    projectsChanged: (d) => { calls.push(['projectsChanged', d]); },
    reportActive: (id) => { calls.push(['reportActive', id]); },
  };
  const remoteSync = {
    fetchRemoteOriginal: async () => new Blob([new Uint8Array([9])], { type: 'image/png' }),
    reloadRemoteActive: () => { calls.push(['reloadRemoteActive']); },
  };
  const host = {
    activeProjectId: null,
    remoteLink: null,
    blankColor: '',
    imageBaseName: '',
    chatPersistence: null,
    updateProjectTitle: () => { calls.push(['updateProjectTitle']); },
    updateIncognitoUI: () => { calls.push(['updateIncognitoUI']); },
    newEditor: () => { calls.push(['newEditor']); },
    loadImageFromFile: (file, opts) => { calls.push(['loadImageFromFile', file, opts]); },
    setBlankColor: (c) => { calls.push(['setBlankColor', c]); },
  };
  const ctrl = new ProjectTransferController({
    storage, tabs, remoteSync,
    getConnections: () => ({ get: (addr) => (addr === conn.url ? conn : null) }),
    host,
  });
  return { ctrl, calls, store, storage, tabs, host };
};
