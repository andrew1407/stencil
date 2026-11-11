// core/project/share/read.js: each Copy entry reads only what it hands on — a stored layout never
// resolves an image, a server row's layout is one record read, an original is one file.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readShareLayout, readShareRenderSource } from '../../../js/core/project/share/read.js';

const layout = { imageWidth: 4, imageHeight: 3, lines: [{ points: [[0, 0], [1, 1]], color: '#f00' }] };

const rig = () => {
  const calls = [];
  const conn = {
    getProject: async (id) => { calls.push(['getProject', id]); return { project: { name: 'S' }, layout }; },
    fetchFile: async (id, kind) => { calls.push(['fetchFile', id, kind]); return new Blob(['r'], { type: 'image/png' }); },
  };
  const c = {
    getConnections: () => ({ get: (url) => (url === 'https://srv.example' ? conn : null) }),
    remoteSync: { fetchRemoteOriginal: async (_conn, id) => { calls.push(['original', id]); return new Blob(['o'], { type: 'image/jpeg' }); } },
    storage: { store: {
      get: (id) => { calls.push(['get', id]); return id === 'p1' ? { meta: { name: 'P' }, payload: { image: 'stencil_image_p1', layout } } : null; },
      resolveImage: async (id) => { calls.push(['resolveImage', id]); return 'data:image/png;base64,iVBORw=='; },
    } },
  };
  return { c, calls };
};

test('a stored row\'s layout is read from the registry alone, in the download shape', async () => {
  const { c, calls } = rig();
  const out = await readShareLayout(c, { id: 'p1' });
  assert.deepEqual(out.lines, layout.lines);
  assert.deepEqual(calls, [['get', 'p1']], 'no image is resolved for a layout');
  await assert.rejects(readShareLayout(c, { id: 'nope' }), /Project not found/);
});

test('a server row\'s layout is one record read; a picture of it is that record and its original', async () => {
  const { c, calls } = rig();
  const remote = { id: 'r1', serverUrl: 'https://srv.example' };
  assert.deepEqual((await readShareLayout(c, { remote })).lines, layout.lines);
  const src = await readShareRenderSource(c, { remote });
  assert.equal(src.blob.type, 'image/jpeg');
  assert.deepEqual(src.layout, layout);
  assert.deepEqual(calls, [['getProject', 'r1'], ['getProject', 'r1'], ['original', 'r1']]);
  await assert.rejects(readShareLayout(c, { remote: { id: 'x', serverUrl: 'https://other.example' } }), /Not connected/);
});

test('a stored row draws from the bytes its data URL holds, under its stored layout', async () => {
  const { c } = rig();
  const { blob, layout: stored } = await readShareRenderSource(c, { id: 'p1' });
  assert.deepEqual(stored, layout);
  assert.equal(blob.type, 'image/png');
  assert.deepEqual([...new Uint8Array(await blob.arrayBuffer())], [0x89, 0x50, 0x4e, 0x47]);
  c.storage.store.resolveImage = async () => 'stencil_image_p1';
  await assert.rejects(readShareRenderSource(c, { id: 'p1' }), /no image/, 'an image key that resolves to no data URL');
});
