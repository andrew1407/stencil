// ── Opt-in per-project chat persistence (llm-contract.md §12) ───────────
// Glues the shared conversation to the IndexedDB chat store and, for server-linked
// projects, the server's `chat` file kind. OFF until the user enables "Save chats with
// projects"; temporary/incognito editors NEVER persist. Transcript changes snapshot
// SYNCHRONOUSLY (keyed by the active project id at that instant — no debounce, so a
// project switch can never misfile a conversation) and write best-effort; an emptied
// transcript deletes the persisted copy, and switches restore into BOTH the controller
// replay history and the visible transcript.
import {
  chatLog, onChatLog, appendChatRow, clearChatLog, sharedChatController, peekChatController,
} from './chatSession.js';
import { loadLlmSettings } from './llmSettings.js';
import { createChatStore, buildChatDoc, rowsToMessages, parseChatDoc, sanitizeChatMessages } from './chatStore.js';

export const createChatPersistence = ({
  app,
  store = createChatStore(),
  getSettings = loadLlmSettings,
  now = Date.now,
} = {}) => {
  let muted = 0;          // >0 while WE mutate the log (restore/clear) — no write-back
  let writes = Promise.resolve();   // serialized writes; awaited by the test seam

  const enabled = () => getSettings().saveChats === true;

  // The §12.2 persistability gate for the ACTIVE editor.
  const activePersistableId = () => {
    const s = app.storage;
    if (!s || s.temporary || s.incognito || s.activeId == null) return null;
    return s.activeId;
  };

  // Server linkage for a project id (meta carries address/remoteId for linked rows).
  const serverFor = (id) => {
    const meta = app.storage?.store?.getMeta?.(id);
    if (!meta || !meta.address || !meta.remoteId) return null;
    const conn = app.connections?.get?.(meta.address);
    return conn ? { conn, remoteId: meta.remoteId } : null;
  };

  const pushServer = async (id, doc) => {
    const link = serverFor(id);
    if (!link) return;
    try { await link.conn.putFile(link.remoteId, 'chat', JSON.stringify(doc), { ext: 'json' }); }
    catch { /* best-effort — the local copy is the source of truth for this tab */ }
  };
  const deleteServer = async (id) => {
    const link = serverFor(id);
    if (!link) return;
    try { await link.conn.deleteFile(link.remoteId, 'chat'); }
    catch { /* best-effort */ }
  };
  const fetchServer = async (id) => {
    const link = serverFor(id);
    if (!link) return null;
    try {
      const blob = await link.conn.fetchFile(link.remoteId, 'chat');
      return parseChatDoc(await blob.text());
    } catch { return null; }   // 404 = no saved chat, network errors degrade the same
  };

  // Snapshot NOW, write async (serialized so a clear can't lose to a lagging save).
  const persistSnapshot = () => {
    if (muted || !enabled()) return;
    const id = activePersistableId();
    if (id == null) return;
    const messages = rowsToMessages(chatLog());
    writes = writes.then(async () => {
      if (!messages.length) {
        await store.remove(id);
        await deleteServer(id);
        return;
      }
      const doc = buildChatDoc(messages, now());
      await store.save(id, doc);
      await pushServer(id, doc);
    });
  };

  // Swap the conversation to project `id` (null = a fresh temporary/blank editor).
  // Only meaningful when saving is on — with it off, the session-long conversation
  // deliberately survives project switches (the pre-§12 behavior).
  const projectOpened = (id) => {
    if (!enabled()) return Promise.resolve();
    muted++;
    const swap = (async () => {
      const ctrl = id != null ? sharedChatController(app) : peekChatController(app);
      ctrl?.clearConversation();
      clearChatLog();
      if (id == null) return;
      const doc = (await store.load(id)) || (await fetchServer(id));
      if (!doc || !doc.messages?.length) return;
      if (app.storage?.activeId !== id) return;   // switched again mid-load
      // §12.1 at the point of use: the shared document may have been written by a build
      // that serialised its MODEL history — a restored transcript must read as a
      // conversation, never hand the model its own machinery back as user text.
      const messages = sanitizeChatMessages(doc.messages);
      if (!messages.length) return;
      for (const m of messages) appendChatRow({ role: m.role, text: m.text });
      sharedChatController(app).seedHistory(messages);
    })();
    // Always unmute, even if the store throws mid-restore.
    return swap.finally(() => { muted--; });
  };

  const controller = {
    projectOpened,
    // Stored chats are cleaned up with their projects REGARDLESS of the toggle —
    // a project that no longer exists must not leave its conversation behind.
    projectRemoved: (id) => store.remove(id),
    allProjectsCleared: () => store.clear(),
    // Test seam: resolves when every snapshot filed so far has been written.
    flush: () => writes,
  };

  onChatLog(persistSnapshot);
  return controller;
};

// Entry-point wiring: expose the controller to the app and restore the boot project's
// chat. Lands on `app.chatPersistence`, NOT `app.chat` — the panel publishes its
// scripting surface as `app.chat` first; sharing the property would break stencil.prompt().
export const wireChatPersistence = (app, opts = {}) => {
  const chat = createChatPersistence({ app, ...opts });
  app.chatPersistence = chat;
  chat.projectOpened(app.storage?.temporary || app.storage?.incognito ? null : app.storage?.activeId ?? null);
  return chat;
};
