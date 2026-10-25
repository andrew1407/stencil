// js/core/storage/save.js: a zoom or scroll save reuses the lines' JSON the last save wrote, spliced
// in by writeJSON (projectRegistryIo.js) byte for byte; any edit — a new undo step, a new lines array,
// another project — serialises them afresh.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mountStorage } from '../../helpers/storageRig.js';
import { createMemoryStorage } from '../../helpers/memoryStorage.js';
import { createProjectsBackend } from '../../../js/core/project/store/projectsBackend.js';
import { ProjectsStore, PROJECT_PREFIX } from '../../../js/core/project/store/projectsStore.js';
import { rawJson, writeJSON } from '../../../js/core/project/store/projectRegistryIo.js';
import { HistoryStack } from '../../../js/core/historyStack.js';

test('spliced text writes the bytes of the value it was taken from, whatever the strings around it hold', () => {
  const lines = [{ points: [{ x: 1.5, y: -2 }], label: 'a "quoted"   line' }];
  const around = { zoom: 2, note: '\u0000raw:not-the-mark', lines, tail: [null, 'x'] };
  const kv = createMemoryStorage();
  writeJSON(kv, 'k', { ...around, lines: rawJson(JSON.stringify(lines)) });
  assert.equal(kv.getItem('k'), JSON.stringify(around));
  writeJSON(kv, 'plain', around);
  assert.equal(kv.getItem('plain'), JSON.stringify(around));
});

const rig = async (t) => {
  const kv = { async get() {}, async set() {}, async remove() {}, async entries() { return []; } };
  const backend = await createProjectsBackend({ storage: createMemoryStorage(), kv });
  const r = mountStorage(t, { app: { history: new HistoryStack(), lines: [{ points: [{ x: 1, y: 2 }] }] } });
  Object.assign(r.storage, { backend, store: new ProjectsStore(backend), activeId: 'p1', temporary: false });
  r.app.activeProjectId = 'p1';
  const stored = () => JSON.parse(backend.getItem(PROJECT_PREFIX + 'p1')).layout;
  return { ...r, stored };
};

test('a view save writes the new zoom over the lines the last save wrote', async (t) => {
  const { app, storage, stored } = await rig(t);
  let serialised = 0;
  Object.defineProperty(app.lines[0], 'toJSON', { value() { serialised++; return { points: this.points }; } });
  storage.save();
  app.scale = 4;
  storage.save({ viewOnly: true });
  assert.equal(stored().zoom, 4);
  assert.deepEqual(stored().lines, [{ points: [{ x: 1, y: 2 }] }]);
  assert.equal(serialised, 1, 'the view save took the lines\' text as it was');
});

test('an edit is never hidden by a view save: a new undo step or lines array is serialised afresh', async (t) => {
  const { app, storage, stored } = await rig(t);
  storage.save();
  app.lines[0].points.push({ x: 3, y: 4 });
  app.history.push(app.lines);
  storage.save({ viewOnly: true });
  assert.deepEqual(stored().lines[0].points, [{ x: 1, y: 2 }, { x: 3, y: 4 }]);
  app.lines = [];
  storage.save({ viewOnly: true });
  assert.deepEqual(stored().lines, []);
});

test('a full save always serialises the lines, even mutated in place with no undo step', async (t) => {
  const { app, storage, stored } = await rig(t);
  storage.save();
  app.lines[0].points[0].x = 9;
  storage.save();
  assert.equal(stored().lines[0].points[0].x, 9);
});
