// ── Background service worker ───────────────────────────────────────────────
// Owns the right-click context menu (Stencil actions → editor). Covers real <img>
// (native 'image' context) and CSS background-image elements (detected by the
// content-script probe, ctxTarget.js).
import { fetchAsDataUrl, isImageDataUrl, filenameFromUrl, openEditorTab, launchEditorModal, launchCrop, getSettings, blobToDataUrl, buildHandoff, editorOriginPattern, focusTab } from '../lib/stencil.js';
import { isEditorTab, editorRow, sourceTabChoices, importModeFor } from '../lib/editorTabs.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../lib/imageScan.js';
import { sourceOf, editableSrc } from '../lib/imageModel.js';
import { MENU, MENU_ITEMS, resolveContextAction, menuVisibilityFor, DYNAMIC_ITEMS, PREVIEW_ITEMS, PIN_ITEMS, STATIC_DESKTOP_ITEMS, pinItemTitle } from '../lib/contextMenu.js';
import { buildStencilSchemeUrl, INLINE_MAX_CHARS } from '../lib/openIn.js';
import { pruneLedger, recordOpened } from '../lib/ledger.js';
import { mountDropZones, unmountDropZones, mountDropChoice } from '../lib/dropZones.js';
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from '../lib/highlightColor.js';
import { THEME_STORAGE_KEY, THEME_MODES } from '../lib/shellTheme.js';
import { setPinned, loadPins, isPinnedIn, siteOf, PINS_KEY } from '../lib/pins.js';
import { isAllowedImageUrl } from '../lib/urlGuard.js';
import { MSG } from '../lib/messages.js';
import { applyAccentActionIcon, watchAccentActionIcon } from '../lib/actionIcon.js';

// Rebuild the menu from scratch; removeAll first avoids "duplicate id" on repeated
// builds. onInstalled/onStartup aren't reliable per reload, so this also runs at
// top level on each worker start (below).
const buildMenus = () => {
  chrome.contextMenus.removeAll(() => {
    for (const item of MENU_ITEMS) chrome.contextMenus.create(item, () => void chrome.runtime.lastError);
    console.info(`[stencil] context menu built: ${MENU_ITEMS.length} items`);
    // Reveal the desktop-app items only if a scheme is configured (they're created hidden).
    syncDesktopMenuVisibility();
  });
};

// Whether a desktop URL scheme is configured — gates the "Open in desktop app" menu items
// (they no-op without a scheme, so hide them). Cached for the synchronous CTX probe handler.
let desktopSchemeSet = true;

// Show/hide the desktop-app hand-off items to match the configured scheme. The STATIC items
// (image / video-frame) toggle on the scheme alone; MENU.bgDesktop is revealed by the probe
// (CTX handler) gated on this flag, so it isn't touched here.
const syncDesktopMenuVisibility = async () => {
  try { const { desktopScheme } = await getSettings(); desktopSchemeSet = !!desktopScheme; }
  catch { desktopSchemeSet = true; }
  for (const id of STATIC_DESKTOP_ITEMS)
    chrome.contextMenus.update(id, { visible: desktopSchemeSet }, () => void chrome.runtime.lastError);
};

// Build immediately on worker startup (covers reloads where onInstalled/onStartup
// don't fire); idempotent thanks to the removeAll above.
buildMenus();

// Declared/registered content scripts only inject into pages loaded AFTER
// install/update/registration — this covers the tabs already open. `scripts` is
// [{ file, world? }], injected in order into every tab matching `urlPatterns`.
const injectIntoOpenTabs = async (urlPatterns, scripts, { allFrames = false } = {}) => {
  try {
    const tabs = await chrome.tabs.query({ url: urlPatterns });
    for (const tab of tabs) {
      if (tab.id == null) continue;
      for (const s of scripts) {
        chrome.scripting.executeScript({
          target: { tabId: tab.id, ...(allFrames ? { allFrames: true } : {}) },
          ...(s.world ? { world: s.world } : {}),
          files: [s.file],
        }).catch(() => { /* restricted page / no access — ignore */ });
      }
    }
  } catch {
    /* ignore */
  }
};

// Inject the probe into already-open http(s) tabs, so the menu works without
// reloading every tab. The probe guards against binding twice (see ctxTarget.js).
const injectProbeIntoOpenTabs = () =>
  injectIntoOpenTabs(['http://*/*', 'https://*/*'], [{ file: 'src/content/ctxTarget.js' }], { allFrames: true });

// ── Editor bridge ───────────────────────────────────────────────────────────
// The editor is cross-origin, so its project registry is unreadable here. A content
// script injected only into the configured editor origin reads it (same-origin) and
// reports back to prune opened-ledger entries; registration follows editorUrl.
const BRIDGE_ID = 'stencil-editor-bridge';
const BRIDGE_FILE = 'src/content/editorBridge.js';

// Registration is unregister-then-register and several triggers fire it concurrently;
// interleaved, the second register throws `Duplicate script ID` and is lost, leaving a
// stale editorUrl registered. Serialised, passes run one at a time and the last wins.
let registrationQueue = Promise.resolve();
const serializeRegistration = (fn) => {
  const next = registrationQueue.then(fn, fn);   // run even if the previous pass rejected
  registrationQueue = next.catch(() => { /* keep the chain alive */ });
  return next;
};

// Register one content script, replacing any prior registration of the same id. A stray
// duplicate (left by an earlier worker generation) is cleared and retried once.
const replaceContentScript = async (id, script, label) => {
  try { await chrome.scripting.unregisterContentScripts({ ids: [id] }); } catch { /* not registered */ }
  try {
    await chrome.scripting.registerContentScripts([script]);
  } catch (e) {
    try {
      await chrome.scripting.unregisterContentScripts({ ids: [id] });
      await chrome.scripting.registerContentScripts([script]);
    } catch (again) {
      console.warn(`[stencil] could not register ${label}:`, again?.message || e?.message);
    }
  }
};

const registerEditorBridge = () => serializeRegistration(async () => {
  // Read the setting INSIDE the critical section: a pass queued before an editorUrl change
  // must still register the pattern that is current when it actually runs.
  const pattern = await editorOriginPattern();
  if (!pattern) {
    try { await chrome.scripting.unregisterContentScripts({ ids: [BRIDGE_ID] }); } catch { /* not registered */ }
    return;
  }
  await replaceContentScript(BRIDGE_ID, {
    id: BRIDGE_ID, js: [BRIDGE_FILE], matches: [pattern], runAt: 'document_start', allFrames: false
  }, 'editor bridge');
});

// Cover editor tabs open now.
const injectBridgeIntoOpenEditors = async () => {
  const pattern = await editorOriginPattern();
  if (!pattern) return;
  await injectIntoOpenTabs([pattern], [{ file: BRIDGE_FILE }]);
};

const setUpEditorBridge = () => { registerEditorBridge(); injectBridgeIntoOpenEditors(); };

// ── Page scripting API (opt-in window.stencil) ──────────────────────────────
// When enabled, inject a MAIN-world script defining window.stencil and an ISOLATED
// bridge relaying its action requests to this worker. Off by default; (un)registered
// as the setting flips.
const PAGE_API = [
  { id: 'stencil-page-bridge', file: 'src/content/pageApiBridge.js', world: 'ISOLATED', runAt: 'document_start' },
  { id: 'stencil-page-main', file: 'src/content/pageApiMain.js', world: 'MAIN', runAt: 'document_idle' },
];

const registerPageApi = () => serializeRegistration(async () => {
  const { exposeWindowStencil } = await getSettings();
  if (!exposeWindowStencil) {
    try { await chrome.scripting.unregisterContentScripts({ ids: PAGE_API.map((s) => s.id) }); } catch { /* not registered */ }
    return;
  }
  for (const s of PAGE_API) {
    await replaceContentScript(s.id, {
      id: s.id, js: [s.file], matches: ['<all_urls>'], runAt: s.runAt, allFrames: false, world: s.world,
    }, 'page API');
  }
});

// Cover already-open http(s) tabs so enabling the API works without a reload.
const injectPageApiIntoOpenTabs = async () => {
  const { exposeWindowStencil } = await getSettings();
  if (!exposeWindowStencil) return;
  await injectIntoOpenTabs(['http://*/*', 'https://*/*'], PAGE_API);
};

const setUpPageApi = () => { registerPageApi(); injectPageApiIntoOpenTabs(); };

// ── Editor page API (stencil.extension on the editor page) ──────────────────
// MAIN-world script defining window.__stencilExt. Like the page API above, but scoped
// to the configured editor origin only (why it defaults ON), and riding on both
// editorUrl and editorPageApi, so a change to either re-runs it.
const EDITOR_API_ID = 'stencil-editor-api-main';
const EDITOR_API_FILE = 'src/content/editorApiMain.js';

const registerEditorApi = () => serializeRegistration(async () => {
  // Settings read inside the critical section (see serializeRegistration): the toggle or the
  // editor URL may have changed while this pass was queued behind another.
  const { editorPageApi } = await getSettings();
  const pattern = editorPageApi ? await editorOriginPattern() : null;
  if (!pattern) {
    try { await chrome.scripting.unregisterContentScripts({ ids: [EDITOR_API_ID] }); } catch { /* not registered */ }
    return;
  }
  await replaceContentScript(EDITOR_API_ID, {
    id: EDITOR_API_ID, js: [EDITOR_API_FILE], matches: [pattern], runAt: 'document_idle', allFrames: false, world: 'MAIN'
  }, 'editor page API');
});

// Cover editor tabs that are already open, so enabling the API (or fixing the editor URL)
// works without reloading them.
const injectEditorApiIntoOpenEditors = async () => {
  const { editorPageApi } = await getSettings();
  if (!editorPageApi) return;
  const pattern = await editorOriginPattern();
  if (!pattern) return;
  await injectIntoOpenTabs([pattern], [{ file: EDITOR_API_FILE, world: 'MAIN' }]);
};

const setUpEditorApi = () => { registerEditorApi(); injectEditorApiIntoOpenEditors(); };

// React to settings changes: re-scope the editor bridge + editor page API (editorUrl),
// and toggle the page API (exposeWindowStencil) / the editor page API (editorPageApi).
chrome.storage.onChanged.addListener((changes, area) => {
  if (area !== 'sync') return;
  if (changes.editorUrl) setUpEditorBridge();
  if (changes.editorUrl || changes.editorPageApi) setUpEditorApi();
  if (changes.exposeWindowStencil) setUpPageApi();
  if (changes.desktopScheme) syncDesktopMenuVisibility();   // reveal/hide the desktop-app items
});

chrome.runtime.onInstalled.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
  setUpEditorApi();
  applyAccentActionIcon();
});
chrome.runtime.onStartup.addListener(() => {
  buildMenus();
  injectProbeIntoOpenTabs();
  setUpEditorBridge();
  setUpPageApi();
  setUpEditorApi();
  applyAccentActionIcon();
});

// Also set up on every worker start (onInstalled/onStartup don't fire on every wake).
setUpEditorBridge();
registerPageApi();   // re-asserts registration (injection into open tabs only on explicit toggle/startup)
registerEditorApi(); // …likewise for the editor page's stencil.extension
applyAccentActionIcon();   // tint the toolbar icon's outline to the saved accent
watchAccentActionIcon();   // …and re-tint it whenever the accent changes

// What the probe last resolved under the cursor, per tab (ready { url } for a
// background or captured frame). Needed because info.srcUrl is absent (backgrounds)
// or wrong (a <video>'s media file, not a frame).
const lastTargetByTab = new Map();
// Last right-click <video> target, per tab: { frameId, point, rect, dpr }. Lets the
// click handler recapture in-page and screenshot-crop the right rect for tainted media.
const lastVideoByTab = new Map();
// Poster URL of the last right-clicked <video>, per tab — drives the Preview submenu.
const lastPosterByTab = new Map();

// In-memory snapshot of the pinned store, kept fresh from storage. The context-menu
// probe relabels the Pin item (Pin ↔ Unpin) on right-click; that must be SYNCHRONOUS to
// beat the native menu appearing, so it reads this cache instead of awaiting loadPins().
let pinsCache = [];
const refreshPinsCache = async () => { try { pinsCache = await loadPins(); } catch { /* keep the last snapshot */ } };
refreshPinsCache();
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[PINS_KEY]) pinsCache = Array.isArray(changes[PINS_KEY].newValue) ? changes[PINS_KEY].newValue : [];
});
chrome.tabs.onRemoved.addListener((tabId) => {
  lastTargetByTab.delete(tabId);
  lastVideoByTab.delete(tabId);
  lastPosterByTab.delete(tabId);
});

// ── Editor mode: request/response plumbing ──────────────────────────────────
// What only chrome.* can answer for the panel and for `stencil.extension`: which editor tabs
// are open and what each holds, which other tabs are scannable, one tab's images, and an import
// INTO an open editor. Unlike every relay above these ANSWER — see `answers()`.

// How long a leg waiting on the editor PAGE gets before it's declared silent (an older editor
// build never replies). The fan-out is parallel, so listing N editors costs one deadline.
const PAGE_ANSWER_MS = 1500;
const NO_PAGE_ANSWER = 'the editor page did not answer';

// The import modes a caller may ask for. 'ask' is resolved HERE (query the target's
// state, then import or bounce back with needsChoice) and never reaches the editor page.
const IMPORT_MODES = ['new', 'replace', 'replace-keep', 'ask'];

// Wrap an async handler into a request/response one: returns true (holds the response
// port open) and answers EXACTLY once, turning a throw into the `{ ok:false, error }`
// object callers branch on — the panel awaits one reply and can't ask twice.
const answers = (fn) => (msg, sender, sendResponse) => {
  Promise.resolve()
    .then(() => fn(msg, sender))
    .catch((err) => ({ ok: false, error: err?.message || String(err) }))
    .then((res) => { try { sendResponse(res); } catch { /* port closed — the panel went away */ } });
  return true;
};

// chrome.tabs.get for a tab that may have closed (or an id that came in as data): null
// instead of a throw.
const getTab = async (tabId) => {
  try { return await chrome.tabs.get(tabId); } catch { return null; }
};

// ── Who may ask for the privileged, cross-tab handlers ──────────────────────
// These reach past the caller's own page (another tab's images, every open tab's URL).
// Our own pages are identified by ORIGIN, not by a missing `sender.tab` (popup.html can
// open as an ordinary tab); any other sender must be the editor origin with the API on.
const senderMayRelayPrivileged = async (sender) => {
  if (!sender) return false;
  const from = sender.url || '';
  if (from.startsWith(chrome.runtime.getURL(''))) return true;   // popup / side panel / options
  const { editorUrl, editorPageApi } = await getSettings();
  if (!editorPageApi) return false;
  return isEditorTab(from || sender.tab?.url || '', editorUrl);
};

// Composed inside answers(): answers(privileged(fn)).
const privileged = (fn) => async (msg, sender) => {
  if (!(await senderMayRelayPrivileged(sender))) {
    return { ok: false, error: 'this request is not allowed from a page' };
  }
  return fn(msg, sender);
};

// Ask ONE editor tab's bridge something, always returning a reply object: no receiver and a
// silent page both become the same structured error, never a hang.
const askEditorTab = async (tabId, message) => {
  try {
    const reply = await Promise.race([
      chrome.tabs.sendMessage(tabId, message),
      new Promise((resolve) => setTimeout(resolve, PAGE_ANSWER_MS)),
    ]);
    return reply && typeof reply === 'object' ? reply : { ok: false, error: NO_PAGE_ANSWER };
  } catch {
    return { ok: false, error: NO_PAGE_ANSWER };
  }
};

// One parallel EDITOR_STATE fan-out over candidate tabs: each tab's bridge is asked for
// its state (a null id yields null). The canvas capture is the expensive part of a state
// reply, so `thumbnail` defaults off; callers that want previews say so.
const probeEditorTabs = (tabs, { thumbnail = false } = {}) =>
  Promise.all(tabs.map((tab) => (tab.id == null
    ? null
    : askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail }))));

// Which tab a message means: an explicit tabId (the panel picked one), else the sender's —
// for the editor page's own API, "no tabId" means "the tab I'm standing in".
const targetTabId = (msg, sender) => (typeof msg.tabId === 'number' ? msg.tabId : sender?.tab?.id);

// The destination editor tab: it must still exist AND still be on the editor origin (a tab
// listed a moment ago may have navigated away). Returns `{tab}` or `{error}`.
const editorTabFor = async (msg, sender) => {
  const tabId = targetTabId(msg, sender);
  if (typeof tabId !== 'number') return { error: 'no tab' };
  const tab = await getTab(tabId);
  if (!tab) return { error: 'no such tab' };
  const { editorUrl } = await getSettings();
  if (!isEditorTab(tab.url || '', editorUrl)) return { error: 'tab is not a Stencil editor' };
  return { tab };
};

// ── Runtime-message dispatch ────────────────────────────────────────────────
// One handler per message `type` (keyed by MSG.*). Two kinds: fire-and-forget handlers
// return undefined (port closes), request/response ones (the editor-mode group) are
// wrapped in `answers()` and return true. The listener just propagates that.
const messageHandlers = {
  // The in-page editor overlay asks us to open a real tab when its iframe is
  // blocked (CSP / mixed content). Doing it here avoids popup blockers.
  [MSG.OPEN_TAB]: (msg) => {
    if (msg.url) chrome.tabs.create({ url: msg.url });
  },
  // The DevTools panel's Settings gear: its context lacks
  // chrome.runtime.openOptionsPage, so the panel asks us to open it.
  [MSG.OPEN_OPTIONS]: () => {
    chrome.runtime.openOptionsPage();
  },
  // The editor-origin bridge reports the editor's live project registry. Prune
  // opened-ledger entries for projects that no longer exist there — scoped to the
  // sender's origin so other editor deployments are left untouched.
  [MSG.REGISTRY]: (msg, sender) => {
    let origin = sender.origin || '';
    if (!origin && sender.url) { try { origin = new URL(sender.url).origin; } catch { origin = ''; } }
    if (origin) pruneLedger(Array.isArray(msg.projects) ? msg.projects : [], origin);
  },
  // ── Page-API (window.stencil) relays ──
  // Open a page image/video in the editor (new tab or in-page modal).
  [MSG.PAGE_OPEN]: (msg, sender) => {
    (async () => {
      try {
        // A caller-supplied dataUrl skips fetchAsDataUrl's allowlist — check it here.
        if (msg.dataUrl && !isImageDataUrl(msg.dataUrl)) throw new Error('dataUrl is not an image');
        // Guard context is sender.tab.url (browser-set) — never msg.resource, which the
        // page could forge to smuggle a private host through the same-host carve-out.
        const dataUrl = msg.dataUrl || await fetchAsDataUrl(msg.url, { pageUrl: sender.tab?.url || '' });
        const { page, desktopScheme } = await getSettings();
        // Desktop hand-off: build the stencil:// scheme URL and let the OS open the app
        // (parity with the context-menu "Open in… Desktop app"), instead of the editor tab.
        if (msg.desktop) {
          if (!desktopScheme) { console.warn('[stencil] open({desktop}) needs a configured desktop scheme'); return; }
          const schemeUrl = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
          if (schemeUrl.length > INLINE_MAX_CHARS) { console.warn('[stencil] image too large for an inline desktop hand-off'); return; }
          chrome.tabs.create({ url: schemeUrl });
          return;
        }
        const payload = buildHandoff(
          { name: msg.name || filenameFromUrl(msg.url || 'image'), source: msg.source || msg.url || '' },
          { dataUrl, page, resource: msg.resource || sender.tab?.url || '', incognito: !!msg.incognito }
        );
        if (msg.newTab) await openEditorTab(payload);
        else await launchEditorModal({ ...payload, tabId: sender.tab?.id });
      } catch (err) { console.warn('[stencil] page open failed:', err?.message); }
    })();
  },
  // Pin / unpin a page image/video. The pin is grouped under the page's origin so the
  // options page can list "pins on this site"; the bridge mirrors the write back to the
  // page API (entry.pinned) and any open popup/side panel.
  [MSG.PAGE_PIN]: (msg, sender) => {
    // sender.tab.url first: the recorded resource later serves as same-host guard
    // context (options pin thumbnails), so it must not be page-forgeable.
    const resource = sender.tab?.url || msg.resource || '';
    setPinned({
      source: msg.source || msg.url || '', site: siteOf(resource), resource,
      name: msg.name || filenameFromUrl(msg.url || 'image'), kind: msg.kind || 'image', pinned: !!msg.pin,
    }).catch(() => { /* storage unavailable */ });
  },
  // Open a page image/video in the quick-crop tool.
  [MSG.PAGE_CROP]: (msg, sender) => {
    // Same rule as PAGE_OPEN; a `url` is left to the crop page's own allowlist.
    if (msg.dataUrl && !isImageDataUrl(msg.dataUrl)) return;
    const src = msg.dataUrl || msg.url;
    // sender.tab.url first: the crop page uses this resource as same-host guard context.
    if (src) launchCrop({ src, source: msg.source || msg.url || '', resource: sender.tab?.url || msg.resource || '', tabId: sender.tab?.id });
  },
  // The API's `stencil.enabled = false` — turn the feature off (unregisters the scripts).
  [MSG.PAGE_DISABLE]: () => {
    chrome.storage.sync.set({ exposeWindowStencil: false });
  },
  // A row drag started in the panel → inject the on-page 4-quadrant drop overlay on that tab,
  // tinted to the current theme accent (resolved from the saved accent key, so the zones match
  // the extension's theme rather than a fixed violet).
  [MSG.DROPZONES_ARM]: (msg) => {
    if (msg.tabId == null) return;
    (async () => {
      let accent = DEFAULT_HL;
      // The Appearance choice rides along UNRESOLVED: 'system' can only be answered by
      // the page the zones land on (lib/shellTheme.js makes the same hand-off).
      let mode = 'system';
      try {
        const l = await chrome.storage.local.get([ACCENT_STORAGE_KEY, THEME_STORAGE_KEY]);
        accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL;
        if (THEME_MODES.includes(l[THEME_STORAGE_KEY])) mode = l[THEME_STORAGE_KEY];
      } catch { /* defaults */ }
      // A live-editor tab gets editor-aware labels (here/incognito/crop act on IT).
      const probe = await askEditorTab(msg.tabId, { type: MSG.EDITOR_STATE, thumbnail: false });
      chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: mountDropZones, args: [accent, !!probe.ok, mode] })
        .catch(() => { /* restricted page — no overlay */ });
    })();
  },
  // The row drag ended without a page drop → tear the overlay down (backstop; it also
  // self-removes on drop / leaving the window / Escape / timeout).
  [MSG.DROPZONES_DISARM]: (msg) => {
    if (msg.tabId == null) return;
    chrome.scripting.executeScript({ target: { tabId: msg.tabId }, world: 'ISOLATED', func: unmountDropZones })
      .catch(() => { /* restricted page — nothing to remove */ });
  },
  // A row was dropped in a quadrant of the on-page overlay → run its action. Reuses the same
  // hand-off machinery as the page API relays (fetch bytes → buildHandoff → open/crop).
  [MSG.PAGE_DROP]: (msg, sender) => {
    (async () => {
      try {
        const { url, action } = msg;
        if (!url) return;
        if (action === 'newtab') { chrome.tabs.create({ url }); return; }
        const tabId = sender.tab?.id;
        const resource = sender.tab?.url || '';
        const name = filenameFromUrl(url);
        const { page, editorUrl } = await getSettings();
        // ── Editor-aware: a drop landing ON a live editor acts on THAT editor ──
        // here/incognito import into it (straight in when it's empty; an occupied
        // editor raises the injected replace/new-tab/cancel chooser), and crop
        // imports then opens the editor's OWN crop dialog — never the crop page.
        const probe = tabId != null ? await askEditorTab(tabId, { type: MSG.EDITOR_STATE, thumbnail: false }) : { ok: false };
        if (probe.ok) {
          let mode = 'new';
          // Incognito is never persisted and the saved project stays put, so it
          // needs no replace chooser — it just opens incognito in this editor.
          if (action !== 'incognito' && probe.state?.hasImage) {
            let accent = DEFAULT_HL;
            try { const l = await chrome.storage.local.get(ACCENT_STORAGE_KEY); accent = ACCENT_HEX[l[ACCENT_STORAGE_KEY]] || DEFAULT_HL; } catch { /* default */ }
            const [res] = await chrome.scripting.executeScript({
              target: { tabId }, world: 'ISOLATED', func: mountDropChoice, args: [accent],
            }).catch(() => [null]);
            const choice = res?.result || 'cancel';
            if (choice === 'cancel') return;
            if (choice === 'newtab') {
              const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
              await openEditorTab(buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' }));
              return;   // crop-in-a-new-tab has no editor to host the dialog — plain open
            }
            mode = 'replace';
          }
          const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
          const payload = buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' });
          const reply = await askEditorTab(tabId, { type: MSG.EDITOR_IMPORT, payload, mode });
          if (!reply.ok) return;
          if (!payload.incognito) {
            await recordOpened({ source: url, resource, name, editorUrl }).catch(() => { /* badges just won't show */ });
          }
          if (action === 'crop') await askEditorTab(tabId, { type: MSG.EDITOR_CROP });
          return;
        }
        if (action === 'crop') {
          const src = await fetchAsDataUrl(url, { pageUrl: resource }).catch(() => url);   // video/non-image → let the crop page report it
          await launchCrop({ src, source: url, resource, tabId });
          return;
        }
        // here / incognito → open the editor with the image bytes.
        const dataUrl = await fetchAsDataUrl(url, { pageUrl: resource });
        const payload = buildHandoff({ name, source: url }, { dataUrl, page, resource, incognito: action === 'incognito' });
        if (action === 'incognito') await openEditorTab(payload);   // incognito = a fresh incognito editor tab
        else await launchEditorModal({ ...payload, tabId });        // here = in-page editor modal
      } catch (err) { console.warn('[stencil] page drop failed:', err?.message); }
    })();
  },
  // ctxTarget probe → what the right-click resolved under the cursor. Records the target
  // per tab and toggles the dynamic background / video-preview menu groups' visibility.
  [MSG.CTX]: (msg, sender) => {
    const tabId = sender.tab?.id;
    if (tabId == null) return;
    const data = msg.data;
    // Remember the video context so the click handler can recapture in-page.
    lastVideoByTab.set(tabId, data && data.video
      ? { frameId: sender.frameId, point: msg.point || null, rect: data.rect || null, dpr: data.dpr || 1, posterShown: !!data.posterShown }
      : null);
    // A tainted (cross-origin) video has no ready frame here; its CURRENT frame is
    // captured at click time (in-page CORS readback → extension byte re-fetch →
    // screenshot crop), so record nothing and let the click handler do the work.
    lastTargetByTab.set(tabId, (data && data.video && !data.url) ? null : (data || null));
    // The poster (preview image) the probe saw, if any — drives the Preview submenu.
    lastPosterByTab.set(tabId, (data && data.poster) ? data.poster : '');
    // Reveal dynamic background/link items only when the probe found a plain image URL
    // (<img>/<video> use native-context items). The group includes its own root, so the
    // "Stencil" entry appears only with items under it — never as an empty submenu.
    const { bg: showBg, preview: showPreview } = menuVisibilityFor(data);
    for (const id of DYNAMIC_ITEMS)
      chrome.contextMenus.update(id, { visible: showBg }, () => void chrome.runtime.lastError);
    // The background "Open in desktop app" item needs BOTH a background under the cursor AND a
    // configured scheme (unlike the rest of the bg group, which only needs the background).
    chrome.contextMenus.update(MENU.bgDesktop, { visible: showBg && desktopSchemeSet }, () => void chrome.runtime.lastError);
    // Reveal the video Preview submenu only when the probed <video> has a poster —
    // otherwise its actions would be silent no-ops.
    for (const id of PREVIEW_ITEMS)
      chrome.contextMenus.update(id, { visible: showPreview }, () => void chrome.runtime.lastError);
    // Relabel the pin item (Pin ↔ Unpin) SYNCHRONOUSLY off the in-memory pins cache — an
    // awaited storage read loses the race against the native menu appearing. Best-effort:
    // a first right-click just after the worker wakes may be stale.
    const site = siteOf(sender.tab?.url || '');
    const relabel = (id, source, kind) => {
      if (!source) return;
      chrome.contextMenus.update(id, { title: pinItemTitle(isPinnedIn(pinsCache, site, source), kind) },
        () => void chrome.runtime.lastError);
    };
    relabel(MENU.pin, data && data.imgUrl, 'image');
    relabel(MENU.bgPin, data && !data.video && data.url, 'image');
    relabel(MENU.framePin, data && data.video && (data.videoUrl || data.poster), 'video');
  },
  // ── Editor mode (request/response — every one of these answers) ──
  // Every open editor tab, each joined with the live state its bridge reports. A tab whose
  // bridge stays silent (old editor build, still loading, no extensionBridge.js) is listed
  // ANYWAY with ready:false — dropping it would hide an editor tab the user is looking at.
  [MSG.EDITOR_LIST]: answers(privileged(async (msg, sender) => {
    const pattern = await editorOriginPattern();
    if (!pattern) return { ok: false, error: 'no editor URL configured' };
    const tabs = await chrome.tabs.query({ url: [pattern] });
    const currentTabId = targetTabId(msg, sender);
    // A poll refresh asks for rows without thumbnails (thumbnails:false) and keeps the
    // previews it already has.
    const states = await probeEditorTabs(tabs, { thumbnail: msg.thumbnails !== false });
    const rows = tabs.map((tab, i) => editorRow(tab, states[i] && states[i].ok ? states[i].state : null, { currentTabId }));
    return {
      ok: true,
      // Origin matching can't tell the editor from an ordinary page served beside it —
      // those answer nothing and belong in SOURCE_TABS, so drop them. A tab still LOADING
      // has simply not answered yet, so it stays: the panel's poll fills it in a moment.
      editors: rows.filter((row, i) => row.ready || (tabs[i] && tabs[i].status === 'loading')),
    };
  })),
  // The other open pages an image can be pulled from (editor tabs and un-scannable
  // schemes filtered out by the shared helper, so the panel and the console agree).
  [MSG.SOURCE_TABS]: answers(privileged(async (msg) => {
    const { editorUrl } = await getSettings();
    const tabs = await chrome.tabs.query(msg.currentWindowOnly ? { currentWindow: true } : {});
    // Ask the editor-origin tabs which are REALLY editors, so an ordinary page on that
    // origin is still offered as a source (see sourceTabChoices). Only same-origin tabs
    // are asked, without thumbnails — a couple of cheap round-trips, not a full fan-out.
    const pattern = await editorOriginPattern();
    const sameOrigin = pattern ? tabs.filter((t) => t.id != null && isEditorTab(t.url || '', editorUrl)) : [];
    const answered = await probeEditorTabs(sameOrigin);
    const editorTabIds = sameOrigin.filter((_, i) => answered[i] && answered[i].ok).map((t) => t.id);
    return { ok: true, tabs: sourceTabChoices(tabs, { editorUrl, editorTabIds }) };
  })),
  // Scan a tab the caller is NOT standing on, with the popup's own scanner (same
  // all-frames executeScript + mergeScanFrames), so both surfaces see the same images.
  // Items come back raw: naming and filtering stay in the panel, as in popup.js scan().
  [MSG.SCAN_TAB]: answers(privileged(async (msg) => {
    if (typeof msg.tabId !== 'number') return { ok: false, error: 'no tab' };
    const tab = await getTab(msg.tabId);
    if (!tab) return { ok: false, error: 'no such tab' };
    const url = tab.url || '';
    if (!url || BLOCKED_SCHEMES.some((s) => url.startsWith(s))) return { ok: false, error: 'this page can’t be scanned' };
    // A caller-supplied limit is data: clamped to the scanner's own ceiling.
    const limit = Number(msg.limit) > 0 ? Math.min(Math.trunc(Number(msg.limit)), MAX_IMAGES) : MAX_IMAGES;
    try {
      const results = await chrome.scripting.executeScript({
        target: { tabId: msg.tabId, allFrames: true }, func: scanPageForImages, args: [limit]
      });
      // The scanned tab's URL rides back with the images: it's the `resource` (provenance)
      // a later EDITOR_IMPORT needs, and the caller isn't on that tab to look it up.
      return { ok: true, tabId: msg.tabId, url, images: mergeScanFrames(results, limit) };
    } catch (err) {
      return { ok: false, error: `could not read this page (${err?.message || err})` };
    }
  })),
  // Import an image INTO an already-open editor tab — no new tab, no navigation. Bytes are
  // resolved here (host permissions bypass page CORS) and handed to the tab's bridge as the
  // same buildHandoff payload the #stencil= launch path carries.
  [MSG.EDITOR_IMPORT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    // Mode is caller data: whitelist it before anything is fetched or replaced. A missing
    // mode means 'ask' — the one value that can never overwrite work on screen unasked.
    let mode = msg.mode || 'ask';
    if (!IMPORT_MODES.includes(mode)) return { ok: false, error: 'unknown import mode' };
    if (mode === 'ask') {
      // Only one boolean of the state matters here (hasImage), so skip the canvas capture:
      // a full state reply carries a 256px JPEG data URL across three hops, and this pre-check
      // runs on EVERY import — including the second one, after the chooser answers.
      const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: false });
      const state = reply.ok ? reply.state : null;
      // Occupied editor → don't import; hand the state back so the panel can raise the
      // chooser (new project / replace / replace keeping annotations) and the console API
      // can say why nothing happened. A silent bridge reads as blank → 'new'.
      if (importModeFor(state) === 'ask')
        return { ok: false, error: 'editor already holds an image', needsChoice: true, state };
      mode = 'new';
    }
    const image = (msg.image && typeof msg.image === 'object') ? msg.image : {};
    // Bytes from the openable source (a video row's captured still, an image's src),
    // provenance from sourceOf via buildHandoff — the split popup.js already makes.
    // fetchAsDataUrl enforces the http(s)/blob/data allowlist on this page-supplied URL.
    const src = editableSrc(image) || sourceOf(image);
    // Guard context: a content-script sender lends its own (browser-set) tab URL; an
    // extension surface (no sender.tab) is trusted to name the scanned page itself.
    const dataUrl = await fetchAsDataUrl(src || '', { pageUrl: sender.tab?.url || msg.resource || '' });   // '' / a bad scheme → 'unsupported URL scheme'
    const { page, editorUrl } = await getSettings();
    const payload = buildHandoff(
      { ...image, name: image.name || filenameFromUrl(src) },
      { dataUrl, page: msg.page || page, resource: msg.resource || '', incognito: !!msg.incognito }
    );
    // Crop rect (original-image pixels) is forwarded untouched; the editor applies it the
    // same way it does for a cropped launch payload.
    if (msg.crop) payload.crop = msg.crop;
    const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_IMPORT, payload, mode });
    if (!reply.ok) return reply;
    // Write the same opened-ledger entry every other hand-off does — without it the panel
    // wouldn't badge the row and a second click would import a duplicate. Skipped for
    // incognito and best-effort: a ledger write must not fail an import that LANDED.
    if (!payload.incognito) {
      await recordOpened({ source: payload.source, resource: payload.resource, name: payload.name, editorUrl })
        .catch(() => { /* storage unavailable — badges just won't show */ });
    }
    return { ok: true, tabId: tab.id, mode, projectId: reply.projectId || '', projectName: reply.projectName || '' };
  }),
  // Switch an editor tab to one of ITS OWN projects (by id — the fire-and-forget
  // EDITOR_SWITCH above matches by source URL instead). The page refuses an unknown id
  // rather than clearing itself, and its reply is passed straight through.
  [MSG.EDITOR_SWITCH_PROJECT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_SWITCH_PROJECT, projectId: String(msg.projectId || '') });
  }),
  // ONE tab's editor state. The panel asks this before flipping into editor mode: an origin
  // match alone also matches ordinary pages served beside the editor, and only the tab's own
  // bridge answering proves it IS one (popup.js resolveScanTab → editorMode.isLiveEditor).
  [MSG.EDITOR_STATE]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: msg.thumbnail !== false, thumbMax: msg.thumbMax });
  }),
  // Bring a tab to the front (select it AND raise its window — focusTab, the same pair
  // resumeInOpenEditor uses). Not restricted to editor tabs: the source-tab picker offers
  // "go look at that page" too.
  [MSG.EDITOR_FOCUS_TAB]: answers(privileged(async (msg) => {
    if (typeof msg.tabId !== 'number') return { ok: false, error: 'no tab' };
    const tab = await getTab(msg.tabId);
    if (!tab) return { ok: false, error: 'no such tab' };
    try {
      await focusTab(tab);
    } catch {
      return { ok: false, error: 'no such tab' };   // closed between the get and the update
    }
    return { ok: true, tabId: tab.id, windowId: tab.windowId != null ? tab.windowId : null };
  })),
};

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const handler = msg && messageHandlers[msg.type];
  // Propagating the return value is what keeps the request/response handlers working: an
  // `answers()`-wrapped one returns true and replies later, a fire-and-forget one returns
  // undefined and the port closes as before.
  if (handler) return handler(msg, sender, sendResponse);
});

// Cap the captured frame's longest side: it rides in the editor launch URL as a
// data URL, and an un-capped retina crop overflows Chrome's URL limit (about:blank).
const FRAME_MAX_SIDE = 1920;

// Crop a tab screenshot to the video's on-screen rectangle. captureVisibleTab
// returns an extension-owned (never tainted) image; downscale + JPEG-encode so the
// data URL stays small enough for the launch URL.
const captureFrameFromScreenshot = async (windowId, rect, dpr = 1) => {
  const shot = await chrome.tabs.captureVisibleTab(windowId, { format: 'png' });
  const bitmap = await createImageBitmap(await (await fetch(shot)).blob());
  const sx = Math.max(0, rect.x * dpr), sy = Math.max(0, rect.y * dpr);
  const sw = Math.max(1, Math.round(rect.width * dpr)), sh = Math.max(1, Math.round(rect.height * dpr));
  const s = Math.min(1, FRAME_MAX_SIDE / Math.max(sw, sh));
  const dw = Math.max(1, Math.round(sw * s)), dh = Math.max(1, Math.round(sh * s));
  const canvas = new OffscreenCanvas(dw, dh);
  canvas.getContext('2d').drawImage(bitmap, sx, sy, sw, sh, 0, 0, dw, dh);
  return blobToDataUrl(await canvas.convertToBlob({ type: 'image/jpeg', quality: 0.92 }));
};

// Capture a <video>'s current frame by in-page canvas readback at click time: direct
// draw (same-origin), then a fresh crossOrigin="anonymous" video at the same src/time
// (works when the CDN serves CORS though the page's <video> is tainted). Returns
// { frame } (JPEG data URL), { src, t } (tainted; caller re-fetches), or null.
const captureVideoFrameInTab = async (tabId, frameId, point) => {
  if (tabId == null) return null;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try {
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [point ? point.x : null, point ? point.y : null, FRAME_MAX_SIDE],
      func: async (px, py, maxSide) => {
        // Smallest <video> whose box contains the cursor — spatially correct even under
        // an overlay, and (unlike querySelector('video') on an ancestor) never jumps to
        // another video.
        const at = (x, y) => {
          if (x == null) return null;
          let best = null, bestArea = Infinity;
          for (const v of document.querySelectorAll('video')) {
            const r = v.getBoundingClientRect();
            if (r.width && r.height && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
              const a = r.width * r.height;
              if (a < bestArea) { bestArea = a; best = v; }
            }
          }
          if (best) return best;
          if (document.elementsFromPoint) for (const el of document.elementsFromPoint(x, y)) if (el.tagName === 'VIDEO') return el;
          return null;
        };
        const largest = () => {
          let best = null, area = -1;
          for (const v of document.querySelectorAll('video')) {
            const a = (v.videoWidth || 0) * (v.videoHeight || 0);
            if (v.readyState >= 2 && a > area) { area = a; best = v; }
          }
          return best;
        };
        const draw = (video) => {
          const s = Math.min(1, maxSide / Math.max(video.videoWidth, video.videoHeight));
          const w = Math.max(1, Math.round(video.videoWidth * s)), h = Math.max(1, Math.round(video.videoHeight * s));
          const c = document.createElement('canvas');
          c.width = w; c.height = h;
          c.getContext('2d').drawImage(video, 0, 0, w, h);
          return c.toDataURL('image/jpeg', 0.92);
        };
        // With a cursor point use only the video under it; largest() is the no-point
        // fallback (Chrome video context without a probe point).
        const v = px != null ? at(px, py) : largest();
        if (!v || !v.videoWidth || !v.videoHeight || v.readyState < 2) return null;
        // Paused at the very start → the poster is showing, not a real frame; let
        // the caller fall back to the poster instead of grabbing a black frame 0.
        if (v.paused && !v.currentTime) return null;
        try {
          return { frame: draw(v) };
        } catch {
          /* tainted — try CORS below */
        }
        const src = v.currentSrc || v.src || '';
        const t = v.currentTime || 0;
        if (!src) return null;
        const frame = await new Promise((resolve) => {
          const nv = document.createElement('video');
          nv.crossOrigin = 'anonymous'; nv.muted = true; nv.preload = 'auto'; nv.src = src;
          let done = false;
          const fin = (x) => { if (!done) { done = true; resolve(x); } };
          nv.addEventListener('loadeddata', () => {
            try {
              nv.currentTime = Math.min(t, Math.max(0, (nv.duration || t) - 0.01));
            } catch {
              fin(null);
            }
          });
          nv.addEventListener('seeked', () => {
            try {
              fin(draw(nv));
            } catch {
              fin(null);
            }
          });
          nv.addEventListener('error', () => fin(null));
          setTimeout(() => fin(null), 8000);
        });
        return frame ? { frame } : { src, t };
      }
    });
    return (res && res.result) || null;
  } catch {
    return null;
  }
};

// Current-frame capture for a tainted, non-CORS video: fetch bytes with the extension's
// host permissions (bypasses page CORS), ship them into the page as a blob URL, draw
// the frame at the recorded time. Skips huge media (caller falls back to a screenshot
// crop). Returns a JPEG data URL or null.
const captureVideoFrameViaFetch = async (tabId, frameId, src, t, pageUrl = '') => {
  if (tabId == null || !src) return null;
  try {
    // `src` is page-derived — refuse private/internal targets (urlGuard.js), like
    // any other unfetchable source: the caller falls back to a screenshot crop.
    // The tab's own host (pageUrl = tab.url) is allowed through.
    if (!isAllowedImageUrl(src, { allowSameHostAs: pageUrl })) return null;
    const resp = await fetch(src);
    if (!resp.ok) return null;
    const clen = Number(resp.headers.get('content-length') || 0);
    if (clen && clen > 25_000_000) return null;
    const dataUrl = await blobToDataUrl(await resp.blob());
    const target = { tabId };
    if (frameId != null) target.frameIds = [frameId];
    const [res] = await chrome.scripting.executeScript({
      target,
      args: [dataUrl, t || 0, FRAME_MAX_SIDE],
      func: async (durl, time, maxSide) => {
        const url = URL.createObjectURL(await (await fetch(durl)).blob());
        return await new Promise((resolve) => {
          const v = document.createElement('video');
          v.muted = true; v.preload = 'auto'; v.src = url;
          let done = false;
          const fin = (x) => { if (!done) { done = true; URL.revokeObjectURL(url); resolve(x); } };
          v.addEventListener('loadeddata', () => {
            try {
              v.currentTime = Math.min(time, Math.max(0, (v.duration || time) - 0.01));
            } catch {
              fin(null);
            }
          });
          v.addEventListener('seeked', () => {
            try {
              const s = Math.min(1, maxSide / Math.max(v.videoWidth, v.videoHeight));
              const w = Math.max(1, Math.round(v.videoWidth * s)), h = Math.max(1, Math.round(v.videoHeight * s));
              const c = document.createElement('canvas');
              c.width = w; c.height = h;
              c.getContext('2d').drawImage(v, 0, 0, w, h);
              fin(c.toDataURL('image/jpeg', 0.92));
            } catch {
              fin(null);
            }
          });
          v.addEventListener('error', () => fin(null));
          setTimeout(() => fin(null), 8000);
        });
      }
    });
    return (res && res.result) || null;
  } catch {
    return null;
  }
};

// Resolve the image source for a click. A probe-captured video frame (rec.video) wins:
// for a <video> info.srcUrl is the media file (not a frame) and Chrome doesn't always
// report mediaType:'video'. Otherwise <img>/<svg> use info.srcUrl, backgrounds use rec.
const resolveSrc = (info, rec) => {
  if (rec && rec.video && rec.url) return rec.url;
  if (info.mediaType === 'video' || info.mediaType === 'audio') return (rec && rec.url) || null;
  if (info.srcUrl) return info.srcUrl;
  return (rec && rec.url) || null;
};

// ── Context-menu click handlers ─────────────────────────────────────────────
// One async handler per click group: the toolbar action items, the video-preview
// submenu, the pin items, and the default image / video-frame path.
// `resolveClickHandler` routes an incoming click to exactly one of them, in order.

// ── Toolbar-icon menu: open a fresh Stencil editor (no image). The incognito variant
// opens it in an incognito window, so the editor's own project storage is throwaway. ──
const openFreshEditor = async (info) => {
  try {
    const { editorUrl } = await getSettings();
    if (info.menuItemId === MENU.actionOpenIncognito) await chrome.windows.create({ url: editorUrl, incognito: true });
    else await chrome.tabs.create({ url: editorUrl });
  } catch (err) {
    console.error('[stencil] open-editor action failed:', err);
  }
};

// ── Preview submenu: act on the video's POSTER (a normal image URL), never a
// frame. A no-op when the right-clicked element had no poster. ──
const actOnPreview = async (info, tab, tabId) => {
  const poster = (tabId != null ? lastPosterByTab.get(tabId) : '') || '';
  const act = resolveContextAction({ menuItemId: info.menuItemId, srcUrl: poster }, poster);
  if (!act) return;   // no poster on this element
  const resource = tab?.url || '';
  try {
    if (act.action === 'open-tab') {
      await chrome.tabs.create({ url: act.src });
      return;
    }
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, source: act.src, resource, tabId });
      return;
    }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src, { pageUrl: resource });
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: act.src }, { dataUrl, page, resource, incognito: act.incognito });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId });
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] preview action failed:', err);
  }
};

// ── Pin / unpin the right-clicked image or video on this site. No editor launch and
// no frame capture — a pin keys on the openable SOURCE URL (a video's media URL, an
// image/background's src), the same thing "open in new tab" uses. Toggles. ──
const togglePinFromMenu = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  const poster = tabId != null ? lastPosterByTab.get(tabId) : '';
  // The openable source + kind depend on which menu item fired: a background/overlay uses
  // the probe's recorded URL; a <video> frame uses the media URL (info.srcUrl) or poster
  // fallback; a plain image uses info.srcUrl.
  const byItem = {
    [MENU.bgPin]: { source: (rec && rec.url) || info.srcUrl || '', kind: 'background' },
    [MENU.framePin]: { source: info.srcUrl || poster || '', kind: 'video' },
  };
  const { source, kind } = byItem[info.menuItemId]
    || { source: info.srcUrl || (rec && rec.url) || '', kind: 'image' };
  if (!source) return;
  const resource = tab?.url || '';
  const site = siteOf(resource);
  try {
    const pinned = isPinnedIn(await loadPins(), site, source);
    await setPinned({ source, site, resource, name: filenameFromUrl(source), kind, pinned: !pinned });
  } catch (err) {
    console.error('[stencil] pin action failed:', err);
  }
};

// ── "Open in desktop app" (context menu) → hand the image over via the stencil:// URL
// scheme with the bytes inline. chrome.tabs.create fires the OS external-protocol prompt
// from the SW; oversized inline payloads are refused (the OS launch can't carry them). ──
const openInDesktopFromMenu = async (src, pageUrl = '') => {
  const { desktopScheme } = await getSettings();
  if (!desktopScheme || !src) return;
  try {
    const dataUrl = await fetchAsDataUrl(src, { pageUrl });
    const url = buildStencilSchemeUrl({ scheme: desktopScheme, src: dataUrl });
    if (url.length > INLINE_MAX_CHARS) { console.warn('[stencil] image too large for an inline desktop hand-off'); return; }
    chrome.tabs.create({ url });
  } catch (err) {
    console.error('[stencil] open-in-desktop failed:', err);
  }
};

// ── Default: an <img> / background / <video>-frame click → resolve the image bytes
// (capturing a video frame as needed) and open / crop them in the editor. ──
const openImageOrFrame = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  let src = resolveSrc(info, rec);

  // Video path: capture in-page at click time → re-fetch bytes → screenshot-crop. A media
  // URL is NEVER used as the image (.mp4 won't decode); sourceUrl is captured for
  // provenance before `src` is overwritten below with a frame data URL.
  const sourceUrl = src || '';
  const resource = tab?.url || '';

  const vinfo = tabId != null ? lastVideoByTab.get(tabId) : null;
  const poster = (tabId != null ? lastPosterByTab.get(tabId) : '') || '';
  const isVideo = info.mediaType === 'video' || !!vinfo || !!(rec && rec.video);
  if (isVideo && !(rec && rec.video && rec.url)) {
    const frameId = vinfo ? vinfo.frameId : info.frameId;
    const probe = await captureVideoFrameInTab(tabId, frameId, vinfo && vinfo.point);
    let frame = probe && probe.frame ? probe.frame : null;
    if (!frame && probe && probe.src) frame = await captureVideoFrameViaFetch(tabId, frameId, probe.src, probe.t, resource);
    if (frame) {
      src = frame;
    } else if (poster && (!vinfo || vinfo.posterShown)) {
      // No real frame AND the video is on its poster (not played) → use the poster.
      // The poster URL is cleaner than a screenshot crop, and avoids a black frame 0.
      src = poster;
    } else if (vinfo && vinfo.rect) {
      // Playing but cross-origin / unreadable → screenshot-crop the on-screen frame.
      src = await captureFrameFromScreenshot(tab.windowId, vinfo.rect, vinfo.dpr);
    } else {
      src = poster || null;  // last resort: any poster we have
    }
  }

  // Feed the resolved src as srcUrl so the raw info.srcUrl (a <video>'s media file)
  // can't slip back in over the frame src picked above.
  const act = resolveContextAction({ ...info, srcUrl: src }, src);
  // Nothing resolvable under the cursor. The menu entry shouldn't have been reachable at
  // all (the dynamic group is revealed only on a probe hit, and the static group needs a
  // native image/video context), so this is the "the page changed under us" case.
  if (!act) {
    console.warn('[stencil] context-menu click found nothing to act on (the target moved or the frame could not be read)');
    return;
  }
  try {
    if (act.action === 'crop') {
      await launchCrop({ src: act.src, source: sourceUrl, resource, tabId: tab?.id });   // small in-page modal
      return;
    }
    if (act.action === 'desktop') { await openInDesktopFromMenu(act.src, resource); return; }
    const { page } = await getSettings();
    const dataUrl = await fetchAsDataUrl(act.src, { pageUrl: resource });
    // `act.open` ('resume') only set by the Resume item; undefined drops out of the
    // JSON payload so a plain open imports fresh, as before.
    const payload = buildHandoff({ name: filenameFromUrl(act.src), source: sourceUrl }, { dataUrl, page, resource, incognito: act.incognito, open: act.open });
    if (act.action === 'open-modal') await launchEditorModal({ ...payload, tabId: tab?.id });   // in-page editor modal
    else await openEditorTab(payload);
  } catch (err) {
    console.error('[stencil] context-menu action failed:', err);
  }
};

// Route a click to its handler: the two toolbar action items, then the preview-*
// submenu, then the pin items, then the default image / video-frame path.
const resolveClickHandler = (info) => {
  if (info.menuItemId === MENU.actionOpen || info.menuItemId === MENU.actionOpenIncognito) return openFreshEditor;
  if (typeof info.menuItemId === 'string' && info.menuItemId.startsWith('stencil-preview-')) return actOnPreview;
  if (PIN_ITEMS.includes(info.menuItemId)) return togglePinFromMenu;
  return openImageOrFrame;
};

chrome.contextMenus.onClicked.addListener((info, tab) => resolveClickHandler(info)(info, tab, tab?.id));
