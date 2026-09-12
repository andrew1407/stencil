// ── The app's ONE assistant conversation (llm-contract.md §7) ───────────
// The chat panel and the context-menu chat are two views of the SAME conversation: both go
// through the single controller this module memoizes per app, so history, attachments and
// the working-video binding stay continuous. Nothing here touches the DOM.
import { createChatController, MAX_ATTACHMENTS, CHAT_ATTACHMENTS_EVENT } from './chatController.js';
import { createLlmClient, LlmError, PROVIDER_LABELS } from './llmClient.js';
import { loadLlmSettings, serverBearerToken } from './llmSettings.js';
import { isAuthStatus } from '../net/connectionManager.js';
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { publish } from '../bus/appBus.js';
import { mediaAdapters } from './adapters/media.js';
import { projectAdapters } from './adapters/project.js';
import { dialogAdapters } from './adapters/dialog.js';
import { editorAdapters } from './adapters/editor.js';
// Re-exported: both were found here first, and the adapters are the other caller.
export { uniqueProjectName, resolveProjectByName } from './projectNames.js';

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

// ── The rendered transcript, shared by every chat surface ───────────────────
// One conversation ⇒ ONE visible transcript, so a surface opened later renders the history
// instead of looking empty. Rows are data only (the DOM lives in ui/chatView.js); `text` is
// model output — always rendered with textContent.
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
// The ONE clear-conversation path every entry point shares (panel trash, context-menu
// clear, stencil.chat.clear(), the §10 clearChat op): controller state, the visible
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

// Is a logged turn running RIGHT NOW? One conversation, one turn: runLoggedChatTurn owns
// this single flag so every entry point (both retries, Resend, stencil.prompt) reads the
// same truth and cannot start a second turn on the same log.
let turnInFlight = false;
export const chatTurnInFlight = () => turnInFlight;

// Queue Files onto the shared controller (picker, paste, or drop), reporting each failure
// separately so one bad file doesn't lose the rest. Files past the §7 cap are counted, not
// thrown — `onCapped` fires ONCE per batch. Returns how many landed.
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

// THIS turn's attachments reduced to what a transcript can show, taken before the send
// consumes the queue so the user's own message carries its images. A video previews its
// first extracted frame (frames are what actually go to the model — §7). Display-only:
// rows are never persisted with images (§12.1, chatStore.js).
export const attachmentPreviews = (controller) =>
  (controller?.attachments || [])
    .map((a) => ({
      name: a.name || (a.kind === 'video' ? 'video' : 'image'),
      kind: a.kind || 'image',
      dataUrl: a.dataUrl || a.frames?.[0] || '',
    }))
    .filter((a) => a.dataUrl);

// Re-queue a logged user row's attachment previews for the row menu's Resend, mirroring
// requeueLastTurnAttachments: only into an EMPTY queue (anything queued since wins),
// capped, always as analyze-images (a video row's preview is the first frame seen).
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

// ── Shared provider-status probe ────────────────────────────────────────────
// The panel gear and the context-menu gear show the same dot; caching the last
// probe (keyed by the settings that produced it, short TTL) means the second
// surface costs nothing instead of re-hitting the endpoint on every open.
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

// The visible answer for a finished turn: the reply plus any unknown-op skips
// appended in parentheses (contract §1). Pure.
export const replyWithWarnings = (entry) => {
  const reply = entry?.reply ?? '';
  const warnings = entry?.warnings || [];
  return warnings.length ? `${reply}\n(${warnings.join('; ')})` : reply;
};

// …and what a settled turn actually SHOWS. A blank completion (a local model whose
// context the prompt overran) parses as a chat-only turn with an empty reply — say so
// instead of rendering a blank bubble.
export const EMPTY_REPLY_TEXT = 'The model returned an empty answer — nothing was changed. Retry, or switch to a larger model.';
export const settledReplyText = (entry) => replyWithWarnings(entry).trim() || EMPTY_REPLY_TEXT;

// "Couldn't reach <provider> at <host> (<why>)" — shown when the transport fails (or the
// assistant is off). An endpoint that ANSWERED with an error (err.answered) is quoted in its
// own words instead: the server is up, so the endpoint is a label on that reason, not a
// second sentence (§6.3 "Say the reason once").
export const unreachableText = (settings, err) => {
  if (settings?.provider === 'none') return 'The assistant is turned off — choose a provider to enable it.';
  const name = PROVIDER_LABELS[settings?.provider] || settings?.provider || 'the assistant';
  const url = (settings?.provider === 'stencil-server' ? settings?.serverUrl : settings?.baseUrl) || '';
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (err?.answered) return `${name}${at}: ${why}`;
  return `Couldn't reach ${name}${at} (${why})`;
};

// Map a failed turn to what the user sees. `kind`: abort → "Stopped." (no toast);
// refusal / notice → textual, they ARE the answer; expired → card + RECONNECT cta;
// unreachable → card + configure CTA; error → "Error: …" + retry.
// Pure — the DOM decisions live in the two chat views.
export const describeChatError = (err, settings) => {
  if (err?.name === 'AbortError') return { kind: 'abort', text: 'Stopped.' };
  const k = err instanceof LlmError ? err.kind : null;
  if (k === 'refusal') return { kind: 'refusal', text: `Refused: ${err.message}` };
  if (k === 'truncated' || k === 'disabled') return { kind: 'notice', text: err.message };
  // A collaboration server that REFUSED the bearer token: the provider is reachable and
  // configured — this session is simply over, exactly as the projects list finds on boot.
  // Its own kind, so the card offers the one thing that helps (reconnect), not "configure".
  if (settings?.provider === 'stencil-server' && isAuthStatus(err?.status)) {
    const url = settings.serverUrl || '';
    return {
      kind: 'expired',
      text: `Your session on ${url.replace(/^https?:\/\//i, '') || 'the server'} has expired — `
        + 'reconnect to that server, then send this again.',
      serverUrl: url,
    };
  }
  // 'network' is tagged by the client at the fetch itself; a bare TypeError is
  // NOT assumed to be one — plan execution can throw those too (canvas APIs),
  // and "couldn't reach the provider" would be the wrong diagnosis for them.
  if (k === 'http' || k === 'config' || k === 'network') {
    return { kind: 'unreachable', text: unreachableText(settings, err) };
  }
  return { kind: 'error', text: `Error: ${err?.message ?? err}` };
};

// One turn through the shared controller, the outcome reduced to what a view renders:
// { ok:true, text, entry } or { ok:false, kind, text, error }. Only shapes the result,
// so the panel and the context-menu chat can't drift apart.
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
// A SPOKEN prompt is echoed back far shorter than that: hands-free, the toast only has
// to prove the mic heard the right thing, and dictated prompts run to whole paragraphs —
// 90 characters of one made the balloon a wall of text over the canvas (user report).
export const SPOKEN_ECHO_CHARS = 34;
export const truncateForToast = (text, max = CHAT_TOAST_CHARS) =>
  (text.length > max ? `${text.slice(0, max - 1)}…` : text);
// What the "Sent" balloon shows of a spoken prompt: its opening words, on ONE line
// (dictation carries no newlines of its own, but a composer's typed prefix can).
export const spokenEcho = (text) =>
  truncateForToast(String(text ?? '').replace(/\s+/g, ' ').trim(), SPOKEN_ECHO_CHARS);

// …and the WHOLE balloon a landed turn deserves, built once so every surface shows the
// same thing. Returns null when nothing should be said — an abort is the user's own doing.
//   { text, type } → notify(text, type, { onClick: <reopen the assistant> })
export const closedTurnToast = (res) => {
  if (!res || res.kind === 'abort') return null;
  if (!res.ok) return { text: truncateForToast(`Assistant failed — ${res.error?.message ?? res.text ?? ''}`), type: 'fail' };
  const made = res.entry?.results?.length || 0;
  const images = made ? ` (${made} image${made === 1 ? '' : 's'})` : '';
  // The reply as the transcript shows it, so a wordless turn says something here too.
  return { text: truncateForToast(`Assistant finished${images} — ${settledReplyText(res.entry)}`), type: 'ok' };
};

// ── One LOGGED turn, shared by both chat surfaces ───────────────────────────
// The frame every surface repeats around runChatTurn: append the user row plus a pending
// "…" assistant row, arm an AbortController for Stop, patch the pending row with the
// outcome, and always run cleanup. Hooks carry the per-surface deltas: begin(abort) stashes
// the controller, onResult(res) fires after the patch, cleanup() always runs last.
// Returns runChatTurn's result; never throws (surfaces rethrow res.error if needed).
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
    // Success resolves the pending row into the reply (+ results / §11 ask card);
    // a transport failure / assistant-off becomes the helpful card with the
    // configure CTA — identical patches on every surface.
    updateChatRow(pending.id, res.ok
      ? { pending: false, text: res.text, results: res.entry.results, ask: res.entry.ask || null, askPreviews: res.entry.askPreviews || [] }
      // A turn that ended without an answer remembers what it tried (a Stop included).
      // An expired session gets a card with the RECONNECT affordance rather than the
      // provider one: nothing about the configuration is wrong.
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
