// Editor bridge, injected only into the configured editor origin (registered from the editorUrl
// setting). Same-origin, so it reads the editor's project registry from localStorage for the SW
// (pruneLedger). READ-ONLY on the editor: never writes the registry or a project — every editor
// mutation goes through the page's own app methods. Also editor mode's relay in both
// directions. Classic script — no import; the guard stops a second inject.
(() => {
  if (window.__stencilEditorBridge) return;
  window.__stencilEditorBridge = true;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = {
    REGISTRY: 'stencil-registry', PAGE_PIN: 'stencil-page-pin', EDITOR_SWITCH: 'stencil-editor-switch',
    EDITOR_LIST: 'stencil-editor-list', EDITOR_STATE: 'stencil-editor-state', EDITOR_IMPORT: 'stencil-editor-import',
    EDITOR_SWITCH_PROJECT: 'stencil-editor-switch-project', EDITOR_CROP: 'stencil-editor-crop',
    EDITOR_FOCUS_TAB: 'stencil-editor-focus-tab',
    SOURCE_TABS: 'stencil-source-tabs', SCAN_TAB: 'stencil-scan-tab',
    PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop',
  };
  const SRC = { EXT_REQ: 'stencil-ext-req', EXT_RES: 'stencil-ext-res', EXT_API: 'stencil-ext-api', EXT_API_RES: 'stencil-ext-api-res' };
  // Must match ProjectsStore.REGISTRY_KEY in browser/js/core/project/store/projectsStore.js.
  const REGISTRY_KEY = 'stencil_projects_v1';

  const publishRegistry = () => {
    let projects = [];
    try {
      const raw = localStorage.getItem(REGISTRY_KEY);
      const arr = raw ? JSON.parse(raw) : [];
      if (Array.isArray(arr)) {
        // Only the fields reconciliation needs; never the image payload.
        projects = arr
          .filter(m => m && m.id != null)
          .map(m => ({ source: m.source || '', name: m.name || '' }));
      }
    } catch {
      // Unreadable / not JSON → report nothing rather than prune against a bad read.
      return;
    }
    try {
      chrome.runtime.sendMessage({ type: MSG.REGISTRY, projects });
    } catch {
      /* worker asleep / extension context invalidated → a later event retries */
    }
  };

  // `stencil:registry-changed` covers the editing tab; the `storage` event covers OTHER editor
  // tabs (it fires in every same-origin document except the one that wrote).
  publishRegistry();
  window.addEventListener('stencil:registry-changed', publishRegistry);
  window.addEventListener('storage', (e) => {
    if (!e || e.key == null || e.key === REGISTRY_KEY) publishRegistry();
  });

  // Editor → extension UNPIN relay, as a PAGE_PIN with pin:false. `resource` is required so the
  // SW computes the (site, source) pin key — the editor passes the OLD image's.
  window.addEventListener('message', (e) => {
    if (e.source !== window) return;                       // same-document page → bridge only
    const m = e && e.data;
    if (!m || m.source !== 'stencil-editor-bridge' || m.type !== 'unpin') return;
    try {
      chrome.runtime.sendMessage({
        type: MSG.PAGE_PIN, pin: false,
        source: m.pinSource || '', resource: m.resource || '',
        name: m.name || '', kind: m.kind || 'image',
      });
    } catch {
      /* worker asleep / extension context invalidated — a stale pin is harmless */
    }
  });

  // Extension → editor RESUME relay: a DOM CustomEvent reaches the editor's own listener
  // (switchToProject) — never writing the registry ourselves.
  chrome.runtime.onMessage?.addListener((msg) => {
    if (!msg || msg.type !== MSG.EDITOR_SWITCH) return;
    try {
      window.dispatchEvent(new CustomEvent('stencil:switch-to-source', {
        detail: { source: msg.source || '', name: msg.name || '' },
      }));
    } catch {
      /* no DOM (unlikely in a content script) — best-effort */
    }
  });

  // Request/response: an id-tagged EXT_REQ to js/core/extensionBridge.js, answered EXT_RES.
  // Capped — an editor build without that module never replies at all.
  const PAGE_TIMEOUT_MS = 1500;
  const PAGE_SILENT = 'the editor page did not answer';
  const pending = new Map();   // request id → settle fn; deleted on the first answer, so a late EXT_RES is dropped
  let seq = 0;

  // Always RESOLVES ({ok…}): a rejection would strand the port with no reply.
  const pageRequest = (request, payload) => new Promise((resolve) => {
    const id = `ext-${++seq}-${Date.now()}`;
    const settle = (reply) => { clearTimeout(timer); pending.delete(id); resolve(reply); };
    const timer = setTimeout(() => settle({ ok: false, error: PAGE_SILENT }), PAGE_TIMEOUT_MS);
    pending.set(id, settle);
    try {
      window.postMessage({ source: SRC.EXT_REQ, id, request, payload: payload || {} }, '*');
    } catch {
      settle({ ok: false, error: PAGE_SILENT });   // no page to talk to
    }
  });

  window.addEventListener('message', (e) => {
    if (e.source !== window) return;                       // same-document page → bridge only
    const d = e && e.data;
    if (!d || d.source !== SRC.EXT_RES || d.id == null) return;
    const settle = pending.get(d.id);
    if (!settle) return;                                   // not ours / already timed out
    settle(d.ok ? { ok: true, result: d.result || {} } : { ok: false, error: d.error || PAGE_SILENT });
  });

  // `import` and `switch` both answer with the project they landed on; the page owns the ids.
  const projectReply = (r) => (r.ok ? { ok: true, projectId: r.result.projectId || '', projectName: r.result.projectName || '' } : r);
  // Import/switch reach a content script only on the SW→bridge hop: `payload`, no `tabId`.
  const pageHandlers = {
    // thumbMax rides along only when asked for (the hover magnifier).
    [MSG.EDITOR_STATE]: (msg) => pageRequest('state', msg.thumbMax
      ? { thumbnail: msg.thumbnail !== false, thumbMax: msg.thumbMax }
      : { thumbnail: msg.thumbnail !== false })
      .then((r) => (r.ok ? { ok: true, state: r.result } : r)),
    [MSG.EDITOR_IMPORT]: (msg) => pageRequest('import', { handoff: msg.payload, mode: msg.mode }).then(projectReply),
    [MSG.EDITOR_SWITCH_PROJECT]: (msg) => pageRequest('switch', { projectId: msg.projectId || '' }).then(projectReply),
    // SW-only (not RELAYABLE below): opens this editor's own crop dialog.
    [MSG.EDITOR_CROP]: () => pageRequest('crop', {}),
  };
  chrome.runtime.onMessage?.addListener((msg, _sender, sendResponse) => {
    const handler = msg && pageHandlers[msg.type];
    if (!handler) return;
    handler(msg).then(sendResponse, (err) => sendResponse({ ok: false, error: (err && err.message) || PAGE_SILENT }));
    return true;   // answering asynchronously — keep the port open
  });

  // The relayable types are a WHITELIST: `e.source === window` only proves same-document, and
  // some types are privileged. The relay also rides on the `editorPageApi` toggle.
  let pageApiEnabled = false;
  const readPageApiSetting = () => {
    try {
      chrome.storage.sync.get({ editorPageApi: true }, (s) => {
        pageApiEnabled = !chrome.runtime.lastError && s.editorPageApi !== false;
      });
    } catch {
      pageApiEnabled = false;   // no storage access → assume off, the safe direction
    }
  };
  readPageApiSetting();
  try {
    chrome.storage.onChanged?.addListener((changes, area) => {
      if (area === 'sync' && changes.editorPageApi) readPageApiSetting();
    });
  } catch {
    /* no storage events — the initial read stands for this document's lifetime */
  }

  const RELAYABLE = new Set([
    MSG.EDITOR_LIST, MSG.EDITOR_STATE, MSG.EDITOR_IMPORT, MSG.EDITOR_SWITCH_PROJECT,
    MSG.EDITOR_FOCUS_TAB, MSG.SOURCE_TABS, MSG.SCAN_TAB, MSG.PAGE_OPEN, MSG.PAGE_CROP,
  ]);
  const GONE = 'extension is not available';
  const PAGE_API_OFF = 'the editor page API is turned off in the extension options';
  // No id = fire-and-forget (PAGE_OPEN / PAGE_CROP never respond): relay and stay quiet.
  const answerApi = (id, reply) => { if (id != null) window.postMessage({ source: SRC.EXT_API_RES, id, ...reply }, '*'); };

  window.addEventListener('message', (e) => {
    if (e.source !== window) return;
    const d = e && e.data;
    if (!d || d.source !== SRC.EXT_API || !d.message) return;
    if (!pageApiEnabled) { answerApi(d.id, { ok: false, error: PAGE_API_OFF }); return; }
    const m = d.message;
    if (!RELAYABLE.has(m.type)) { answerApi(d.id, { ok: false, error: 'unknown request' }); return; }
    // EDITOR_STATE with no tabId is answered by our own round-trip (`stencil.extension.current`).
    if (m.type === MSG.EDITOR_STATE && m.tabId == null) {
      pageRequest('state', { thumbnail: m.thumbnail !== false })
        .then((r) => answerApi(d.id, r.ok ? { ok: true, result: { ok: true, state: r.result } } : { ok: false, error: r.error }));
      return;
    }
    let out;
    try {
      out = chrome.runtime.sendMessage(m);
    } catch {
      answerApi(d.id, { ok: false, error: GONE });          // extension context invalidated
      return;
    }
    if (d.id == null) { Promise.resolve(out).catch(() => { /* SW asleep — nothing to report */ }); return; }
    Promise.resolve(out).then(
      (res) => {
        // No receiver / a worker that died mid-call resolves undefined (or rejects below).
        if (!res) { answerApi(d.id, { ok: false, error: GONE }); return; }
        // `result` carries the response whole so a refusal keeps its context (needsChoice + state).
        answerApi(d.id, res.ok === false ? { ok: false, error: res.error || GONE, result: res } : { ok: true, result: res });
      },
      () => answerApi(d.id, { ok: false, error: GONE }),
    );
  });
})();
