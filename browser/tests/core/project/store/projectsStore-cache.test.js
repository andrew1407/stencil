// BR-7: the registry (480px thumbnails and all) is parsed once per stored string, not per read;
// any writer — this store, another tab, the extension — changes the string and so the parse.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ProjectsStore, REGISTRY_KEY } from '../../../../js/core/project/store/projectsStore.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';

const countParses = (t) => {
  const real = JSON.parse;
  const seen = { n: 0 };
  JSON.parse = (...a) => { seen.n++; return real(...a); };
  t.after(() => { JSON.parse = real; });
  return seen;
};
const meta = (id, over = {}) => ({ id, name: id, thumbnail: 'data:image/jpeg;base64,AAAA', updatedAt: 1, ...over });

test('repeated reads of an unchanged registry parse it once', (t) => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert(meta('a'), { image: null, layout: {} });
  const parses = countParses(t);
  for (let i = 0; i < 5; i++) { s.list(); s.getMeta('a'); s.nameExists('a'); }
  assert.equal(parses.n, 0, 'fifteen reads of this store\'s own save, no parse');
  storage.setItem(REGISTRY_KEY, JSON.stringify([meta('b')]));
  assert.deepEqual(s.list().map((m) => m.id), ['b'], 'another writer\'s string is picked up');
  s.list();
  assert.equal(parses.n, 1, 'parsed once, for the new string');
});

test('a caller mutating what it read cannot corrupt the cache', () => {
  const s = new ProjectsStore(createMemoryStorage());
  s.upsert(meta('a', { keywords: ['x'] }), { image: null, layout: {} });
  const m = s.getMeta('a');
  m.name = 'changed';
  m.keywords.push('y');
  s.list()[0].name = 'also changed';
  assert.equal(s.getMeta('a').name, 'a');
  assert.deepEqual(s.getMeta('a').keywords, ['x']);
});

test('patches still write through and read back', () => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert(meta('a'), { image: null, layout: {} });
  s.rename('a', 'renamed');
  assert.equal(s.getMeta('a').name, 'renamed');
  assert.equal(JSON.parse(storage.getItem(REGISTRY_KEY))[0].name, 'renamed');
  s.clearAll();
  assert.deepEqual(s.list(), []);
});

test('a save caches what it wrote: the reads after it parse nothing and match a fresh parse', (t) => {
  const storage = createMemoryStorage();
  const s = new ProjectsStore(storage);
  s.upsert(meta('a', { remoteId: undefined, tags: [1, undefined] }), { image: null, layout: {} });
  s.list();
  const parses = countParses(t);
  s.upsert(meta('b', { color: '#ff0000' }), { image: null, layout: {} });
  s.rename('a', 'renamed');
  s.setKeywords('b', ['k']);
  const read = [s.list(), s.getMeta('a'), s.getMeta('b')];
  assert.equal(parses.n, 0, 'three saves and their reads, no parse');
  const fresh = new ProjectsStore(storage);
  assert.deepEqual(read, [fresh.list(), fresh.getMeta('a'), fresh.getMeta('b')]);
  assert.equal('remoteId' in s.getMeta('a'), false, 'an undefined field is dropped, as JSON drops it');
});
