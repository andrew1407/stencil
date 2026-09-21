// ── Per-project chat persistence: the stored document + the IndexedDB store ──
// llm-contract.md §12: an OPT-IN, text-only chat transcript saved per project. IndexedDB,
// not localStorage, to stay out of the projects' quota budget; every operation is
// best-effort. The backend is injected so `node --test` drives it with an async Map shim.

import PROMPT_ASSET from '../../config/llm/systemPrompt.json' with { type: 'json' };

const CHAT_DB_NAME = 'stencil_chats';
const CHAT_DB_STORE = 'chats';
export const CHAT_DOC_VERSION = 1;
// §7's history bound — the persisted transcript never exceeds the replay window.
export const CHAT_MESSAGE_LIMIT = 32;

// The text is the shared prose asset's, re-exported beside the persistence rules so the place
// that must never store it and the place that writes it agree by construction.
export const CONTINUATION_NOTE = PROMPT_ASSET.continuationNote;

// §12.1: machinery is refused on BOTH sides of the store — §7's continuation note (any
// bracketed variant) and a raw op-plan as the ASSISTANT turn; a user may still paste JSON.
export const isInternalChatText = (role, text) => {
  const t = String(text ?? '').trim();
  if (!t) return false;
  if (t === CONTINUATION_NOTE) return true;
  if (/^\[The working image is now\b[\s\S]*\]$/.test(t)) return true;
  if (role !== 'assistant') return false;
  return /^[{[]/.test(t) && /"version"\s*:/.test(t) && /"(actions|reply|variants|ask)"\s*:/.test(t);
};

// One valid persisted message or null. Images are NEVER persisted (§12.1): a
// stray `images` field on a stored message is dropped on read, not written back.
const cleanMessage = (m) => {
  if (!m || (m.role !== 'user' && m.role !== 'assistant')) return null;
  if (typeof m.text !== 'string') return null;
  if (isInternalChatText(m.role, m.text)) return null;
  return { role: m.role, text: m.text };
};

// The §12.1 gate, applied wherever a document is about to be BELIEVED: the restore sanitises
// at the point of use too, so an unparsed object from a backend cannot bypass it.
export const sanitizeChatMessages = (messages) => {
  const out = [];
  for (const m of messages || []) {
    const clean = cleanMessage(m);
    if (clean && clean.text) out.push(clean);
  }
  return out.slice(-CHAT_MESSAGE_LIMIT);
};

// Build the §12.1 document from [{role, text}] turns (already display-text form).
export const buildChatDoc = (messages, now = Date.now()) => {
  const out = [];
  for (const m of messages || []) {
    const clean = cleanMessage(m);
    if (clean && clean.text) out.push(clean);
  }
  return { version: CHAT_DOC_VERSION, savedAt: now, messages: out.slice(-CHAT_MESSAGE_LIMIT) };
};

// Parse a stored document (object or JSON string) → {version, savedAt, messages}
// or null. Per §12.1 an unknown version means "treated as missing", never an error.
export const parseChatDoc = (raw) => {
  let doc = raw;
  if (typeof raw === 'string') {
    try { doc = JSON.parse(raw); } catch { return null; }
  }
  if (!doc || typeof doc !== 'object' || doc.version !== CHAT_DOC_VERSION || !Array.isArray(doc.messages)) return null;
  const messages = [];
  for (const m of doc.messages) {
    const clean = cleanMessage(m);
    if (clean) messages.push(clean);
  }
  return { version: CHAT_DOC_VERSION, savedAt: Number(doc.savedAt) || 0, messages: messages.slice(-CHAT_MESSAGE_LIMIT) };
};

// Only settled user/assistant text survives: no pending "…" row, no error or config cards —
// an error is not part of the conversation the model is replayed.
export const rowsToMessages = (rows) => {
  const out = [];
  for (const r of rows || []) {
    if (r?.error || r?.card) continue;
    if (r?.text === '…') continue;   // an in-flight turn is not a message yet
    const clean = cleanMessage(r);
    if (clean && clean.text) out.push(clean);
  }
  return out;
};

// Returns null when IndexedDB is missing (Node) so createChatStore degrades to
// no-persistence instead of throwing; private-mode quirks land in the same catch-all.
export const createIdbBackend = (idb = (typeof indexedDB !== 'undefined' ? indexedDB : null)) => {
  if (!idb) return null;
  let dbPromise = null;
  const openDb = () => new Promise((resolve, reject) => {
    const req = idb.open(CHAT_DB_NAME, 1);
    req.onupgradeneeded = () => { req.result.createObjectStore(CHAT_DB_STORE); };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const db = () => (dbPromise ||= openDb());
  const op = (mode, run) => db().then((d) => new Promise((resolve, reject) => {
    const tx = d.transaction(CHAT_DB_STORE, mode);
    const req = run(tx.objectStore(CHAT_DB_STORE));
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  }));
  return {
    get: (key) => op('readonly', (s) => s.get(key)),
    set: (key, value) => op('readwrite', (s) => s.put(value, key)),
    remove: (key) => op('readwrite', (s) => s.delete(key)),
    clear: () => op('readwrite', (s) => s.clear()),
  };
};

// Every call is best-effort — a broken or absent backend yields null/no-op — and the document
// is validated on the way out, so corrupt bytes read as "no saved chat".
export const createChatStore = (backend = createIdbBackend()) => ({
  async load(projectId) {
    if (!backend || projectId == null) return null;
    try { return parseChatDoc(await backend.get(String(projectId))); }
    catch { return null; }
  },
  async save(projectId, doc) {
    if (!backend || projectId == null || !doc) return false;
    try { await backend.set(String(projectId), doc); return true; }
    catch { return false; }
  },
  async remove(projectId) {
    if (!backend || projectId == null) return;
    try { await backend.remove(String(projectId)); } catch { /* best-effort */ }
  },
  async clear() {
    if (!backend) return;
    try { await backend.clear(); } catch { /* best-effort */ }
  },
});
