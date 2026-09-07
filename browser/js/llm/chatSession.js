// ── The app's ONE assistant conversation (llm-contract.md §7) ───────────
// The chat panel and the context-menu chat are two views of the SAME conversation:
// both go through the single controller this module memoizes per app, so history,
// queued attachments and the working-video binding stay continuous whichever
// surface you type into. Nothing here touches the DOM directly — the turn runner
// and the error mapping are pure enough to unit-test with a recording stub.
import { createChatController, downscaleImageToDataUrl, MAX_ATTACHMENTS, CHAT_ATTACHMENTS_EVENT } from './chatController.js';
import { toHexColor } from '../core/accents.js';
import { createLlmClient, LlmError, PROVIDER_LABELS } from './llmClient.js';
import { loadLlmSettings, serverBearerToken } from './llmSettings.js';
import { videoFrameSamples, videoFrameByIndex } from '../core/videoFrame.js';
import { isAuthStatus } from '../net/connectionManager.js';

const CONTROLLERS = new WeakMap();

// §10 dialog: the toolbar button behind each editor window the assistant may open — the
// very ids controlsBinder's openProjects/openServers/… hotkey actions press, so the plan
// and the shortcut take exactly the same path. Keys are the op's `name` enum (opPlan.js).
const DIALOG_BUTTON_IDS = {
  projects: 'projects-btn', servers: 'connect-btn', shortcuts: 'settings-btn',
  visuals: 'visuals-btn', help: 'info-btn',
};

// Project names are unique (projectsStore.nameExists), and a batch of saves routinely
// wants the same base — suffix until it is free rather than losing the save to a clash.
export const uniqueProjectName = (app, wanted) => {
  const store = app?.storage?.store;
  if (!store?.nameExists) return wanted;
  if (!store.nameExists(wanted)) return wanted;
  for (let n = 2; n < 1000; n++) {
    const candidate = `${wanted} ${n}`;
    if (!store.nameExists(candidate)) return candidate;
  }
  return wanted;
};

// §10 name resolution, shared by removeProject/openProject: exact name match among
// the SAVED local projects, else a unique case-insensitive prefix. Returns the meta
// record, or a note string explaining why nothing resolved.
export const resolveProjectByName = (app, name) => {
  const list = app.storage.store.list();
  const exact = list.filter((m) => (m.name || '') === name);
  const picks = exact.length
    ? exact
    : list.filter((m) => (m.name || '').toLowerCase().startsWith(name.toLowerCase()));
  if (!picks.length) return { note: `no saved project named "${name}"` };
  if (picks.length > 1) return { note: `"${name}" matches ${picks.length} projects — use the full name` };
  return { meta: picks[0] };
};

// The controller for `app`, created on FIRST USE (not at wire time) so it captures
// the frozen window.stencil facade — createStencil runs after the UI is wired.
// `create` is injected by tests; the first caller wins, every later caller shares.
export const sharedChatController = (app, { create = createChatController } = {}) => {
  let controller = CONTROLLERS.get(app);
  if (!controller) {
    controller = create({
      stencil: typeof window !== 'undefined' ? window.stencil : undefined,
      getClient: () => createLlmClient({
        settings: loadLlmSettings(),
        getToken: (url) => serverBearerToken(app, url),
      }),
      // Guarded: a plan wanting variants / ask previews on an EMPTY editor must
      // fail with words, not a raw canvas drawImage(null) TypeError.
      exportImage: async () => {
        if (!app.image) throw new Error('No image is loaded in the editor — load or create one first');
        return app.export.renderExportCanvas().toDataURL('image/png');
      },
      prepareAttachment: (file) => downscaleImageToDataUrl(file),
      extractFrames: (file, n) => videoFrameSamples(file, n),
      frameAt: (file, i) => videoFrameByIndex(file, i),
      // §10 openUrl with incognito: adopt incognito IN PLACE (desktop's openSourceHere),
      // NOT a new tab — the rest of the plan and the §7 continuation that has to LOOK at
      // the picture run here, so loading here keeps chat and plan on the fetched image.
      openIncognito: async (url) => { await window.stencil.load(url, { incognito: true }); },
      // §10 copy: the clipboard write's REAL outcome, so a blocked write becomes a
      // reply warning instead of only a transient toast.
      copyRendered: () => app.export.copyImageToClipboard(),
      // §10 copy what:"layout": same outcome-promise pattern for the layout JSON.
      copyLayoutRendered: () => app.export.copyLayoutToClipboard(),
      // §10 project management — resolve by name among the SAVED projects, then run
      // the same guarded flows the projects modal uses (their confirms included).
      // Returns a note string when nothing was removed (unknown/ambiguous/declined).
      removeProjectNamed: async (name) => {
        const { meta, note } = resolveProjectByName(app, name);
        if (note) return note;
        const ok = await app.confirm(`Remove project "${meta.name}"? This cannot be undone.`,
          { title: 'Remove project', danger: true, confirmIcon: 'trash' });
        if (!ok) return 'removal canceled';
        app.removeProject(meta.id);
        return null;
      },
      // §10 removeProject current:true with NOTHING saved: the `clear` op's own flow
      // (newEditor) behind removeProject's confirm — worded for what actually goes.
      // The chat stays (mid-turn; an unsaved/incognito editor never filed one anyway).
      clearWorkingImage: async () => {
        if (!app.image && !app.lines.length) return 'nothing to remove';
        const what = app.storage.incognito
          ? 'the image in this incognito editor' : 'the unsaved image and its lines';
        const ok = await app.confirm(`Remove ${what}? This cannot be undone.`,
          { title: 'Remove image', danger: true, confirmIcon: 'trash' });
        if (!ok) return 'removal canceled';
        app.newEditor({ keepChat: true });
        return null;
      },
      // §10 openProject: the projects modal's open path. Replacing an UNSAVED dirty
      // temporary asks the modal's own confirm first; an already-open project or a
      // failed switch comes back as a note, never a failed plan.
      openProjectNamed: async (name, last = false) => {
        // "the last project I worked on": the store lists newest-edited first, so the
        // head of the list IS the answer — resolved HERE, never by the model (it is
        // never shown the project list).
        const recent = last ? app.storage.store.list()[0] : null;
        if (last && !recent) return 'there are no saved projects yet';
        const { meta, note } = last ? { meta: recent } : resolveProjectByName(app, name);
        if (note) return note;
        if (meta.id === app.activeProjectId) return `"${meta.name}" is already open`;
        if (app.storage.temporary && (app.image || app.lines.length)) {
          const ok = await app.confirm(
            `Open "${meta.name}" here? Any unsaved changes in the current tab will be replaced.`,
            { title: 'Open project', confirmLabel: 'Open', confirmIcon: 'folder' });
          if (!ok) return 'open canceled';
        }
        return app.switchToProject(meta.id) ? null : `could not open "${meta.name}"`;
      },
      // §10 renameProject: the inline rename control's path; the store's own
      // duplicate-name refusal surfaces as the note.
      renameActiveProject: async (name) => {
        const id = app.activeProjectId;
        if (id == null) return 'no active saved project to rename';
        if (app.storage.store.nameExists(name, id)) return `a project named "${name}" already exists`;
        return app.renameProject(id, name) ? null : `could not rename to "${name}"`;
      },
      // §10 blankColor: valid only on a BLANK project (keeps the drawn lines).
      // CSS names resolve to hex first (the setters take normalizeHex forms only).
      setBlankColor: async (color) => {
        const hex = toHexColor(color);
        if (app.activeProjectId != null) {
          return app.setProjectBlankColor(app.activeProjectId, hex)
            ? null : 'only a blank project has a recolourable background';
        }
        if (!app.activeIsBlank() || !app.image) return 'only a blank project has a recolourable background';
        app.setBlankColor(hex);
        return null;
      },
      // §10 clearProjects: every saved local project, or every one BUT the open project
      // (`keepCurrent`) — "delete the others" used to leave the model clearing the lot
      // and trying to save the working image back, which lost the project when there was
      // no image to re-save (user report).
      clearLocalProjects: async (keepCurrent = false) => {
        const all = app.storage.store.list();
        const keepId = keepCurrent ? app.activeProjectId : null;
        const doomed = keepId == null ? all : all.filter((m) => m.id !== keepId);
        if (!doomed.length) {
          return all.length ? 'no other saved projects to clear' : 'no saved projects to clear';
        }
        const kept = keepId == null ? null : all.find((m) => m.id === keepId);
        const ok = await app.confirm(kept
          ? `Delete the ${doomed.length} other saved local project${doomed.length === 1 ? '' : 's'}, keeping "${kept.name}"? This cannot be undone.`
          : `Delete every saved local project (${doomed.length})? This cannot be undone.`,
          { title: kept ? 'Clear other projects' : 'Clear all projects', danger: true, confirmIcon: 'trash' });
        if (!ok) return 'clear canceled';
        if (!kept) app.clearAllProjects();
        else for (const m of doomed) app.removeProject(m.id);
        return null;
      },
      // §10 clearChat: the shared clear-conversation flow behind the app's own
      // confirm; the executor defers it to the plan's end. Declined = the note.
      clearChatConversation: async () => {
        const ok = await app.confirm('Clear this conversation? Its chat history will be deleted.',
          { title: 'Clear chat', danger: true, confirmIcon: 'trash' });
        if (!ok) return 'clear canceled';
        clearSharedConversation(app);
        return null;
      },
      // §10 dialog: the editor's own windows, opened through the very toolbar buttons
      // the user would click (controlsBinder's openProjects/openServers/… actions press
      // the same ids). A disabled button is a note, never a failed plan; null closes
      // whatever is open, which is the facade's own dismissal path.
      openDialog: async (name) => {
        if (typeof document === 'undefined') return 'no dialogs on this surface';
        if (!name) {
          const open = document.querySelectorAll('.app-modal-overlay.modal-open');
          if (!open.length) return 'no dialog is open';
          open.forEach((o) => o.classList.remove('modal-open'));
          return null;
        }
        const btn = document.getElementById(DIALOG_BUTTON_IDS[name]);
        if (!btn) return `the ${name} window is not available here`;
        if (btn.disabled) return `the ${name} window is not available right now`;
        btn.click();
        return null;
      },
      // §10 chat: the assistant panel's own placement, through the very buttons its
      // header carries (app.chat = the panel's public face). A "dock" with no "open"
      // opens it too — moving a panel nobody can see is not what was asked for.
      setChatPlacement: async ({ open, dock } = {}) => {
        if (!app.chat) return 'this surface has no assistant panel';
        // Show FIRST, then place, then close if that is what was asked: the desktop's
        // float leg only has a window to lift once the panel is showing, and the two
        // surfaces must end in the same state for the same plan.
        const show = open == null ? !!dock : open === true;
        try {
          if (show) app.chat.open();
          if (dock) app.chat.dock(dock);
          if (!show && open === false) app.chat.close();
          return null;
        } catch (err) { return err?.message || String(err); }
      },
      // §10 voiceChat: the browser-only hands-free toggle; an unsupported browser's
      // throw (or a missing coordinator) is the note, never a failed plan.
      setVoiceChat: (on) => {
        if (!app.voice) return 'voice input is not available';
        try { app.voice.voiceChat = !!on; return null; }
        catch (err) { return err?.message || String(err); }
      },
      // Send drained the queued attachments — repaint every composer's chips.
      onAttachmentsChanged: () => {
        try { window.dispatchEvent(new Event(CHAT_ATTACHMENTS_EVENT)); } catch { /* no DOM */ }
      },
      // §2.1 `save`: persist the working image + layout as a LOCAL project (publishing to
      // a server stays a user action). Each save promotes to a FRESH project id, so a
      // multi-image plan leaves one project per image instead of overwriting one.
      saveProject: async (name) => {
        if (!app.image) throw new Error('there is no image to save');
        const wanted = String(name || app.imageBaseName || 'Untitled').trim() || 'Untitled';
        const unique = uniqueProjectName(app, wanted);
        app.storage.promoteTemporaryToProject();
        app.imageBaseName = unique;
        app.storage.save();
        app.renameProject(app.activeProjectId, unique);
        app.updateProjectTitle?.();
        return unique;
      },
    });
    CONTROLLERS.set(app, controller);
  }
  return controller;
};

// The controller if one already exists — never creates it (callers that only read
// state, e.g. the panel's attachment row on first paint, must not force creation
// before window.stencil is frozen).
export const peekChatController = (app) => CONTROLLERS.get(app) || null;

// Test seam: forget the memoized controller for `app`.
export const forgetChatController = (app) => CONTROLLERS.delete(app);

// ── The rendered transcript, shared by every chat surface ───────────────────
// One conversation ⇒ ONE visible transcript: the panel and the context-menu flyout both
// render this log, and a surface opened later renders the history instead of looking
// empty. Rows are data only (the DOM lives in ui/chatView.js); `text` is model output —
// always rendered with textContent.
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
  try { window.dispatchEvent(new Event(CHAT_ATTACHMENTS_EVENT)); } catch { /* no DOM */ }
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

// Queue Files onto the shared controller (picker, paste, or drop), reporting each
// failure separately so one bad file doesn't lose the rest. Files past the §7 cap are
// counted, not thrown: `onCapped` fires ONCE per batch so the caller can say so as an
// accent notice (the desktop's toast), while a genuinely bad file still reports as a
// failure. Returns how many landed.
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
export const PROBE_TTL_MS = 15000;
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
// assistant is off). An endpoint that ANSWERED with an error (err.answered) is quoted in
// its own words instead — the server is clearly up, and the endpoint is a label on that
// reason, not a second sentence (§6.3 "Say the reason once"). No "— is it running?": a
// rhetorical question adds nothing the user can act on and pads the one line that has to
// carry the actual reason.
export const unreachableText = (settings, err) => {
  if (settings?.provider === 'none') return 'The assistant is turned off — choose a provider to enable it.';
  const name = PROVIDER_LABELS[settings?.provider] || settings?.provider || 'the assistant';
  const url = (settings?.provider === 'stencil-server' ? settings?.serverUrl : settings?.baseUrl) || '';
  const at = url ? ` at ${url.replace(/^https?:\/\//i, '')}` : '';
  const why = err?.message ?? String(err ?? '');
  if (err?.answered) return `${name}${at}: ${why}`;
  return `Couldn't reach ${name}${at} (${why})`;
};

// Map a failed turn to what the user sees. `kind`:
//   abort       the user pressed Stop      → "Stopped." (no toast anywhere)
//   refusal     the model refused          → textual, it IS the answer
//   notice      truncated / server disabled→ the typed message, textual
//   expired     the server refused the token→ card + a RECONNECT cta (session over)
//   unreachable transport/config failure   → rendered as a card + configure CTA
//   error       anything else (bad plan, badReply malformed 200…) → "Error: …" + retry
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
// The frame every surface repeats around runChatTurn: append the user row plus a
// pending "…" assistant row to the shared log, arm an AbortController for Stop,
// patch the pending row with the outcome, and always run the surface's cleanup.
// Hooks carry the per-surface deltas:
//   begin(abort)   the turn started — stash the AbortController for the Stop button
//   onResult(res)  after the row patch — toasts, status refreshes
//   cleanup()      always runs last — control sync, attachment rows, scroll
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
