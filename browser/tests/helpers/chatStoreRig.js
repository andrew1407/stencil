// The chat store double behind tests/llm/chat/chatPersistence*.test.js: an async Map, seeded or
// empty, that records every write so a case can assert the muted-restore rule (a restore never
// writes back what it just read).

export const makeStore = (seed = {}) => {
  const m = new Map(Object.entries(seed));
  const calls = { save: [], remove: [], clear: 0 };
  return {
    _map: m,
    calls,
    async load(id) { return m.get(String(id)) || null; },
    async save(id, doc) { calls.save.push(String(id)); m.set(String(id), doc); return true; },
    async remove(id) { calls.remove.push(String(id)); m.delete(String(id)); },
    async clear() { calls.clear++; m.clear(); },
  };
};
