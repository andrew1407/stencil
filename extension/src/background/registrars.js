// Injection into already-open tabs, plus the SCRIPT_SETS registration pass.
import { getSettings, originPattern } from '../lib/stencil.js';

// Declared/registered content scripts only inject into pages loaded AFTER
// install/update/registration — this covers the tabs already open.
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
export const injectProbeIntoOpenTabs = () =>
  injectIntoOpenTabs(['http://*/*', 'https://*/*'], [{ file: 'src/content/ctxTarget.js' }], { allFrames: true });

// Three sets, registered the same way: the editor bridge, the opt-in page API
// (window.stencil), and the editor page API (stencil.extension). `matches` = null unregisters.
const BRIDGE_ID = 'stencil-editor-bridge';
const BRIDGE_FILE = 'src/content/editorBridge.js';
const HTTP_PATTERNS = ['http://*/*', 'https://*/*'];

const SCRIPT_SETS = [
  {
    key: 'bridge',
    label: 'editor bridge',
    scripts: [{ id: BRIDGE_ID, file: BRIDGE_FILE, runAt: 'document_start' }],
    matches: ({ pattern }) => (pattern ? [pattern] : null),
  },
  {
    key: 'pageApi',
    label: 'page API',
    scripts: [
      { id: 'stencil-page-bridge', file: 'src/content/pageApiBridge.js', world: 'ISOLATED', runAt: 'document_start' },
      { id: 'stencil-page-main', file: 'src/content/pageApiMain.js', world: 'MAIN', runAt: 'document_idle' },
    ],
    // Registered for every page; only http(s) tabs can be injected into after the fact.
    openTabs: HTTP_PATTERNS,
    matches: ({ settings }) => (settings.exposeWindowStencil ? ['<all_urls>'] : null),
  },
  {
    key: 'editorApi',
    label: 'editor page API',
    scripts: [{ id: 'stencil-editor-api-main', file: 'src/content/editorApiMain.js', world: 'MAIN', runAt: 'document_idle' }],
    matches: ({ settings, pattern }) => (settings.editorPageApi && pattern ? [pattern] : null),
  },
];

// Registration is unregister-then-register and several triggers fire it concurrently;
// interleaved, a second register throws `Duplicate script ID`. Serialised, one at a time.
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

// ONE settings read, then every named set (un)registered and injected into open tabs — the
// read stays INSIDE the critical section, so a queued pass registers the CURRENT pattern.
export const setUpScripts = ({ keys, inject = true } = {}) => serializeRegistration(async () => {
  const settings = await getSettings();
  const ctx = { settings, pattern: originPattern(settings.editorUrl) };
  const sets = keys ? SCRIPT_SETS.filter((s) => keys.includes(s.key)) : SCRIPT_SETS;
  await Promise.all(sets.map(async (set) => {
    const matches = set.matches(ctx);
    if (!matches) {
      try { await chrome.scripting.unregisterContentScripts({ ids: set.scripts.map((s) => s.id) }); } catch { /* not registered */ }
      return;
    }
    await Promise.all(set.scripts.map((s) => replaceContentScript(s.id, {
      id: s.id, js: [s.file], matches, runAt: s.runAt, allFrames: false, ...(s.world ? { world: s.world } : {}),
    }, set.label)));
    if (inject === true || inject.includes(set.key)) await injectIntoOpenTabs(set.openTabs || matches, set.scripts);
  }));
});
