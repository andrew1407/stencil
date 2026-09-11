// ── window.stencil — console control API for the Stencil editor ─────────────
// Thin chainable facade over the live DrawingApp; NEVER reimplements editor behaviour —
// every mutation routes through the same core methods the toolbar uses, so console and
// toolbar stay in sync. A closure factory (not a class): `app` lives in scope and the
// returned objects carry no fields. index.js builds it after the app → window.stencil.
//   stencil.apply({ page: 'a3', pointSize: 9 }).rotateLeft().crop({ x1: '10%' })
//   (await stencil.load(url)).crop({ x2: '-10%' }).apply({ lineColor: 'aqua' })
import { hotkeys } from '../core/hotkeys.js';
import { resolveAxisPx } from '../core/units.js';
import { cropAspect, scaleCropCentered } from '../core/cropGeometry.js';
import { ConnectionManager } from '../net/connectionManager.js';
import { loadSavedServers, saveServers, getAutoConnect } from '../net/connectionStore.js';
import { requireConnection } from '../net/remoteSync.js';
import { notify } from '../utils.js';
import { videoFrameDataUrl } from '../core/videoFrame.js';
import { loadLlmSettings, saveLlmSettings, PROVIDERS, withProvider, URL_KEYS, isHttpUrl } from '../llm/llmSettings.js';
import {
  chatSide, setChatSide, applyChatSide, CHAT_SIDE_SWAPPED,
} from '../ui/chatLayoutPrefs.js';
import { closeOpenModal } from '../ui/base.js';
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };
import { publish, EVENTS } from '../bus/appBus.js';
import { str } from './coerce.js';
import { createLineWrappers } from './lineAndPoint.js';
import { createProjectWrapper, DURATION_HELP } from './project.js';
import { createSettingsFacade } from './settingsFacade.js';

// A layout argument may be an OBJECT or a raw JSON string — parse the latter so callers can
// hand over clipboard text directly. A non-object (or bad JSON) throws rather than no-op.
const toLayoutObject = (data) => {
  if (typeof data !== 'string') return data;
  let parsed;
  try { parsed = JSON.parse(data); } catch { throw new TypeError('stencil: layout JSON could not be parsed'); }
  if (parsed == null || typeof parsed !== 'object') throw new TypeError('stencil: layout JSON must describe an object');
  return parsed;
};


// The editor's windows, for stencil.openWindow(title). The table is config/uiStrings.json;
// an opener's disabled state gates the script route exactly as it gates the click.
export const WINDOWS = Object.freeze(UI_STRINGS.windows);
// Loose title matching: case-insensitive, punctuation/whitespace-free, so 'Visuals',
// 'visuals & settings', 'open-in' and 'Open In…' all land.
const windowNameKey = (v) => str(v).toLowerCase().replace(/[^a-z0-9]+/g, '');
const findWindow = (ref) => {
  const want = windowNameKey(ref);
  if (!want) return null;
  return WINDOWS.find((w) => [w.key, w.title, w.hotkey, ...(w.aliases || [])].some((n) => windowNameKey(n) === want)) || null;
};

// Help text for stencil.expire() — shown when it's called with no argument. The
// grammar is DurationParser's (durationParser.js / core/parse/durationParser.cpp).
export const createStencil = (app) => {
  // One ConnectionManager per session, shared with the connection UI via app.connections;
  // onChange fires a DOM event so the projects modal / connect dialog can refresh.
  const firstInit = !app.connections;
  const connMgr = app.connections || (app.connections = new ConnectionManager({
    onChange: (change) => {
      // Persist the live set so it survives reloads (connectionStore.js), then let
      // the connect/projects UI refresh off the same DOM event.
      try { saveServers(connMgr.snapshot()); } catch { /* storage blocked */ }
      // Live co-edit: forward a server project-event to the editor so it can reload the
      // active project when a peer changes it.
      if (change && change.type === 'event' && change.message?.type === 'project-event') {
        try { app.remoteSync?.onServerProjectEvent?.(change.message, change.connection); } catch { /* editor not ready */ }
      }
      try {
        publish(EVENTS.connectionsChanged);
      } catch { /* no DOM */ }
    },
  }));
  // On first boot, optionally re-establish the saved server set ("auto-connect on open").
  // Each connects independently, and each unreachable address gets its own toast.
  if (firstInit && getAutoConnect()) {
    const saved = loadSavedServers();
    // A credential the server ALREADY refused is never retried. The row is adopted into
    // the expired set instead, so the Servers button keeps its dot and offers Reconnect.
    const dead = saved.filter((s) => s.expired);
    const live = saved.filter((s) => !s.expired);
    for (const s of dead) { try { connMgr.adoptExpired(s); } catch { /* bad url — skip */ } }
    if (dead.length) {
      console.warn(`stencil: ${dead.length} saved server session(s) need signing in again — Servers ▸ Reconnect`);
    }
    if (live.length) {
      Promise.allSettled(live.map((s) => connMgr.connect(s))).then((results) => {
        // Results stay positional with `live`, so a failure can be traced back to the
        // server it belongs to.
        const rejected = results
          .map((r, i) => ({ r, url: live[i].url }))
          .filter(({ r }) => r.status === 'rejected');
        // A REFUSED credential is not an unreachable server: the server answered, this
        // session is simply over — say it needs a new token and open the Connections
        // modal on click, rather than sending the user hunting a server that is up.
        const expired = rejected.filter(({ r }) => r.reason?.expired);
        const unreachable = rejected.filter(({ r }) => !r.reason?.expired);
        if (expired.length) {
          console.warn(`stencil: ${expired.length} saved server session(s) expired — reconnect from Connections`);
          notify(`Session expired on ${expired.length} saved server${expired.length === 1 ? '' : 's'} — reconnect`, 'fail',
            { onClick: () => document.getElementById('connect-btn')?.click() });
        }
        // One toast per address, not a count — a count says nothing about WHICH
        // server to go check.
        for (const { url } of unreachable) notify(`Couldn't reach ${url}`, 'info');
      });
    }
  }
  let peers = [];
  try { app.tabs.onPeers((ids) => { peers = Array.isArray(ids) ? ids : []; }); } catch { /* no coordinator */ }
  const openedIds = () => {
    const ids = new Set(peers.filter((x) => x != null));
    if (app.activeProjectId != null) ids.add(app.activeProjectId);   // own active id, defensively
    return ids;
  };

  let stencil;   // forward ref so wrappers can return the facade for chaining

  // Hard-guard an API object: real setters write through, but writing a method/read-only
  // getter (or add/delete) THROWS instead of silently no-opping in the non-strict console.
  // Applied to the facade and every Line/Point/Project/settings object handed back.
  const guard = (obj) => new Proxy(Object.freeze(obj), {
    set(target, prop, value) {
      const d = Object.getOwnPropertyDescriptor(target, prop);
      if (d && typeof d.set === 'function') { d.set.call(target, value); return true; }
      throw new TypeError(`stencil: "${String(prop)}" is read-only and cannot be reassigned`);
    },
    defineProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" is read-only`); },
    deleteProperty(target, prop) { throw new TypeError(`stencil: "${String(prop)}" cannot be deleted`); },
  });

  // Dismiss any open editor modal (projects, shortcuts/info, links, visuals, …) so a
  // console-driven load isn't hidden behind one. Toggles the shared .modal-open class;
  // each modal's onClose cleanup is idempotent and re-runs on next open.
  const closeModals = () => {
    try { document.querySelectorAll('.app-modal-overlay.modal-open').forEach((o) => o.classList.remove('modal-open')); }
    catch { /* no DOM (node tests) */ }
  };

  // The Line / Point / Project wrappers and the settings namespace are their own modules
  // beside this one (lineAndPoint.js / project.js / settingsFacade.js).
  const { makePoint, makeLine, setFacade } = createLineWrappers({ app, guard });
  const makeProject = createProjectWrapper({ app, guard, openedIds });
  const { settingsAccessors, settings } = createSettingsFacade({ app, guard });

  const incognitoList = () => (app.storage.incognito ? [makeProject(null, true)] : []);
  const savedOpen = () => {
    const open = openedIds();
    return app.storage.store.list().filter((m) => open.has(m.id)).map((m) => makeProject(m.id));
  };

  // Validate + persist a partial LLM-settings update through the SAME store the
  // assistant's gear dialog uses (llmSettings.js), so UI and scripting stay in sync.
  const applyLlmSetup = (opts = {}) => {
    const cur = loadLlmSettings();
    let next = { ...cur };
    if (opts.provider != null) {
      const p = str(opts.provider).trim();
      if (!PROVIDERS.includes(p)) throw new Error(`Unknown LLM provider "${opts.provider}" — one of ${PROVIDERS.join(', ')}`);
      // Switching providers refills the default base URL unless the user overrode it
      // (withProvider — literally the settings modal's rule); an explicit baseUrl
      // below still wins.
      if (p !== cur.provider) next = withProvider(next, p);
    }
    for (const k of URL_KEYS) {
      if (opts[k] != null) {
        const v = str(opts[k]).trim();
        if (v && !isHttpUrl(v)) throw new Error(`${k} must be an http(s) URL`);
        next[k] = v;
      }
    }
    if (opts.model != null) next.model = str(opts.model).trim();
    if (opts.apiKey != null) next.apiKey = str(opts.apiKey);
    saveLlmSettings(next);
    // Same live-refresh signal the settings modal fires (panel re-probes status).
    publish(EVENTS.llmSettingsChanged);
    return next;
  };

  // The chat panel registers its scripting surface as app.chat when it wires.
  const chatPanel = () => {
    if (!app.chat) throw new Error('Chat panel not ready — the editor UI has not wired yet');
    return app.chat;
  };

  // "Swap message sides" (chatLayoutPrefs.js) is deliberately NOT persisted. Both
  // transcripts share the one preference, so restamp whichever is mounted.
  const applyChatSideEverywhere = (side) => {
    if (typeof document === 'undefined') return;
    applyChatSide(document.getElementById('chat-transcript'), side);
    applyChatSide(document.getElementById('ctx-assist-transcript'), side);
  };

  // loadImageFromFile decodes async with no promise; poll until the image is in place.
  // `previous` = the image loaded BEFORE the call, so a REPLACE waits for the swap — not
  // for "some image exists", which would run chained ops against the old picture.
  const waitForImage = (timeoutMs = 8000, previous = null) => new Promise((resolve) => {
    const start = Date.now();
    const again = typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame : (fn) => setTimeout(fn, 16);   // node --test has no rAF
    const tick = () => {
      if ((app.image && app.image !== previous) || Date.now() - start > timeoutMs) resolve();
      else again(tick);
    };
    tick();
  });

  stencil = {
    // ── Projects ──
    get current() {
      if (app.activeProjectId != null) return makeProject(app.activeProjectId);
      if (app.storage.incognito) return makeProject(null, true);
      return null;
    },
    // Open in some tab/window — INCLUDING this tab's incognito editor (it's open too).
    get openedProjects() { return incognitoList().concat(savedOpen()); },
    get archivedProjects() {
      const open = openedIds();
      return app.storage.store.list().filter((m) => !open.has(m.id)).map((m) => makeProject(m.id));
    },
    // Only the CURRENT tab's incognito editor is knowable (others report null).
    get incognitoProjects() { return incognitoList(); },
    // Default = currently-open saved projects; flags add the archived/incognito sets.
    getProjects({ archived = false, incognito = false } = {}) {
      let list = savedOpen();
      if (archived) list = list.concat(stencil.archivedProjects);
      if (incognito) list = list.concat(incognitoList());
      return list;
    },
    getProjectByName(name) {
      const n = str(name).trim().toLowerCase();
      const m = app.storage.store.list().find((p) => str(p.name).trim().toLowerCase() === n);
      return m ? makeProject(m.id) : null;
    },
    // Local projects whose keywords match ANY of the query terms (case-insensitive substring),
    // mirroring the CLI /keywords-search. Returns Project handles, most-recently-updated first.
    getProjectsByKeyword(...keywords) {
      const terms = keywords.flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[\s,]+/)))
        .map((s) => str(s).trim().toLowerCase()).filter(Boolean);
      if (!terms.length) return [];
      return app.storage.store.list()
        .filter((p) => (p.keywords || []).some((kw) => terms.some((t) => str(kw).toLowerCase().includes(t))))
        .map((p) => makeProject(p.id));
    },
    // Set when the ACTIVE project expires, from a free-form duration; no argument returns
    // the accepted formats. Delegates to the same Project.expire() used for chaining,
    // e.g. stencil.current.expire('months 3').
    expire(spec) {
      const s = str(spec).trim();
      if (!s) return DURATION_HELP;
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to set an expiration on — open or create one first');
      return makeProject(id).expire(s);
    },

    // ── Server connections ──
    // Connect one or more collaboration servers for this session. Accepts a URL string,
    // { url, token }, or an array of either; resolves to the facade for chaining.
    //   await stencil.connect(['a:8090', { url: 'b:8090', token: 't' }])
    async connect(urlOrUrls) { await connMgr.connect(urlOrUrls); return stencil; },
    // Close one connection by URL, or (no arg) the most recently opened one.
    disconnect(url) { connMgr.disconnect(url); return stencil; },
    // Re-establish the last connected set (re-validates/re-issues tokens).
    async reconnect() { await connMgr.reconnect(); return stencil; },
    // Read-only list of connected server URLs.
    get connections() { return connMgr.urls; },
    // Aggregated remote projects across every connection (each tagged remote:true
    // with its serverUrl). Resolves to an array of metadata records.
    serverProjects() { return connMgr.remoteProjects(); },
    // Move/copy a SERVER project (a record from serverProjects(), shape { serverUrl, id, … })
    // to local storage, or copy it into an incognito session (opts.newTab opens a new tab).
    moveServerProjectToLocal(meta) { return app.moveProjectToLocal(meta); },
    copyServerProjectToLocal(meta, opts = {}) { return app.copyServerProjectToLocal(meta, opts); },
    copyServerProjectToIncognito(meta, opts = {}) { return app.copyServerProjectToIncognito(meta, opts); },
    // Publish the current incognito session to a server (becomes a normal server project).
    publishIncognito(address) { return app.publishIncognitoToServer(address); },

    // ── AI assistant (LLM) ──
    // Settings mirror the gear dialog (llm-contract.md §5) and share its store, so UI and
    // scripting stay in sync. apiKey reads back as-is — the trust stance server tokens take.
    //   stencil.llm.setup({ provider: 'ollama', model: 'llama3.2-vision' })
    get llm() {
      return guard({
        get provider() { return loadLlmSettings().provider; },
        set provider(v) { applyLlmSetup({ provider: v }); },
        get baseUrl() { return loadLlmSettings().baseUrl; },
        set baseUrl(v) { applyLlmSetup({ baseUrl: v }); },
        get model() { return loadLlmSettings().model; },
        set model(v) { applyLlmSetup({ model: v }); },
        get apiKey() { return loadLlmSettings().apiKey; },
        set apiKey(v) { applyLlmSetup({ apiKey: v }); },
        get serverUrl() { return loadLlmSettings().serverUrl; },
        set serverUrl(v) { applyLlmSetup({ serverUrl: v }); },
        // Partial update in one call; unknown providers / non-http(s) URLs throw.
        setup(opts = {}) { applyLlmSetup(opts); return stencil; },
      });
    },
    // One assistant turn through the panel's pipeline — shared history, rendered in its
    // transcript. `images` attaches base64 data: URLs. Resolves { reply, warnings,
    // results:[{ label, dataUrl }] }; typed LlmErrors (truncated/refusal/…) reject.
    prompt(text, { images = [] } = {}) {
      return chatPanel().prompt(str(text), Array.isArray(images) ? images : [images]);
    },
    // Chat panel control — the same code paths as the panel's own buttons.
    get chat() {
      return guard({
        open() { chatPanel().open(); return stencil; },
        close() { chatPanel().close(); return stencil; },
        dock(mode) { chatPanel().dock(mode); return stencil; },
        get isOpen() { return !!app.chat && app.chat.isOpen(); },
        // The settled transcript (contract §12.1 display form): [{ role, text }]
        // copies — no raw model JSON, no error cards, no in-flight row.
        get history() { return chatPanel().history(); },
        // Stop the in-flight turn (the Stop button's path). True when a turn
        // was actually running.
        abort() { return chatPanel().abort(); },
        // Fresh conversation — the trash button's exact path (history, queued
        // attachments, transcript, and the §12 persisted copy). Throws mid-turn.
        clear() { chatPanel().clear(); return stencil; },
        get isSending() { return !!app.chat && app.chat.isSending; },
        // Which side user/assistant/error bubbles draw on. Scoped to THIS tab's
        // session: never persisted (chatLayoutPrefs.js), and this is the one way to
        // adjust it outside the panel's own menu item.
        get swapSides() { return chatSide() === CHAT_SIDE_SWAPPED; },
        set swapSides(v) {
          setChatSide(v ? CHAT_SIDE_SWAPPED : 'normal');
          applyChatSideEverywhere();
        },
        // Dictation into the panel's composer — the mic face (the "…" item, a
        // double-click or a hold on Send). Turning it on stops the hands-free voice chat.
        get voiceInput() { return !!app.chat && app.chat.voiceInput; },
        set voiceInput(v) { chatPanel().setVoiceInput(!!v); },
      });
    },

    // ── Browser extension ──
    // Its editor-page API, installed on window.__stencilExt by the content script — the
    // extension owns the shape (extension/README.md). null unless installed and enabled.
    get extension() { return (typeof window !== 'undefined' && window.__stencilExt) || null; },

    // ── Settings / modes ──
    get settings() { return settings(); },
    get fullscreen() { return typeof document !== 'undefined' && document.body.classList.contains('fullscreen-mode'); },
    set fullscreen(v) { if (!!v !== stencil.fullscreen && typeof app.toggleFullscreen === 'function') app.toggleFullscreen(); },
    get imageSize() { const img = app.image; return img ? { width: img.width, height: img.height } : undefined; },
    // Current crop rect in rotated-original px — {x,y,w,h} plus legacy width/height
    // aliases; null before an image loads. The LLM plan executor re-maps against it (§1).
    get cropRect() { const r = app.cropRect; return r ? { x: r.x, y: r.y, w: r.width, h: r.height, width: r.width, height: r.height } : null; },
    get incognito() { return !!app.storage.incognito; },
    // Incognito can only be turned on for a blank editor (no image yet) — same rule as
    // the toolbar toggle; setting it otherwise throws.
    set incognito(on) {
      if (!!app.storage.incognito === !!on) return;
      if (on && (app.image || app.lines.length || app.activeProjectId != null || !app.storage.temporary))
        throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
      // Turning it OFF with work on screen keeps the work: leaving incognito IS the user
      // asking to save, so it promotes to a local project (promoteIncognitoToLocal).
      if (!on && app.image) { app.promoteIncognitoToLocal(); return; }
      app.storage.incognito = !!on;
      app.updateIncognitoUI();
    },
    // Leave incognito and keep the current picture + lines as a local project. Returns the
    // project id (null when the editor is blank). The server twin is publishIncognito().
    promoteIncognito() { return app.promoteIncognitoToLocal(); },
    // Tooltip sections as a live get/set object.
    get tooltip() {
      return guard({
        get enabled() { return app.tooltipEnabled; }, set enabled(v) { app.settings.setTooltipOption('enabled', v); },
        get page() { return app.tooltipShowPage; }, set page(v) { app.settings.setTooltipOption('page', v); },
        get screen() { return app.tooltipShowScreen; }, set screen(v) { app.settings.setTooltipOption('screen', v); },
        get coords() { return app.tooltipShowCoords; }, set coords(v) { app.settings.setTooltipOption('coords', v); },
      });
    },

    // ── Lines / points ──
    get lines() { return app.lines.map((_, i) => makeLine(i)); },
    // Points of the "current" line — in-progress, else coord-table line, else last line.
    get points() {
      if (app.currentLine) return app.currentLine.points.map((_, i) => makePoint(-1, i));
      const idx = app.coordLineIdx >= 0 ? app.coordLineIdx : app.lines.length - 1;
      return idx >= 0 && app.lines[idx] ? app.lines[idx].points.map((_, i) => makePoint(idx, i)) : [];
    },

    // ── Shortcuts ──
    get shortcuts() {
      const out = {};
      for (const [id, combo] of hotkeys.entries()) out[id] = combo;
      return out;
    },
    // Rebind a shortcut. `oldRef` matches an action id OR its current combo string.
    changeShortcut(oldRef, newCombo) {
      let id = null;
      for (const [aid, combo] of hotkeys.entries()) if (aid === oldRef || combo === oldRef) { id = aid; break; }
      if (!id) throw new Error(`No shortcut matches "${oldRef}"`);
      for (const [aid, combo] of hotkeys.entries()) if (aid !== id && combo === newCombo) throw new Error(`"${newCombo}" is already bound to ${aid}`);
      hotkeys.set(id, newCombo);
      hotkeys.save();
      try { hotkeys.updateCtxHints?.(); hotkeys.updateHotkeyTitles?.(); } catch { /* no DOM */ }
      return stencil;
    },

    // ── Windows (the toolbar windows, by title) ──
    // Titles come from config/uiStrings.json and match loosely (case/punctuation-free;
    // hotkey ids like 'openProjects' work too). Opens through the window's own shell,
    // flying out of its toolbar control; a disabled control throws the button's reason.
    get windows() { return WINDOWS.map((w) => w.title); },
    openWindow(title) {
      const w = findWindow(title);
      if (!w) throw new Error(`stencil: no window called "${str(title)}" — one of: ${WINDOWS.map((x) => x.title).join(', ')}`);
      const doc = typeof document !== 'undefined' ? document : null;
      const ids = Array.isArray(w.opener) ? w.opener : [w.opener];
      const btn = doc && ids.map((id) => doc.getElementById(id)).find((el) => el && !el.hidden) || null;
      const shell = doc?.getElementById(w.overlay)?.__stencilModal;
      if (!btn || !shell) throw new Error(`stencil: the "${w.title}" window is not available here`);
      if (btn.disabled) throw new Error(`stencil: "${w.title}" is unavailable — ${btn.dataset?.disabledReason || 'its control is disabled'}`);
      if (!shell.isOpen()) shell.open(btn);
      return stencil;
    },
    // Closes whatever window is showing — the table's own shells first, then anything
    // else the shell registry knows (a confirm, the expiration prompt).
    closeWindow() {
      const doc = typeof document !== 'undefined' ? document : null;
      for (const w of WINDOWS) {
        const shell = doc?.getElementById(w.overlay)?.__stencilModal;
        if (shell?.isOpen()) shell.close();
      }
      closeOpenModal();
      return stencil;
    },
    // Which window is showing right now, by title (null when none).
    get openedWindow() {
      const doc = typeof document !== 'undefined' ? document : null;
      return WINDOWS.find((w) => doc?.getElementById(w.overlay)?.__stencilModal?.isOpen())?.title ?? null;
    },
    openProjectsWindow() { return stencil.openWindow('projects'); },
    openServersWindow() { return stencil.openWindow('servers'); },
    openConnectionsWindow() { return stencil.openWindow('servers'); },   // alias: the Servers window
    openLinksWindow() { return stencil.openWindow('links'); },
    openDescriptionWindow() { return stencil.openWindow('description'); },
    openKeywordsWindow() { return stencil.openWindow('keywords'); },
    openAssistantSettingsWindow() { return stencil.openWindow('assistant-settings'); },
    openShortcutsWindow() { return stencil.openWindow('shortcuts'); },
    openVisualsWindow() { return stencil.openWindow('visuals'); },
    openHelpWindow() { return stencil.openWindow('help'); },
    openImageWindow() { return stencil.openWindow('open-image'); },
    openCropWindow() { return stencil.openWindow('crop'); },

    // ── Editor actions (chainable) ──
    rotateLeft() { app.imageModel.rotateImage(-1); return stencil; },
    rotateRight() { app.imageModel.rotateImage(1); return stencil; },
    // Transform the SELECTED line about its bbox centre (same pivot as the per-line rotate) —
    // flip left↔right / top↔bottom, or rotate a quarter turn ±90. No selection is a no-op.
    flipH() { app.flipSelectedLine(true); return stencil; },
    flipV() { app.flipSelectedLine(false); return stencil; },
    rotate90() { app.rotateSelectedLineQuarter(1); return stencil; },
    rotateMinus90() { app.rotateSelectedLineQuarter(-1); return stencil; },
    undo() { app.undo(); return stencil; },
    redo() { app.redo(); return stencil; },
    startDrawing() { app.startDrawingMode(); return stencil; },
    stopDrawing() { app.stopDrawingMode(); return stencil; },
    // Point-adding mode as a get/set toggle (mirrors the Start/Stop drawing buttons).
    // Enabling needs a loaded image (matches the toolbar's guard).
    get drawing() { return !!app.isDrawing; },
    set drawing(on) {
      if (on) { if (app.image && !app.isDrawing) app.startDrawingMode(); }
      else if (app.isDrawing) app.stopDrawingMode();
    },
    // Hands-free voice chat (js/llm/voiceModes.js): listens with the chat closed and sends
    // every utterance as a turn. Turning it on stops any composer dictation.
    get voiceChat() { return !!app.voice?.voiceChat; },
    set voiceChat(on) {
      if (!app.voice) throw new Error('Voice input not ready — the editor UI has not wired yet');
      app.voice.voiceChat = !!on;
    },
    clearLines() { app.clearAllLines(); return stencil; },
    // variant: 'current' (default — tint + lines/points) | 'original' | 'tint' (tint only)
    // | 'split' (download only; needs a split compare view, divider baked in).
    downloadImage(variant = 'current') { app.export.saveImage(variant); return stencil; },
    copyLayout() { app.export.copyLayoutToClipboard(); return stencil; },
    // variant: 'current' | 'original' | 'tint' — see downloadImage. 'current' during a
    // split compare view copies the split composite shown on screen (no divider).
    copyImage(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; }, // alias of copyImageToClipboard
    copyImageToClipboard(variant = 'current') { app.export.copyImageToClipboard(variant); return stencil; },
    shareImage() { app.export.shareImage(); return stencil; },          // Web Share API (mobile/PWA)
    openIn() { document.getElementById('open-in-btn')?.click(); return stencil; },   // Open-in-another-app modal
    downloadLayout() { app.export.downloadJSON(); return stencil; },
    get layout() { return stencil.current?.layout; },
    // Accepts a layout OBJECT or a raw JSON string. Routes through the clipboard-paste
    // path, so an existing layout raises the Combine / Replace / Cancel prompt.
    set layout(data) { app.export.applyPastedLayout(toLayoutObject(data)); },
    // Apply a layout with no prompt or toast. `data` is an object or JSON string;
    // `mode:'combine'` adds on top of the current lines; `history:false` skips undo.
    //   stencil.applyLayout('{"lines":[…]}', { mode: 'combine' })
    applyLayout(data, opts = {}) {
      app.export.installLayout(toLayoutObject(data), opts);
      return stencil;
    },
    // Install lines directly — unlike `stencil.layout = …` (the paste path) this raises
    // no "Replace layout?" prompt and no toast. `history:false` keeps it out of undo.
    //   stencil.setLines([{ points: [{x:0,y:0},{x:10,y:10}], color: '#f00' }])
    setLines(lines, opts = {}) {
      const size = stencil.imageSize;
      const list = Array.isArray(lines) ? lines : [];
      app.export.installLayout(
        size ? { imageWidth: size.width, imageHeight: size.height, lines: list } : { lines: list },
        opts);
      return stencil;
    },

    // Save the whole project as a portable .stencil file (image + layout + metadata + optional
    // theme; `opts.includeTheme` default true embeds light/dark + accent). Resolves to the facade.
    saveProjectFile(opts = {}) { return app.export.saveProjectFile(opts).then(() => stencil); },
    // Open a .stencil project. Pass a File or the raw JSON text; omit to show a file picker.
    // Loads it as a fresh local project. Resolves to the facade.
    openProjectFile(fileOrText) {
      const p = fileOrText == null ? app.export.pickAndOpenProjectFile() : app.export.openProjectFile(fileOrText);
      return p.then(() => stencil);
    },
    // Live two-way sync of a file-linked project to its .stencil (Chromium only): toggle `liveSync`,
    // read `linkedFile` (name or null), `syncNow()` flushes a pending auto-save.
    get liveSync() { return app.stencilSync.supported && app.stencilSync.liveSync; },
    set liveSync(on) { app.stencilSync.liveSync = !!on; },
    get linkedFile() { return app.stencilSync.linked ? app.stencilSync.name : null; },
    syncNow() { return app.stencilSync.flush().then(() => stencil); },
    // Delete the linked .stencil file from disk (Chromium only); confirms first, then unlinks so
    // live-sync stops. The project stays open in the editor. Resolves to the facade.
    deleteProjectFile() { return app.export.deleteProjectFile().then(() => stencil); },

    // Pan the canvas viewport by pixel deltas (positive x → right, y → down).
    move({ x = 0, y = 0 } = {}) {
      const vp = document.getElementById('canvas-viewport');
      if (vp) { vp.scrollLeft += Number(x) || 0; vp.scrollTop += Number(y) || 0; }
      return stencil;
    },

    // Zoom by a relative step (0.25 in, -0.4 out). With `point` ({x,y} in image px) the
    // zoom keeps that point fixed on screen; otherwise it recentres.
    zoom(amount, point) {
      const next = app.zoomPan.clampScale((app.scale || 1) + Number(amount || 0));
      if (point && (point.x != null || point.y != null)) app.zoomPan.zoomToImagePoint(next, Number(point.x) || 0, Number(point.y) || 0);
      else app.zoomPan.zoomAroundCenter(next);
      return stencil;
    },
    // Absolute zoom as a percentage (mirrors the toolbar's zoom % input).
    get zoomLevel() { return Math.round((app.scale || 1) * 100); },
    set zoomLevel(pct) { app.zoomPan.zoomAroundCenter(app.zoomPan.clampScale((Number(pct) || 100) / 100)); },
    // Fit the image to the window (the toolbar's "fit" button).
    zoomFit() { app.zoomPan.fitToWindow(); return stencil; },
    // Start a fresh blank (unsaved) editor — the toolbar's clear/new. `opts.address` also
    // creates+links an empty project on that server, so a later save() writes back.
    newEditor(opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before resetting
      app.newEditor();
      if (address) return app.createRemoteBlank(address).then(() => stencil);
      return stencil;
    },
    // Create a solid-color blank image to draw on. `color` is any CSS color; opts.size =
    // { width, height } px; `opts.address` also creates+links it on that server.
    async blank(color = '#ffffff', opts = {}) {
      const size = opts.size || {};
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before replacing
      const blankOpts = { color, width: size.width, height: size.height };
      if (address) blankOpts.address = address;
      await app.createBlankImage(blankOpts);   // awaited: the swap is already done
      await waitForImage();
      return stencil;
    },
    // Save the session: a server-linked project writes back to its origin server
    // (version-guarded); a purely-local one flushes to storage. Resolves to the facade.
    save() {
      if (app.remoteLink) return app.remoteSync.saveToServer().then(() => stencil);
      app.storage.save();
      return stencil;
    },

    // Crop by axis edges: x1/y1/x2/y2 each a px move, an absolute length ('3cm'/'-4in'/
    // '50%'; '-' = from the axis end), or omitted. Commits via the UI's own applyCrop.
    // ONE axis alone derives the other's LENGTH from the page proportion (`album` picks the
    // orientation); `aspect` ('W:H') then SHRINKS one dimension about its centre to fit.
    // `{ scale }` instead grows/shrinks the current crop about its centre (the modal's
    // wheel/pinch) — mutually exclusive with the edge tokens.
    crop(spec = {}) {
      if (!app.originalImage) throw new Error('No image loaded to crop');
      const dims = app.imageModel.effectiveOriginalDims();   // { w, h } in rotated-original pixels
      const r = app.cropRect || app.imageModel.defaultCropRect();
      if (spec.scale != null) {
        const factor = Number(spec.scale);
        if (!(factor > 0)) throw new Error('crop scale must be a positive number');
        const aspect = r.height > 0 ? r.width / r.height : 1;
        const next = scaleCropCentered(r, factor, aspect, dims.w, dims.h);
        app.imageModel.applyCrop({ x: next.x, y: next.y, width: next.width, height: next.height }, { recalc: true });
        return stencil;
      }
      const ps = app.getPageDimensions();
      const pxPerCmX = app.canvas.width / ps.width, pxPerCmY = app.canvas.height / ps.height;
      const edge = (tok, cur, lengthPx, pxPerCm) =>
        tok == null ? cur : resolveAxisPx(tok, { lengthPx, pxPerCm, currentPx: cur });
      let x1 = edge(spec.x1, r.x, dims.w, pxPerCmX);
      let x2 = edge(spec.x2, r.x + r.width, dims.w, pxPerCmX);
      let y1 = edge(spec.y1, r.y, dims.h, pxPerCmY);
      let y2 = edge(spec.y2, r.y + r.height, dims.h, pxPerCmY);

      const xGiven = spec.x1 != null || spec.x2 != null;
      const yGiven = spec.y1 != null || spec.y2 != null;
      if (xGiven !== yGiven) {
        const aspect = cropAspect(ps.width, ps.height, !!spec.album);   // width / height
        if (xGiven) {                                  // have width → derive height
          y1 = r.y; y2 = r.y + Math.abs(x2 - x1) / aspect;
        } else {                                       // have height → derive width
          x1 = r.x; x2 = r.x + Math.abs(y2 - y1) * aspect;
        }
      }
      let rx = Math.min(x1, x2), ry = Math.min(y1, y2);
      let rw = Math.abs(x2 - x1), rh = Math.abs(y2 - y1);
      // Optional aspect fit (mirrors core resolveCropRect): shrink ONE dimension
      // symmetrically about the centre to hit W:H — never grow, never move the rect.
      if (spec.aspect != null) {
        const m = /^(\d+):(\d+)$/.exec(String(spec.aspect));
        const ratio = m && Number(m[1]) > 0 && Number(m[2]) > 0 ? Number(m[1]) / Number(m[2]) : 0;
        if (!(ratio > 0)) throw new Error('crop aspect must be "W:H" with positive integers');
        let w = rw, h = rh;
        if (h * ratio <= w) w = h * ratio;   // too wide → shrink the width
        else h = w / ratio;                  // too tall → shrink the height
        // Degenerate results keep at least 1px, but never grow past the resolved rect.
        w = Math.min(rw, Math.max(w, 1));
        h = Math.min(rh, Math.max(h, 1));
        rx += (rw - w) / 2; ry += (rh - h) / 2;
        rw = w; rh = h;
      }
      app.imageModel.applyCrop({ x: rx, y: ry, width: rw, height: rh }, { recalc: true });
      return stencil;
    },

    // px → page coords (cm, with active formulas applied).
    px2Page({ x = 0, y = 0 } = {}) { return app.pixelToPageCoords(Number(x), Number(y)); },
    // page (cm) → px. Inverts the linear page mapping; does NOT invert formulas.
    page2Px({ x = 0, y = 0 } = {}) {
      const ps = app.getPageDimensions();
      return { x: (Number(x) / ps.width) * app.canvas.width, y: (Number(y) / ps.height) * app.canvas.height };
    },

    // Bulk-apply from one object, then return the facade. Any settings key routes through
    // stencil.settings; plus showTooltip, fullscreen, incognito, zoom, crop, move, layout.
    apply(opts = {}) {
      const set = stencil.settings;
      for (const k of [
        'unit', 'lineColor', 'pointColor', 'pointSize', 'thickness', 'lineStyle',
        'pointStyle', 'showPoints', 'showLines', 'filter', 'filterColor', 'pageSize', 'drawMode',
        'allowFormulas', 'formulaX', 'formulaY', 'fillColor', 'selectionGlow', 'hoverRing', 'focusRing',
      ]) {
        if (opts[k] != null) set[k] = opts[k];
      }
      if (opts.page != null) set.pageSize = opts.page;            // `page` alias for pageSize
      if (opts.showTooltip != null) app.settings.setTooltipOption('enabled', opts.showTooltip);
      if (opts.tooltip && typeof opts.tooltip === 'object')
        for (const k of ['enabled', 'page', 'screen', 'coords'])
          if (opts.tooltip[k] != null) app.settings.setTooltipOption(k, opts.tooltip[k]);
      if (opts.fullscreen != null) stencil.fullscreen = opts.fullscreen;
      if (opts.incognito != null) stencil.incognito = opts.incognito;
      if (opts.zoom != null) stencil.zoom(opts.zoom);
      if (opts.layout != null) stencil.layout = opts.layout;
      if (opts.crop && typeof opts.crop === 'object') stencil.crop(opts.crop);
      if (opts.move && typeof opts.move === 'object') stencil.move(opts.move);
      return stencil;
    },

    // Load an image (or, with `frame`/a video URL, that frame) by URL. Resolves to the
    // facade, so `(await stencil.load(url)).crop(...)` chains. `incognito: true` adopts
    // incognito IN PLACE first — the outgoing project is flushed, the tab stays put.
    async load(url, opts = {}) {
      const address = opts.address || null;
      if (address) requireConnection(connMgr, address);   // validate before fetching
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`Failed to fetch ${url}: HTTP ${resp.status}`);
      const blob = await resp.blob();
      const type = blob.type || '';
      const baseName = opts.name || decodeURIComponent(url.split('/').pop().split(/[?#]/)[0] || '') || 'image';

      let file;
      if (type.startsWith('video/') || opts.frame != null || opts.usePoster) {
        // Grab a frame from the video (usePoster has no poster on a bare URL → ignored).
        const dataUrl = await videoFrameDataUrl(URL.createObjectURL(blob), Number(opts.frame) || 0);
        const fb = await (await fetch(dataUrl)).blob();
        file = new File([fb], baseName.replace(/\.[^.]+$/, '') + '.jpg', { type: 'image/jpeg' });
      } else if (type.startsWith('image/')) {
        file = new File([blob], baseName, { type });
      } else {
        throw new Error(`Not an image or video (got "${type || 'unknown'}")`);
      }

      const loadOpts = { source: opts.source ?? url, resource: opts.resource ?? '' };
      if (opts.crop) loadOpts.crop = opts.crop;
      if (address) loadOpts.address = address;   // create+link on that server after load
      const previous = app.image;
      // Adopt only once the bytes are in hand: a failed fetch must leave the editor
      // exactly as it was, never reset into an empty incognito session.
      if (opts.incognito) app.adoptIncognitoHere();
      app.loadImageFromFile(file, loadOpts);
      await waitForImage(8000, previous);
      return stencil;
    },
  };

  // Flatten the settings accessors onto the facade so `stencil.lineColor` works as well as
  // `stencil.settings.lineColor`. Copies the get/set descriptors, not values.
  Object.defineProperties(stencil, Object.getOwnPropertyDescriptors(settingsAccessors()));

  // Hide every member from enumeration so `console.log(stencil)` reads clean. Access and
  // autocomplete are unaffected; runs before the freeze, which locks descriptors.
  for (const k of Reflect.ownKeys(stencil)) {
    const d = Object.getOwnPropertyDescriptor(stencil, k);
    if (d.enumerable) Object.defineProperty(stencil, k, { ...d, enumerable: false });
  }
  // Freeze + hard-guard via the same proxy as every nested object. Tamper-resistance,
  // not security. Reassigning the ref makes every `return stencil` hand back the proxy.
  stencil = guard(stencil);
  setFacade(stencil);   // a removed Point falls back to the facade for chaining
  return stencil;
};
