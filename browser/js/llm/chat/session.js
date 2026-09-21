// ── The app's ONE assistant conversation (llm-contract.md §7) ───────────
// The chat panel and the context-menu chat are two views of the SAME conversation: both go
// through the single controller this module memoizes per app, so history, attachments and
// the working-video binding stay continuous. Nothing here touches the DOM.
import { createChatController, MAX_ATTACHMENTS, CHAT_ATTACHMENTS_EVENT } from './controller.js';
import { createLlmClient } from '../client.js';
import { loadLlmSettings, serverBearerToken } from '../settings.js';
import { describeChatError, settledReplyText } from './reply.js';
import UI_STRINGS from '../../config/uiStrings.json' with { type: 'json' };
import { publish } from '../../eventBus/appBus.js';
import { mediaAdapters } from '../adapters/media.js';
import { projectAdapters } from '../adapters/project.js';
import { dialogAdapters } from '../adapters/dialog.js';
import { editorAdapters } from '../adapters/editor.js';
// Re-exported: both were found here first, and the adapters are the other caller.
export { uniqueProjectName, resolveProjectByName } from '../projectNames.js';
export { replyWithWarnings, EMPTY_REPLY_TEXT, settledReplyText, unreachableText, describeChatError }
  from './reply.js';

const CONTROLLERS = new WeakMap();

// The controller for `app`, created on FIRST USE so it captures the frozen window.stencil
// facade. `create` is a test seam; the first caller wins, every later caller shares.
export const sharedChatController = (app, { create = createChatController } = {}) => {
  let controller = CONTROLLERS.get(app);
  if (!controller) {
    controller = create({
      stencil: typeof window !== 'undefined' ? window.stencil : undefined,
      getClient: () => createLlmClient({
        settings: loadLlmSettings(),
        getToken: (url) => serverBearerToken(app, url),
      }),
      ...mediaAdapters(app),
      ...projectAdapters(app),
      ...dialogAdapters(app),
      ...editorAdapters(app),
    });
    CONTROLLERS.set(app, controller);
  }
  return controller;
};

// The controller if one already exists — never creates it: a caller that only reads state
// (the panel's attachment row on first paint) must not force it before the facade is frozen.
export const peekChatController = (app) => CONTROLLERS.get(app) || null;

// Test seam: forget the memoized controller for `app`.
export const forgetChatController = (app) => CONTROLLERS.delete(app);

// One conversation ⇒ ONE visible transcript, so a surface opened later renders the history.
// Rows are data only (the DOM lives in ui/view.js); `text` is always rendered as textContent.
const chatRows = [];
const chatRowListeners = new Set();
let chatRowSeq = 0;

export const chatLog = () => chatRows;
// Subscribe to every change; returns an unsubscribe function.
export const onChatLog = (fn) => { chatRowListeners.add(fn); return () => chatRowListeners.delete(fn); };
const emitChatLog = () => { for (const fn of [...chatRowListeners]) fn(chatRows); };

export const appendChatRow = (row) => {
  const entry = { id: ++chatRowSeq, ...row };
  chatRows.push(entry);
  emitChatLog();
  return entry;
};
export const updateChatRow = (id, patch) => {
  const row = chatRows.find((r) => r.id === id);
  if (row) Object.assign(row, patch);
  emitChatLog();
  return row;
};
// A fresh conversation (the panel's Clear) empties the visible transcript everywhere.
export const clearChatLog = () => {
  chatRows.length = 0;
  emitChatLog();
};
// The ONE clear-conversation path every entry point shares: controller state, the visible
// transcript (emptying it deletes the §12 persisted copy), and the composer chips.
export const clearSharedConversation = (app) => {
  peekChatController(app)?.clearConversation();
  clearChatLog();
  publish(CHAT_ATTACHMENTS_EVENT);
};
// Test seam: drop the rows AND the subscribers.
export const resetChatLog = () => {
  chatRows.length = 0; chatRowListeners.clear(); chatRowSeq = 0; turnInFlight = false;
};

// One conversation, one turn: runLoggedChatTurn owns this single flag, so every entry point
// reads the same truth and cannot start a second turn on the same log.
let turnInFlight = false;
export const chatTurnInFlight = () => turnInFlight;

// Each failure is reported separately so one bad file doesn't lose the rest. Files past the
// §7 cap are counted, not thrown — `onCapped` fires ONCE per batch.
export const ATTACHMENT_CAP_NOTICE =
  `Up to ${MAX_ATTACHMENTS} images per message — the extra ones were not attached.`;
export const queueAttachments = async (controller, files, onError, onCapped) => {
  let added = 0, capped = 0;
  for (const file of files) {
    if (controller.attachments.length >= MAX_ATTACHMENTS) { capped++; continue; }
    try { await controller.addAttachment(file); added++; }
    catch (err) { onError?.(err, file); }
  }
  if (capped) onCapped?.(capped);
  return added;
};

// Taken before the send consumes the queue, so the user's own message carries its images. A
// video previews its first extracted frame. Display-only — rows never persist images (§12.1).
export const attachmentPreviews = (controller) =>
  (controller?.attachments || [])
    .map((a) => ({
      name: a.name || (a.kind === 'video' ? 'video' : 'image'),
      kind: a.kind || 'image',
      dataUrl: a.dataUrl || a.frames?.[0] || '',
    }))
    .filter((a) => a.dataUrl);

// Only into an EMPTY queue (anything queued since wins), capped, always as analyze-images
// (a video row's preview is the first frame seen).
export const requeueRowAttachments = (controller, attachments = []) => {
  if (!controller || controller.attachments.length) return 0;
  let n = 0;
  for (const at of attachments) {
    if (controller.attachments.length >= MAX_ATTACHMENTS) break;
    if (!at?.dataUrl) continue;
    controller.attachments.push({ name: at.name || 'image', kind: 'image', use: 'analyze', dataUrl: at.dataUrl });
    n++;
  }
  return n;
};

// Shared provider-status probe: the panel gear and the context-menu gear show the same dot.
// The last probe is cached, keyed by the settings that produced it, with a short TTL.
export const PROBE_TTL_MS = 15_000;
const settingsKey = (s) => [s?.provider, s?.baseUrl, s?.model, s?.serverUrl].join('|');
let probeEntry = null;
export const cacheProbe = (settings, probe) => {
  probeEntry = { key: settingsKey(settings), at: Date.now(), probe };
};
export const cachedProbe = (settings, now = Date.now()) => {
  if (!probeEntry || probeEntry.key !== settingsKey(settings) || now - probeEntry.at > PROBE_TTL_MS) return null;
  return probeEntry.probe;
};
export const forgetProbe = () => { probeEntry = null; };
// probe → the .conn-status class suffix (null/in-flight = amber "connecting"). Pure.
export const probeStatusClass = (probe) => (!probe ? 'connecting' : probe.ok ? 'connected' : 'error');


// Only shapes the result ({ ok, kind, text, entry, error }), so the panel and the
// context-menu chat can't drift apart.
export const runChatTurn = async (controller, text, { signal, settings } = {}) => {
  try {
    const entry = await controller.send(text, { signal });
    return { ok: true, text: settledReplyText(entry), entry };
  } catch (err) {
    return { ok: false, error: err, ...describeChatError(err, settings) };
  }
};

// A turn's outcome shortened for a notification balloon (a turn that lands while
// the surface is closed must not vanish silently — both surfaces toast it).
export const CHAT_TOAST_CHARS = 90;
// A SPOKEN prompt is echoed back far shorter: 90 characters of a dictated paragraph made the
// balloon a wall of text over the canvas (user report).
export const SPOKEN_ECHO_CHARS = 34;
export const truncateForToast = (text, max = CHAT_TOAST_CHARS) =>
  (text.length > max ? `${text.slice(0, max - 1)}…` : text);
// What the "Sent" balloon shows of a spoken prompt: its opening words, on ONE line
// (dictation carries no newlines of its own, but a composer's typed prefix can).
export const spokenEcho = (text) =>
  truncateForToast(String(text ?? '').replace(/\s+/g, ' ').trim(), SPOKEN_ECHO_CHARS);

// Returns null when nothing should be said — an abort is the user's own doing.
export const closedTurnToast = (res) => {
  if (!res || res.kind === 'abort') return null;
  if (!res.ok) return { text: truncateForToast(`Assistant failed — ${res.error?.message ?? res.text ?? ''}`), type: 'fail' };
  const made = res.entry?.results?.length || 0;
  const images = made ? ` (${made} image${made === 1 ? '' : 's'})` : '';
  // The reply as the transcript shows it, so a wordless turn says something here too.
  return { text: truncateForToast(`Assistant finished${images} — ${settledReplyText(res.entry)}`), type: 'ok' };
};

// One LOGGED turn, shared by both chat surfaces: the user row plus a pending row, an
// AbortController for Stop, the patch, then cleanup. Never throws; hooks carry the deltas.
export const runLoggedChatTurn = async (controller, text, { settings, begin, onResult, cleanup } = {}) => {
  turnInFlight = true;
  // The queue is read BEFORE the send drains it, so the images ride the user's own
  // row (they are the user's, not the assistant's).
  appendChatRow({ role: 'user', text, attachments: attachmentPreviews(controller) });
  const pending = appendChatRow({ role: 'assistant', text: '…', pending: true });
  const abort = typeof AbortController !== 'undefined' ? new AbortController() : null;
  begin?.(abort);
  try {
    const res = await runChatTurn(controller, text, { signal: abort?.signal, settings });
    updateChatRow(pending.id, res.ok
      ? { pending: false, text: res.text, results: res.entry.results, ask: res.entry.ask || null, askPreviews: res.entry.askPreviews || [] }
      : { pending: false, text: res.text, error: true, retryText: text,
        card: res.kind === 'unreachable' || res.kind === 'expired',
        reconnect: res.kind === 'expired' ? (res.serverUrl || '') : null });
    onResult?.(res);
    return res;
  } finally {
    turnInFlight = false;   // cleared BEFORE cleanup: a surface may send again from it
    // Belt and braces: NO row may be left spinning — a forever-pending bubble is the one
    // outcome the user cannot recover from (they cannot even retry).
    if (chatLog().some((r) => r.id === pending.id && r.pending)) {
      updateChatRow(pending.id, { pending: false, error: true, retryText: text,
        text: 'Error: the turn ended without an answer.' });
    }
    cleanup?.();
  }
};
